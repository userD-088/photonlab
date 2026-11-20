#pragma once
// Grafikkarte für das 2D- und 3D-FDTD über OpenCL (docs/performance.md). Die OpenCL-Bibliothek des
// Grafiktreibers wird zur Laufzeit geladen; fehlt sie oder kann das Gerät keine doppelte Genauigkeit, rechnet
// alles auf dem Prozessor. Fdtd- und Extruded3d-Objekte, die nach set_enabled(true) entstehen, rechnen auf der
// Grafikkarte, wenn die Szene es erlaubt (linear: keine Kerr-Formen; inverses Design und S-Parameter bleiben
// auf dem Prozessor). Fdtd3d selbst wechselt nur über use_gpu(true).
#include <string>

namespace photonlab::field::gpu {

struct DeviceInfo {
  bool available = false;
  std::string name;      // z. B. "AMD Radeon RX 6700 XT"
  std::string platform;  // z. B. "AMD Accelerated Parallel Processing"
  bool is_gpu = false;   // false: OpenCL auf dem Prozessor (nur zum Testen, siehe PHOTONLAB_OPENCL_DEVICE)
  std::string reason;    // warum nicht verfügbar (Klartext)
};

// Erstes passendes Gerät (beim ersten Aufruf gesucht). Umgebungsvariable PHOTONLAB_OPENCL_DEVICE: "gpu"
// (Standard: nur Grafikkarten), "any" (auch OpenCL auf dem Prozessor, für Tests), "off" (nie).
const DeviceInfo& device();

void set_enabled(bool enabled);  // Standard: aus (Bibliothek und Tests rechnen auf dem Prozessor)
bool enabled();                  // eingeschaltet und Gerät vorhanden

}  // namespace photonlab::field::gpu
