#include "photonlab/logic/reference_gates.hpp"

#include <cmath>
#include <string>

#include "photonlab/core/units.hpp"

namespace photonlab::logic {

bool has_reference_gate(std::string_view name) {
  return name == "xor_interference" || name == "and_interference" || name == "xor_directed" ||
         name == "half_adder" || name == "full_adder" || name == "adder2" || name == "adder4" ||
         name == "adder8";
}

namespace {

// Heizleistung für die Phase pi (P_pi der Heizer im MZI-Schalter, siehe builtin_components.cpp)
constexpr double p_pi = 24.77e-3;

// Schwelle auf ganze Nanowatt gerundet: die Beispieldateien sollen auf jedem Compiler gleich sein
double round_nw(double watt) { return std::round(watt * 1e9) / 1e9; }

// Leistung auf dem Chip: Laser 10 mW durch den Gitterkoppler (4 dB bei 1550 nm)
double chip_power() { return 10e-3 * std::pow(10.0, -0.4); }

// Ripple-Carry-Addierer: Eingänge A(n-1)..A0, B(n-1)..B0, Cin; A schaltet Arm 1 von g, c, n und s, B Arm 2
// von c, n und s. Schwellen: halbe schlechteste 1 (Übertrag aus Cin, der durch alle Abgriffe läuft) -
// eine Festlegung, docs/physics/logic.md.
GateDefinition ripple_adder_gate(int bits) {
  GateDefinition gate;
  const bool single = bits == 1;
  gate.name = single ? "Volladdierer" : std::to_string(bits) + "-Bit-Addierer";
  auto bit_name = [&](const char* word, int i) { return single ? std::string(word) : word + std::to_string(i); };
  for (int i = bits - 1; i >= 0; --i) {
    const std::string n = std::to_string(i);
    gate.inputs.push_back(
        {bit_name("A", i), "g" + n, "power_a", 0.0, p_pi, Encoding::electrical, {"c" + n, "n" + n, "s" + n}});
  }
  for (int i = bits - 1; i >= 0; --i) {
    const std::string n = std::to_string(i);
    gate.inputs.push_back({bit_name("B", i), "c" + n, "power_b", 0.0, p_pi, Encoding::electrical, {"n" + n, "s" + n}});
  }
  gate.inputs.push_back({"Cin", "cin", "power_a", 0.0, p_pi, Encoding::electrical, {}});

  const double feed = chip_power() / (bits + 1), tap = 0.1;
  const std::size_t rows = gate.inputs.size() <= max_inputs ? gate.row_count() : 0;
  auto sum_of_row = [&](std::size_t row) {
    const std::size_t a = row >> (bits + 1), b = (row >> 1) & ((std::size_t{1} << bits) - 1);
    return a + b + (row & 1u);
  };
  auto expected = [&](int bit) {
    std::vector<int> e(rows);
    for (std::size_t row = 0; row < rows; ++row) e[row] = static_cast<int>((sum_of_row(row) >> bit) & 1u);
    return e;
  };
  LogicOutput cout{"Cout", "det_cout", "Leistung", expected(bits), round_nw(0.5 * std::pow(0.9, bits) * feed)};
  if (single) {
    gate.outputs.push_back({"S", "det_s0", "Leistung", expected(0), round_nw(0.5 * tap * feed)});
    gate.outputs.push_back(cout);
  } else {
    gate.outputs.push_back(cout);
    for (int i = bits - 1; i >= 0; --i)
      gate.outputs.push_back({"S" + std::to_string(i), "det_s" + std::to_string(i), "Leistung", expected(i),
                              round_nw(0.5 * tap * std::pow(0.9, i) * feed)});
  }
  return gate;
}

}  // namespace

GateDefinition reference_gate(std::string_view name) {
  const double pi = constants::pi;
  GateDefinition gate;
  if (name == "xor_interference") {
    // Bits stecken in der Phase der beiden Laser (0 oder pi), die Leistung bleibt konstant.
    gate.name = "Interferenz-XOR/XNOR";
    gate.inputs = {{"A", "laser_a", "phase", 0.0, pi, Encoding::phase, {}},
                   {"B", "laser_b", "phase", 0.0, pi, Encoding::phase, {}}};
    gate.outputs = {{"XOR", "det_xor", "Leistung", function_bits("XOR", 2)},
                    {"XNOR", "det_xnor", "Leistung", function_bits("XNOR", 2)}};
  } else if (name == "and_interference") {
    // Bits schalten die Laser aus und ein.
    gate.name = "Interferenz-AND";
    gate.inputs = {{"A", "laser_a", "power", 0.0, 1e-3, Encoding::intensity, {}},
                   {"B", "laser_b", "power", 0.0, 1e-3, Encoding::intensity, {}}};
    gate.outputs = {{"Y", "det", "Leistung", function_bits("AND", 2)}};
  } else if (name == "xor_directed") {
    // Bit 0: Ring in Resonanz (koppelt über). Bit 1: Abstimmphase pi/2 verstimmt den Ring (lässt durch).
    gate.name = "Directed Logic XOR/XNOR";
    gate.inputs = {{"A", "ring_a", "tuning_phase", 0.0, pi / 2, Encoding::electrical, {}},
                   {"B", "ring_b", "tuning_phase", 0.0, pi / 2, Encoding::electrical, {}}};
    gate.outputs = {{"XOR", "det_xor", "Leistung", function_bits("XOR", 2)},
                    {"XNOR", "det_xnor", "Leistung", function_bits("XNOR", 2)}};
  } else if (name == "half_adder") {
    // A und B heizen je einen Arm; Summe = A xor B (Schalter s), Übertrag = A und B (Schalter g, dann c)
    gate.name = "Halbaddierer";
    const double threshold = round_nw(0.25 * chip_power());
    gate.inputs = {{"A", "s", "power_a", 0.0, p_pi, Encoding::electrical, {"g", "c"}},
                   {"B", "s", "power_b", 0.0, p_pi, Encoding::electrical, {"c"}}};
    gate.outputs = {{"S", "det_s", "Leistung", function_bits("XOR", 2), threshold},
                    {"C", "det_c", "Leistung", function_bits("AND", 2), threshold}};
  } else if (name == "full_adder") {
    gate = ripple_adder_gate(1);
  } else if (name == "adder2") {
    gate = ripple_adder_gate(2);
  } else if (name == "adder4") {
    gate = ripple_adder_gate(4);
  } else if (name == "adder8") {
    gate = ripple_adder_gate(8);
  } else {
    throw circuit::CircuitError("Kein Referenz-Gatter '" + std::string(name) + "'");
  }
  return gate;
}

}  // namespace photonlab::logic
