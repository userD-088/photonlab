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
  add_crystal(s, layout, index_cladding);

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
  add_crystal(s, layout, index_cladding);

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

// Taper: einmodiger Wellenleiter (0,45 um) weitet sich über 10 um auf 3 um auf. Langsam genug (adiabatisch),
// bleibt das Licht in der Grundmode; die Flussmonitore vor und hinter dem Taper zeigen den Durchsatz
// (gemessen: hinten 100 % des Flusses vorn).
FieldScene taper() {
  FieldScene s;
  s.name = "Taper 0,45 auf 3 um";
  s.width = 20 * um, s.height = 6 * um;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 400e-15;
  Shape narrow, wide, cone;
  narrow.name = "schmal", narrow.x = 2.5 * um, narrow.y = 3 * um, narrow.width = 5 * um,
  narrow.height = 0.45 * um;
  wide.name = "breit", wide.x = 17.5 * um, wide.y = 3 * um, wide.width = 5 * um, wide.height = 3 * um;
  cone.name = "Taper", cone.kind = ShapeKind::taper, cone.x = 10 * um, cone.y = 3 * um;
  cone.width = 10 * um, cone.height = 0.45 * um, cone.width2 = 3 * um;
  for (Shape* sh : {&narrow, &wide, &cone}) sh->index = index_core;
  s.shapes = {narrow, cone, wide};
  Source source;
  source.name = "Modenquelle";
  source.kind = SourceKind::mode;
  source.x1 = source.x2 = 1.5 * um;
  source.y1 = 1.5 * um, source.y2 = 4.5 * um;
  s.sources = {source};
  s.monitors = {{"Fluss vorn", MonitorKind::flux, 3.5 * um, 1.5 * um, 3.5 * um, 4.5 * um},
                {"Fluss hinten", MonitorKind::flux, 18.5 * um, 0.8 * um, 18.5 * um, 5.2 * um}};
  return s;
}

// 90-Grad-Bogen mit 2,5 um Radius (Mitte des Kerns): bei diesem Brechzahlunterschied verliert der Bogen
// kaum Licht (gemessen: 98,8 % kommen hinten an); mit kleinerem Radius steigt die Abstrahlung sichtbar.
FieldScene bend() {
  const double radius = 2.5 * um, y0 = 2 * um, x0 = 4 * um, w = 0.45 * um;
  FieldScene s;
  s.name = "90-Grad-Bogen";
  s.width = 9 * um, s.height = 9 * um;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 400e-15;
  Shape in, arc, out;
  in.name = "Zuleitung", in.x = 0.5 * x0, in.y = y0, in.width = x0, in.height = w;
  arc.name = "Bogen", arc.kind = ShapeKind::arc, arc.x = x0, arc.y = y0 + radius;
  arc.radius = radius + 0.5 * w, arc.inner_radius = radius - 0.5 * w;
  arc.rotation = round_pico(-0.5 * constants::pi), arc.sweep = round_pico(0.5 * constants::pi);
  out.name = "Ableitung", out.x = x0 + radius, out.y = 0.5 * (y0 + radius + s.height);
  out.width = w, out.height = s.height - y0 - radius;
  for (Shape* sh : {&in, &arc, &out}) sh->index = index_core;
  s.shapes = {in, arc, out};
  Source source;
  source.name = "Modenquelle";
  source.kind = SourceKind::mode;
  source.x1 = source.x2 = 1.2 * um;
  source.y1 = 0.6 * um, source.y2 = 3.4 * um;
  s.sources = {source};
  s.monitors = {{"Fluss vorn", MonitorKind::flux, 2.5 * um, 0.6 * um, 2.5 * um, 3.4 * um},
                {"Fluss hinten", MonitorKind::flux, x0 + radius - 1.4 * um, 7.6 * um, x0 + radius + 1.4 * um,
                 7.6 * um}};
  return s;
}

// Bragg-Spiegel: 30 Verbreiterungen (0,45 -> 0,65 um, je eine halbe Periode) auf einem einmodigen
// Wellenleiter. Ein kurzer Puls deckt viele Wellenlängen ab: hinter dem Spiegel fehlt im Spektrum ein Band um
// 1550 nm (gemessen: etwa zwei Drittel reflektiert, 1530-1580 nm), davor kommt es zurück. Periode aus der
// Bragg-Bedingung lambda = 2 n_mittel Lambda mit dem Mittel der effektiven Indizes beider Breiten
// (Schicht-Eigenwertgleichung). Ez-Polarisation: mit Hz koppelt diese Zahnform die Grundmode kaum in sich
// zurück (gemessen: keine Sperrbande, nur Umwandlung in höhere Moden um 1,3 um).
FieldScene bragg() {
  auto n_eff = [](double width) {
    return modes::slab_modes(index_core, index_cladding, index_cladding, width, 1550e-9,
                             modes::SlabPolarization::te)
        .front()
        .n_eff;
  };
  const double period = round_pico(1550e-9 / (n_eff(0.45 * um) + n_eff(0.65 * um)));
  const int teeth = 30;
  FieldScene s;
  s.name = "Bragg-Spiegel im Wellenleiter";
  s.width = round_pico(10 * um + teeth * period), s.height = 4 * um;
  s.cell_size = 20e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::ez;
  s.duration = 1.2e-12;
  Shape core;
  core.name = "Kern", core.x = 0.5 * s.width, core.y = 2 * um, core.width = s.width, core.height = 0.45 * um;
  core.index = index_core;
  s.shapes = {core};
  for (int k = 0; k < teeth; ++k) {
    Shape tooth = core;
    tooth.name = "Zahn " + std::to_string(k + 1);
    tooth.x = round_pico(5 * um + (k + 0.25) * period), tooth.width = round_pico(0.5 * period);
    tooth.height = 0.65 * um;
    tooth.group = 0;
    s.shapes.push_back(tooth);
  }
  s.groups = {{"Gitter", round_pico(5 * um + 0.5 * teeth * period), 2 * um, 0.0, -1}};
  Source source;
  source.name = "Puls";
  source.kind = SourceKind::mode;
  source.signal = SignalKind::pulse;
  source.pulse_duration = 8e-15;
  source.x1 = source.x2 = 2 * um;
  source.y1 = 0.6 * um, source.y2 = 3.4 * um;
  s.sources = {source};
  const double behind = s.width - 2.5 * um;
  s.monitors = {{"Fluss vorn", MonitorKind::flux, 3.5 * um, 0.6 * um, 3.5 * um, 3.4 * um},
                {"Fluss hinten", MonitorKind::flux, behind, 0.6 * um, behind, 3.4 * um},
                {"Feld vorn", MonitorKind::point, 3 * um, 2 * um, 0, 0},
                {"Feld hinten", MonitorKind::point, behind, 2 * um, 0, 0}};
  return s;
}

// Kugellinse (in 2D: Zylinder) aus Glas im Vakuum: ein breiter Gauß-Strahl wird gebündelt. Brennweite einer
// Kugellinse vom Mittelpunkt aus: f = n R / (2 (n - 1)); hier etwa 6,5 um, der Monitor sitzt dort (gemessen:
// Feldspitze dort 15-mal größer als 2 um daneben).
FieldScene lens() {
  const double n = index_cladding, radius = 4 * um, center = 7 * um;
  FieldScene s;
  s.name = "Linse: Strahl bündeln";
  s.width = 20 * um, s.height = 14 * um;
  s.cell_size = 40e-9;
  s.background_index = 1.0;
  s.polarization = Polarization::ez;
  s.duration = 250e-15;
  Shape glass;
  glass.name = "Linse", glass.kind = ShapeKind::circle, glass.x = center, glass.y = 7 * um;
  glass.radius = radius, glass.index = n;
  s.shapes = {glass};
  Source source;
  source.name = "Strahl";
  source.kind = SourceKind::line;
  source.x1 = source.x2 = 1.5 * um;
  source.y1 = 1 * um, source.y2 = 13 * um;
  source.beam_width = 3.5 * um;
  s.sources = {source};
  const double focus = round_pico(center + n * radius / (2.0 * (n - 1.0)));
  s.monitors = {{"Brennpunkt", MonitorKind::point, focus, 7 * um, 0, 0},
                {"neben dem Brennpunkt", MonitorKind::point, focus, 9 * um, 0, 0},
                {"Fluss durch den Fokus", MonitorKind::flux, focus, 5 * um, focus, 9 * um}};
  return s;
}

// Halbaddierer auf einem Chip (Streifenwellenleiter, Hz): Eingang A kommt von links, B von rechts. Jeder
// Eingang teilt sich in einem Y-Teiler (zwei Bögen) in einen oberen und einen unteren Arm. Oben treffen sich
// die Arme in einem Y-Kombinierer (zwei Bögen, Ausgang nach oben): Summe = XOR, weil ein verbreitertes Stück
// im oberen Arm von B dessen Phase um pi verschiebt. Unten ebenso, aber gleichphasig: Übertrag = UND über
// einen Schwellwert (Pegel 1/4 gegen 1, also höchstens 6 dB Kontrast - ein besseres UND braucht
// Nichtlinearität). Spiegelsymmetrisch, damit beide Wege gleich lang sind.
// phase_length: Länge des verbreiterten Stücks [m]; 0 = aus der Schicht-Eigenwertgleichung (pi-Bedingung).
FieldScene halfadder(double phase_length) {
  const double w = 0.30 * um, w_shift = 0.45 * um;  // 0,30 um: auch für Hz einmodig (TM1 nicht geführt)
  const double r = 2 * um;                          // Radius aller Bögen (Kernmitte)
  const double width = 27 * um, height = 24 * um;
  const double xc = 0.5 * width, yc = 0.5 * height;
  const double xs = 2.5 * um;  // Teiler von A
  const double yt = 7 * um;    // Höhe der oberen Arme: 5 um Ausgang, damit Streulicht des Kombinierers
                               // am Monitor vorbeigeht
  if (phase_length <= 0.0) {
    auto n_eff = [](double core_width) {
      return modes::slab_modes(index_core, index_cladding, index_cladding, core_width, 1550e-9,
                               modes::SlabPolarization::tm)
          .front()
          .n_eff;
    };
    phase_length = 1550e-9 / (2.0 * (n_eff(w_shift) - n_eff(w)));
  }
  FieldScene s;
  s.name = "Halbaddierer (XOR + UND auf einem Chip)";
  s.width = width, s.height = height;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 1.2e-12;

  // Viertel oben links; die anderen drei entstehen durch Spiegeln an x = xc bzw. y = yc
  auto rect = [&](const char* name, double x0, double y0, double x1, double y1) {
    Shape sh;
    sh.name = name, sh.x = round_pico(0.5 * (x0 + x1)), sh.y = round_pico(0.5 * (y0 + y1));
    sh.width = round_pico(std::abs(x1 - x0)), sh.height = round_pico(std::abs(y1 - y0));
    sh.index = index_core;
    return sh;
  };
  auto arc = [&](const char* name, double cx, double cy, double start, double sweep) {
    Shape sh;
    sh.name = name, sh.kind = ShapeKind::arc, sh.x = cx, sh.y = cy;
    sh.radius = r + 0.5 * w, sh.inner_radius = r - 0.5 * w;
    sh.rotation = round_pico(start), sh.sweep = round_pico(sweep);
    sh.index = index_core;
    return sh;
  };
  const double pi = constants::pi;
  std::vector<Shape> quarter = {
      rect("Eingang", 0.0, yc - 0.5 * w, xs, yc + 0.5 * w),
      arc("Teiler", xs, yc - r, 0.0, 0.5 * pi),  // nach rechts, dann nach oben
      rect("Steigung", xs + r - 0.5 * w, yt + r, xs + r + 0.5 * w, yc - r),
      arc("Knick", xs + 2 * r, yt + r, pi, 0.5 * pi),  // nach oben, dann nach rechts
      rect("Arm", xs + 2 * r, yt - 0.5 * w, xc - r, yt + 0.5 * w),
      arc("Kombinierer", xc - r, yt - r, 0.0, 0.5 * pi),  // nach rechts, dann nach oben
      rect("Ausgang", xc - 0.5 * w, 0.0, xc + 0.5 * w, yt - r)};
  for (int half = 0; half < 2; ++half)      // 0 = oben (Summe), 1 = unten (Übertrag)
    for (int side = 0; side < 2; ++side) {  // 0 = links (A), 1 = rechts (B)
      for (Shape sh : quarter) {
        const std::string part = sh.name;
        if (part == "Eingang" && half == 1) continue;  // jeder Eingang nur einmal
        if (part == "Ausgang" && side == 1) continue;  // der Ausgang liegt auf der Mittellinie
        if (side == 1) {
          sh.x = round_pico(width - sh.x);
          if (sh.kind == ShapeKind::arc) sh.rotation = round_pico(pi - sh.rotation - sh.sweep);
        }
        if (half == 1) {
          sh.y = round_pico(height - sh.y);
          if (sh.kind == ShapeKind::arc) sh.rotation = round_pico(-sh.rotation - sh.sweep);
        }
        if (part == "Eingang")
          sh.name = side == 0 ? "Eingang A" : "Eingang B";
        else if (part == "Ausgang")
          sh.name = half == 0 ? "Ausgang Summe" : "Ausgang Übertrag";
        else
          sh.name = std::string(half == 0 ? "Summe: " : "Übertrag: ") + (side == 0 ? "A " : "B ") + part;
        s.shapes.push_back(sh);
      }
    }
  // Phasenschieber: verbreitertes Stück mittig im oberen Arm von B
  const double arm_mid = round_pico(0.5 * ((width - xs - 2 * r) + (xc + r)));
  Shape shifter = rect("Phasenschieber (pi)", arm_mid - 0.5 * phase_length, yt - 0.5 * w_shift,
                       arm_mid + 0.5 * phase_length, yt + 0.5 * w_shift);
  s.shapes.push_back(shifter);

  for (const char* name : {"A", "B"}) {
    Source source;
    source.name = name;
    source.kind = SourceKind::mode;
    source.signal = SignalKind::continuous;
    source.x1 = source.x2 = std::string(name) == "A" ? 1 * um : width - 1 * um;
    source.y1 = yc - 1.2 * um, source.y2 = yc + 1.2 * um;
    const bool name_is_b = std::string(name) == "B";
    source.phase = name_is_b ? 0.0 * constants::pi : 0.0;
    s.sources.push_back(source);
  }
  // Modenmonitore zählen nur das geführte Licht (Gatter-Ausgang), Flussmonitore alles, was durchläuft
  s.monitors = {{"Mode Summe", MonitorKind::mode, xc - 1.2 * um, 1.5 * um, xc + 1.2 * um, 1.5 * um},
                {"Mode Übertrag", MonitorKind::mode, xc - 1.2 * um, height - 1.5 * um, xc + 1.2 * um,
                 height - 1.5 * um},
                {"Fluss Summe", MonitorKind::flux, xc - 0.7 * um, 1.0 * um, xc + 0.7 * um, 1.0 * um},
                {"Fluss Übertrag", MonitorKind::flux, xc - 0.7 * um, height - 1.0 * um, xc + 0.7 * um,
                 height - 1.0 * um}};
  return s;
}

// Halbaddierer als ein Kristall-Chip (W1-Wellenleiter im Dreiecksgitter, Hz, 1580 nm): A kommt von links und
// teilt sich am Knoten SA in Arme nach oben rechts (60°) und unten rechts (300°); B kommt von rechts, teilt
// sich am Knoten SB nach oben links und unten links und biegt mit einem 60°-Knick in die Waagerechte. Oben
// treffen sich die Arme in einer 3-zähligen Y-Verzweigung, Ausgang schräg nach oben links (Summe); unten
// gespiegelt (Übertrag). Unten ist der Arm von A länger: das verschiebt dort die Phase gegenüber oben, so
// dass oben gegenphasig (Summe = XOR) und unten gleichphasig (Übertrag = UND, höchstens 6 dB) addiert wird.
// extra_steps: zusätzliche Gitterschritte des unteren A-Arms; b_phase: Phase der Quelle B; a_amplitude:
// Amplitude von A relativ zu B (Abgleich der Pegel: der Weg von A verliert mehr als der von B).
FieldScene phc_halfadder(int extra_steps, double b_phase, double a_amplitude, double wavelength) {
  const double a = phc_a;
  FieldScene s;
  s.name = "Photonischer Kristall: Halbaddierer auf einem Chip";
  s.width = round_pico(30 * a), s.height = round_pico(20 * a);
  s.cell_size = round_pico(a / 20.0);
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;
  s.duration = 4.8e-12;

  CrystalLayout layout;
  layout.a = a, layout.radius = phc_radius;
  layout.origin_x = round_pico(9 * a), layout.origin_y = round_pico(0.5 * s.height);
  layout.x_min = 0.0, layout.x_max = s.width, layout.y_min = 0.0, layout.y_max = s.height;
  // oben n = 6 Schritte je Arm; unten ist der Arm von A um extra_steps länger (Phasenhebel: ein Schritt
  // entspricht der Bloch-Phase k*a der W1-Mode), B gleicht das mit dem waagerechten Stück aus
  const int n_top = 6, n_bottom = 6 + extra_steps, m = 14;
  const Site sa{0, 0}, sb{m, 0}, top{0, n_top}, bottom{n_bottom, -n_bottom};
  remove_line(layout, {-14, 0}, 0, 15);                             // Eingang A bis SA
  remove_line(layout, sa, 1, n_top + 1);                            // A nach oben rechts bis top
  remove_line(layout, sa, 5, n_bottom + 1);                         // A nach unten rechts bis bottom
  remove_line(layout, sb, 0, 14);                                   // Eingang B von rechts bis SB
  remove_path(layout, sb, {{2, n_top + 1}, {3, m - n_top}});        // B nach oben links, dann waagerecht
  remove_path(layout, sb, {{4, n_bottom + 1}, {3, m - n_bottom}});  // B nach unten links, dann waagerecht
  remove_line(layout, top, 2, 12);                                  // Ausgang Summe schräg nach oben links
  remove_line(layout, bottom, 4, 12);  // Ausgang Übertrag schräg nach unten links
  s.shapes.push_back({"Platte", ShapeKind::rectangle, round_pico(0.5 * s.width), round_pico(0.5 * s.height),
                      s.width, s.height, 0.0, 1e-6, 0.5e-6, index_core});
  add_crystal(s, layout, index_cladding);

  Source source;
  source.kind = SourceKind::point;
  source.signal = SignalKind::continuous;
  source.wavelength = wavelength;
  source.name = "A";
  std::tie(source.x1, source.y1) = site_position(layout, {-6, 0});
  source.x2 = source.x1, source.y2 = source.y1;
  source.amplitude = a_amplitude;
  s.sources.push_back(source);
  source.name = "B";
  source.amplitude = 1.0;
  std::tie(source.x1, source.y1) = site_position(layout, {20, 0});
  source.x2 = source.x1, source.y2 = source.y1;
  source.phase = round_pico(b_phase);
  s.sources.push_back(source);

  // Flussmonitore waagerecht über den schrägen Ausgängen, kurz vor dem Rand
  const auto [tx, ty] = site_position(layout, {top.i - 3, top.j + 3});
  const auto [bx, by] = site_position(layout, {bottom.i, bottom.j - 3});
  s.monitors = {
      {"Fluss Summe", MonitorKind::flux, round_pico(tx - 1.0e-6), ty, round_pico(tx + 1.0e-6), ty},
      {"Fluss Übertrag", MonitorKind::flux, round_pico(bx - 1.0e-6), by, round_pico(bx + 1.0e-6), by}};
  return s;
}

// --- Bausteine ---------------------------------------------------------------------------------------------

// Weg aus geraden Stücken und Bögen ab einem Punkt mit Richtung heading (Bildschirm: y nach unten, Winkel im
// Uhrzeigersinn). turn > 0: Bogen im Uhrzeigersinn (nach rechts in Laufrichtung), turn < 0: dagegen.
struct Path {
  double x = 0.0, y = 0.0, heading = 0.0;
  double width = 0.30 * um;
  std::vector<Shape>* out = nullptr;
  std::string name;
  void straight(double length) {
    Shape sh;
    sh.name = name;
    sh.x = round_pico(x + 0.5 * length * std::cos(heading));
    sh.y = round_pico(y + 0.5 * length * std::sin(heading));
    sh.width = round_pico(length), sh.height = width, sh.rotation = round_pico(heading);
    sh.index = index_core;
    out->push_back(sh);
    x += length * std::cos(heading), y += length * std::sin(heading);
  }
  void bend(double radius, double turn) {
    const double side = turn > 0.0 ? 0.5 * constants::pi : -0.5 * constants::pi;
    const double cx = x + radius * std::cos(heading + side), cy = y + radius * std::sin(heading + side);
    const double phi0 = heading - side;  // Winkel des Startpunkts vom Mittelpunkt aus
    const double phi1 = phi0 + turn;
    Shape sh;
    sh.name = name;
    sh.kind = ShapeKind::arc;
    sh.x = round_pico(cx), sh.y = round_pico(cy);
    sh.radius = round_pico(radius + 0.5 * width), sh.inner_radius = round_pico(radius - 0.5 * width);
    sh.rotation = round_pico(std::min(phi0, phi1)), sh.sweep = round_pico(std::abs(turn));
    sh.index = index_core;
    out->push_back(sh);
    x = cx + radius * std::cos(phi1), y = cy + radius * std::sin(phi1);
    heading += turn;
  }
};

std::vector<std::string> block_names() {
  return {"y_splitter",    "y_combiner", "s_bend",    "bend90",     "coupler",
          "phase_shifter", "ring_bus",   "halfadder", "regenerator"};
}

}  // namespace

std::vector<std::string> building_block_names() { return block_names(); }

std::string building_block_label(std::string_view name) {
  if (name == "y_splitter") return "Y-Verzweigung (1 auf 2)";
  if (name == "y_combiner") return "Y-Kombinierer (2 auf 1)";
  if (name == "s_bend") return "S-Bogen (2 um Versatz)";
  if (name == "bend90") return "90-Grad-Bogen";
  if (name == "coupler") return "Richtkoppler";
  if (name == "phase_shifter") return "Phasenschieber (Verbreiterung)";
  if (name == "ring_bus") return "Ringresonator am Bus";
  if (name == "halfadder") return "Halbaddierer-Chip";
  if (name == "regenerator") return "Regenerator (Detektor + Sender)";
  throw FieldError("Unbekannter Baustein '" + std::string(name) + "'");
}

int add_building_block(FieldScene& scene, std::string_view name, double x, double y, int parent_group) {
  const std::string label = building_block_label(name);  // prüft den Namen
  std::vector<Shape> shapes;
  std::vector<Source> sources;
  std::vector<Monitor> monitors;
  const double w = 0.30 * um, pi = constants::pi;
  Path path;
  path.out = &shapes;
  auto start = [&](const char* part, double px, double py, double heading) {
    path.name = part, path.x = px, path.y = py, path.heading = heading;
  };
  // Bezugspunkt (0, 0); Koordinaten relativ, am Ende verschoben
  if (name == "y_splitter" || name == "y_combiner") {
    // Eingang 2 um, dann je ein S-Bogen (Radius 6 um, 25 Grad) nach oben und unten, 1 um Ausgänge
    const double r = 6 * um, a = 25.0 * pi / 180.0;
    start("Eingang", -2 * um, 0.0, 0.0);
    path.straight(2 * um);
    for (int side : {-1, 1}) {
      start(side < 0 ? "Arm oben" : "Arm unten", 0.0, 0.0, 0.0);
      path.bend(r, side * a);
      path.bend(r, -side * a);
      path.straight(1 * um);
    }
    if (name == "y_combiner")  // gespiegelt: zwei Eingänge links, Ausgang rechts
      for (Shape& sh : shapes) {
        sh.x = -sh.x;
        if (sh.kind == ShapeKind::arc)
          sh.rotation = round_pico(pi - sh.rotation - sh.sweep);
        else
          sh.rotation = round_pico(-sh.rotation);
        if (sh.name == "Eingang")
          sh.name = "Ausgang";
        else if (sh.name.rfind("Arm", 0) == 0)
          sh.name = "Eingang" + sh.name.substr(3);
      }
  } else if (name == "s_bend") {
    // Versatz 2 um nach unten: zwei Bögen gleichen Radius, je arccos(1 - d/(2r))
    const double r = 5 * um, turn = std::acos(1.0 - 2 * um / (2.0 * r));
    start("S-Bogen", -r * std::sin(turn), -1 * um, 0.0);
    path.bend(r, turn);
    path.bend(r, -turn);
  } else if (name == "bend90") {
    start("Bogen", 0.0, 0.0, 0.0);
    path.bend(2.5 * um, 0.5 * pi);
  } else if (name == "coupler") {
    // zwei Wellenleiter, die sich über 5 um auf 0,2 um Spalt nähern (S-Bögen ein und aus)
    const double r = 5 * um, gap = 0.2 * um, offset = 1 * um, turn = std::acos(1.0 - offset / (2.0 * r));
    const double run_in = 2.0 * r * std::sin(turn), length = 5 * um;
    for (int side : {-1, 1}) {
      const double yc = side * 0.5 * (gap + w);
      start(side < 0 ? "Arm oben" : "Arm unten", -0.5 * length - run_in, yc + side * offset, 0.0);
      path.bend(r, -side * turn);
      path.bend(r, side * turn);
      path.straight(length);
      path.bend(r, side * turn);
      path.bend(r, -side * turn);
    }
  } else if (name == "phase_shifter") {
    // Verbreiterung 0,30 -> 0,45 um über 1,75 um: bei Hz und 1550 nm etwa pi (gemessen im Halbaddierer)
    start("Zuleitung", -2.875 * um, 0.0, 0.0);
    path.straight(2 * um);
    path.width = 0.45 * um, path.name = "Verbreiterung";
    path.straight(1.75 * um);
    path.width = w, path.name = "Ableitung";
    path.straight(2 * um);
  } else if (name == "ring_bus") {
    start("Bus", -6 * um, 0.0, 0.0);
    path.straight(12 * um);
    Shape ring;
    ring.name = "Ring", ring.kind = ShapeKind::ring;
    ring.radius = 3 * um + 0.5 * w, ring.inner_radius = 3 * um - 0.5 * w;
    ring.x = 0.0, ring.y = round_pico(3 * um + w + 0.2 * um), ring.index = index_core;
    shapes.push_back(ring);
  } else if (name == "halfadder") {
    // ganzer Chip samt Quellen und Monitoren, um seine Mitte
    const FieldScene chip = halfadder(1.75 * um);
    for (Shape sh : chip.shapes) {
      sh.x = round_pico(sh.x - 0.5 * chip.width), sh.y = round_pico(sh.y - 0.5 * chip.height);
      shapes.push_back(sh);
    }
    for (Source q : chip.sources) {
      q.x1 -= 0.5 * chip.width, q.x2 -= 0.5 * chip.width, q.y1 -= 0.5 * chip.height,
          q.y2 -= 0.5 * chip.height;
      sources.push_back(q);
    }
    for (Monitor m : chip.monitors) {
      m.x1 -= 0.5 * chip.width, m.x2 -= 0.5 * chip.width, m.y1 -= 0.5 * chip.height,
          m.y2 -= 0.5 * chip.height;
      monitors.push_back(m);
    }
  } else if (name == "regenerator") {
    // Detektor (Flussmonitor) links, Sender (Punktquelle) rechts: funktionieren überall. Auf einem
    // Wellenleiter besser als Modenmonitor und Modenquelle quer über den Kern legen (nur geführtes Licht).
    // Die Schwelle ist ein Startwert und muss zum Pegel passen.
    Monitor detector;
    detector.kind = MonitorKind::flux;
    detector.x1 = detector.x2 = -1 * um, detector.y1 = -1.2 * um, detector.y2 = 1.2 * um;
    Source sender;
    sender.kind = SourceKind::point;
    sender.x1 = sender.x2 = 1 * um, sender.y1 = sender.y2 = 0.0;
    sender.trigger_threshold = 1e-9;
    if (!scene.sources.empty()) sender.wavelength = scene.sources.front().wavelength;
    monitors.push_back(detector);
    sources.push_back(sender);
  }

  // eindeutige Namen: Baustein-Nummer anhängen
  const int number = static_cast<int>(scene.groups.size()) + 1;
  const std::string suffix = " " + std::to_string(number);
  const int group = static_cast<int>(scene.groups.size());
  scene.groups.push_back({label + suffix, round_pico(x), round_pico(y), 0.0, parent_group});
  for (Shape& sh : shapes) {
    sh.name += suffix;
    sh.x = round_pico(sh.x + x), sh.y = round_pico(sh.y + y);
    sh.group = group;
    scene.shapes.push_back(sh);
  }
  for (Monitor& m : monitors) {
    m.name = (m.name.empty() ? std::string("Detektor") : m.name) + suffix;
    m.x1 += x, m.x2 += x, m.y1 += y, m.y2 += y;
    m.group = group;
    scene.monitors.push_back(m);
  }
  for (Source& q : sources) {
    q.name = (q.name.empty() ? std::string("Sender") : q.name) + suffix;
    if (name == "regenerator") q.trigger = "Detektor" + suffix;
    q.x1 += x, q.x2 += x, q.y1 += y, q.y2 += y;
    q.group = group;
    scene.sources.push_back(q);
  }
  return group;
}

// Volladdierer in einer Szene: Halbaddierer-Chip 1 (A, B) und Chip 2 (S1, Cin) nebeneinander, rechts ein
// Y-Kombinierer als ODER für den Übertrag. Verbunden über Regeneratoren (Detektor + Elektronik + Sender): der
// Modenmonitor der Summe von Chip 1 schaltet den Sender S1 von Chip 2, die Überträge beider Chips schalten
// die Sender C1 und C2 am Kombinierer. Alle Sender strahlen einseitig ins Bauteil, damit kein Licht über die
// Lücken in den Nachbar-Chip läuft. Schwellen aus den gemessenen Pegeln des Chips (logic.md).
FieldScene fulladder_scene() {
  const FieldScene chip = halfadder(1.75 * um);
  const double gap = 6 * um, w = 0.30 * um;
  const double x_or = 2 * chip.width + 2 * gap;  // Anfang der Eingangsarme des Kombinierers
  FieldScene s;
  s.name = "Volladdierer (zwei Halbaddierer + ODER, Regeneratoren)";
  s.width = round_pico(x_or + 14 * um), s.height = chip.height;
  s.cell_size = chip.cell_size;
  s.background_index = chip.background_index;
  s.polarization = chip.polarization;
  s.duration = 3.6e-12;  // drei Stufen nacheinander, je etwa 1 ps bis zum stationären Zustand

  for (int n = 0; n < 2; ++n) {
    const double dx = n * (chip.width + gap);
    const int group = static_cast<int>(s.groups.size());
    const std::string tag = " " + std::to_string(n + 1);
    s.groups.push_back(
        {"Halbaddierer" + tag, round_pico(dx + 0.5 * chip.width), round_pico(0.5 * chip.height), 0.0, -1});
    for (Shape sh : chip.shapes) {
      sh.x = round_pico(sh.x + dx), sh.name += tag, sh.group = group;
      s.shapes.push_back(sh);
    }
    for (Monitor m : chip.monitors) {
      if (m.kind != MonitorKind::mode) continue;  // Flussmonitore des Chips werden hier nicht gebraucht
      m.x1 = round_pico(m.x1 + dx), m.x2 = round_pico(m.x2 + dx), m.name += tag, m.group = group;
      s.monitors.push_back(m);
    }
    for (std::size_t q = 0; q < chip.sources.size(); ++q) {
      Source src = chip.sources[q];
      src.x1 = round_pico(src.x1 + dx), src.x2 = src.x1, src.group = group;
      src.direction = q == 0 ? 1 : -1;  // links nach rechts, rechts nach links: ins Bauteil
      if (n == 0) {
        src.name = q == 0 ? "A" : "B";
      } else if (q == 0) {
        src.name = "S1";  // Regenerator: Summe von Chip 1
        src.trigger = "Mode Summe 1", src.trigger_threshold = 5e-8;
      } else {
        src.name = "Cin";
      }
      s.sources.push_back(src);
    }
  }

  // ODER: Y-Kombinierer, Ausgang bis an den rechten Rand verlängert
  const double r = 6 * um, a = 25.0 * constants::pi / 180.0;
  const double arm_dx = 2 * r * std::sin(a) + 1 * um, arm_dy = 2 * r * (1 - std::cos(a));
  const double yc = 0.5 * chip.height;
  const int or_group = add_building_block(s, "y_combiner", round_pico(x_or + arm_dx), yc);
  s.groups[static_cast<std::size_t>(or_group)].name = "ODER (Übertrag)";
  Shape tail;
  tail.name = "Ausgang ODER", tail.group = or_group, tail.index = index_core;
  tail.x = round_pico(0.5 * (x_or + arm_dx + 2 * um + s.width)), tail.y = yc;
  tail.width = round_pico(s.width - (x_or + arm_dx + 2 * um)), tail.height = w;
  s.shapes.push_back(tail);
  for (int side : {-1, 1}) {
    Source src;
    src.name = side < 0 ? "C1" : "C2";
    src.kind = SourceKind::mode, src.direction = 1, src.group = or_group;
    src.x1 = src.x2 = round_pico(x_or + 0.5 * um);
    src.y1 = round_pico(yc + side * arm_dy - 0.9 * um), src.y2 = round_pico(yc + side * arm_dy + 0.9 * um);
    src.trigger = side < 0 ? "Mode Übertrag 1" : "Mode Übertrag 2";
    src.trigger_threshold = 1.1e-6;
    s.sources.push_back(src);
  }
  s.monitors.push_back({"Mode Übertrag (Cout)", MonitorKind::mode, round_pico(s.width - 3 * um),
                        round_pico(yc - 1.2 * um), round_pico(s.width - 3 * um), round_pico(yc + 1.2 * um)});
  s.monitors.back().group = or_group;
  return s;
}

// --- weitere Beispiele (Grundlagen, Logik, Kristalle)
// -------------------------------------------------------
namespace {

FieldScene base(const char* name, double width, double height, double cell, Polarization polarization,
                double duration, double background = index_cladding) {
  FieldScene s;
  s.name = name;
  s.width = width, s.height = height, s.cell_size = cell;
  s.background_index = background;
  s.polarization = polarization;
  s.duration = duration;
  return s;
}

Source mode_source(const char* name, double x, double y, int direction, double half = 1.2 * um) {
  Source q;
  q.name = name, q.kind = SourceKind::mode, q.direction = direction;
  q.x1 = q.x2 = round_pico(x), q.y1 = round_pico(y - half), q.y2 = round_pico(y + half);
  return q;
}

Monitor vertical(const char* name, MonitorKind kind, double x, double y, double half = 1.2 * um) {
  return {name, kind, round_pico(x), round_pico(y - half), round_pico(x), round_pico(y + half)};
}

Shape strip(const char* name, double x0, double y0, double x1, double y1, double width = 0.30 * um) {
  Shape sh;
  sh.name = name, sh.index = index_core;
  sh.x = round_pico(0.5 * (x0 + x1)), sh.y = round_pico(0.5 * (y0 + y1));
  sh.width = round_pico(std::max(std::abs(x1 - x0), width)),
  sh.height = round_pico(std::max(std::abs(y1 - y0), width));
  return sh;
}

// Mach-Zehnder: Y-Verzweigung, zwei Arme, Y-Kombinierer. Das verbreiterte Stück im oberen Arm (zunächst
// deaktiviert, Taste H) verschiebt dort die Phase um etwa pi: der Ausgang wird dunkel.
FieldScene mzi() {
  FieldScene s = base("Mach-Zehnder-Interferometer", 30 * um, 8 * um, 25e-9, Polarization::hz, 600e-15);
  const double yc = 4 * um, arm = 2 * 6 * um * std::sin(25.0 * constants::pi / 180.0) + 1 * um;
  const double dy = 2 * 6 * um * (1 - std::cos(25.0 * constants::pi / 180.0));
  const double x_split = 4 * um, x_join = 26 * um;
  add_building_block(s, "y_splitter", x_split, yc);
  add_building_block(s, "y_combiner", x_join, yc);
  s.shapes.push_back(strip("Zuleitung", 0.0, yc, x_split - 2 * um, yc));
  s.shapes.push_back(strip("Ausleitung", x_join + 2 * um, yc, s.width, yc));
  for (int side : {-1, 1})
    s.shapes.push_back(strip(side < 0 ? "Arm oben" : "Arm unten", x_split + arm, yc + side * dy, x_join - arm,
                             yc + side * dy));
  Shape shifter = strip("Phasenschieber (H: an/aus)", 15 * um - 0.875 * um, yc - dy, 15 * um + 0.875 * um,
                        yc - dy, 0.45 * um);
  shifter.height = 0.45 * um, shifter.enabled = false;
  s.shapes.push_back(shifter);
  s.sources = {mode_source("Laser", 1 * um, yc, 1)};
  s.monitors = {vertical("Mode Ausgang", MonitorKind::mode, 28.5 * um, yc),
                vertical("Fluss Ausgang", MonitorKind::flux, 29 * um, yc)};
  return s;
}

// Add-Drop-Ringfilter: Ring zwischen zwei Bussen; ein kurzer Puls deckt viele Wellenlängen ab, im Spektrum
// des Drop-Monitors erscheinen die Resonanzen (Abstand FSR = lambda^2 / (n_g L)).
FieldScene add_drop() {
  FieldScene s = base("Add-Drop-Ringfilter", 14 * um, 11 * um, 25e-9, Polarization::hz, 2.0e-12);
  const double w = 0.30 * um, gap = 0.15 * um, r = 3 * um, xc = 7 * um, yc = 5.5 * um;
  const double y_top = yc - r - 0.5 * w - gap - 0.5 * w, y_bottom = yc + r + 0.5 * w + gap + 0.5 * w;
  s.shapes.push_back(strip("Bus oben", 0.0, y_top, s.width, y_top));
  s.shapes.push_back(strip("Bus unten", 0.0, y_bottom, s.width, y_bottom));
  Shape ring;
  ring.name = "Ring", ring.kind = ShapeKind::ring, ring.x = xc, ring.y = yc, ring.index = index_core;
  ring.radius = r + 0.5 * w, ring.inner_radius = r - 0.5 * w;
  s.shapes.push_back(ring);
  Source pulse = mode_source("Puls", 1 * um, y_top, 1, 0.8 * um);
  pulse.signal = SignalKind::pulse, pulse.pulse_duration = 10e-15, pulse.direction = 0;
  s.sources = {pulse};
  s.monitors = {vertical("Durchgang", MonitorKind::point, 13 * um, y_top, 0.0),
                vertical("Drop", MonitorKind::point, 1 * um, y_bottom, 0.0)};
  for (auto& m : s.monitors) m.x2 = m.x1, m.y1 = m.y2 = round_pico(m.y1);
  return s;
}

// Kreuzung zweier Wellenleiter: wie viel läuft geradeaus weiter, wie viel spricht in den Quer-Wellenleiter
// über?
FieldScene crossing() {
  FieldScene s = base("Wellenleiterkreuzung", 12 * um, 12 * um, 25e-9, Polarization::hz, 400e-15);
  s.shapes = {strip("waagerecht", 0.0, 6 * um, 12 * um, 6 * um),
              strip("senkrecht", 6 * um, 0.0, 6 * um, 12 * um)};
  s.sources = {mode_source("Laser", 1.5 * um, 6 * um, 1)};
  s.monitors = {vertical("geradeaus", MonitorKind::flux, 10.5 * um, 6 * um),
                {"quer", MonitorKind::flux, 4.8 * um, 1.5 * um, 7.2 * um, 1.5 * um}};
  return s;
}

// MMI-Teiler 1x2: im 3 um breiten Stück bilden sich Abbilder des Eingangs (Selbstabbildung); in der Länge
// 3/8 L_pi mit L_pi = 4 n W^2 / (3 lambda) entstehen zwei Abbilder bei +-W/4.
FieldScene mmi() {
  const double width = 3 * um,
               length = round_pico(3.0 / 8.0 * 4.0 * index_core * width * width / (3.0 * 1550e-9));
  FieldScene s =
      base("MMI-Teiler 1x2", round_pico(length + 10 * um), 7 * um, 25e-9, Polarization::hz, 500e-15);
  const double yc = 3.5 * um, x0 = 4 * um, x1 = x0 + length;
  s.shapes = {strip("Eingang", 0.0, yc, x0, yc),
              strip("MMI", x0, yc - 0.5 * width, x1, yc + 0.5 * width, width),
              strip("Ausgang oben", x1, yc - 0.25 * width, s.width, yc - 0.25 * width),
              strip("Ausgang unten", x1, yc + 0.25 * width, s.width, yc + 0.25 * width)};
  s.shapes[1].height = width;
  s.sources = {mode_source("Laser", 1.5 * um, yc, 1)};
  s.monitors = {vertical("oben", MonitorKind::flux, s.width - 2 * um, yc - 0.25 * width, 0.6 * um),
                vertical("unten", MonitorKind::flux, s.width - 2 * um, yc + 0.25 * width, 0.6 * um),
                vertical("Eingang", MonitorKind::flux, 3 * um, yc)};
  return s;
}

// Totalreflexion: ein breiter Gauß-Strahl (Taille 2,5 um, Aufweitung etwa 8 Grad) im Glas trifft unter 60
// Grad auf die Grenze zu Luft; der Grenzwinkel ist arcsin(1/n) = 43,8 Grad, also wird praktisch alles
// reflektiert (oben bleibt nur die abklingende Welle).
FieldScene total_reflection() {
  FieldScene s = base("Totalreflexion an Glas/Luft", 28 * um, 16 * um, 40e-9, Polarization::ez, 250e-15, 1.0);
  s.shapes = {strip("Glas", 0.0, 5 * um, 28 * um, 16 * um, 11 * um)};
  s.shapes[0].index = index_cladding;
  const double a = 60.0 * constants::pi / 180.0, cx = 6 * um, cy = 10.5 * um, half = 5 * um;
  Source beam;
  beam.name = "Strahl", beam.kind = SourceKind::line, beam.beam_width = 2.5 * um;
  beam.x1 = round_pico(cx - half * std::cos(a)), beam.y1 = round_pico(cy - half * std::sin(a));
  beam.x2 = round_pico(cx + half * std::cos(a)), beam.y2 = round_pico(cy + half * std::sin(a));
  s.sources = {beam};
  s.monitors = {{"Luft (durchgelassen)", MonitorKind::flux, 6 * um, 3 * um, 26 * um, 3 * um},
                {"Glas (reflektiert)", MonitorKind::flux, 17 * um, 8 * um, 27 * um, 8 * um}};
  return s;
}

// Gauß-Strahl im freien Raum: Taille 0,8 um, Divergenz lambda / (pi w0); die Punktmonitore zeigen, wie die
// Amplitude auf der Achse mit dem Abstand sinkt.
FieldScene gaussian_beam() {
  FieldScene s = base("Gauß-Strahl im freien Raum", 20 * um, 12 * um, 40e-9, Polarization::ez, 150e-15, 1.0);
  Source beam;
  beam.name = "Strahl", beam.kind = SourceKind::line, beam.beam_width = 0.8 * um;
  beam.x1 = beam.x2 = 2 * um, beam.y1 = 3 * um, beam.y2 = 9 * um;
  s.sources = {beam};
  s.monitors = {{"nah", MonitorKind::point, 4 * um, 6 * um, 0, 0},
                {"mittel", MonitorKind::point, 10 * um, 6 * um, 0, 0},
                {"fern", MonitorKind::point, 17 * um, 6 * um, 0, 0}};
  return s;
}

// XOR bzw. ODER/UND aus einem Y-Kombinierer: zwei einseitige Quellen auf den Eingangsarmen, gegenphasig
// (XOR: A = B = 1 löscht aus) bzw. gleichphasig (eine: Pegel 1/4, beide: Pegel 1; ODER mit niedriger, UND mit
// hoher Schwelle).
FieldScene strip_gate(bool antiphase) {
  FieldScene s =
      base(antiphase ? "XOR (Y-Kombinierer, gegenphasig)" : "ODER / UND (Y-Kombinierer, gleichphasig)",
           16 * um, 7 * um, 25e-9, Polarization::hz, 600e-15);
  const double yc = 3.5 * um, dy = 2 * 6 * um * (1 - std::cos(25.0 * constants::pi / 180.0));
  const double x_in = 2 * um, arm = 2 * 6 * um * std::sin(25.0 * constants::pi / 180.0) + 1 * um;
  add_building_block(s, "y_combiner", round_pico(x_in + arm), yc);
  for (int side : {-1, 1})
    s.shapes.push_back(
        strip(side < 0 ? "Zuleitung A" : "Zuleitung B", 0.0, yc + side * dy, x_in, yc + side * dy));
  s.shapes.push_back(strip("Ausleitung", x_in + arm + 2 * um, yc, s.width, yc));
  Source a = mode_source("A", 1 * um, yc - dy, 1, 0.9 * um),
         b = mode_source("B", 1 * um, yc + dy, 1, 0.9 * um);
  b.phase = antiphase ? constants::pi : 0.0;
  s.sources = {a, b};
  s.monitors = {vertical("Mode Ausgang", MonitorKind::mode, 14 * um, yc)};
  return s;
}

// NICHT: Licht am Detektor schaltet den invertierten Regenerator aus; ohne Licht sendet er.
FieldScene not_gate() {
  FieldScene s = base("NICHT (invertierter Regenerator)", 16 * um, 5 * um, 25e-9, Polarization::hz, 500e-15);
  // Eingang und Ausgang versetzt: sonst strahlt das Ende des Eingangs direkt in den Ausgang
  s.shapes = {strip("Eingang", 0.0, 1.5 * um, 6 * um, 1.5 * um),
              strip("Ausgang", 10 * um, 3.5 * um, 16 * um, 3.5 * um)};
  Source a = mode_source("A", 1 * um, 1.5 * um, 1, 1.0 * um);
  Source out = mode_source("NICHT A", 11 * um, 3.5 * um, 1, 1.0 * um);
  out.trigger = "Detektor", out.trigger_threshold = 1e-8, out.trigger_invert = true;
  s.sources = {a, out};
  s.monitors = {vertical("Detektor", MonitorKind::mode, 5 * um, 1.5 * um, 1.0 * um),
                vertical("Mode Ausgang", MonitorKind::mode, 14.5 * um, 3.5 * um, 1.0 * um)};
  return s;
}

// Kristall-Resonator: ein fehlendes Loch im Dreiecksgitter (Punktdefekt) hält Licht in der Bandlücke fest.
// Ein kurzer Puls regt ihn an; im Spektrum des Monitors bleibt nach dem Abklingen die Resonanz übrig.
FieldScene phc_cavity() {
  const double a = phc_a;
  FieldScene s = base("Photonischer Kristall: Resonator (Punktdefekt)", round_pico(16 * a),
                      round_pico(14 * a), round_pico(a / 20.0), Polarization::hz, 2.0e-12);
  CrystalLayout layout;
  layout.a = a, layout.radius = phc_radius;
  layout.origin_x = round_pico(0.5 * s.width), layout.origin_y = round_pico(0.5 * s.height);
  layout.x_min = 0.0, layout.x_max = s.width, layout.y_min = 0.0, layout.y_max = s.height;
  remove_site(layout, {0, 0});
  s.shapes.push_back({"Platte", ShapeKind::rectangle, round_pico(0.5 * s.width), round_pico(0.5 * s.height),
                      s.width, s.height, 0.0, 1e-6, 0.5e-6, index_core});
  add_crystal(s, layout, index_cladding);
  Source pulse;
  pulse.name = "Puls", pulse.kind = SourceKind::point, pulse.signal = SignalKind::pulse,
  pulse.pulse_duration = 8e-15;
  pulse.wavelength = phc_wavelength;
  pulse.x1 = pulse.x2 = round_pico(layout.origin_x + 0.1 * a),
  pulse.y1 = pulse.y2 = round_pico(layout.origin_y + 0.05 * a);
  s.sources = {pulse};
  s.monitors = {
      {"im Resonator", MonitorKind::point, round_pico(layout.origin_x - 0.1 * a), layout.origin_y, 0, 0}};
  return s;
}

// Stabkristall: quadratisches Gitter aus Si-Stäben (r = 0,2 a) in Luft, Ez; Bandlücke etwa a/lambda = 0,30
// ... 0,44 (Joannopoulos u. a., Photonic Crystals, Kap. 5). Eine fehlende Reihe führt Licht bei a/lambda =
// 0,38.
FieldScene phc_rods() {
  const double a = round_pico(0.38 * 1550e-9);
  FieldScene s = base("Photonischer Kristall: Stäbe in Luft, Wellenleiter (Ez)", round_pico(24 * a),
                      round_pico(13 * a), round_pico(a / 20.0), Polarization::ez, 1.5e-12, 1.0);
  CrystalLayout layout;
  layout.kind = LatticeKind::square, layout.a = a, layout.radius = round_pico(0.2 * a);
  layout.origin_x = round_pico(0.5 * a), layout.origin_y = round_pico(0.5 * s.height);
  layout.x_min = 0.0, layout.x_max = s.width, layout.y_min = 0.0, layout.y_max = s.height;
  remove_line(layout, {-2, 0}, 0, 30);
  add_crystal(s, layout, materials::get("Si").index(1550e-9), "Kristall (Stäbe)", "stab");
  Source source;
  source.name = "Quelle", source.kind = SourceKind::point;
  source.x1 = source.x2 = round_pico(2 * a), source.y1 = source.y2 = layout.origin_y;
  s.sources = {source};
  s.monitors = {{"Fluss Ende", MonitorKind::flux, round_pico(21 * a), round_pico(layout.origin_y - 1.5 * a),
                 round_pico(21 * a), round_pico(layout.origin_y + 1.5 * a)}};
  return s;
}

// Kristall als Spiegel: eine ebene Welle (Puls) trifft auf ein Dreiecksgitter; in der Bandlücke (a/lambda um
// 0,28) wird sie fast ganz reflektiert, darüber und darunter läuft sie teils hindurch (Spektrum der
// Monitore).
FieldScene phc_mirror() {
  const double a = phc_a;
  FieldScene s = base("Photonischer Kristall als Spiegel", round_pico(30 * a), round_pico(8 * a),
                      round_pico(a / 20.0), Polarization::hz, 1.2e-12);
  s.boundary_y = Boundary::periodic;
  s.height = round_pico(8 * a * std::sqrt(3.0) / 2.0 * 2.0 / 2.0);  // ganzzahlig viele Gitterzeilen
  CrystalLayout layout;
  layout.a = a, layout.radius = phc_radius;
  layout.origin_x = round_pico(12 * a), layout.origin_y = 0.0;
  layout.x_min = round_pico(12 * a), layout.x_max = round_pico(22 * a), layout.y_min = -s.height,
  layout.y_max = 2 * s.height;
  s.shapes.push_back({"Platte", ShapeKind::rectangle, round_pico(0.5 * s.width), round_pico(0.5 * s.height),
                      s.width, s.height, 0.0, 1e-6, 0.5e-6, index_core});
  add_crystal(s, layout, index_cladding);
  Source wave;
  wave.name = "ebene Welle", wave.kind = SourceKind::line, wave.signal = SignalKind::pulse,
  wave.pulse_duration = 8e-15;
  wave.wavelength = phc_wavelength;
  wave.x1 = wave.x2 = round_pico(4 * a), wave.y1 = 0.0, wave.y2 = s.height;
  s.sources = {wave};
  s.monitors = {
      {"vor dem Kristall", MonitorKind::point, round_pico(8 * a), round_pico(0.5 * s.height), 0, 0},
      {"hinter dem Kristall", MonitorKind::point, round_pico(26 * a), round_pico(0.5 * s.height), 0, 0}};
  return s;
}

}  // namespace

namespace {
// Beispiele nach Gruppen; die Reihenfolge ist die des Menüs
const std::vector<std::pair<std::string, std::string>>& catalog() {
  static const std::vector<std::pair<std::string, std::string>> list = {
      {"field_waveguide", "Grundlagen"},
      {"field_interface", "Grundlagen"},
      {"field_interference", "Grundlagen"},
      {"field_gaussian_beam", "Grundlagen"},
      {"field_total_reflection", "Grundlagen"},
      {"field_lens", "Grundlagen"},
      {"field_gap", "Grundlagen"},
      {"field_gap_pair", "Grundlagen"},
      {"field_taper", "Grundlagen"},
      {"field_bend", "Grundlagen"},
      {"field_crossing", "Grundlagen"},
      {"field_coupler", "Grundlagen"},
      {"field_mmi", "Grundlagen"},
      {"field_mzi", "Grundlagen"},
      {"field_ring", "Grundlagen"},
      {"field_add_drop", "Grundlagen"},
      {"field_bragg", "Grundlagen"},
      {"field_xor", "Logische Bauteile"},
      {"field_or_and", "Logische Bauteile"},
      {"field_not", "Logische Bauteile"},
      {"field_halfadder", "Logische Bauteile"},
      {"field_fulladder", "Logische Bauteile"},
      {"field_phc_w1", "Photonische Kristalle"},
      {"field_phc_bend", "Photonische Kristalle"},
      {"field_phc_mirror", "Photonische Kristalle"},
      {"field_phc_cavity", "Photonische Kristalle"},
      {"field_phc_rods", "Photonische Kristalle"},
      {"field_phc_y", "Photonische Kristalle"},
      {"field_phc_y_xor", "Photonische Kristalle"},
      {"field_phc_halfadder", "Photonische Kristalle"}};
  return list;
}
}  // namespace

std::vector<std::string> reference_scene_names() {
  std::vector<std::string> names;
  for (const auto& [name, group] : catalog()) names.push_back(name);
  return names;
}

std::string reference_scene_category(std::string_view name) {
  for (const auto& [n, group] : catalog())
    if (n == name) return group;
  return "Grundlagen";
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
  if (name == "field_taper") return taper();
  if (name == "field_bend") return bend();
  if (name == "field_bragg") return bragg();
  if (name == "field_lens") return lens();
  if (name == "field_mzi") return mzi();
  if (name == "field_add_drop") return add_drop();
  if (name == "field_crossing") return crossing();
  if (name == "field_mmi") return mmi();
  if (name == "field_total_reflection") return total_reflection();
  if (name == "field_gaussian_beam") return gaussian_beam();
  if (name == "field_xor") return strip_gate(true);
  if (name == "field_or_and") return strip_gate(false);
  if (name == "field_not") return not_gate();
  if (name == "field_phc_cavity") return phc_cavity();
  if (name == "field_phc_rods") return phc_rods();
  if (name == "field_phc_mirror") return phc_mirror();
  if (name == "field_halfadder") return halfadder(1.75 * um);
  if (name == "field_fulladder") return fulladder_scene();
  if (name == "field_phc_halfadder") return phc_halfadder(1, 0.25, 1.3, phc_wavelength);
  throw FieldError("Unbekannte Referenz-Szene '" + std::string(name) + "'");
}

}  // namespace photonlab::field
