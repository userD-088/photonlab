#include "photonlab/field/photonic_crystal.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace photonlab::field {

namespace {

double round_pico(double value) { return std::round(value * 1e12) / 1e12; }

struct Axes {
  double a1x, a1y, a2x, a2y;
};

Axes axes(const CrystalLayout& l) {
  if (l.kind == LatticeKind::triangular) return {l.a, 0.0, 0.5 * l.a, -0.5 * std::sqrt(3.0) * l.a};
  return {l.a, 0.0, 0.0, -l.a};
}

}  // namespace

std::pair<int, int> lattice_step(LatticeKind kind, int direction) {
  if (kind == LatticeKind::triangular) {
    static constexpr int steps[6][2] = {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};
    if (direction < 0 || direction > 5) throw FieldError("Gitterrichtung des Dreiecksgitters muss 0..5 sein");
    return {steps[direction][0], steps[direction][1]};
  }
  static constexpr int steps[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  if (direction < 0 || direction > 3) throw FieldError("Gitterrichtung des Quadratgitters muss 0..3 sein");
  return {steps[direction][0], steps[direction][1]};
}

std::pair<double, double> site_position(const CrystalLayout& layout, Site site) {
  const Axes ax = axes(layout);
  return {round_pico(layout.origin_x + site.i * ax.a1x + site.j * ax.a2x),
          round_pico(layout.origin_y + site.i * ax.a1y + site.j * ax.a2y)};
}

Site nearest_site(const CrystalLayout& layout, double x, double y) {
  const Axes ax = axes(layout);
  const double det = ax.a1x * ax.a2y - ax.a1y * ax.a2x;
  const double dx = x - layout.origin_x, dy = y - layout.origin_y;
  const double fi = (dx * ax.a2y - dy * ax.a2x) / det;
  const double fj = (ax.a1x * dy - ax.a1y * dx) / det;
  Site best{static_cast<int>(std::lround(fi)), static_cast<int>(std::lround(fj))};
  double best_distance = 1e300;
  for (int di = -1; di <= 1; ++di)  // Rundung der Koordinaten kann daneben liegen: Nachbarn prüfen
    for (int dj = -1; dj <= 1; ++dj) {
      const Site s{static_cast<int>(std::lround(fi)) + di, static_cast<int>(std::lround(fj)) + dj};
      const auto [px, py] = site_position(layout, s);
      const double d = std::hypot(px - x, py - y);
      if (d < best_distance) best_distance = d, best = s;
    }
  return best;
}

void remove_site(CrystalLayout& layout, Site site) {
  if (std::find(layout.removed.begin(), layout.removed.end(), site) == layout.removed.end())
    layout.removed.push_back(site);
}

void set_radius(CrystalLayout& layout, Site site, double radius) {
  if (!(radius > 0.0)) throw FieldError("Der Lochradius muss positiv sein");
  layout.radius_override[site] = radius;
}

Site remove_line(CrystalLayout& layout, Site start, int direction, int count) {
  if (count < 1) throw FieldError("Ein Liniendefekt braucht mindestens ein Loch");
  const auto [di, dj] = lattice_step(layout.kind, direction);
  Site s = start;
  for (int n = 0; n < count; ++n) {
    remove_site(layout, s);
    if (n + 1 < count) s = {s.i + di, s.j + dj};
  }
  return s;
}

Site remove_path(CrystalLayout& layout, Site start, const std::vector<PathLeg>& legs) {
  Site s = start;
  bool first = true;
  for (const PathLeg& leg : legs) {
    if (first) {
      s = remove_line(layout, s, leg.direction, leg.count);
      first = false;
    } else {  // der Anfang des Teilstücks ist der Endpunkt des vorigen: ein Schritt weiter, count Löcher
      const auto [di, dj] = lattice_step(layout.kind, leg.direction);
      s = remove_line(layout, {s.i + di, s.j + dj}, leg.direction, leg.count);
    }
  }
  return s;
}

std::vector<Hole> holes(const CrystalLayout& layout) {
  if (!(layout.a > 0.0)) throw FieldError("Die Gitterkonstante muss positiv sein");
  if (!(layout.radius > 0.0)) throw FieldError("Der Lochradius muss positiv sein");
  if (layout.radius >= 0.5 * layout.a)
    throw FieldError("Löcher überlappen: Radius muss kleiner als a/2 sein");
  const std::set<Site> removed(layout.removed.begin(), layout.removed.end());
  std::vector<Hole> list;
  const int span = static_cast<int>(std::ceil(
                       std::max(layout.x_max - layout.x_min, layout.y_max - layout.y_min) / layout.a)) *
                       2 +
                   4;
  const Site center =
      nearest_site(layout, 0.5 * (layout.x_min + layout.x_max), 0.5 * (layout.y_min + layout.y_max));
  for (int i = center.i - span; i <= center.i + span; ++i)
    for (int j = center.j - span; j <= center.j + span; ++j) {
      const Site s{i, j};
      if (removed.count(s)) continue;
      const auto [x, y] = site_position(layout, s);
      const auto it = layout.radius_override.find(s);
      const double r = it != layout.radius_override.end() ? it->second : layout.radius;
      if (x - r < layout.x_min || x + r > layout.x_max || y - r < layout.y_min || y + r > layout.y_max)
        continue;
      list.push_back({s, x, y, r});
    }
  std::sort(list.begin(), list.end(), [](const Hole& p, const Hole& q) {
    return p.y != q.y ? p.y < q.y : p.x < q.x;  // feste Reihenfolge: gleiche Beispieldatei auf allen Rechnern
  });
  return list;
}

std::vector<Shape> crystal_shapes(const CrystalLayout& layout, double hole_index, const std::string& prefix) {
  std::vector<Shape> shapes;
  for (const Hole& h : holes(layout)) {
    Shape s;
    s.name = prefix + "_" + std::to_string(h.site.i) + "_" + std::to_string(h.site.j);
    s.kind = ShapeKind::circle;
    s.x = h.x, s.y = h.y, s.radius = h.radius, s.index = hole_index;
    shapes.push_back(std::move(s));
  }
  return shapes;
}

}  // namespace photonlab::field
