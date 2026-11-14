#include "photonlab/pwe/pwe.hpp"

#include <Eigen/Dense>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <numbers>

#include "photonlab/core/complex.hpp"
#include "photonlab/core/parallel_for.hpp"

namespace photonlab::pwe {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr int kMaxPlaneWaves = 3000;  // dichte Matrizen: darüber wird die Rechnung unpraktisch langsam

struct Reciprocal {
  double b1x, b1y, b2x, b2y;  // [1/a], a_i * b_j = delta_ij
};

Reciprocal reciprocal(const UnitCell& cell) {
  const double det = cell.a1x * cell.a2y - cell.a1y * cell.a2x;
  return {cell.a2y / det, -cell.a2x / det, -cell.a1y / det, cell.a1x / det};
}

struct GVector {
  int m, n;
  double gx, gy;  // [1/a]
};

std::vector<GVector> g_vectors(const UnitCell& cell, double cutoff) {
  const Reciprocal b = reciprocal(cell);
  const int mmax = static_cast<int>(std::ceil(cutoff * std::hypot(cell.a1x, cell.a1y)));
  const int nmax = static_cast<int>(std::ceil(cutoff * std::hypot(cell.a2x, cell.a2y)));
  std::vector<GVector> list;
  for (int m = -mmax; m <= mmax; ++m)
    for (int n = -nmax; n <= nmax; ++n) {
      const double gx = m * b.b1x + n * b.b2x;
      const double gy = m * b.b1y + n * b.b2y;
      if (std::hypot(gx, gy) <= cutoff + 1e-12) list.push_back({m, n, gx, gy});
    }
  return list;
}

double sinc(double x) { return std::abs(x) < 1e-8 ? 1.0 : std::sin(x) / x; }

// Fourier-Koeffizient der Permittivität bei Gittervektor g [1/a]: (1/A) * Integral eps(r) exp(-i 2 pi g r) dA
cplx epsilon_coefficient(const UnitCell& cell, double gx, double gy) {
  const double area = std::abs(cell.a1x * cell.a2y - cell.a1y * cell.a2x);
  const double qx = kTwoPi * gx, qy = kTwoPi * gy;
  const double q = std::hypot(qx, qy);
  cplx sum = (q < 1e-12) ? cplx(cell.epsilon_background * area, 0.0) : cplx(0.0, 0.0);
  for (const Circle& c : cell.circles) {
    const double d = c.epsilon - cell.epsilon_background;
    const double qr = q * c.radius;
    const double shape = (qr < 1e-8) ? std::numbers::pi * c.radius * c.radius
                                     : kTwoPi * c.radius * c.radius * std::cyl_bessel_j(1.0, qr) / qr;
    sum += d * shape * std::exp(cplx(0.0, -(qx * c.x + qy * c.y)));
  }
  for (const Rectangle& r : cell.rectangles) {
    const double d = r.epsilon - cell.epsilon_background;
    const double shape = r.width * r.height * sinc(0.5 * qx * r.width) * sinc(0.5 * qy * r.height);
    sum += d * shape * std::exp(cplx(0.0, -(qx * r.x + qy * r.y)));
  }
  return sum / area;
}

// Tabelle der Koeffizienten über Indexdifferenzen (dm, dn) - Eintrag (i, j) der Matrix ist eps(G_i - G_j).
struct EpsilonTable {
  int mspan = 0, nspan = 0;
  std::vector<cplx> values;
  const cplx& at(int dm, int dn) const {
    return values[static_cast<std::size_t>((dm + mspan) * (2 * nspan + 1) + dn + nspan)];
  }
};

EpsilonTable build_table(const UnitCell& cell, const std::vector<GVector>& g) {
  const Reciprocal b = reciprocal(cell);
  int mmax = 0, nmax = 0;
  for (const GVector& v : g) {
    mmax = std::max(mmax, std::abs(v.m));
    nmax = std::max(nmax, std::abs(v.n));
  }
  EpsilonTable t;
  t.mspan = 2 * mmax;
  t.nspan = 2 * nmax;
  t.values.resize(static_cast<std::size_t>((2 * t.mspan + 1) * (2 * t.nspan + 1)));
  for (int dm = -t.mspan; dm <= t.mspan; ++dm)
    for (int dn = -t.nspan; dn <= t.nspan; ++dn) {
      const double gx = dm * b.b1x + dn * b.b2x;
      const double gy = dm * b.b1y + dn * b.b2y;
      t.values[static_cast<std::size_t>((dm + t.mspan) * (2 * t.nspan + 1) + dn + t.nspan)] =
          epsilon_coefficient(cell, gx, gy);
    }
  return t;
}

// Alles, was nicht vom Wellenvektor abhängt, einmal vorbereiten.
struct Prepared {
  std::vector<GVector> g;
  Eigen::MatrixXcd eps;      // eps(G_i - G_j)
  Eigen::MatrixXcd eps_inv;  // nur für hz
};

Prepared prepare(const UnitCell& cell, const BandSettings& settings) {
  Prepared p;
  p.g = g_vectors(cell, settings.cutoff);
  const int n = static_cast<int>(p.g.size());
  const EpsilonTable table = build_table(cell, p.g);
  p.eps.resize(n, n);
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) p.eps(i, j) = table.at(p.g[i].m - p.g[j].m, p.g[i].n - p.g[j].n);
  if (settings.polarization == Polarization::hz) {
    // Laurent-/"Inverse"-Regel: 1/eps als Inverse der eps-Matrix (Ho, Chan, Soukoulis 1990) - konvergiert
    // bei Sprüngen der Permittivität deutlich besser als die Fourier-Reihe von 1/eps.
    p.eps_inv = p.eps.llt().solve(Eigen::MatrixXcd::Identity(n, n));
  }
  return p;
}

std::vector<double> solve_prepared(const Prepared& p, const BandSettings& settings, KPoint k) {
  const int n = static_cast<int>(p.g.size());
  std::vector<double> kx(n), ky(n);
  for (int i = 0; i < n; ++i) {
    kx[i] = k.kx + p.g[i].gx;
    ky[i] = k.ky + p.g[i].gy;
  }
  Eigen::VectorXd values;
  if (settings.polarization == Polarization::ez) {
    // |k+G|^2 E = nu^2 * eps E  (verallgemeinert, eps positiv definit)
    Eigen::MatrixXcd a = Eigen::MatrixXcd::Zero(n, n);
    for (int i = 0; i < n; ++i) a(i, i) = kx[i] * kx[i] + ky[i] * ky[i];
    Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::MatrixXcd> es(a, p.eps, Eigen::EigenvaluesOnly);
    values = es.eigenvalues();
  } else {
    Eigen::MatrixXcd m(n, n);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) m(i, j) = (kx[i] * kx[j] + ky[i] * ky[j]) * p.eps_inv(i, j);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcd> es(m, Eigen::EigenvaluesOnly);
    values = es.eigenvalues();
  }
  const int count = std::min<int>(settings.bands, n);
  std::vector<double> f(count);
  for (int i = 0; i < count; ++i) f[i] = std::sqrt(std::max(0.0, values[i]));
  return f;
}

}  // namespace

UnitCell triangular_cell(double radius, double epsilon_background, double epsilon_inclusion) {
  UnitCell c;
  c.a1x = 1.0;
  c.a1y = 0.0;
  c.a2x = 0.5;
  c.a2y = std::sqrt(3.0) / 2.0;
  c.epsilon_background = epsilon_background;
  c.circles.push_back({0.0, 0.0, radius, epsilon_inclusion});
  return c;
}

UnitCell square_cell(double radius, double epsilon_background, double epsilon_inclusion) {
  UnitCell c;
  c.epsilon_background = epsilon_background;
  c.circles.push_back({0.0, 0.0, radius, epsilon_inclusion});
  return c;
}

BandPath standard_path(const UnitCell& cell, PathKind kind, int segment_points) {
  const Reciprocal b = reciprocal(cell);
  const KPoint gamma{0.0, 0.0};
  const KPoint m{0.5 * (b.b1x + b.b2x), 0.5 * (b.b1y + b.b2y)};
  std::vector<std::pair<KPoint, std::string>> corners;
  if (kind == PathKind::triangular) {
    const KPoint k{(2.0 * b.b1x + b.b2x) / 3.0, (2.0 * b.b1y + b.b2y) / 3.0};
    corners = {{gamma, "Gamma"}, {m, "M"}, {k, "K"}, {gamma, "Gamma"}};
  } else {
    const KPoint x{0.5 * b.b1x, 0.5 * b.b1y};
    corners = {{gamma, "Gamma"}, {x, "X"}, {m, "M"}, {gamma, "Gamma"}};
  }
  BandPath path;
  const int steps = std::max(1, segment_points);
  path.points.push_back(corners[0].first);
  path.distance.push_back(0.0);
  path.marks.emplace_back(0, corners[0].second);
  for (std::size_t s = 1; s < corners.size(); ++s) {
    const KPoint a = corners[s - 1].first, e = corners[s].first;
    for (int i = 1; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      const KPoint p{a.kx + t * (e.kx - a.kx), a.ky + t * (e.ky - a.ky)};
      const KPoint q = path.points.back();
      path.distance.push_back(path.distance.back() + std::hypot(p.kx - q.kx, p.ky - q.ky));
      path.points.push_back(p);
    }
    path.marks.emplace_back(static_cast<int>(path.points.size()) - 1, corners[s].second);
  }
  return path;
}

void validate(const UnitCell& cell, const BandSettings& settings) {
  const double det = cell.a1x * cell.a2y - cell.a1y * cell.a2x;
  if (!(std::abs(det) > 1e-9)) throw PweError("Die Gittervektoren sind linear abhängig.");
  if (!(cell.epsilon_background > 0.0)) throw PweError("Die Hintergrund-Permittivität muss positiv sein.");
  for (const Circle& c : cell.circles) {
    if (!(c.radius > 0.0)) throw PweError("Ein Loch hat keinen positiven Radius.");
    if (!(c.epsilon > 0.0)) throw PweError("Die Permittivität eines Lochs muss positiv sein.");
  }
  for (const Rectangle& r : cell.rectangles) {
    if (!(r.width > 0.0) || !(r.height > 0.0)) throw PweError("Ein Rechteck hat keine positive Ausdehnung.");
    if (!(r.epsilon > 0.0)) throw PweError("Die Permittivität eines Rechtecks muss positiv sein.");
  }
  if (!(settings.cutoff >= 1.0))
    throw PweError("Die Abschneidegrenze der ebenen Wellen muss mindestens 1 sein.");
  if (settings.bands < 1) throw PweError("Es muss mindestens ein Band berechnet werden.");
  const int count = static_cast<int>(g_vectors(cell, settings.cutoff).size());
  if (count > kMaxPlaneWaves)
    throw PweError("Zu viele ebene Wellen (" + std::to_string(count) + " > " +
                   std::to_string(kMaxPlaneWaves) + "): Abschneidegrenze verkleinern.");
  if (settings.bands > count) throw PweError("Mehr Bänder verlangt, als ebene Wellen vorhanden sind.");
}

std::vector<double> solve_k(const UnitCell& cell, const BandSettings& settings, KPoint k) {
  validate(cell, settings);
  return solve_prepared(prepare(cell, settings), settings, k);
}

std::vector<Gap> find_gaps(const std::vector<std::vector<double>>& frequency, double min_ratio) {
  std::vector<Gap> gaps;
  if (frequency.empty()) return gaps;
  const int bands = static_cast<int>(frequency.front().size());
  for (int b = 0; b + 1 < bands; ++b) {
    double top = -1e300, bottom = 1e300;
    for (const auto& f : frequency) {
      top = std::max(top, f[b]);
      bottom = std::min(bottom, f[b + 1]);
    }
    if (bottom > top) {
      Gap g{b + 1, top, bottom};
      if (g.ratio() >= min_ratio) gaps.push_back(g);
    }
  }
  return gaps;
}

BandResult solve_bands(const UnitCell& cell, const BandSettings& settings, const BandPath& path,
                       ThreadPool* pool, JobControl* control) {
  validate(cell, settings);
  const Prepared prepared = prepare(cell, settings);
  BandResult result;
  result.path = path;
  result.plane_waves = static_cast<int>(prepared.g.size());
  result.frequency.assign(path.points.size(), {});
  std::atomic<std::size_t> done{0};
  const bool completed = parallel_for(
      path.points.size(),
      [&](std::size_t i) {
        result.frequency[i] = solve_prepared(prepared, settings, path.points[i]);
        if (control) control->progress = static_cast<float>(++done) / static_cast<float>(path.points.size());
      },
      pool, control);
  if (!completed) {
    result.frequency.clear();
    return result;
  }
  result.gaps = find_gaps(result.frequency);
  return result;
}

}  // namespace photonlab::pwe
