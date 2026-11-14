#include "photonlab/circuit/sweep.hpp"

#include "photonlab/core/parallel_for.hpp"

namespace photonlab::circuit {

std::vector<double> sweep_wavelengths(const SweepSettings& settings) {
  if (settings.points < 1) throw CircuitError("Sweep braucht mindestens einen Punkt");
  if (!(settings.wavelength_start > 0.0) || !(settings.wavelength_stop > 0.0))
    throw CircuitError("Sweep-Wellenlängen müssen positiv sein");
  std::vector<double> wavelengths(static_cast<std::size_t>(settings.points));
  for (int i = 0; i < settings.points; ++i) {
    const double f = settings.points > 1 ? static_cast<double>(i) / (settings.points - 1) : 0.0;
    wavelengths[static_cast<std::size_t>(i)] =
        settings.wavelength_start + f * (settings.wavelength_stop - settings.wavelength_start);
  }
  return wavelengths;
}

std::vector<Solution> sweep(const CompiledCircuit& circuit, const SweepSettings& settings, ThreadPool* pool,
                            JobControl* control) {
  const std::vector<double> wavelengths = sweep_wavelengths(settings);
  std::vector<Solution> solutions(wavelengths.size());
  const bool completed = parallel_for(
      wavelengths.size(), [&](std::size_t i) { solutions[i] = circuit.solve(wavelengths[i]); }, pool,
      control);
  if (!completed) return {};
  return solutions;
}

namespace {
template <class Point>
RampResult ramp(const CompiledCircuit& circuit, std::size_t count, const Point& point, CircuitState* state,
                JobControl* control) {
  RampResult result;
  CircuitState local = state ? *state : circuit.zero_state();
  for (std::size_t i = 0; i < count; ++i) {
    if (control && control->cancel_requested) return {};
    result.solutions.push_back(point(i, local));
    result.states.push_back(local);
    if (control) control->progress = static_cast<float>(i + 1) / static_cast<float>(count);
  }
  if (state) *state = local;
  return result;
}
}  // namespace

RampResult wavelength_ramp(const CompiledCircuit& circuit, const std::vector<double>& wavelengths,
                           double source_scale, CircuitState* state, JobControl* control) {
  return ramp(
      circuit, wavelengths.size(),
      [&](std::size_t i, CircuitState& s) { return circuit.solve(wavelengths[i], s, nullptr, source_scale); },
      state, control);
}

RampResult power_ramp(const CompiledCircuit& circuit, double wavelength,
                      const std::vector<double>& source_scales, CircuitState* state, JobControl* control) {
  return ramp(
      circuit, source_scales.size(),
      [&](std::size_t i, CircuitState& s) { return circuit.solve(wavelength, s, nullptr, source_scales[i]); },
      state, control);
}

}  // namespace photonlab::circuit
