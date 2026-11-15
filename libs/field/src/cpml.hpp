#pragma once
// CPML-Koeffizienten (intern, gemeinsam für 2D- und 3D-FDTD)
#include <algorithm>
#include <cmath>

#include "photonlab/core/units.hpp"

namespace photonlab::field::detail {

// CPML-Koeffizienten b und c an der Stelle u (in Zellen) einer Achse mit count Zellen.
// Polynomielles Leitfähigkeitsprofil (Grad 3) nach Roden/Gedney 2000 mit kappa = 1 und alpha = 0;
// sigma_max = 0.8*(m + 1)/(eta0*dx*n) ist der übliche Richtwert (Taflove/Hagness, Kap. 7).
inline void pml_coefficient(double u, int count, int pml_cells, double dx, double dt, double index, double* b,
                            double* c) {
  const double eta0 = constants::mu0 * constants::c0;  // Wellenwiderstand des Vakuums [Ohm]
  *b = 0.0, *c = 0.0;                                  // außerhalb der Schicht: psi bleibt 0
  double depth = 0.0;
  if (u < pml_cells)
    depth = (pml_cells - u) / pml_cells;
  else if (u > count - pml_cells)
    depth = (u - (count - pml_cells)) / pml_cells;
  if (depth <= 0.0) return;
  depth = std::min(depth, 1.0);
  const double order = 3.0;
  const double sigma_max = 0.8 * (order + 1.0) / (eta0 * dx * index);
  const double sigma = sigma_max * std::pow(depth, order);
  *b = std::exp(-sigma * dt / constants::eps0);
  *c = *b - 1.0;
}

}  // namespace photonlab::field::detail
