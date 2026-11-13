#pragma once
// Moden eines beliebigen eindimensionalen Brechzahlprofils (finite Differenzen). Liefert das Querprofil
// für Modenquellen und Modenmonitore im 2D-FDTD, wo jeder Wellenleiter ein Schichtwellenleiter in der Ebene
// ist.
#include <vector>

#include "photonlab/modes/slab.hpp"

namespace photonlab::modes {

struct ProfileMode {
  int order = 0;
  double n_eff = 0.0;
  std::vector<double> field;  // TE: E, TM: H; Maximum des Betrags = 1, dort positiv
};

// index: Brechzahl an gleichabständigen Punkten (Abstand spacing [m]); außerhalb gilt Feld = 0.
// Liefert höchstens max_modes geführte Moden (n_eff größer als die Brechzahl an beiden Enden),
// nach fallendem n_eff sortiert.
std::vector<ProfileMode> profile_modes(const std::vector<double>& index, double spacing, double wavelength,
                                       SlabPolarization polarization, int max_modes = 4);

// Gewicht des Skalarprodukts, unter dem die Moden orthogonal sind: TE 1, TM 1/n².
double overlap_weight(double index, SlabPolarization polarization);

}  // namespace photonlab::modes
