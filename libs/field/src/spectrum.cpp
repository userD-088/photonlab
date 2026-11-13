#include "photonlab/field/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

#include "photonlab/core/units.hpp"

namespace photonlab::field {

std::vector<double> amplitude_spectrum(const std::vector<double>& samples, double sample_interval,
                                       const std::vector<double>& frequencies) {
  std::vector<double> result(frequencies.size(), 0.0);
  if (samples.empty()) return result;
  for (std::size_t k = 0; k < frequencies.size(); ++k) {
    // Drehzeiger schrittweise weiterdrehen statt jedes Mal exp() zu rechnen
    const std::complex<double> rotation = std::polar(1.0, -2.0 * constants::pi * frequencies[k] * sample_interval);
    std::complex<double> phasor = 1.0, sum = 0.0;
    for (double x : samples) {
      sum += x * phasor;
      phasor *= rotation;
    }
    result[k] = 2.0 * std::abs(sum) / static_cast<double>(samples.size());
  }
  return result;
}

double average_over_periods(const std::vector<double>& samples, double sample_interval, double period,
                            int periods) {
  if (samples.empty() || !(sample_interval > 0.0) || !(period > 0.0)) return 0.0;
  const auto wanted = static_cast<std::size_t>(std::llround(periods * period / sample_interval));
  const std::size_t count = std::clamp<std::size_t>(wanted, 1, samples.size());
  double sum = 0.0;
  for (std::size_t i = samples.size() - count; i < samples.size(); ++i) sum += samples[i];
  return sum / static_cast<double>(count);
}

}  // namespace photonlab::field
