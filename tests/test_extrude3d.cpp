// 3D-Rechnung einer Feld-Szene: Schichtaufbau, Brechzahlen und Leistungserhaltung im geraden Wellenleiter.
#include <doctest/doctest.h>

#include <cmath>

#include "photonlab/core/units.hpp"
#include "photonlab/field/extrude3d.hpp"
#include "photonlab/materials/material.hpp"

using namespace photonlab;
using namespace photonlab::field;

namespace {
// Gerader Si-Wellenleiter (in 2D mit dem effektiven Index 2,85 der 220-nm-Schicht) in Oxid, Hz-Szene
FieldScene straight_guide() {
  FieldScene scene;
  scene.name = "Gerade";
  scene.width = 4.4e-6, scene.height = 2.4e-6;
  scene.cell_size = 25e-9;
  scene.background_index = 1.444;
  scene.polarization = Polarization::hz;
  Shape core;
  core.name = "Kern";
  core.x = 2.2e-6, core.y = 1.2e-6, core.width = 4.4e-6, core.height = 0.5e-6, core.index = 2.85;
  scene.shapes = {core};
  Source source;
  source.name = "Quelle";
  source.kind = SourceKind::mode;
  source.x1 = source.x2 = 0.8e-6, source.y1 = 0.3e-6, source.y2 = 2.1e-6;
  source.direction = +1;
  scene.sources = {source};
  Monitor a, b;
  a.name = "A", b.name = "B";
  a.kind = b.kind = MonitorKind::flux;
  a.x1 = a.x2 = 2.4e-6, b.x1 = b.x2 = 3.6e-6;  // A weit genug hinter der Quelle (Streulicht, fdtd3d.md)
  a.y1 = b.y1 = 0.35e-6, a.y2 = b.y2 = 2.05e-6;
  scene.monitors = {a, b};
  scene.duration = 250e-15;
  return scene;
}
}  // namespace

TEST_CASE("3D-Szene: Kerne werden Silizium, darüber und darunter Oxid") {
  const FieldScene scene = straight_guide();
  Stack3d stack;
  stack.cell_size = 50e-9;
  const Extruded3d sim(scene, stack);
  const double n_si = materials::get("Si").index(1550e-9), n_ox = materials::get("SiO2").index(1550e-9);
  CHECK(sim.indices().core == doctest::Approx(n_si));
  const auto top = sim.top_index();
  const int nx = sim.solver().nx(), ny = sim.solver().ny(), nz = sim.solver().nz();
  CHECK(top[static_cast<std::size_t>(nx / 2 + nx * (ny / 2))] == doctest::Approx(n_si).epsilon(1e-6));  // Kern
  CHECK(top[static_cast<std::size_t>(nx / 2 + nx * 2)] == doctest::Approx(n_ox).epsilon(1e-6));        // daneben
  const auto side = sim.side_index(1.2e-6);
  CHECK(side[static_cast<std::size_t>(nx / 2 + nx * 1)] == doctest::Approx(n_ox).epsilon(1e-6));       // Substrat
  CHECK(side[static_cast<std::size_t>(nx / 2 + nx * (nz - 2))] == doctest::Approx(n_ox).epsilon(1e-6));  // Mantel
  CHECK(side[static_cast<std::size_t>(nx / 2 + nx * sim.layer_k())] == doctest::Approx(n_si).epsilon(1e-6));
  CHECK(sim.layer_top() - sim.layer_bottom() == doctest::Approx(220e-9));
  const Extent3d e = extent_3d(scene, stack);
  CHECK(e.nx == nx);
  CHECK(e.nz == nz);
}

TEST_CASE("3D-Szene: gerader Wellenleiter führt die Leistung verlustfrei von Monitor A nach B") {
  const FieldScene scene = straight_guide();
  Stack3d stack;
  stack.cell_size = 50e-9;
  stack.below = stack.above = 0.5e-6;
  Extruded3d sim(scene, stack);
  sim.run(sim.steps_for(scene.duration));
  const double a = sim.monitors()[0].mean, b = sim.monitors()[1].mean;
  MESSAGE("Fluss A ", a, " W, B ", b, " W, B/A ", b / a);
  CHECK(a > 0.0);
  CHECK(b / a == doctest::Approx(1.0).epsilon(0.03));
}
