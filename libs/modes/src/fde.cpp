#include "photonlab/modes/fde.hpp"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "photonlab/core/units.hpp"
#include "photonlab/modes/profile.hpp"

namespace photonlab::modes {

std::vector<FdeMode> solve_modes(const FdeGrid& grid, const FdeSettings& settings) {
  const int nx = grid.nx, ny = grid.ny;
  if (nx < 3 || ny < 3) throw ModeError("Das Gitter braucht mindestens 3 x 3 Zellen");
  if (grid.index.size() != static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny))
    throw ModeError("Die Brechzahlverteilung passt nicht zur Gittergröße");
  if (!(grid.dx > 0.0) || !(grid.dy > 0.0) || !(settings.wavelength > 0.0))
    throw ModeError("Zellgrößen und Wellenlänge müssen positiv sein");
  const int total = nx * ny;
  const int wanted = std::clamp(settings.modes, 1, total - 2);

  const double k0 = 2.0 * constants::pi / settings.wavelength;
  auto eps = [&](int i, int j) {
    const double n =
        grid.index[static_cast<std::size_t>(i) + static_cast<std::size_t>(nx) * static_cast<std::size_t>(j)];
    return n * n;
  };

  // Semi-vektorielle Gleichung für die dominante E-Komponente (hier für quasi-TE, E entlang x):
  //   d/dx[(1/eps) d(eps*E)/dx] + d²E/dy² + k0²*eps*E = beta²*E
  // Entlang der Feldrichtung springt E an Materialgrenzen (eps*E ist stetig), quer dazu ist E stetig.
  // Für quasi-TM tauschen x und y die Rollen.
  const bool te = settings.polarization == FdePolarization::quasi_te;
  std::vector<Eigen::Triplet<double>> entries;
  entries.reserve(static_cast<std::size_t>(total) * 5);
  double eps_max = 0.0;
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      const int row = i + nx * j;
      const double e0 = eps(i, j);
      eps_max = std::max(eps_max, e0);
      double diagonal = k0 * k0 * e0;

      // Nachbar in einer Richtung: (di, dj), Zellgröße h, along = Richtung des dominanten Feldes
      auto couple = [&](int di, int dj, double h, bool along, EdgeCondition edge) {
        const int ni = i + di, nj = j + dj;
        const bool inside = ni >= 0 && ni < nx && nj >= 0 && nj < ny;
        const double inv_h2 = 1.0 / (h * h);
        if (!inside && edge == EdgeCondition::zero) {
          // Feld jenseits des Randes ist 0, das Material setzt sich fort: nur der Diagonalanteil bleibt
          diagonal -= inv_h2;
          return;
        }
        if (!inside) return;  // Spiegelrand: Nachbar gleich der Zelle selbst, die Differenz verschwindet
        const double e1 = eps(ni, nj);
        if (along) {
          // (1/eps_mitte) * (eps1*E1 - eps0*E0) / h² mit eps_mitte = (eps0 + eps1)/2
          const double mid = 0.5 * (e0 + e1);
          entries.emplace_back(row, ni + nx * nj, e1 / mid * inv_h2);
          diagonal -= e0 / mid * inv_h2;
        } else {
          entries.emplace_back(row, ni + nx * nj, inv_h2);
          diagonal -= inv_h2;
        }
      };
      couple(-1, 0, grid.dx, te, settings.x_edges);
      couple(+1, 0, grid.dx, te, settings.x_edges);
      couple(0, -1, grid.dy, !te, settings.y_edges);
      couple(0, +1, grid.dy, !te, settings.y_edges);
      entries.emplace_back(row, row, diagonal);
    }

  Eigen::SparseMatrix<double> matrix(total, total);
  matrix.setFromTriplets(entries.begin(), entries.end());
  matrix.makeCompressed();

  // Gesucht sind die größten beta². Verfahren: Unterraum-Iteration mit Shift-Invert und Rayleigh-Ritz.
  // (A - sigma)^-1 verstärkt die Eigenwerte nahe sigma; sigma liegt erst sicher über allen Eigenwerten
  // (k0²*eps_max) und rückt dann dicht über den größten gefundenen, damit die Iteration schnell konvergiert.
  // Jede gemeldete Mode ist über ihr Residuum |A x - beta² x| geprüft.
  const int block = std::min(total, wanted + 4);
  Eigen::MatrixXd basis(total, block);
  {
    // fester Startblock (keine Zufallszahlen: gleiche Eingabe, gleiches Ergebnis)
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    for (int c = 0; c < block; ++c)
      for (int r = 0; r < total; ++r) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        basis(r, c) = static_cast<double>(state >> 11) / 9007199254740992.0 - 0.5;
      }
  }
  Eigen::SparseMatrix<double> identity(total, total);
  identity.setIdentity();
  Eigen::SparseLU<Eigen::SparseMatrix<double>> lu;
  auto factorize = [&](double sigma) {
    const Eigen::SparseMatrix<double> shifted = matrix - sigma * identity;
    lu.compute(shifted);
    if (lu.info() != Eigen::Success)
      throw ModeError("Die Zerlegung des Eigenwertproblems ist fehlgeschlagen");
  };

  // Grenze der Führung. Oben und unten: die größte Brechzahl am Rand. Seitlich: der effektive Index der
  // Schichtmode der Randspalte (bei einer Rippe reicht die Kernschicht bis zum Rand und führt selbst Licht;
  // eine Mode der Rippe ist nur geführt, wenn ihr n_eff darüber liegt), sonst die größte Brechzahl der
  // Spalte.
  double n_edge = 0.0;
  for (int i = 0; i < nx; ++i) n_edge = std::max({n_edge, std::sqrt(eps(i, 0)), std::sqrt(eps(i, ny - 1))});
  for (int column : {0, nx - 1}) {
    std::vector<double> profile(static_cast<std::size_t>(ny));
    for (int j = 0; j < ny; ++j) profile[static_cast<std::size_t>(j)] = std::sqrt(eps(column, j));
    const auto slab = profile_modes(profile, grid.dy, settings.wavelength,
                                    te ? SlabPolarization::te : SlabPolarization::tm, 1);
    n_edge = std::max(n_edge, slab.empty() ? std::max(profile.front(), profile.back()) : slab.front().n_eff);
  }

  const double scale = k0 * k0;  // Eigenwerte in Einheiten von k0²: beta²/k0² = n_eff²
  double sigma = scale * eps_max * 1.0001;
  factorize(sigma);
  Eigen::VectorXd ritz_values = Eigen::VectorXd::Zero(block);
  Eigen::VectorXd residuals = Eigen::VectorXd::Constant(block, 1.0);
  const double tolerance = 1e-9;
  // Ablauf: mit dem fernen Shift iterieren, bis der größte Eigenwert grob feststeht; dann den Shift dicht
  // darüber legen (schnelle Konvergenz) und weiterrechnen, bis die verlangten Moden konvergiert sind.
  // Moden, die bis zum Ende nicht konvergieren (strahlende Kastenmoden), werden nicht gemeldet.
  bool shift_moved = false;
  int since_shift = 0;
  double previous_top = 0.0;
  for (int iteration = 0; iteration < 120; ++iteration) {
    // ein Schritt inverse Iteration auf dem ganzen Block, dann orthonormieren
    for (int c = 0; c < block; ++c) basis.col(c) = lu.solve(basis.col(c));
    Eigen::HouseholderQR<Eigen::MatrixXd> qr(basis);
    basis = qr.householderQ() * Eigen::MatrixXd::Identity(total, block);

    // Rayleigh-Ritz: Eigenwerte der kleinen Matrix Q^T A Q
    const Eigen::MatrixXd projected = basis.transpose() * (matrix * basis);
    Eigen::EigenSolver<Eigen::MatrixXd> small(projected);
    if (small.info() != Eigen::Success)
      throw ModeError("Das projizierte Eigenwertproblem ist nicht konvergiert");
    std::vector<int> order(static_cast<std::size_t>(block));
    for (int c = 0; c < block; ++c) order[static_cast<std::size_t>(c)] = c;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return small.eigenvalues()(a).real() > small.eigenvalues()(b).real(); });
    Eigen::MatrixXd rotation(block, block);
    for (int c = 0; c < block; ++c) {
      rotation.col(c) = small.eigenvectors().col(order[static_cast<std::size_t>(c)]).real();
      ritz_values(c) = small.eigenvalues()(order[static_cast<std::size_t>(c)]).real();
    }
    basis = basis * rotation;
    for (int c = 0; c < block; ++c) {
      basis.col(c).normalize();
      residuals(c) =
          (matrix * basis.col(c) - ritz_values(c) * basis.col(c)).norm() / std::abs(ritz_values(c));
    }

    if (!shift_moved) {
      // größter Eigenwert stabil auf 0,02 in n_eff²: Shift nachziehen (spätestens nach 40 Schritten)
      const bool stable = iteration >= 3 && std::abs(ritz_values(0) - previous_top) < 0.02 * scale;
      previous_top = ritz_values(0);
      if ((stable || iteration >= 40) && ritz_values(0) > 0.0) {
        const double target = ritz_values(0) + 0.2 * scale;
        if (target < sigma) {
          sigma = target;
          factorize(sigma);
        }
        shift_moved = true;
      }
      continue;
    }
    ++since_shift;
    bool converged = true;
    for (int c = 0; c < wanted; ++c)
      if (residuals(c) > tolerance) converged = false;
    if (converged || since_shift >= 40) break;
  }

  std::vector<FdeMode> modes;
  for (int c = 0; c < wanted; ++c) {
    const double beta2 = ritz_values(c);
    // nur geprüfte Moden melden: positive Ausbreitungskonstante und kleines Residuum
    if (!(beta2 > 0.0) || residuals(c) > 1e-6) continue;
    FdeMode mode;
    mode.n_eff = std::sqrt(beta2) / k0;
    mode.guided = mode.n_eff > n_edge;
    mode.field.resize(static_cast<std::size_t>(total));
    double peak = 0.0;
    for (int r = 0; r < total; ++r) {
      const double value = basis(r, c);
      mode.field[static_cast<std::size_t>(r)] = value;
      if (std::abs(value) > std::abs(peak)) peak = value;
    }
    if (peak == 0.0) continue;
    for (double& value : mode.field) value /= peak;
    modes.push_back(std::move(mode));
  }
  std::sort(modes.begin(), modes.end(), [](const FdeMode& a, const FdeMode& b) { return a.n_eff > b.n_eff; });
  return modes;
}

}  // namespace photonlab::modes
