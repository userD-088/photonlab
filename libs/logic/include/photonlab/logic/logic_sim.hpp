#pragma once
// Logik-Ebene (Phase 10, Bauplan 8): Logikblöcke mit gemessenen Eigenschaften (Brücke 2) und ein
// ereignisgesteuerter Simulator, der Logik und Physik zugleich rechnet. Jedes Netz trägt die physikalische
// Größe (optische Leistung und Phase) und den idealen Logikwert; wo ein Block einen Pegel anders liest als
// die ideale Logik verlangt, meldet der Simulator die Stelle mit Pegel und Schwelle statt ein geschöntes
// Ergebnis zu liefern. Messvorschriften und Festlegungen: docs/physics/logic_level.md
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/logic/gate.hpp"

namespace photonlab::logic {

// Signal auf einem Netz: physikalisch (Leistung, Phase) und der ideale Logikwert, den eine fehlerfreie
// Schaltung hätte.
struct Signal {
  double power = 0.0;  // [W]
  double phase = 0.0;  // [rad], relativ (Kohärenz zwischen Gattern ist angenommen, siehe logic_level.md)
  int bit = 0;         // idealer Logikwert
};

struct BlockPort {
  std::string name;
  Encoding encoding = Encoding::intensity;
  double threshold = 0.5e-3;  // Entscheidungspegel [W]: Eingang liest 1, wenn die Leistung darüber liegt
  double one_level = 1e-3;    // Nennpegel der 1 [W] (Eingang: wofür der Block ausgelegt ist)
  double zero_level = 0.0;    // Nennpegel der 0 [W]
  // Nur Ausgänge: gemessene schlechteste Pegel bei Nenneingängen (Charakterisierung)
  double worst_one = 1e-3, worst_zero = 0.0;
};

// Logikblock: abstraktes Gatter mit gemessenen Eigenschaften. evaluate rechnet die Physik, ideal die Logik.
class LogicBlock {
 public:
  virtual ~LogicBlock() = default;
  std::string type;    // z. B. "AND"
  std::string family;  // z. B. "passiv (Interferenz)"
  std::vector<BlockPort> inputs, outputs;
  double delay = 10e-12;          // Laufzeit Eingang -> Ausgang [s]
  double switching_energy = 0.0;  // je Pegelwechsel eines Ausgangs [J]
  double static_power = 0.0;      // dauernd verbrauchte Leistung (Laser, Empfänger) [W]
  bool regenerating = true;       // Ausgangspegel unabhängig von den Eingangspegeln
  std::string note;               // Modell in einem Satz

  // Physik: Ausgangssignale aus den Eingangssignalen; state = innerer Zustand (z. B. Flip-Flop)
  virtual std::vector<Signal> evaluate(const std::vector<Signal>& in, std::vector<double>& state) const = 0;
  // Ideale Logik (Referenz); ideal_state = gespeichertes Bit bei Flip-Flops
  virtual std::vector<int> ideal(const std::vector<int>& bits, int& ideal_state) const = 0;
  virtual int state_size() const { return 0; }
};
using BlockPtr = std::shared_ptr<const LogicBlock>;

// Gatter-Familien. ideal: Referenz ohne Kosten. regenerated: jedes Gatter liest optisch, entscheidet
// elektrisch und sendet mit eigenem Laser neu (O/E/O): Pegel werden wiederhergestellt, kostet Laufzeit und
// Energie. passive: echte Schaltungen aus dem Chip-Designer (Interferenz-Gatter, Kerr-Ring), Ausgang über den
// Schaltungslöser.
enum class Family { ideal, regenerated, passive };
std::string_view to_string(Family family);  // "ideal", "regeneriert", "passiv"
std::string_view label(Family family);      // deutscher Anzeigename
Family family_from_string(std::string_view text);

// Block einer Familie: "AND", "OR", "XOR", "NOT", "SR" (Flip-Flop mit Setzen/Rücksetzen, Ausgang Qn = NICHT
// Q).
BlockPtr make_block(Family family, std::string_view type);

// Netzliste der Logik-Ebene. Anschlüsse: "in:<Name>" (primärer Eingang), "out:<Name>" (primärer Ausgang),
// "<Instanz>.<Port>".
struct LogicInstance {
  std::string name;
  std::string type;  // Blocktyp für make_block
};
struct LogicWire {
  std::string from, to;
};
struct LogicCircuit {
  std::string name;
  std::vector<std::string> inputs, outputs;
  std::vector<LogicInstance> instances;
  std::vector<LogicWire> wires;
};

// Beispiel-Designs: Halbaddierer, Volladdierer, n-Bit-Addierer (Übertragskette), SR-Flip-Flop
LogicCircuit half_adder();
LogicCircuit full_adder();
LogicCircuit ripple_adder(int bits);      // Eingänge A<n-1>..A0, B<n-1>..B0, Cin; Ausgänge S0..S<n-1>, Cout
LogicCircuit and_chain(int stages);       // Kaskade von UND-Gattern: Y = A und B und ... (zeigt Pegelverfall)
LogicCircuit sr_latch();                  // Eingänge S, R; Ausgang Qn
std::vector<std::string> design_names();  // "half_adder", "full_adder", "adder4", "and_chain", "sr_latch"
LogicCircuit design(std::string_view name);

struct Issue {
  enum class Severity { error, warning, info } severity = Severity::warning;
  std::string where;  // Instanz oder Netz
  std::string text;
};

struct VectorResult {
  std::vector<int> inputs;
  std::vector<int> expected;  // ideale Ausgangsbits
  std::vector<int> decoded;   // physikalisch gelesene Ausgangsbits (Schwelle des treibenden Ausgangs)
  std::vector<double> power;  // Ausgangsleistungen [W]
  bool correct = true;
  bool checked = true;       // false: Steuerphase, Ausgang nicht bewertet (check_idle_only)
  double settle_time = 0.0;  // Zeit bis zum letzten Ereignis [s]
  std::string first_error;   // erste Stelle, an der ein Block falsch liest (leer, wenn keine)
};

struct NetTrace {
  std::string name;
  std::vector<double> time, power;
  std::vector<int> bit;  // ideale Werte
};

struct SimResult {
  std::vector<Issue> static_issues;  // Codierung, Pegel, Fan-out, Regeneration (vor der Simulation)
  std::vector<VectorResult> vectors;
  int correct = 0;                 // richtige unter den bewerteten Vektoren
  int checked = 0;                 // bewertete Vektoren
  double latency = 0.0;            // größte Einschwingzeit über alle Vektoren [s]
  double energy_per_vector = 0.0;  // Schaltenergie + statische Leistung * Periode, gemittelt [J]
  int block_count = 0;
  std::vector<NetTrace> traces;  // Zeitverlauf aller Netze (für das Zeitdiagramm)
  bool cancelled = false;
};

struct SimSettings {
  double period = 1e-9;     // Abstand der Eingangsvektoren [s]
  double input_one = 1e-3;  // Leistung der 1 an primären Eingängen [W]
  bool keep_traces = true;
  // Speicher (Flip-Flop): Ausgang nur bewerten, wenn alle Eingänge 0 sind (Haltephase); während eines Setz-
  // oder Rücksetzpulses ist er nicht definiert
  bool check_idle_only = false;
};

// Statische Prüfung: Codierung, Pegel (mit Fan-out-Aufteilung), Regeneration, offene Anschlüsse.
std::vector<Issue> check(const LogicCircuit& circuit, Family family, const SimSettings& settings = {});

// Ereignisgesteuerte Simulation der Vektoren (jeweils alle Eingänge, erster Eingang höchstwertig).
SimResult simulate(const LogicCircuit& circuit, Family family, const std::vector<std::vector<int>>& vectors,
                   const SimSettings& settings = {}, JobControl* control = nullptr);

// Alle 2^n Eingangsvektoren in Zählreihenfolge
std::vector<std::vector<int>> all_vectors(std::size_t inputs);

}  // namespace photonlab::logic
