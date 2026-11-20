// Phase 13: Maskenlayout und GDSII
#include <doctest/doctest.h>

#include <cmath>

#include "photonlab/core/units.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "photonlab/io/project.hpp"
#include "photonlab/layout/circuit_layout.hpp"
#include "photonlab/layout/gds.hpp"
#include "photonlab/layout/layout.hpp"

using namespace photonlab;
using namespace photonlab::layout;

TEST_CASE("GDSII: 8-Byte-Gleitkommazahlen wie in der Formatbeschreibung") {
  // Beispiele aus dem GDSII Stream Format Manual und den UNITS-Sätzen üblicher Dateien (1 nm, 1 um)
  CHECK(encode_real8(1.0) == 0x4110000000000000ull);
  CHECK(encode_real8(0.5) == 0x4080000000000000ull);
  CHECK(encode_real8(-2.0) == 0xC120000000000000ull);
  CHECK(encode_real8(0.0) == 0ull);
  // Übliche Dateien schreiben für 1e-3 und 1e-9 die Bytes 3E4189374BC6A7EF und 3944B82FA09B5A51. Die 56-Bit-
  // Mantisse kann einen double exakt halten; diese Werkzeuge runden ungenauer. Gleicher Exponent, Mantisse
  // höchstens 4 Einheiten daneben (relativ 1e-16).
  CHECK((encode_real8(1e-3) >> 56) == 0x3Eull);
  CHECK((encode_real8(1e-9) >> 56) == 0x39ull);
  CHECK((encode_real8(1e-3) - 0x3E4189374BC6A7EFull) <= 4ull);
  CHECK((encode_real8(1e-9) - 0x3944B82FA09B5A51ull) <= 4ull);
  for (double v : {1e-9, 1e-3, 0.1, 3.14159, -42.5, 1e20, 1.0 / 3.0})
    CHECK(decode_real8(encode_real8(v)) == v);  // exakt
  CHECK(decode_real8(0x3944B82FA09B5A51ull) == doctest::Approx(1e-9).epsilon(1e-15).scale(0.0));
}

TEST_CASE("GDSII: Datei beginnt mit HEADER 600, endet mit ENDLIB, Rücklesen ergibt dieselben Polygone") {
  field::FieldScene scene;
  scene.name = "Prüfszene 1";
  scene.width = 10e-6, scene.height = 8e-6;
  field::Shape rect;
  rect.x = 3e-6, rect.y = 2e-6, rect.width = 2e-6, rect.height = 0.5e-6, rect.rotation = 0.3;
  field::Shape circle;
  circle.kind = field::ShapeKind::circle, circle.x = 6e-6, circle.y = 5e-6, circle.radius = 1e-6,
  circle.index = 2.0;
  field::Shape ring;
  ring.kind = field::ShapeKind::ring, ring.x = 4e-6, ring.y = 5e-6, ring.radius = 1.5e-6,
  ring.inner_radius = 1.0e-6;
  scene.shapes = {rect, circle, ring};
  const Layout layout = scene_to_layout(scene);
  REQUIRE(layout.cells.size() == 1);
  CHECK(layout.cells[0].name == "Pr__fszene_1");  // nur ASCII-Buchstaben, Ziffern und _
  REQUIRE(layout.layers.size() == 2);
  CHECK(layout.layers[0].index == doctest::Approx(3.476));
  CHECK(layout.cells[0].polygons[0].layer == 1);
  CHECK(layout.cells[0].polygons[1].layer == 2);

  const auto bytes = write_gds(layout);
  REQUIRE(bytes.size() > 8);
  CHECK(bytes[0] == 0x00);
  CHECK(bytes[1] == 0x06);
  CHECK(bytes[2] == 0x00);
  CHECK(bytes[3] == 0x02);
  CHECK((bytes[4] << 8 | bytes[5]) == 600);
  CHECK(bytes[bytes.size() - 2] == 0x04);  // ENDLIB
  CHECK(bytes[bytes.size() - 1] == 0x00);
  CHECK(write_gds(layout) == bytes);  // feste Zeitangabe: gleiche Datei

  const Layout back = read_gds(bytes);
  CHECK(back.library == layout.library);
  CHECK(back.database_unit == doctest::Approx(1e-9).epsilon(1e-15).scale(0.0));
  CHECK(back.user_unit == doctest::Approx(1e-6).epsilon(1e-12).scale(0.0));
  REQUIRE(back.cells.size() == 1);
  CHECK(back.cells[0].name == layout.cells[0].name);
  CHECK(back.cells[0].polygons == layout.cells[0].polygons);

  std::vector<std::uint8_t> broken(bytes.begin(), bytes.end() - 4);
  CHECK_THROWS_AS(read_gds(broken), LayoutError);
}

TEST_CASE("Layout: Flächen der Formen (Rechteck exakt, Kreis und Ring innerhalb der Sehnenabweichung)") {
  field::FieldScene scene;
  scene.width = scene.height = 20e-6;
  field::Shape rect;
  rect.x = 5e-6, rect.y = 5e-6, rect.width = 3e-6, rect.height = 0.5e-6;
  field::Shape circle;
  circle.kind = field::ShapeKind::circle, circle.x = 10e-6, circle.y = 10e-6, circle.radius = 2e-6;
  field::Shape ring;
  ring.kind = field::ShapeKind::ring, ring.x = 14e-6, ring.y = 14e-6, ring.radius = 3e-6,
  ring.inner_radius = 2.5e-6;
  scene.shapes = {rect, circle, ring};
  LayoutSettings settings;
  settings.max_deviation = 1e-9;
  const Layout layout = scene_to_layout(scene, settings);
  const auto& p = layout.cells[0].polygons;
  const double nm2 = 1.0;  // Datenbank-Einheit 1 nm
  CHECK(signed_area(p[0]) == doctest::Approx(3000.0 * 500.0 * nm2).epsilon(1e-12).scale(0.0));
  // Sehnenpolygon: Fläche liegt um höchstens Umfang * Abweichung unter der Kreisfläche
  const double circle_area = constants::pi * 2000.0 * 2000.0;
  CHECK(signed_area(p[1]) < circle_area);
  CHECK(circle_area - signed_area(p[1]) < 2 * constants::pi * 2000.0 * 1.0 * 1.05);
  const double ring_area = constants::pi * (3000.0 * 3000.0 - 2500.0 * 2500.0);
  CHECK(signed_area(p[2]) ==
        doctest::Approx(ring_area).epsilon(2.0 * 2 * constants::pi * 3000.0 / ring_area).scale(0.0));
  for (const auto& polygon : p) CHECK(signed_area(polygon) > 0.0);  // gegen den Uhrzeigersinn
  // y gespiegelt: Rechteck um y = 5 um liegt in GDSII bei 20 - 5 = 15 um
  double y_sum = 0.0;
  for (const auto& point : p[0].points) y_sum += point.y;
  CHECK(y_sum / 4.0 == doctest::Approx(15000.0));
}

TEST_CASE("Layout: Pixelbild als Rechtecke, Fläche = Zahl der Pixel") {
  // L-Form 4 x 3: Zeilen 0 und 1 je Pixel 0..1, Zeile 2 Pixel 0..3
  const std::vector<double> pixels = {1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1};
  const auto polygons = pixels_to_polygons(pixels, 4, 3, 0.0, 0.0, 100e-9, 1e-6, 0.5, 7);
  double area = 0.0;
  for (const auto& p : polygons) {
    area += signed_area(p);
    CHECK(p.layer == 7);
  }
  CHECK(area == doctest::Approx(8 * 100.0 * 100.0));
  CHECK(polygons.size() == 2);  // zwei gleiche Läufe zusammengefasst + der breite Lauf
}

TEST_CASE("Layout: Kristall-Szene (W1) – je Loch ein Polygon, Löcher auf eigener Lage") {
  const field::FieldScene scene = field::reference_scene("field_phc_w1");
  const Layout layout = scene_to_layout(scene);
  CHECK(layout.cells[0].polygons.size() == scene.shapes.size());
  CHECK(layout.layers.size() >= 2);
  const auto back = read_gds(write_gds(layout));
  CHECK(back.cells[0].polygons.size() == scene.shapes.size());
}

TEST_CASE("Layout: Taper als Trapez (Fläche exakt), Bogen als Ringausschnitt") {
  field::FieldScene scene;
  scene.width = scene.height = 20e-6;
  field::Shape taper;
  taper.kind = field::ShapeKind::taper;
  taper.x = taper.y = 5e-6, taper.width = 4e-6, taper.height = 0.5e-6, taper.width2 = 1.5e-6;
  field::Shape arc;
  arc.kind = field::ShapeKind::arc;
  arc.x = arc.y = 10e-6, arc.radius = 3e-6, arc.inner_radius = 2.5e-6, arc.sweep = constants::pi / 2;
  scene.shapes = {taper, arc};
  LayoutSettings settings;
  settings.max_deviation = 1e-9;
  const Layout layout = scene_to_layout(scene, settings);
  const auto& p = layout.cells[0].polygons;
  CHECK(signed_area(p[0]) == doctest::Approx(4000.0 * 0.5 * (500.0 + 1500.0)).epsilon(1e-12).scale(0.0));
  const double quarter = 0.25 * constants::pi * (3000.0 * 3000.0 - 2500.0 * 2500.0);
  CHECK(signed_area(p[1]) == doctest::Approx(quarter).epsilon(0.01).scale(0.0));
}

TEST_CASE("Layout der Schaltung: Kopplungslänge, gerader Weg, 8-Bit-Addierer als GDSII") {
  const layout::CircuitLayoutSettings settings;
  // gekoppelte Leistung sin²(kappa L): 50 % bei L = pi/(4 kappa), alles bei pi/(2 kappa)
  CHECK(layout::coupler_length(0.5, settings) ==
        doctest::Approx(constants::pi / (4.0 * settings.coupling_per_length)).epsilon(1e-12));
  CHECK(layout::coupler_length(1.0, settings) ==
        doctest::Approx(constants::pi / (2.0 * settings.coupling_per_length)).epsilon(1e-12));

  // zwei Wellenleiter in einer Reihe, verbunden: drei gerade Stücke, Fläche = Länge * Breite
  circuit::Circuit line;
  line.add("a", "waveguide", {}, 0, 0);
  line.add("b", "waveguide", {}, 10, 0);
  line.connect("a.out", "b.in");
  const auto& registry = circuit::ComponentRegistry::builtin();
  layout::CircuitLayoutReport report;
  const layout::Layout straight = layout::circuit_to_layout(line, registry, {}, settings, &report);
  REQUIRE(straight.cells.size() == 1);
  double area_nm2 = 0.0;
  for (const auto& polygon : straight.cells[0].polygons)
    if (polygon.layer == layout::layers::waveguide) area_nm2 += layout::signed_area(polygon);
  const double pitch_um = settings.pitch / 1e-6;
  const double length_um = 5.0 * pitch_um + 5.0 * pitch_um + 5.0 * pitch_um;  // a, Verbindung, b
  CHECK(area_nm2 * 1e-6 == doctest::Approx(length_um * 0.5).epsilon(1e-3));
  CHECK(report.crossings == 0);

  // 8-Bit-Addierer: alle Heizer gezeichnet (33 Schalter mit je zwei), GDSII hin und zurück
  const io::Project adder = io::reference_project("adder8");
  const layout::Layout chip = layout::circuit_to_layout(adder.circuit, registry, {}, settings, &report);
  int heaters = 0, germanium = 0, grating = 0;
  for (const auto& polygon : chip.cells[0].polygons) {
    heaters += polygon.layer == layout::layers::heater;
    germanium += polygon.layer == layout::layers::germanium;
    grating += polygon.layer == layout::layers::grating;
  }
  CHECK(heaters == 2 * 33);
  CHECK(germanium == 9);  // acht Summen und der Übertrag
  CHECK(grating == 20);   // ein Gitterkoppler mit 20 Zähnen
  CHECK(report.width > 1e-3);  // gut 1,5 mm lang bei 25 µm Raster
  MESSAGE("8-Bit-Addierer: ", report.width * 1e3, " x ", report.height * 1e3, " mm, ", report.crossings,
          " Kreuzungen");
  const layout::Layout back = layout::read_gds(layout::write_gds(chip));
  REQUIRE(back.cells.size() == 1);
  CHECK(back.cells[0].polygons.size() == chip.cells[0].polygons.size());

  // Addierer aus Zellen: Kasten im Chip, Inneres als eigene Zelle
  const io::Project cells = io::reference_project("adder8_cells");
  const layout::Layout hierarchical =
      layout::circuit_to_layout(cells.circuit, *io::make_registry(cells), cells.subcircuits, settings, &report);
  REQUIRE(hierarchical.cells.size() == 2);
  CHECK(hierarchical.cells[1].name == "SUB_volladdierer");
}
