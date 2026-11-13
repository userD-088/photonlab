#include "photonlab/modes/slab.hpp"

#include <algorithm>
#include <cmath>

#include "photonlab/core/units.hpp"

namespace photonlab::modes {
namespace {

struct Transverse {
  double kappa, gamma_lower, gamma_upper;  // Querwellenzahl im Kern, Abklingkonstanten in den Mänteln
  double weight_lower, weight_upper;       // 1 für TE, (n_core/n_mantel)² für TM
};

Transverse transverse(double n_eff, double n_core, double n_lower, double n_upper, double wavelength,
                      SlabPolarization polarization) {
  const double k0 = 2.0 * constants::pi / wavelength;
  Transverse t;
  t.kappa = k0 * std::sqrt(std::max(n_core * n_core - n_eff * n_eff, 0.0));
  t.gamma_lower = k0 * std::sqrt(std::max(n_eff * n_eff - n_lower * n_lower, 0.0));
  t.gamma_upper = k0 * std::sqrt(std::max(n_eff * n_eff - n_upper * n_upper, 0.0));
  const bool tm = polarization == SlabPolarization::tm;
  t.weight_lower = tm ? (n_core * n_core) / (n_lower * n_lower) : 1.0;
  t.weight_upper = tm ? (n_core * n_core) / (n_upper * n_upper) : 1.0;
  return t;
}

}  // namespace

// Eigenwertgleichung: kappa*d = atan(w_l*gamma_l/kappa) + atan(w_u*gamma_u/kappa) + m*pi
double slab_dispersion_residual(double n_eff, int order, double n_core, double n_lower, double n_upper,
                                double thickness, double wavelength, SlabPolarization polarization) {
  const Transverse t = transverse(n_eff, n_core, n_lower, n_upper, wavelength, polarization);
  // atan2 bleibt auch für kappa -> 0 wohldefiniert
  return t.kappa * thickness - std::atan2(t.weight_lower * t.gamma_lower, t.kappa) -
         std::atan2(t.weight_upper * t.gamma_upper, t.kappa) - order * constants::pi;
}

std::vector<SlabMode> slab_modes(double n_core, double n_lower, double n_upper, double thickness,
                                 double wavelength, SlabPolarization polarization) {
  if (!(thickness > 0.0) || !(wavelength > 0.0)) throw ModeError("Dicke und Wellenlänge müssen positiv sein");
  std::vector<SlabMode> modes;
  const double n_cladding = std::max(n_lower, n_upper);
  if (!(n_core > n_cladding)) return modes;  // keine Führung ohne Indexsprung

  // Das Residuum fällt monoton mit n_eff: von kappa_max*d - ... bei n_eff = n_cladding bis -pi - m*pi bei
  // n_core.
  for (int order = 0; order < 1000; ++order) {
    auto residual = [&](double n) {
      return slab_dispersion_residual(n, order, n_core, n_lower, n_upper, thickness, wavelength,
                                      polarization);
    };
    double low = n_cladding, high = n_core;
    if (residual(low) < 0.0) break;  // unterhalb der Grenzfrequenz dieser und aller höheren Ordnungen
    for (int i = 0; i < 200 && high - low > 1e-15 * n_core; ++i) {
      const double mid = 0.5 * (low + high);
      if (residual(mid) > 0.0)
        low = mid;
      else
        high = mid;
    }
    modes.push_back({order, 0.5 * (low + high)});
  }
  return modes;
}

double slab_field(const SlabMode& mode, double y, double n_core, double n_lower, double n_upper,
                  double thickness, double wavelength, SlabPolarization polarization) {
  const Transverse t = transverse(mode.n_eff, n_core, n_lower, n_upper, wavelength, polarization);
  // Im Kern cos(kappa*y - phi) mit tan(phi) = w_l*gamma_l/kappa, außen exponentiell abklingend und stetig
  const double phi = std::atan2(t.weight_lower * t.gamma_lower, t.kappa);
  double value = 0.0;
  if (y < 0.0)
    value = std::cos(phi) * std::exp(t.gamma_lower * y);
  else if (y > thickness)
    value = std::cos(t.kappa * thickness - phi) * std::exp(-t.gamma_upper * (y - thickness));
  else
    value = std::cos(t.kappa * y - phi);
  // Das Maximum cos = 1 liegt immer im Kern, weil kappa*d = phi + phi_oben + m*pi >= phi ist.
  return value;
}

double effective_index_method(double n_core, double n_lower, double n_upper, double n_side, double width,
                              double height, double wavelength, SlabPolarization polarization) {
  const auto vertical = slab_modes(n_core, n_lower, n_upper, height, wavelength, polarization);
  if (vertical.empty()) throw ModeError("Effektiv-Index-Methode: die Schicht führt keine Mode");
  const SlabPolarization other =
      polarization == SlabPolarization::te ? SlabPolarization::tm : SlabPolarization::te;
  const auto horizontal = slab_modes(vertical.front().n_eff, n_side, n_side, width, wavelength, other);
  if (horizontal.empty()) throw ModeError("Effektiv-Index-Methode: der Streifen führt keine Mode");
  return horizontal.front().n_eff;
}

}  // namespace photonlab::modes
