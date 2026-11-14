#include "photonlab/circuit/pcm.hpp"

#include <algorithm>
#include <cmath>

#include "photonlab/core/units.hpp"
#include "photonlab/materials/phase_change.hpp"

namespace photonlab::circuit {

std::complex<double> pcm_index_change(const PcmCell& cell) {
  const auto& gst = materials::gst225();
  const std::complex<double> eps_a = gst.index_amorphous * gst.index_amorphous;
  const std::complex<double> eps_c = gst.index_crystalline * gst.index_crystalline;
  const double x = std::clamp(cell.fraction, 0.0, 1.0);
  const std::complex<double> n = std::sqrt((1.0 - x) * eps_a + x * eps_c);
  return cell.confinement * (n - std::complex<double>(gst.index_amorphous.real(), 0.0));
}

std::complex<double> pcm_field_transmission(const PcmCell& cell, double wavelength) {
  const double k0 = 2.0 * constants::pi / wavelength;
  return std::exp(std::complex<double>(0.0, 1.0) * k0 * pcm_index_change(cell) * cell.length);
}

double pcm_transmission(const PcmCell& cell, double wavelength) {
  return std::norm(pcm_field_transmission(cell, wavelength));
}

PulseResult apply_pcm_pulse(PcmCell& cell, double power, double duration, double wavelength) {
  const auto& gst = materials::gst225();
  PulseResult r;
  r.fraction_before = cell.fraction;
  double temperature = cell.ambient;
  r.peak_temperature = temperature;
  const double total = duration + 5.0 * cell.thermal_time;
  const double dt = std::min({duration / 400.0, cell.thermal_time / 40.0, cell.crystallization_time / 40.0});
  const long steps = static_cast<long>(std::ceil(total / dt));
  for (long i = 0; i < steps; ++i) {
    const double t = static_cast<double>(i) * dt;
    const double p = t < duration ? power : 0.0;
    const double absorbed = p * (1.0 - pcm_transmission(cell, wavelength));
    temperature += dt / cell.thermal_time * (cell.thermal_resistance * absorbed - (temperature - cell.ambient));
    r.peak_temperature = std::max(r.peak_temperature, temperature);
    if (temperature >= gst.melting_temperature) r.melted = true;
    if (!r.melted && temperature >= gst.crystallization_temperature)
      cell.fraction += dt / cell.crystallization_time * (1.0 - cell.fraction);
  }
  if (r.melted) cell.fraction = 0.0;  // geschmolzen -> nach dem Puls amorph erstarrt
  r.fraction_after = cell.fraction;
  return r;
}

ProgramResult program_pcm(PcmCell& cell, double target_fraction, double wavelength, double reset_power,
                          double reset_duration, double tolerance, double max_step, int max_pulses) {
  const auto& gst = materials::gst225();
  ProgramResult result;
  const double target = std::clamp(target_fraction, 0.0, 1.0);
  if (target < cell.fraction - tolerance) result.pulses.push_back(apply_pcm_pulse(cell, reset_power, reset_duration, wavelength));
  // Zieltemperatur im unteren Drittel des Fensters: Reserve, weil die Absorption beim Kristallisieren steigt
  const double t_target =
      gst.crystallization_temperature + 0.3 * (gst.melting_temperature - gst.crystallization_temperature);
  // Aufheizzeit bis zur Kristallisationstemperatur
  const double t_heat = cell.thermal_time * std::log((t_target - cell.ambient) /
                                                     (t_target - gst.crystallization_temperature));
  // Nach dem Puls bleibt das GST noch eine Weile im Fenster (Abkühlen bis T_c): diese Zeit kristallisiert mit
  const double t_cool =
      cell.thermal_time * std::log((t_target - cell.ambient) / (gst.crystallization_temperature - cell.ambient));
  for (int k = 0; k < max_pulses && cell.fraction < target - tolerance; ++k) {
    // Lesen: Absorption bei kleiner Leistung (verändert den Zustand nicht)
    const double absorption = 1.0 - pcm_transmission(cell, wavelength);
    const double power = (t_target - cell.ambient) / (cell.thermal_resistance * absorption);
    // Dauer so, dass der Zuwachs höchstens max_step bzw. der Rest bis zum Ziel beträgt (bei Fenstertemperatur)
    // Zwischenziel: höchstens max_step weiter, nie ganz 1 (sonst unendliche Dauer)
    const double goal = std::min({cell.fraction + max_step, target, 0.995});
    const double step = std::max(goal - cell.fraction, 1e-6);
    const double duration =
        t_heat + std::max(cell.crystallization_time * std::log((1.0 - cell.fraction) / (1.0 - cell.fraction - step)) -
                              t_cool,
                          0.2e-9);
    result.pulses.push_back(apply_pcm_pulse(cell, power, duration, wavelength));
    if (result.pulses.back().melted) break;  // sollte nicht vorkommen; dann lieber abbrechen
  }
  result.reached = std::abs(cell.fraction - target) <= tolerance ||
                   (target >= 1.0 - tolerance && cell.fraction >= 1.0 - tolerance);
  return result;
}

}  // namespace photonlab::circuit
