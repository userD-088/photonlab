#include "photonlab/signal/link.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

#include "photonlab/circuit/monte_carlo.hpp"
#include "photonlab/core/units.hpp"

namespace photonlab::signal {

namespace {
using cplx = std::complex<double>;
constexpr double boltzmann = 1.380649e-23;

// Erster-Ordnung-Tiefpass als exakte Diskretisierung: y += (1 - exp(-dt/tau)) (x - y)
struct Pole {
  double k = 1.0;
  double y = 0.0;
  bool active = false;
  void set(double bandwidth, double dt) {
    active = bandwidth > 0.0;
    if (active) k = 1.0 - std::exp(-2.0 * constants::pi * bandwidth * dt);
  }
  double operator()(double x) {
    if (!active) return x;
    y += k * (x - y);
    return y;
  }
};

double amplitude_from_db_per_m(double db_per_m, double length) {
  return std::pow(10.0, -db_per_m * length / 20.0);
}
}  // namespace

std::vector<int> prbs(int order, int count) {
  int tap = 0;
  switch (order) {
    case 7:
      tap = 6;
      break;
    case 9:
      tap = 5;
      break;
    case 11:
      tap = 9;
      break;
    case 15:
      tap = 14;
      break;
    default:
      throw std::invalid_argument("PRBS-Ordnung muss 7, 9, 11 oder 15 sein");
  }
  std::uint32_t state = (1u << order) - 1u;  // alle Einsen
  std::vector<int> bits;
  bits.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    const std::uint32_t bit = ((state >> (order - 1)) ^ (state >> (tap - 1))) & 1u;
    state = ((state << 1) | bit) & ((1u << order) - 1u);
    bits.push_back(static_cast<int>(bit));
  }
  return bits;
}

double ber_from_q(double q) { return 0.5 * std::erfc(q / std::sqrt(2.0)); }

double RingCmt::transmission() const {
  const double gamma = kappa_in + kappa_0;
  return std::norm(1.0 - kappa_in / cplx(0.5 * gamma, detuning));
}

RingCmt ring_cmt(const LinkSettings& s, double voltage) {
  const double length = 2.0 * constants::pi * s.ring_radius;
  const double t_rt = s.n_group * length / constants::c0;
  const double l_pn = s.ring_coverage * length;
  const double a2 = std::pow(amplitude_from_db_per_m(s.loss_db_per_cm * 100.0, length), 2) *
                    std::exp(-s.pn.absorption(voltage) * l_pn);
  const double omega_l = 2.0 * constants::pi * constants::c0 / s.wavelength;
  // Resonanz bei v_low liegt um ring_detuning unter der Laserwellenlänge; Spannung verschiebt sie mit
  // d(lambda) = lambda * dn_eff * Anteil / n_g (Rotverschiebung = kleinere Kreisfrequenz)
  const double lambda_res0 = s.wavelength - s.ring_detuning;
  const double dlambda =
      lambda_res0 * (s.pn.index_change(voltage) - s.pn.index_change(s.v_low)) * s.ring_coverage / s.n_group;
  const double omega_res = 2.0 * constants::pi * constants::c0 / (lambda_res0 + dlambda);
  RingCmt r;
  r.kappa_in = s.ring_coupling / t_rt;
  r.kappa_0 = (1.0 - a2) / t_rt;
  r.detuning = omega_res - omega_l;
  return r;
}

LinkResult simulate_link(const LinkSettings& s, JobControl* control) {
  if (!(s.bit_rate > 0.0) || s.samples_per_bit < 4 || s.bits < 32)
    throw std::invalid_argument("Bitrate > 0, mindestens 4 Abtastungen je Bit und 32 Bits nötig");
  LinkResult r;
  const int spb = s.samples_per_bit;
  const double dt = 1.0 / (s.bit_rate * spb);
  r.sample_interval = dt;
  r.bits = prbs(s.prbs_order, s.bits);
  const std::size_t n = static_cast<std::size_t>(s.bits) * static_cast<std::size_t>(spb);

  // Treiber: Rechteck -> Treiberpol -> RC-Pol der Sperrschicht (Kapazität beim mittleren Arbeitspunkt)
  Pole driver, junction;
  driver.set(s.driver_bandwidth, dt);
  const double v_mid = 0.5 * (s.v_low + s.v_high);
  const double length_pn = s.modulator == ModulatorKind::mzm
                               ? s.mzm_length
                               : s.ring_coverage * 2.0 * constants::pi * s.ring_radius;
  const double capacitance = s.pn.capacitance_per_length(v_mid) * length_pn;
  if (s.junction_rc) junction.set(1.0 / (2.0 * constants::pi * s.series_resistance * capacitance), dt);
  driver.y = junction.y = r.bits.front() ? s.v_high : s.v_low;

  // Modulator-Konstanten
  const double k0 = 2.0 * constants::pi / s.wavelength;
  const double wg_amp = amplitude_from_db_per_m(s.loss_db_per_cm * 100.0, s.mzm_length);
  const double link_amp = std::pow(10.0, -s.link_loss_db / 20.0);
  // Ring: Zustand a (Feld im Ring, |a|^2 = Energie), Start im stationären Zustand des ersten Bits
  const double omega_l = 2.0 * constants::pi * constants::c0 / s.wavelength;
  (void)omega_l;
  cplx a = 0.0;
  {
    const RingCmt c = ring_cmt(s, driver.y);
    a = std::sqrt(c.kappa_in) * std::sqrt(s.laser_power) / cplx(0.5 * (c.kappa_in + c.kappa_0), c.detuning);
  }

  // Empfänger: zwei gleiche Pole mit Gesamtbandbreite f_rx: f_pol = f_rx / sqrt(sqrt(2) - 1)
  const double f_rx = s.receiver_bandwidth > 0.0 ? s.receiver_bandwidth : 0.7 * s.bit_rate;
  const double f_pole = f_rx / std::sqrt(std::sqrt(2.0) - 1.0);
  Pole rx1, rx2;
  rx1.set(f_pole, dt);
  rx2.set(f_pole, dt);
  r.noise_bandwidth = constants::pi * f_pole / 4.0;  // Rauschbandbreite zweier gleicher Pole
  const double rin = std::pow(10.0, s.rin_db_per_hz / 10.0);

  double phase_noise = 0.0;
  r.drive.resize(n);
  r.optical.resize(n);
  r.current.resize(n);
  r.current_clean.resize(n);
  bool first = true;
  for (std::size_t i = 0; i < n; ++i) {
    if (control && (i & 1023u) == 0) {
      if (control->cancel_requested) return {};
      control->progress = static_cast<float>(i) / static_cast<float>(n);
    }
    const int bit = r.bits[i / static_cast<std::size_t>(spb)];
    const double v = junction(driver(bit ? s.v_high : s.v_low));
    r.drive[i] = v;
    // Laser mit Phasenrauschen (Zufallsweg, Varianz 2 pi linewidth dt je Schritt)
    if (s.linewidth > 0.0)
      phase_noise +=
          std::sqrt(2.0 * constants::pi * s.linewidth * dt) * circuit::standard_normal(s.seed, i, 3, 0);
    const cplx field_in = std::sqrt(s.laser_power) * std::exp(cplx(0.0, phase_noise));
    cplx field_out;
    if (s.modulator == ModulatorKind::mzm) {
      const double v2 = s.v_low + s.v_high - v;
      auto arm = [&](double volt) {
        return wg_amp * std::exp(-0.5 * s.pn.absorption(volt) * s.mzm_length) *
               std::exp(cplx(0.0, k0 * s.pn.index_change(volt) * s.mzm_length));
      };
      field_out = 0.5 * field_in * (arm(v) + arm(v2) * std::exp(cplx(0.0, s.mzm_bias_phase)));
    } else {
      // Coupled-Mode-Gleichung mit RK4: da/dt = (-i delta(V) - gamma(V)/2) a + sqrt(kappa_in) s
      const RingCmt c = ring_cmt(s, v);
      const cplx rate(-0.5 * (c.kappa_in + c.kappa_0), -c.detuning);
      const cplx drive = std::sqrt(c.kappa_in) * field_in;
      auto f = [&](cplx x) { return rate * x + drive; };
      const cplx k1 = f(a), k2 = f(a + 0.5 * dt * k1), k3 = f(a + 0.5 * dt * k2), k4 = f(a + dt * k3);
      a += dt / 6.0 * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
      field_out = field_in - std::sqrt(c.kappa_in) * a;
    }
    const double p = std::norm(field_out * link_amp);
    r.optical[i] = p;
    const double i_clean = s.responsivity * p + s.dark_current;
    if (first) rx1.y = rx2.y = i_clean, first = false;
    const double filtered = rx2(rx1(i_clean));
    r.current_clean[i] = filtered;
    double noisy = filtered;
    if (s.noise) {
      const double var =
          (2.0 * constants::q * std::max(filtered, 0.0) +
           4.0 * boltzmann * s.temperature / s.load_resistance + rin * std::pow(s.responsivity * p, 2)) *
          r.noise_bandwidth;
      noisy += std::sqrt(var) * circuit::standard_normal(s.seed, i, 7, 1);
    }
    r.current[i] = noisy;
  }

  // Beste Abtastung: Phase im Bit und Laufzeit in Bits mit größtem Q (ohne die ersten 8 Bits)
  const int warmup = 8;
  double best_q = -1.0;
  for (int latency = 0; latency <= 2; ++latency)
    for (int phase = 0; phase < spb; ++phase) {
      double s1 = 0, s0 = 0, q1 = 0, q0 = 0;
      int c1 = 0, c0 = 0;
      for (int b = warmup; b + latency < s.bits; ++b) {
        const double x = r.current[static_cast<std::size_t>((b + latency) * spb + phase)];
        if (r.bits[static_cast<std::size_t>(b)])
          s1 += x, q1 += x * x, ++c1;
        else
          s0 += x, q0 += x * x, ++c0;
      }
      if (c1 < 2 || c0 < 2) continue;
      const double m1 = s1 / c1, m0 = s0 / c0;
      const double sd1 = std::sqrt(std::max(0.0, q1 / c1 - m1 * m1)),
                   sd0 = std::sqrt(std::max(0.0, q0 / c0 - m0 * m0));
      const double q = std::abs(m1 - m0) / std::max(sd1 + sd0, 1e-30);
      if (q > best_q) {
        best_q = q, r.sample_phase = phase, r.latency_bits = latency;
        r.mean1 = m1, r.mean0 = m0, r.sigma1 = sd1, r.sigma0 = sd0;
      }
    }
  r.q_factor = best_q;
  r.ber = ber_from_q(best_q);

  // Pegel ohne Rauschen an der Abtastung: Extinktion (optisch) und vertikale Augenöffnung (ISI)
  double p1 = 0, p0 = 0, min1 = 1e300, max0 = -1e300, m1c = 0, m0c = 0;
  int c1 = 0, c0 = 0;
  for (int b = warmup; b + r.latency_bits < s.bits; ++b) {
    const std::size_t k = static_cast<std::size_t>((b + r.latency_bits) * spb + r.sample_phase);
    const double x = r.current_clean[k];
    const double popt = (x - s.dark_current) / s.responsivity;
    if (r.bits[static_cast<std::size_t>(b)])
      p1 += popt, m1c += x, min1 = std::min(min1, x), ++c1;
    else
      p0 += popt, m0c += x, max0 = std::max(max0, x), ++c0;
  }
  p1 /= std::max(c1, 1), p0 /= std::max(c0, 1), m1c /= std::max(c1, 1), m0c /= std::max(c0, 1);
  const bool high_is_one = m1c >= m0c;
  r.extinction_db = 10.0 * std::log10(std::max(high_is_one ? p1 / p0 : p0 / p1, 1e-30));
  if (high_is_one) {
    r.eye_opening = (min1 - max0) / (m1c - m0c);
  } else {
    double max1 = -1e300, min0 = 1e300;
    for (int b = warmup; b + r.latency_bits < s.bits; ++b) {
      const double x = r.current_clean[static_cast<std::size_t>((b + r.latency_bits) * spb + r.sample_phase)];
      if (r.bits[static_cast<std::size_t>(b)])
        max1 = std::max(max1, x);
      else
        min0 = std::min(min0, x);
    }
    r.eye_opening = (min0 - max1) / (m0c - m1c);
  }
  // Erwartete Rauschamplitude an den Pegeln (zur Kontrolle gegen die gemessene Streuung)
  auto expected = [&](double level) {
    const double popt = (level - s.dark_current) / s.responsivity;
    return std::sqrt((2.0 * constants::q * level + 4.0 * boltzmann * s.temperature / s.load_resistance +
                      rin * std::pow(s.responsivity * popt, 2)) *
                     r.noise_bandwidth);
  };
  r.noise_sigma_expected1 = s.noise ? expected(m1c) : 0.0;
  r.noise_sigma_expected0 = s.noise ? expected(m0c) : 0.0;

  // Augendiagramm: Fenster von zwei Bitdauern, Abtastpunkt in der Mitte
  for (int b = warmup; b + r.latency_bits + 1 < s.bits; ++b) {
    const long center = static_cast<long>((b + r.latency_bits) * spb + r.sample_phase);
    const long start = center - spb;
    if (start < 0 || center + spb >= static_cast<long>(n)) continue;
    r.eye.emplace_back(r.current.begin() + start, r.current.begin() + center + spb + 1);
  }
  if (control) control->progress = 1.0f;
  return r;
}

}  // namespace photonlab::signal
