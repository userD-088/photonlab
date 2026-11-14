#pragma once
// Gatter auf Feld-Ebene (Bauplan 7.3, Familie B): Eingänge sind Quellen einer FDTD-Szene (Bit 1 = Quelle an,
// Bit 0 = aus), Ausgänge sind Flussmonitore im stationären Zustand. Die Wahrheitstabelle entsteht aus je
// einer Rechnung pro Eingangskombination; Auswertung (Kontrastverhältnis, Pegel) wie auf der
// Schaltungs-Ebene über TruthTable und analyze_output. Die Leistung ist der zeitlich gemittelte
// Poynting-Fluss durch die Monitorlinie je Meter Bauteilhöhe [W/m]; nur Verhältnisse haben Bedeutung.
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/field/scene.hpp"
#include "photonlab/logic/report.hpp"
#include "photonlab/logic/truth_table.hpp"

namespace photonlab::logic {

struct FieldGateInput {
  std::string name;    // z. B. "A"
  std::string source;  // Name der Quelle in den Szenen (Quellen dieses Namens werden ein- bzw. ausgeschaltet)
};

struct FieldGateOutput {
  std::string name;           // z. B. "Summe"
  std::size_t scene = 0;      // Index in FieldGate::scenes
  std::string monitor;        // Name des Flussmonitors in dieser Szene
  std::vector<int> expected;  // Soll-Bits je Zeile (erster Eingang = höchstwertiges Bit)
};

struct FieldGate {
  std::string name;
  std::string description;
  std::vector<field::FieldScene> scenes;  // meist eine; ein Halbaddierer nutzt zwei parallele Gatter
  std::vector<FieldGateInput> inputs;
  std::vector<std::string> bias_sources;  // Quellen, die immer an sind (Vorspannung, z. B. beim NICHT)
  std::vector<FieldGateOutput> outputs;

  std::size_t row_count() const { return std::size_t{1} << inputs.size(); }
};

// Prüft die Definition (Quellen und Monitore vorhanden, Soll-Länge, Dauer > 0); wirft field::FieldError.
void validate(const FieldGate& gate);

// Setzt die Amplituden der Quellen für die Tabellenzeile row (Bit 0 = Amplitude 0).
std::vector<field::FieldScene> scenes_for_row(const FieldGate& gate, std::size_t row);

// Rechnet alle Zeilen (und Szenen) als parallele Aufgaben. Zeilen, in denen kein Licht eingespeist wird,
// werden nicht gerechnet (Leistung 0). Abbruch über control liefert eine leere Tabelle.
TruthTable run_field_truth_table(const FieldGate& gate, ThreadPool* pool = nullptr,
                                 JobControl* control = nullptr);

// Zeugnis eines Feld-Gatters: Wahrheitstabelle, Pegel und Kontrast je Ausgang, Ampeln. Bewertet wird nur,
// was gemessen wurde (Funktion und Kontrast, Schwellen wie auf der Schaltungs-Ebene, docs/physics/logic.md);
// Abstrahlung aus der Ebene, Toleranzen und Kaskadierbarkeit bleiben "nicht bewertet".
struct FieldGateReport {
  std::string gate_name;
  double wavelength = 0.0;
  TruthTable table;
  std::vector<OutputAnalysis> outputs;
  bool function_correct = false;
  double worst_contrast_db = 0.0;
  std::vector<Criterion> criteria;
};
FieldGateReport characterize_field_gate(const FieldGate& gate, ThreadPool* pool = nullptr,
                                        JobControl* control = nullptr);

// Vorgefertigte Gatter: "phc_or_and", "phc_xor", "phc_not", "phc_halfadder".
std::vector<std::string> field_gate_names();
FieldGate field_gate(std::string_view name);  // wirft field::FieldError bei unbekanntem Namen

}  // namespace photonlab::logic
