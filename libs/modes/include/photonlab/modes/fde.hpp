#pragma once
// Modenlöser für Wellenleiter-Querschnitte: finite Differenzen, semi-vektoriell (Stern 1988).
// Gleichungen, Gitter und Grenzen des Verfahrens: docs/physics/modes.md
#include <vector>

#include "photonlab/modes/slab.hpp"

namespace photonlab::modes {

// Welche Feldkomponente gerechnet wird. x ist waagerecht (Breite), y senkrecht (Höhe des Wellenleiters).
enum class FdePolarization {
  quasi_te,  // E überwiegend waagerecht (Ex): die "TE"-Mode eines Streifenwellenleiters
  quasi_tm,  // E überwiegend senkrecht (Ey): die "TM"-Mode
};

enum class EdgeCondition {
  zero,       // Feld verschwindet am Rand (genug Mantel einplanen)
  symmetric,  // Feld setzt sich spiegelbildlich fort (Symmetrieebene oder unendlich breite Schicht)
};

struct FdeGrid {
  int nx = 0, ny = 0;
  double dx = 0.0, dy = 0.0;      // Zellgrößen [m]
  std::vector<double> index;      // Brechzahl je Zelle, Zeile für Zeile (i + nx*j)
};

struct FdeSettings {
  double wavelength = 1550e-9;
  FdePolarization polarization = FdePolarization::quasi_te;
  int modes = 4;                  // Anzahl der gesuchten Moden (größte n_eff zuerst)
  EdgeCondition x_edges = EdgeCondition::zero;
  EdgeCondition y_edges = EdgeCondition::zero;
};

struct FdeMode {
  double n_eff = 0.0;
  bool guided = false;            // n_eff größer als jede Brechzahl am Rand des Gitters
  std::vector<double> field;      // dominante E-Komponente je Zelle, Maximum des Betrags = 1
};

// Löst das Eigenwertproblem; Moden nach fallendem n_eff. Gemeldet werden nur Moden, deren Residuum
// klein ist; strahlende "Kastenmoden" unterhalb des Mantelindex konvergieren langsam und fehlen dann.
// Wirft ModeError bei ungültigen Eingaben.
std::vector<FdeMode> solve_modes(const FdeGrid& grid, const FdeSettings& settings);

}  // namespace photonlab::modes
