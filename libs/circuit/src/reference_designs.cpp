#include "photonlab/circuit/reference_designs.hpp"

#include <cmath>

#include "photonlab/circuit/registry.hpp"
#include "photonlab/core/units.hpp"

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

Circuit reference_ring_allpass() {
  Circuit c;
  c.name = "Ringresonator (All-Pass)";
  c.add("laser", "laser", {{"power", 1e-3}}, 1, 3);
  c.add("ring", "ring_allpass", {{"radius", 10e-6}, {"power_coupling", 0.02}, {"loss_db_per_cm", 10.0}}, 8, 3);
  c.add("det", "detector", {}, 15, 3);
  c.connect("laser.out", "ring.in");
  c.connect("ring.out", "det.in");
  return c;
}

Circuit reference_ring_adddrop() {
  Circuit c;
  c.name = "Ringresonator (Add-Drop)";
  c.add("laser", "laser", {{"power", 1e-3}}, 1, 3);
  c.add("ring", "ring_adddrop",
        {{"radius", 10e-6}, {"power_coupling_1", 0.02}, {"power_coupling_2", 0.02}, {"loss_db_per_cm", 10.0}}, 8, 3);
  c.add("det_through", "detector", {}, 15, 3);
  c.add("det_drop", "detector", {}, 15, 5);
  c.connect("laser.out", "ring.in");
  c.connect("ring.through", "det_through.in");
  c.connect("ring.drop", "det_drop.in");
  return c;
}

std::vector<std::string> reference_design_names() {
  return {"mzi", "mzi_unbalanced", "ring_allpass", "ring_adddrop"};
}

Circuit reference_design(std::string_view name) {
  if (name == "mzi") return reference_mzi();
  if (name == "mzi_unbalanced") return reference_mzi_unbalanced();
  if (name == "ring_allpass") return reference_ring_allpass();
  if (name == "ring_adddrop") return reference_ring_adddrop();
  throw CircuitError("Unbekanntes Referenz-Design '" + std::string(name) + "'");
}

ReferenceSetup reference_setup(std::string_view name) {
  ReferenceSetup setup;
  setup.circuit = reference_design(name);
  // Die Streuungen sind Beispielwerte zum Ausprobieren, keine Daten eines realen Fertigungsprozesses.
  if (name == "mzi") {
    setup.monte_carlo.variations = {{"coupler", "power_coupling", 0.02, false, false}};
  } else if (name == "mzi_unbalanced") {
    setup.monte_carlo.variations = {{"waveguide", "n_eff", 2e-4, false, false}};
  } else {
    // Ringe: schmale Resonanzen brauchen einen feinen Sweep (5 pm Schrittweite)
    setup.sweep = {1540e-9, 1560e-9, 4001};
    // Arbeitswellenlänge auf die Resonanz nahe 1550 nm legen: Umlaufphase 2*pi*L*(n_g/lambda + (n_eff - n_g)/lambda0) = 2*pi*m
    const ComponentType& type = *ComponentRegistry::builtin().find(name);
    const Instance& ring = *setup.circuit.find_instance("ring");
    auto value = [&](const char* param) {
      const auto it = ring.params.find(param);
      return it != ring.params.end() ? it->second : type.find_param(param)->default_value;
    };
    const double length = 2.0 * constants::pi * value("radius");
    const double n_eff = value("n_eff"), n_group = value("n_group"), lambda0 = value("wavelength_ref");
    const double order = std::round(n_eff * length / lambda0);
    setup.wavelength = n_group * length / (order - length * (n_eff - n_group) / lambda0);
    setup.monte_carlo.variations = {{std::string(name), "n_eff", 1e-4, false, false}};
  }
  return setup;
}

}  // namespace photonlab::circuit
