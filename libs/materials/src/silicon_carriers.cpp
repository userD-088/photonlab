#include "photonlab/materials/silicon_carriers.hpp"

#include <cmath>
#include <stdexcept>

#include "photonlab/core/units.hpp"

namespace photonlab::materials {

namespace {
constexpr double per_cm3 = 1e-6;            // m^-3 -> cm^-3
constexpr double boltzmann = 1.380649e-23;  // [J/K], exakt (SI 2019)
}  // namespace

double plasma_index_change(double electrons, double holes) {
  const double ne = electrons * per_cm3, nh = holes * per_cm3;
  const double hole_term =
      nh > 0.0 ? 8.5e-18 * std::pow(nh, 0.8) : (nh < 0.0 ? -8.5e-18 * std::pow(-nh, 0.8) : 0.0);
  return -(8.8e-22 * ne + hole_term);
}

double plasma_absorption(double electrons, double holes) {
  const double alpha_per_cm = 8.5e-18 * electrons * per_cm3 + 6.0e-18 * holes * per_cm3;
  return alpha_per_cm * 100.0;
}

double PnJunction::built_in_voltage() const {
  const double thermal = boltzmann * temperature / constants::q;
  return thermal * std::log(acceptors * donors / (silicon_intrinsic_density * silicon_intrinsic_density));
}

double PnJunction::depletion_width(double reverse_voltage) const {
  const double v = built_in_voltage() + reverse_voltage;
  if (!(v > 0.0))
    throw std::invalid_argument("Durchlassspannung über der Diffusionsspannung: Verarmungsnäherung ungültig");
  const double eps = silicon_permittivity * constants::eps0;
  return std::sqrt(2.0 * eps * v * (acceptors + donors) / (constants::q * acceptors * donors));
}

double PnJunction::width_n(double reverse_voltage) const {
  return depletion_width(reverse_voltage) * acceptors / (acceptors + donors);
}

double PnJunction::width_p(double reverse_voltage) const {
  return depletion_width(reverse_voltage) * donors / (acceptors + donors);
}

double PnJunction::capacitance_per_area(double reverse_voltage) const {
  return silicon_permittivity * constants::eps0 / depletion_width(reverse_voltage);
}

}  // namespace photonlab::materials
