#include "photonlab/logic/truth_table.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace photonlab::logic {

using circuit::CircuitError;

TruthTable run_truth_table(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                           const GateDefinition& gate, double wavelength, double source_scale) {
  validate(gate, circuit, registry);
  TruthTable table;
  table.rows.resize(gate.row_count());
  for (std::size_t row = 0; row < table.rows.size(); ++row) {
    TruthRow& result = table.rows[row];
    for (std::size_t k = 0; k < gate.inputs.size(); ++k) result.bits.push_back(input_bit(gate, row, k));

    const circuit::CompiledCircuit compiled(apply_inputs(circuit, gate, row), registry);
    const auto measurements = compiled.measurements(compiled.solve(wavelength, nullptr, source_scale));
    for (const auto& output : gate.outputs) {
      const auto it = std::find_if(measurements.begin(), measurements.end(), [&](const auto& m) {
        return m.instance == output.instance && m.measurement.name == output.measurement;
      });
      if (it == measurements.end())
        throw CircuitError("Ausgang '" + output.name + "': '" + output.instance +
                           "' liefert keine Messgröße '" + output.measurement + "'");
      if (it->measurement.unit != "W")
        throw CircuitError("Ausgang '" + output.name + "': die Messgröße '" + output.measurement +
                           "' ist keine Leistung");
      result.power.push_back(it->measurement.value);
    }
  }
  return table;
}

double contrast_ratio_db(double p1_min, double p0_max) {
  if (!(p1_min > 0.0) || p0_max < 0.0) return std::numeric_limits<double>::quiet_NaN();
  if (p0_max == 0.0) return std::numeric_limits<double>::infinity();
  return 10.0 * std::log10(p1_min / p0_max);
}

OutputAnalysis analyze_output(const TruthTable& table, std::size_t output, const std::vector<int>& expected) {
  OutputAnalysis a;
  const std::size_t n = table.rows.size();
  if (n == 0) return a;
  std::vector<double> power(n);
  for (std::size_t i = 0; i < n; ++i) power[i] = table.rows[i].power.at(output);
  a.p_max = *std::max_element(power.begin(), power.end());
  a.p_min = *std::min_element(power.begin(), power.end());
  a.extinction_db = contrast_ratio_db(a.p_max, a.p_min);

  a.has_expected = expected.size() == n;
  if (a.has_expected) {
    a.bits = expected;
  } else {
    // Erkennung: sortierte Pegel an der größten Lücke trennen
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return power[x] < power[y]; });
    std::size_t split = 0;  // Zeilen order[0..split] gelten als 0
    double largest_gap = -1.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
      const double gap = power[order[i + 1]] - power[order[i]];
      if (gap > largest_gap) {
        largest_gap = gap;
        split = i;
      }
    }
    a.bits.assign(n, 0);
    for (std::size_t i = split + 1; i < n; ++i) a.bits[order[i]] = 1;
    if (!(largest_gap > 0.0)) a.bits.assign(n, 0);  // alle Pegel gleich: keine Logik erkennbar
  }
  a.function = identify_function(a.bits);

  bool any1 = false, any0 = false;
  a.p1_min = std::numeric_limits<double>::infinity();
  a.p0_max = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    if (a.bits[i]) {
      a.p1_min = std::min(a.p1_min, power[i]);
      any1 = true;
    } else {
      a.p0_max = std::max(a.p0_max, power[i]);
      any0 = true;
    }
  }
  if (!any1) a.p1_min = 0.0;
  a.separable = any1 && any0 && a.p1_min > a.p0_max;
  a.threshold = 0.5 * (a.p1_min + a.p0_max);
  a.contrast_db =
      any1 && any0 ? contrast_ratio_db(a.p1_min, a.p0_max) : std::numeric_limits<double>::quiet_NaN();
  return a;
}

bool logic_holds(const TruthTable& table, std::size_t output, const std::vector<int>& bits,
                 double threshold) {
  if (bits.size() != table.rows.size()) return false;
  for (std::size_t i = 0; i < bits.size(); ++i)
    if ((table.rows[i].power.at(output) > threshold) != (bits[i] != 0)) return false;
  return true;
}

}  // namespace photonlab::logic
