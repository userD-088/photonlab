// Phase 9: Zeitbereich der Übertragungsstrecke (PRBS, Modulatoren, Empfängerrauschen, Augendiagramm).
#include <doctest/doctest.h>

#include <cmath>
#include <complex>

#include "photonlab/circuit/registry.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/signal/link.hpp"

using namespace photonlab;
using namespace photonlab::signal;

TEST_CASE("PRBS 2^7-1: Periode 127 mit 64 Einsen") {
  const auto bits = prbs(7, 254);
  int ones = 0;
  for (int i = 0; i < 127; ++i) ones += bits[static_cast<std::size_t>(i)];
  CHECK(ones == 64);
  for (int i = 0; i < 127; ++i)
    CHECK(bits[static_cast<std::size_t>(i)] == bits[static_cast<std::size_t>(i + 127)]);
  // keine kürzere Periode
  for (int p = 1; p < 127; ++p) {
    bool same = true;
    for (int i = 0; i < 127 && same; ++i)
      same = bits[static_cast<std::size_t>(i)] == bits[static_cast<std::size_t>(i + p)];
    CHECK_FALSE(same);
  }
  CHECK_THROWS(prbs(8, 10));
}

TEST_CASE("Bitfehlerrate aus Q: BER(7) = 1,28e-12, BER(6) = 9,9e-10") {
  CHECK(ber_from_q(7.0) == doctest::Approx(1.28e-12).epsilon(0.01).scale(0.0));
  CHECK(ber_from_q(6.0) == doctest::Approx(9.87e-10).epsilon(0.01).scale(0.0));
}

TEST_CASE("Mach-Zehnder ohne Rauschen und Bandbreitengrenzen: Pegel = quasistatische Formel, offenes Auge") {
  LinkSettings s;
  s.modulator = ModulatorKind::mzm;
  s.noise = false;
  s.junction_rc = false;
  s.receiver_bandwidth = 50.0 * s.bit_rate;
  s.dark_current = 0.0;
  const LinkResult r = simulate_link(s);
  const double k0 = 2.0 * constants::pi / s.wavelength;
  const double wg = std::pow(10.0, -s.loss_db_per_cm * 100.0 * s.mzm_length / 20.0);
  auto power = [&](double v) {
    const double v2 = s.v_low + s.v_high - v;
    auto arm = [&](double volt) {
      return wg * std::exp(-0.5 * s.pn.absorption(volt) * s.mzm_length) *
             std::exp(std::complex<double>(0.0, k0 * s.pn.index_change(volt) * s.mzm_length));
    };
    return s.laser_power *
           std::norm(0.5 * (arm(v) + arm(v2) * std::exp(std::complex<double>(0.0, s.mzm_bias_phase))));
  };
  const double hi = std::max(power(s.v_high), power(s.v_low)), lo = std::min(power(s.v_high), power(s.v_low));
  CHECK(std::max(r.mean1, r.mean0) == doctest::Approx(hi).epsilon(1e-3).scale(0.0));
  CHECK(std::min(r.mean1, r.mean0) == doctest::Approx(lo).epsilon(1e-3).scale(0.0));
  CHECK(r.eye_opening > 0.99);
  CHECK(r.extinction_db == doctest::Approx(10.0 * std::log10(hi / lo)).epsilon(1e-3).scale(0.0));
  CHECK(r.q_factor > 1e3);  // ohne Rauschen praktisch unendlich
}

TEST_CASE(
    "Empfängerrauschen: gemessene Streuung und Q stimmen mit Schrot-, thermischem und RIN-Rauschen überein") {
  LinkSettings s;
  s.modulator = ModulatorKind::mzm;
  s.bits = 2000;
  s.prbs_order = 9;
  s.laser_power = 0.2e-3;
  s.rin_db_per_hz = -140.0;
  s.junction_rc = false;
  const LinkResult r = simulate_link(s);
  MESSAGE("Q = " << r.q_factor << ", sigma1 = " << r.sigma1 << " (erwartet " << r.noise_sigma_expected1
                 << "), sigma0 = " << r.sigma0 << " (erwartet " << r.noise_sigma_expected0 << ")");
  CHECK(r.sigma1 == doctest::Approx(r.noise_sigma_expected1).epsilon(0.1).scale(0.0));
  CHECK(r.sigma0 == doctest::Approx(r.noise_sigma_expected0).epsilon(0.1).scale(0.0));
  const double q_expected = std::abs(r.mean1 - r.mean0) / (r.noise_sigma_expected1 + r.noise_sigma_expected0);
  CHECK(r.q_factor == doctest::Approx(q_expected).epsilon(0.1).scale(0.0));
  CHECK(r.ber == doctest::Approx(ber_from_q(r.q_factor)).epsilon(1e-12).scale(0.0));
}

TEST_CASE("Ring-Modulator: Coupled-Mode-Modell stationär = exakter Ring (ring_pn) nahe der Resonanz") {
  LinkSettings s;
  // exakter Ring mit denselben Parametern; Resonanz bei 0 V suchen
  const auto ring =
      circuit::ComponentRegistry::builtin().create("ring_pn", {{"radius", s.ring_radius},
                                                               {"power_coupling", s.ring_coupling},
                                                               {"coverage", s.ring_coverage},
                                                               {"loss_db_per_cm", s.loss_db_per_cm},
                                                               {"n_group", s.n_group},
                                                               {"voltage", 0.0}});
  double res = 0.0, best = 1e9;
  for (double l = 1545e-9; l < 1555e-9; l += 0.02e-12) {
    const double t = std::norm(ring->s_matrix(l)(1, 0));
    if (t < best) best = t, res = l;
  }
  for (const double d : {0.0, 10e-12, -20e-12, 40e-12}) {
    s.wavelength = res + d;
    s.ring_detuning = d;
    const double t_cmt = ring_cmt(s, 0.0).transmission();
    const double t_exact = std::norm(ring->s_matrix(res + d)(1, 0));
    CAPTURE(d);
    CHECK(std::abs(t_cmt - t_exact) < 0.02);
  }
}

TEST_CASE("Ring-Modulator: Photonenlebensdauer begrenzt die Bitrate (Auge schließt bei hoher Rate)") {
  LinkSettings s;
  s.noise = false;
  s.receiver_bandwidth = 200e9;
  s.junction_rc = false;
  s.bit_rate = 5e9;
  const LinkResult slow = simulate_link(s);
  s.bit_rate = 50e9;
  const LinkResult fast = simulate_link(s);
  MESSAGE("Augenöffnung 5 Gbit/s: " << slow.eye_opening << ", 50 Gbit/s: " << fast.eye_opening
                                    << ", Extinktion " << slow.extinction_db << " dB");
  CHECK(slow.eye_opening > 0.8);
  CHECK(fast.eye_opening < slow.eye_opening - 0.2);
  CHECK(slow.extinction_db > 3.0);
}

TEST_CASE("Treiber-Bandbreite: zu langsamer Treiber schließt das Auge des Mach-Zehnders") {
  LinkSettings s;
  s.modulator = ModulatorKind::mzm;
  s.noise = false;
  s.junction_rc = false;
  s.receiver_bandwidth = 100e9;
  s.driver_bandwidth = 3.0 * s.bit_rate;
  const double wide = simulate_link(s).eye_opening;
  s.driver_bandwidth = 0.15 * s.bit_rate;
  const double narrow = simulate_link(s).eye_opening;
  CHECK(wide > 0.95);
  CHECK(narrow < 0.7);
}
