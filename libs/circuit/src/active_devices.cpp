#include "photonlab/circuit/active_devices.hpp"

#include <algorithm>

#include "photonlab/core/units.hpp"

namespace photonlab::circuit {

double PnSection::index_change(double reverse_voltage) const {
  const double dn_e = materials::plasma_index_change(-junction.donors, 0.0);  // Elektronen entfernt: n steigt
  const double dn_h = materials::plasma_index_change(0.0, -junction.acceptors);  // Löcher entfernt
  const double wn = std::min(junction.width_n(reverse_voltage), 0.5 * width);
  const double wp = std::min(junction.width_p(reverse_voltage), 0.5 * width);
  return confinement * (dn_e * wn + dn_h * wp) / width;
}

double PnSection::absorption(double reverse_voltage) const {
  const double alpha_e = materials::plasma_absorption(junction.donors, 0.0);
  const double alpha_h = materials::plasma_absorption(0.0, junction.acceptors);
  const double wn = std::min(junction.width_n(reverse_voltage), 0.5 * width);
  const double wp = std::min(junction.width_p(reverse_voltage), 0.5 * width);
  return confinement * (alpha_e * (0.5 * width - wn) + alpha_h * (0.5 * width - wp)) / width;
}

double PnSection::capacitance_per_length(double reverse_voltage) const {
  return junction.capacitance_per_area(reverse_voltage) * height;
}

double heater_phase(double wavelength, double length, double dn_dT, double thermal_resistance, double power) {
  return 2.0 * constants::pi / wavelength * dn_dT * thermal_resistance * power * length;
}

double heater_p_pi(double wavelength, double length, double dn_dT, double thermal_resistance) {
  return wavelength / (2.0 * length * dn_dT * thermal_resistance);
}

}  // namespace photonlab::circuit
