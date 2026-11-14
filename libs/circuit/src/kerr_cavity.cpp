#include "photonlab/circuit/kerr_cavity.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "photonlab/core/units.hpp"

namespace photonlab::circuit {

namespace {

using constants::c0;

double hbar() { return constants::h / (2.0 * std::numbers::pi); }

// Nenner D(U) = (w_eff - w_L)^2 + gamma^2/4 mit Ladungsträgern im Gleichgewicht
double denominator(const KerrCavity& c, double omega_laser, double energy) {
  const double n = c.carriers_steady(energy);
  const double d = c.frequency(energy, n) - omega_laser;
  const double g = c.rate(energy, n);
  return d * d + 0.25 * g * g;
}

}  // namespace

KerrCavity make_kerr_cavity(double wavelength, double q_intrinsic, double q_coupling, double n2,
                            double n_group, double mode_volume, double beta_tpa) {
  KerrCavity c;
  c.omega0 = 2.0 * std::numbers::pi * c0 / wavelength;
  c.kappa_0 = c.omega0 / q_intrinsic;
  c.kappa_in = c.omega0 / q_coupling;
  c.kerr = c.omega0 * n2 * c0 / (n_group * n_group * mode_volume);
  c.tpa = beta_tpa * c0 * c0 / (n_group * n_group * mode_volume);
  return c;
}

KerrCavity silicon_cavity(double wavelength, double q_intrinsic, double q_coupling, double n_group,
                          double mode_volume, double carrier_lifetime, bool with_carriers) {
  // Bristow, Rotter, Bachman, Appl. Phys. Lett. 90, 191104 (2007), 1550 nm
  KerrCavity c =
      make_kerr_cavity(wavelength, q_intrinsic, q_coupling, 4.5e-18, n_group, mode_volume, 8.0e-12);
  if (with_carriers) {
    // Soref und Bennett 1987 (linearisiert um 1e17 cm^-3): dn/dN = -3,6e-27 m^3, Absorption 1,45e-21 m^2
    c.carrier_gen =
        c.tpa / (2.0 * hbar() * c.omega0 * mode_volume);  // Paare je Zeit und Volumen = P_TPA/(2 hbar w V)
    c.carrier_tau = carrier_lifetime;
    c.carrier_shift = c.omega0 * 3.6e-27 / n_group;  // delta w = -w dn/n_g, dn < 0 -> blau
    c.carrier_loss = 1.45e-21 * c0 / n_group;        // Rate = alpha * v_g
  }
  return c;
}

double steady_power(const KerrCavity& c, double omega_laser, double energy) {
  return energy * denominator(c, omega_laser, energy) / c.kappa_in;
}

double transmission_at(const KerrCavity& c, double omega_laser, double energy, double carriers) {
  const double delta = c.frequency(energy, carriers) - omega_laser;
  const double g = c.rate(energy, carriers);
  const std::complex<double> a_over_s = std::sqrt(c.kappa_in) / std::complex<double>(0.5 * g, delta);
  return std::norm(1.0 - std::sqrt(c.kappa_in) * a_over_s);
}

std::vector<SteadyState> steady_states(const KerrCavity& c, double omega_laser, double power_in) {
  std::vector<SteadyState> result;
  if (!(power_in > 0.0)) {
    result.push_back({0.0, 0.0, transmission_at(c, omega_laser, 0.0, 0.0), true});
    return result;
  }
  // Obergrenze: P(U) wächst für große U (mindestens ~U^1), also verdoppeln, bis P(U) > power_in
  double u_max = c.kappa_in * power_in / (0.25 * c.rate_cold() * c.rate_cold());
  u_max = std::max(u_max, 1e-30);
  for (int i = 0; i < 200 && steady_power(c, omega_laser, u_max) <= power_in; ++i) u_max *= 2.0;
  u_max *= 1.5;  // Reserve, damit die Lösung hinter einem Maximum nicht am Rand liegt
  // Kerr-Verschiebung kann Zustände weit oberhalb der linearen Energie erlauben: Bereich bis zur Verstimmung
  if (c.kerr > 0.0) u_max = std::max(u_max, 3.0 * std::abs(c.omega0 - omega_laser) / c.kerr);
  const int n = 20000;
  auto f = [&](double u) { return steady_power(c, omega_laser, u) - power_in; };
  double u_prev = 0.0, f_prev = f(0.0);
  for (int i = 1; i <= n; ++i) {
    const double u = u_max * i / n;
    const double fu = f(u);
    if (f_prev == 0.0 || (f_prev < 0.0) != (fu < 0.0)) {
      double lo = u_prev, hi = u;
      for (int it = 0; it < 100; ++it) {  // Bisektion
        const double mid = 0.5 * (lo + hi);
        ((f(lo) < 0.0) == (f(mid) < 0.0)) ? lo = mid : hi = mid;
      }
      const double root = 0.5 * (lo + hi);
      const double h = std::max(root * 1e-6, 1e-40);
      const double slope =
          steady_power(c, omega_laser, root + h) - steady_power(c, omega_laser, std::max(0.0, root - h));
      const double carriers = c.carriers_steady(root);
      result.push_back({root, carriers, transmission_at(c, omega_laser, root, carriers), slope > 0.0});
    }
    u_prev = u;
    f_prev = fu;
  }
  return result;
}

Trace simulate(const KerrCavity& c, double omega_laser, const std::function<double(double)>& power,
               double t_end, double dt, int record_every) {
  using cplx = std::complex<double>;
  struct State {
    cplx a;
    double n;
  };
  auto derivative = [&](const State& s, double p) {
    const double u = std::norm(s.a);
    const double delta = c.frequency(u, s.n) - omega_laser;
    const double g = c.rate(u, s.n);
    State d;
    d.a = cplx(-0.5 * g, -delta) * s.a + std::sqrt(c.kappa_in * p);
    d.n = c.carrier_gen * u * u - s.n / c.carrier_tau;
    return d;
  };
  auto add = [](const State& s, const State& d, double h) { return State{s.a + h * d.a, s.n + h * d.n}; };
  Trace trace;
  State s{{0.0, 0.0}, 0.0};
  const long steps = static_cast<long>(std::ceil(t_end / dt));
  for (long i = 0; i <= steps; ++i) {
    const double t = static_cast<double>(i) * dt;
    if (i % std::max(1, record_every) == 0) {
      const double u = std::norm(s.a);
      trace.time.push_back(t);
      trace.power.push_back(power(t));
      trace.energy.push_back(u);
      trace.carriers.push_back(s.n);
      trace.transmission.push_back(transmission_at(c, omega_laser, u, s.n));
    }
    // RK4 mit der Leistung an den Stützstellen t, t + dt/2, t + dt
    const State k1 = derivative(s, power(t));
    const State k2 = derivative(add(s, k1, 0.5 * dt), power(t + 0.5 * dt));
    const State k3 = derivative(add(s, k2, 0.5 * dt), power(t + 0.5 * dt));
    const State k4 = derivative(add(s, k3, dt), power(t + dt));
    s.a += dt / 6.0 * (k1.a + 2.0 * k2.a + 2.0 * k3.a + k4.a);
    s.n += dt / 6.0 * (k1.n + 2.0 * k2.n + 2.0 * k3.n + k4.n);
  }
  return trace;
}

HysteresisLoop hysteresis_sweep(const KerrCavity& c, double omega_laser, double p_max, double ramp_time,
                                int records) {
  const double gamma = c.rate_cold();
  const double dt = 0.02 / std::max({gamma, std::abs(c.omega0 - omega_laser), 1.0});
  const double t_total = 2.0 * ramp_time;
  auto power = [&](double t) {
    return t < ramp_time ? p_max * t / ramp_time : p_max * (2.0 - t / ramp_time);
  };
  const long steps = static_cast<long>(std::ceil(t_total / dt));
  const int every = static_cast<int>(std::max<long>(1, steps / (2 * records)));
  const Trace full = simulate(c, omega_laser, power, t_total, dt, every);
  HysteresisLoop loop;
  for (std::size_t i = 0; i < full.time.size(); ++i) {
    Trace& t = full.time[i] <= ramp_time ? loop.up : loop.down;
    t.time.push_back(full.time[i]);
    t.power.push_back(full.power[i]);
    t.energy.push_back(full.energy[i]);
    t.carriers.push_back(full.carriers[i]);
    t.transmission.push_back(full.transmission[i]);
  }
  return loop;
}

double kerr_critical_power(const KerrCavity& c) {
  const double g = c.rate_cold();
  return std::sqrt(3.0) / 9.0 * g * g * g / (c.kerr * c.kappa_in);
}

}  // namespace photonlab::circuit
