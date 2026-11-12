#pragma once
// Physikalische Konstanten und Einheiten. Intern rechnet PhotonLab IMMER in SI.
// Quelle der Konstanten: CODATA 2018 (exakte SI-Definitionen seit 2019).
#include <numbers>

namespace photonlab {

namespace constants {
inline constexpr double c0   = 299'792'458.0;          // Lichtgeschwindigkeit im Vakuum [m/s], exakt
inline constexpr double mu0  = 1.25663706212e-6;       // magnetische Feldkonstante [H/m]
inline constexpr double eps0 = 1.0 / (mu0 * c0 * c0);  // elektrische Feldkonstante [F/m]
inline constexpr double h    = 6.62607015e-34;         // Planck-Konstante [J s], exakt
inline constexpr double q    = 1.602176634e-19;        // Elementarladung [C], exakt
inline constexpr double pi   = std::numbers::pi;
}  // namespace constants

namespace units {
// Umrechnung in SI: 1550 * units::nm == 1.55e-6 (Meter)
inline constexpr double m  = 1.0;
inline constexpr double mm = 1e-3;
inline constexpr double um = 1e-6;
inline constexpr double nm = 1e-9;
inline constexpr double s  = 1.0;
inline constexpr double ns = 1e-9;
inline constexpr double ps = 1e-12;
inline constexpr double fs = 1e-15;
inline constexpr double W  = 1.0;
inline constexpr double mW = 1e-3;
inline constexpr double J  = 1.0;
inline constexpr double fJ = 1e-15;
}  // namespace units

// Hilfsfunktionen zwischen Wellenlänge, Frequenz und Kreisfrequenz
constexpr double frequency_from_wavelength(double wavelength) { return constants::c0 / wavelength; }
constexpr double omega_from_wavelength(double wavelength) {
  return 2.0 * constants::pi * constants::c0 / wavelength;
}
constexpr double wavelength_from_frequency(double frequency) { return constants::c0 / frequency; }

// Dezibel
double to_db(double power_ratio);
double from_db(double db);

}  // namespace photonlab
