#pragma once
// Gatter-Definitionen zu den Referenz-Designs der Schaltungs-Ebene (gleiche Namen wie in
// photonlab/circuit/reference_designs.hpp).
#include <string_view>

#include "photonlab/logic/gate.hpp"

namespace photonlab::logic {

bool has_reference_gate(std::string_view name);
GateDefinition reference_gate(std::string_view name);  // wirft CircuitError bei unbekanntem Namen

}  // namespace photonlab::logic
