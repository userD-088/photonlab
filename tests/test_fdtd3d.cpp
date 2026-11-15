// Phase 12: Geschwindigkeit des 2D-FDTD (Threads ohne Einfluss auf das Ergebnis) und 3D-FDTD
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "photonlab/core/step_team.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/fdtd3d.hpp"
#include "photonlab/field/waveguide3d.hpp"

using namespace photonlab;
using namespace photonlab::field;
using photonlab::constants::c0;
using photonlab::constants::pi;

TEST_CASE("StepTeam: jedes Mitglied genau einmal, Ausnahmen kommen beim Aufrufer an") {
  StepTeam team(3);
  CHECK(team.size() == 3);
  for (int round = 0; round < 200; ++round) {
    std::vector<int> hits(3, 0);
    team.run([&](int member) { hits[static_cast<std::size_t>(member)] += 1; });
    CHECK(hits == std::vector<int>{1, 1, 1});
  }
  CHECK_THROWS(team.run([](int member) {
    if (member == 2) throw std::runtime_error("Test");
  }));
  int sum = 0;
  team.run([&](int member) {
    if (member == 0) sum = 7;
  });
  CHECK(sum == 7);
  int begin = 0, end = 0;
  split_range(10, 3, 2, &begin, &end);
  CHECK(begin == 6);
  CHECK(end == 10);
}

TEST_CASE("Phase 12: 2D-FDTD rechnet mit jeder Zahl von Threads bitgleich") {
  for (Polarization polarization : {Polarization::ez, Polarization::hz})
    for (bool periodic : {false, true}) {
      FieldScene s;
      s.width = 8e-6, s.height = 6e-6, s.cell_size = 40e-9;
      s.polarization = polarization;
      if (periodic) s.boundary_x = Boundary::periodic;
      Shape guide;
      guide.x = 4e-6, guide.y = 3e-6, guide.width = 8e-6, guide.height = 0.5e-6;
      Shape kerr;
      kerr.kind = ShapeKind::circle;
      kerr.x = 5e-6, kerr.y = 4.3e-6, kerr.radius = 0.6e-6, kerr.index = 2.0, kerr.n2 = 1e-16;
      s.shapes = {guide, kerr};
      Source source;
      source.kind = SourceKind::point;
      source.x1 = 2e-6, source.y1 = 3e-6, source.amplitude = 1e9;
      s.sources = {source};
      s.monitors = {{"Fluss", MonitorKind::flux, 6e-6, 2e-6, 6e-6, 4e-6}};
      Fdtd serial(s), parallel(s);
      serial.set_threads(1);
      parallel.set_threads(4);
      serial.run(400);
      parallel.run(400);
      CHECK(serial.field() == parallel.field());
      CHECK(serial.in_plane_x() == parallel.in_plane_x());
      CHECK(serial.in_plane_y() == parallel.in_plane_y());
      CHECK(serial.monitors()[0].values == parallel.monitors()[0].values);
      CHECK(serial.energy() == parallel.energy());
    }
}

TEST_CASE("Phase 12: 3D-Hohlraum schwingt mit der Frequenz der Yee-Dispersionsrelation") {
  // Ideal leitende Box (ohne Randschicht) mit Dielektrikum, Eigenmode mit einer E-Komponente:
  //   E_x ~ sin(pi j/ny) sin(pi k/nz), E_y ~ sin(pi i/nx) sin(pi k/nz), E_z ~ sin(pi i/nx) sin(pi j/ny).
  // Für eine Gitter-Eigenlösung gilt exakt F(n+1) + F(n-1) = 2 cos(w dt) F(n) mit
  //   sin²(w dt/2) = (c dt/(n dx))² * Summe sin²(k_a dx/2).
  const double dx = 50e-9, index = 1.5;
  Scene3d scene;
  scene.size_x = 14 * dx, scene.size_y = 11 * dx, scene.size_z = 9 * dx;
  scene.cell_size = dx;
  scene.pml_cells = 0;
  scene.background_index = index;
  const int n[3] = {14, 11, 9};
  for (int c = 0; c < 3; ++c) {
    Fdtd3d sim(scene);
    const auto component = static_cast<Component3d>(c);
    // die beiden Achsen quer zur Komponente
    const int a = (c + 1) % 3, b = (c + 2) % 3;
    auto& field = sim.component(component);
    for (int k = 0; k < sim.nz(); ++k)
      for (int j = 0; j < sim.ny(); ++j)
        for (int i = 0; i < sim.nx(); ++i) {
          const int pos[3] = {i, j, k};
          field[sim.at(i, j, k)] = std::sin(pi * pos[a] / n[a]) * std::sin(pi * pos[b] / n[b]);
        }
    const double s = c0 * sim.time_step() / (index * dx);
    const double sum = std::pow(std::sin(pi / (2 * n[a])), 2) + std::pow(std::sin(pi / (2 * n[b])), 2);
    const double expected = 1.0 - 2.0 * s * s * sum;  // cos(w dt)
    const std::size_t probe = sim.at(5, 4, 3);
    double previous = field[probe];
    sim.step();
    double current = field[probe];
    int checked = 0;
    for (int step = 0; step < 60; ++step) {
      sim.step();
      const double next = field[probe];
      if (std::abs(current) > 0.2) {
        CHECK((next + previous) / (2.0 * current) == doctest::Approx(expected).epsilon(1e-10).scale(0.0));
        ++checked;
      }
      previous = current, current = next;
    }
    CHECK(checked > 10);
    // die anderen E-Komponenten bleiben 0 (Mode ist exakt)
    for (int other = 0; other < 3; ++other)
      if (other != c) {
        double largest = 0.0;
        for (double v : sim.component(static_cast<Component3d>(other)))
          largest = std::max(largest, std::abs(v));
        CHECK(largest < 1e-12);
      }
  }
}

TEST_CASE("Phase 12: 3D-Energie bleibt im geschlossenen Hohlraum beschränkt, Threads ändern nichts") {
  Scene3d scene;
  scene.size_x = scene.size_y = scene.size_z = 24 * 40e-9;
  scene.cell_size = 40e-9;
  scene.pml_cells = 6;
  scene.blocks = {{"Würfel", 300e-9, 600e-9, 300e-9, 700e-9, 200e-9, 500e-9, 3.0}};
  Fdtd3d serial(scene), parallel(scene);
  serial.set_threads(1);
  parallel.set_threads(3);
  Source3d source;
  source.component = Component3d::ez;
  source.cells = {serial.at(10, 12, 11), serial.at(11, 12, 11)};
  source.weights = {1.0, 0.5};
  source.signal.signal = SignalKind::pulse;
  source.signal.pulse_duration = 2e-15;
  source.signal.wavelength = 1000e-9;
  serial.add_source(source);
  parallel.add_source(source);
  serial.run(150);
  parallel.run(150);
  for (int c = 0; c < 6; ++c)
    CHECK(serial.component(static_cast<Component3d>(c)) == parallel.component(static_cast<Component3d>(c)));
  CHECK(std::isfinite(serial.energy()));
  CHECK(serial.permittivity(Component3d::ex, serial.at(10, 10, 8)) == doctest::Approx(9.0));
}

TEST_CASE("Phase 12: Reflexion der 3D-Randschicht unter -40 dB") {
  // Punktquelle mit Puls; Differenz zu einem größeren Gebiet, dessen Rand im Messfenster kein Echo liefert
  const double dx = 40e-9;
  auto run = [&](int cells, int shift, int pml) {
    Scene3d scene;
    scene.size_x = scene.size_y = scene.size_z = cells * dx;
    scene.cell_size = dx;
    scene.pml_cells = pml;
    Fdtd3d sim(scene);
    Source3d source;
    source.component = Component3d::ez;
    source.cells = {sim.at(shift + 22, shift + 22, shift + 22)};
    source.weights = {1.0};
    source.signal.signal = SignalKind::pulse;
    source.signal.pulse_duration = 2e-15;
    source.signal.wavelength = 1000e-9;
    sim.add_source(source);
    std::vector<double> trace;
    const std::size_t probe = sim.at(shift + 30, shift + 16, shift + 22);
    for (int step = 0; step < 220; ++step) {
      sim.step();
      trace.push_back(sim.component(Component3d::ez)[probe]);
    }
    return trace;
  };
  const auto large = run(132, 44, 10);
  auto reflection_db = [&](const std::vector<double>& small) {
    double difference = 0.0, peak = 0.0;
    for (std::size_t k = 0; k < small.size(); ++k) {
      difference = std::max(difference, std::abs(small[k] - large[k]));
      peak = std::max(peak, std::abs(large[k]));
    }
    return 20.0 * std::log10(difference / peak);
  };
  const double with_pml = reflection_db(run(44, 0, 10));
  const double walls = reflection_db(run(44, 0, 0));  // Gegenprobe: ideal leitende Wände ohne Schicht
  MESSAGE("3D-Randschicht: Reflexion " << with_pml << " dB (ohne Schicht " << walls << " dB)");
  CHECK(with_pml < -40.0);
  CHECK(walls > -20.0);  // das Messfenster sieht das Echo tatsächlich
}

TEST_CASE("Abnahme Phase 12: Si-Schicht im 3D-FDTD = analytischer Schichtwellenleiter") {
  Waveguide3dSettings settings;
  settings.slab = true;
  const Waveguide3dResult r = run_waveguide_3d(settings);
  REQUIRE(r.completed);
  MESSAGE("Schicht 220 nm: FDTD " << r.n_eff_fdtd << ", analytisch " << r.n_eff_analytic << ", Modenlöser "
                                  << r.n_eff_mode << ", Überlapp " << r.mode_overlap);
  CHECK(r.n_eff_fdtd == doctest::Approx(r.n_eff_analytic).epsilon(0.002).scale(0.0));
  CHECK(r.mode_overlap > 0.99);
  CHECK(r.amplitude_ripple < 0.06);
}

TEST_CASE("Abnahme Phase 12: Si-Draht 500 x 220 nm im 3D-FDTD") {
  // Vollvektoriell: Literatur n_eff = 2,44 (Chrostowski/Hochberg 2015, Kap. 3). Der semi-vektorielle
  // Modenlöser liegt bekanntermaßen etwa 2 % darüber (docs/physics/modes.md); das 3D-FDTD braucht diese
  // Näherung nicht.
  const Waveguide3dResult r = run_waveguide_3d(Waveguide3dSettings{});
  REQUIRE(r.completed);
  MESSAGE("Si-Draht: FDTD " << r.n_eff_fdtd << " (Yee-korrigiert " << r.n_eff_corrected << "), Modenlöser "
                            << r.n_eff_mode << ", Überlapp " << r.mode_overlap << ", "
                            << r.cells_per_second / 1e6 << " Mio. Zellen/s mit " << r.threads << " Threads");
  CHECK(r.n_eff_fdtd == doctest::Approx(2.44).epsilon(0.01).scale(0.0));
  CHECK(r.n_eff_mode > r.n_eff_fdtd);
  CHECK(r.relative_error < 0.03);
  CHECK(r.mode_overlap > 0.98);
}
