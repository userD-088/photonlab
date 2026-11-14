// Phase 8: nichtlineare Bauteile in der Schaltung (Kerr-Ring mit TPA und Thermik, SOA) gegen geschlossene
// Lösungen und gegen das Coupled-Mode-Kompaktmodell.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "photonlab/circuit/kerr_cavity.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/circuit/sweep.hpp"
#include "photonlab/core/units.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {
const ComponentRegistry& reg() { return ComponentRegistry::builtin(); }
const double lambda0 = 1550 * units::nm;
const double radius = 10 * units::um, coupling = 0.02, loss_db_per_cm = 2.0, n_eff = 2.44, n_group = 4.2;
const double ring_length = 2.0 * constants::pi * radius;
const double t = std::sqrt(1.0 - coupling);
const double a_rt = std::pow(10.0, -loss_db_per_cm*(ring_length / 1e-2) / 20.0);  // Amplitude je Umlauf

// Laser -> Ring -> Detektor
Circuit ring_circuit(const ParamMap& ring_params, double power) {
  Circuit c;
  c.add("laser", "laser", {{"power", power}});
  c.add("ring", "ring_kerr", ring_params);
  c.add("det", "detector");
  c.connect("laser.out", "ring.in");
  c.connect("ring.out", "det.in");
  return c;
}

double linear_phase(double lambda) {
  const double n = n_eff + (n_eff - n_group) * (lambda - lambda0) / lambda0;
  return 2.0 * constants::pi * n * ring_length / lambda;
}

// Abstimmphase, die bei lambda0 eine Verstimmung um delta (rad je Umlauf, > 0 = unterhalb der Resonanz)
// ergibt
double tuning_for(double delta) {
  const double phi = std::fmod(linear_phase(lambda0), 2.0 * constants::pi);
  return -phi - delta;
}

double rel(double a, double b) { return std::abs(a - b) / std::max(std::abs(a), std::abs(b)); }
}  // namespace

TEST_CASE("Kerr-Ring: ohne Nichtlinearität identisch mit dem linearen All-Pass-Ring") {
  const ParamMap common = {
      {"radius", radius}, {"power_coupling", coupling}, {"loss_db_per_cm", loss_db_per_cm}};
  ParamMap kerr = common;
  kerr["n2"] = 0.0, kerr["beta_tpa"] = 0.0, kerr["thermal_resistance"] = 0.0;
  const auto lin = reg().create("ring_allpass", common);
  const auto nl = reg().create("ring_kerr", kerr);
  CHECK(nl->state_size() == 2);
  for (double lambda = 1545e-9; lambda < 1555e-9; lambda += 0.37e-9) {
    const CMatrix s1 = lin->s_matrix(lambda), s2 = nl->s_matrix(lambda);
    CHECK(std::abs(s1(1, 0) - s2(1, 0)) < 1e-12);
    CHECK(std::abs(s1(0, 1) - s2(0, 1)) < 1e-12);
  }
  // Schaltung mit hoher Leistung: ohne Nichtlinearität bleibt der Durchgang gleich
  const CompiledCircuit c1(ring_circuit(kerr, 1.0), reg());
  const Solution sol = c1.solve(lambda0);
  const double p_out = std::norm(sol.a(c1.port_index("det", "in")));
  CHECK(p_out == doctest::Approx(std::norm(lin->s_matrix(lambda0)(1, 0)) * 1.0).epsilon(1e-9).scale(0.0));
}

TEST_CASE("Kerr-Ring: Klappleistungen der Rampe = Extrema der geschlossenen Ringgleichung = Kompaktmodell") {
  // Halbe Linienbreite in Umlaufphase: h = (1 - t a)/sqrt(t a); Verstimmung 3 h (> sqrt(3) h)
  const double h = (1.0 - t * a_rt) / std::sqrt(t * a_rt);
  const double delta = 3.0 * h;
  ParamMap params = {{"radius", radius},
                     {"power_coupling", coupling},
                     {"loss_db_per_cm", loss_db_per_cm},
                     {"tuning_phase", tuning_for(delta)},
                     {"beta_tpa", 0.0},
                     {"thermal_resistance", 0.0}};
  const double n2 = 4.5e-18, area = 0.1e-12;
  // Ringgleichung explizit: P_in(P) = P |1 - t a exp(i(-delta + k0 n2 P L/A))|^2 / kappa^2
  const double k0 = 2.0 * constants::pi / lambda0;
  auto p_in_of = [&](double p) {
    const cplx hh = a_rt * expi(-delta + k0 * n2 * p / area * ring_length);
    return p * std::norm(1.0 - t * hh) / coupling;
  };
  double p_up = 0.0, p_down = 0.0, prev = 0.0, prev_slope = 1.0;
  bool have_up = false, have_down = false;
  for (int i = 1; i <= 200000; ++i) {
    const double p = 20.0 * i / 200000.0;  // umlaufende Leistung bis 20 W
    const double pin = p_in_of(p), d = pin - prev;
    if (i > 1 && prev_slope > 0.0 && d < 0.0 && !have_up) p_up = prev, have_up = true;
    if (i > 1 && prev_slope < 0.0 && d > 0.0 && !have_down) p_down = prev, have_down = true;
    prev = pin, prev_slope = d;
  }
  REQUIRE(have_up);
  REQUIRE(have_down);

  // Rampe in der Schaltung: Laser 1 W nominell, Quellfaktor = sqrt(P/1 W)
  const CompiledCircuit circuit(ring_circuit(params, 1.0), reg());
  REQUIRE(circuit.nonlinear());
  const double p_max = 1.4 * p_up;
  const int steps = 600;
  std::vector<double> up, down;
  for (int i = 0; i <= steps; ++i) up.push_back(std::sqrt(p_max * i / steps));
  for (int i = steps; i >= 0; --i) down.push_back(std::sqrt(p_max * i / steps));
  CircuitState state = circuit.zero_state();
  const RampResult r_up = power_ramp(circuit, lambda0, up, &state);
  const RampResult r_down = power_ramp(circuit, lambda0, down, &state);
  auto jump = [&](const RampResult& r, const std::vector<double>& scales) {
    double best = 0.0, at = 0.0;
    for (std::size_t i = 1; i < r.states.size(); ++i) {
      const double d = std::abs(r.states[i][1][0] - r.states[i - 1][1][0]);
      if (d > best) best = d, at = scales[i] * scales[i];
    }
    return at;
  };
  const double step = p_max / steps;
  CHECK(std::abs(jump(r_up, up) - p_up) <= 1.5 * step);
  CHECK(std::abs(jump(r_down, down) - p_down) <= 1.5 * step);
  // Hysterese: in der Mitte der Schleife ist die umlaufende Leistung auf dem Rückweg viel größer
  const double mid = 0.5 * (p_up + p_down);
  const std::size_t i_up = static_cast<std::size_t>(std::lround(mid / step));
  const std::size_t i_down = static_cast<std::size_t>(steps) - i_up;
  CHECK(r_down.states[i_down][1][0] > 3.0 * r_up.states[i_up][1][0]);  // Instanz 1 = Ring

  // Kompaktmodell (Coupled-Mode) mit T_rt = n_g L/c: kappa_in = kappa^2/T_rt, kappa_0 = (1 - a^2)/T_rt,
  // Verstimmung delta/T_rt, Kerr beta = omega n2 /(n_g A T_rt)
  const double t_rt = n_group * ring_length / constants::c0;
  KerrCavity cmt;
  cmt.omega0 = 2.0 * constants::pi * constants::c0 / lambda0;
  cmt.kappa_in = coupling / t_rt;
  cmt.kappa_0 = (1.0 - a_rt * a_rt) / t_rt;
  cmt.kerr = cmt.omega0 * n2 / (n_group * area * t_rt);
  const double omega_l = cmt.omega0 - delta / t_rt;
  double c_up = 0.0, c_down = 0.0;
  prev = 0.0, prev_slope = 1.0, have_up = have_down = false;
  const double u_max = 3.0 * (delta / t_rt) / cmt.kerr;
  for (int i = 1; i <= 200000; ++i) {
    const double pin = steady_power(cmt, omega_l, u_max * i / 200000.0), d = pin - prev;
    if (i > 1 && prev_slope > 0.0 && d < 0.0 && !have_up) c_up = prev, have_up = true;
    if (i > 1 && prev_slope < 0.0 && d > 0.0 && !have_down) c_down = prev, have_down = true;
    prev = pin, prev_slope = d;
  }
  MESSAGE("Ring: hinauf " << p_up << " W, hinunter " << p_down << " W; Kompaktmodell " << c_up << " / "
                          << c_down);
  CHECK(rel(c_up, p_up) < 0.05);
  CHECK(rel(c_down, p_down) < 0.05);
}

TEST_CASE("Kerr-Ring: TPA begrenzt die umlaufende Leistung, Gegenrichtung wirkt doppelt") {
  ParamMap params = {{"radius", radius},
                     {"power_coupling", coupling},
                     {"loss_db_per_cm", loss_db_per_cm},
                     {"tuning_phase", tuning_for(0.0)},
                     {"thermal_resistance", 0.0},
                     {"n2", 0.0}};
  const auto ring = reg().create("ring_kerr", params);
  params["beta_tpa"] = 0.0;
  const auto ring_no_tpa = reg().create("ring_kerr", params);
  CVector a = CVector::Zero(2);
  a(0) = std::sqrt(0.1);  // 100 mW
  const auto with = ring->settle_state(lambda0, a, {0.0, 0.0});
  const auto without = ring_no_tpa->settle_state(lambda0, a, {0.0, 0.0});
  CHECK(with[0] < 0.8 * without[0]);
  CHECK(with[1] == 0.0);
  // Selbstkonsistenz: P = kappa^2 |a|^2 / |1 - t H(P)|^2
  const CMatrix s = ring->s_matrix_at(lambda0, with);
  const cplx tau = s(1, 0);
  const cplx h = (t - tau) / (1.0 - t * tau);  // aus tau = (t - H)/(1 - tH) zurückgerechnet
  CHECK(with[0] == doctest::Approx(coupling * 0.1 / std::norm(1.0 - t * h)).epsilon(1e-8).scale(0.0));
}

TEST_CASE("Kerr-Ring mit Erwärmung: Wellenlängen-Rampe zeigt das Thermik-Dreieck (hin anders als zurück)") {
  ParamMap params = {{"radius", radius},
                     {"power_coupling", coupling},
                     {"loss_db_per_cm", loss_db_per_cm},
                     {"tuning_phase", tuning_for(0.0)},
                     {"n2", 0.0},
                     {"beta_tpa", 0.0},
                     {"thermal_resistance", 5e3}};
  const CompiledCircuit circuit(ring_circuit(params, 5e-3), reg());
  std::vector<double> forward, backward;
  for (int i = 0; i <= 800; ++i) forward.push_back(lambda0 - 0.2e-9 + 0.6e-9 * i / 800);
  backward.assign(forward.rbegin(), forward.rend());
  const RampResult f = wavelength_ramp(circuit, forward);
  const RampResult b = wavelength_ramp(circuit, backward);
  const int det = circuit.port_index("det", "in");
  auto minimum = [&](const RampResult& r, const std::vector<double>& w) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < r.solutions.size(); ++i)
      if (std::norm(r.solutions[i].a(det)) < std::norm(r.solutions[best].a(det))) best = i;
    return w[best];
  };
  const double min_f = minimum(f, forward), min_b = minimum(b, backward);
  MESSAGE("Minimum hin " << min_f * 1e9 << " nm, zurück " << min_b * 1e9 << " nm");
  // Erwärmung schiebt die Resonanz zu längeren Wellen; auf dem Hinweg wird sie mitgezogen
  CHECK(min_f > lambda0 + 0.02e-9);
  CHECK(min_f > min_b + 0.02e-9);
}

TEST_CASE("SOA: Verstärkung sättigt nach G = G0 exp(-(G-1)P/P_sat); 3-dB-Ausgangsleistung analytisch") {
  const double g0_db = 20.0, p_sat = 10e-3, g0 = 100.0;
  // P_in, bei der G = G0/2 ist: P = P_sat ln2 /(G0/2 - 1); P_out = P_sat G0 ln2 /(G0 - 2)
  const double p_in = p_sat * std::log(2.0) / (g0 / 2.0 - 1.0);
  Circuit c;
  c.add("laser", "laser", {{"power", p_in}});
  c.add("soa", "soa", {{"gain_db", g0_db}, {"saturation_power", p_sat}});
  c.add("det", "detector");
  c.connect("laser.out", "soa.in");
  c.connect("soa.out", "det.in");
  const CompiledCircuit compiled(c, reg());
  REQUIRE(compiled.nonlinear());
  const Solution sol = compiled.solve(lambda0);
  const double p_out = std::norm(sol.a(compiled.port_index("det", "in")));
  CHECK(p_out == doctest::Approx(p_sat * g0 * std::log(2.0) / (g0 - 2.0)).epsilon(1e-6).scale(0.0));
  // Kleinsignal: volle Verstärkung
  c.find_instance("laser")->params["power"] = 1e-9;
  const CompiledCircuit small(c, reg());
  const Solution s2 = small.solve(lambda0);
  CHECK(std::norm(s2.a(small.port_index("det", "in"))) ==
        doctest::Approx(1e-9 * g0).epsilon(1e-4).scale(0.0));  // bei 1 nW sättigt es um 1e-5
  // Messwert Verstärkung in dB
  bool found = false;
  for (const auto& m : compiled.measurements(sol))
    if (m.instance == "soa" && m.measurement.name == "Verstärkung") {
      found = true;
      CHECK(m.measurement.value == doctest::Approx(g0_db - 10.0 * std::log10(2.0)).epsilon(1e-6).scale(0.0));
    }
  CHECK(found);
}

TEST_CASE("Nichtlineare Schaltung mit Rückkopplung: zwei Kerr-Ringe hintereinander konvergieren") {
  const double h = (1.0 - t * a_rt) / std::sqrt(t * a_rt);
  ParamMap params = {{"radius", radius},
                     {"power_coupling", coupling},
                     {"loss_db_per_cm", loss_db_per_cm},
                     {"tuning_phase", tuning_for(1.0 * h)},
                     {"thermal_resistance", 0.0}};
  Circuit c;
  c.add("laser", "laser", {{"power", 0.05}});
  c.add("r1", "ring_kerr", params);
  c.add("r2", "ring_kerr", params);
  c.add("det", "detector");
  c.connect("laser.out", "r1.in");
  c.connect("r1.out", "r2.in");
  c.connect("r2.out", "det.in");
  const CompiledCircuit compiled(c, reg());
  CircuitState state = compiled.zero_state();
  int iterations = 0;
  const Solution sol = compiled.solve(lambda0, state, nullptr, 1.0, &iterations);
  CHECK(iterations < 50);
  // Ergebnis = zwei unabhängig gerechnete Ringe hintereinander
  const auto ring = reg().create("ring_kerr", params);
  CVector a = CVector::Zero(2);
  a(0) = std::sqrt(0.05);
  const auto s1 = ring->settle_state(lambda0, a, {0.0, 0.0});
  const cplx out1 = ring->s_matrix_at(lambda0, s1)(1, 0) * a(0);
  a(0) = out1;
  const auto s2 = ring->settle_state(lambda0, a, {0.0, 0.0});
  const cplx out2 = ring->s_matrix_at(lambda0, s2)(1, 0) * out1;
  CHECK(std::norm(sol.a(compiled.port_index("det", "in"))) ==
        doctest::Approx(std::norm(out2)).epsilon(1e-8).scale(0.0));
}
