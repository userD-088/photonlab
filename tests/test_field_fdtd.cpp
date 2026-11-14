// Phase 4: 2D-FDTD gegen bekannte Lösungen (Bauplan Abschnitt 16).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "photonlab/field/spectrum.hpp"
#include "photonlab/modes/profile.hpp"
#include "photonlab/modes/slab.hpp"

using namespace photonlab;
using namespace photonlab::field;

namespace {
const double pi = constants::pi;
const double c0 = constants::c0;
const double eta0 = constants::mu0 * constants::c0;

double max_abs(const std::vector<double>& v, std::size_t from = 0,
               std::size_t to = static_cast<std::size_t>(-1)) {
  double m = 0.0;
  for (std::size_t i = from; i < std::min(to, v.size()); ++i) m = std::max(m, std::abs(v[i]));
  return m;
}

// Gebiet ohne Randschicht, überall periodisch: ideal für Eigenlösungen des Gitters
FieldScene periodic_box(int nx, int ny, double dx, Polarization polarization) {
  FieldScene s;
  s.width = nx * dx, s.height = ny * dx, s.cell_size = dx;
  s.boundary_x = s.boundary_y = Boundary::periodic;
  s.polarization = polarization;
  return s;
}
}  // namespace

TEST_CASE("Courant: dt = courant*dx/(c*sqrt(2)); ungültige Szenen werden abgelehnt") {
  FieldScene s = periodic_box(32, 32, 50e-9, Polarization::ez);
  CHECK(time_step(s) == doctest::Approx(0.99 * 50e-9 / (c0 * std::sqrt(2.0))).epsilon(1e-14));
  s.courant = 1.01;
  CHECK_THROWS_AS(Fdtd{s}, FieldError);
  s.courant = 0.99, s.cell_size = 0.0;
  CHECK_THROWS_AS(Fdtd{s}, FieldError);
  s.cell_size = 50e-9, s.background_index = 0.5;
  CHECK_THROWS_AS(Fdtd{s}, FieldError);
  s.background_index = 1.0;
  s.monitors = {{"schräg", MonitorKind::flux, 0.2e-6, 0.2e-6, 0.8e-6, 0.9e-6}};
  CHECK_THROWS_AS(Fdtd{s}, FieldError);
  CHECK_THROWS_AS(Fdtd{FieldScene{}}, FieldError);  // kein Rechengebiet
  CHECK_THROWS_AS(reference_scene("gibt_es_nicht"), FieldError);
}

TEST_CASE("Abnahme Phase 4: numerische Dispersionsrelation des Yee-Schemas") {
  // Stehende Welle cos(kx*x + ky*y) im periodischen Gitter. Für eine Gitter-Eigenlösung gilt exakt
  //   F(n+1) + F(n-1) = 2*cos(omega*dt)*F(n)   mit   cos(omega*dt) = 1 - 2*S²*(sin²(kx*dx/2) +
  //   sin²(ky*dx/2)),
  // S = c*dt/dx. Das ist die Dispersionsrelation sin²(omega*dt/2) = S²*(sin² + sin²) des Verfahrens.
  const int n = 48;
  const double dx = 50e-9;
  for (Polarization polarization : {Polarization::ez, Polarization::hz})
    for (auto [mx, my] : {std::pair{3, 0}, std::pair{0, 2}, std::pair{2, 5}, std::pair{6, 6}}) {
      Fdtd sim(periodic_box(n, n, dx, polarization));
      const double kx = 2 * pi * mx / (n * dx), ky = 2 * pi * my / (n * dx);
      for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
          sim.field()[static_cast<std::size_t>(i + n * j)] = std::cos(kx * i * dx + ky * j * dx);

      const double s = c0 * sim.time_step() / dx;
      const double expected =
          1.0 - 2.0 * s * s * (std::pow(std::sin(kx * dx / 2), 2) + std::pow(std::sin(ky * dx / 2), 2));
      const std::size_t probe = 5 + 7 * static_cast<std::size_t>(n);
      double previous = sim.field()[probe];
      sim.step();
      double current = sim.field()[probe];
      for (int step = 0; step < 40; ++step) {
        sim.step();
        const double next = sim.field()[probe];
        if (std::abs(current) > 0.2)
          CHECK((next + previous) / (2.0 * current) == doctest::Approx(expected).epsilon(1e-11));
        previous = current, current = next;
      }
      // Zum Vergleich: die numerische Kreisfrequenz liegt unter der physikalischen c*|k|
      const double omega_numeric = std::acos(expected) / sim.time_step();
      CHECK(omega_numeric < c0 * std::hypot(kx, ky));
      CHECK(omega_numeric > 0.95 * c0 * std::hypot(kx, ky));
    }
}

TEST_CASE("Abnahme Phase 4: Energieerhaltung im verlustfreien Gitter mit Dielektrikum") {
  for (Polarization polarization : {Polarization::ez, Polarization::hz})
    for (Boundary boundary : {Boundary::periodic, Boundary::pml}) {
      FieldScene s = periodic_box(60, 50, 40e-9, polarization);
      s.boundary_x = s.boundary_y = boundary;
      s.pml_cells = 0;  // Randtyp "pml" ohne Schicht = ideal leitende Wände: geschlossener Hohlraum
      s.shapes = {{"Block", ShapeKind::rectangle, 1.4e-6, 0.9e-6, 0.8e-6, 0.6e-6, 0.3, 1e-6, 0.5e-6, 3.476},
                  {"Kreis", ShapeKind::circle, 0.6e-6, 1.3e-6, 1e-6, 1e-6, 0.0, 0.3e-6, 0.1e-6, 2.0}};
      Fdtd sim(s);
      // Anfangszustand: Gauß-Fleck im Skalarfeld
      for (int j = 0; j < sim.ny(); ++j)
        for (int i = 0; i < sim.nx(); ++i)
          sim.field()[static_cast<std::size_t>(i + sim.nx() * j)] =
              std::exp(-(std::pow(i - 22.0, 2) + std::pow(j - 27.0, 2)) / 18.0);
      sim.step();
      const double start = sim.energy();
      REQUIRE(start > 0.0);
      double worst = 0.0;
      for (int step = 0; step < 3000; ++step) {
        sim.step();
        if (step % 50 == 0) worst = std::max(worst, std::abs(sim.energy() / start - 1.0));
      }
      CHECK(worst < 1e-11);
    }
}

TEST_CASE("Abnahme Phase 4: Reflexion der CPML-Randschicht unter -40 dB") {
  // Punktquelle mit Puls. Vergleich mit einem größeren Gebiet, dessen Rand im Messfenster noch kein
  // Echo liefern kann: die Differenz am Messpunkt ist genau die Reflexion der Randschicht.
  for (Polarization polarization : {Polarization::ez, Polarization::hz}) {
    const double dx = 40e-9;
    // Orte in Zellmitten angeben, damit beide Gitter trotz Rundung dieselbe Zelle treffen
    auto scene = [&](int cells, int shift) {
      const double offset = shift + 0.25;
      FieldScene s;
      s.width = cells * dx, s.height = cells * dx, s.cell_size = dx;
      s.polarization = polarization;
      Source source;
      source.kind = SourceKind::point;
      source.x1 = (offset + 40) * dx, source.y1 = (offset + 40) * dx;
      source.signal = SignalKind::pulse;
      source.pulse_duration = 6e-15;
      s.sources = {source};
      // Messpunkt nahe der Randschicht, schräg zur Quelle: Welle trifft die Schicht unter verschiedenen
      // Winkeln
      s.monitors = {{"Probe", MonitorKind::point, (offset + 16) * dx, (offset + 60) * dx, 0, 0}};
      return s;
    };
    Fdtd small(scene(80, 0));
    Fdtd large(scene(480, 200));    // gleiche Lage relativ zur Quelle, Rand 200 Zellen weiter weg
    const std::size_t steps = 500;  // in dieser Zeit läuft Licht 350 Zellen weit: kein Echo im großen Gebiet
    small.run(steps);
    large.run(steps);
    const auto& a = small.monitors()[0].values;
    const auto& b = large.monitors()[0].values;
    double difference = 0.0;
    for (std::size_t k = 0; k < steps; ++k) difference = std::max(difference, std::abs(a[k] - b[k]));
    const double reflection_db = 20.0 * std::log10(difference / max_abs(b));
    INFO("Reflexion: ", reflection_db, " dB");
    CHECK(reflection_db < -40.0);
  }
}

TEST_CASE("Linienquelle strahlt eine ebene Welle der eingestellten Amplitude ab") {
  // Dauerstrich in einem seitlich periodischen Streifen; E-Amplitude A, H-Amplitude A*n/eta0
  for (double index : {1.0, 1.5})
    for (Polarization polarization : {Polarization::ez, Polarization::hz}) {
      FieldScene s;
      s.width = 16e-6, s.height = 0.5e-6, s.cell_size = 25e-9;
      s.background_index = index;
      s.boundary_y = Boundary::periodic;
      s.polarization = polarization;
      Source source;
      source.x1 = source.x2 = 4e-6, source.y1 = 0.0, source.y2 = 0.5e-6;
      source.amplitude = 3.0;
      s.sources = {source};
      s.monitors = {{"rechts", MonitorKind::point, 9e-6, 0.25e-6, 0, 0},
                    {"links", MonitorKind::point, 1.5e-6, 0.25e-6, 0, 0},
                    {"Fluss", MonitorKind::flux, 9e-6, 0.0, 9e-6, 0.5e-6}};
      Fdtd sim(s);
      const double period = source.wavelength / c0;
      sim.run(static_cast<std::size_t>(60e-15 / sim.time_step()));
      const std::size_t last_periods = static_cast<std::size_t>(3 * period / sim.time_step());
      const double expected = polarization == Polarization::ez ? 3.0 : 3.0 * index / eta0;
      for (int m : {0, 1}) {
        const auto& v = sim.monitors()[static_cast<std::size_t>(m)].values;
        CHECK(max_abs(v, v.size() - last_periods) == doctest::Approx(expected).epsilon(0.01));
      }
      // Leistungsfluss: Intensität A²*n/(2*eta0) mal Linienlänge
      const auto& flux = sim.monitors()[2];
      const double average = average_over_periods(flux.values, flux.sample_interval, period, 4);
      CHECK(average == doctest::Approx(9.0 * index / (2.0 * eta0) * 0.5e-6).epsilon(0.02));
      // Spektrum des Messpunkts: Linie bei der Trägerfrequenz mit der Amplitude der Welle
      const auto& probe = sim.monitors()[0];
      const std::vector<double> tail(
          probe.values.end() - static_cast<std::ptrdiff_t>(4 * period / sim.time_step()), probe.values.end());
      const double f0 = c0 / source.wavelength;
      const auto spectrum = amplitude_spectrum(tail, probe.sample_interval, {0.5 * f0, f0, 1.5 * f0});
      CHECK(spectrum[1] == doctest::Approx(expected).epsilon(0.02));
      CHECK(spectrum[0] < 0.05 * expected);
      CHECK(spectrum[2] < 0.05 * expected);
    }
}

TEST_CASE("Grenzfläche zweier Medien: Fresnel-Reflexion und -Transmission bei senkrechtem Einfall") {
  // r = (n1 - n2)/(n1 + n2), t = 2*n1/(n1 + n2) für das E-Feld
  const double n1 = 1.0, n2 = 1.5;
  for (Polarization polarization : {Polarization::ez, Polarization::hz}) {
    FieldScene s = reference_scene("field_interface");
    s.polarization = polarization;
    FieldScene empty = s;
    empty.shapes.clear();  // Referenzlauf ohne Glas liefert den einfallenden Puls
    Fdtd with(s), without(empty);
    const auto steps = static_cast<std::size_t>(s.duration / with.time_step());
    with.run(steps);
    without.run(steps);
    const auto& total = with.monitors()[0].values;  // vor der Grenzfläche: einfallend + reflektiert
    const auto& incident = without.monitors()[0].values;
    std::vector<double> reflected(steps);
    for (std::size_t k = 0; k < steps; ++k) reflected[k] = total[k] - incident[k];
    const double r = max_abs(reflected) / max_abs(incident);
    const double transmitted = max_abs(with.monitors()[1].values) / max_abs(incident);
    CHECK(r == doctest::Approx((n2 - n1) / (n1 + n2)).epsilon(0.01));
    // Messgröße ist das Skalarfeld: bei Ez direkt t, bei Hz trägt H den Faktor n2/n1 gegenüber E
    const double t = 2.0 * n1 / (n1 + n2);
    CHECK(transmitted == doctest::Approx(polarization == Polarization::ez ? t : t * n2 / n1).epsilon(0.01));
  }
}

TEST_CASE("Zeitsignale der Quellen") {
  Source cw;
  const double period = cw.wavelength / c0;
  CHECK(Fdtd::signal(cw, 0.0) == 0.0);
  CHECK(std::abs(Fdtd::signal(cw, 10.25 * period) - 1.0) < 1e-9);  // nach der Rampe: sin(omega*t)
  cw.phase = pi / 2;
  CHECK(std::abs(Fdtd::signal(cw, 10.0 * period) - 1.0) < 1e-9);
  CHECK(std::abs(Fdtd::signal(cw, 1.0 * period)) < 0.2);  // noch in der Rampe (4 Perioden)

  Source pulse;
  pulse.signal = SignalKind::pulse;
  pulse.pulse_duration = 10e-15;
  pulse.phase = pi / 2;
  CHECK(Fdtd::signal(pulse, 40e-15) == doctest::Approx(1.0));              // Maximum bei 4*tau
  CHECK(std::abs(Fdtd::signal(pulse, 0.0)) < 2e-7);                        // e^-16
  CHECK(std::abs(Fdtd::signal(pulse, 50e-15)) <= std::exp(-1.0) + 1e-12);  // Einhüllende e^-1 bei t0 + tau
}

TEST_CASE("Geometrie: Formen, Überdeckung und Hinweise zur Auflösung") {
  FieldScene s;
  s.width = 4e-6, s.height = 4e-6, s.cell_size = 50e-9, s.background_index = 1.444;
  s.shapes = {{"Platte", ShapeKind::rectangle, 2e-6, 2e-6, 2e-6, 1e-6, 0.0, 1e-6, 0.5e-6, 2.0},
              {"Ring", ShapeKind::ring, 2e-6, 2e-6, 1e-6, 1e-6, 0.0, 0.4e-6, 0.2e-6, 3.476}};
  CHECK(index_at(s, 0.2e-6, 0.2e-6) == 1.444);  // Hintergrund
  CHECK(index_at(s, 1.2e-6, 2.0e-6) == 2.0);    // Platte
  CHECK(index_at(s, 2.3e-6, 2.0e-6) == 3.476);  // Ring liegt oben
  CHECK(index_at(s, 2.0e-6, 2.0e-6) == 2.0);    // Loch des Rings: wieder die Platte
  // gedrehtes Rechteck: Ecke der ungedrehten Platte liegt nach 45° Drehung außerhalb
  Shape rotated = s.shapes[0];
  CHECK(contains(rotated, 2.9e-6, 1.6e-6));
  CHECK_FALSE(contains(rotated, 2.6e-6, 2.6e-6));
  rotated.rotation = pi / 4;
  CHECK_FALSE(contains(rotated, 2.9e-6, 1.6e-6));
  CHECK(contains(rotated, 2.6e-6, 2.6e-6));

  Source source;
  source.x1 = source.x2 = 1e-6, source.y1 = 1e-6, source.y2 = 3e-6;
  s.sources = {source};
  CHECK(cells_per_wavelength(s) == doctest::Approx(1550e-9 / (3.476 * 50e-9)));  // 8,9: zu grob
  const auto warnings = check(s);
  REQUIRE_FALSE(warnings.empty());
  CHECK(warnings.front().severe);
  s.cell_size = 20e-9;
  for (const auto& w : check(s)) CHECK_FALSE(w.severe);
}

TEST_CASE("Referenz-Szenen sind gültig und laufen stabil") {
  for (const auto& name : reference_scene_names()) {
    const FieldScene scene = reference_scene(name);
    CHECK_NOTHROW(validate(scene));
    for (const auto& w : check(scene)) CHECK_FALSE(w.severe);
    Fdtd sim(scene);
    sim.run(200);
    CHECK(std::isfinite(sim.energy()));
    CHECK(max_abs(sim.field()) < 1e3);  // keine Instabilität
  }
}

TEST_CASE("Modenquelle regt die Grundmode mit der eingestellten Amplitude und der analytischen Leistung an") {
  // Schichtwellenleiter in der Ebene; Vergleich mit den analytischen Schichtmoden (Ez: TE, Hz: TM)
  const double n_core = 2.85, n_clad = 1.444, thickness = 0.45e-6, amplitude = 2.0;
  for (Polarization polarization : {Polarization::ez, Polarization::hz}) {
    FieldScene s;
    s.width = 12e-6, s.height = 4e-6, s.cell_size = 25e-9;
    s.background_index = n_clad;
    s.polarization = polarization;
    s.shapes = {{"Kern", ShapeKind::rectangle, 6e-6, 2e-6, 12e-6, thickness, 0.0, 1e-6, 0.5e-6, n_core}};
    Source source;
    source.kind = SourceKind::mode;
    source.x1 = source.x2 = 2e-6, source.y1 = 0.7e-6, source.y2 = 3.3e-6;
    source.amplitude = amplitude;
    s.sources = {source};
    s.monitors = {{"Mode", MonitorKind::mode, 8e-6, 0.7e-6, 8e-6, 3.3e-6},
                  {"Mode 1", MonitorKind::mode, 8e-6, 0.7e-6, 8e-6, 3.3e-6, 1},
                  {"Fluss nah", MonitorKind::flux, 5e-6, 0.7e-6, 5e-6, 3.3e-6},
                  {"Fluss fern", MonitorKind::flux, 9e-6, 0.7e-6, 9e-6, 3.3e-6}};
    Fdtd sim(s);
    const double period = source.wavelength / c0;
    sim.run(static_cast<std::size_t>(200e-15 / sim.time_step()));
    const std::size_t tail = static_cast<std::size_t>(3 * period / sim.time_step());

    const auto slab_polarization =
        polarization == Polarization::ez ? modes::SlabPolarization::te : modes::SlabPolarization::tm;
    const auto exact =
        modes::slab_modes(n_core, n_clad, n_clad, thickness, source.wavelength, slab_polarization);
    REQUIRE(exact.size() >= 2);  // der Wellenleiter führt zwei Moden; angeregt werden soll nur die erste
    CHECK(sim.monitors()[0].mode_index == doctest::Approx(exact[0].n_eff).epsilon(0.01));

    // Amplitude der Grundmode: A für Ez, A/eta0 für Hz
    const double expected = polarization == Polarization::ez ? amplitude : amplitude / eta0;
    const auto& mode0 = sim.monitors()[0].values;
    const auto& mode1 = sim.monitors()[1].values;
    CHECK(max_abs(mode0, mode0.size() - tail) == doctest::Approx(expected).epsilon(0.03));
    CHECK(max_abs(mode1, mode1.size() - tail) < 0.02 * expected);  // Modenreinheit

    // Leistung der Mode: P = n_eff*A²/(2*eta0) * Integral(f² * w) mit w = 1 (TE) bzw. 1/n² (TM)
    double integral = 0.0;
    const double step = 1e-9;
    for (double y = -1.5e-6; y < thickness + 1.5e-6; y += step) {
      const double f = modes::slab_field(exact[0], y, n_core, n_clad, n_clad, thickness, source.wavelength,
                                         slab_polarization);
      const double n = y >= 0.0 && y <= thickness ? n_core : n_clad;
      integral += f * f * modes::overlap_weight(n, slab_polarization) * step;
    }
    const double power = exact[0].n_eff * amplitude * amplitude / (2.0 * eta0) * integral;
    const auto& near = sim.monitors()[2];
    const auto& far = sim.monitors()[3];
    const double p_near = average_over_periods(near.values, near.sample_interval, period, 6);
    const double p_far = average_over_periods(far.values, far.sample_interval, period, 6);
    CHECK(p_near == doctest::Approx(power).epsilon(0.04));
    CHECK(p_far == doctest::Approx(p_near).epsilon(0.01));  // verlustfrei geführt
  }
}

TEST_CASE("Modenquelle und Modenmonitor melden fehlende Führung im Klartext") {
  FieldScene s;
  s.width = 6e-6, s.height = 4e-6, s.cell_size = 40e-9;
  Source source;
  source.kind = SourceKind::mode;
  source.x1 = source.x2 = 2e-6, source.y1 = 1e-6, source.y2 = 3e-6;
  s.sources = {source};
  CHECK_THROWS_AS(Fdtd{s}, FieldError);  // kein Wellenleiter unter der Linie
  s.sources[0].y2 = 1e-6, s.sources[0].x2 = 3e-6;
  s.sources[0].y1 = 1.3e-6;  // schräge Linie
  CHECK_THROWS_AS(Fdtd{s}, FieldError);
  CHECK(layer_effective_index(s, "Si", "SiO2", 220e-9) > 1.5);  // TM0 der Schicht (Szene mit Ez)
  s.polarization = Polarization::hz;
  CHECK(layer_effective_index(s, "Si", "SiO2", 220e-9) == doctest::Approx(2.85).epsilon(0.01));  // TE0
  CHECK(material_index(s, "SiO2") == doctest::Approx(1.444).epsilon(1e-3));
  CHECK_THROWS_AS(material_index(s, "Unobtainium"), FieldError);
}

TEST_CASE("Wellenleiter führt Licht: hinter 9 µm kommt der Großteil der eingekoppelten Leistung an") {
  FieldScene s = reference_scene("field_waveguide");
  Fdtd sim(s);
  sim.run(static_cast<std::size_t>(s.duration / sim.time_step()));
  const double period = s.sources[0].wavelength / c0;
  const auto& front = sim.monitors()[0];
  const auto& back = sim.monitors()[1];
  const double p_front = average_over_periods(front.values, front.sample_interval, period, 6);
  const double p_back = average_over_periods(back.values, back.sample_interval, period, 6);
  CHECK(p_front > 0.0);
  // Ein Teil des Gauß-Strahls passt nicht zur Mode und strahlt ab; der geführte Rest kommt verlustfrei an.
  CHECK(p_back / p_front > 0.5);
  CHECK(p_back / p_front < 1.02);
}

// Kerr-Effekt im FDTD: Phasenverschiebung einer ebenen Welle in einem nichtlinearen Streifen gegen die
// analytische Näherung phi = 2 pi/lambda * n2 * I * L mit I = n0 c eps0 E0^2 / 2 (Ez-Polarisation).
TEST_CASE("Kerr-Effekt im FDTD: Phasenverschiebung entspricht n2*I*L") {
  const double n0 = 1.5, e0 = 1e8, length = 20e-6, wavelength = 1550e-9;
  auto run = [&](double n2) {
    FieldScene s;
    s.width = 40e-6, s.height = 0.4e-6, s.cell_size = 50e-9;
    s.background_index = n0;
    s.polarization = Polarization::ez;
    s.boundary_x = Boundary::pml;
    s.boundary_y = Boundary::periodic;
    s.shapes = {{"Kerr", ShapeKind::rectangle, 25e-6, 0.2e-6, length, 1e-6, 0.0, 1e-6, 0.5e-6, n0, n2}};
    Source q;
    q.kind = SourceKind::line;
    q.x1 = q.x2 = 5e-6, q.y1 = 0.0, q.y2 = s.height;
    q.amplitude = e0;
    q.wavelength = wavelength;
    s.sources = {q};
    s.monitors = {{"E hinter dem Streifen", MonitorKind::point, 37e-6, 0.2e-6, 0.0, 0.0}};
    Fdtd sim(s);
    sim.run(static_cast<std::size_t>(400e-15 / sim.time_step()));
    const MonitorData& m = sim.monitors().front();
    // Lock-in über die letzten 8 Perioden
    const double omega = 2.0 * pi * c0 / wavelength;
    const std::size_t count = static_cast<std::size_t>(8.0 * wavelength / c0 / m.sample_interval);
    std::complex<double> z = 0.0;
    for (std::size_t k = m.values.size() - count; k < m.values.size(); ++k)
      z += m.values[k] * std::exp(std::complex<double>(
                             0.0, -omega * (m.start_time + static_cast<double>(k) * m.sample_interval)));
    return z;
  };
  const double n2 = 1e-16;  // künstlich groß, damit die Verschiebung messbar ist (Silizium: 4,5e-18)
  const std::complex<double> linear = run(0.0), kerr = run(n2);
  const double measured = -std::arg(kerr / linear);
  const double intensity = n0 * c0 * constants::eps0 * e0 * e0 / 2.0;
  const double expected = 2.0 * pi / wavelength * n2 * intensity * length;
  INFO("Phase gemessen " << measured << ", erwartet " << expected);
  CHECK(measured == doctest::Approx(expected).epsilon(0.02));
  CHECK(std::abs(kerr) == doctest::Approx(std::abs(linear)).epsilon(0.03));  // keine Verluste
}
