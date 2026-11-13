#pragma once
// Gatter-Zeugnis (Bauplan 7.8): misst an einer Schaltung, welche Anforderungen an ein Logikelement
// erfüllt sind, und bewertet jede mit einer Ampel. Messvorschriften und Schwellen: docs/physics/logic.md
#include <string>
#include <vector>

#include "photonlab/logic/truth_table.hpp"

namespace photonlab::logic {

enum class Rating { good, limited, bad, not_rated };
std::string_view to_string(Rating rating);  // "gut", "eingeschränkt", "nicht erfüllt", "nicht bewertet"

struct Criterion {
  std::string key;     // stabiler Schlüssel, z. B. "contrast"
  std::string name;    // deutscher Anzeigename
  Rating rating = Rating::not_rated;
  std::string value;   // Messwert als Text
  std::string remark;  // Begründung bzw. Einschränkung
};

// Kaskaden-Test: Ausgang eines Gatters speist einen Eingang eines zweiten, identischen Gatters.
struct CascadeResult {
  bool possible = false;        // lässt sich optisch überhaupt verbinden?
  std::string reason;           // warum nicht, bzw. was verbunden wurde
  bool logic_preserved = false;
  GateDefinition gate;          // Definition der zweistufigen Schaltung
  TruthTable table;
};

struct GateReport {
  std::string gate_name;
  double wavelength = 0.0;
  TruthTable table;
  std::vector<OutputAnalysis> outputs;

  bool function_correct = false;
  double worst_contrast_db = 0.0;
  double worst_extinction_db = 0.0;
  int phase_parameter_count = 0;       // Anzahl der Phasen-Parameter (Einheit rad) in der Schaltung
  double phase_tolerance = 0.0;        // größter gemeinsamer Phasenfehler, bei dem die Logik hält [rad]
  double wavelength_tolerance = 0.0;   // größte Verstimmung, bei der die Logik hält [m]
  double level_slope_db_per_db = 0.0;  // Abfall des 1-Pegels in dB pro dB weniger Quellleistung
  double end_slope = 0.0;              // normierte Steigung der Kennlinie an den Logikpegeln
  double mid_slope = 0.0;              // größte normierte Steigung dazwischen
  double input_power = 0.0;            // optische Leistung je optischem Eingang [W]
  double fan_out = 0.0;                // schlechtester 1-Pegel / Leistung je Eingang
  double isolation_db = 0.0;           // Quellleistung / Rückreflexion in die Quelle
  double latency = 0.0;                // größte Gruppenlaufzeit zu einem Ausgang im 1-Zustand [s]
  CascadeResult cascade;

  std::vector<Criterion> criteria;
  int good_count = 0;
  int rated_count = 0;
  bool real_gate = false;  // alle bewerteten Kriterien grün

  const Criterion* find(std::string_view key) const;
};

// Zweistufige Schaltung für den Kaskaden-Test: Ausgang output_index von Stufe 1 ersetzt die Quelle
// des Eingangs input_index von Stufe 2. possible == false mit Begründung, wenn das nicht geht.
CascadeResult build_cascade(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                            const GateDefinition& gate, const std::vector<int>& function_bits,
                            std::size_t output_index, std::size_t input_index, circuit::Circuit* cascade);

// Vollständige Charakterisierung. Wirft CircuitError, wenn Definition oder Schaltung fehlerhaft sind.
GateReport characterize(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                        const GateDefinition& gate, double wavelength);

// Text mit einer Nachkommastelle, "> 100 dB" für sehr große und unendliche Werte.
std::string format_db(double db);

}  // namespace photonlab::logic
