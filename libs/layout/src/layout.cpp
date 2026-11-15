#include "photonlab/layout/layout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>

#include "photonlab/core/units.hpp"

namespace photonlab::layout {
namespace {

// Szenen-Koordinate [m] (y nach unten) -> Datenbank-Einheit (y nach oben)
Point to_point(double x, double y, double scene_height, double unit) {
  const double px = std::round(x / unit), py = std::round((scene_height - y) / unit);
  const double limit = static_cast<double>(std::numeric_limits<std::int32_t>::max());
  if (std::abs(px) > limit || std::abs(py) > limit)
    throw LayoutError("Koordinate außerhalb des Bereichs von GDSII (32 Bit in Datenbank-Einheiten)");
  return {static_cast<std::int32_t>(px), static_cast<std::int32_t>(py)};
}

// Ecken eines Kreises: so viele, dass die Sehne höchstens max_deviation vom Bogen abweicht
int circle_segments(double radius, const LayoutSettings& settings) {
  const double ratio = std::clamp(1.0 - settings.max_deviation / radius, -1.0, 1.0);
  const double angle = std::acos(ratio);  // halber Winkel je Segment
  const int n = angle > 0.0 ? static_cast<int>(std::ceil(constants::pi / angle)) : settings.min_segments;
  return std::clamp(n, settings.min_segments, 8000);
}

void make_ccw(Polygon& polygon) {
  if (signed_area(polygon) < 0.0) std::reverse(polygon.points.begin(), polygon.points.end());
}

std::string sanitize(const std::string& name) {
  std::string out;
  for (unsigned char ch : name) {
    if (out.size() >= 32) break;
    out += (std::isalnum(ch) && ch < 128) || ch == '_' ? static_cast<char>(ch) : '_';
  }
  if (out.empty()) out = "TOP";
  return out;
}

}  // namespace

double signed_area(const Polygon& polygon) {
  double sum = 0.0;
  const std::size_t n = polygon.points.size();
  for (std::size_t k = 0; k < n; ++k) {
    const Point& a = polygon.points[k];
    const Point& b = polygon.points[(k + 1) % n];
    sum += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
  }
  return 0.5 * sum;
}

Layout scene_to_layout(const field::FieldScene& scene, const LayoutSettings& settings) {
  if (scene.empty()) throw LayoutError("Die Feld-Szene ist leer");
  if (!(settings.max_deviation > 0.0)) throw LayoutError("Abweichung der Kreisbögen muss positiv sein");
  Layout layout;
  // Lagen: je Brechzahl eine, größte zuerst
  std::vector<double> indices;
  for (const auto& shape : scene.shapes) indices.push_back(shape.index);
  std::sort(indices.begin(), indices.end(), std::greater<>());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  for (std::size_t k = 0; k < indices.size(); ++k) {
    char name[48];
    std::snprintf(name, sizeof name, "n = %.4f%s", indices[k],
                  indices[k] == scene.background_index ? " (wie Hintergrund: Loch)" : "");
    layout.layers.push_back({static_cast<int>(k) + 1, indices[k], name});
  }
  auto layer_of = [&](double index) {
    const auto it = std::find(indices.begin(), indices.end(), index);
    return static_cast<int>(it - indices.begin()) + 1;
  };

  Cell cell;
  cell.name = sanitize(settings.cell_name.empty() ? scene.name : settings.cell_name);
  const double unit = layout.database_unit, height = scene.height;
  for (const auto& shape : scene.shapes) {
    Polygon polygon;
    polygon.layer = layer_of(shape.index);
    if (shape.kind == field::ShapeKind::rectangle) {
      const double c = std::cos(shape.rotation), s = std::sin(shape.rotation);
      const double corners[4][2] = {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
      for (const auto& corner : corners) {
        const double u = corner[0] * shape.width, v = corner[1] * shape.height;
        polygon.points.push_back(to_point(shape.x + c * u - s * v, shape.y + s * u + c * v, height, unit));
      }
    } else {
      const int n = circle_segments(shape.radius, settings);
      for (int k = 0; k < n; ++k) {
        const double a = 2.0 * constants::pi * k / n;
        polygon.points.push_back(to_point(shape.x + shape.radius * std::cos(a),
                                          shape.y + shape.radius * std::sin(a), height, unit));
      }
      if (shape.kind == field::ShapeKind::ring) {
        // Loch über eine Schnittkante bei Winkel 0: außen herum, hinein, innen gegenläufig, zurück
        make_ccw(polygon);
        const Point outer_start = polygon.points.front();
        const int m = circle_segments(shape.inner_radius, settings);
        std::vector<Point> inner;
        for (int k = 0; k < m; ++k) {
          const double a = 2.0 * constants::pi * k / m;
          inner.push_back(to_point(shape.x + shape.inner_radius * std::cos(a),
                                   shape.y + shape.inner_radius * std::sin(a), height, unit));
        }
        Polygon hole{0, 0, inner};
        if (signed_area(hole) > 0.0) std::reverse(inner.begin() + 1, inner.end());  // Start bleibt bei 0
        polygon.points.push_back(outer_start);
        for (const Point& p : inner) polygon.points.push_back(p);
        polygon.points.push_back(inner.front());
      }
    }
    make_ccw(polygon);
    if (polygon.points.size() > 8190)
      throw LayoutError("Polygon mit mehr als 8190 Ecken (Grenze von GDSII): größere Abweichung zulassen");
    cell.polygons.push_back(std::move(polygon));
  }
  layout.cells.push_back(std::move(cell));
  return layout;
}

std::vector<Polygon> pixels_to_polygons(const std::vector<double>& values, int nx, int ny, double x0,
                                        double y0, double dx, double scene_height, double threshold,
                                        int layer, double database_unit) {
  if (nx <= 0 || ny <= 0 || values.size() != static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny))
    throw LayoutError("Pixelbild: Größe passt nicht");
  // Läufe je Zeile; gleiche Läufe in aufeinanderfolgenden Zeilen zu einem Rechteck zusammenfassen
  struct Run {
    int i0, i1, j0, j1;  // Pixel [i0, i1) x [j0, j1)
  };
  std::vector<Run> open, done;
  for (int j = 0; j < ny; ++j) {
    std::vector<Run> row;
    for (int i = 0; i < nx;) {
      if (values[static_cast<std::size_t>(i + nx * j)] < threshold) {
        ++i;
        continue;
      }
      int end = i;
      while (end < nx && values[static_cast<std::size_t>(end + nx * j)] >= threshold) ++end;
      row.push_back({i, end, j, j + 1});
      i = end;
    }
    std::vector<Run> next;
    for (Run& run : row) {
      const auto it = std::find_if(open.begin(), open.end(),
                                   [&](const Run& o) { return o.i0 == run.i0 && o.i1 == run.i1; });
      if (it != open.end()) {
        Run extended = *it;
        extended.j1 = j + 1;
        open.erase(it);
        next.push_back(extended);
      } else {
        next.push_back(run);
      }
    }
    for (const Run& o : open) done.push_back(o);
    open = std::move(next);
  }
  for (const Run& o : open) done.push_back(o);
  std::vector<Polygon> polygons;
  for (const Run& r : done) {
    const double xa = x0 + r.i0 * dx, xb = x0 + r.i1 * dx, ya = y0 + r.j0 * dx, yb = y0 + r.j1 * dx;
    Polygon p;
    p.layer = layer;
    p.points = {to_point(xa, yb, scene_height, database_unit), to_point(xb, yb, scene_height, database_unit),
                to_point(xb, ya, scene_height, database_unit), to_point(xa, ya, scene_height, database_unit)};
    make_ccw(p);
    polygons.push_back(std::move(p));
  }
  return polygons;
}

Layout inverse_design_layout(const field::InverseDesignResult& result) {
  if (!result.completed || result.binary.empty()) throw LayoutError("Kein fertiges inverses Design");
  LayoutSettings settings;
  settings.cell_name = "INVERSE_DESIGN";
  Layout layout = scene_to_layout(result.scene, settings);
  const double n_core = std::sqrt(result.eps_core);
  int layer = 1;
  for (const auto& info : layout.layers)
    if (std::abs(info.index - n_core) < 1e-9) layer = info.layer;
  auto pixels = pixels_to_polygons(result.binary, result.nx, result.ny, result.pixel_x0, result.pixel_y0,
                                   result.pixel_size, result.scene.height, 0.5, layer, layout.database_unit);
  for (auto& p : pixels) layout.cells[0].polygons.push_back(std::move(p));
  return layout;
}

}  // namespace photonlab::layout
