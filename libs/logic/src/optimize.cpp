#include "photonlab/logic/optimize.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace photonlab::logic {

using circuit::CircuitError;

double worst_contrast_db(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                         const GateDefinition& gate, double wavelength) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  try {
    const TruthTable table = run_truth_table(circuit, registry, gate, wavelength);
    double worst = std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
      const OutputAnalysis a = analyze_output(table, k, gate.outputs[k].expected);
      if (!a.separable) return nan;
      worst = std::min(worst, a.contrast_db);
    }
    return worst;
  } catch (const CircuitError&) {
    return nan;
  }
}

OptimizeResult optimize_parameter(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                                  const GateDefinition& gate, double wavelength, const std::string& instance,
                                  const std::string& param, double lower, double upper, int points) {
  if (!circuit.find_instance(instance))
    throw CircuitError("Optimierer: Instanz '" + instance + "' gibt es nicht");
  if (!(lower < upper) || points < 3)
    throw CircuitError("Optimierer braucht Minimum < Maximum und mindestens 3 Punkte");
  for (const auto& input : gate.inputs)
    if (input.instance == instance && input.param == param)
      throw CircuitError("Optimierer: '" + instance + "." + param + "' ist ein logischer Eingang");

  circuit::Circuit varied = circuit;
  circuit::Instance* target = varied.find_instance(instance);
  // Bewertung: Kontrast, auf 100 dB begrenzt; nicht trennbar zählt als minus unendlich
  auto score = [&](double x) {
    target->params[param] = x;
    const double cr = worst_contrast_db(varied, registry, gate, wavelength);
    return cr == cr ? std::min(cr, 100.0) : -std::numeric_limits<double>::infinity();
  };

  OptimizeResult result;
  double best_score = -std::numeric_limits<double>::infinity();
  int best_index = -1;
  for (int i = 0; i < points; ++i) {
    const double x = lower + (upper - lower) * i / (points - 1);
    const double s = score(x);
    result.x.push_back(x);
    result.contrast_db.push_back(std::isfinite(s) ? s : std::numeric_limits<double>::quiet_NaN());
    if (s > best_score) {
      best_score = s;
      best_index = i;
    }
  }
  if (best_index < 0) return result;
  result.found = true;
  result.best_x = result.x[static_cast<std::size_t>(best_index)];
  result.best_contrast_db = best_score;

  // Verfeinerung zwischen den Rasternachbarn des besten Punktes
  double a = result.x[static_cast<std::size_t>(std::max(best_index - 1, 0))];
  double b = result.x[static_cast<std::size_t>(std::min(best_index + 1, points - 1))];
  const double ratio = (std::sqrt(5.0) - 1.0) / 2.0;
  double c = b - ratio * (b - a), d = a + ratio * (b - a);
  double fc = score(c), fd = score(d);
  for (int i = 0; i < 40 && best_score < 100.0; ++i) {
    if (fc > fd) {
      b = d, d = c, fd = fc;
      c = b - ratio * (b - a), fc = score(c);
    } else {
      a = c, c = d, fc = fd;
      d = a + ratio * (b - a), fd = score(d);
    }
    if (std::max(fc, fd) > best_score) {
      best_score = std::max(fc, fd);
      result.best_x = fc > fd ? c : d;
      result.best_contrast_db = best_score;
    }
  }
  return result;
}

}  // namespace photonlab::logic
