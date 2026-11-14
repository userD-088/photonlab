#pragma once
// Aktive Bauteile (Phase 9): Physik der Phasenschieber, die Bauteile, Zeitbereich und Tests gemeinsam nutzen.
// Herleitung und Quellen: docs/physics/active.md
#include "photonlab/materials/silicon_carriers.hpp"

namespace photonlab::circuit {

// Seitlicher PN-Übergang mitten im Wellenleiter (Verarmungs-Modulator). Vereinfachtes Überlappmodell: die
// Mode füllt die Wellenleiterbreite W gleichmäßig, der Anteil Gamma ihrer Leistung liegt im Silizium. Eine
// Sperrspannung räumt die Zone w_n (Elektronen) und w_p (Löcher) leer:
//   dn_eff(V) = Gamma * [dn_e * w_n(V) + dn_h * w_p(V)] / W,   dn_e = +8,8e-22 N_D, dn_h = +8,5e-18 N_A^0,8
//   (cm^-3) alpha(V)  = Gamma * [alpha_e * (W/2 - w_n) + alpha_h * (W/2 - w_p)] / W   (Restverlust der
//   dotierten Zonen)
struct PnSection {
  materials::PnJunction junction;
  double width = 500e-9;     // Wellenleiterbreite W [m]
  double height = 220e-9;    // Höhe des Übergangs (für die Kapazität) [m]
  double confinement = 0.8;  // Anteil der Modenleistung im Silizium

  // Änderung des effektiven Index gegenüber dem undotiert gedachten Wellenleiter mit voller Dotierung
  double index_change(double reverse_voltage) const;
  // Leistungs-Absorption durch die verbliebenen freien Ladungsträger [1/m]
  double absorption(double reverse_voltage) const;
  // Sperrschichtkapazität je Länge [F/m]
  double capacitance_per_length(double reverse_voltage) const;
};

// Heizer-Phasenschieber: dphi = 2 pi / lambda * dn/dT * R_th * P_el * L  =>  P_pi = lambda / (2 L dn/dT R_th)
double heater_phase(double wavelength, double length, double dn_dT, double thermal_resistance, double power);

}  // namespace photonlab::circuit
