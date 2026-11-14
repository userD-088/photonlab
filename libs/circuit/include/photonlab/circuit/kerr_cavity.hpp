#pragma once
// Nichtlinearer Resonator als Kompaktmodell (Phase 8): Coupled-Mode-Gleichungen mit Kerr-Effekt,
// Zwei-Photonen- Absorption (TPA) und freien Ladungsträgern. Gleichungen und Herleitung:
// docs/physics/nonlinear.md
//
//   da/dt = ( -i (w_eff - w_L) - gamma/2 ) a + sqrt(kappa_in) s_in          |a|^2 = U  Energie im Resonator
//   [J] dN/dt = carrier_gen U^2 - N / tau                                        |s_in|^2 = P
//   Eingangsleistung [W] w_eff = w0 - kerr U + carrier_shift N,   gamma = kappa_in + kappa_0 + tpa U +
//   carrier_loss N
#include <functional>
#include <vector>

namespace photonlab::circuit {

struct KerrCavity {
  double omega0 = 0.0;    // Resonanzfrequenz ohne Nichtlinearität [rad/s]
  double kappa_in = 0.0;  // Energie-Abklingrate in den Bus (Kopplung) [1/s]
  double kappa_0 = 0.0;   // Energie-Abklingrate durch intrinsische Verluste [1/s]
  double kerr =
      0.0;  // Kerr-Verschiebung: w_eff = w0 - kerr*U [rad/(s J)]; kerr > 0 = Rotverschiebung (n2 > 0)
  double tpa = 0.0;            // TPA: Zusatzrate tpa*U [1/(s J)]
  double carrier_gen = 0.0;    // Erzeugung freier Ladungsträger: dN/dt = gen*U^2 - N/tau [m^-3/(s J^2)]
  double carrier_tau = 1e-9;   // Lebensdauer der Ladungsträger [s]
  double carrier_shift = 0.0;  // Blauverschiebung durch Ladungsträger: + shift*N [rad/s pro m^-3]
  double carrier_loss = 0.0;   // Zusatzrate durch Absorption an Ladungsträgern: loss*N [1/s pro m^-3]

  double rate_cold() const { return kappa_in + kappa_0; }  // gamma ohne Nichtlinearität
  double frequency(double energy, double carriers) const {
    return omega0 - kerr * energy + carrier_shift * carriers;
  }
  double rate(double energy, double carriers) const {
    return rate_cold() + tpa * energy + carrier_loss * carriers;
  }
  // Ladungsträgerdichte im Gleichgewicht bei Energie U
  double carriers_steady(double energy) const { return carrier_gen * energy * energy * carrier_tau; }
};

// Parameter aus Güten und Material (Herleitung in nonlinear.md):
//   kappa = omega0/Q, kerr = omega0 n2 c / (n_g^2 V_eff), tpa = beta_tpa c^2 / (n_g^2 V_eff)
//   (V_eff = A_eff * L, n2 [m^2/W], beta_tpa [m/W]); Ladungsträger werden nicht gesetzt.
KerrCavity make_kerr_cavity(double wavelength, double q_intrinsic, double q_coupling, double n2,
                            double n_group, double mode_volume, double beta_tpa = 0.0);

// Silizium bei 1550 nm: Kerr n2 und TPA beta nach Bristow, Rotter, Bachman, Appl. Phys. Lett. 90, 191104
// (2007) (n2 = 4,5e-18 m^2/W, beta = 8,0e-12 m/W); freie Ladungsträger nach Soref und Bennett, IEEE J.
// Quantum Electron. 23, 123 (1987), linearisiert um N = 1e17 cm^-3 mit gleich vielen Elektronen und Löchern:
// dn/dN = -3,6e-27 m^3, Absorption 1,45e-21 m^2. Erzeugung: ein Paar je zwei absorbierte Photonen.
// carrier_tau ist die effektive Lebensdauer (Diffusion, Rekombination) in einem Streifenwellenleiter.
KerrCavity silicon_cavity(double wavelength, double q_intrinsic, double q_coupling, double n_group,
                          double mode_volume, double carrier_lifetime = 1e-9, bool with_carriers = true);

// Ein stationärer Zustand bei gegebener Laserfrequenz und Eingangsleistung.
struct SteadyState {
  double energy = 0.0;        // U [J]
  double carriers = 0.0;      // N [m^-3]
  double transmission = 0.0;  // |s_out/s_in|^2 am Bus (Durchgang eines All-Pass-Resonators)
  bool stable = false;  // Steigung dP/dU > 0 (für Kerr/TPA hinreichend und notwendig; mit Ladungsträgern
                        // notwendig, Selbstpulsieren ist damit nicht ausgeschlossen)
};

// Eingangsleistung, die zur Energie U im Gleichgewicht gehört (explizit!): P(U) = U * D(U) / kappa_in.
double steady_power(const KerrCavity& cavity, double omega_laser, double energy);

// Alle stationären Zustände (aufsteigend nach Energie): Lösungen von P(U) = power_in.
std::vector<SteadyState> steady_states(const KerrCavity& cavity, double omega_laser, double power_in);

// Durchgang |s_out/s_in|^2 bei gegebenem Zustand (U, N).
double transmission_at(const KerrCavity& cavity, double omega_laser, double energy, double carriers);

// Zeitverlauf; power(t) liefert die Eingangsleistung. RK4 mit festem Schritt dt.
struct Trace {
  std::vector<double> time, power, energy, carriers, transmission;
};
Trace simulate(const KerrCavity& cavity, double omega_laser, const std::function<double(double)>& power,
               double t_end, double dt, int record_every = 1);

// Langsame Rampe der Leistung 0 -> p_max -> 0 (Hysterese): up und down getrennt.
struct HysteresisLoop {
  Trace up, down;
};
HysteresisLoop hysteresis_sweep(const KerrCavity& cavity, double omega_laser, double p_max, double ramp_time,
                                int records = 400);

// Kritische Leistung des reinen Kerr-Resonators (tpa = 0, keine Ladungsträger) bei Verstimmung
// delta = w0 - w_L = sqrt(3) gamma / 2 (Beginn der Bistabilität): P_c = sqrt(3)/9 gamma^3 / (kerr kappa_in).
double kerr_critical_power(const KerrCavity& cavity);

}  // namespace photonlab::circuit
