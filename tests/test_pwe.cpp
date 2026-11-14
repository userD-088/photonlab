#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "doctest/doctest.h"
#include "photonlab/pwe/pwe.hpp"

using namespace photonlab::pwe;

namespace {

constexpr double kPi = std::numbers::pi;

// Alle |k+G|/sqrt(eps) eines leeren Gitters, sortiert (unabhängige Berechnung ohne Matrizen).
std::vector<double> empty_lattice_frequencies(const UnitCell& cell, KPoint k, double eps) {
  const double det = cell.a1x * cell.a2y - cell.a1y * cell.a2x;
  const double b1x = cell.a2y / det, b1y = -cell.a2x / det, b2x = -cell.a1y / det, b2y = cell.a1x / det;
  std::vector<double> f;
  for (int m = -12; m <= 12; ++m)
    for (int n = -12; n <= 12; ++n)
      f.push_back(std::hypot(k.kx + m * b1x + n * b2x, k.ky + m * b1y + n * b2y) / std::sqrt(eps));
  std::sort(f.begin(), f.end());
  return f;
}

}  // namespace

TEST_CASE("PWE: leeres Gitter liefert exakt |k+G|/sqrt(eps) (beide Polarisationen)") {
  for (const Polarization pol : {Polarization::ez, Polarization::hz})
    for (const UnitCell& base : {triangular_cell(0.3, 2.25, 2.25), square_cell(0.3, 2.25, 2.25)}) {
      BandSettings s;
      s.polarization = pol;
      s.cutoff = 5.0;
      s.bands = 8;
      for (const KPoint k : {KPoint{0.0, 0.0}, KPoint{0.31, 0.12}, KPoint{0.5, 0.2}}) {
        const std::vector<double> got = solve_k(base, s, k);
        const std::vector<double> want = empty_lattice_frequencies(base, k, 2.25);
        for (int i = 0; i < 8; ++i) CHECK(got[i] == doctest::Approx(want[i]).epsilon(1e-9).scale(1.0));
      }
    }
}

TEST_CASE("PWE: Bragg-Stapel (1D) stimmt mit der analytischen Dispersionsrelation überein") {
  // Schichten d1 = 0.3a (eps1 = 12) und d2 = 0.7a (eps2 = 2): bei senkrechtem Einfall gilt für beide
  // Polarisationen cos(2 pi k) = cos(p1) cos(p2) - (n1/n2 + n2/n1)/2 * sin(p1) sin(p2), p_i = 2 pi nu n_i
  // d_i. Schmale Zelle in y (a2 = 0,1a): nur ebene Wellen mit G_y = 0 liegen unter der Abschneidegrenze.
  UnitCell cell;
  cell.a2y = 0.1;
  cell.epsilon_background = 2.0;
  cell.rectangles.push_back({0.0, 0.0, 0.3, 0.1, 12.0});
  const double n1 = std::sqrt(12.0), n2 = std::sqrt(2.0), d1 = 0.3, d2 = 0.7;
  auto dispersion = [&](double nu, double k) {
    const double p1 = 2 * kPi * nu * n1 * d1, p2 = 2 * kPi * nu * n2 * d2;
    return std::cos(p1) * std::cos(p2) - 0.5 * (n1 / n2 + n2 / n1) * std::sin(p1) * std::sin(p2) -
           std::cos(2 * kPi * k);
  };
  for (const Polarization pol : {Polarization::ez, Polarization::hz}) {
    BandSettings s;
    s.polarization = pol;
    s.cutoff = 40.0;  // Rechteck-Sprünge konvergieren nur algebraisch
    s.bands = 4;
    for (const double k : {0.1, 0.25, 0.4}) {
      const std::vector<double> f = solve_k(cell, s, {k, 0.0});
      for (int b = 0; b < 4; ++b) {
        // Residuum der Dispersionsrelation, bezogen auf ihre Steilheit: Frequenzfehler
        const double h = 1e-5;
        const double slope = (dispersion(f[b] + h, k) - dispersion(f[b] - h, k)) / (2 * h);
        CHECK(std::abs(dispersion(f[b], k) / slope) < 3e-3);
      }
    }
  }
}

TEST_CASE("PWE: Bandlücke Quadratgitter aus Stäben (Joannopoulos, Kap. 5)") {
  // Stäbe eps = 8,9 (Aluminiumoxid), r = 0,2a in Luft: TM-Lücke zwischen Band 1 und 2 bei etwa
  // a/lambda = 0,32 ... 0,44 (Wert aus dem Gedächtnis nach Joannopoulos, Abb. zu Kap. 5; Toleranz grob).
  const UnitCell cell = square_cell(0.2, 1.0, 8.9);
  BandSettings s;
  s.polarization = Polarization::ez;
  s.cutoff = 6.0;
  s.bands = 6;
  const BandResult r = solve_bands(cell, s, standard_path(cell, PathKind::square, 10));
  REQUIRE(!r.gaps.empty());
  CHECK(r.gaps.front().band == 1);
  CHECK(r.gaps.front().lower == doctest::Approx(0.32).epsilon(0.04));
  CHECK(r.gaps.front().upper == doctest::Approx(0.44).epsilon(0.04));
  // Es gibt keine TE-Lücke zwischen Band 1 und 2 (Stäbe: nur TM)
  s.polarization = Polarization::hz;
  const BandResult te = solve_bands(cell, s, standard_path(cell, PathKind::square, 10));
  for (const Gap& g : te.gaps) CHECK(g.band != 1);
}

TEST_CASE("PWE: Dreiecksgitter aus Luftlöchern hat eine vollständige Lücke (Joannopoulos, Kap. 5)") {
  // Löcher r = 0,48a in eps = 13: überlappende TE- und TM-Lücke (vollständige Lücke) bei a/lambda ~
  // 0,43-0,52.
  const UnitCell cell = triangular_cell(0.48, 13.0, 1.0);
  BandSettings s;
  s.cutoff = 7.0;
  s.bands = 6;
  const BandPath path = standard_path(cell, PathKind::triangular, 10);
  s.polarization = Polarization::ez;
  const BandResult tm = solve_bands(cell, s, path);
  s.polarization = Polarization::hz;
  const BandResult te = solve_bands(cell, s, path);
  REQUIRE(!tm.gaps.empty());
  REQUIRE(!te.gaps.empty());
  const Gap& g_tm = *std::find_if(tm.gaps.begin(), tm.gaps.end(), [](const Gap& g) { return g.band == 2; });
  const Gap& g_te = *std::find_if(te.gaps.begin(), te.gaps.end(), [](const Gap& g) { return g.band == 1; });
  const double lower = std::max(g_tm.lower, g_te.lower), upper = std::min(g_tm.upper, g_te.upper);
  REQUIRE(upper > lower);
  CHECK(lower == doctest::Approx(0.43).epsilon(0.03));
  CHECK(upper == doctest::Approx(0.52).epsilon(0.03));
}

TEST_CASE("PWE: Ergebnis konvergiert mit der Zahl der ebenen Wellen") {
  const UnitCell cell = triangular_cell(0.3, 12.11, 1.0);
  BandSettings s;
  s.polarization = Polarization::hz;
  s.bands = 3;
  const BandPath path = standard_path(cell, PathKind::triangular, 6);
  s.cutoff = 6.0;
  const BandResult a = solve_bands(cell, s, path);
  s.cutoff = 9.0;
  const BandResult b = solve_bands(cell, s, path);
  REQUIRE(!a.gaps.empty());
  REQUIRE(!b.gaps.empty());
  CHECK(a.gaps.front().lower == doctest::Approx(b.gaps.front().lower).epsilon(0.005));
  CHECK(a.gaps.front().upper == doctest::Approx(b.gaps.front().upper).epsilon(0.01));
}

TEST_CASE("PWE: Pfad Gamma-M-K-Gamma hat die bekannten Eckpunkte") {
  const UnitCell cell = triangular_cell(0.3, 12.0, 1.0);
  const BandPath path = standard_path(cell, PathKind::triangular, 4);
  REQUIRE(path.marks.size() == 4);
  const KPoint m = path.points[static_cast<std::size_t>(path.marks[1].first)];
  const KPoint k = path.points[static_cast<std::size_t>(path.marks[2].first)];
  CHECK(std::hypot(m.kx, m.ky) == doctest::Approx(1.0 / std::sqrt(3.0)).epsilon(1e-12));  // |M| = 1/(sqrt3 a)
  CHECK(std::hypot(k.kx, k.ky) == doctest::Approx(2.0 / 3.0).epsilon(1e-12));             // |K| = 2/(3a)
}

TEST_CASE("PWE: Eingaben werden geprüft") {
  BandSettings s;
  UnitCell cell = square_cell(0.2, 1.0, 12.0);
  CHECK_NOTHROW(validate(cell, s));
  cell.circles[0].radius = -1.0;
  CHECK_THROWS_AS(validate(cell, s), PweError);
  cell = square_cell(0.2, 1.0, 12.0);
  s.cutoff = 100.0;
  CHECK_THROWS_AS(validate(cell, s), PweError);
}
