// Phase 8: nichtlinearer Resonator (Kerr, TPA, Ladungsträger) gegen analytische Lösungen.
#include <algorithm>
#include <cmath>
#include <numbers>

#include "doctest/doctest.h"
#include "photonlab/circuit/kerr_cavity.hpp"

using namespace photonlab::circuit;

namespace {
// Reiner Kerr-Resonator mit runden Zahlen: gamma = 2e10 1/s, kappa_in = kappa_0 (kritisch gekoppelt)
KerrCavity plain() {
  KerrCavity c;
  c.omega0 = 1.2e15;
  c.kappa_in = 1e10, c.kappa_0 = 1e10;
  c.kerr = 1e20;  // rad/(s J)
  return c;
}
}  // namespace

TEST_CASE("Kerr-Resonator: linear stimmt mit der Lorentz-Kurve und dem kritisch gekoppelten Minimum") {
  KerrCavity c = plain();
  c.kerr = 0.0;
  const double p = 1e-3, g = c.rate_cold();
  for (const double delta : {0.0, 0.3 * g, 1.7 * g}) {
    const auto s = steady_states(c, c.omega0 - delta, p);
    REQUIRE(s.size() == 1);
    CHECK(s[0].energy ==
          doctest::Approx(c.kappa_in * p / (delta * delta + 0.25 * g * g)).epsilon(1e-9).scale(0.0));
  }
  // kritische Kopplung: bei Resonanz kein Durchgang
  CHECK(steady_states(c, c.omega0, p)[0].transmission ==
        doctest::Approx(0.0).epsilon(1e-9).scale(0.0).scale(1.0));
  // unterkoppelt: T = ((k0 - kin)/(k0 + kin))^2
  c.kappa_in = 0.25e10;
  const double expect = std::pow((c.kappa_0 - c.kappa_in) / (c.kappa_0 + c.kappa_in), 2);
  CHECK(steady_states(c, c.omega0, p)[0].transmission == doctest::Approx(expect).epsilon(1e-9).scale(0.0));
}

TEST_CASE(
    "Kerr-Resonator: Bistabilität beginnt bei Verstimmung sqrt(3)*gamma/2 und kritischer Leistung P_c") {
  const KerrCavity c = plain();
  const double g = c.rate_cold();
  const double p_c = kerr_critical_power(c);
  auto count_at = [&](double delta_over_g, double power) {
    return steady_states(c, c.omega0 - delta_over_g * g, power).size();
  };
  // unter der Schwelle (delta < sqrt(3)/2 gamma = 0,866 gamma): bei jeder Leistung genau ein Zustand
  for (const double p : {0.1 * p_c, p_c, 3.0 * p_c, 30.0 * p_c}) CHECK(count_at(0.8, p) == 1);
  // darüber: es gibt Leistungen mit drei Zuständen, mit zwei stabilen und einem instabilen
  bool found_three = false;
  for (const double f : {1.0, 1.2, 1.4, 1.6, 2.0, 3.0, 4.0, 6.0}) {
    const auto s = steady_states(c, c.omega0 - 1.2 * g, f * p_c);
    if (s.size() == 3) {
      found_three = true;
      CHECK(s[0].stable);
      CHECK_FALSE(s[1].stable);
      CHECK(s[2].stable);
    }
  }
  CHECK(found_three);
  // genau an der Schwelle liegt der Wendepunkt bei P_c: Steigung dP/dU = 0 bei U_c = 2 delta / (3 kerr)
  const double delta_c = std::sqrt(3.0) * 0.5 * g;
  const double u_c = 2.0 * delta_c / (3.0 * c.kerr);
  CHECK(steady_power(c, c.omega0 - delta_c, u_c) == doctest::Approx(p_c).epsilon(1e-9).scale(0.0));
}

TEST_CASE("Kerr-Resonator: Klapp-Leistungen bei delta = 2 g/2 folgen aus der Kubik") {
  // x = kerr*U; Extrema von f(x) = x((D-x)^2 + g^2), g = gamma/2: x = (2D +- sqrt(D^2 - 3 g^2))/3
  const KerrCavity c = plain();
  const double half = 0.5 * c.rate_cold(), delta = 2.0 * half;
  const double root = std::sqrt(delta * delta - 3.0 * half * half);
  auto f = [&](double x) { return x * ((delta - x) * (delta - x) + half * half) / (c.kerr * c.kappa_in); };
  const double p_up =
      f((2.0 * delta + root) / 3.0 * 0 + (2.0 * delta - root) / 3.0);  // lokales Maximum von P(U)
  const double p_down = f((2.0 * delta + root) / 3.0);                 // lokales Minimum
  REQUIRE(p_up > p_down);
  const double mid = 0.5 * (p_up + p_down);
  CHECK(steady_states(c, c.omega0 - delta, mid).size() == 3);
  CHECK(steady_states(c, c.omega0 - delta, 1.02 * p_up).size() == 1);
  CHECK(steady_states(c, c.omega0 - delta, 0.98 * p_down).size() == 1);
  CHECK(steady_states(c, c.omega0 - delta, 1.02 * p_down).size() == 3);
  CHECK(steady_states(c, c.omega0 - delta, 0.98 * p_up).size() == 3);
}

TEST_CASE("Kerr-Resonator: Zeitverlauf läuft in den stationären Zustand und zeigt die Hysterese") {
  const KerrCavity c = plain();
  const double g = c.rate_cold(),
               delta = 4.0 * 0.5 * g;  // breite Schleife: Klapp-Leistungen weit auseinander
  const double omega_l = c.omega0 - delta;
  const double p_c = kerr_critical_power(c);
  // konstante Leistung unterhalb der Klappschwelle: stationärer Wert der unteren Lösung
  const double p = 0.5 * p_c;
  const auto s = steady_states(c, omega_l, p);
  REQUIRE(s.size() == 1);
  const Trace t = simulate(c, omega_l, [&](double) { return p; }, 40.0 / g, 0.02 / g);
  CHECK(t.energy.back() == doctest::Approx(s[0].energy).epsilon(1e-4).scale(0.0));
  // langsame Rampe: hinauf klappt der Zustand bei der oberen Klapp-Leistung, hinunter bei der unteren
  const double half = 0.5 * g, root = std::sqrt(delta * delta - 3.0 * half * half);
  auto f = [&](double x) { return x * ((delta - x) * (delta - x) + half * half) / (c.kerr * c.kappa_in); };
  const double p_up = f((2.0 * delta - root) / 3.0), p_down = f((2.0 * delta + root) / 3.0);
  const double p_max = 2.0 * p_up;
  const HysteresisLoop loop = hysteresis_sweep(c, omega_l, p_max, 2000.0 / g, 1000);
  auto jump_power = [](const Trace& tr, bool upward) {
    double best = 0.0, at = 0.0;
    for (std::size_t i = 1; i < tr.energy.size(); ++i) {
      const double d = std::abs(tr.energy[i] - tr.energy[i - 1]);
      if (d > best) best = d, at = tr.power[i];
    }
    (void)upward;
    return at;
  };
  CHECK(jump_power(loop.up, true) == doctest::Approx(p_up).epsilon(0.05).scale(0.0));
  CHECK(jump_power(loop.down, false) == doctest::Approx(p_down).epsilon(0.08).scale(0.0));
  // Hysterese: bei P zwischen p_down und p_up ist der Durchgang auf dem Hin- und Rückweg verschieden
  const double probe = 0.5 * (p_up + p_down);
  auto at_power = [&](const Trace& tr) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < tr.power.size(); ++i)
      if (std::abs(tr.power[i] - probe) < std::abs(tr.power[best] - probe)) best = i;
    return tr.energy[best];
  };
  CHECK(at_power(loop.down) > 2.0 * at_power(loop.up));
}

TEST_CASE(
    "Silizium-Resonator: Parameter aus Material und Güte, TPA begrenzt die Energie, Ladungsträger "
    "verschieben blau") {
  // Ring 10 um Radius: V_eff = A_eff * L, A_eff = 0,1 um^2, L = 62,8 um -> V = 6,3 um^3
  const double wavelength = 1550e-9, volume = 0.1e-12 * 2.0 * std::numbers::pi * 10e-6;
  const KerrCavity c = silicon_cavity(wavelength, 2e5, 2e5, 4.2, volume);
  CHECK(c.kerr ==
        doctest::Approx(c.omega0 * 4.5e-18 * 299792458.0 / (4.2 * 4.2 * volume)).epsilon(1e-12).scale(0.0));
  CHECK(c.tpa > 0.0);
  CHECK(c.carrier_gen > 0.0);
  // Mit TPA ist die Energie bei hoher Leistung kleiner als ohne
  KerrCavity no_tpa = c;
  no_tpa.tpa = 0.0, no_tpa.carrier_gen = 0.0, no_tpa.carrier_loss = 0.0, no_tpa.carrier_shift = 0.0;
  const double p = 5e-3;
  const auto with = steady_states(c, c.omega0, p), without = steady_states(no_tpa, c.omega0, p);
  REQUIRE(!with.empty());
  REQUIRE(!without.empty());
  CHECK(with.back().energy < without.back().energy);
  // Ladungsträger im Gleichgewicht: N = gen U^2 tau, Verschiebung nach Blau
  const auto s = with.back();
  CHECK(s.carriers ==
        doctest::Approx(c.carrier_gen * s.energy * s.energy * c.carrier_tau).epsilon(1e-9).scale(0.0));
  CHECK(c.frequency(s.energy, s.carriers) > c.frequency(s.energy, 0.0));
}
