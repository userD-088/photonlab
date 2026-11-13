#pragma once
// Wahrheitstabellen-Läufer und Schwellwert-Analyse (Bauplan 7.9).
// Definitionen und Annahmen: docs/physics/logic.md
#include <string>
#include <vector>

#include "photonlab/circuit/solver.hpp"
#include "photonlab/logic/gate.hpp"

namespace photonlab::logic {

struct TruthRow {
  std::vector<int> bits;      // Eingangsbits
  std::vector<double> power;  // gemessene Leistung je Ausgang [W]
};

struct TruthTable {
  std::vector<TruthRow> rows;  // 2^n Zeilen in Zählreihenfolge
};

// Simuliert alle 2^n Eingangskombinationen auf der Schaltungs-Ebene.
// source_scale skaliert alle Quellen (siehe CompiledCircuit::solve).
TruthTable run_truth_table(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                           const GateDefinition& gate, double wavelength, double source_scale = 1.0);

// Kontrastverhältnis CR = 10*log10(P1_min / P0_max); +unendlich bei P0_max = 0, NaN ohne gültige Pegel.
double contrast_ratio_db(double p1_min, double p0_max);

struct OutputAnalysis {
  bool has_expected = false;   // Erwartung vorgegeben (sonst aus der größten Pegellücke erkannt)
  std::vector<int> bits;       // zugeordnete Bits je Zeile (Erwartung bzw. Erkennung)
  std::string function;        // Name der Funktion, falls bekannt
  double p1_min = 0.0;         // schlechteste logische 1 [W]
  double p0_max = 0.0;         // schlechteste logische 0 [W]
  double p_max = 0.0, p_min = 0.0;  // größter und kleinster Pegel überhaupt [W]
  bool separable = false;      // jede 1 liegt über jeder 0 und es gibt beide
  double threshold = 0.0;      // Entscheidungspegel mittig zwischen P0_max und P1_min [W]
  double contrast_db = 0.0;    // Kontrastverhältnis
  double extinction_db = 0.0;  // 10*log10(P_max / P_min)
};

// Wertet eine Spalte der Wahrheitstabelle aus. expected leer: die Bits werden an der größten Lücke
// der sortierten Pegel getrennt (dann ist "separable" nur eine Aussage über diese Lücke).
OutputAnalysis analyze_output(const TruthTable& table, std::size_t output, const std::vector<int>& expected);

// Entscheidet jede Zeile mit einem festen Schwellwert und vergleicht mit den Bits.
bool logic_holds(const TruthTable& table, std::size_t output, const std::vector<int>& bits, double threshold);

}  // namespace photonlab::logic
