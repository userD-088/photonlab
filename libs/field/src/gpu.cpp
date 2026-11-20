// 2D-FDTD auf der Grafikkarte über OpenCL (docs/performance.md). Die Kerne rechnen dieselben Formeln in
// derselben Reihenfolge wie fdtd.cpp, in doppelter Genauigkeit und ohne zusammengezogene Multiplikation
// (FP_CONTRACT OFF); Quellen, Regeneratoren und Monitore laufen mit, damit nur selten Daten zurückkommen.
// Die OpenCL-Bibliothek des Treibers wird zur Laufzeit geladen: ohne sie bleibt alles auf dem Prozessor.
#include "photonlab/field/gpu.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"

#ifdef PHOTONLAB_HAVE_OPENCL
#include <CL/cl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX  // MinGW setzt es schon
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace photonlab::field {

namespace gpu {
namespace {
std::atomic<bool> g_enabled{false};
}
void set_enabled(bool enabled) { g_enabled = enabled; }
bool enabled() { return g_enabled && device().available; }
}  // namespace gpu

#ifndef PHOTONLAB_HAVE_OPENCL

// Ohne OpenCL gebaut: alles auf dem Prozessor
const gpu::DeviceInfo& gpu::device() {
  static const DeviceInfo info{false, "", "", false, "ohne OpenCL gebaut (PHOTONLAB_OPENCL=OFF)"};
  return info;
}
struct Fdtd::GpuState {};
void Fdtd::GpuStateDeleter::operator()(GpuState* state) const { delete state; }
bool Fdtd::use_gpu(bool) { return false; }
void Fdtd::gpu_step() {}
void Fdtd::gpu_sync() const {}

#else

namespace {

// --- OpenCL zur Laufzeit laden
// -------------------------------------------------------------------------------

struct Api {
#define PHOTONLAB_CL(name) decltype(&::name) name = nullptr;
  PHOTONLAB_CL(clGetPlatformIDs)
  PHOTONLAB_CL(clGetPlatformInfo)
  PHOTONLAB_CL(clGetDeviceIDs)
  PHOTONLAB_CL(clGetDeviceInfo)
  PHOTONLAB_CL(clCreateContext)
  PHOTONLAB_CL(clCreateCommandQueue)
  PHOTONLAB_CL(clCreateProgramWithSource)
  PHOTONLAB_CL(clBuildProgram)
  PHOTONLAB_CL(clGetProgramBuildInfo)
  PHOTONLAB_CL(clCreateKernel)
  PHOTONLAB_CL(clSetKernelArg)
  PHOTONLAB_CL(clCreateBuffer)
  PHOTONLAB_CL(clEnqueueWriteBuffer)
  PHOTONLAB_CL(clEnqueueReadBuffer)
  PHOTONLAB_CL(clEnqueueNDRangeKernel)
  PHOTONLAB_CL(clFinish)
  PHOTONLAB_CL(clFlush)
  PHOTONLAB_CL(clReleaseMemObject)
  PHOTONLAB_CL(clReleaseKernel)
  PHOTONLAB_CL(clReleaseCommandQueue)
#undef PHOTONLAB_CL
  bool loaded = false;
};

const Api& api() {
  static const Api instance = [] {
    Api a;
#ifdef _WIN32
    HMODULE lib = LoadLibraryA("OpenCL.dll");
    auto symbol = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(lib, name)); };
#else
    void* lib = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
    auto symbol = [&](const char* name) { return dlsym(lib, name); };
#endif
    if (!lib) return a;
    bool ok = true;
#define PHOTONLAB_CL(name)                   \
  {                                          \
    void* p = symbol(#name);                 \
    ok = ok && p != nullptr;                 \
    std::memcpy(&a.name, &p, sizeof a.name); \
  }
    PHOTONLAB_CL(clGetPlatformIDs)
    PHOTONLAB_CL(clGetPlatformInfo)
    PHOTONLAB_CL(clGetDeviceIDs)
    PHOTONLAB_CL(clGetDeviceInfo)
    PHOTONLAB_CL(clCreateContext)
    PHOTONLAB_CL(clCreateCommandQueue)
    PHOTONLAB_CL(clCreateProgramWithSource)
    PHOTONLAB_CL(clBuildProgram)
    PHOTONLAB_CL(clGetProgramBuildInfo)
    PHOTONLAB_CL(clCreateKernel)
    PHOTONLAB_CL(clSetKernelArg)
    PHOTONLAB_CL(clCreateBuffer)
    PHOTONLAB_CL(clEnqueueWriteBuffer)
    PHOTONLAB_CL(clEnqueueReadBuffer)
    PHOTONLAB_CL(clEnqueueNDRangeKernel)
    PHOTONLAB_CL(clFinish)
    PHOTONLAB_CL(clFlush)
    PHOTONLAB_CL(clReleaseMemObject)
    PHOTONLAB_CL(clReleaseKernel)
    PHOTONLAB_CL(clReleaseCommandQueue)
#undef PHOTONLAB_CL
    a.loaded = ok;
    return a;
  }();
  return instance;
}

// --- Kerne -------------------------------------------------------------------------------------------------

// Gleiche Formeln und Reihenfolge wie Fdtd::step_ez/step_hz/record. Hilfsfelder psi werden nur in Zeilen bzw.
// Spalten mit Randschicht (c != 0) angefasst; außerhalb bleiben sie 0, und d + 0 = d ergibt dieselben Bits.
const char* kernel_source = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#pragma OPENCL FP_CONTRACT OFF

// Ez: H von n-1/2 nach n+1/2
__kernel void ez_h(__global const double* e, __global double* hx, __global double* hy,
                   __global double* psi_gx, __global double* psi_gy,
                   __global const double* bxh, __global const double* cxh,
                   __global const double* byh, __global const double* cyh,
                   int nx, int ny, int px, int py, double inv, double cm) {
  const int i = get_global_id(0), j = get_global_id(1);
  if (i >= nx || j >= ny) return;
  const size_t k = (size_t)i + (size_t)nx * (size_t)j;
  const double e_next_row = j + 1 < ny ? e[k + nx] : (py ? e[i] : 0.0);
  const double dy = (e_next_row - e[k]) * inv;
  if (cyh[j] != 0.0) {
    psi_gx[k] = byh[j] * psi_gx[k] + cyh[j] * dy;
    hx[k] -= cm * (dy + psi_gx[k]);
  } else {
    hx[k] -= cm * dy;
  }
  const double e_right = i + 1 < nx ? e[k + 1] : (px ? e[k - (nx - 1)] : 0.0);
  const double dx = (e_right - e[k]) * inv;
  if (cxh[i] != 0.0 || i == nx - 1) {
    psi_gy[k] = bxh[i] * psi_gy[k] + cxh[i] * dx;
    hy[k] += cm * (dx + psi_gy[k]);
  } else {
    hy[k] += cm * dx;
  }
}

// Ez: E von n nach n+1 (e_new aus e_old)
__kernel void ez_e(__global const double* e_old, __global double* e_new,
                   __global const double* hx, __global const double* hy,
                   __global double* psi_fx, __global double* psi_fy,
                   __global const double* bxe, __global const double* cxe,
                   __global const double* bye, __global const double* cye, __global const double* cf,
                   int nx, int ny, int px, int py, double inv) {
  const int i = get_global_id(0), j = get_global_id(1);
  if (i >= nx || j >= ny) return;
  const size_t k = (size_t)i + (size_t)nx * (size_t)j;
  if (j == 0 && !py) { e_new[k] = e_old[k]; return; }
  const double hx_prev = j > 0 ? hx[k - nx] : hx[(size_t)i + (size_t)nx * (size_t)(ny - 1)];
  double dhy;
  if (i == 0) {
    if (!px) { e_new[k] = e_old[k]; return; }
    dhy = (hy[k] - hy[k + nx - 1]) * inv;
  } else {
    dhy = (hy[k] - hy[k - 1]) * inv;
  }
  const double dhx = (hx[k] - hx_prev) * inv;
  double qx = 0.0, qy = 0.0;
  if (cxe[i] != 0.0 || (i == 0 && px)) { psi_fx[k] = bxe[i] * psi_fx[k] + cxe[i] * dhy; qx = psi_fx[k]; }
  if (cye[j] != 0.0) { psi_fy[k] = bye[j] * psi_fy[k] + cye[j] * dhx; qy = psi_fy[k]; }
  e_new[k] = e_old[k] + cf[k] * (dhy + qx - dhx - qy);
}

// Hz: E von n-1/2 nach n+1/2 aus Hz
__kernel void hz_e(__global const double* h, __global double* ex, __global double* ey,
                   __global double* psi_gx, __global double* psi_gy,
                   __global const double* bxe, __global const double* cxe,
                   __global const double* bye, __global const double* cye,
                   __global const double* cgx, __global const double* cgy,
                   int nx, int ny, int px, int py, double inv) {
  const int i = get_global_id(0), j = get_global_id(1);
  if (i >= nx || j >= ny) return;
  const size_t k = (size_t)i + (size_t)nx * (size_t)j;
  if (j > 0 || py) {
    const double h_prev = j > 0 ? h[k - nx] : h[(size_t)i + (size_t)nx * (size_t)(ny - 1)];
    const double d = (h[k] - h_prev) * inv;
    if (cye[j] != 0.0) {
      psi_gx[k] = bye[j] * psi_gx[k] + cye[j] * d;
      ex[k] += cgx[k] * (d + psi_gx[k]);
    } else {
      ex[k] += cgx[k] * d;
    }
  }
  if (i == 0) {
    if (px) {
      const double d = (h[k] - h[k + nx - 1]) * inv;
      psi_gy[k] = bxe[0] * psi_gy[k] + cxe[0] * d;
      ey[k] -= cgy[k] * (d + psi_gy[k]);
    }
  } else {
    const double d = (h[k] - h[k - 1]) * inv;
    if (cxe[i] != 0.0) {
      psi_gy[k] = bxe[i] * psi_gy[k] + cxe[i] * d;
      ey[k] -= cgy[k] * (d + psi_gy[k]);
    } else {
      ey[k] -= cgy[k] * d;
    }
  }
}

// Hz: H von n nach n+1 (h_new aus h_old)
__kernel void hz_h(__global const double* h_old, __global double* h_new,
                   __global const double* ex, __global const double* ey,
                   __global double* psi_fx, __global double* psi_fy,
                   __global const double* bxh, __global const double* cxh,
                   __global const double* byh, __global const double* cyh,
                   int nx, int ny, int px, int py, double inv, double cm) {
  const int i = get_global_id(0), j = get_global_id(1);
  if (i >= nx || j >= ny) return;
  const size_t k = (size_t)i + (size_t)nx * (size_t)j;
  const double ey_next = i + 1 < nx ? ey[k + 1] : (px ? ey[k - (nx - 1)] : 0.0);
  const double ex_next = j + 1 < ny ? ex[k + nx] : (py ? ex[i] : 0.0);
  const double dex = (ex_next - ex[k]) * inv;
  const double dey = (ey_next - ey[k]) * inv;
  double qy = 0.0, qx = 0.0;
  if (cyh[j] != 0.0) { psi_fy[k] = byh[j] * psi_fy[k] + cyh[j] * dex; qy = psi_fy[k]; }
  if (cxh[i] != 0.0 || i == nx - 1) { psi_fx[k] = bxh[i] * psi_fx[k] + cxh[i] * dey; qx = psi_fx[k]; }
  h_new[k] = h_old[k] + cm * (dex + qy - dey - qx);
}

// Quellen: je Feldzelle alle Beiträge in der Reihenfolge des Prozessors (Quelle, Schicht, Liste)
__kernel void add_sources(__global double* f, int count, __global const int* cells,
                          __global const int* offsets, __global const int* slots,
                          __global const double* weights, __global const double* signals, int row) {
  const int u = get_global_id(0);
  if (u >= count) return;
  double v = f[cells[u]];
  for (int t = offsets[u]; t < offsets[u + 1]; ++t) {
    const double s = signals[row + slots[t]];
    if (s != 0.0) v += weights[t] * s;
  }
  f[cells[u]] = v;
}

// Monitore: je Monitor ein Arbeitselement; Art 0 Punkt, 1 Mode, 2 Fluss senkrecht, 3 Fluss waagerecht
__kernel void record(__global const double* f, __global const double* fp, __global const double* gx,
                     __global const double* gy, int count, __global const int* kind,
                     __global const int* p0, __global const int* p1, __global const int* p2,
                     __global const int* mode_cells, __global const double* mode_weights,
                     __global double* history, int row, int nx, int ez, double dx) {
  const int m = get_global_id(0);
  if (m >= count) return;
  double value = 0.0;
  if (kind[m] == 0) {
    value = f[p0[m]];
  } else if (kind[m] == 1) {
    for (int t = p0[m]; t < p1[m]; ++t) value += f[mode_cells[t]] * mode_weights[t];
  } else {
    double sum = 0.0;
    const int fixed = p0[m];
    for (int r = p1[m]; r <= p2[m]; ++r) {
      if (kind[m] == 2) {
        const size_t k = (size_t)fixed + (size_t)nx * (size_t)r;
        if (ez) {
          const double e = 0.5 * (f[k] + fp[k]);
          sum -= e * 0.5 * (gy[k - 1] + gy[k]);
        } else {
          const double h = 0.25 * (f[k - 1] + f[k] + fp[k - 1] + fp[k]);
          sum += gy[k] * h;
        }
      } else {
        const size_t k = (size_t)r + (size_t)nx * (size_t)fixed;
        if (ez) {
          const double e = 0.5 * (f[k] + fp[k]);
          sum += e * 0.5 * (gx[k - nx] + gx[k]);
        } else {
          const double h = 0.25 * (f[k - nx] + f[k] + fp[k - nx] + fp[k]);
          sum -= gx[k] * h;
        }
      }
    }
    value = sum * dx;
  }
  history[row + m] = value;
}
)CLC";

// --- Gerät, Kontext und Programm (einmal je Prozess) ------------------------------------------------------

struct Shared {
  gpu::DeviceInfo info;
  cl_context context = nullptr;
  cl_device_id device = nullptr;
  cl_program program = nullptr;
};

std::string device_string(const Api& cl, cl_device_id d, cl_device_info what) {
  std::size_t size = 0;
  cl.clGetDeviceInfo(d, what, 0, nullptr, &size);
  std::string text(size, '\0');
  cl.clGetDeviceInfo(d, what, size, text.data(), nullptr);
  while (!text.empty() && text.back() == '\0') text.pop_back();
  return text;
}

Shared& shared() {
  static Shared s = [] {
    Shared result;
    const Api& cl = api();
    const char* env = std::getenv("PHOTONLAB_OPENCL_DEVICE");
    const std::string mode = env ? env : "gpu";
    if (mode == "off") {
      result.info.reason = "abgeschaltet (PHOTONLAB_OPENCL_DEVICE=off)";
      return result;
    }
    if (!cl.loaded) {
      result.info.reason = "keine OpenCL-Bibliothek gefunden (Grafiktreiber mit OpenCL installieren)";
      return result;
    }
    cl_uint platform_count = 0;
    if (cl.clGetPlatformIDs(0, nullptr, &platform_count) != CL_SUCCESS || platform_count == 0) {
      result.info.reason = "keine OpenCL-Plattform gefunden";
      return result;
    }
    std::vector<cl_platform_id> platforms(platform_count);
    cl.clGetPlatformIDs(platform_count, platforms.data(), nullptr);
    // Grafikkarten zuerst; Prozessor-Geräte nur mit PHOTONLAB_OPENCL_DEVICE=any
    for (const cl_device_type type :
         {static_cast<cl_device_type>(CL_DEVICE_TYPE_GPU), static_cast<cl_device_type>(CL_DEVICE_TYPE_ALL)}) {
      if (type == CL_DEVICE_TYPE_ALL && mode != "any") break;
      for (cl_platform_id platform : platforms) {
        cl_uint count = 0;
        if (cl.clGetDeviceIDs(platform, type, 0, nullptr, &count) != CL_SUCCESS || count == 0) continue;
        std::vector<cl_device_id> devices(count);
        cl.clGetDeviceIDs(platform, type, count, devices.data(), nullptr);
        for (cl_device_id d : devices) {
          cl_device_fp_config fp64 = 0;
          cl.clGetDeviceInfo(d, CL_DEVICE_DOUBLE_FP_CONFIG, sizeof fp64, &fp64, nullptr);
          const std::string name = device_string(cl, d, CL_DEVICE_NAME);
          if (fp64 == 0) {
            result.info.reason = name + ": keine doppelte Genauigkeit";
            continue;
          }
          cl_int error = CL_SUCCESS;
          cl_context context = cl.clCreateContext(nullptr, 1, &d, nullptr, nullptr, &error);
          if (error != CL_SUCCESS) continue;
          cl_program program = cl.clCreateProgramWithSource(context, 1, &kernel_source, nullptr, &error);
          if (error == CL_SUCCESS) error = cl.clBuildProgram(program, 1, &d, "", nullptr, nullptr);
          if (error != CL_SUCCESS) {
            std::size_t size = 0;
            cl.clGetProgramBuildInfo(program, d, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size);
            std::string log(size, '\0');
            cl.clGetProgramBuildInfo(program, d, CL_PROGRAM_BUILD_LOG, size, log.data(), nullptr);
            result.info.reason = name + ": Kerne lassen sich nicht übersetzen: " + log.substr(0, 300);
            continue;
          }
          cl_device_type actual = 0;
          cl.clGetDeviceInfo(d, CL_DEVICE_TYPE, sizeof actual, &actual, nullptr);
          std::size_t size = 0;
          cl.clGetPlatformInfo(platform, CL_PLATFORM_NAME, 0, nullptr, &size);
          std::string platform_name(size, '\0');
          cl.clGetPlatformInfo(platform, CL_PLATFORM_NAME, size, platform_name.data(), nullptr);
          while (!platform_name.empty() && platform_name.back() == '\0') platform_name.pop_back();
          result.context = context, result.device = d, result.program = program;
          result.info = {true, name, platform_name, (actual & CL_DEVICE_TYPE_GPU) != 0, ""};
          return result;
        }
      }
    }
    if (result.info.reason.empty())
      result.info.reason =
          mode == "any" ? "kein OpenCL-Gerät gefunden" : "keine Grafikkarte mit OpenCL gefunden";
    return result;
  }();
  return s;
}

}  // namespace

const gpu::DeviceInfo& gpu::device() { return shared().info; }

// --- Zustand eines Fdtd-Objekts auf der Grafikkarte
// ---------------------------------------------------------

struct Fdtd::GpuState {
  cl_command_queue queue = nullptr;
  cl_kernel k_field1 = nullptr, k_field2 = nullptr, k_sources = nullptr, k_record = nullptr;
  std::vector<cl_mem> buffers;  // zum Freigeben
  cl_mem cur = nullptr, other = nullptr, gx = nullptr, gy = nullptr;
  cl_mem psi_fx = nullptr, psi_fy = nullptr, psi_gx = nullptr, psi_gy = nullptr;
  cl_mem coef = nullptr, coef2 = nullptr;  // cf (Ez) bzw. cgx, cgy (Hz)
  cl_mem bxe = nullptr, cxe = nullptr, bxh = nullptr, cxh = nullptr, bye = nullptr, cye = nullptr,
         byh = nullptr, cyh = nullptr;
  // Quellen (je Feldzelle eine Liste) und Monitore
  int source_count = 0;
  cl_mem s_cells = nullptr, s_offsets = nullptr, s_slots = nullptr, s_weights = nullptr, signals = nullptr;
  int monitor_count = 0;
  cl_mem m_kind = nullptr, m_p0 = nullptr, m_p1 = nullptr, m_p2 = nullptr, m_cells = nullptr,
         m_weights = nullptr, history = nullptr;
  std::size_t chunk = 128;    // Schritte je Block (Signale vorab, Messreihen danach)
  std::size_t position = 0;   // Schritte im laufenden Block
  std::size_t fetched = 0;    // davon schon zurückgeholte Messwerte
  std::size_t slots = 0;      // Signalplätze je Schritt (zwei je Quelle)
  std::size_t global_x = 0;   // auf ein Vielfaches von 64 aufgerundet
  bool fields_stale = false;  // Felder auf dem Prozessor veraltet
  std::vector<double> chunk_signals;
  std::vector<double> chunk_history;

  ~GpuState() {
    const Api& cl = api();
    for (cl_mem b : buffers) cl.clReleaseMemObject(b);
    for (cl_kernel k : {k_field1, k_field2, k_sources, k_record})
      if (k) cl.clReleaseKernel(k);
    if (queue) cl.clReleaseCommandQueue(queue);
  }
  cl_mem make(std::size_t bytes, const void* data) {
    const Api& cl = api();
    cl_int error = CL_SUCCESS;
    cl_mem b = cl.clCreateBuffer(shared().context, CL_MEM_READ_WRITE | (data ? CL_MEM_COPY_HOST_PTR : 0),
                                 std::max<std::size_t>(bytes, 8), const_cast<void*>(data), &error);
    if (error != CL_SUCCESS)
      throw FieldError("Grafikkarte: Speicher reicht nicht (" + std::to_string(error) + ")");
    buffers.push_back(b);
    return b;
  }
  template <class T>
  cl_mem upload(const std::vector<T>& v) {
    return make(v.size() * sizeof(T), v.empty() ? nullptr : v.data());
  }
  void read(cl_mem b, std::vector<double>& v) const {
    if (!v.empty())
      api().clEnqueueReadBuffer(queue, b, CL_TRUE, 0, v.size() * sizeof(double), v.data(), 0, nullptr,
                                nullptr);
  }
};

void Fdtd::GpuStateDeleter::operator()(GpuState* state) const { delete state; }

namespace {
template <class T>
void arg(cl_kernel k, cl_uint index, const T& value) {
  api().clSetKernelArg(k, index, sizeof(T), &value);
}
}  // namespace

bool Fdtd::use_gpu(bool on) {
  if (!on) {
    if (!gpu_) return false;
    gpu_sync();
    // Hilfsfelder der Randschichten mitnehmen, dann auf dem Prozessor weiterrechnen
    gpu_->read(gpu_->psi_fx, psi_fx_);
    gpu_->read(gpu_->psi_fy, psi_fy_);
    gpu_->read(gpu_->psi_gx, psi_gx_);
    gpu_->read(gpu_->psi_gy, psi_gy_);
    gpu_.reset();
    return false;
  }
  if (gpu_) return true;
  const Shared& sh = shared();
  if (!sh.info.available || !kerr_cells_.empty() || !kerr_x_cells_.empty() || !kerr_y_cells_.empty())
    return false;
  for (const auto& source : sources_)
    if (source.cells.empty() && source.cells2.empty()) return false;
  const Api& cl = api();
  auto state = std::unique_ptr<GpuState, GpuStateDeleter>(new GpuState);
  GpuState& g = *state;
  cl_int error = CL_SUCCESS;
  g.queue = cl.clCreateCommandQueue(sh.context, sh.device, 0, &error);
  if (error != CL_SUCCESS) return false;
  const bool ez = polarization_ == Polarization::ez;
  g.k_field1 = cl.clCreateKernel(sh.program, ez ? "ez_h" : "hz_e", &error);
  g.k_field2 = cl.clCreateKernel(sh.program, ez ? "ez_e" : "hz_h", &error);
  g.k_sources = cl.clCreateKernel(sh.program, "add_sources", &error);
  g.k_record = cl.clCreateKernel(sh.program, "record", &error);
  if (error != CL_SUCCESS) return false;
  try {
    g.cur = g.upload(f_), g.other = g.upload(f_prev_), g.gx = g.upload(gx_), g.gy = g.upload(gy_);
    g.psi_fx = g.upload(psi_fx_), g.psi_fy = g.upload(psi_fy_);
    g.psi_gx = g.upload(psi_gx_), g.psi_gy = g.upload(psi_gy_);
    if (ez) {
      g.coef = g.upload(cf_);
    } else {
      g.coef = g.upload(cgx_), g.coef2 = g.upload(cgy_);
    }
    g.bxe = g.upload(bxe_), g.cxe = g.upload(cxe_), g.bxh = g.upload(bxh_), g.cxh = g.upload(cxh_);
    g.bye = g.upload(bye_), g.cye = g.upload(cye_), g.byh = g.upload(byh_), g.cyh = g.upload(cyh_);

    // Quellen: Beiträge in der Reihenfolge des Prozessors, nach Zelle stabil sortiert
    struct Contribution {
      int cell, slot;
      double weight;
    };
    std::vector<Contribution> list;
    for (std::size_t q = 0; q < sources_.size(); ++q) {
      for (const auto& c : sources_[q].cells)
        list.push_back({static_cast<int>(c.index), static_cast<int>(2 * q), c.weight});
      for (const auto& c : sources_[q].cells2)
        list.push_back({static_cast<int>(c.index), static_cast<int>(2 * q + 1), c.weight});
    }
    std::stable_sort(list.begin(), list.end(),
                     [](const Contribution& a, const Contribution& b) { return a.cell < b.cell; });
    std::vector<int> cells, offsets{0}, slots;
    std::vector<double> weights;
    for (std::size_t t = 0; t < list.size(); ++t) {
      if (t == 0 || list[t].cell != list[t - 1].cell) {
        if (t > 0) offsets.push_back(static_cast<int>(t));
        cells.push_back(list[t].cell);
      }
      slots.push_back(list[t].slot);
      weights.push_back(list[t].weight);
    }
    offsets.push_back(static_cast<int>(list.size()));
    if (list.empty()) offsets = {0};
    g.source_count = static_cast<int>(cells.size());
    g.s_cells = g.upload(cells), g.s_offsets = g.upload(offsets), g.s_slots = g.upload(slots);
    g.s_weights = g.upload(weights);
    g.slots = 2 * sources_.size();

    // Monitore in der Reihenfolge von monitors_
    const std::size_t count = monitors_.size();
    std::vector<int> kind(count, 0), p0(count, 0), p1(count, 0), p2(count, 0), mode_cells;
    std::vector<double> mode_weights;
    for (const auto& probe : probes_)
      kind[probe.monitor] = 0, p0[probe.monitor] = static_cast<int>(probe.index);
    for (const auto& probe : mode_probes_) {
      kind[probe.monitor] = 1;
      p0[probe.monitor] = static_cast<int>(mode_cells.size());
      for (std::size_t t = 0; t < probe.cells.size(); ++t) {
        mode_cells.push_back(static_cast<int>(probe.cells[t]));
        mode_weights.push_back(probe.weights[t]);
      }
      p1[probe.monitor] = static_cast<int>(mode_cells.size());
    }
    for (const auto& line : flux_lines_) {
      kind[line.monitor] = line.vertical ? 2 : 3;
      p0[line.monitor] = line.fixed, p1[line.monitor] = line.from, p2[line.monitor] = line.to;
    }
    g.monitor_count = static_cast<int>(count);
    g.m_kind = g.upload(kind), g.m_p0 = g.upload(p0), g.m_p1 = g.upload(p1), g.m_p2 = g.upload(p2);
    g.m_cells = g.upload(mode_cells), g.m_weights = g.upload(mode_weights);
    // Regeneratoren entscheiden an Blockgrenzen: Block = Entscheidungsabstand
    g.chunk = decision_interval_ > 0 ? decision_interval_ : 128;
    g.history = g.make(g.chunk * std::max<std::size_t>(count, 1) * sizeof(double), nullptr);
    g.signals = g.make(g.chunk * std::max<std::size_t>(g.slots, 1) * sizeof(double), nullptr);
    g.chunk_history.resize(g.chunk * count);
  } catch (const FieldError&) {
    return false;
  }
  g.global_x = (static_cast<std::size_t>(nx_) + 63) / 64 * 64;
  gpu_ = std::move(state);
  team_.reset();  // die Threads des Prozessors werden nicht mehr gebraucht
  return true;
}

void Fdtd::gpu_step() {
  GpuState& g = *gpu_;
  const Api& cl = api();
  const bool ez = polarization_ == Polarization::ez;
  // Blockanfang: Signale aller Schritte des Blocks vorab (Regeneratoren schalten nur an Blockgrenzen)
  if (g.position == 0) {
    g.chunk_signals.assign(g.chunk * std::max<std::size_t>(g.slots, 1), 0.0);
    for (std::size_t s = 0; s < g.chunk; ++s) {
      source_signals((static_cast<double>(steps_ + s) + 0.5) * dt_, signals_);
      std::copy(signals_.begin(), signals_.end(),
                g.chunk_signals.begin() + static_cast<std::ptrdiff_t>(s * g.slots));
    }
    if (g.slots > 0)
      cl.clEnqueueWriteBuffer(g.queue, g.signals, CL_FALSE, 0, g.chunk_signals.size() * sizeof(double),
                              g.chunk_signals.data(), 0, nullptr, nullptr);
  }
  const int nx = nx_, ny = ny_, px = periodic_x_ ? 1 : 0, py = periodic_y_ ? 1 : 0;
  const double inv = 1.0 / dx_, cm = dt_ / constants::mu0;
  const std::size_t global[2] = {g.global_x, static_cast<std::size_t>(ny_)};
  const std::size_t local[2] = {64, 1};
  if (ez) {
    cl_kernel k = g.k_field1;  // ez_h
    arg(k, 0, g.cur), arg(k, 1, g.gx), arg(k, 2, g.gy), arg(k, 3, g.psi_gx), arg(k, 4, g.psi_gy);
    arg(k, 5, g.bxh), arg(k, 6, g.cxh), arg(k, 7, g.byh), arg(k, 8, g.cyh);
    arg(k, 9, nx), arg(k, 10, ny), arg(k, 11, px), arg(k, 12, py), arg(k, 13, inv), arg(k, 14, cm);
    cl.clEnqueueNDRangeKernel(g.queue, k, 2, nullptr, global, local, 0, nullptr, nullptr);
    k = g.k_field2;  // ez_e: neues Feld in den anderen Puffer
    arg(k, 0, g.cur), arg(k, 1, g.other), arg(k, 2, g.gx), arg(k, 3, g.gy), arg(k, 4, g.psi_fx),
        arg(k, 5, g.psi_fy);
    arg(k, 6, g.bxe), arg(k, 7, g.cxe), arg(k, 8, g.bye), arg(k, 9, g.cye), arg(k, 10, g.coef);
    arg(k, 11, nx), arg(k, 12, ny), arg(k, 13, px), arg(k, 14, py), arg(k, 15, inv);
    cl.clEnqueueNDRangeKernel(g.queue, k, 2, nullptr, global, local, 0, nullptr, nullptr);
  } else {
    cl_kernel k = g.k_field1;  // hz_e
    arg(k, 0, g.cur), arg(k, 1, g.gx), arg(k, 2, g.gy), arg(k, 3, g.psi_gx), arg(k, 4, g.psi_gy);
    arg(k, 5, g.bxe), arg(k, 6, g.cxe), arg(k, 7, g.bye), arg(k, 8, g.cye), arg(k, 9, g.coef),
        arg(k, 10, g.coef2);
    arg(k, 11, nx), arg(k, 12, ny), arg(k, 13, px), arg(k, 14, py), arg(k, 15, inv);
    cl.clEnqueueNDRangeKernel(g.queue, k, 2, nullptr, global, local, 0, nullptr, nullptr);
    k = g.k_field2;  // hz_h
    arg(k, 0, g.cur), arg(k, 1, g.other), arg(k, 2, g.gx), arg(k, 3, g.gy), arg(k, 4, g.psi_fx),
        arg(k, 5, g.psi_fy);
    arg(k, 6, g.bxh), arg(k, 7, g.cxh), arg(k, 8, g.byh), arg(k, 9, g.cyh);
    arg(k, 10, nx), arg(k, 11, ny), arg(k, 12, px), arg(k, 13, py), arg(k, 14, inv), arg(k, 15, cm);
    cl.clEnqueueNDRangeKernel(g.queue, k, 2, nullptr, global, local, 0, nullptr, nullptr);
  }
  std::swap(g.cur, g.other);  // cur = neues Feld, other = altes

  if (g.source_count > 0) {
    cl_kernel k = g.k_sources;
    const int row = static_cast<int>(g.position * g.slots);
    arg(k, 0, g.cur), arg(k, 1, g.source_count), arg(k, 2, g.s_cells), arg(k, 3, g.s_offsets),
        arg(k, 4, g.s_slots);
    arg(k, 5, g.s_weights), arg(k, 6, g.signals), arg(k, 7, row);
    const std::size_t n = (static_cast<std::size_t>(g.source_count) + 63) / 64 * 64, l = 64;
    cl.clEnqueueNDRangeKernel(g.queue, k, 1, nullptr, &n, &l, 0, nullptr, nullptr);
  }
  ++steps_;
  if (g.monitor_count > 0) {
    cl_kernel k = g.k_record;
    const int row = static_cast<int>(g.position * static_cast<std::size_t>(g.monitor_count));
    const int ez_flag = ez ? 1 : 0;
    arg(k, 0, g.cur), arg(k, 1, g.other), arg(k, 2, g.gx), arg(k, 3, g.gy), arg(k, 4, g.monitor_count);
    arg(k, 5, g.m_kind), arg(k, 6, g.m_p0), arg(k, 7, g.m_p1), arg(k, 8, g.m_p2), arg(k, 9, g.m_cells);
    arg(k, 10, g.m_weights), arg(k, 11, g.history), arg(k, 12, row), arg(k, 13, nx), arg(k, 14, ez_flag);
    arg(k, 15, dx_);
    const std::size_t n = (static_cast<std::size_t>(g.monitor_count) + 63) / 64 * 64, l = 64;
    cl.clEnqueueNDRangeKernel(g.queue, k, 1, nullptr, &n, &l, 0, nullptr, nullptr);
  }
  ++g.position;
  g.fields_stale = true;
  if (g.position == g.chunk) {
    gpu_sync();  // Messreihen des Blocks holen
    g.position = 0, g.fetched = 0;
    update_triggers();
  } else if (g.position % 16 == 0) {
    cl.clFlush(g.queue);
  }
}

void Fdtd::gpu_sync() const {
  if (!gpu_) return;
  GpuState& g = *gpu_;
  const Api& cl = api();
  // neue Messwerte des laufenden Blocks anhängen
  if (g.position > g.fetched && g.monitor_count > 0) {
    const std::size_t m = static_cast<std::size_t>(g.monitor_count);
    cl.clEnqueueReadBuffer(g.queue, g.history, CL_TRUE, g.fetched * m * sizeof(double),
                           (g.position - g.fetched) * m * sizeof(double), g.chunk_history.data(), 0, nullptr,
                           nullptr);
    for (std::size_t s = 0; s < g.position - g.fetched; ++s)
      for (std::size_t k = 0; k < m; ++k) monitors_[k].values.push_back(g.chunk_history[s * m + k]);
  }
  g.fetched = g.position;
  if (g.fields_stale) {
    g.read(g.cur, f_);
    g.read(g.other, f_prev_);
    g.read(g.gx, gx_);
    g.read(g.gy, gy_);
    g.fields_stale = false;
  }
}

#endif  // PHOTONLAB_HAVE_OPENCL

}  // namespace photonlab::field
