#pragma once
// Freie Ladungsträger in Silizium (Phase 9): Plasma-Dispersion nach Soref-Bennett und Verarmungszone eines
// abrupten PN-Übergangs. Alle Größen in SI (Dichten in m^-3), Formeln und Quellen in docs/physics/active.md.

namespace photonlab::materials {

// Soref und Bennett, IEEE J. Quantum Electron. 23, 123 (1987), empirische Anpassung bei 1550 nm:
//   dn = -(8,8e-22 dN_e + 8,5e-18 dN_h^0,8),  d(alpha) = 8,5e-18 dN_e + 6,0e-18 dN_h   (N in cm^-3, alpha in
//   1/cm)
// Rückgabe: Brechzahländerung (dimensionslos) bzw. Leistungs-Absorptionskoeffizient [1/m] für Dichten in
// m^-3.
double plasma_index_change(double electrons, double holes);
double plasma_absorption(double electrons, double holes);

// Silizium bei 300 K: relative Permittivität 11,7 und intrinsische Dichte 1,0e10 cm^-3 (Sze, Physics of
// Semiconductor Devices, 3. Aufl. 2007, Anhang).
inline constexpr double silicon_permittivity = 11.7;
inline constexpr double silicon_intrinsic_density = 1.0e16;  // [m^-3]

// Abrupter PN-Übergang (Verarmungsnäherung). Dotierungen in m^-3, Spannung in Sperrrichtung positiv [V].
struct PnJunction {
  double acceptors = 5e23;  // N_A (p-Seite)
  double donors = 5e23;     // N_D (n-Seite)
  double temperature = 300.0;

  double built_in_voltage() const;                       // V_bi = kT/q ln(N_A N_D / n_i^2)
  double depletion_width(double reverse_voltage) const;  // w = sqrt(2 eps (V_bi + V)(N_A + N_D)/(q N_A N_D))
  double width_n(double reverse_voltage) const;          // Anteil auf der n-Seite: w N_A/(N_A + N_D)
  double width_p(double reverse_voltage) const;          // Anteil auf der p-Seite: w N_D/(N_A + N_D)
  double capacitance_per_area(double reverse_voltage) const;  // eps / w [F/m^2]
};

}  // namespace photonlab::materials
