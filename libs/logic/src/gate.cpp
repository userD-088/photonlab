#include "photonlab/logic/gate.hpp"

#include <array>
#include <cmath>
#include <utility>

namespace photonlab::logic {

using circuit::CircuitError;

std::string_view to_string(Encoding encoding) {
  switch (encoding) {
    case Encoding::intensity:
      return "intensity";
    case Encoding::phase:
      return "phase";
    case Encoding::electrical:
      return "electrical";
  }
  return "intensity";
}

std::string_view label(Encoding encoding) {
  switch (encoding) {
    case Encoding::intensity:
      return "Intensität";
    case Encoding::phase:
      return "Phase";
    case Encoding::electrical:
      return "elektrisch";
  }
  return "Intensität";
}

std::string_view adjective(Encoding encoding) {
  switch (encoding) {
    case Encoding::intensity:
      return "intensitätscodiert";
    case Encoding::phase:
      return "phasencodiert";
    case Encoding::electrical:
      return "elektrisch gesteuert";
  }
  return "intensitätscodiert";
}

Encoding encoding_from_string(std::string_view text) {
  for (Encoding e : {Encoding::intensity, Encoding::phase, Encoding::electrical})
    if (to_string(e) == text) return e;
  throw CircuitError("Unbekannte Codierung '" + std::string(text) + "'");
}

void validate(const GateDefinition& gate, const circuit::Circuit& circuit,
              const circuit::ComponentRegistry& registry) {
  if (gate.inputs.empty()) throw CircuitError("Das Gatter hat keinen logischen Eingang");
  if (gate.outputs.empty()) throw CircuitError("Das Gatter hat keinen logischen Ausgang");
  if (gate.inputs.size() > max_inputs)
    throw CircuitError("Mehr als " + std::to_string(max_inputs) + " logische Eingänge werden nicht unterstützt");

  for (const auto& input : gate.inputs) {
    const circuit::Instance* instance = circuit.find_instance(input.instance);
    if (!instance)
      throw CircuitError("Eingang '" + input.name + "': Instanz '" + input.instance + "' gibt es nicht");
    const circuit::ComponentType* type = registry.find(instance->type);
    const circuit::ParamSpec* spec = type ? type->find_param(input.param) : nullptr;
    if (!spec)
      throw CircuitError("Eingang '" + input.name + "': '" + input.instance + "' hat keinen Parameter '" +
                         input.param + "'");
    for (double value : {input.value0, input.value1})
      if (!std::isfinite(value) || value < spec->min_value || value > spec->max_value)
        throw CircuitError("Eingang '" + input.name + "': Wert liegt außerhalb des Bereichs von '" +
                           input.param + "'");
    if (input.value0 == input.value1)
      throw CircuitError("Eingang '" + input.name + "': Bit 0 und Bit 1 haben denselben Wert");
  }
  for (const auto& output : gate.outputs) {
    if (!circuit.find_instance(output.instance))
      throw CircuitError("Ausgang '" + output.name + "': Instanz '" + output.instance + "' gibt es nicht");
    if (!output.expected.empty() && output.expected.size() != gate.row_count())
      throw CircuitError("Ausgang '" + output.name + "': die erwartete Funktion passt nicht zur Zahl der Eingänge");
    for (int bit : output.expected)
      if (bit != 0 && bit != 1) throw CircuitError("Ausgang '" + output.name + "': erwartete Bits müssen 0 oder 1 sein");
  }
}

int input_bit(const GateDefinition& gate, std::size_t row, std::size_t input) {
  return static_cast<int>((row >> (gate.inputs.size() - 1 - input)) & 1u);
}

circuit::Circuit apply_inputs(const circuit::Circuit& circuit, const GateDefinition& gate, std::size_t row) {
  circuit::Circuit result = circuit;
  for (std::size_t k = 0; k < gate.inputs.size(); ++k) {
    const LogicInput& input = gate.inputs[k];
    circuit::Instance* instance = result.find_instance(input.instance);
    if (!instance) throw CircuitError("Eingang '" + input.name + "': Instanz '" + input.instance + "' gibt es nicht");
    instance->params[input.param] = input_bit(gate, row, k) ? input.value1 : input.value0;
  }
  return result;
}

namespace {
struct NamedFunction {
  const char* name;
  std::size_t inputs;
  std::vector<int> bits;
};
const std::vector<NamedFunction>& named_functions() {
  static const std::vector<NamedFunction> functions = {
      {"BUFFER", 1, {0, 1}},        {"NOT", 1, {1, 0}},
      {"AND", 2, {0, 0, 0, 1}},     {"OR", 2, {0, 1, 1, 1}},
      {"XOR", 2, {0, 1, 1, 0}},     {"XNOR", 2, {1, 0, 0, 1}},
      {"NAND", 2, {1, 1, 1, 0}},    {"NOR", 2, {1, 0, 0, 0}},
      {"A", 2, {0, 0, 1, 1}},       {"B", 2, {0, 1, 0, 1}},
      {"NOT A", 2, {1, 1, 0, 0}},   {"NOT B", 2, {1, 0, 1, 0}},
      {"A AND NOT B", 2, {0, 0, 1, 0}}, {"B AND NOT A", 2, {0, 1, 0, 0}},
      {"A OR NOT B", 2, {1, 0, 1, 1}},  {"B OR NOT A", 2, {1, 1, 0, 1}},
  };
  return functions;
}
}  // namespace

std::vector<std::string> function_names(std::size_t input_count) {
  std::vector<std::string> names;
  for (const auto& f : named_functions())
    if (f.inputs == input_count) names.emplace_back(f.name);
  return names;
}

std::vector<int> function_bits(std::string_view name, std::size_t input_count) {
  for (const auto& f : named_functions())
    if (f.inputs == input_count && name == f.name) return f.bits;
  return {};
}

std::string identify_function(const std::vector<int>& bits) {
  for (const auto& f : named_functions())
    if (f.bits == bits) return f.name;
  return {};
}

}  // namespace photonlab::logic
