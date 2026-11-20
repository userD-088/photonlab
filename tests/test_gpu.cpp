// Grafikkarte (OpenCL): gleiche Ergebnisse wie der Prozessor. Ohne OpenCL-Gerät werden die Vergleiche
// übersprungen; mit PHOTONLAB_OPENCL_DEVICE=any laufen sie auch auf OpenCL für Prozessoren (z. B. PoCL).
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "photonlab/field/extrude3d.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/fdtd3d.hpp"
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

void use_any_device() {
#ifdef _WIN32
  _putenv_s("PHOTONLAB_OPENCL_DEVICE", "any");
#else
  setenv("PHOTONLAB_OPENCL_DEVICE", "any", 0);
#endif
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
  use_any_device();
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

TEST_CASE("Grafikkarte: 3D-FDTD mit Randschichten, Quellen und Proben wie auf dem Prozessor") {
  use_any_device();
  if (!gpu::device().available) {
    MESSAGE("kein OpenCL-Gerät: " << gpu::device().reason);
    return;
  }
  // Quader-Wellenleiter, Randschicht an allen Seiten, Puls- und Dauerstrich-Quelle in verschiedenen
  // Komponenten
  Scene3d scene;
  scene.size_x = 1.6e-6, scene.size_y = 1.3e-6, scene.size_z = 1.1e-6, scene.cell_size = 50e-9;
  scene.pml_cells = 6, scene.background_index = 1.444;
  scene.blocks = {{"Kern", 0.0, 1.6e-6, 0.4e-6, 0.9e-6, 0.45e-6, 0.65e-6, 3.48}};
  Fdtd3d cpu(scene), gpu(scene);
  auto add_sources = [&](Fdtd3d& f) {
    Source3d a;
    a.component = Component3d::ey;
    a.signal.signal = SignalKind::pulse, a.signal.wavelength = 1.55e-6, a.signal.pulse_duration = 5e-15;
    for (int j = 9; j < 17; ++j) a.cells.push_back(f.at(8, j, 11)), a.weights.push_back(1.0 + 0.1 * j);
    Source3d b;
    b.component = Component3d::ez;
    b.signal.wavelength = 1.31e-6, b.signal.phase = 0.3;
    b.cells = {f.at(20, 12, 10), f.at(21, 12, 10)}, b.weights = {0.5, -0.25};
    f.add_source(a);
    f.add_source(b);
  };
  add_sources(cpu);
  add_sources(gpu);
  REQUIRE(gpu.use_gpu(true));
  const std::vector<Probe3d> probes = {{Component3d::ey, cpu.at(25, 12, 11)},
                                       {Component3d::hz, cpu.at(25, 12, 11)},
                                       {Component3d::ex, cpu.at(3, 3, 3)}};
  std::vector<double> cpu_values, gpu_values;
  cpu.run_probed(150, probes, &cpu_values);
  gpu.run_probed(70, probes, &gpu_values);
  (void)std::as_const(gpu).component(Component3d::ey);  // Zwischenstand holen, weiter auf der Grafikkarte
  gpu.run_probed(80, probes, &gpu_values);
  CHECK(gpu.on_gpu());
  double scale = 0.0;
  REQUIRE(cpu_values.size() == gpu_values.size());
  CHECK(max_difference(cpu_values, gpu_values, &scale) <= 1e-12 * scale);
  CHECK(scale > 0.0);
  for (auto c : {Component3d::ex, Component3d::ey, Component3d::ez, Component3d::hx, Component3d::hy,
                 Component3d::hz}) {
    const double error = max_difference(cpu.component(c), gpu.component(c), &scale);
    MESSAGE("3D Komponente " << static_cast<int>(c) << ": max. Abweichung " << error << " bei Größe "
                             << scale);
    CHECK(error <= 1e-12 * scale);
  }
  CHECK(gpu.energy() == doctest::Approx(cpu.energy()).epsilon(1e-12).scale(0.0));
  // zurück auf den Prozessor (mit den Hilfsfeldern der Randschicht) und weiter
  gpu.use_gpu(false);
  cpu.run(40), gpu.run(40);
  CHECK(max_difference(cpu.component(Component3d::ey), gpu.component(Component3d::ey), &scale) <=
        1e-12 * scale);
}

TEST_CASE("Grafikkarte: extrudierte 3D-Szene misst dieselben Flüsse") {
  use_any_device();
  if (!gpu::device().available) return;
  FieldScene scene;
  scene.width = 3.0e-6, scene.height = 2.0e-6, scene.background_index = 1.444;
  Shape core;
  core.name = "Kern", core.x = 1.5e-6, core.y = 1.0e-6, core.width = 3.0e-6, core.height = 0.5e-6;
  core.index = 2.85;
  scene.shapes = {core};
  Source source;
  source.name = "Quelle", source.kind = SourceKind::mode, source.direction = +1;
  source.x1 = source.x2 = 0.7e-6, source.y1 = 0.4e-6, source.y2 = 1.6e-6;
  scene.sources = {source};
  Monitor a, p;
  a.name = "A", a.kind = MonitorKind::flux, a.x1 = a.x2 = 2.0e-6, a.y1 = 0.45e-6, a.y2 = 1.55e-6;
  Monitor b = a;
  b.name = "B", b.x1 = 0.5e-6, b.x2 = 2.5e-6, b.y1 = b.y2 = 1.4e-6;  // waagerechte Fläche
  p.name = "P", p.kind = MonitorKind::point, p.x1 = 2.2e-6, p.y1 = 1.0e-6;
  scene.monitors = {a, b, p};
  Stack3d stack;
  stack.cell_size = 60e-9, stack.below = stack.above = 0.4e-6;
  gpu::set_enabled(false);
  Extruded3d cpu(scene, stack);
  gpu::set_enabled(true);
  Extruded3d gpu(scene, stack);
  gpu::set_enabled(false);
  CHECK_FALSE(cpu.solver().on_gpu());
  REQUIRE(gpu.solver().on_gpu());
  cpu.run(300), gpu.run(300);
  REQUIRE(cpu.monitors().size() == gpu.monitors().size());
  for (std::size_t m = 0; m < cpu.monitors().size(); ++m) {
    double scale = 0.0;
    REQUIRE(cpu.monitors()[m].value.size() == gpu.monitors()[m].value.size());
    const double error = max_difference(cpu.monitors()[m].value, gpu.monitors()[m].value, &scale);
    MESSAGE("Monitor " << cpu.monitors()[m].name << ": max. Abweichung " << error << " bei Größe " << scale);
    CHECK(scale > 0.0);
    CHECK(error <= 1e-12 * scale);
  }
  CHECK(gpu.top_view(false) == cpu.top_view(false));
}
