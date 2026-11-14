// Phase 9: aktive Bauteile (Heizer, PN-Phasenschieber, Ring-Modulator, Photodiode) gegen Formeln und Literatur.
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "photonlab/circuit/active_devices.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/materials/silicon_carriers.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {
const ComponentRegistry& reg() { return ComponentRegistry::builtin(); }
const double lambda0 = 1550e-9;
double phase_of(const Component& c, double lambda) { return std::arg(c.s_matrix(lambda)(1, 0)); }
double wrap(double x) { return std::remainder(x, 2.0 * constants::pi); }
}  // namespace

TEST_CASE("Soref-Bennett: Brechzahl- und Absorptionsänderung bei 1e18 cm^-3") {
  using materials::plasma_absorption;
  using materials::plasma_index_change;
  CHECK(plasma_index_change(1e24, 0.0) == doctest::Approx(-8.8e-4).epsilon(1e-12).scale(0.0));
  CHECK(plasma_index_change(0.0, 1e24) == doctest::Approx(-8.5e-18 * std::pow(1e18, 0.8)).epsilon(1e-12).scale(0.0));
  CHECK(plasma_index_change(0.0, -1e24) == doctest::Approx(8.5e-18 * std::pow(1e18, 0.8)).epsilon(1e-12).scale(0.0));
  CHECK(plasma_absorption(1e24, 0.0) == doctest::Approx(8.5 * 100.0).epsilon(1e-12).scale(0.0));  // 8,5 /cm
  CHECK(plasma_absorption(0.0, 1e24) == doctest::Approx(6.0 * 100.0).epsilon(1e-12).scale(0.0));
}

TEST_CASE("PN-Übergang: Diffusionsspannung, Verarmungszone und Kapazität nach der Verarmungsnäherung") {
  materials::PnJunction pn;  // 5e17 cm^-3 beidseitig
  const double vt = 1.380649e-23 * 300.0 / constants::q;
  CHECK(pn.built_in_voltage() == doctest::Approx(vt * std::log(2.5e47 / 1e32)).epsilon(1e-12).scale(0.0));
  CHECK(pn.built_in_voltage() == doctest::Approx(0.917).epsilon(0.01).scale(0.0));
  const double eps = 11.7 * constants::eps0;
  const double w = std::sqrt(2.0 * eps * (pn.built_in_voltage() + 2.0) * 1e24 / (constants::q * 2.5e47));
  CHECK(pn.depletion_width(2.0) == doctest::Approx(w).epsilon(1e-12).scale(0.0));
  CHECK(pn.width_n(2.0) + pn.width_p(2.0) == doctest::Approx(w).epsilon(1e-12).scale(0.0));
  CHECK(pn.capacitance_per_area(2.0) == doctest::Approx(eps / w).epsilon(1e-12).scale(0.0));
  // w ~ sqrt(V_bi + V): bei 4 V etwa 2,3-mal so breit wie bei 0 V
  CHECK(pn.depletion_width(4.0) / pn.depletion_width(0.0) ==
        doctest::Approx(std::sqrt((pn.built_in_voltage() + 4.0) / pn.built_in_voltage())).epsilon(1e-12).scale(0.0));
}

TEST_CASE("PN-Phasenschieber: V_pi*L im Bereich gemessener Verarmungs-Modulatoren (1 bis 4 V cm)") {
  const auto shifter = [&](double v) { return reg().create("pn_phase_shifter", {{"voltage", v}, {"length", 2e-3}}); };
  const double phi0 = phase_of(*shifter(0.0), lambda0);
  auto dphi = [&](double v) { return std::abs(wrap(phase_of(*shifter(v), lambda0) - phi0)); };
  // Phase steigt mit der Sperrspannung (Verarmung erhöht n); V_pi über die Formel aus PnSection
  PnSection pn;
  auto shift = [&](double v) { return 2.0 * constants::pi / lambda0 * (pn.index_change(v) - pn.index_change(0.0)) * 2e-3; };
  for (const double v : {0.5, 1.0, 2.0}) CHECK(dphi(v) == doctest::Approx(shift(v)).epsilon(1e-9).scale(0.0));
  // V_pi * L: Spannung, bei der ein Phasenschieber der Länge L gerade pi erreicht; L so, dass V_pi = 4 V
  const double per_length = (pn.index_change(4.0) - pn.index_change(0.0)) * 2.0 * constants::pi / lambda0;  // rad/m
  const double l_pi = constants::pi / per_length;
  const double vpil_vcm = 4.0 * l_pi * 100.0;
  MESSAGE("V_pi L = " << vpil_vcm << " V cm (bei 4 V), L_pi = " << l_pi * 1e3 << " mm");
  CHECK(vpil_vcm > 1.0);
  CHECK(vpil_vcm < 4.0);
  // Kapazität und RC-Grenzfrequenz als Messwerte
  const auto m = shifter(2.0)->measure(CVector::Zero(2), CVector::Zero(2));
  CHECK(m[1].name == "Kapazität");
  CHECK(m[1].value == doctest::Approx(pn.capacitance_per_length(2.0) * 2e-3).epsilon(1e-12).scale(0.0));
  CHECK(m[2].value == doctest::Approx(1.0 / (2.0 * constants::pi * 50.0 * m[1].value)).epsilon(1e-12).scale(0.0));
  // Verarmung senkt den Verlust
  CHECK(pn.absorption(4.0) < pn.absorption(0.0));
}

TEST_CASE("Heizer: P_pi = 24,8 mW für 61,6 µm (Harris 2014) und lineare Phase") {
  const auto heater = [&](double p) { return reg().create("heater", {{"power_el", p}}); };
  const double phi0 = phase_of(*heater(0.0), lambda0);
  CHECK(std::abs(wrap(phase_of(*heater(24.77e-3), lambda0) - phi0)) == doctest::Approx(constants::pi).epsilon(1e-6).scale(0.0));
  CHECK(std::abs(wrap(phase_of(*heater(10e-3), lambda0) - phi0)) ==
        doctest::Approx(constants::pi * 10.0 / 24.77).epsilon(1e-6).scale(0.0));
  // Temperaturerhöhung bei P_pi: lambda/(2 L dn/dT) = 1550 nm / (2 * 61,6 µm * 1,8e-4) ~ 70 K
  const auto m = heater(24.77e-3)->measure(CVector::Zero(2), CVector::Zero(2));
  CHECK(m[0].value == doctest::Approx(1550e-9 / (2.0 * 61.6e-6 * 1.8e-4)).epsilon(0.02).scale(0.0));
}

TEST_CASE("Ring-Modulator: Sperrspannung verschiebt die Resonanz um lambda * dn_eff * Anteil / n_g") {
  const ParamMap base = {{"radius", 10e-6}, {"power_coupling", 0.02}, {"coverage", 0.8}};
  auto minimum = [&](double v) {
    ParamMap p = base;
    p["voltage"] = v;
    const auto ring = reg().create("ring_pn", p);
    double best = 1e9, at = 0.0;
    for (double l = 1545e-9; l < 1555e-9; l += 0.05e-12) {  // feines Raster: 50 fm
      const double t = std::norm(ring->s_matrix(l)(1, 0));
      if (t < best) best = t, at = l;
    }
    return at;
  };
  const double shift = minimum(2.0) - minimum(0.0);
  PnSection pn;
  // Resonanzbedingung n(lambda) L / lambda = m: d(lambda) = lambda * dn_eff * Anteil / n_g
  const double expected = 1550e-9 * (pn.index_change(2.0) - pn.index_change(0.0)) * 0.8 / 4.2;
  MESSAGE("Resonanzverschiebung 0 -> 2 V: " << shift * 1e12 << " pm, erwartet " << expected * 1e12 << " pm");
  CHECK(shift > 0.0);  // Rotverschiebung
  CHECK(shift == doctest::Approx(expected).epsilon(0.03).scale(0.0));
}

TEST_CASE("Photodiode: Photostrom, Schrot- und thermisches Rauschen, Signal-Rausch-Verhältnis") {
  const auto pd = reg().create("photodiode", {});
  CVector a(1);
  a(0) = std::sqrt(100e-6);  // 100 µW
  const auto m = pd->measure(a, CVector::Zero(1));
  const double i = 100e-6 * 1.0 + 10e-9, b = 30e9;
  CHECK(m[1].value == doctest::Approx(i).epsilon(1e-12).scale(0.0));
  CHECK(m[2].value == doctest::Approx(std::sqrt(2.0 * constants::q * i * b)).epsilon(1e-12).scale(0.0));
  const double thermal = std::sqrt(4.0 * 1.380649e-23 * 300.0 * b / 50.0);
  CHECK(m[3].value == doctest::Approx(thermal).epsilon(1e-12).scale(0.0));
  const double snr = 10.0 * std::log10(1e-8 / (2.0 * constants::q * i * b + thermal * thermal));
  CHECK(m[4].value == doctest::Approx(snr).epsilon(1e-9).scale(0.0));
}
