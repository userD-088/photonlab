#include "photonlab/field/reference_scenes.hpp"

#include <cmath>
#include <tuple>

#include "photonlab/core/units.hpp"
#include "photonlab/field/photonic_crystal.hpp"
#include "photonlab/materials/material.hpp"
#include "photonlab/modes/slab.hpp"

namespace photonlab::field {
namespace {

using units::um;
// Mantel: SiO2 bei 1550 nm (Malitson 1965, siehe libs/materials).
const double index_cladding = materials::get("SiO2").index(1550e-9);
// Kern in der Draufsicht: effektiver Index der TE0-Mode einer 220 nm dicken Si-Schicht in SiO2 bei 1550 nm
// (Effektiv-Index-Methode, aus der Eigenwertgleichung der Schicht): etwa 2,85.
const double index_core = modes::slab_modes(materials::get("Si").index(1550e-9), index_cladding,
                                            index_cladding, 220e-9, 1550e-9, modes::SlabPolarization::te)
                              .front()
                              .n_eff;

// Design 1: gerader Wellenleiter mit Modenquelle: die Grundmode läuft ohne Abstrahlung nach beiden Seiten.
FieldScene waveguide() {
  FieldScene s;
  s.name = "Gerader Wellenleiter";
  s.width = 14 * um, s.height = 5 * um;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;  // E in der Ebene: TE-artig für die Draufsicht auf einen Chip
  s.duration = 300e-15;
  s.shapes = {
      {"Kern", ShapeKind::rectangle, 7 * um, 2.5 * um, 14 * um, 0.45 * um, 0.0, 1e-6, 0.5e-6, index_core}};
  Source source;
  source.name = "Modenquelle";
  source.kind = SourceKind::mode;
  source.x1 = source.x2 = 1.5 * um;
  source.y1 = 1.0 * um, source.y2 = 4.0 * um;
  s.sources = {source};
  s.monitors = {{"Fluss vorn", MonitorKind::flux, 3 * um, 1.2 * um, 3 * um, 3.8 * um},
                {"Fluss hinten", MonitorKind::flux, 12 * um, 1.2 * um, 12 * um, 3.8 * um},
                {"Feld im Kern", MonitorKind::point, 12 * um, 2.5 * um, 0, 0},
                {"Grundmode", MonitorKind::mode, 10 * um, 1.0 * um, 10 * um, 4.0 * um}};
  // Ports für die S-Parameter: ein gerader Wellenleiter muss |S21| = 1 und keine Reflexion liefern
  s.ports = {{"links", 3 * um, 1.0 * um, 3 * um, 4.0 * um}, {"rechts", 11 * um, 1.0 * um, 11 * um, 4.0 * um}};
  return s;
}

// Störstelle für den Konsistenztest der Brücke 1: einmodiger Wellenleiter (0,3 µm) mit einem 0,1 µm breiten
// Spalt. Ez-Polarisation, weil der Spalt dort deutlich reflektiert (einige Prozent) und die Kaskade
// zweier Spalte sichtbare Fabry-Perot-Streifen zeigt. copies > 1 legt weitere Spalte im Abstand der
// Portlinien dahinter; die Ports sitzen dann am Anfang der ersten und am Ende der letzten Kopie.
FieldScene gap_chain(int copies) {
  const double period = 3 * um;  // Abstand der beiden Portlinien einer Kopie
  FieldScene s;
  s.name = copies == 1 ? "Wellenleiter mit Spalt" : "Zwei Spalte hintereinander";
  s.width = 3 * um + copies * period, s.height = 3 * um;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::ez;
  s.duration = 250e-15;
  s.shapes = {{"Kern", ShapeKind::rectangle, 0.5 * s.width, 1.5 * um, s.width, 0.3 * um, 0.0, 1e-6, 0.5e-6,
               index_core}};
  for (int c = 0; c < copies; ++c)
    s.shapes.push_back({"Spalt " + std::to_string(c + 1), ShapeKind::rectangle, 3 * um + c * period, 1.5 * um,
                        0.1 * um, 0.3 * um, 0.0, 1e-6, 0.5e-6, index_cladding});
  Source source;
  source.name = "Modenquelle";
  source.kind = SourceKind::mode;
  source.x1 = source.x2 = 0.9 * um, source.y1 = 0.6 * um, source.y2 = 2.4 * um;
  s.sources = {source};
  s.monitors = {
      {"Fluss davor", MonitorKind::flux, 1.4 * um, 0.6 * um, 1.4 * um, 2.4 * um},
      {"Fluss dahinter", MonitorKind::flux, s.width - 1.4 * um, 0.6 * um, s.width - 1.4 * um, 2.4 * um}};
  s.ports = {{"ein", 1.5 * um, 0.6 * um, 1.5 * um, 2.4 * um},
             {"aus", s.width - 1.5 * um, 0.6 * um, s.width - 1.5 * um, 2.4 * um}};
  return s;
}

// Auf Pikometer bzw. Piko-Radiant runden: berechnete Geometrie unterscheidet sich je nach Compiler und
// Mathe-Bibliothek im letzten Bit; gerundet sind die Beispieldateien auf allen Plattformen gleich.
double round_pico(double value) { return std::round(value * 1e12) / 1e12; }

// Rechteck-Segmente entlang einer S-Kurve y(x) = y0 + offset * (1 - cos(pi*t)) / 2, t = (x - x0)/length.
void add_s_bend(std::vector<Shape>& shapes, const std::string& name, double x0, double y0, double length,
                double offset, double width, double index, int segments = 16) {
  auto y = [&](double t) { return y0 + offset * 0.5 * (1.0 - std::cos(constants::pi * t)); };
  for (int k = 0; k < segments; ++k) {
    const double t0 = static_cast<double>(k) / segments, t1 = static_cast<double>(k + 1) / segments;
    const double xa = x0 + t0 * length, xb = x0 + t1 * length, ya = y(t0), yb = y(t1);
    const double segment = std::hypot(xb - xa, yb - ya);
    // etwas länger als nötig, damit an den Knicken keine Lücken entstehen
    shapes.push_back({name, ShapeKind::rectangle, round_pico(0.5 * (xa + xb)), round_pico(0.5 * (ya + yb)),
                      round_pico(segment + 0.6 * width), width, round_pico(std::atan2(yb - ya, xb - xa)),
                      1e-6, 0.5e-6, index});
  }
}

// Design 4: Richtkoppler aus zwei Wellenleitern, die sich über S-Kurven auf 0,2 µm annähern. Vier Ports;
// die S-Parameter lassen sich in den Chip-Designer übernehmen und dort wie ein Koppler verschalten.
FieldScene coupler() {
  const double width = 0.3 * um, gap = 0.2 * um, bend = 4 * um, straight = 4 * um, offset = 1.0 * um;
  FieldScene s;
  s.name = "Richtkoppler mit S-Kurven";
  s.width = 2 * 2.5 * um + 2 * bend + straight, s.height = 5.4 * um;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 400e-15;
  const double center = 0.5 * s.height, near = 0.5 * (width + gap), far = near + offset;
  const double x_bend1 = 2.5 * um, x_straight = x_bend1 + bend, x_bend2 = x_straight + straight,
               x_out = x_bend2 + bend;
  for (int arm : {-1, +1}) {
    const std::string name = arm < 0 ? "oben" : "unten";
    const double y_far = center + arm * far, y_near = center + arm * near;
    s.shapes.push_back({"Zuleitung " + name, ShapeKind::rectangle, 0.5 * x_bend1 + 0.1 * um, y_far,
                        x_bend1 + 0.2 * um, width, 0.0, 1e-6, 0.5e-6, index_core});
    add_s_bend(s.shapes, "Kurve " + name, x_bend1, y_far, bend, y_near - y_far, width, index_core);
    s.shapes.push_back({"Koppelstrecke " + name, ShapeKind::rectangle, x_straight + 0.5 * straight, y_near,
                        straight + 0.2 * um, width, 0.0, 1e-6, 0.5e-6, index_core});
    add_s_bend(s.shapes, "Kurve " + name, x_bend2, y_near, bend, y_far - y_near, width, index_core);
    s.shapes.push_back({"Ableitung " + name, ShapeKind::rectangle, 0.5 * (x_out + s.width) - 0.1 * um, y_far,
                        s.width - x_out + 0.2 * um, width, 0.0, 1e-6, 0.5e-6, index_core});
  }
  Source source;
  source.name = "Modenquelle";
  source.kind = SourceKind::mode;
  source.x1 = source.x2 = 1.0 * um;
  source.y1 = center - far - 0.9 * um, source.y2 = center - far + 0.9 * um;
  s.sources = {source};
  s.monitors = {{"Fluss oben", MonitorKind::flux, s.width - 1.5 * um, center - far - 0.9 * um,
                 s.width - 1.5 * um, center - far + 0.9 * um},
                {"Fluss unten", MonitorKind::flux, s.width - 1.5 * um, center + far - 0.9 * um,
                 s.width - 1.5 * um, center + far + 0.9 * um}};
  for (int arm : {-1, +1}) {
    const double y = center + arm * far;
    const std::string side = arm < 0 ? "1" : "2";
    s.ports.push_back({"in" + side, 1.5 * um, y - 0.9 * um, 1.5 * um, y + 0.9 * um});
    s.ports.push_back({"out" + side, s.width - 1.5 * um, y - 0.9 * um, s.width - 1.5 * um, y + 0.9 * um});
  }
  return s;
}

// Grenzfläche zweier Medien: ebener Puls trifft senkrecht auf Glas; Reflexion nach Fresnel.
FieldScene interface() {
  FieldScene s;
  s.name = "Grenzfläche (Fresnel)";
  s.width = 20 * um, s.height = 3 * um;
  s.cell_size = 25e-9;
  s.boundary_y = Boundary::periodic;  // seitlich unendlich ausgedehnte ebene Welle
  s.duration = 120e-15;
  s.shapes = {{"Glas", ShapeKind::rectangle, 15 * um, 1.5 * um, 10 * um, 3 * um, 0.0, 1e-6, 0.5e-6, 1.5}};
  Source source;
  source.name = "Ebene Welle";
  source.x1 = source.x2 = 3 * um;
  source.y1 = 0.0, source.y2 = 3 * um;
  source.signal = SignalKind::pulse;
  source.pulse_duration = 8e-15;
  s.sources = {source};
  s.monitors = {{"vor der Grenzfläche", MonitorKind::point, 6 * um, 1.5 * um, 0, 0},
                {"im Glas", MonitorKind::point, 14 * um, 1.5 * um, 0, 0}};
  return s;
}

// Zwei kohärente Punktquellen: Interferenzmuster, das der Phasenunterschied dreht. Das ist die
// Feld-Ansicht dessen, was die Interferenz-Gatter des Logik-Labors ausnutzen.
FieldScene interference() {
  FieldScene s;
  s.name = "Interferenz zweier Quellen";
  s.width = 14 * um, s.height = 12 * um;
  s.cell_size = 40e-9;
  s.duration = 150e-15;
  Source a;
  a.name = "Quelle A";
  a.kind = SourceKind::point;
  a.x1 = 3 * um, a.y1 = 4.5 * um;
  Source b = a;
  b.name = "Quelle B";
  b.y1 = 7.5 * um;
  s.sources = {a, b};
  s.monitors = {{"Mitte", MonitorKind::point, 11 * um, 6 * um, 0, 0},
                {"seitlich", MonitorKind::point, 11 * um, 4 * um, 0, 0}};
  return s;
}

// Ringresonator am Buswellenleiter. Zeigt das Aufladen des Rings; die Resonanzen liegen wegen der
// Treppenstufen des Gitters und der 2D-Näherung nicht dort, wo ein gefertigter Ring sie hätte.
FieldScene ring() {
  FieldScene s;
  s.name = "Ringresonator am Wellenleiter";
  s.width = 12 * um, s.height = 10 * um;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 600e-15;
  Shape ring_shape;
  ring_shape.name = "Ring";
  ring_shape.kind = ShapeKind::ring;
  ring_shape.x = 6 * um, ring_shape.y = 5.2 * um;
  ring_shape.radius = 3 * um, ring_shape.inner_radius = 2.55 * um;
  ring_shape.index = index_core;
  s.shapes = {
      {"Bus", ShapeKind::rectangle, 6 * um, 1.65 * um, 12 * um, 0.45 * um, 0.0, 1e-6, 0.5e-6, index_core},
      ring_shape};
  Source source;
  source.name = "Einkopplung";
  source.x1 = source.x2 = 1.2 * um;
  source.y1 = 0.9 * um, source.y2 = 2.4 * um;
  source.beam_width = 0.3 * um;
  source.signal = SignalKind::pulse;
  source.pulse_duration = 20e-15;
  s.sources = {source};
  s.monitors = {{"Durchgang", MonitorKind::flux, 10.8 * um, 0.9 * um, 10.8 * um, 2.4 * um},
                {"Feld im Ring", MonitorKind::point, 6 * um, 8.0 * um, 0, 0}};
  return s;
}

// Photonischer Kristall: Dreiecksgitter aus Löchern in der Si-Schicht (effektive Brechzahl der TE-Mode,
// Löcher mit Oxid gefüllt), Hz-Polarisation. Gitterkonstante a = 430 nm, r = 150 nm (0,35 a): die
// PWE-Rechnung (docs/physics/pwe.md) liefert für diese Brechzahlen eine Lücke a/lambda = 0,261 ... 0,310
// (hier lambda = 1550 nm entspricht 0,277), der Liniendefekt führt dort bis a/lambda = 0,279 genau eine Mode.
constexpr double phc_a = 430e-9;
constexpr double phc_radius = 150e-9;
// Betriebswellenlänge: a/lambda = 0,272, im unteren Teil der Defektmode (nur eine Mode); der 60°-Knick
// überträgt hier fast alles, bei 1520 nm (a/lambda = 0,283, zweite Mode) nur etwa 30 %.
constexpr double phc_wavelength = 1580e-9;

enum class PhcDevice { straight, bend };

FieldScene phc_waveguide(PhcDevice device) {
  const double a = phc_a;
  FieldScene s;
  s.name = device == PhcDevice::straight ? "Photonischer Kristall: gerader W1-Wellenleiter"
                                         : "Photonischer Kristall: W1 mit 60°-Knick";
  s.width = round_pico(30 * a), s.height = round_pico(15 * a);
  s.cell_size = round_pico(a / 20.0);  // 20 Zellen je Gitterkonstante, Radius = 7 Zellen
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 2.4e-12;  // CW: ab ca. 2 ps stationär (Kontrolle: 3,6 ps ändert den Fluss um unter 2 %)

  CrystalLayout layout;
  layout.a = a, layout.radius = phc_radius;
  layout.origin_x = 0.0, layout.origin_y = round_pico(0.5 * s.height);
  layout.x_min = 0.0, layout.x_max = s.width, layout.y_min = 0.0, layout.y_max = s.height;
  const int bend_i = 14;  // Knickpunkt: x = 14 a
  if (device == PhcDevice::straight) {
    remove_line(layout, {-2, 0}, 0, 30 + 5);
  } else {
    remove_path(layout, {-2, 0}, {{0, bend_i + 3}, {1, 8}});
  }
  s.shapes.push_back({"Platte", ShapeKind::rectangle, round_pico(0.5 * s.width), round_pico(0.5 * s.height),
                      s.width, s.height, 0.0, 1e-6, 0.5e-6, index_core});
  for (Shape& hole : crystal_shapes(layout, index_cladding)) s.shapes.push_back(std::move(hole));

  const double yc = layout.origin_y;
  Source source;
  source.name = "Quelle im Defekt";
  source.kind = SourceKind::line;
  source.x1 = source.x2 = round_pico(1.5e-6);
  source.y1 = round_pico(yc - 1.1e-6), source.y2 = round_pico(yc + 1.1e-6);
  source.beam_width = 0.25e-6;
  source.signal = SignalKind::continuous;
  source.wavelength = phc_wavelength;
  s.sources = {source};
  s.monitors.push_back({"Fluss Eingang", MonitorKind::flux, round_pico(3.0e-6), round_pico(yc - 1.3e-6),
                        round_pico(3.0e-6), round_pico(yc + 1.3e-6)});
  if (device == PhcDevice::straight) {
    // gleicher Weg ab dem Knickpunkt wie im Knick-Aufbau: 6 Gitterkonstanten
    const double x = round_pico((bend_i + 6) * a);
    s.monitors.push_back(
        {"Fluss Ausgang", MonitorKind::flux, x, round_pico(yc - 1.3e-6), x, round_pico(yc + 1.3e-6)});
  } else {
    const auto [x, y] = site_position(layout, {bend_i, 6});
    s.monitors.push_back(
        {"Fluss Ausgang", MonitorKind::flux, round_pico(x - 1.3e-6), y, round_pico(x + 1.3e-6), y});
  }
  return s;
}

// Y-Kombinierer im Dreiecksgitter: zwei Eingangsarme (unter 120° und 240°) und ein Ausgangsarm (0°) treffen
// sich an einem Punkt. Quelle A und B sind Punktquellen in den Armen, gleich weit vom Knoten entfernt.
// Gleichphasig (phase_b = 0): Ausgang |a+b|^2/2 -> ODER bzw. UND je nach Schwelle; mit pi Phasenversatz
// (phase_b = pi): Auslöschung bei A = B = 1 -> XOR. Ausgabe: Fluss quer über den Ausgangsarm.
FieldScene phc_y(bool antiphase) {
  const double a = phc_a;
  FieldScene s;
  s.name = antiphase ? "Photonischer Kristall: Y-Kombinierer, Eingänge gegenphasig (XOR)"
                     : "Photonischer Kristall: Y-Kombinierer, Eingänge gleichphasig (ODER/UND)";
  s.width = round_pico(24 * a), s.height = round_pico(15 * a);
  s.cell_size = round_pico(a / 20.0);
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 2.4e-12;

  CrystalLayout layout;
  layout.a = a, layout.radius = phc_radius;
  layout.origin_x = 0.0, layout.origin_y = round_pico(0.5 * s.height);
  layout.x_min = 0.0, layout.x_max = s.width, layout.y_min = 0.0, layout.y_max = s.height;
  const Site node{12, 0};
  remove_line(layout, node, 0, 15);  // Ausgangsarm bis zum rechten Rand
  remove_line(layout, node, 2, 9);   // Eingangsarm A: nach links oben (120°)
  remove_line(layout, node, 4, 9);   // Eingangsarm B: nach links unten (240°)
  s.shapes.push_back({"Platte", ShapeKind::rectangle, round_pico(0.5 * s.width), round_pico(0.5 * s.height),
                      s.width, s.height, 0.0, 1e-6, 0.5e-6, index_core});
  for (Shape& hole : crystal_shapes(layout, index_cladding)) s.shapes.push_back(std::move(hole));

  Source source;
  source.kind = SourceKind::point;
  source.signal = SignalKind::continuous;
  source.wavelength = phc_wavelength;
  source.name = "A";
  std::tie(source.x1, source.y1) = site_position(layout, {node.i - 4, 4});
  s.sources.push_back(source);
  source.name = "B";
  std::tie(source.x1, source.y1) = site_position(layout, {node.i, -4});
  source.phase = antiphase ? constants::pi : 0.0;
  s.sources.push_back(source);

  const double x_out = round_pico((node.i + 6) * a);
  s.monitors.push_back({"Fluss Ausgang", MonitorKind::flux, x_out, round_pico(layout.origin_y - 1.3e-6),
                        x_out, round_pico(layout.origin_y + 1.3e-6)});
  return s;
}

}  // namespace

std::vector<std::string> reference_scene_names() {
  return {"field_waveguide", "field_interface", "field_interference", "field_ring",
          "field_gap",       "field_gap_pair",  "field_coupler",      "field_phc_w1",
          "field_phc_bend",  "field_phc_y",     "field_phc_y_xor"};
}

FieldScene reference_scene(std::string_view name) {
  if (name == "field_waveguide") return waveguide();
  if (name == "field_interface") return interface();
  if (name == "field_interference") return interference();
  if (name == "field_ring") return ring();
  if (name == "field_gap") return gap_chain(1);
  if (name == "field_gap_pair") return gap_chain(2);
  if (name == "field_coupler") return coupler();
  if (name == "field_phc_w1") return phc_waveguide(PhcDevice::straight);
  if (name == "field_phc_bend") return phc_waveguide(PhcDevice::bend);
  if (name == "field_phc_y") return phc_y(false);
  if (name == "field_phc_y_xor") return phc_y(true);
  throw FieldError("Unbekannte Referenz-Szene '" + std::string(name) + "'");
}

}  // namespace photonlab::field
