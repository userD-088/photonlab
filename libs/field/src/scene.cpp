#include "photonlab/field/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "photonlab/core/units.hpp"
#include "photonlab/materials/material.hpp"
#include "photonlab/modes/slab.hpp"

namespace photonlab::field {

bool contains(const Shape& shape, double x, double y) {
  const double dx = x - shape.x, dy = y - shape.y;
  switch (shape.kind) {
    case ShapeKind::rectangle: {
      // in das gedrehte Koordinatensystem des Rechtecks wechseln
      const double c = std::cos(shape.rotation), s = std::sin(shape.rotation);
      const double u = c * dx + s * dy, v = -s * dx + c * dy;
      return std::abs(u) <= 0.5 * shape.width && std::abs(v) <= 0.5 * shape.height;
    }
    case ShapeKind::circle:
      return dx * dx + dy * dy <= shape.radius * shape.radius;
    case ShapeKind::ring: {
      const double r2 = dx * dx + dy * dy;
      return r2 <= shape.radius * shape.radius && r2 >= shape.inner_radius * shape.inner_radius;
    }
  }
  return false;
}

double index_at(const FieldScene& scene, double x, double y) {
  for (auto it = scene.shapes.rbegin(); it != scene.shapes.rend(); ++it)
    if (contains(*it, x, y)) return it->index;
  return scene.background_index;
}

double kerr_n2_at(const FieldScene& scene, double x, double y) {
  for (auto it = scene.shapes.rbegin(); it != scene.shapes.rend(); ++it)
    if (contains(*it, x, y)) return it->n2;
  return 0.0;
}

int grid_nx(const FieldScene& scene) { return static_cast<int>(std::lround(scene.width / scene.cell_size)); }
int grid_ny(const FieldScene& scene) { return static_cast<int>(std::lround(scene.height / scene.cell_size)); }

double time_step(const FieldScene& scene) {
  // Courant-Bedingung in 2D mit dx = dy: dt <= dx / (c0 * sqrt(2)). Maßgeblich ist die größte
  // Phasengeschwindigkeit, also c0 (Brechzahlen >= 1 machen das Gitter nur stabiler).
  return scene.courant * scene.cell_size / (constants::c0 * std::sqrt(2.0));
}

void validate(const FieldScene& scene) {
  if (scene.empty())
    throw FieldError("Die Szene hat kein Rechengebiet (Breite und Höhe müssen positiv sein)");
  if (!(scene.cell_size > 0.0)) throw FieldError("Die Zellgröße muss positiv sein");
  for (const auto& shape : scene.shapes) {
    if (shape.n2 != 0.0 && scene.polarization == Polarization::hz)
      throw FieldError("Kerr-Effekt (n2) ist nur in der Ez-Polarisation implementiert (Form '" + shape.name +
                       "')");
  }
  const double nx = scene.width / scene.cell_size, ny = scene.height / scene.cell_size;
  if (nx < 8 || ny < 8) throw FieldError("Das Rechengebiet braucht mindestens 8 x 8 Zellen");
  if (nx * ny > 4.0e6)
    throw FieldError("Das Gitter hätte mehr als 4 Millionen Zellen: Zellgröße oder Gebiet anpassen");
  if (!(scene.courant > 0.0) || scene.courant > 1.0)
    throw FieldError("Die Courant-Zahl muss in (0, 1] liegen, sonst ist das Verfahren instabil");
  if (!(scene.background_index >= 1.0))
    throw FieldError("Die Brechzahl des Hintergrunds muss mindestens 1 sein");
  if (scene.pml_cells < 0) throw FieldError("Die Dicke der Randschicht darf nicht negativ sein");
  if (scene.pml_cells > 0 &&
      ((scene.boundary_x == Boundary::pml && 2 * scene.pml_cells + 4 > grid_nx(scene)) ||
       (scene.boundary_y == Boundary::pml && 2 * scene.pml_cells + 4 > grid_ny(scene))))
    throw FieldError("Die Randschichten sind dicker als das Rechengebiet");
  if (!(scene.duration >= 0.0)) throw FieldError("Die Laufzeit darf nicht negativ sein");

  for (const auto& shape : scene.shapes) {
    const std::string who = "Form '" + shape.name + "': ";
    if (!(shape.index >= 1.0)) throw FieldError(who + "die Brechzahl muss mindestens 1 sein");
    if (shape.kind == ShapeKind::rectangle && (!(shape.width > 0.0) || !(shape.height > 0.0)))
      throw FieldError(who + "Breite und Höhe müssen positiv sein");
    if (shape.kind != ShapeKind::rectangle && !(shape.radius > 0.0))
      throw FieldError(who + "der Radius muss positiv sein");
    if (shape.kind == ShapeKind::ring && !(shape.inner_radius >= 0.0 && shape.inner_radius < shape.radius))
      throw FieldError(who + "der Innenradius muss kleiner als der Außenradius sein");
  }
  auto inside = [&](double x, double y) {
    return x >= 0.0 && x <= scene.width && y >= 0.0 && y <= scene.height;
  };
  for (const auto& source : scene.sources) {
    const std::string who = "Quelle '" + source.name + "': ";
    if (!(source.wavelength > 0.0)) throw FieldError(who + "die Wellenlänge muss positiv sein");
    if (!inside(source.x1, source.y1) || (source.kind != SourceKind::point && !inside(source.x2, source.y2)))
      throw FieldError(who + "liegt außerhalb des Rechengebiets");
    if (source.signal == SignalKind::pulse && !(source.pulse_duration > 0.0))
      throw FieldError(who + "die Pulsdauer muss positiv sein");
    if (source.kind == SourceKind::mode) {
      const bool vertical = std::abs(source.x2 - source.x1) < 0.5 * scene.cell_size;
      const bool horizontal = std::abs(source.y2 - source.y1) < 0.5 * scene.cell_size;
      if (vertical == horizontal)
        throw FieldError(who + "die Linie einer Modenquelle muss waagerecht oder senkrecht liegen");
      if (source.mode_order < 0) throw FieldError(who + "die Modenordnung darf nicht negativ sein");
    }
    if (!(source.ramp_periods >= 0.0) || !(source.beam_width >= 0.0))
      throw FieldError(who + "ungültige Einstellung");
  }
  for (const auto& monitor : scene.monitors) {
    const std::string who = "Monitor '" + monitor.name + "': ";
    if (!inside(monitor.x1, monitor.y1) ||
        (monitor.kind != MonitorKind::point && !inside(monitor.x2, monitor.y2)))
      throw FieldError(who + "liegt außerhalb des Rechengebiets");
    if (monitor.kind != MonitorKind::point) {
      const bool vertical = std::abs(monitor.x2 - monitor.x1) < 0.5 * scene.cell_size;
      const bool horizontal = std::abs(monitor.y2 - monitor.y1) < 0.5 * scene.cell_size;
      if (vertical == horizontal)
        throw FieldError(who + "die Linie muss waagerecht oder senkrecht liegen und eine Länge haben");
    }
  }
  for (const auto& port : scene.ports) {
    const std::string who = "Port '" + port.name + "': ";
    if (!inside(port.x1, port.y1) || !inside(port.x2, port.y2))
      throw FieldError(who + "liegt außerhalb des Rechengebiets");
    const bool vertical = std::abs(port.x2 - port.x1) < 0.5 * scene.cell_size;
    const bool horizontal = std::abs(port.y2 - port.y1) < 0.5 * scene.cell_size;
    if (vertical == horizontal)
      throw FieldError(who + "die Linie muss waagerecht oder senkrecht liegen und eine Länge haben");
    if (port.mode_order < 0) throw FieldError(who + "die Modenordnung darf nicht negativ sein");
  }
}

double cells_per_wavelength(const FieldScene& scene) {
  if (scene.sources.empty()) return 0.0;
  double shortest = scene.sources.front().wavelength;
  for (const auto& source : scene.sources) shortest = std::min(shortest, source.wavelength);
  double highest = scene.background_index;
  for (const auto& shape : scene.shapes) highest = std::max(highest, shape.index);
  return shortest / (highest * scene.cell_size);
}

std::vector<Warning> check(const FieldScene& scene) {
  std::vector<Warning> warnings;
  char buffer[256];
  const double cells = cells_per_wavelength(scene);
  if (cells > 0.0 && cells < 10.0) {
    std::snprintf(buffer, sizeof buffer,
                  "Nur %.1f Zellen pro Wellenlänge im dichtesten Material: das Feld wird grob falsch "
                  "(numerische Dispersion). Empfohlen sind mindestens 15 bis 20.",
                  cells);
    warnings.push_back({buffer, true});
  } else if (cells > 0.0 && cells < 15.0) {
    std::snprintf(buffer, sizeof buffer,
                  "%.1f Zellen pro Wellenlänge im dichtesten Material: Phasen und Resonanzen sind spürbar "
                  "verschoben. Empfohlen sind mindestens 15 bis 20.",
                  cells);
    warnings.push_back({buffer, false});
  }
  const double pml = scene.pml_cells * scene.cell_size;
  auto near_pml = [&](double x, double y) {
    const double margin = 2.0 * scene.cell_size;
    return (scene.boundary_x == Boundary::pml && (x < pml + margin || x > scene.width - pml - margin)) ||
           (scene.boundary_y == Boundary::pml && (y < pml + margin || y > scene.height - pml - margin));
  };
  for (const auto& source : scene.sources) {
    const bool in_pml = source.kind == SourceKind::point
                            ? near_pml(source.x1, source.y1)
                            : near_pml(0.5 * (source.x1 + source.x2), 0.5 * (source.y1 + source.y2));
    if (in_pml)
      warnings.push_back({"Quelle '" + source.name + "' liegt in oder direkt an der Randschicht.", true});
  }
  for (const auto& monitor : scene.monitors)
    if (near_pml(monitor.x1, monitor.y1))
      warnings.push_back({"Monitor '" + monitor.name + "' liegt in oder direkt an der Randschicht.", false});
  if ((scene.boundary_x == Boundary::pml || scene.boundary_y == Boundary::pml) && scene.pml_cells < 8)
    warnings.push_back({"Randschicht dünner als 8 Zellen: deutliche Reflexionen am Rand.", false});
  if (scene.sources.empty()) warnings.push_back({"Keine Quelle: es gibt kein Licht.", false});
  return warnings;
}

namespace {
template <class Enum, std::size_t N>
Enum from_string(std::string_view text, const Enum (&values)[N], const char* what) {
  for (Enum v : values)
    if (to_string(v) == text) return v;
  throw FieldError(std::string("Unbekannte Angabe für ") + what + ": '" + std::string(text) + "'");
}
}  // namespace

std::string_view to_string(Polarization p) { return p == Polarization::ez ? "ez" : "hz"; }
std::string_view to_string(Boundary b) { return b == Boundary::pml ? "pml" : "periodic"; }
std::string_view to_string(ShapeKind k) {
  return k == ShapeKind::rectangle ? "rectangle" : k == ShapeKind::circle ? "circle" : "ring";
}
std::string_view to_string(SourceKind k) {
  return k == SourceKind::point ? "point" : k == SourceKind::line ? "line" : "mode";
}
std::string_view to_string(SignalKind k) { return k == SignalKind::continuous ? "continuous" : "pulse"; }
std::string_view to_string(MonitorKind k) {
  return k == MonitorKind::point ? "point" : k == MonitorKind::flux ? "flux" : "mode";
}

Polarization polarization_from_string(std::string_view text) {
  static const Polarization values[] = {Polarization::ez, Polarization::hz};
  return from_string(text, values, "die Polarisation");
}
Boundary boundary_from_string(std::string_view text) {
  static const Boundary values[] = {Boundary::pml, Boundary::periodic};
  return from_string(text, values, "den Rand");
}
ShapeKind shape_kind_from_string(std::string_view text) {
  static const ShapeKind values[] = {ShapeKind::rectangle, ShapeKind::circle, ShapeKind::ring};
  return from_string(text, values, "die Form");
}
SourceKind source_kind_from_string(std::string_view text) {
  static const SourceKind values[] = {SourceKind::point, SourceKind::line, SourceKind::mode};
  return from_string(text, values, "die Quelle");
}
SignalKind signal_kind_from_string(std::string_view text) {
  static const SignalKind values[] = {SignalKind::continuous, SignalKind::pulse};
  return from_string(text, values, "das Signal");
}
MonitorKind monitor_kind_from_string(std::string_view text) {
  static const MonitorKind values[] = {MonitorKind::point, MonitorKind::flux, MonitorKind::mode};
  return from_string(text, values, "den Monitor");
}

double material_index(const FieldScene& scene, std::string_view material) {
  const double wavelength = scene.sources.empty() ? 1550e-9 : scene.sources.front().wavelength;
  const materials::Material* m = materials::find(material);
  if (!m) throw FieldError("Unbekanntes Material '" + std::string(material) + "'");
  return m->index(wavelength);
}

double layer_effective_index(const FieldScene& scene, std::string_view core, std::string_view cladding,
                             double thickness) {
  const double wavelength = scene.sources.empty() ? 1550e-9 : scene.sources.front().wavelength;
  const double n_core = material_index(scene, core), n_clad = material_index(scene, cladding);
  // Hz-Polarisation der Szene: E liegt in der Chip-Ebene, also parallel zur Schicht: TE der Schicht
  const auto polarization =
      scene.polarization == Polarization::hz ? modes::SlabPolarization::te : modes::SlabPolarization::tm;
  const auto found = modes::slab_modes(n_core, n_clad, n_clad, thickness, wavelength, polarization);
  return found.empty() ? n_clad : found.front().n_eff;
}

}  // namespace photonlab::field
