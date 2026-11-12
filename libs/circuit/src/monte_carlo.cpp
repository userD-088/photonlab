#include "photonlab/circuit/monte_carlo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "photonlab/core/parallel_for.hpp"
#include "photonlab/core/units.hpp"

namespace photonlab::circuit {
namespace {

// SplitMix64 (Steele, Lea, Flood: "Fast splittable pseudorandom number generators", OOPSLA 2014).
// Eigene Umsetzung statt <random>, weil std::normal_distribution je nach Standardbibliothek
// andere Zahlen liefert und die Ergebnisse dann nicht mehr plattformunabhängig wären.
std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// Gleichverteilt in (0, 1): 53 Zufallsbits, um eine halbe Stufe verschoben
double unit_interval(std::uint64_t bits) {
  return (static_cast<double>(bits >> 11) + 0.5) / 9007199254740992.0;
}

}  // namespace

double standard_normal(std::uint64_t seed, std::uint64_t a, std::uint64_t b, std::uint64_t c) {
  std::uint64_t h = splitmix64(seed);
  h = splitmix64(h ^ a);
  h = splitmix64(h ^ b);
  h = splitmix64(h ^ c);
  const double u1 = unit_interval(h);
  const double u2 = unit_interval(splitmix64(h));
  // Box-Muller-Transformation
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * constants::pi * u2);
}

void validate(const MonteCarloSettings& settings, const Circuit& /*circuit*/,
              const ComponentRegistry& registry) {
  if (settings.samples < 1) throw CircuitError("Monte-Carlo braucht mindestens eine Stichprobe");
  for (const auto& v : settings.variations) {
    const ComponentType* type = registry.find(v.type);
    if (!type) throw CircuitError("Streuung nennt den unbekannten Bauteiltyp '" + v.type + "'");
    if (!type->find_param(v.param))
      throw CircuitError("Streuung: Bauteiltyp '" + v.type + "' kennt den Parameter '" + v.param + "' nicht");
    if (!(v.sigma >= 0.0) || !std::isfinite(v.sigma))
      throw CircuitError("Streuung von '" + v.type + "." + v.param + "' braucht sigma >= 0");
  }
}

Circuit perturbed_circuit(const Circuit& circuit, const ComponentRegistry& registry,
                          const MonteCarloSettings& settings, std::uint64_t sample_index) {
  Circuit result = circuit;
  for (std::size_t v = 0; v < settings.variations.size(); ++v) {
    const Variation& variation = settings.variations[v];
    const ComponentType* type = registry.find(variation.type);
    const ParamSpec* spec = type ? type->find_param(variation.param) : nullptr;
    if (!spec) continue;
    for (std::size_t k = 0; k < result.instances.size(); ++k) {
      Instance& instance = result.instances[k];
      if (instance.type != variation.type) continue;
      const auto it = instance.params.find(variation.param);
      const double nominal = it != instance.params.end() ? it->second : spec->default_value;
      // korreliert: eine Zufallszahl für alle Instanzen (Index 0), sonst eine je Instanz (Index k + 1)
      const double z = standard_normal(settings.seed, sample_index, v, variation.correlated ? 0 : k + 1);
      const double delta = variation.sigma * (variation.relative ? nominal : 1.0) * z;
      instance.params[variation.param] = std::clamp(nominal + delta, spec->min_value, spec->max_value);
    }
  }
  return result;
}

MonteCarloResult monte_carlo(const Circuit& circuit, const ComponentRegistry& registry, double wavelength,
                             const MonteCarloSettings& settings, ThreadPool* pool, JobControl* control) {
  validate(settings, circuit, registry);

  // Nennschaltung legt Namen und Reihenfolge der Messgrößen fest (und meldet Fehler der Netzliste sofort)
  MonteCarloResult result;
  {
    const CompiledCircuit nominal(circuit, registry);
    for (const auto& m : nominal.measurements(nominal.solve(wavelength))) {
      result.names.push_back(m.instance + " " + m.measurement.name);
      result.units.push_back(m.measurement.unit);
    }
  }
  const std::size_t samples = static_cast<std::size_t>(settings.samples);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  result.values.assign(result.names.size(), std::vector<double>(samples, nan));

  std::vector<char> failed(samples, 0);
  const bool completed = parallel_for(
      samples,
      [&](std::size_t i) {
        try {
          const CompiledCircuit compiled(perturbed_circuit(circuit, registry, settings, i), registry);
          const auto measurements = compiled.measurements(compiled.solve(wavelength));
          for (std::size_t m = 0; m < measurements.size() && m < result.values.size(); ++m)
            result.values[m][i] = measurements[m].measurement.value;
        } catch (const CircuitError&) {
          failed[i] = 1;  // z. B. singuläre Stichprobe: bleibt NaN und zählt als Ausfall
        }
      },
      pool, control);
  result.cancelled = !completed;
  result.failed = static_cast<int>(std::count(failed.begin(), failed.end(), 1));
  return result;
}

Statistics statistics(const std::vector<double>& values) {
  Statistics s;
  double sum = 0.0;
  for (double v : values) {
    if (!std::isfinite(v)) continue;
    if (s.count == 0) s.min = s.max = v;
    s.min = std::min(s.min, v);
    s.max = std::max(s.max, v);
    sum += v;
    ++s.count;
  }
  if (s.count == 0) return s;
  s.mean = sum / s.count;
  double squares = 0.0;
  for (double v : values)
    if (std::isfinite(v)) squares += (v - s.mean) * (v - s.mean);
  s.stddev = s.count > 1 ? std::sqrt(squares / (s.count - 1)) : 0.0;
  return s;
}

double yield_fraction(const std::vector<double>& values, double limit, bool at_least) {
  if (values.empty()) return 0.0;
  std::size_t good = 0;
  for (double v : values)
    if (std::isfinite(v) && (at_least ? v >= limit : v <= limit)) ++good;
  return static_cast<double>(good) / static_cast<double>(values.size());
}

}  // namespace photonlab::circuit
