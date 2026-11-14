#pragma once
// Schnittstelle aller Bauteile der Schaltungs-Ebene.
// Ein Bauteil hat Ports und liefert pro Wellenlänge eine komplexe Streumatrix S mit b = S·a
// (a = einlaufende, b = auslaufende Wellen, |a|² = Leistung in Watt).
#include <Eigen/Dense>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/core/complex.hpp"

namespace photonlab::circuit {

using CMatrix = Eigen::MatrixXcd;
using CVector = Eigen::VectorXcd;

// Parameterwerte eines Bauteils, immer in SI (Ausnahmen tragen die Einheit im Namen).
using ParamMap = std::map<std::string, double>;

// Beschreibung eines Parameters: Wertebereich in SI, Anzeige-Einheit nur für Oberfläche und CLI.
struct ParamSpec {
  std::string name;   // Schlüssel in ParamMap und in der Projektdatei
  std::string label;  // deutscher Anzeigename
  double default_value = 0.0;
  double min_value = 0.0;
  double max_value = 1.0;
  std::string display_unit;    // z. B. "µm"
  double display_scale = 1.0;  // SI-Wert = Anzeigewert * display_scale
};

// Messwert, den ein Bauteil aus den Wellen an seinen Ports ableitet (z. B. Detektorstrom).
struct Measurement {
  std::string name;
  double value = 0.0;
  std::string unit;
};

class Component {
 public:
  virtual ~Component() = default;

  virtual int port_count() const = 0;

  // Streumatrix (port_count × port_count) bei der Vakuumwellenlänge [m].
  virtual CMatrix s_matrix(double wavelength) const = 0;

  // Von selbst auslaufende Wellen (Quellen, z. B. Laser). Standard: keine.
  virtual CVector source(double wavelength) const;

  // Messwerte aus einlaufenden (a) und auslaufenden (b) Wellen an den eigenen Ports. Standard: keine.
  virtual std::vector<Measurement> measure(const CVector& a, const CVector& b) const;

  // --- Nichtlineare Bauteile (Phase 8, docs/physics/nonlinear.md) ---
  // Ein innerer Zustand (z. B. umlaufende Leistung im Ring, Verstärkung eines SOA) bestimmt die S-Matrix.
  // Der Löser sucht den Zustand, der zu den einlaufenden Wellen passt (Fixpunkt), und reicht den vorigen
  // Zustand als Startwert weiter: so folgt eine Rampe einem Ast der Lösung (Hysterese). Größe des Zustands; 0
  // = linear (Standard).
  virtual int state_size() const { return 0; }
  // S-Matrix bei gegebenem Zustand. Standard: s_matrix(wavelength).
  virtual CMatrix s_matrix_at(double wavelength, const std::vector<double>& state) const;
  // Zustand im Gleichgewicht zu den einlaufenden Wellen a. Gibt es mehrere, wird der dem vorigen Zustand
  // nächste gewählt. Standard: previous.
  virtual std::vector<double> settle_state(double wavelength, const CVector& a,
                                           const std::vector<double>& previous) const;
  // Messwerte mit Zustand. Standard: measure(a, b).
  virtual std::vector<Measurement> measure_at(const CVector& a, const CVector& b,
                                              const std::vector<double>& state) const;
};

}  // namespace photonlab::circuit
