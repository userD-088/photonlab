#include <algorithm>
#include <cmath>

#include "doctest/doctest.h"
#include "photonlab/field/photonic_crystal.hpp"

using namespace photonlab::field;

namespace {
CrystalLayout box_layout(LatticeKind kind) {
  CrystalLayout l;
  l.kind = kind;
  l.a = 400e-9;
  l.radius = 120e-9;
  l.x_min = 0.0, l.x_max = 12e-6, l.y_min = 0.0, l.y_max = 8e-6;
  l.origin_x = 1e-6, l.origin_y = 4e-6;
  return l;
}
}  // namespace

TEST_CASE("Kristallgenerator: Lochdichte entspricht der Elementarzelle") {
  for (const LatticeKind kind : {LatticeKind::triangular, LatticeKind::square}) {
    const CrystalLayout l = box_layout(kind);
    const double cell_area = kind == LatticeKind::triangular ? l.a * l.a * std::sqrt(3.0) / 2.0 : l.a * l.a;
    const double expected = (l.x_max - l.x_min) * (l.y_max - l.y_min) / cell_area;
    const double count = static_cast<double>(holes(l).size());
    CHECK(count ==
          doctest::Approx(expected).epsilon(0.07));  // Randeffekt: Löcher müssen ganz ins Gebiet passen
  }
}

TEST_CASE("Kristallgenerator: Nachbarabstand ist a, alle Löcher liegen im Gebiet") {
  const CrystalLayout l = box_layout(LatticeKind::triangular);
  const std::vector<Hole> list = holes(l);
  for (const Hole& h : list) {
    CHECK(h.x - h.radius >= l.x_min);
    CHECK(h.x + h.radius <= l.x_max);
    CHECK(h.y - h.radius >= l.y_min);
    CHECK(h.y + h.radius <= l.y_max);
  }
  // jedes innere Loch hat genau 6 Nachbarn im Abstand a
  const Hole& mid = *std::min_element(list.begin(), list.end(), [&](const Hole& p, const Hole& q) {
    return std::hypot(p.x - 6e-6, p.y - 4e-6) < std::hypot(q.x - 6e-6, q.y - 4e-6);
  });
  int neighbours = 0;
  for (const Hole& h : list)
    if (std::abs(std::hypot(h.x - mid.x, h.y - mid.y) - l.a) < 1e-12) ++neighbours;
  CHECK(neighbours == 6);
}

TEST_CASE("Kristallgenerator: W1 entfernt genau eine Reihe, Punktdefekt genau ein Loch") {
  CrystalLayout l = box_layout(LatticeKind::triangular);
  const std::size_t full = holes(l).size();
  const Site start = nearest_site(l, 1e-6, 4e-6);
  CHECK(start == Site{0, 0});
  remove_line(l, {-2, 0}, 0, 20);  // Reihe y = origin_y, von i = -2 bis 17 (x = 0,2 µm ... 7,8 µm)
  const std::vector<Hole> after = holes(l);
  CHECK(after.size() < full);
  for (const Hole& h : after) {
    const bool in_row_range = h.x >= 0.2e-6 - 1e-12 && h.x <= 7.8e-6 + 1e-12;
    if (in_row_range) CHECK(std::abs(h.y - 4e-6) > 1e-9);
  }
  CrystalLayout cavity = box_layout(LatticeKind::triangular);
  remove_site(cavity, {5, 0});
  remove_site(cavity, {5, 0});  // doppelt entfernen ist harmlos
  CHECK(holes(cavity).size() == full - 1);
  set_radius(cavity, {6, 0}, 90e-9);
  bool found = false;
  for (const Hole& h : holes(cavity)) {
    if (h.site == Site{6, 0}) {
      found = true;
      CHECK(h.radius == doctest::Approx(90e-9));
    }
  }
  CHECK(found);
}

TEST_CASE("Kristallgenerator: 60°-Knick folgt zwei Gitterrichtungen") {
  CrystalLayout l = box_layout(LatticeKind::triangular);
  const Site end = remove_path(l, {0, 0}, {{0, 6}, {1, 5}});
  // 6 Löcher in +x (0..5), dann 5 Löcher in Richtung a2: (5,1)...(5,5)
  CHECK(end == Site{5, 5});
  CHECK(l.removed.size() == 11);
  const auto [x, y] = site_position(l, end);
  CHECK(x == doctest::Approx(1e-6 + 5 * 400e-9 + 5 * 200e-9).epsilon(1e-9));
  CHECK(y == doctest::Approx(4e-6 - 5 * 0.5 * std::sqrt(3.0) * 400e-9).epsilon(1e-9));
}

TEST_CASE("Kristallgenerator: ungültige Eingaben") {
  CrystalLayout l = box_layout(LatticeKind::triangular);
  CHECK_THROWS_AS(lattice_step(LatticeKind::triangular, 6), FieldError);
  CHECK_THROWS_AS(lattice_step(LatticeKind::square, 4), FieldError);
  CHECK_THROWS_AS(remove_line(l, {0, 0}, 0, 0), FieldError);
  l.radius = 250e-9;
  CHECK_THROWS_AS(holes(l), FieldError);
  l.radius = 120e-9;
  const auto shapes = crystal_shapes(l, 1.444);
  CHECK(!shapes.empty());
  CHECK(shapes.front().kind == ShapeKind::circle);
  CHECK(shapes.front().index == doctest::Approx(1.444));
}
