#include "photonlab/circuit/reference_designs.hpp"

#include "photonlab/circuit/registry.hpp"

namespace photonlab::circuit {

Circuit reference_mzi() {
  Circuit c;
  c.name = "Mach-Zehnder-Interferometer";
  c.add("laser", "laser", {{"power", 1e-3}}, 1, 3);
  c.add("koppler1", "coupler", {{"power_coupling", 0.5}}, 8, 3);
  c.add("phase", "phase_shifter", {{"phase", 0.0}}, 15, 3);
  c.add("koppler2", "coupler", {{"power_coupling", 0.5}}, 22, 3);
  c.add("det_bar", "detector", {}, 29, 3);
  c.add("det_cross", "detector", {}, 29, 5);
  c.connect("laser.out", "koppler1.in1");
  c.connect("koppler1.out1", "phase.in");
  c.connect("phase.out", "koppler2.in1");
  c.connect("koppler1.out2", "koppler2.in2");
  c.connect("koppler2.out1", "det_bar.in");
  c.connect("koppler2.out2", "det_cross.in");
  return c;
}

Circuit reference_mzi_unbalanced() {
  Circuit c;
  c.name = "Unbalanciertes Mach-Zehnder-Interferometer";
  c.add("laser", "laser", {{"power", 1e-3}}, 1, 3);
  c.add("koppler1", "coupler", {{"power_coupling", 0.5}}, 8, 3);
  c.add("arm_oben", "waveguide", {{"length", 200e-6}}, 15, 3);
  c.add("arm_unten", "waveguide", {{"length", 100e-6}}, 15, 5);
  c.add("koppler2", "coupler", {{"power_coupling", 0.5}}, 22, 3);
  c.add("det_bar", "detector", {}, 29, 3);
  c.add("det_cross", "detector", {}, 29, 5);
  c.connect("laser.out", "koppler1.in1");
  c.connect("koppler1.out1", "arm_oben.in");
  c.connect("koppler1.out2", "arm_unten.in");
  c.connect("arm_oben.out", "koppler2.in1");
  c.connect("arm_unten.out", "koppler2.in2");
  c.connect("koppler2.out1", "det_bar.in");
  c.connect("koppler2.out2", "det_cross.in");
  return c;
}

std::vector<std::string> reference_design_names() { return {"mzi", "mzi_unbalanced"}; }

Circuit reference_design(std::string_view name) {
  if (name == "mzi") return reference_mzi();
  if (name == "mzi_unbalanced") return reference_mzi_unbalanced();
  throw CircuitError("Unbekanntes Referenz-Design '" + std::string(name) + "'");
}

}  // namespace photonlab::circuit
