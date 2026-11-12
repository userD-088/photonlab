#pragma once
// Schnittstelle aller Bauteile der Schaltungs-Ebene.
// Ein Bauteil hat Ports und liefert pro Wellenlänge eine komplexe Streumatrix S mit b = S·a
// (a = einlaufende, b = auslaufende Wellen, |a|² = Leistung in Watt).
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include "photonlab/core/complex.hpp"

namespace photonlab::circuit {

using CMatrix = Eigen::MatrixXcd;
using CVector = Eigen::VectorXcd;

// Parameterwerte eines Bauteils, immer in SI (Ausnahmen tragen die Einheit im Namen).
using ParamMap = std::map<std::string, double>;

// Beschreibung eines Parameters: Wertebereich in SI, Anzeige-Einheit nur für Oberfläche und CLI.
struct ParamSpec {
  std::string name;           // Schlüssel in ParamMap und in der Projektdatei
  std::string label;          // deutscher Anzeigename
  double default_value = 0.0;
  double min_value = 0.0;
  double max_value = 1.0;
  std::string display_unit;   // z. B. "µm"
  double display_scale = 1.0; // SI-Wert = Anzeigewert * display_scale
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
};

}  // namespace photonlab::circuit
