#pragma once
// Parametrischer Wellenleiter-Querschnitt (Streifen oder Rippe) mit Materialien aus der Bibliothek:
// baut das Gitter, ruft den Modenlöser und leitet Kenngrößen ab (Gruppenindex, Einschluss, Modenfläche).
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/modes/fde.hpp"

namespace photonlab::modes {

struct CrossSection {
  std::string core = "Si";     // Material des Kerns (Name aus der Materialbibliothek)
  std::string lower = "SiO2";  // Material unter dem Kern (vergrabenes Oxid)
  std::string upper = "SiO2";  // Material über und neben dem Kern (Deckschicht oder Luft)
  double width = 500e-9;       // Breite des Kerns [m]
  double height = 220e-9;      // Höhe des Kerns [m]
  double slab_height = 0.0;    // Rippenwellenleiter: Höhe der durchgehenden Kernschicht [m]; 0 = Streifen
  double margin_x = 1.0e-6;    // Mantel links und rechts des Kerns im Rechengebiet [m]
  double margin_y = 0.8e-6;    // Mantel über und unter dem Kern [m]
  double cell_size = 20e-9;    // Gitterzelle [m]
  double wavelength = 1550e-9;
  FdePolarization polarization = FdePolarization::quasi_te;
  int modes = 4;
  bool operator==(const CrossSection&) const = default;
};

struct CrossSectionMode {
  double n_eff = 0.0;
  double n_group = 0.0;  // 0, wenn nicht berechnet
  bool guided = false;
  double confinement = 0.0;     // Anteil von |E|² im Kern (dominante Komponente)
  double effective_area = 0.0;  // (Integral |E|²)² / Integral |E|^4 [m²]
  std::vector<double> field;
};

struct CrossSectionResult {
  FdeGrid grid;
  std::vector<double> core_fraction;  // Flächenanteil des Kerns je Zelle (0..1)
  std::vector<CrossSectionMode> modes;
};

// Prüft den Querschnitt; wirft ModeError mit Klartext.
void validate(const CrossSection& section);

// Gitter mit Brechzahlen bei der angegebenen Wellenlänge (Sellmeier). Zellen, die eine Materialgrenze
// schneiden, erhalten die flächengewichtete Permittivität.
FdeGrid build_grid(const CrossSection& section, double wavelength,
                   std::vector<double>* core_fraction = nullptr);

// Moden des Querschnitts. with_group_index löst zusätzlich bei zwei benachbarten Wellenlängen
// (Material- und Wellenleiterdispersion): n_g = n_eff - lambda * dn_eff/dlambda.
// control (optional) meldet Fortschritt; Abbruch liefert ein Ergebnis ohne Moden.
CrossSectionResult analyze(const CrossSection& section, bool with_group_index = true,
                           JobControl* control = nullptr);

}  // namespace photonlab::modes
