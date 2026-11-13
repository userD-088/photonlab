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
  c.add("ring", "ring_allpass", {{"radius", 10e-6}, {"power_coupling", 0.02}, {"loss_db_per_cm", 10.0}}, 8,
        3);
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
        {{"radius", 10e-6}, {"power_coupling_1", 0.02}, {"power_coupling_2", 0.02}, {"loss_db_per_cm", 10.0}},
        8, 3);
  c.add("det_through", "detector", {}, 15, 3);
  c.add("det_drop", "detector", {}, 15, 5);
  c.connect("laser.out", "ring.in");
  c.connect("ring.through", "det_through.in");
  c.connect("ring.drop", "det_drop.in");
  return c;
}

// Design 5: Interferenz-XOR/XNOR. Zwei phasencodierte Laser (0 oder pi) treffen sich an einem 50:50-Koppler.
// Wegen der i-Phase des Kopplers braucht ein Arm die feste Bias-Phase pi/2.
Circuit reference_xor_interference() {
  Circuit c;
  c.name = "Interferenz-XOR/XNOR";
  c.add("laser_a", "laser", {{"power", 1e-3}}, 1, 3);
  c.add("laser_b", "laser", {{"power", 1e-3}}, 1, 5);
  c.add("bias", "phase_shifter", {{"phase", constants::pi / 2}}, 8, 5);
  c.add("koppler", "coupler", {{"power_coupling", 0.5}}, 15, 3);
  c.add("det_xor", "detector", {}, 22, 3);
  c.add("det_xnor", "detector", {}, 22, 5);
  c.connect("laser_a.out", "koppler.in1");
  c.connect("laser_b.out", "bias.in");
  c.connect("bias.out", "koppler.in2");
  c.connect("koppler.out1", "det_xor.in");
  c.connect("koppler.out2", "det_xnor.in");
  return c;
}

// Lineares "AND" mit Intensitätscodierung: derselbe Aufbau wie das XOR, aber die Bits schalten die Laser
// ein und aus, und die Bias-Phase -pi/2 lässt beide Felder an "det" konstruktiv zusammenlaufen.
// Pegel 0, P/2, P/2, 2P – nur 6 dB Kontrast und nicht kaskadierbar: bewusst ein Negativbeispiel.
Circuit reference_and_interference() {
  Circuit c;
  c.name = "Interferenz-AND (Intensität)";
  c.add("laser_a", "laser", {{"power", 1e-3}}, 1, 3);
  c.add("laser_b", "laser", {{"power", 1e-3}}, 1, 5);
  c.add("bias", "phase_shifter", {{"phase", -constants::pi / 2}}, 8, 5);
  c.add("koppler", "coupler", {{"power_coupling", 0.5}}, 15, 3);
  c.add("det", "detector", {}, 22, 3);
  c.add("det_rest", "detector", {}, 22, 5);
  c.connect("laser_a.out", "koppler.in1");
  c.connect("laser_b.out", "bias.in");
  c.connect("bias.out", "koppler.in2");
  c.connect("koppler.out1", "det.in");
  c.connect("koppler.out2", "det_rest.in");
  return c;
}

// Design 10: Directed Logic. Zwei elektrisch abgestimmte Add-Drop-Ringe als Weichen:
// in Resonanz koppelt ein Ring über (cross), verstimmt lässt er durch (bar). Ausgänge XOR und XNOR.
Circuit reference_xor_directed() {
  const ParamMap ring = {{"radius", 10e-6}, {"power_coupling_1", 0.05}, {"power_coupling_2", 0.05}};
  Circuit c;
  c.name = "Directed Logic XOR/XNOR (Ringe)";
  c.add("laser", "laser", {{"power", 1e-3}}, 1, 3);
  c.add("ring_a", "ring_adddrop", ring, 8, 3);
  c.add("ring_b", "ring_adddrop", ring, 15, 3);
  c.add("det_xnor", "detector", {}, 22, 3);
  c.add("det_xor", "detector", {}, 22, 5);
  c.connect("laser.out", "ring_a.in");
  c.connect("ring_a.through", "ring_b.in");
  c.connect("ring_a.drop", "ring_b.add");
  c.connect("ring_b.through", "det_xnor.in");
  c.connect("ring_b.drop", "det_xor.in");
  return c;
}

std::vector<std::string> reference_design_names() {
  return {"mzi",          "mzi_unbalanced",   "ring_allpass",
          "ring_adddrop", "xor_interference", "and_interference",
          "xor_directed"};
}

Circuit reference_design(std::string_view name) {
  if (name == "mzi") return reference_mzi();
  if (name == "mzi_unbalanced") return reference_mzi_unbalanced();
  if (name == "ring_allpass") return reference_ring_allpass();
  if (name == "ring_adddrop") return reference_ring_adddrop();
  if (name == "xor_interference") return reference_xor_interference();
  if (name == "and_interference") return reference_and_interference();
  if (name == "xor_directed") return reference_xor_directed();
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
  } else if (name == "xor_interference") {
    setup.monte_carlo.variations = {{"coupler", "power_coupling", 0.02, false, false},
                                    {"phase_shifter", "phase", 0.05, false, false}};
  } else if (name == "and_interference") {
    setup.monte_carlo.variations = {{"laser", "power", 0.05, true, false}};
  } else {
    // Ringe: schmale Resonanzen brauchen einen feinen Sweep (5 pm Schrittweite)
    setup.sweep = {1540e-9, 1560e-9, 4001};
    // Arbeitswellenlänge auf die Resonanz nahe 1550 nm legen:
    // Umlaufphase 2*pi*L*(n_g/lambda + (n_eff - n_g)/lambda0) = 2*pi*m
    const Instance* ring = nullptr;
    for (const auto& instance : setup.circuit.instances)
      if (instance.type.rfind("ring_", 0) == 0) {
        ring = &instance;
        break;
      }
    const ComponentType& type = *ComponentRegistry::builtin().find(ring->type);
    auto value = [&](const char* param) {
      const auto it = ring->params.find(param);
      return it != ring->params.end() ? it->second : type.find_param(param)->default_value;
    };
    const double length = 2.0 * constants::pi * value("radius");
    const double n_eff = value("n_eff"), n_group = value("n_group"), lambda0 = value("wavelength_ref");
    const double order = std::round(n_eff * length / lambda0);
    setup.wavelength = n_group * length / (order - length * (n_eff - n_group) / lambda0);
    setup.monte_carlo.variations = {{ring->type, "n_eff", 1e-4, false, false}};
  }
  return setup;
}

}  // namespace photonlab::circuit
