#include "photonlab/layout/circuit_layout.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>

#include "photonlab/core/units.hpp"

namespace photonlab::layout {
namespace {

constexpr double pi = 3.14159265358979323846;

// Koordinaten in µm, y nach oben (wie im Layout); erst beim Schreiben auf das 1-nm-Raster gerundet
struct V {
  double x = 0.0, y = 0.0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
V operator*(V a, double s) { return {a.x * s, a.y * s}; }
double length(V a) { return std::hypot(a.x, a.y); }
V unit(V a) {
  const double n = length(a);
  return n > 0.0 ? a * (1.0 / n) : a;
}

class Painter {
 public:
  std::vector<Polygon> polygons;

  void polygon(int layer, const std::vector<V>& points) {
    Polygon p;
    p.layer = layer;
    for (const V& v : points) {
      const Point q{static_cast<std::int32_t>(std::lround(v.x * 1000.0)),
                    static_cast<std::int32_t>(std::lround(v.y * 1000.0))};
      if (p.points.empty() || !(p.points.back() == q)) p.points.push_back(q);
    }
    while (p.points.size() > 1 && p.points.front() == p.points.back()) p.points.pop_back();
    if (p.points.size() < 3) return;
    if (signed_area(p) < 0.0) std::reverse(p.points.begin(), p.points.end());
    polygons.push_back(std::move(p));
  }
  void rect(int layer, double x0, double y0, double x1, double y1) {
    polygon(layer, {{std::min(x0, x1), std::min(y0, y1)},
                    {std::max(x0, x1), std::min(y0, y1)},
                    {std::max(x0, x1), std::max(y0, y1)},
                    {std::min(x0, x1), std::max(y0, y1)}});
  }
  // Weg der Breite width entlang einer (schon gerundeten) Mittellinie
  void path(int layer, const std::vector<V>& line, double width) {
    if (line.size() < 2) return;
    std::vector<V> left, right;
    for (std::size_t k = 0; k < line.size(); ++k) {
      const V in = k > 0 ? unit(line[k] - line[k - 1]) : unit(line[1] - line[0]);
      const V out = k + 1 < line.size() ? unit(line[k + 1] - line[k]) : in;
      const V n_in{-in.y, in.x}, n_out{-out.y, out.x};
      V n = unit(n_in + n_out);
      const double c = n.x * n_in.x + n.y * n_in.y;
      n = n * (0.5 * width / std::max(c, 0.3));  // Gehrung, bei spitzen Winkeln begrenzt
      left.push_back(line[k] + n);
      right.push_back(line[k] - n);
    }
    std::vector<V> outline = left;
    outline.insert(outline.end(), right.rbegin(), right.rend());
    polygon(layer, outline);
  }
  // Ring (Kreisring) als ein Polygon mit Schnittkante bei Winkel 0; höchstens 1 nm Abweichung vom Kreis
  void ring(int layer, V center, double r_out, double r_in) {
    const int n = std::clamp(static_cast<int>(std::ceil(pi / std::acos(1.0 - 1e-3 / r_out))), 16, 4000);
    std::vector<V> points;
    for (int k = 0; k <= n; ++k) {
      const double a = 2.0 * pi * k / n;
      points.push_back(center + V{r_out * std::cos(a), r_out * std::sin(a)});
    }
    for (int k = n; k >= 0; --k) {
      const double a = 2.0 * pi * k / n;
      points.push_back(center + V{r_in * std::cos(a), r_in * std::sin(a)});
    }
    Polygon p;
    p.layer = layer;
    for (const V& v : points)
      p.points.push_back({static_cast<std::int32_t>(std::lround(v.x * 1000.0)),
                          static_cast<std::int32_t>(std::lround(v.y * 1000.0))});
    polygons.push_back(std::move(p));
  }
};

// Ecken einer Linie mit Bögen vom Radius r abrunden (kleiner, wenn die Strecken zu kurz sind)
std::vector<V> fillet(const std::vector<V>& corners, double r) {
  if (corners.size() < 3) return corners;
  std::vector<V> out = {corners.front()};
  for (std::size_t k = 1; k + 1 < corners.size(); ++k) {
    const V p = corners[k];
    const V d1 = unit(p - corners[k - 1]), d2 = unit(corners[k + 1] - p);
    const double cross = d1.x * d2.y - d1.y * d2.x, dot = d1.x * d2.x + d1.y * d2.y;
    const double theta = std::atan2(std::abs(cross), dot);  // Richtungsänderung
    if (theta < 1e-6) continue;
    const double room = 0.5 * std::min(length(p - corners[k - 1]), length(corners[k + 1] - p));
    const double tangent = std::min(r * std::tan(0.5 * theta), room);
    const double radius = tangent / std::tan(0.5 * theta);
    const V a = p - d1 * tangent;
    const V normal = cross > 0.0 ? V{-d1.y, d1.x} : V{d1.y, -d1.x};
    const V center = a + normal * radius;
    const double start = std::atan2(a.y - center.y, a.x - center.x);
    const int steps = std::max(4, static_cast<int>(std::ceil(theta / (pi / 32))));
    for (int s = 0; s <= steps; ++s) {
      const double angle = start + (cross > 0.0 ? 1.0 : -1.0) * theta * s / steps;
      out.push_back(center + V{radius * std::cos(angle), radius * std::sin(angle)});
    }
  }
  out.push_back(corners.back());
  return out;
}

// Weg von a (Abfahrt in x-Richtung da = +-1) nach b (Ankunft in x-Richtung db), nur waagerecht/senkrecht;
// xm: Lage des senkrechten Stücks (NaN = Mitte). Ist der Platz für zwei Bögen mit Radius r zu knapp, werden
// die Bögen enger (S-Bogen).
std::vector<V> route(V a, int da, V b, int db, double r, double xm = std::nan("")) {
  if (std::abs(a.y - b.y) < 1e-6 && da == db && (b.x - a.x) * da > 0.0) return {a, b};
  if (da == db && (b.x - a.x) * da > 0.0) {
    if (std::isnan(xm)) xm = 0.5 * (a.x + b.x);
    return fillet({a, {xm, a.y}, {xm, b.y}, b}, r);
  }
  if (da != db) {
    const double x = da > 0 ? std::max(a.x, b.x) + 2.0 * r : std::min(a.x, b.x) - 2.0 * r;
    return fillet({a, {x, a.y}, {x, b.y}, b}, r);
  }
  // rückwärts: über eine Umgehung zwischen den beiden Höhen (bzw. darunter, wenn sie gleich sind)
  const double ym = std::abs(a.y - b.y) > 4.0 * r ? 0.5 * (a.y + b.y) : std::min(a.y, b.y) - 4.0 * r;
  const double xa = a.x + da * 2.0 * r, xb = b.x - db * 2.0 * r;
  return fillet({a, {xa, a.y}, {xa, ym}, {xb, ym}, {xb, b.y}, b}, r);
}

bool segments_cross(V p1, V p2, V q1, V q2) {
  auto orient = [](V a, V b, V c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
  const double d1 = orient(q1, q2, p1), d2 = orient(q1, q2, p2), d3 = orient(p1, p2, q1),
               d4 = orient(p1, p2, q2);
  return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) &&
         ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}

double param(const circuit::Instance& instance, const circuit::ComponentType& type, const std::string& name) {
  const auto it = instance.params.find(name);
  if (it != instance.params.end()) return it->second;
  const circuit::ParamSpec* spec = type.find_param(name);
  return spec ? spec->default_value : 0.0;
}

struct Context {
  const circuit::ComponentRegistry& registry;
  const std::vector<circuit::Subcircuit>& subcircuits;
  const CircuitLayoutSettings& settings;
  CircuitLayoutReport& report;
  std::set<std::string> notes;
  std::set<std::string> sub_cells;  // gebrauchte Unterschaltungen
};

// Ort eines Ports in µm (wie im Editor: Bauteil 5 Rasterzellen breit, Ports 2 Zellen übereinander) und die
// Richtung, in der man ihn von außen kommend durchläuft (+1: von links hinein, -1: von rechts)
struct PortPlace {
  V at;
  bool left = true;
};
PortPlace port_place(const circuit::Instance& instance, const circuit::ComponentType& type, int index,
                     double p) {
  int row = 0;
  for (int k = 0; k < index; ++k)
    if (type.is_left_port(k) == type.is_left_port(index)) ++row;
  const bool left = type.is_left_port(index);
  return {{(instance.grid_x + (left ? 0.0 : 5.0)) * p, -(instance.grid_y + 2.0 * row) * p}, left};
}

void draw_instance(Painter& paint, const circuit::Instance& instance, const circuit::ComponentType& type,
                   Context& ctx) {
  const CircuitLayoutSettings& s = ctx.settings;
  const double p = s.pitch / units::um, w = s.waveguide_width / units::um, r = s.bend_radius / units::um;
  const double gap = s.coupler_gap / units::um;
  std::vector<PortPlace> ports;
  for (int k = 0; k < static_cast<int>(type.port_names.size()); ++k)
    ports.push_back(port_place(instance, type, k, p));
  auto port = [&](const char* name) -> V {
    for (std::size_t k = 0; k < type.port_names.size(); ++k)
      if (type.port_names[k] == name) return ports[k].at;
    return {};
  };
  const double x0 = instance.grid_x * p, x1 = x0 + 5.0 * p;
  const double cx = 0.5 * (x0 + x1);
  auto wire = [&](V a, V b) { paint.path(layers::waveguide, route(a, +1, b, +1, r), w); };
  auto contacts = [&](double xa, double xb, double y) {
    const double c = s.contact_size / units::um, offset = 0.5 * c + 4.0;
    for (const double x : {xa, xb}) {
      paint.rect(layers::metal, x - 2.0, y, x + 2.0, y + offset);
      paint.rect(layers::metal, x - 0.5 * c, y + offset - 0.5 * c, x + 0.5 * c, y + offset + 0.5 * c);
    }
  };
  // Richtkoppler: zwei parallele Stücke der Länge lc um (xc, yc), dazu die Zuleitungen
  auto coupler = [&](double xc, double yc, double lc) {
    const double up = yc + 0.5 * (w + gap), down = yc - 0.5 * (w + gap);
    paint.path(layers::waveguide, {{xc - 0.5 * lc, up}, {xc + 0.5 * lc, up}}, w);
    paint.path(layers::waveguide, {{xc - 0.5 * lc, down}, {xc + 0.5 * lc, down}}, w);
    return std::pair{up, down};
  };
  const std::string& t = type.name;

  if (t == "laser") {
    ctx.notes.insert("Laser liegen außerhalb des Chips (Faser auf den Gitterkoppler); nicht gezeichnet.");
    return;
  }
  if (t == "coupler") {
    const double lc = coupler_length(param(instance, type, "power_coupling"), s) / units::um;
    const double yc = 0.5 * (port("in1").y + port("in2").y);
    const auto [up, down] = coupler(cx, yc, lc);
    wire(port("in1"), {cx - 0.5 * lc, up});
    wire(port("in2"), {cx - 0.5 * lc, down});
    wire({cx + 0.5 * lc, up}, port("out1"));
    wire({cx + 0.5 * lc, down}, port("out2"));
    return;
  }
  if (t == "mzi_switch") {
    const double lc = coupler_length(param(instance, type, "power_coupling"), s) / units::um;
    double heater = param(instance, type, "length") / units::um;
    const double arm_gap = 12.0;  // Abstand der Arme (thermisches Übersprechen klein halten)
    const double room = (x1 - x0) - 2.0 * lc - 6.0 * r - 4.0 * r;  // Zuleitungen und S-Bögen der Arme
    if (heater > room) {
      ctx.notes.insert(
          "Heizer eines MZI-Schalters länger als sein Platz im Raster: im Layout gekürzt (Raster "
          "vergrößern).");
      heater = std::max(room, 5.0);
    }
    const double yc = 0.5 * (port("in1").y + port("in2").y);
    const double xa = cx - 0.5 * heater - 3.0 * r - 0.5 * lc, xb = cx + 0.5 * heater + 3.0 * r + 0.5 * lc;
    const auto [up1, down1] = coupler(xa, yc, lc);
    const auto [up2, down2] = coupler(xb, yc, lc);
    wire(port("in1"), {xa - 0.5 * lc, up1});
    wire(port("in2"), {xa - 0.5 * lc, down1});
    wire({xb + 0.5 * lc, up2}, port("out1"));
    wire({xb + 0.5 * lc, down2}, port("out2"));
    for (const double sign : {+1.0, -1.0}) {
      const double y_arm = yc + sign * 0.5 * arm_gap;
      const double y_from = sign > 0 ? up1 : down1, y_to = sign > 0 ? up2 : down2;
      wire({xa + 0.5 * lc, y_from}, {cx - 0.5 * heater, y_arm});
      paint.path(layers::waveguide, {{cx - 0.5 * heater, y_arm}, {cx + 0.5 * heater, y_arm}}, w);
      wire({cx + 0.5 * heater, y_arm}, {xb - 0.5 * lc, y_to});
      const double hw = 0.5 * s.heater_width / units::um;
      paint.rect(layers::heater, cx - 0.5 * heater, y_arm - hw, cx + 0.5 * heater, y_arm + hw);
    }
    contacts(cx - 0.5 * heater, cx + 0.5 * heater, yc + 0.5 * arm_gap + 2.0);
    ctx.notes.insert("Heizer-Kontakte (Metall) sind nicht zu Bondflächen verdrahtet.");
    return;
  }
  if (t == "y_splitter") {
    const V junction{cx - 2.0 * r, port("in").y};
    wire(port("in"), junction);
    paint.path(layers::waveguide, route(junction, +1, port("out1"), +1, r), w);
    paint.path(layers::waveguide, route(junction, +1, port("out2"), +1, r), w);
    return;
  }
  if (t == "ring_allpass" || t == "ring_kerr" || t == "ring_pn") {
    const double radius = param(instance, type, "radius") / units::um;
    const V in = port("in"), out = port("out");
    wire(in, out);
    const V center{cx, in.y + radius + w + gap};
    paint.ring(layers::waveguide, center, radius + 0.5 * w, radius - 0.5 * w);
    if (t == "ring_pn") {
      paint.ring(layers::doping_p, center, radius + 0.5 * w + 1.5, radius + 0.5 * w);
      paint.ring(layers::doping_n, center, radius - 0.5 * w, std::max(radius - 0.5 * w - 1.5, 0.1));
      contacts(center.x - radius, center.x + radius, center.y + radius + 3.0);
    }
    if (radius + 2.0 * w > 0.5 * (x1 - x0))
      ctx.notes.insert(
          "Ein Ring ist größer als sein Platz im Raster und überlappt Nachbarn (Raster vergrößern).");
    return;
  }
  if (t == "ring_adddrop") {
    const double radius = param(instance, type, "radius") / units::um;
    const double yc = 0.5 * (port("in").y + port("add").y);
    const V center{cx, yc};
    paint.ring(layers::waveguide, center, radius + 0.5 * w, radius - 0.5 * w);
    const double top = yc + radius + w + gap, bottom = yc - radius - w - gap, half = radius + 2.0;
    paint.path(layers::waveguide, {{cx - half, top}, {cx + half, top}}, w);
    paint.path(layers::waveguide, {{cx - half, bottom}, {cx + half, bottom}}, w);
    wire(port("in"), {cx - half, top});
    wire({cx + half, top}, port("through"));
    wire(port("add"), {cx - half, bottom});
    wire({cx + half, bottom}, port("drop"));
    return;
  }
  if (t == "grating_coupler") {
    // Gitter 20 Perioden zu 0,63 µm, 12 µm breit, Taper 15 µm auf den Wellenleiter (übliche Größenordnung)
    const V wg = port("wg");
    const double period = 0.63, width = 12.0, teeth = 20, x_end = x0 + 4.0 + teeth * period;
    paint.rect(layers::waveguide, x0 + 4.0, wg.y - 0.5 * width, x_end, wg.y + 0.5 * width);
    for (int k = 0; k < static_cast<int>(teeth); ++k) {
      const double xa = x0 + 4.0 + k * period;
      paint.rect(layers::grating, xa, wg.y - 0.5 * width, xa + 0.5 * period, wg.y + 0.5 * width);
    }
    paint.polygon(layers::waveguide, {{x_end, wg.y - 0.5 * width},
                                      {x_end + 15.0, wg.y - 0.5 * w},
                                      {x_end + 15.0, wg.y + 0.5 * w},
                                      {x_end, wg.y + 0.5 * width}});
    wire({x_end + 15.0, wg.y}, wg);
    return;
  }
  if (t == "photodiode" || t == "detector") {
    const V in = port("in");
    const V end{x0 + 0.5 * (x1 - x0), in.y};
    wire(in, end);
    paint.rect(layers::germanium, end.x - 2.0, end.y - 2.0, end.x + 12.0, end.y + 2.0);
    contacts(end.x, end.x + 10.0, end.y + 2.0);
    return;
  }
  const bool two_port = type.port_names.size() == 2 && type.is_left_port(0) && !type.is_left_port(1) &&
                        t.rfind("sparam:", 0) != 0 && t.rfind("sub:", 0) != 0;
  if (two_port) {
    const V in = ports[0].at, out = ports[1].at;
    wire(in, out);
    const double straight = std::min(0.8 * (x1 - x0), 100.0);
    if (t == "heater") {
      double heater = std::min(param(instance, type, "length") / units::um, straight);
      const double hw = 0.5 * s.heater_width / units::um;
      paint.rect(layers::heater, cx - 0.5 * heater, in.y - hw, cx + 0.5 * heater, in.y + hw);
      contacts(cx - 0.5 * heater, cx + 0.5 * heater, in.y + hw);
    } else if (t == "pn_phase_shifter") {
      const double l = std::min(param(instance, type, "length") / units::um, straight);
      paint.rect(layers::doping_p, cx - 0.5 * l, in.y, cx + 0.5 * l, in.y + 3.0);
      paint.rect(layers::doping_n, cx - 0.5 * l, in.y - 3.0, cx + 0.5 * l, in.y);
      contacts(cx - 0.5 * l, cx + 0.5 * l, in.y + 3.0);
      if (param(instance, type, "length") / units::um > straight)
        ctx.notes.insert("PN-Phasenschieber länger als sein Platz im Raster: im Layout gekürzt.");
    } else if (t == "pcm_cell") {
      const double l = param(instance, type, "length") / units::um;
      paint.rect(layers::phase_change, cx - 0.5 * l, in.y - 0.5 * w, cx + 0.5 * l, in.y + 0.5 * w);
    } else if (t == "soa") {
      paint.rect(layers::gain, cx - 0.5 * straight, in.y - 3.0, cx + 0.5 * straight, in.y + 3.0);
      ctx.notes.insert("SOA als III-V-Fläche angedeutet (hybride Integration nicht ausgeführt).");
    } else if (t == "waveguide") {
      ctx.notes.insert("Wellenleiterlängen folgen dem Raster des Editors, nicht dem Parameter 'Länge'.");
    } else if (t == "bandpass" || t == "phase_shifter") {
      ctx.notes.insert("Ideale Bauteile (Phasenschieber, Bandpass) als einfacher Wellenleiter gezeichnet.");
    }
    return;
  }
  // gemessene Bauteile, Unterschaltungen, Plugins: Kasten mit Anschlüssen
  double y_low = 1e300, y_high = -1e300;
  for (const auto& place : ports) y_low = std::min(y_low, place.at.y), y_high = std::max(y_high, place.at.y);
  paint.rect(layers::outline, x0 + 2.0, y_low - 0.5 * p, x1 - 2.0, y_high + 0.5 * p);
  for (const auto& place : ports)
    paint.path(layers::waveguide, {place.at, place.at + V{place.left ? 2.0 : -2.0, 0.0}}, w);
  if (t.rfind("sub:", 0) == 0) {
    ctx.sub_cells.insert(t.substr(4));
    ctx.notes.insert("Unterschaltungen als Kasten; ihr Inneres steht in eigenen Zellen 'SUB_<name>'.");
  } else {
    ctx.notes.insert(
        "Bauteile ohne eigene Geometrie (gemessene S-Parameter, Plugins) als Kasten auf Lage 99.");
  }
}

std::vector<Polygon> draw_circuit(const circuit::Circuit& circuit, Context& ctx, int* crossings) {
  const CircuitLayoutSettings& s = ctx.settings;
  const double p = s.pitch / units::um, w = s.waveguide_width / units::um, r = s.bend_radius / units::um;
  Painter paint;
  for (const auto& instance : circuit.instances) {
    const circuit::ComponentType* type = ctx.registry.find(instance.type);
    if (!type) throw LayoutError("Unbekannter Bauteiltyp '" + instance.type + "'");
    draw_instance(paint, instance, *type, ctx);
  }
  // Verbindungen: von Port zu Port, von links nach rechts mit Bögen; Faser-Anschlüsse (Laser) fehlen
  std::vector<std::vector<V>> routes;
  std::vector<std::array<double, 3>> lanes;  // belegte senkrechte Stücke: x, y von, y bis
  int overlaps = 0;
  for (const auto& c : circuit.connections) {
    const circuit::Instance* ia = circuit.find_instance(c.a.instance);
    const circuit::Instance* ib = circuit.find_instance(c.b.instance);
    const circuit::ComponentType* ta = ctx.registry.find(ia->type);
    const circuit::ComponentType* tb = ctx.registry.find(ib->type);
    if (ta->name == "laser" || tb->name == "laser") continue;
    auto index_of = [](const circuit::ComponentType* type, const std::string& port) {
      return static_cast<int>(std::find(type->port_names.begin(), type->port_names.end(), port) -
                              type->port_names.begin());
    };
    const PortPlace a = port_place(*ia, *ta, index_of(ta, c.a.port), p);
    const PortPlace b = port_place(*ib, *tb, index_of(tb, c.b.port), p);
    // Abfahrt aus einem rechten Port nach rechts (+1), aus einem linken nach links; Ankunft entsprechend.
    // Das senkrechte Stück bekommt eine eigene Bahn (3 µm Abstand zu anderen auf gleicher Höhe), damit sich
    // Wellenleiter nicht überdecken.
    const int da = a.left ? -1 : +1, db = b.left ? +1 : -1;
    double xm = std::nan("");
    if (da == db && (b.at.x - a.at.x) * da > 0.0 && std::abs(a.at.y - b.at.y) > 1e-6) {
      const double y0 = std::min(a.at.y, b.at.y) - w, y1 = std::max(a.at.y, b.at.y) + w;
      const double lo = std::min(a.at.x, b.at.x) + 2.0 * w, hi = std::max(a.at.x, b.at.x) - 2.0 * w;
      const double middle = 0.5 * (a.at.x + b.at.x);
      for (int k = 0; k < 400; ++k) {
        const double x = middle + ((k % 2) ? 1.0 : -1.0) * ((k + 1) / 2) * 3.0;
        if (x < lo || x > hi) continue;
        const bool taken = std::any_of(lanes.begin(), lanes.end(), [&](const std::array<double, 3>& lane) {
          return std::abs(lane[0] - x) < 2.0 && lane[1] < y1 && y0 < lane[2];
        });
        if (!taken) {
          xm = x;
          break;
        }
      }
      if (std::isnan(xm)) ++overlaps;
      lanes.push_back({std::isnan(xm) ? middle : xm, y0, y1});
    }
    std::vector<V> line = route(a.at, da, b.at, db, r, xm);
    paint.path(layers::waveguide, line, w);
    routes.push_back(std::move(line));
  }
  for (std::size_t i = 0; i < routes.size(); ++i)
    for (std::size_t j = i + 1; j < routes.size(); ++j)
      for (std::size_t a = 0; a + 1 < routes[i].size(); ++a)
        for (std::size_t b = 0; b + 1 < routes[j].size(); ++b)
          if (segments_cross(routes[i][a], routes[i][a + 1], routes[j][b], routes[j][b + 1])) ++*crossings;
  ctx.report.instances += static_cast<int>(circuit.instances.size());
  if (overlaps > 0)
    ctx.notes.insert(
        "Zu wenig Platz zwischen Bauteilen: " + std::to_string(overlaps) +
        " Verbindung(en) teilen sich eine senkrechte Bahn (Bauteile im Editor weiter auseinander).");
  return paint.polygons;
}

}  // namespace

double coupler_length(double kappa2, const CircuitLayoutSettings& settings) {
  return std::asin(std::sqrt(std::clamp(kappa2, 0.0, 1.0))) / settings.coupling_per_length;
}

Layout circuit_to_layout(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                         const std::vector<circuit::Subcircuit>& subcircuits,
                         const CircuitLayoutSettings& settings, CircuitLayoutReport* report) {
  CircuitLayoutReport local;
  CircuitLayoutReport& out = report ? *report : local;
  out = {};
  Context ctx{registry, subcircuits, settings, out, {}, {}};
  Layout layout;
  Cell chip;
  chip.name = "CHIP";
  chip.polygons = draw_circuit(circuit, ctx, &out.crossings);
  // Inneres der benutzten Unterschaltungen (und ihrer Unterschaltungen) als eigene Zellen
  std::set<std::string> done;
  while (true) {
    std::string next;
    for (const auto& name : ctx.sub_cells)
      if (!done.count(name)) next = name;
    if (next.empty()) break;
    done.insert(next);
    const auto it = std::find_if(subcircuits.begin(), subcircuits.end(),
                                 [&](const circuit::Subcircuit& sub) { return sub.name == next; });
    if (it == subcircuits.end()) continue;
    int crossings = 0;
    Cell cell;
    cell.name = "SUB_" + next;
    cell.polygons = draw_circuit(it->circuit, ctx, &crossings);
    out.crossings += crossings;
    layout.cells.push_back(std::move(cell));
  }
  // Ausdehnung des Chips
  std::int64_t x_low = INT64_MAX, x_high = INT64_MIN, y_low = INT64_MAX, y_high = INT64_MIN;
  for (const auto& polygon : chip.polygons)
    for (const auto& point : polygon.points) {
      x_low = std::min<std::int64_t>(x_low, point.x), x_high = std::max<std::int64_t>(x_high, point.x);
      y_low = std::min<std::int64_t>(y_low, point.y), y_high = std::max<std::int64_t>(y_high, point.y);
    }
  if (!chip.polygons.empty()) {
    out.width = static_cast<double>(x_high - x_low) * 1e-9;
    out.height = static_cast<double>(y_high - y_low) * 1e-9;
  }
  layout.cells.insert(layout.cells.begin(), std::move(chip));
  layout.layers = {{layers::waveguide, 3.476, "Si 220 nm (voll geätzt)"},
                   {layers::grating, 0.0, "Gitter (flache Ätzung)"},
                   {layers::heater, 0.0, "Heizer (TiN)"},
                   {layers::metal, 0.0, "Metall (Kontakte)"},
                   {layers::germanium, 0.0, "Germanium (Photodiode)"},
                   {layers::doping_p, 0.0, "p-Dotierung"},
                   {layers::doping_n, 0.0, "n-Dotierung"},
                   {layers::phase_change, 0.0, "GST"},
                   {layers::gain, 0.0, "III-V (Verstärker)"},
                   {layers::outline, 0.0, "Umriss ohne Geometrie"}};
  out.notes.assign(ctx.notes.begin(), ctx.notes.end());
  if (out.crossings > 0)
    out.notes.push_back(
        std::to_string(out.crossings) +
        " Kreuzung(en) geführter Wellenleiter: dort Kreuzungsbauteile einsetzen oder umlegen.");
  return layout;
}

}  // namespace photonlab::layout
