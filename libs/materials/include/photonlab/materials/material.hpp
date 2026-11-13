#pragma once
// Materialdaten: Brechzahl über Sellmeier-Gleichungen, daraus Gruppenindex und thermische Drift.
// Jede Zahl hat eine Quellenangabe in material.cpp; Gültigkeitsbereiche siehe docs/physics/materials.md.
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace photonlab::materials {

struct MaterialError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Ein Sellmeier-Term: B * lambda² / (lambda² - C²), C in Metern.
struct SellmeierTerm {
  double strength;   // B, dimensionslos
  double resonance;  // C [m]
};

struct Material {
  std::string name;                  // Schlüssel, z. B. "Si"
  std::string label;                 // deutscher Anzeigename
  std::string source;                // Literaturquelle der Sellmeier-Koeffizienten
  std::vector<SellmeierTerm> terms;  // n² = 1 + Summe der Terme; leer = konstante Brechzahl 1
  double valid_min = 0.0;            // Gültigkeitsbereich der Gleichung [m]
  double valid_max = 0.0;
  double thermo_optic = 0.0;  // dn/dT bei Raumtemperatur nahe 1550 nm [1/K]
  std::string thermo_optic_source;

  // Brechzahl bei der Vakuumwellenlänge [m] (reell; Absorption wird nicht modelliert).
  double index(double wavelength) const;
  // Gruppenindex n_g = n - lambda * dn/dlambda (analytische Ableitung der Sellmeier-Gleichung).
  double group_index(double wavelength) const;
  // Brechzahl bei einer Temperaturabweichung dT [K] in linearer Näherung.
  double index(double wavelength, double delta_temperature) const;
  bool in_valid_range(double wavelength) const;
};

// Eingebaute Materialien: Luft, SiO2, Si3N4, Si.
const std::vector<Material>& library();
const Material* find(std::string_view name);
const Material& get(std::string_view name);  // wirft MaterialError bei unbekanntem Namen

}  // namespace photonlab::materials
