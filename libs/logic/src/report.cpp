#include "photonlab/logic/report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "photonlab/circuit/monte_carlo.hpp"
#include "photonlab/core/units.hpp"

// Hinweis: Texte bleiben im Latin-1-Zeichenvorrat (sie erscheinen in der Oberfläche).

namespace photonlab::logic {
namespace {

using circuit::Circuit;
using circuit::CircuitError;
using circuit::CompiledCircuit;
using circuit::ComponentRegistry;
using circuit::ComponentType;

std::string text(const char* format, double value) {
  char buffer[512];
  std::snprintf(buffer, sizeof buffer, format, value);
  return buffer;
}

// Schwellen der Ampeln. Das sind Festlegungen von PhotonLab, keine Naturkonstanten (docs/physics/logic.md).
constexpr double contrast_good_db = 20.0, contrast_limited_db = 10.0;
constexpr double isolation_good_db = 30.0, isolation_limited_db = 20.0;
constexpr double phase_good = 0.3, phase_limited = 0.1;                  // rad
constexpr double wavelength_good = 1e-9, wavelength_limited = 0.1e-9;    // m
constexpr double level_slope_good = 0.1, level_slope_limited = 0.5;      // dB pro dB

Rating rate_above(double value, double good, double limited) {
  if (!(value == value)) return Rating::bad;  // NaN
  return value >= good ? Rating::good : value >= limited ? Rating::limited : Rating::bad;
}

Rating worse(Rating a, Rating b) {
  auto rank = [](Rating r) { return r == Rating::bad ? 2 : r == Rating::limited ? 1 : 0; };
  return rank(a) >= rank(b) ? a : b;
}

// Nennzustand: zugeordnete Bits und Entscheidungspegel je Ausgang
struct Nominal {
  std::vector<std::vector<int>> bits;
  std::vector<double> threshold;
};

// Hält die Logik aller Ausgänge in einer veränderten Schaltung mit den Nenn-Schwellwerten?
bool holds(const Circuit& circuit, const ComponentRegistry& registry, const GateDefinition& gate,
           double wavelength, const Nominal& nominal, double source_scale = 1.0) {
  try {
    const TruthTable table = run_truth_table(circuit, registry, gate, wavelength, source_scale);
    for (std::size_t k = 0; k < gate.outputs.size(); ++k)
      if (!logic_holds(table, k, nominal.bits[k], nominal.threshold[k])) return false;
    return true;
  } catch (const CircuitError&) {
    return false;  // nicht lösbar zählt als Ausfall
  }
}

struct PhaseParam {
  std::string instance, param;
  double min_value, max_value;
};

std::vector<PhaseParam> phase_params(const Circuit& circuit, const ComponentRegistry& registry) {
  std::vector<PhaseParam> result;
  for (const auto& instance : circuit.instances) {
    const ComponentType* type = registry.find(instance.type);
    if (!type) continue;
    for (const auto& spec : type->params)
      if (spec.display_unit == "rad") result.push_back({instance.name, spec.name, spec.min_value, spec.max_value});
  }
  return result;
}

// Größter Phasenfehler delta, bei dem die Logik für alle Vorzeichen-Kombinationen (+-delta auf jedem
// Phasen-Parameter) noch hält. Bei mehr als 6 Parametern 64 feste Zufalls-Kombinationen.
double phase_tolerance(const Circuit& circuit, const ComponentRegistry& registry, const GateDefinition& gate,
                       double wavelength, const Nominal& nominal, const std::vector<PhaseParam>& params) {
  if (params.empty()) return std::numeric_limits<double>::infinity();
  const std::size_t k = params.size();
  const std::size_t patterns = k <= 6 ? (std::size_t{1} << k) : 64;
  auto sign = [&](std::size_t pattern, std::size_t index) {
    if (k <= 6) return ((pattern >> index) & 1u) ? 1.0 : -1.0;
    return circuit::standard_normal(1, pattern, index, 0) >= 0.0 ? 1.0 : -1.0;
  };

  double tolerance = 0.0;
  const double factor = std::pow((constants::pi / 2) / 0.005, 1.0 / 27.0);
  double delta = 0.005;
  for (int step = 0; step < 28; ++step, delta *= factor) {
    for (std::size_t pattern = 0; pattern < patterns; ++pattern) {
      Circuit varied = circuit;
      GateDefinition varied_gate = gate;
      for (std::size_t i = 0; i < k; ++i) {
        const PhaseParam& p = params[i];
        const double shift = sign(pattern, i) * delta;
        auto clamp = [&](double v) { return std::clamp(v + shift, p.min_value, p.max_value); };
        bool is_input = false;
        for (auto& input : varied_gate.inputs)
          if (input.instance == p.instance && input.param == p.param) {
            input.value0 = clamp(input.value0);
            input.value1 = clamp(input.value1);
            is_input = true;
          }
        if (is_input) continue;
        circuit::Instance* instance = varied.find_instance(p.instance);
        const ComponentType* type = registry.find(instance->type);
        const auto it = instance->params.find(p.param);
        const double nominal_value =
            it != instance->params.end() ? it->second : type->find_param(p.param)->default_value;
        instance->params[p.param] = clamp(nominal_value);
      }
      if (!holds(varied, registry, varied_gate, wavelength, nominal)) return tolerance;
    }
    tolerance = delta;
  }
  return tolerance;
}

// Größte Verstimmung der Wellenlänge (in beide Richtungen), bei der die Logik noch hält; höchstens 20 nm.
double wavelength_tolerance(const Circuit& circuit, const ComponentRegistry& registry, const GateDefinition& gate,
                            double wavelength, const Nominal& nominal) {
  double tolerance = 0.0;
  const double factor = std::pow(20e-9 / 1e-12, 1.0 / 29.0);
  double delta = 1e-12;
  for (int step = 0; step < 30; ++step, delta *= factor) {
    if (!holds(circuit, registry, gate, wavelength + delta, nominal) ||
        !holds(circuit, registry, gate, wavelength - delta, nominal))
      return tolerance;
    tolerance = delta;
  }
  return tolerance;
}

// Leistung eines Ausgangs, wenn Eingang input_index stufenlos zwischen Bit 0 (x = 0) und Bit 1 (x = 1) steht.
double output_power_at(const Circuit& circuit, const ComponentRegistry& registry, const GateDefinition& gate,
                       double wavelength, std::size_t row, std::size_t input_index, double x, std::size_t output) {
  Circuit c = apply_inputs(circuit, gate, row);
  const LogicInput& input = gate.inputs[input_index];
  c.find_instance(input.instance)->params[input.param] = input.value0 + x * (input.value1 - input.value0);
  const CompiledCircuit compiled(c, registry);
  for (const auto& m : compiled.measurements(compiled.solve(wavelength)))
    if (m.instance == gate.outputs[output].instance && m.measurement.name == gate.outputs[output].measurement)
      return m.measurement.value;
  return 0.0;
}

}  // namespace

std::string_view to_string(Rating rating) {
  switch (rating) {
    case Rating::good:
      return "gut";
    case Rating::limited:
      return "eingeschränkt";
    case Rating::bad:
      return "nicht erfüllt";
    case Rating::not_rated:
      return "nicht bewertet";
  }
  return "nicht bewertet";
}

std::string format_db(double db) {
  if (db != db) return "-";
  if (db > 100.0) return "> 100 dB";
  if (db < -100.0) return "< -100 dB";
  return text("%.1f dB", db);
}

const Criterion* GateReport::find(std::string_view key) const {
  for (const auto& c : criteria)
    if (c.key == key) return &c;
  return nullptr;
}

CascadeResult build_cascade(const Circuit& circuit, const ComponentRegistry& registry, const GateDefinition& gate,
                            const std::vector<int>& function_bits, std::size_t output_index,
                            std::size_t input_index, Circuit* cascade) {
  CascadeResult result;
  const LogicInput& input = gate.inputs.at(input_index);
  const LogicOutput& output = gate.outputs.at(output_index);
  if (input.encoding != Encoding::intensity) {
    result.reason = "Eingang '" + input.name + "' ist " + std::string(adjective(input.encoding)) +
                    ", der Ausgang ist eine Lichtleistung (intensitätscodiert): die Codierungen passen nicht.";
    return result;
  }

  // Der Eingang muss eine Lichtquelle mit einem Port sein, der Ausgang ein Messgerät mit einem Port.
  auto single_port_partner = [&](const std::string& instance_name, circuit::PortRef* partner) {
    const circuit::Instance* instance = circuit.find_instance(instance_name);
    const ComponentType* type = instance ? registry.find(instance->type) : nullptr;
    if (!type || type->port_names.size() != 1) return false;
    const circuit::PortRef own{instance_name, type->port_names.front()};
    for (const auto& c : circuit.connections) {
      if (c.a == own) *partner = c.b;
      if (c.b == own) *partner = c.a;
      if (c.a == own || c.b == own) return true;
    }
    return false;
  };
  circuit::PortRef gate_in, gate_out;
  if (!single_port_partner(input.instance, &gate_in)) {
    result.reason = "Eingang '" + input.name + "' ist keine angeschlossene Lichtquelle mit einem Port.";
    return result;
  }
  if (!single_port_partner(output.instance, &gate_out)) {
    result.reason = "Ausgang '" + output.name + "' ist kein angeschlossenes Messgerät mit einem Port.";
    return result;
  }
  if (gate_out.instance == input.instance || gate_in.instance == output.instance) {
    result.reason = "Ein- und Ausgang sind direkt verbunden; es gibt kein Gatter dazwischen.";
    return result;
  }

  // Zwei Kopien: Stufe 1 ohne ihr Ausgangs-Messgerät, Stufe 2 ohne die Quelle des gewählten Eingangs.
  Circuit combined;
  combined.name = circuit.name + " (zweistufig)";
  auto copy_stage = [&](const std::string& prefix, const std::string& skip) {
    for (const auto& instance : circuit.instances)
      if (instance.name != skip)
        combined.instances.push_back(
            {prefix + instance.name, instance.type, instance.params, instance.grid_x, instance.grid_y});
    for (const auto& c : circuit.connections)
      if (c.a.instance != skip && c.b.instance != skip)
        combined.connections.push_back({{prefix + c.a.instance, c.a.port}, {prefix + c.b.instance, c.b.port}});
  };
  copy_stage("s1_", output.instance);
  copy_stage("s2_", input.instance);
  combined.connections.push_back({{"s1_" + gate_out.instance, gate_out.port}, {"s2_" + gate_in.instance, gate_in.port}});

  // Definition der zweistufigen Schaltung: alle Eingänge von Stufe 1, dann die übrigen von Stufe 2
  GateDefinition two_stage;
  two_stage.name = gate.name + " -> " + gate.name;
  for (const auto& in : gate.inputs) {
    LogicInput copy = in;
    copy.name = in.name + "1";
    copy.instance = "s1_" + in.instance;
    two_stage.inputs.push_back(copy);
  }
  for (std::size_t k = 0; k < gate.inputs.size(); ++k) {
    if (k == input_index) continue;
    LogicInput copy = gate.inputs[k];
    copy.name += "2";
    copy.instance = "s2_" + copy.instance;
    two_stage.inputs.push_back(copy);
  }
  if (two_stage.inputs.size() > max_inputs) {
    result.reason = "Die zweistufige Schaltung hätte zu viele Eingänge.";
    return result;
  }
  LogicOutput out = output;
  out.instance = "s2_" + output.instance;
  const std::size_t n = gate.inputs.size();
  out.expected.resize(two_stage.row_count());
  for (std::size_t row = 0; row < out.expected.size(); ++row) {
    // Zeile zerlegen: erst n Bits für Stufe 1, dann n - 1 Bits für Stufe 2
    const std::size_t rest_bits = n - 1;
    const std::size_t row1 = row >> rest_bits;
    const std::size_t rest = row & ((std::size_t{1} << rest_bits) - 1);
    const int y1 = function_bits.at(row1);
    std::size_t row2 = 0, taken = 0;
    for (std::size_t k = 0; k < n; ++k) {
      int bit = y1;
      if (k != input_index) {
        bit = static_cast<int>((rest >> (rest_bits - 1 - taken)) & 1u);
        ++taken;
      }
      row2 = (row2 << 1) | static_cast<std::size_t>(bit);
    }
    out.expected[row] = function_bits.at(row2);
  }
  two_stage.outputs.push_back(out);

  result.possible = true;
  result.reason = "Ausgang '" + output.name + "' der ersten Stufe ersetzt die Quelle von Eingang '" + input.name +
                  "' der zweiten.";
  result.gate = two_stage;
  if (cascade) *cascade = combined;
  return result;
}

GateReport characterize(const Circuit& circuit, const ComponentRegistry& registry, const GateDefinition& gate,
                        double wavelength) {
  GateReport report;
  report.gate_name = gate.name;
  report.wavelength = wavelength;
  report.table = run_truth_table(circuit, registry, gate, wavelength);

  // --- Funktion, Kontrast, Extinktion ---
  Nominal nominal;
  report.function_correct = true;
  report.worst_contrast_db = std::numeric_limits<double>::infinity();
  report.worst_extinction_db = std::numeric_limits<double>::infinity();
  std::string functions;
  bool all_expected = true, all_named = true;
  for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
    OutputAnalysis a = analyze_output(report.table, k, gate.outputs[k].expected);
    report.function_correct = report.function_correct && a.separable;
    if (a.contrast_db == a.contrast_db) report.worst_contrast_db = std::min(report.worst_contrast_db, a.contrast_db);
    else report.worst_contrast_db = std::numeric_limits<double>::quiet_NaN();
    if (a.extinction_db == a.extinction_db) report.worst_extinction_db = std::min(report.worst_extinction_db, a.extinction_db);
    all_expected = all_expected && a.has_expected;
    all_named = all_named && !a.function.empty();
    functions += (k ? ", " : "") + gate.outputs[k].name + " = " + (a.function.empty() ? "?" : a.function);
    nominal.bits.push_back(a.bits);
    nominal.threshold.push_back(a.threshold);
    report.outputs.push_back(std::move(a));
  }
  const bool ok = report.function_correct;
  const char* skipped = "Nicht messbar: die Logik stimmt schon im Nennzustand nicht.";

  {
    Criterion c{"function", "Logische Funktion", Rating::bad, functions, {}};
    if (ok && all_expected)
      c.rating = Rating::good, c.remark = "Alle Zeilen liegen auf der richtigen Seite des Entscheidungspegels.";
    else if (ok)
      c.rating = all_named ? Rating::good : Rating::limited,
      c.remark = "Keine Erwartung vorgegeben: Funktion aus der größten Pegellücke erkannt.";
    else
      c.remark = all_expected ? "Mindestens eine 0 liegt nicht unter jeder 1: kein Entscheidungspegel trennt die Zeilen."
                              : "Die Pegel lassen sich nicht in 0 und 1 trennen.";
    report.criteria.push_back(c);
  }
  {
    Criterion c{"contrast", "Kontrastverhältnis", Rating::bad, format_db(report.worst_contrast_db),
                "Schlechteste 1 gegen schlechteste 0 über alle Eingangskombinationen."};
    if (ok) c.rating = rate_above(report.worst_contrast_db, contrast_good_db, contrast_limited_db);
    report.criteria.push_back(c);
  }
  {
    Criterion c{"extinction", "Extinktionsverhältnis", Rating::bad, format_db(report.worst_extinction_db),
                "Größter gegen kleinsten Pegel eines Ausgangs."};
    c.rating = rate_above(report.worst_extinction_db, contrast_good_db, contrast_limited_db);
    report.criteria.push_back(c);
  }

  // --- Lösungen aller Zeilen: Quellleistung, Rückreflexion, Laufzeit ---
  double emitted_max = 0.0, worst_reflection = 0.0;
  report.latency = 0.0;
  const double d_lambda = 1e-12;
  for (std::size_t row = 0; row < gate.row_count(); ++row) {
    const Circuit c = apply_inputs(circuit, gate, row);
    const CompiledCircuit compiled(c, registry);
    const circuit::Solution solution = compiled.solve(wavelength);
    emitted_max = std::max(emitted_max, compiled.power_balance(solution).emitted);
    for (const auto& instance : c.instances) {
      const ComponentType* type = registry.find(instance.type);
      if (type->port_names.size() != 1) continue;
      const int port = compiled.port_index(instance.name, type->port_names.front());
      const double out = std::norm(solution.b(port)), in = std::norm(solution.a(port));
      if (out > 0.0) worst_reflection = std::max(worst_reflection, in / out);
    }
    // Gruppenlaufzeit dphi/domega zu jedem Ausgang, der in dieser Zeile eine 1 zeigt
    const circuit::Solution plus = compiled.solve(wavelength + d_lambda), minus = compiled.solve(wavelength - d_lambda);
    const double d_omega = omega_from_wavelength(wavelength + d_lambda) - omega_from_wavelength(wavelength - d_lambda);
    for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
      if (!nominal.bits[k][row]) continue;
      const circuit::Instance* instance = c.find_instance(gate.outputs[k].instance);
      const ComponentType* type = registry.find(instance->type);
      const int port = compiled.port_index(instance->name, type->port_names.front());
      if (std::norm(minus.a(port)) <= 0.0 || std::norm(plus.a(port)) <= 0.0) continue;
      report.latency = std::max(report.latency, std::arg(plus.a(port) / minus.a(port)) / d_omega);
    }
  }
  report.isolation_db = worst_reflection > 0.0 ? -10.0 * std::log10(worst_reflection)
                                               : std::numeric_limits<double>::infinity();

  // --- Kaskadierbarkeit ---
  std::size_t optical_inputs = 0;
  for (const auto& input : gate.inputs)
    if (input.encoding != Encoding::electrical) ++optical_inputs;
  {
    Criterion c{"cascade", "Kaskadierbarkeit", Rating::bad, "nein", {}};
    // erster intensitätscodierter Eingang, sonst der erste überhaupt (liefert dann die Begründung)
    std::size_t input_index = 0;
    for (std::size_t k = 0; k < gate.inputs.size(); ++k)
      if (gate.inputs[k].encoding == Encoding::intensity) {
        input_index = k;
        break;
      }
    if (!ok) {
      c.rating = Rating::not_rated, c.value = "-", c.remark = skipped;
    } else if (gate.inputs[input_index].encoding == Encoding::electrical) {
      c.remark = "Eingänge elektrisch, Ausgang optisch: eine zweite Stufe braucht Detektor und Treiber "
                 "dazwischen. Realistisch für hybride Systeme, aber keine rein optische Kaskade.";
      report.cascade.reason = c.remark;
    } else {
      Circuit combined;
      report.cascade = build_cascade(circuit, registry, gate, nominal.bits[0], 0, input_index, &combined);
      if (report.cascade.possible) {
        try {
          report.cascade.table = run_truth_table(combined, registry, report.cascade.gate, wavelength);
          report.cascade.logic_preserved = logic_holds(report.cascade.table, 0, report.cascade.gate.outputs[0].expected,
                                                       nominal.threshold[0]);
        } catch (const CircuitError& e) {
          report.cascade.reason += std::string(" Zweistufige Schaltung nicht lösbar: ") + e.what();
        }
        if (report.cascade.logic_preserved)
          c.rating = Rating::good, c.value = "ja",
          c.remark = "Zwei Stufen hintereinander rechnen mit demselben Entscheidungspegel richtig.";
        else
          c.remark = "Zwei Stufen hintereinander liefern falsche Bits: die Pegel der ersten Stufe passen "
                     "nicht zu den Eingangspegeln der zweiten.";
      } else {
        c.remark = report.cascade.reason;
      }
    }
    report.criteria.push_back(c);
  }
  const bool cascadable = report.cascade.possible && report.cascade.logic_preserved;

  // --- Fan-out ---
  {
    Criterion c{"fan_out", "Fan-out", Rating::not_rated, "-", {}};
    double p1_min = std::numeric_limits<double>::infinity();
    for (const auto& a : report.outputs) p1_min = std::min(p1_min, a.p1_min);
    if (!ok) {
      c.remark = skipped;
    } else if (optical_inputs == 0) {
      c.remark = "Elektrische Eingänge brauchen kein Licht; der Fan-out hängt an Detektor und Treiber, "
                 "die hier nicht modelliert sind.";
    } else {
      report.input_power = emitted_max / static_cast<double>(optical_inputs);
      report.fan_out = report.input_power > 0.0 ? p1_min / report.input_power : 0.0;
      c.value = text("%.2f", report.fan_out);
      if (report.fan_out >= 2.0 && cascadable)
        c.rating = Rating::good, c.remark = "Der 1-Pegel reicht für mindestens zwei Eingänge.";
      else if (report.fan_out >= 2.0)
        c.rating = Rating::limited,
        c.remark = "Die Leistung würde für zwei Eingänge reichen, das Gatter ist aber nicht kaskadierbar.";
      else
        c.rating = Rating::bad,
        c.remark = "1-Pegel am Ausgang geteilt durch die Quellleistung je Eingang ist kleiner als 2: "
                   "ohne Verstärkung lassen sich keine zwei Eingänge treiben.";
    }
    report.criteria.push_back(c);
  }

  // --- Abhängigkeit der Pegel von der Quellleistung (Verlust vor dem Gatter) ---
  if (ok) {
    const double scale = std::pow(10.0, -1.0 / 20.0);  // 1 dB weniger Leistung
    const TruthTable weaker = run_truth_table(circuit, registry, gate, wavelength, scale);
    report.level_slope_db_per_db = 0.0;
    for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
      const OutputAnalysis a = analyze_output(weaker, k, nominal.bits[k]);
      if (a.p1_min > 0.0 && report.outputs[k].p1_min > 0.0)
        report.level_slope_db_per_db = std::max(report.level_slope_db_per_db,
                                                10.0 * std::log10(report.outputs[k].p1_min / a.p1_min));
    }
  }

  // --- Pegelregeneration: Kennlinie Ausgang über Eingang ---
  {
    Criterion c{"regeneration", "Pegelregeneration", Rating::not_rated, "-", {}};
    if (!ok) {
      c.remark = skipped;
    } else {
      const double h = 0.02;
      report.end_slope = 0.0, report.mid_slope = 0.0;
      for (std::size_t i = 0; i < gate.inputs.size(); ++i)
        for (std::size_t row = 0; row < gate.row_count(); ++row) {
          if (input_bit(gate, row, i)) continue;  // jede Kombination der übrigen Eingänge einmal
          for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
            const double swing = report.outputs[k].p1_min - report.outputs[k].p0_max;
            auto p = [&](double x) { return output_power_at(circuit, registry, gate, wavelength, row, i, x, k); };
            const double at0 = std::abs(p(h) - p(0.0)) / h / swing, at1 = std::abs(p(1.0) - p(1.0 - h)) / h / swing;
            report.end_slope = std::max({report.end_slope, at0, at1});
            for (int j = 1; j < 20; ++j) {
              const double x = j / 20.0;
              report.mid_slope = std::max(report.mid_slope, std::abs(p(x + h / 2) - p(x - h / 2)) / h / swing);
            }
          }
        }
      const bool s_shaped = report.end_slope < 1.0 && report.mid_slope > 1.0;
      const bool level_restored = report.level_slope_db_per_db <= level_slope_limited;
      const bool same_encoding = std::all_of(gate.inputs.begin(), gate.inputs.end(),
                                             [](const LogicInput& in) { return in.encoding == Encoding::intensity; });
      c.value = report.end_slope > 100.0 ? "Steigung an den Pegeln > 100" : text("Steigung an den Pegeln %.2f", report.end_slope);
      const bool all_electrical = std::all_of(gate.inputs.begin(), gate.inputs.end(), [](const LogicInput& in) {
        return in.encoding == Encoding::electrical;
      });
      if (s_shaped && level_restored && same_encoding) {
        c.rating = Rating::good;
        c.remark = "Kennlinie flach an den Pegeln und steil dazwischen: Störungen werden gedämpft.";
      } else if (all_electrical) {
        c.rating = Rating::limited;
        c.remark = "Das optische Gatter stellt keine Pegel wieder her; das übernimmt die Elektronik "
                   "(Detektor und Treiber) zwischen den Stufen, die hier nicht modelliert ist.";
        if (!level_restored)
          c.remark += text(" Der optische 1-Pegel folgt der Quellleistung mit %.2f dB pro dB.",
                           report.level_slope_db_per_db);
      } else {
        c.rating = Rating::bad;
        std::string why;
        if (!s_shaped)
          why += "Die Kennlinie ist an den Logikpegeln nicht flach (normierte Steigung >= 1): Störungen am "
                 "Eingang erscheinen ungedämpft oder verstärkt am Ausgang. ";
        if (s_shaped && !same_encoding)
          why += "Bezüglich des Eingangs ist die Kennlinie zwar S-förmig, aber der Ausgang liegt in einer "
                 "anderen Codierung vor als der Eingang: das saubere Ausgangsbit hilft der nächsten Stufe nicht. ";
        if (!s_shaped && !same_encoding) why += "Außerdem ist der Ausgang anders codiert als der Eingang. ";
        if (!level_restored)
          why += text("Der 1-Pegel folgt der Quellleistung mit %.2f dB pro dB: ein lineares Bauteil stellt "
                      "keine Pegel wieder her, dafür braucht es eine nichtlineare Kennlinie.",
                      report.level_slope_db_per_db);
        c.remark = why;
      }
    }
    report.criteria.push_back(c);
  }

  // --- Eingang-Ausgang-Isolation ---
  {
    Criterion c{"isolation", "Eingang-Ausgang-Isolation", Rating::not_rated, format_db(report.isolation_db), {}};
    c.rating = rate_above(report.isolation_db, isolation_good_db, isolation_limited_db);
    c.remark = worst_reflection > 0.0
                   ? "Quellleistung gegen das Licht, das in die Quellen zurückläuft."
                   : "Kein Licht läuft in die Quellen zurück. Achtung: alle Bauteilmodelle sind reflexionsfrei, "
                     "das Ergebnis ist deshalb idealisiert.";
    report.criteria.push_back(c);
  }

  // --- Kein kritischer Bias: Toleranz gegen Phasenfehler und Verstimmung ---
  {
    Criterion c{"bias", "Kein kritischer Bias", Rating::not_rated, "-", {}};
    if (!ok) {
      c.remark = skipped;
    } else {
      const auto params = phase_params(circuit, registry);
      report.phase_parameter_count = static_cast<int>(params.size());
      report.phase_tolerance = phase_tolerance(circuit, registry, gate, wavelength, nominal, params);
      report.wavelength_tolerance = wavelength_tolerance(circuit, registry, gate, wavelength, nominal);
      const Rating wavelength_rating = rate_above(report.wavelength_tolerance, wavelength_good, wavelength_limited);
      const Rating phase_rating = params.empty() ? Rating::good
                                                 : rate_above(report.phase_tolerance, phase_good, phase_limited);
      c.rating = worse(wavelength_rating, phase_rating);
      const std::string wl = report.wavelength_tolerance >= 19.9e-9
                                 ? std::string("Wellenlänge >= 20 nm")
                                 : text("Wellenlänge +-%.3g nm", report.wavelength_tolerance / units::nm);
      const std::string ph = params.empty() ? std::string()
                                            : text(", Phase +-%.0f°", report.phase_tolerance * 180.0 / constants::pi);
      c.value = wl + ph;
      c.remark = "So weit dürfen alle Phasen der Schaltung gemeinsam (ungünstigste Vorzeichen) bzw. die "
                 "Wellenlänge abweichen, bevor ein Bit kippt.";
      if (wavelength_rating != Rating::good)
        c.remark += " Schmale Resonanzen: zum Vergleich driftet ein Silizium-Ring thermisch um etwa 0,07 nm pro Kelvin.";
    }
    report.criteria.push_back(c);
  }

  // --- Pegel unabhängig vom Verlust ---
  {
    Criterion c{"loss", "Pegel unabhängig vom Verlust", Rating::not_rated, "-", {}};
    if (!ok) {
      c.remark = skipped;
    } else {
      c.value = text("%.2f dB pro dB", report.level_slope_db_per_db);
      c.rating = report.level_slope_db_per_db <= level_slope_good      ? Rating::good
                 : report.level_slope_db_per_db <= level_slope_limited ? Rating::limited
                                                                       : Rating::bad;
      c.remark = c.rating == Rating::good
                     ? "Weniger Eingangslicht ändert den 1-Pegel kaum."
                     : "Jedes dB Verlust vor dem Gatter senkt den 1-Pegel entsprechend: in großen Schaltungen "
                       "summieren sich die Verluste, bis der Entscheidungspegel unterschritten wird.";
    }
    report.criteria.push_back(c);
  }

  // --- Angaben ohne Bewertung ---
  report.criteria.push_back({"energy", "Schaltenergie", Rating::not_rated, "-",
                             "Im statischen Modell nicht bestimmbar: braucht Bitdauer und Zeitbereich (Phase 9)."});
  if (report.latency < 1e-18) report.latency = 0.0;  // numerisches Rauschen der Ableitung
  report.criteria.push_back(
      {"latency", "Latenz", Rating::not_rated,
       report.latency > 0.0 ? text("%.3g ps", report.latency / units::ps) : std::string("0 ps"),
       report.latency > 0.0
           ? "Gruppenlaufzeit durch das Gatter; keine Schaltzeit und keine Bandbreite der Ansteuerung."
           : "Die Schaltung enthält keine Laufstrecken (keine Wellenleiter oder Ringe im Signalweg)."});

  for (const auto& c : report.criteria) {
    if (c.rating == Rating::not_rated) continue;
    ++report.rated_count;
    if (c.rating == Rating::good) ++report.good_count;
  }
  // "Echtes" Gatter nur, wenn nichts unbewertet bleibt, was bewertet werden müsste (Fan-out, Kaskade)
  const Criterion* fan = report.find("fan_out");
  report.real_gate = report.rated_count > 0 && report.good_count == report.rated_count &&
                     fan && fan->rating == Rating::good;
  return report;
}

}  // namespace photonlab::logic
