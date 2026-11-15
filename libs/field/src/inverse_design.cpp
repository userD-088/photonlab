#include "photonlab/field/inverse_design.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/materials/material.hpp"

namespace photonlab::field {
namespace {

using cplx = std::complex<double>;

// Gewichte des Kegelfilters je Pixel (Nachbarn im Radius, Gewicht r - Abstand)
struct FilterKernel {
  std::vector<int> di, dj;
  std::vector<double> w;
};

FilterKernel kernel(double radius) {
  FilterKernel k;
  const int reach = static_cast<int>(std::floor(radius));
  for (int dj = -reach; dj <= reach; ++dj)
    for (int di = -reach; di <= reach; ++di) {
      const double weight = radius - std::hypot(di, dj);
      if (weight > 0.0) k.di.push_back(di), k.dj.push_back(dj), k.w.push_back(weight);
    }
  return k;
}

// Transponierte des (am Rand normierten) Filters für die Kettenregel
std::vector<double> cone_filter_transpose(const std::vector<double>& g, int nx, int ny, double radius) {
  if (radius <= 1.0) return g;
  const FilterKernel k = kernel(radius);
  std::vector<double> norm(g.size(), 0.0), out(g.size(), 0.0);
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i)
      for (std::size_t q = 0; q < k.w.size(); ++q) {
        const int a = i + k.di[q], b = j + k.dj[q];
        if (a >= 0 && b >= 0 && a < nx && b < ny) norm[static_cast<std::size_t>(i + nx * j)] += k.w[q];
      }
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      const std::size_t p = static_cast<std::size_t>(i + nx * j);
      for (std::size_t q = 0; q < k.w.size(); ++q) {
        const int a = i + k.di[q], b = j + k.dj[q];
        if (a >= 0 && b >= 0 && a < nx && b < ny)
          out[static_cast<std::size_t>(a + nx * b)] += k.w[q] * g[p] / norm[p];
      }
    }
  return out;
}

// Zeitkonvention der Auswertung: X(w) = Summe_n x(t_n) exp(-i w t_n)
cplx phasor(double omega, double time) { return std::polar(1.0, -omega * time); }

}  // namespace

std::vector<double> cone_filter(const std::vector<double>& x, int nx, int ny, double radius) {
  if (radius <= 1.0) return x;
  const FilterKernel k = kernel(radius);
  std::vector<double> out(x.size(), 0.0);
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      double sum = 0.0, norm = 0.0;
      for (std::size_t q = 0; q < k.w.size(); ++q) {
        const int a = i + k.di[q], b = j + k.dj[q];
        if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
        sum += k.w[q] * x[static_cast<std::size_t>(a + nx * b)];
        norm += k.w[q];
      }
      out[static_cast<std::size_t>(i + nx * j)] = sum / norm;
    }
  return out;
}

double project(double x, double beta) {
  const double eta = 0.5;
  return (std::tanh(beta * eta) + std::tanh(beta * (x - eta))) /
         (std::tanh(beta * eta) + std::tanh(beta * (1.0 - eta)));
}

double project_derivative(double x, double beta) {
  const double eta = 0.5, c = std::cosh(beta * (x - eta));
  return beta / (c * c) / (std::tanh(beta * eta) + std::tanh(beta * (1.0 - eta)));
}

BendProblem::BendProblem(const InverseDesignSettings& s) : settings_(s) {
  const double dx = s.cell_size;
  if (!(dx > 0.0) || !(s.design_size > 4 * dx) || !(s.waveguide_width > 2 * dx) || s.pml_cells < 4)
    throw FieldError("Inverses Design: Zelle, Designgebiet oder Wellenleiter ungültig");
  if (s.objective_wavelengths.empty()) throw FieldError("Inverses Design: keine Zielwellenlänge");
  const double n_core = materials::get(s.core).index(s.wavelength);
  const double n_clad = materials::get(s.cladding).index(s.wavelength);
  eps_core_ = n_core * n_core, eps_clad_ = n_clad * n_clad;
  design_nx_ = design_ny_ = static_cast<int>(std::lround(s.design_size / dx));
  const int lead = static_cast<int>(std::lround(s.lead / dx));
  i0_ = j0_ = s.pml_cells + lead;
  const int n = 2 * i0_ + design_nx_;
  const double size = n * dx;
  // Designgebiet: Knoten i0 .. i0+nx-1, als Fläche [(i0 - 1/2) dx, (i0 + nx - 1/2) dx]
  const double r0 = (i0_ - 0.5) * dx, r1 = (i0_ + design_nx_ - 0.5) * dx, center = 0.5 * (r0 + r1);

  FieldScene scene;
  scene.name = "Inverses Design 90-Grad-Knick";
  scene.width = scene.height = size;
  scene.cell_size = dx;
  scene.background_index = n_clad;
  scene.polarization = Polarization::ez;
  scene.pml_cells = s.pml_cells;
  Shape input;
  input.name = "Eingang";
  input.x = 0.5 * (r0 - 1e-6), input.y = center, input.width = r0 + 1e-6, input.height = s.waveguide_width;
  input.index = n_core;
  Shape output;
  output.name = "Ausgang";
  output.x = center, output.y = 0.5 * (r1 + size + 1e-6), output.width = s.waveguide_width;
  output.height = size + 1e-6 - r1;
  output.index = n_core;
  scene.shapes = {input, output};
  Source source;
  source.name = "Eingangsmode";
  source.kind = SourceKind::mode;
  const double xs = (s.pml_cells + 4) * dx;
  source.x1 = source.x2 = xs, source.y1 = center - 1e-6, source.y2 = center + 1e-6;
  source.signal = SignalKind::pulse;
  source.wavelength = s.wavelength;
  source.pulse_duration = s.pulse_duration;
  scene.sources = {source};
  const double ym = size - (s.pml_cells + 6) * dx;
  scene.monitors = {
      {"Ausgangsmode", MonitorKind::mode, center - 1e-6, ym, center + 1e-6, ym, 0, s.wavelength}};
  scene_ = scene;

  reference_scene_ = scene;
  reference_scene_.name = "Bezug: gerader Wellenleiter";
  Shape straight = input;
  straight.x = 0.5 * size, straight.width = size + 2e-6;
  reference_scene_.shapes = {straight};
  const double xm = size - (s.pml_cells + 6) * dx;
  reference_scene_.monitors = {
      {"Bezug", MonitorKind::mode, xm, center - 1e-6, xm, center + 1e-6, 0, s.wavelength}};

  // Laufzeit: Puls (8 tau) plus dreimal der Weg durch das Gebiet mit dem Kernindex; danach ist das Feld
  // abgeklungen (Voraussetzung der Herleitung, siehe docs/physics/inverse_design.md)
  const double time = 8.0 * s.pulse_duration + 3.0 * 2.0 * size * n_core / constants::c0;
  steps_ = static_cast<std::size_t>(std::ceil(time / field::time_step(scene)));
}

double BendProblem::pixel_x0() const { return (i0_ - 0.5) * settings_.cell_size; }
double BendProblem::pixel_y0() const { return (j0_ - 0.5) * settings_.cell_size; }

std::vector<cplx> BendProblem::reference_amplitudes(const std::vector<double>& wavelengths) const {
  Fdtd sim(reference_scene_);
  sim.set_threads(settings_.threads);
  sim.run(steps_);
  const auto& values = sim.monitors()[0].values;
  std::vector<cplx> a(wavelengths.size());
  for (std::size_t w = 0; w < wavelengths.size(); ++w) {
    const double omega = 2.0 * constants::pi * constants::c0 / wavelengths[w];
    for (std::size_t n = 0; n < values.size(); ++n)
      a[w] += values[n] * phasor(omega, static_cast<double>(n + 1) * sim.time_step());
  }
  return a;
}

std::vector<double> BendProblem::evaluate(const std::vector<double>& eps,
                                          std::vector<std::vector<double>>* gradient,
                                          const std::vector<double>& wavelengths) const {
  const std::size_t pixels = static_cast<std::size_t>(design_nx_) * static_cast<std::size_t>(design_ny_);
  if (eps.size() != pixels) throw FieldError("Inverses Design: Zahl der Pixel passt nicht");
  const std::size_t nw = wavelengths.size();
  std::vector<double> omega(nw);
  for (std::size_t w = 0; w < nw; ++w) omega[w] = 2.0 * constants::pi * constants::c0 / wavelengths[w];
  auto design_cells = [&](const Fdtd& sim) {
    std::vector<std::size_t> cells(pixels);
    for (int j = 0; j < design_ny_; ++j)
      for (int i = 0; i < design_nx_; ++i)
        cells[static_cast<std::size_t>(i + design_nx_ * j)] =
            static_cast<std::size_t>(i0_ + i) +
            static_cast<std::size_t>(sim.nx()) * static_cast<std::size_t>(j0_ + j);
    return cells;
  };
  // Ein Lauf: Fourier-Summen im Designgebiet (falls verlangt) und am Monitor
  struct Run {
    std::vector<std::vector<cplx>> design;  // [Wellenlänge][Pixel]
    std::vector<cplx> monitor;              // [Wellenlänge]
    std::vector<cplx> source_spectrum;      // [Wellenlänge]
    double dt = 0.0;
  };
  auto run = [&](bool adjoint, bool with_design) {
    FieldScene scene = scene_;
    if (adjoint) scene.sources.clear();
    Fdtd sim(scene);
    sim.set_threads(settings_.threads);
    const auto cells = design_cells(sim);
    for (std::size_t p = 0; p < pixels; ++p) sim.set_permittivity(cells[p], eps[p]);
    Run r;
    r.dt = sim.time_step();
    Source pulse = scene_.sources.front();
    if (adjoint) {
      // Adjungierte Quelle: Stromschicht mit den Monitor-Gewichten w_k, geteilt durch eps_r (D-Einspeisung)
      const Fdtd::ModeWeights weights = sim.mode_weights(0);
      std::vector<double> q(weights.cells.size());
      for (std::size_t k = 0; k < q.size(); ++k)
        q[k] = weights.weights[k] / sim.permittivity()[weights.cells[k]];
      pulse.amplitude = 1.0;
      sim.add_injection(weights.cells, q, pulse);
    }
    r.design.assign(with_design ? nw : 0, std::vector<cplx>(pixels));
    r.monitor.assign(nw, cplx(0.0, 0.0));
    r.source_spectrum.assign(nw, cplx(0.0, 0.0));
    std::vector<cplx> rot(nw);
    for (std::size_t s = 0; s < steps_; ++s) {
      const double source_time = (static_cast<double>(s) + 0.5) * r.dt;
      const double signal = Fdtd::signal(pulse, source_time);
      for (std::size_t w = 0; w < nw; ++w) r.source_spectrum[w] += signal * phasor(omega[w], source_time);
      sim.step();
      const double t = sim.time();
      for (std::size_t w = 0; w < nw; ++w) rot[w] = phasor(omega[w], t);
      if (with_design) {
        const auto& f = sim.field();
        for (std::size_t w = 0; w < nw; ++w) {
          cplx* d = r.design[w].data();
          for (std::size_t p = 0; p < pixels; ++p) d[p] += f[cells[p]] * rot[w];
        }
      }
      const double m = sim.monitors()[0].values.back();
      for (std::size_t w = 0; w < nw; ++w) r.monitor[w] += m * rot[w];
    }
    return r;
  };

  const bool with_gradient = gradient != nullptr;
  Run forward, adjoint;
  std::vector<cplx> reference;
  std::thread adjoint_thread, reference_thread;
  if (with_gradient) adjoint_thread = std::thread([&] { adjoint = run(true, true); });
  reference_thread = std::thread([&] { reference = reference_amplitudes(wavelengths); });
  forward = run(false, with_gradient);
  if (adjoint_thread.joinable()) adjoint_thread.join();
  reference_thread.join();

  std::vector<double> transmission(nw);
  if (with_gradient) gradient->assign(nw, std::vector<double>(pixels, 0.0));
  for (std::size_t w = 0; w < nw; ++w) {
    const double norm = std::norm(reference[w]);
    transmission[w] = std::norm(forward.monitor[w]) / norm;
    if (!with_gradient) continue;
    // dT/d eps(r) = 2 Re( conj(a) * Omega dt * E_adj(r) E(r) / (i S_adj) ) / |a_ref|^2, Omega = 2 sin(w
    // dt/2)/dt
    const double big_omega_dt = 2.0 * std::sin(0.5 * omega[w] * forward.dt);
    const cplx factor =
        std::conj(forward.monitor[w]) * big_omega_dt / (cplx(0.0, 1.0) * adjoint.source_spectrum[w]);
    for (std::size_t p = 0; p < pixels; ++p)
      (*gradient)[w][p] = 2.0 * std::real(factor * adjoint.design[w][p] * forward.design[w][p]) / norm;
  }
  return transmission;
}

std::vector<double> BendProblem::spectrum(const std::vector<double>& eps,
                                          const std::vector<double>& wavelengths) const {
  return evaluate(eps, nullptr, wavelengths);
}

InverseDesignResult run_inverse_design(
    const InverseDesignSettings& s, JobControl* control,
    const std::function<void(const InverseDesignProgress&)>& on_iteration) {
  const BendProblem problem(s);
  const int nx = problem.nx(), ny = problem.ny();
  const std::size_t pixels = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
  const double radius = s.filter_radius / s.cell_size;
  const double delta_eps = problem.eps_core() - problem.eps_cladding();
  InverseDesignResult result;
  result.nx = nx, result.ny = ny, result.pixel_size = s.cell_size;
  result.pixel_x0 = problem.pixel_x0(), result.pixel_y0 = problem.pixel_y0();
  result.scene = problem.scene();
  result.eps_core = problem.eps_core(), result.eps_cladding = problem.eps_cladding();

  std::vector<double> rho(pixels, std::clamp(s.initial, 0.0, 1.0));
  auto beta_at = [&](int iteration) {
    const double f = static_cast<double>(iteration) / std::max(1, s.iterations);
    return f < 0.35 ? 1.0 : f < 0.6 ? 4.0 : f < 0.85 ? 16.0 : 64.0;  // Projektion schrittweise schärfer
  };
  auto density_of = [&](const std::vector<double>& r, double beta, std::vector<double>* filtered) {
    *filtered = cone_filter(r, nx, ny, radius);
    std::vector<double> d(pixels);
    for (std::size_t p = 0; p < pixels; ++p) d[p] = project((*filtered)[p], beta);
    return d;
  };
  auto eps_of = [&](const std::vector<double>& d) {
    std::vector<double> e(pixels);
    for (std::size_t p = 0; p < pixels; ++p) e[p] = problem.eps_cladding() + d[p] * delta_eps;
    return e;
  };
  auto mean = [](const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) sum += x;
    return sum / static_cast<double>(v.size());
  };

  std::vector<double> filtered;
  double step = s.step;  // passt sich an: kleiner, wenn das Ziel sinkt, langsam wieder größer
  for (int iteration = 0; iteration < s.iterations; ++iteration) {
    if (control) {
      if (control->cancel_requested) return result;
      control->progress = static_cast<float>(iteration) / static_cast<float>(s.iterations + 2);
    }
    const double beta = beta_at(iteration);
    result.density = density_of(rho, beta, &filtered);
    std::vector<std::vector<double>> gradient;
    const std::vector<double> t =
        problem.evaluate(eps_of(result.density), &gradient, s.objective_wavelengths);
    result.history.push_back(mean(t));
    result.beta.push_back(beta);
    if (iteration == 0) result.transmission_start = result.history.front();
    if (iteration > 0 && result.beta[result.beta.size() - 2] == beta) {
      const double previous = result.history[result.history.size() - 2];
      step = result.history.back() < previous ? std::max(0.5 * step, 0.01) : std::min(1.2 * step, s.step);
    }
    // Kettenregel: Ziel = Mittel über die Wellenlängen; d eps/d Dichte; Projektion; Filter (transponiert)
    std::vector<double> g(pixels, 0.0);
    for (const auto& gw : gradient)
      for (std::size_t p = 0; p < pixels; ++p) g[p] += gw[p] / static_cast<double>(gradient.size());
    for (std::size_t p = 0; p < pixels; ++p) g[p] *= delta_eps * project_derivative(filtered[p], beta);
    g = cone_filter_transpose(g, nx, ny, radius);
    double largest = 0.0;
    for (double v : g) largest = std::max(largest, std::abs(v));
    if (largest > 0.0)
      for (std::size_t p = 0; p < pixels; ++p) rho[p] = std::clamp(rho[p] + step * g[p] / largest, 0.0, 1.0);
    if (on_iteration) on_iteration({iteration, &result.density, &result.history});
  }

  // Ergebnis: stetig, binär (Schwelle 0,5), leer; Spektrum des binären Designs
  const double beta = beta_at(s.iterations);
  result.density = density_of(rho, beta, &filtered);
  result.transmission_final =
      mean(problem.evaluate(eps_of(result.density), nullptr, s.objective_wavelengths));
  if (control)
    control->progress = static_cast<float>(s.iterations + 1) / static_cast<float>(s.iterations + 2);
  result.binary.resize(pixels);
  for (std::size_t p = 0; p < pixels; ++p) result.binary[p] = result.density[p] >= 0.5 ? 1.0 : 0.0;
  for (int k = 0; k <= 20; ++k) result.spectrum_wavelengths.push_back(s.wavelength - 100e-9 + k * 10e-9);
  std::vector<double> objective_and_spectrum = s.objective_wavelengths;
  objective_and_spectrum.insert(objective_and_spectrum.end(), result.spectrum_wavelengths.begin(),
                                result.spectrum_wavelengths.end());
  const auto binary_t = problem.evaluate(eps_of(result.binary), nullptr, objective_and_spectrum);
  result.transmission_binary = mean(std::vector<double>(
      binary_t.begin(), binary_t.begin() + static_cast<std::ptrdiff_t>(s.objective_wavelengths.size())));
  result.spectrum_binary.assign(
      binary_t.begin() + static_cast<std::ptrdiff_t>(s.objective_wavelengths.size()), binary_t.end());
  result.transmission_empty = mean(problem.evaluate(std::vector<double>(pixels, problem.eps_cladding()),
                                                    nullptr, s.objective_wavelengths));
  result.completed = true;
  if (control) control->progress = 1.0f;
  return result;
}

FieldScene design_scene(const InverseDesignResult& r) {
  if (!r.completed || r.binary.empty()) throw FieldError("Kein fertiges inverses Design");
  FieldScene scene = r.scene;
  scene.name = "Inverses Design (binär)";
  const double n_core = std::sqrt(r.eps_core), dx = r.pixel_size;
  struct Run {
    int i0, i1, j0, j1;
  };
  std::vector<Run> open, done;
  for (int j = 0; j < r.ny; ++j) {
    std::vector<Run> next;
    for (int i = 0; i < r.nx;) {
      if (r.binary[static_cast<std::size_t>(i + r.nx * j)] < 0.5) {
        ++i;
        continue;
      }
      int end = i;
      while (end < r.nx && r.binary[static_cast<std::size_t>(end + r.nx * j)] >= 0.5) ++end;
      const auto it =
          std::find_if(open.begin(), open.end(), [&](const Run& o) { return o.i0 == i && o.i1 == end; });
      if (it != open.end()) {
        next.push_back({i, end, it->j0, j + 1});
        open.erase(it);
      } else {
        next.push_back({i, end, j, j + 1});
      }
      i = end;
    }
    done.insert(done.end(), open.begin(), open.end());
    open = std::move(next);
  }
  done.insert(done.end(), open.begin(), open.end());
  int count = 0;
  for (const Run& run : done) {
    Shape rect;
    rect.name = "Design " + std::to_string(++count);
    rect.x = r.pixel_x0 + 0.5 * (run.i0 + run.i1) * dx;
    rect.y = r.pixel_y0 + 0.5 * (run.j0 + run.j1) * dx;
    rect.width = (run.i1 - run.i0) * dx;
    rect.height = (run.j1 - run.j0) * dx;
    rect.index = n_core;
    scene.shapes.push_back(rect);
  }
  scene.duration =
      8.0 * scene.sources.front().pulse_duration + 3.0 * 2.0 * scene.width * n_core / constants::c0;
  return scene;
}

std::vector<GradientSample> check_gradient(const InverseDesignSettings& s, const std::vector<int>& pixels,
                                           double delta_eps) {
  const BendProblem problem(s);
  const std::size_t count = static_cast<std::size_t>(problem.nx()) * static_cast<std::size_t>(problem.ny());
  const double eps0 = problem.eps_cladding() + s.initial * (problem.eps_core() - problem.eps_cladding());
  // ungleichmäßiger Startpunkt, damit der Test nicht von einer Symmetrie lebt
  std::vector<double> eps(count);
  for (std::size_t p = 0; p < count; ++p) eps[p] = eps0 + 0.8 * std::sin(0.37 * static_cast<double>(p));
  std::vector<std::vector<double>> gradient;
  problem.evaluate(eps, &gradient, s.objective_wavelengths);
  std::vector<GradientSample> samples;
  auto mean = [](const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) sum += x;
    return sum / static_cast<double>(v.size());
  };
  for (int pixel : pixels) {
    const auto p = static_cast<std::size_t>(pixel);
    GradientSample sample;
    sample.pixel = pixel;
    for (const auto& gw : gradient) sample.adjoint += gw[p] / static_cast<double>(gradient.size());
    std::vector<double> plus = eps, minus = eps;
    plus[p] += delta_eps, minus[p] -= delta_eps;
    sample.finite_difference = (mean(problem.evaluate(plus, nullptr, s.objective_wavelengths)) -
                                mean(problem.evaluate(minus, nullptr, s.objective_wavelengths))) /
                               (2.0 * delta_eps);
    samples.push_back(sample);
  }
  return samples;
}

}  // namespace photonlab::field
