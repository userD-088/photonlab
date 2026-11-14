#pragma once
// Phasenwechselmaterial Ge2Sb2Te5 (GST-225) für nichtflüchtige photonische Speicher (Phase 9).
#include <complex>
#include <string>

namespace photonlab::materials {

struct PhaseChangeMaterial {
  std::string name;
  std::complex<double> index_amorphous;  // n + i k bei 1550 nm
  std::complex<double> index_crystalline;
  double crystallization_temperature;  // [K] ab hier kristallisiert das amorphe Material merklich
  double melting_temperature;          // [K]
  std::string source;
};

// GST-225 bei 1550 nm: n + ik = 4,4 + 0,098i (amorph) und 7,1 + 0,78i (kristallin), wie in "Reversible
// optical gate switching in Si wire waveguide integrated with Ge2Sb2Te5 thin film" verwendet; die Literatur
// streut stark (Delta n zwischen 0,1 und 3, je nach Schicht und Messung). Kristallisation ab etwa 430 K,
// Schmelzen bei etwa 890 K (Größenordnungen nach Wuttig und Yamada, Nature Materials 6, 824 (2007)).
const PhaseChangeMaterial& gst225();

}  // namespace photonlab::materials
