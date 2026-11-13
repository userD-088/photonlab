#pragma once
// Optimierer des Logik-Labors (Bauplan 7.9): stimmt einen Parameter auf das beste Kontrastverhältnis ab.
#include <string>
#include <vector>

#include "photonlab/logic/truth_table.hpp"

namespace photonlab::logic {

struct OptimizeResult {
  std::vector<double> x;  // abgetastete Parameterwerte (SI)
  std::vector<double>
      contrast_db;     // schlechtestes Kontrastverhältnis aller Ausgänge; NaN, wenn die Logik nicht stimmt
  bool found = false;  // gibt es überhaupt einen Wert, bei dem die Logik stimmt?
  double best_x = 0.0;
  double best_contrast_db =
      0.0;  // auf 100 dB begrenzt (darüber ist "unendlich" numerisch nicht unterscheidbar)
};

// Schlechtestes Kontrastverhältnis aller Ausgänge; NaN, wenn ein Ausgang nicht trennbar ist oder die
// Schaltung nicht lösbar.
double worst_contrast_db(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                         const GateDefinition& gate, double wavelength);

// Rastert den Parameter zwischen lower und upper ab und verfeinert um das beste Rasterergebnis
// (Suche nach dem goldenen Schnitt). Findet bei mehreren Maxima das beste des Rasters, nicht garantiert das
// globale.
OptimizeResult optimize_parameter(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                                  const GateDefinition& gate, double wavelength, const std::string& instance,
                                  const std::string& param, double lower, double upper, int points = 121);

}  // namespace photonlab::logic
