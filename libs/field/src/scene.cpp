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
    case ShapeKind::taper: {
      const double c = std::cos(shape.rotation), s = std::sin(shape.rotation);
      const double u = c * dx + s * dy, v = -s * dx + c * dy;
      if (std::abs(u) > 0.5 * shape.width) return false;
      const double t = u / shape.width + 0.5;  // 0 am Anfang, 1 am Ende
      return std::abs(v) <= 0.5 * (shape.height + t * (shape.width2 - shape.height));
    }
    case ShapeKind::arc: {
      const double r2 = dx * dx + dy * dy;
      if (r2 > shape.radius * shape.radius || r2 < shape.inner_radius * shape.inner_radius) return false;
      // Winkel relativ zum Anfang, in [0, 2 pi)
      const double two_pi = 2.0 * constants::pi;
      const double a = std::fmod(std::fmod(std::atan2(dy, dx) - shape.rotation, two_pi) + two_pi, two_pi);
      return a <= shape.sweep;
    }
  }
  return false;
}

bool in_group(const FieldScene& scene, int member_group, int group) {
  // Kette der Eltern hochlaufen; höchstens so viele Schritte wie Gruppen (schützt vor Zyklen)
  for (std::size_t steps = 0; member_group >= 0 && steps <= scene.groups.size(); ++steps) {
    if (member_group == group) return true;
    if (static_cast<std::size_t>(member_group) >= scene.groups.size()) return false;
    member_group = scene.groups[static_cast<std::size_t>(member_group)].parent;
  }
  return false;
}

void transform_group(FieldScene& scene, int group, double dx, double dy, double rotation) {
  if (group < 0 || static_cast<std::size_t>(group) >= scene.groups.size()) return;
  const double px = scene.groups[static_cast<std::size_t>(group)].x,
               py = scene.groups[static_cast<std::size_t>(group)].y;
  const double c = std::cos(rotation), s = std::sin(rotation);
  // Punkt um den Bezugspunkt drehen, dann verschieben (gleiche Drehrichtung wie Rechteck-Drehung)
  auto map = [&](double& x, double& y) {
    const double u = x - px, v = y - py;
    x = px + c * u - s * v + dx;
    y = py + s * u + c * v + dy;
  };
  for (auto& sh : scene.shapes)
    if (in_group(scene, sh.group, group)) {
      map(sh.x, sh.y);
      sh.rotation += rotation;
    }
  for (auto& q : scene.sources)
    if (in_group(scene, q.group, group)) map(q.x1, q.y1), map(q.x2, q.y2);
  for (auto& m : scene.monitors)
    if (in_group(scene, m.group, group)) map(m.x1, m.y1), map(m.x2, m.y2);
  for (auto& p : scene.ports)
    if (in_group(scene, p.group, group)) map(p.x1, p.y1), map(p.x2, p.y2);
  for (int g = 0; g < static_cast<int>(scene.groups.size()); ++g)
    if (in_group(scene, g, group)) {
      auto& gr = scene.groups[static_cast<std::size_t>(g)];
      map(gr.x, gr.y);
      gr.rotation += rotation;
    }
}

void remove_group(FieldScene& scene, int group) {
  if (group < 0 || static_cast<std::size_t>(group) >= scene.groups.size()) return;
  const int parent = scene.groups[static_cast<std::size_t>(group)].parent;
  // Verweise umhängen und die Indizes dahinter um eins verkleinern
  auto fix = [&](int& g) {
    if (g == group) g = parent;
    if (g > group) --g;
  };
  for (auto& sh : scene.shapes) fix(sh.group);
  for (auto& q : scene.sources) fix(q.group);
  for (auto& m : scene.monitors) fix(m.group);
  for (auto& p : scene.ports) fix(p.group);
  for (auto& gr : scene.groups) fix(gr.parent);
  scene.groups.erase(scene.groups.begin() + group);
}

FieldScene active_scene(const FieldScene& scene) {
  FieldScene active = scene;
  std::erase_if(active.shapes, [](const Shape& s) { return !s.enabled; });
  std::erase_if(active.sources, [](const Source& s) { return !s.enabled; });
  std::erase_if(active.monitors, [](const Monitor& m) { return !m.enabled; });
  return active;
}

double index_at(const FieldScene& scene, double x, double y) {
  for (auto it = scene.shapes.rbegin(); it != scene.shapes.rend(); ++it)
    if (it->enabled && contains(*it, x, y)) return it->index;
  return scene.background_index;
}

NonlinearParams nonlinear_at(const FieldScene& scene, double x, double y) {
  for (auto it = scene.shapes.rbegin(); it != scene.shapes.rend(); ++it)
    if (it->enabled && contains(*it, x, y)) return {it->n2, it->beta_tpa, it->carrier_lifetime};
  return {};
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
    if (shape.beta_tpa < 0.0 || shape.carrier_lifetime < 0.0)
      throw FieldError("TPA-Koeffizient und Ladungsträger-Lebensdauer dürfen nicht negativ sein (Form '" +
                       shape.name + "')");
    if (shape.carrier_lifetime > 0.0 && shape.beta_tpa == 0.0)
      throw FieldError("Ladungsträger entstehen aus TPA: beta_tpa muss > 0 sein (Form '" + shape.name + "')");
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
    const bool box = shape.kind == ShapeKind::rectangle || shape.kind == ShapeKind::taper;
    if (box && (!(shape.width > 0.0) || !(shape.height > 0.0)))
      throw FieldError(who + "Breite und Höhe müssen positiv sein");
    if (shape.kind == ShapeKind::taper && !(shape.width2 > 0.0))
      throw FieldError(who + "die Endbreite muss positiv sein");
    if (!box && !(shape.radius > 0.0)) throw FieldError(who + "der Radius muss positiv sein");
    if ((shape.kind == ShapeKind::ring || shape.kind == ShapeKind::arc) &&
        !(shape.inner_radius >= 0.0 && shape.inner_radius < shape.radius))
      throw FieldError(who + "der Innenradius muss kleiner als der Außenradius sein");
    if (shape.kind == ShapeKind::arc && !(shape.sweep > 0.0 && shape.sweep <= 2.0 * constants::pi))
      throw FieldError(who + "der Winkel des Bogens muss zwischen 0 und 360 Grad liegen");
  }
  // Gruppen: Verweise gültig, keine Zyklen
  const int group_count = static_cast<int>(scene.groups.size());
  auto check_group = [&](int g, const std::string& who) {
    if (g < -1 || g >= group_count) throw FieldError(who + "verweist auf eine Gruppe, die es nicht gibt");
  };
  for (int g = 0; g < group_count; ++g) {
    const auto& group = scene.groups[static_cast<std::size_t>(g)];
    check_group(group.parent, "Gruppe '" + group.name + "' ");
    if (group.parent >= 0 && in_group(scene, group.parent, g))
      throw FieldError("Gruppe '" + group.name + "' liegt in sich selbst");
  }
  for (const auto& sh : scene.shapes) check_group(sh.group, "Form '" + sh.name + "' ");
  for (const auto& q : scene.sources) check_group(q.group, "Quelle '" + q.name + "' ");
  for (const auto& m : scene.monitors) check_group(m.group, "Monitor '" + m.name + "' ");
  for (const auto& p : scene.ports) check_group(p.group, "Port '" + p.name + "' ");
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
    if (source.direction != 0 && (source.kind != SourceKind::mode ||
                                  source.signal != SignalKind::continuous || std::abs(source.direction) != 1))
      throw FieldError(who +
                       "nur eine Modenquelle mit Dauerstrich strahlt einseitig (Richtung -1, 0 oder 1)");
    if (!source.trigger.empty()) {
      const auto monitor = std::find_if(scene.monitors.begin(), scene.monitors.end(),
                                        [&](const Monitor& m) { return m.name == source.trigger; });
      if (monitor == scene.monitors.end() || monitor->kind == MonitorKind::point)
        throw FieldError(who + "der Auslöser '" + source.trigger +
                         "' muss ein Fluss- oder Modenmonitor der Szene sein");
      if (source.signal != SignalKind::continuous)
        throw FieldError(who + "ein Regenerator sendet Dauerstrich (kein Puls)");
      if (!(source.trigger_threshold >= 0.0) || !(source.trigger_delay >= 0.0))
        throw FieldError(who + "Schwelle und Verzögerung dürfen nicht negativ sein");
    }
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
  switch (k) {
    case ShapeKind::rectangle:
      return "rectangle";
    case ShapeKind::circle:
      return "circle";
    case ShapeKind::ring:
      return "ring";
    case ShapeKind::taper:
      return "taper";
    case ShapeKind::arc:
      return "arc";
  }
  return "rectangle";
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
  static const ShapeKind values[] = {ShapeKind::rectangle, ShapeKind::circle, ShapeKind::ring,
                                     ShapeKind::taper, ShapeKind::arc};
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
