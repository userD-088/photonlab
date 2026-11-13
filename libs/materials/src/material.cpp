#include "photonlab/materials/material.hpp"

#include <cmath>

namespace photonlab::materials {

double Material::index(double wavelength) const {
  if (!(wavelength > 0.0)) throw MaterialError("Wellenlänge muss positiv sein");
  const double l2 = wavelength * wavelength;
  double n2 = 1.0;
  for (const auto& term : terms) n2 += term.strength * l2 / (l2 - term.resonance * term.resonance);
  if (!(n2 > 0.0))
    throw MaterialError("Sellmeier-Gleichung von '" + name + "' ist bei dieser Wellenlänge ungültig");
  return std::sqrt(n2);
}

double Material::group_index(double wavelength) const {
  // d(n²)/dlambda = Summe B * (-2*lambda*C²) / (lambda² - C²)²  und  dn/dlambda = d(n²)/dlambda / (2n)
  const double n = index(wavelength);
  const double l2 = wavelength * wavelength;
  double dn2 = 0.0;
  for (const auto& term : terms) {
    const double c2 = term.resonance * term.resonance;
    dn2 += term.strength * (-2.0 * wavelength * c2) / ((l2 - c2) * (l2 - c2));
  }
  return n - wavelength * dn2 / (2.0 * n);
}

double Material::index(double wavelength, double delta_temperature) const {
  return index(wavelength) + thermo_optic * delta_temperature;
}

bool Material::in_valid_range(double wavelength) const {
  return terms.empty() || (wavelength >= valid_min && wavelength <= valid_max);
}

const std::vector<Material>& library() {
  const double um = 1e-6;
  static const std::vector<Material> materials = {
      // Luft: n - 1 ist kleiner als 3e-4 und wird vernachlässigt.
      {"Luft", "Luft", "Vakuumwert (n - 1 < 3e-4)", {}, 0.0, 0.0, 0.0, ""},

      // Quarzglas (fused silica), 20 °C.
      // I. H. Malitson, "Interspecimen comparison of the refractive index of fused silica",
      // J. Opt. Soc. Am. 55, 1205-1209 (1965). Gültig 0,21 bis 3,71 µm.
      // dn/dT: etwa 1,0e-5 1/K bei 1550 nm; D. B. Leviton, B. J. Frey, Proc. SPIE 6273, 62732K (2006).
      {"SiO2",
       "SiO2 (Quarzglas)",
       "Malitson, J. Opt. Soc. Am. 55, 1205 (1965)",
       {{0.6961663, 0.0684043 * um}, {0.4079426, 0.1162414 * um}, {0.8974794, 9.896161 * um}},
       0.21 * um,
       3.71 * um,
       1.0e-5,
       "Leviton/Frey, Proc. SPIE 6273 (2006), ca."},

      // Stöchiometrisches LPCVD-Siliziumnitrid.
      // K. Luke, Y. Okawachi, M. R. E. Lamont, A. L. Gaeta, M. Lipson, "Broadband mid-infrared frequency
      // comb generation in a Si3N4 microresonator", Opt. Lett. 40, 4823-4826 (2015). Gültig 0,31 bis 5,5 µm.
      // dn/dT: 2,45e-5 1/K bei 1550 nm; A. Arbabi, L. L. Goddard, Opt. Lett. 38, 3878 (2013).
      {"Si3N4",
       "Si3N4 (Siliziumnitrid)",
       "Luke et al., Opt. Lett. 40, 4823 (2015)",
       {{3.0249, 0.1353406 * um}, {40314.0, 1239.842 * um}},
       0.31 * um,
       5.504 * um,
       2.45e-5,
       "Arbabi/Goddard, Opt. Lett. 38, 3878 (2013)"},

      // Kristallines Silizium, Raumtemperatur.
      // C. D. Salzberg, J. J. Villa, "Infrared refractive indexes of silicon, germanium and modified
      // selenium glass", J. Opt. Soc. Am. 47, 244-246 (1957); Sellmeier-Anpassung nach B. Tatian,
      // Appl. Opt. 23, 4477-4485 (1984). Gültig 1,36 bis 11 µm.
      // dn/dT: 1,8e-4 1/K bei 1550 nm (Bauplan 4.4); vgl. Komma et al., Appl. Phys. Lett. 101, 041905 (2012).
      {"Si",
       "Si (Silizium)",
       "Salzberg/Villa, J. Opt. Soc. Am. 47, 244 (1957); Tatian, Appl. Opt. 23, 4477 (1984)",
       {{10.6684293, 0.301516485 * um}, {0.0030434748, 1.13475115 * um}, {1.54133408, 1104.0 * um}},
       1.36 * um,
       11.0 * um,
       1.8e-4,
       "Komma et al., Appl. Phys. Lett. 101, 041905 (2012), ca."},
  };
  return materials;
}

const Material* find(std::string_view name) {
  for (const auto& m : library())
    if (m.name == name) return &m;
  return nullptr;
}

const Material& get(std::string_view name) {
  const Material* m = find(name);
  if (!m) throw MaterialError("Unbekanntes Material '" + std::string(name) + "'");
  return *m;
}

}  // namespace photonlab::materials
