#include "photonlab/modes/profile.hpp"

#include <algorithm>
#include <cmath>

#include <Eigen/Dense>

#include "photonlab/core/units.hpp"

namespace photonlab::modes {

double overlap_weight(double index, SlabPolarization polarization) {
  return polarization == SlabPolarization::te ? 1.0 : 1.0 / (index * index);
}

std::vector<ProfileMode> profile_modes(const std::vector<double>& index, double spacing, double wavelength,
                                       SlabPolarization polarization, int max_modes) {
  const auto n = static_cast<Eigen::Index>(index.size());
  if (n < 3) throw ModeError("Das Brechzahlprofil braucht mindestens 3 Punkte");
  if (!(spacing > 0.0) || !(wavelength > 0.0)) throw ModeError("Punktabstand und Wellenlänge müssen positiv sein");
  const double k0 = 2.0 * constants::pi / wavelength;
  const double inv_h2 = 1.0 / (spacing * spacing);
  const bool tm = polarization == SlabPolarization::tm;

  // TE:  E'' + k0²n²E = beta²E                         (symmetrisch tridiagonal)
  // TM:  n²(H'/n²)' + k0²n²H = beta²H. Mit D = diag(n²) ist der Operator D*S mit symmetrischem S;
  //      B = D^(1/2) S D^(1/2) hat dieselben Eigenwerte, Eigenvektor v gibt H = D^(1/2) v.
  Eigen::VectorXd diagonal(n), off(n - 1), scale(n);
  auto inverse_eps_between = [&](Eigen::Index i) {  // 1/n² zwischen den Punkten i und i + 1
    const double a = index[static_cast<std::size_t>(i)], b = index[static_cast<std::size_t>(i + 1)];
    return 0.5 * (1.0 / (a * a) + 1.0 / (b * b));
  };
  for (Eigen::Index i = 0; i < n; ++i) {
    const double ni = index[static_cast<std::size_t>(i)];
    scale(i) = tm ? ni : 1.0;
    if (!tm) {
      diagonal(i) = -2.0 * inv_h2 + k0 * k0 * ni * ni;
    } else {
      // jenseits der Enden gilt die Brechzahl des Randpunkts
      const double left = i > 0 ? inverse_eps_between(i - 1) : 1.0 / (ni * ni);
      const double right = i + 1 < n ? inverse_eps_between(i) : 1.0 / (ni * ni);
      diagonal(i) = ni * ni * (-(left + right) * inv_h2 + k0 * k0);
    }
  }
  for (Eigen::Index i = 0; i + 1 < n; ++i)
    off(i) = tm ? scale(i) * scale(i + 1) * inverse_eps_between(i) * inv_h2 : inv_h2;

  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver;
  solver.computeFromTridiagonal(diagonal, off);
  if (solver.info() != Eigen::Success) throw ModeError("Eigenwertproblem des Profils ist nicht konvergiert");

  const double n_cladding = std::max(index.front(), index.back());
  std::vector<ProfileMode> modes;
  // Eigenwerte sind aufsteigend sortiert: die größten beta² stehen hinten
  for (Eigen::Index k = n - 1; k >= 0 && static_cast<int>(modes.size()) < max_modes; --k) {
    const double beta2 = solver.eigenvalues()(k);
    if (!(beta2 > 0.0)) break;
    const double n_eff = std::sqrt(beta2) / k0;
    if (!(n_eff > n_cladding)) break;  // nicht mehr geführt
    ProfileMode mode;
    mode.order = static_cast<int>(modes.size());
    mode.n_eff = n_eff;
    mode.field.resize(index.size());
    double peak = 0.0;
    for (Eigen::Index i = 0; i < n; ++i) {
      const double value = scale(i) * solver.eigenvectors()(i, k);
      mode.field[static_cast<std::size_t>(i)] = value;
      if (std::abs(value) > std::abs(peak)) peak = value;
    }
    for (double& value : mode.field) value /= peak;  // Maximum = +1
    modes.push_back(std::move(mode));
  }
  return modes;
}

}  // namespace photonlab::modes
