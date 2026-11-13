#pragma once
// Gatter-Definition des Logik-Labors: welche Parameter einer Schaltung die logischen Eingänge sind,
// welche Messgrößen die Ausgänge, und welche Funktion erwartet wird. Reine Daten, keine Physik.
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"

namespace photonlab::logic {

// Wie ein Bit im Signal steckt (Bauplan 7.1). Der Ausgang eines Gatters ist hier immer eine
// Detektorleistung, also intensitätscodiert.
enum class Encoding {
  intensity,   // kein Licht / Licht
  phase,       // Phase 0 / pi bei konstanter Leistung
  electrical,  // elektrisches Steuersignal (Heizer, Modulator); das Licht trägt nur das Ergebnis
};
std::string_view to_string(Encoding encoding);
std::string_view label(Encoding encoding);                     // deutscher Anzeigename
std::string_view adjective(Encoding encoding);                 // "phasencodiert", "intensitätscodiert", ...
Encoding encoding_from_string(std::string_view text);          // wirft CircuitError

// Logischer Eingang: Bit 0 und Bit 1 setzen einen Parameter einer Instanz auf zwei Werte (SI).
struct LogicInput {
  std::string name;      // z. B. "A"
  std::string instance;  // Instanz in der Schaltung
  std::string param;     // Parameter der Instanz
  double value0 = 0.0;
  double value1 = 1.0;
  Encoding encoding = Encoding::intensity;
  bool operator==(const LogicInput&) const = default;
};

// Logischer Ausgang: eine Messgröße in Watt (üblich: "Leistung" eines Detektors).
struct LogicOutput {
  std::string name;      // z. B. "Y"
  std::string instance;  // Instanz mit der Messgröße
  std::string measurement = "Leistung";
  // Erwartete Bits für alle 2^n Eingangskombinationen (Zeile = Eingänge als Binärzahl, erster Eingang
  // ist das höchstwertige Bit). Leer = keine Erwartung, die Funktion wird aus den Pegeln erkannt.
  std::vector<int> expected;
  bool operator==(const LogicOutput&) const = default;
};

struct GateDefinition {
  std::string name;
  std::vector<LogicInput> inputs;
  std::vector<LogicOutput> outputs;

  bool empty() const { return inputs.empty() && outputs.empty(); }
  std::size_t row_count() const { return std::size_t{1} << inputs.size(); }
  bool operator==(const GateDefinition&) const = default;
};

inline constexpr std::size_t max_inputs = 6;

// Prüft die Definition gegen Schaltung und Registry; wirft CircuitError mit Klartext.
void validate(const GateDefinition& gate, const circuit::Circuit& circuit,
              const circuit::ComponentRegistry& registry);

// Bit des Eingangs input in der Tabellenzeile row (erster Eingang = höchstwertiges Bit).
int input_bit(const GateDefinition& gate, std::size_t row, std::size_t input);

// Schaltung mit den Parameterwerten der Tabellenzeile row.
circuit::Circuit apply_inputs(const circuit::Circuit& circuit, const GateDefinition& gate, std::size_t row);

// Bekannte Funktionen mit einem oder zwei Eingängen: Namen und Wahrheitstabellen.
std::vector<std::string> function_names(std::size_t input_count);
std::vector<int> function_bits(std::string_view name, std::size_t input_count);  // leer, wenn unbekannt
std::string identify_function(const std::vector<int>& bits);                     // leer, wenn unbekannt

}  // namespace photonlab::logic
