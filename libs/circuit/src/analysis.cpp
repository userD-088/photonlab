#include "photonlab/circuit/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace photonlab::circuit {

std::vector<Resonance> find_resonances(const std::vector<double>& x, const std::vector<double>& y_in,
                                       ResonanceKind kind, double min_prominence) {
  std::vector<Resonance> result;
  const std::size_t n = std::min(x.size(), y_in.size());
  if (n < 5) return result;

  // Spitzen werden als Einbrüche des gespiegelten Spektrums behandelt
  const double sign = kind == ResonanceKind::dip ? 1.0 : -1.0;
  std::vector<double> y(n);
  for (std::size_t i = 0; i < n; ++i) y[i] = sign * y_in[i];

  const auto [min_it, max_it] = std::minmax_element(y.begin(), y.end());
  const double span = *max_it - *min_it;
  if (!(span > 0.0)) return result;

  // lokale Minima (bei Plateaus zählt der linke Rand)
  std::vector<std::size_t> minima;
  for (std::size_t i = 1; i + 1 < n; ++i)
    if (y[i] < y[i - 1] && y[i] <= y[i + 1]) minima.push_back(i);

  for (std::size_t k = 0; k < minima.size(); ++k) {
    const std::size_t i = minima[k];
    // Bezugspegel: das kleinere der beiden Maxima bis zum nächsten Minimum links und rechts
    const std::size_t left_end = k > 0 ? minima[k - 1] : 0;
    const std::size_t right_end = k + 1 < minima.size() ? minima[k + 1] : n - 1;
    const double left_max = *std::max_element(y.begin() + static_cast<std::ptrdiff_t>(left_end),
                                              y.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    const double right_max = *std::max_element(y.begin() + static_cast<std::ptrdiff_t>(i),
                                               y.begin() + static_cast<std::ptrdiff_t>(right_end) + 1);
    if (std::min(left_max, right_max) - y[i] < min_prominence * span) continue;
    // Für Breite und Extinktion zählt die höhere Seite: liegt die Resonanz am Fensterrand, ist die
    // abgeschnittene Flanke kein Maß für den ungestörten Pegel.
    const double baseline = std::max(left_max, right_max);

    // Extremum parabolisch durch drei Punkte verfeinern (allgemein für ungleiche Abstände)
    double position = x[i], value = y[i];
    {
      const double x0 = x[i - 1], x1 = x[i], x2 = x[i + 1];
      const double d01 = (y[i] - y[i - 1]) / (x1 - x0), d12 = (y[i + 1] - y[i]) / (x2 - x1);
      const double curvature = (d12 - d01) / (x2 - x0);
      if (curvature > 0.0) {
        const double vertex = 0.5 * (x0 + x1) - d01 / (2.0 * curvature);
        if (vertex > x0 && vertex < x2) {
          position = vertex;
          value = y[i - 1] + d01 * (vertex - x0) + curvature * (vertex - x0) * (vertex - x1);
        }
      }
    }

    // Halbwertsbreite: Schnittpunkte mit dem halben Pegel, linear interpoliert
    const double half = 0.5 * (baseline + value);
    double left_cross = std::numeric_limits<double>::quiet_NaN(), right_cross = left_cross;
    for (std::size_t j = i; j > left_end; --j)
      if (y[j - 1] >= half && y[j] < half) {
        left_cross = x[j - 1] + (x[j] - x[j - 1]) * (half - y[j - 1]) / (y[j] - y[j - 1]);
        break;
      }
    for (std::size_t j = i; j < right_end; ++j)
      if (y[j + 1] >= half && y[j] < half) {
        right_cross = x[j] + (x[j + 1] - x[j]) * (half - y[j]) / (y[j + 1] - y[j]);
        break;
      }

    Resonance r;
    r.position = position;
    r.value = sign * value;
    r.baseline = sign * baseline;
    r.fwhm = right_cross - left_cross;  // NaN, wenn ein Schnittpunkt fehlt
    r.quality = r.fwhm > 0.0 ? position / r.fwhm : std::numeric_limits<double>::quiet_NaN();
    const double high = std::max(r.value, r.baseline), low = std::min(r.value, r.baseline);
    r.extinction_db = low > 0.0 && high > 0.0 ? 10.0 * std::log10(high / low)
                                              : std::numeric_limits<double>::infinity();
    result.push_back(r);
  }
  return result;
}

std::vector<Resonance> find_dominant_resonances(const std::vector<double>& x, const std::vector<double>& y,
                                                ResonanceKind* kind, double min_prominence) {
  auto dips = find_resonances(x, y, ResonanceKind::dip, min_prominence);
  auto peaks = find_resonances(x, y, ResonanceKind::peak, min_prominence);
  // schmalste messbare Breite je Art; ohne messbare Breite gilt die Art als "breit"
  auto narrowest = [](const std::vector<Resonance>& list) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& r : list)
      if (r.fwhm > 0.0) best = std::min(best, r.fwhm);
    return best;
  };
  const bool use_dips = !dips.empty() && (peaks.empty() || narrowest(dips) <= narrowest(peaks));
  if (kind) *kind = use_dips ? ResonanceKind::dip : ResonanceKind::peak;
  return use_dips ? dips : peaks;
}

double mean_spacing(const std::vector<Resonance>& resonances) {
  if (resonances.size() < 2) return 0.0;
  return (resonances.back().position - resonances.front().position) /
         static_cast<double>(resonances.size() - 1);
}

}  // namespace photonlab::circuit
