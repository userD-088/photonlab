#include "photonlab/logic/reference_gates.hpp"

#include "photonlab/core/units.hpp"

namespace photonlab::logic {

bool has_reference_gate(std::string_view name) {
  return name == "xor_interference" || name == "and_interference" || name == "xor_directed";
}

GateDefinition reference_gate(std::string_view name) {
  const double pi = constants::pi;
  GateDefinition gate;
  if (name == "xor_interference") {
    // Bits stecken in der Phase der beiden Laser (0 oder pi), die Leistung bleibt konstant.
    gate.name = "Interferenz-XOR/XNOR";
    gate.inputs = {{"A", "laser_a", "phase", 0.0, pi, Encoding::phase},
                   {"B", "laser_b", "phase", 0.0, pi, Encoding::phase}};
    gate.outputs = {{"XOR", "det_xor", "Leistung", function_bits("XOR", 2)},
                    {"XNOR", "det_xnor", "Leistung", function_bits("XNOR", 2)}};
  } else if (name == "and_interference") {
    // Bits schalten die Laser aus und ein.
    gate.name = "Interferenz-AND";
    gate.inputs = {{"A", "laser_a", "power", 0.0, 1e-3, Encoding::intensity},
                   {"B", "laser_b", "power", 0.0, 1e-3, Encoding::intensity}};
    gate.outputs = {{"Y", "det", "Leistung", function_bits("AND", 2)}};
  } else if (name == "xor_directed") {
    // Bit 0: Ring in Resonanz (koppelt über). Bit 1: Abstimmphase pi/2 verstimmt den Ring (lässt durch).
    gate.name = "Directed Logic XOR/XNOR";
    gate.inputs = {{"A", "ring_a", "tuning_phase", 0.0, pi / 2, Encoding::electrical},
                   {"B", "ring_b", "tuning_phase", 0.0, pi / 2, Encoding::electrical}};
    gate.outputs = {{"XOR", "det_xor", "Leistung", function_bits("XOR", 2)},
                    {"XNOR", "det_xnor", "Leistung", function_bits("XNOR", 2)}};
  } else {
    throw circuit::CircuitError("Kein Referenz-Gatter '" + std::string(name) + "'");
  }
  return gate;
}

}  // namespace photonlab::logic
