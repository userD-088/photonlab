#include "photonlab/field/reference_scenes.hpp"

#include "photonlab/core/units.hpp"

namespace photonlab::field {
namespace {

using units::um;
const double index_cladding = 1.444;  // SiO2, siehe material_presets()
const double index_core = 2.85;       // Si-Schicht 220 nm als effektiver Index, siehe material_presets()

// Design 1: gerader Wellenleiter. Ein Gauß-Strahl trifft auf den Kern; was nicht zur Mode passt, strahlt
// in den Mantel ab und verschwindet in der Randschicht.
FieldScene waveguide() {
  FieldScene s;
  s.name = "Gerader Wellenleiter";
  s.width = 14 * um, s.height = 5 * um;
  s.cell_size = 25e-9;
  s.background_index = index_cladding;
  s.polarization = Polarization::hz;  // E in der Ebene: TE-artig für die Draufsicht auf einen Chip
  s.duration = 300e-15;
  s.shapes = {{"Kern", ShapeKind::rectangle, 7 * um, 2.5 * um, 14 * um, 0.45 * um, 0.0, 1e-6, 0.5e-6, index_core}};
  Source source;
  source.name = "Einkopplung";
  source.x1 = source.x2 = 1.5 * um;
  source.y1 = 1.5 * um, source.y2 = 3.5 * um;
  source.beam_width = 0.3 * um;
  s.sources = {source};
  s.monitors = {{"Fluss vorn", MonitorKind::flux, 3 * um, 1.2 * um, 3 * um, 3.8 * um},
                {"Fluss hinten", MonitorKind::flux, 12 * um, 1.2 * um, 12 * um, 3.8 * um},
                {"Feld im Kern", MonitorKind::point, 12 * um, 2.5 * um, 0, 0}};
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
  s.shapes = {{"Bus", ShapeKind::rectangle, 6 * um, 1.65 * um, 12 * um, 0.45 * um, 0.0, 1e-6, 0.5e-6, index_core},
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

}  // namespace

std::vector<std::string> reference_scene_names() {
  return {"field_waveguide", "field_interface", "field_interference", "field_ring"};
}

FieldScene reference_scene(std::string_view name) {
  if (name == "field_waveguide") return waveguide();
  if (name == "field_interface") return interface();
  if (name == "field_interference") return interference();
  if (name == "field_ring") return ring();
  throw FieldError("Unbekannte Referenz-Szene '" + std::string(name) + "'");
}

}  // namespace photonlab::field
