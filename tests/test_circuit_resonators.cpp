// Phase 2: Ringresonatoren, Y-Splitter, Bandpass, Gitterkoppler gegen Formeln und gegen ausgeschriebene Schaltungen.
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <vector>

#include "photonlab/circuit/analysis.hpp"
#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/circuit/sweep.hpp"
#include "photonlab/core/units.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {
const ComponentRegistry& reg() { return ComponentRegistry::builtin(); }
const double lambda0 = 1550 * units::nm;

// Parameter des Referenz-Rings
const double radius = 10 * units::um, coupling = 0.02, loss_db_per_cm = 10.0, n_eff = 2.44, n_group = 4.2;
const double ring_length = 2.0 * constants::pi * radius;
const double t = std::sqrt(1.0 - coupling);
const double a = std::pow(10.0, -loss_db_per_cm * (ring_length / 1e-2) / 20.0);

double round_trip_phase(double lambda) {
  const double n = n_eff + (n_eff - n_group) * (lambda - lambda0) / lambda0;
  return 2.0 * constants::pi * n * ring_length / lambda;
}
}  // namespace

TEST_CASE("All-Pass-Ring: Kompaktmodell = Schaltung aus Koppler und Schleife = analytische Transmission") {
  const ParamMap ring_params = {{"radius", radius}, {"power_coupling", coupling}, {"loss_db_per_cm", loss_db_per_cm}};
  const auto ring = reg().create("ring_allpass", ring_params);

  Circuit explicit_ring;
  explicit_ring.add("koppler", "coupler", {{"power_coupling", coupling}});
  explicit_ring.add("schleife", "waveguide", {{"length", ring_length}, {"loss_db_per_cm", loss_db_per_cm}});
  explicit_ring.connect("koppler.out2", "schleife.in");
  explicit_ring.connect("schleife.out", "koppler.in2");
  const CompiledCircuit compiled(explicit_ring, reg());
  CVector a_ext = CVector::Zero(compiled.port_count());
  a_ext(compiled.port_index("koppler", "in1")) = 1.0;

  for (int k = 0; k <= 4000; ++k) {
    const double lambda = (1540.0 + 0.005 * k) * units::nm;
    const cplx tau = ring->s_matrix(lambda)(1, 0);
    const cplx tau_circuit = compiled.solve(lambda, &a_ext).b(compiled.port_index("koppler", "out1"));
    CHECK(std::abs(tau - tau_circuit) < 1e-11);
    const double phi = round_trip_phase(lambda);
    const double expected = (a * a - 2 * t * a * std::cos(phi) + t * t) / (1 - 2 * a * t * std::cos(phi) + t * t * a * a);
    CHECK(std::abs(std::norm(tau) - expected) < 1e-11);
  }
}

TEST_CASE("Add-Drop-Ring: Kompaktmodell = Schaltung aus zwei Kopplern und zwei Halbringen") {
  const double k1 = 0.05, k2 = 0.03;
  const auto ring = reg().create(
      "ring_adddrop",
      {{"radius", radius}, {"power_coupling_1", k1}, {"power_coupling_2", k2}, {"loss_db_per_cm", loss_db_per_cm}});

  Circuit c;
  c.add("k1", "coupler", {{"power_coupling", k1}});  // in1 = in, in2 = Ring, out1 = through, out2 = Ring
  c.add("k2", "coupler", {{"power_coupling", k2}});  // in1 = Ring, in2 = add, out1 = Ring, out2 = drop
  c.add("halb1", "waveguide", {{"length", ring_length / 2}, {"loss_db_per_cm", loss_db_per_cm}});
  c.add("halb2", "waveguide", {{"length", ring_length / 2}, {"loss_db_per_cm", loss_db_per_cm}});
  c.connect("k1.out2", "halb1.in");
  c.connect("halb1.out", "k2.in1");
  c.connect("k2.out1", "halb2.in");
  c.connect("halb2.out", "k1.in2");
  const CompiledCircuit compiled(c, reg());
  const int in = compiled.port_index("k1", "in1"), add = compiled.port_index("k2", "in2");
  const int through = compiled.port_index("k1", "out1"), drop = compiled.port_index("k2", "out2");

  for (int k = 0; k <= 2000; ++k) {
    const double lambda = (1540.0 + 0.01 * k) * units::nm;
    const CMatrix s = ring->s_matrix(lambda);  // Ports: in, add, through, drop
    CHECK((s - s.transpose()).norm() < 1e-15);  // reziprok
    CVector from_in = CVector::Zero(compiled.port_count()), from_add = from_in;
    from_in(in) = 1.0;
    from_add(add) = 1.0;
    const Solution s_in = compiled.solve(lambda, &from_in), s_add = compiled.solve(lambda, &from_add);
    CHECK(std::abs(s(2, 0) - s_in.b(through)) < 1e-11);
    CHECK(std::abs(s(3, 0) - s_in.b(drop)) < 1e-11);
    CHECK(std::abs(s(2, 1) - s_add.b(through)) < 1e-11);
    CHECK(std::abs(s(3, 1) - s_add.b(drop)) < 1e-11);
  }
}

TEST_CASE("Add-Drop-Ring: verlustfrei unitär, symmetrisch gekoppelt volle Übertragung in Resonanz") {
  const auto ring = reg().create(
      "ring_adddrop", {{"radius", radius}, {"power_coupling_1", 0.1}, {"power_coupling_2", 0.1}, {"loss_db_per_cm", 0.0}});
  // Resonanz: Umlaufphase = 2*pi*m. phi(lambda) = 2*pi*L*(n_g/lambda + (n_eff - n_g)/lambda0)
  const double m = std::round(round_trip_phase(lambda0) / (2.0 * constants::pi));
  const double lambda_res = n_group / (m / ring_length - (n_eff - n_group) / lambda0);
  const CMatrix s = ring->s_matrix(lambda_res);
  CHECK((s.adjoint() * s - CMatrix::Identity(4, 4)).norm() < 1e-12);
  CHECK(std::norm(s(3, 0)) == doctest::Approx(1.0).epsilon(1e-9));  // drop
  CHECK(std::norm(s(2, 0)) < 1e-9);                                 // through
}

TEST_CASE("Abnahme Phase 2: Ring-FSR, Güte und Extinktion entsprechen den Formeln") {
  const CompiledCircuit compiled(reference_ring_allpass(), reg());
  const auto solutions = sweep(compiled, {1540e-9, 1560e-9, 40001});  // 0,5 pm Schrittweite
  std::vector<double> wavelengths, transmission;
  const int det = compiled.port_index("det", "in");
  for (const auto& s : solutions) {
    wavelengths.push_back(s.wavelength);
    transmission.push_back(power(s.a(det)) / 1e-3);
  }
  const auto resonances = find_resonances(wavelengths, transmission, ResonanceKind::dip);
  REQUIRE(resonances.size() == 3);  // FSR ca. 9 nm im 20-nm-Fenster
  const Resonance& r1 = resonances[0];
  const Resonance& r2 = resonances[1];

  // Lage: Umlaufphase ist ein Vielfaches von 2*pi
  const double cycles = round_trip_phase(r1.position) / (2.0 * constants::pi);
  CHECK(std::abs(cycles - std::round(cycles)) < 1e-4);
  // FSR = lambda1*lambda2/(n_g*L), also etwa lambda²/(n_g*L)
  CHECK(r2.position - r1.position == doctest::Approx(r1.position * r2.position / (n_group * ring_length)).epsilon(1e-4));
  CHECK(mean_spacing(resonances) == doctest::Approx(r2.position - r1.position).epsilon(1e-2));
  // Halbwertsbreite = lambda²*(1 - t*a)/(pi*n_g*L*sqrt(t*a)), Güte = lambda/Breite
  const double fwhm = r1.position * r1.position * (1 - t * a) / (constants::pi * n_group * ring_length * std::sqrt(t * a));
  CHECK(r1.fwhm == doctest::Approx(fwhm).epsilon(1e-2));
  CHECK(r1.quality == doctest::Approx(r1.position / fwhm).epsilon(1e-2));
  // Extinktion: T_min = (a - t)²/(1 - t*a)²
  const double t_min = std::pow((a - t) / (1 - t * a), 2);
  CHECK(r1.value == doctest::Approx(t_min).epsilon(1e-2));
  CHECK(r1.extinction_db == doctest::Approx(-10.0 * std::log10(t_min)).epsilon(5e-3));
}

TEST_CASE("Kritische Kopplung: t = a löscht die Transmission in Resonanz aus") {
  const double k_critical = 1.0 - a * a;
  const auto ring = reg().create(
      "ring_allpass", {{"radius", radius}, {"power_coupling", k_critical}, {"loss_db_per_cm", loss_db_per_cm}});
  const double m = std::round(round_trip_phase(lambda0) / (2.0 * constants::pi));
  const double lambda_res = n_group / (m / ring_length - (n_eff - n_group) / lambda0);
  CHECK(std::norm(ring->s_matrix(lambda_res)(1, 0)) < 1e-12);
}

TEST_CASE("Abnahme Phase 2: 10.000-Punkte-Sweep in unter 1 s") {
  const CompiledCircuit compiled(reference_ring_adddrop(), reg());
  const auto start = std::chrono::steady_clock::now();
  const auto solutions = sweep(compiled, {1500e-9, 1600e-9, 10000});  // bewusst ohne Threadpool
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(solutions.size() == 10000);
  CHECK(seconds < 1.0);
}

TEST_CASE("Y-Splitter: halbe Leistung je Arm, als Vereiniger nur der gleichphasige Anteil") {
  const auto y = reg().create("y_splitter", {});
  const CMatrix s = y->s_matrix(lambda0);
  CHECK(std::norm(s(1, 0)) == doctest::Approx(0.5).epsilon(1e-14));
  CHECK(std::norm(s(2, 0)) == doctest::Approx(0.5).epsilon(1e-14));
  CHECK((s - s.transpose()).norm() == 0.0);

  // Zwei Y-Splitter als MZI: Ausgang cos²(dphi/2), der Rest geht im Vereiniger verloren
  for (double dphi : {0.0, 1.0, constants::pi / 2, constants::pi}) {
    Circuit c;
    c.add("laser", "laser", {{"power", 1.0}});
    c.add("teiler", "y_splitter");
    c.add("phase", "phase_shifter", {{"phase", dphi}});
    c.add("vereiniger", "y_splitter");
    c.add("det", "detector");
    c.connect("laser.out", "teiler.in");
    c.connect("teiler.out1", "phase.in");
    c.connect("phase.out", "vereiniger.out1");
    c.connect("teiler.out2", "vereiniger.out2");
    c.connect("vereiniger.in", "det.in");
    const CompiledCircuit compiled(c, reg());
    const Solution solution = compiled.solve(lambda0);
    CHECK(power(solution.a(compiled.port_index("det", "in"))) ==
          doctest::Approx(std::pow(std::cos(dphi / 2), 2)).epsilon(1e-12));
  }
}

TEST_CASE("Bandpass: Butterworth |H|² = 1/(1 + x^(2n)), 3 dB an den Bandkanten") {
  const double center = 1550 * units::nm, bandwidth = 2 * units::nm;
  for (int order : {1, 2, 3, 5, 8}) {
    const auto filter = reg().create(
        "bandpass", {{"wavelength_center", center}, {"bandwidth", bandwidth}, {"order", static_cast<double>(order)}});
    CHECK(std::norm(filter->s_matrix(center)(1, 0)) == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(std::norm(filter->s_matrix(center + bandwidth / 2)(1, 0)) == doctest::Approx(0.5).epsilon(1e-12));
    CHECK(std::norm(filter->s_matrix(center - bandwidth / 2)(1, 0)) == doctest::Approx(0.5).epsilon(1e-12));
    for (double x : {-3.0, -0.7, 0.3, 1.9}) {
      const double expected = 1.0 / (1.0 + std::pow(x, 2 * order));
      CHECK(std::norm(filter->s_matrix(center + x * bandwidth / 2)(1, 0)) == doctest::Approx(expected).epsilon(1e-10));
    }
  }
}

TEST_CASE("Gitterkoppler: Gauß mit 3-dB-Bandbreite und Einfügeverlust") {
  const double center = 1550 * units::nm, bandwidth = 60 * units::nm;
  const auto gc = reg().create(
      "grating_coupler", {{"wavelength_center", center}, {"bandwidth", bandwidth}, {"insertion_loss_db", 4.0}});
  const double peak = from_db(-4.0);
  CHECK(std::norm(gc->s_matrix(center)(1, 0)) == doctest::Approx(peak).epsilon(1e-12));
  CHECK(std::norm(gc->s_matrix(center + bandwidth / 2)(1, 0)) == doctest::Approx(peak / 2).epsilon(1e-12));
  CHECK(std::norm(gc->s_matrix(center - bandwidth)(1, 0)) == doctest::Approx(peak / 16).epsilon(1e-12));
}

TEST_CASE("Resonanzsuche: Lorentz-Spitze mit bekannter Lage, Breite und Höhe") {
  const double center = 3.2, width = 0.05;
  std::vector<double> x, y;
  for (int i = 0; i <= 4000; ++i) {
    x.push_back(2.0 + 0.0005 * i);
    y.push_back(0.1 + 0.9 / (1.0 + std::pow(2.0 * (x.back() - center) / width, 2)));
  }
  const auto peaks = find_resonances(x, y, ResonanceKind::peak);
  REQUIRE(peaks.size() == 1);
  CHECK(peaks[0].position == doctest::Approx(center).epsilon(1e-6));
  CHECK(peaks[0].value == doctest::Approx(1.0).epsilon(1e-4));
  // Der Bezugspegel am Rand des Fensters liegt etwas über dem Sockel, daher ist die Breite geringfügig kleiner
  CHECK(peaks[0].fwhm == doctest::Approx(width).epsilon(2e-2));
  CHECK(find_resonances(x, y, ResonanceKind::dip).empty());
  ResonanceKind kind = ResonanceKind::dip;
  CHECK(find_dominant_resonances(x, y, &kind).size() == 1);
  CHECK(kind == ResonanceKind::peak);
  CHECK(find_resonances({1, 2, 3}, {1, 0, 1}, ResonanceKind::dip).empty());  // zu wenige Punkte
}
