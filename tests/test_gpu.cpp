// Grafikkarte (OpenCL): gleiche Ergebnisse wie der Prozessor. Ohne OpenCL-Gerät werden die Vergleiche
// übersprungen; mit PHOTONLAB_OPENCL_DEVICE=any laufen sie auch auf OpenCL für Prozessoren (z. B. PoCL).
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/gpu.hpp"
#include "photonlab/field/reference_scenes.hpp"

using namespace photonlab;
using namespace photonlab::field;

namespace {

double max_difference(const std::vector<double>& a, const std::vector<double>& b, double* scale) {
  double d = 0.0, m = 0.0;
  for (std::size_t k = 0; k < a.size() && k < b.size(); ++k) {
    d = std::max(d, std::abs(a[k] - b[k]));
    m = std::max(m, std::abs(a[k]));
  }
  *scale = m;
  return d;
}

// Rechnet die Szene auf beiden Wegen; Abweichung relativ zum größten Wert
void compare(const FieldScene& scene, std::size_t steps, const std::string& what) {
  Fdtd cpu(scene), gpu(scene);
  REQUIRE(gpu.use_gpu(true));
  for (std::size_t s = 0; s < steps; ++s) cpu.step();
  gpu.run(steps / 2);
  gpu.field();  // Zwischenstand holen (prüft das Weiterrechnen nach einem Abruf mitten im Block)
  gpu.run(steps - steps / 2);
  double scale = 0.0;
  const double field_error = max_difference(cpu.field(), gpu.field(), &scale);
  MESSAGE(what << ": Feld max. Abweichung " << field_error << " bei Größe " << scale);
  CHECK(field_error <= 1e-12 * scale);
  REQUIRE(cpu.monitors().size() == gpu.monitors().size());
  for (std::size_t m = 0; m < cpu.monitors().size(); ++m) {
    REQUIRE(cpu.monitors()[m].values.size() == gpu.monitors()[m].values.size());
    const double error = max_difference(cpu.monitors()[m].values, gpu.monitors()[m].values, &scale);
    CHECK(error <= 1e-12 * scale);
  }
  CHECK(gpu.energy() == doctest::Approx(cpu.energy()).epsilon(1e-12).scale(0.0));
  // zurück auf den Prozessor und weiter: gleicher Verlauf
  gpu.use_gpu(false);
  for (int s = 0; s < 20; ++s) cpu.step(), gpu.step();
  CHECK(max_difference(cpu.field(), gpu.field(), &scale) <= 1e-12 * scale);
}

}  // namespace

TEST_CASE("Grafikkarte: Ez, Hz, Randschichten, periodisch, Quellen, Regenerator wie auf dem Prozessor") {
#ifdef _WIN32
  _putenv_s("PHOTONLAB_OPENCL_DEVICE", "any");
#else
  setenv("PHOTONLAB_OPENCL_DEVICE", "any", 0);
#endif
  const auto& device = gpu::device();
  if (!device.available) {
    MESSAGE("kein OpenCL-Gerät: " << device.reason);
    return;
  }
  MESSAGE("OpenCL-Gerät: " << device.name << " (" << device.platform << ")");
  // Wellenleiter mit Modenquelle und allen Monitorarten, beide Polarisationen
  FieldScene waveguide = reference_scene("field_waveguide");
  compare(waveguide, 600, "Wellenleiter Hz");
  waveguide.polarization = Polarization::ez;
  compare(waveguide, 600, "Wellenleiter Ez");
  // periodisch in x, Linienquelle mit Gauß-Profil, Punktmonitor
  FieldScene periodic = reference_scene("field_interference");
  periodic.boundary_x = Boundary::periodic;
  compare(periodic, 400, "periodisch Ez");
  periodic.polarization = Polarization::hz;
  compare(periodic, 400, "periodisch Hz");
  // Ring mit Puls, Kristall
  compare(reference_scene("field_ring"), 500, "Ring");
  // Regenerator: Detektor schaltet einen zweiten Sender (Entscheidungen an denselben Schritten)
  FieldScene regen;
  regen.width = 6e-6, regen.height = 3e-6, regen.cell_size = 50e-9;
  Source x;
  x.name = "X", x.kind = SourceKind::point, x.x1 = x.x2 = 1.0e-6, x.y1 = x.y2 = 1.5e-6;
  Source y = x;
  y.name = "Y", y.x1 = y.x2 = 5.0e-6;
  y.trigger = "Detektor", y.trigger_threshold = 1e-30, y.trigger_delay = 30e-15, y.trigger_invert = false;
  regen.sources = {x, y};
  regen.monitors = {{"Detektor", MonitorKind::flux, 1.5e-6, 1.0e-6, 1.5e-6, 2.0e-6},
                    {"Ausgang", MonitorKind::point, 5.0e-6, 1.6e-6, 0, 0}};
  compare(regen, 2000, "Regenerator Ez");
  regen.polarization = Polarization::hz;
  compare(regen, 2000, "Regenerator Hz");
  // einseitige Modenquelle (zweite Schicht)
  FieldScene one_sided = reference_scene("field_waveguide");
  one_sided.sources[0].direction = 1;
  compare(one_sided, 600, "einseitige Quelle");
}

TEST_CASE("Grafikkarte: Kerr-Formen und zusätzliche Einspeisungen bleiben auf dem Prozessor") {
  FieldScene s = reference_scene("field_waveguide");
  s.polarization = Polarization::ez;
  s.shapes[0].n2 = 1e-17;
  Fdtd sim(s);
  CHECK_FALSE(sim.use_gpu(true));
  CHECK_FALSE(sim.on_gpu());
}
