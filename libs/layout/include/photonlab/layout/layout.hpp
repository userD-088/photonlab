#pragma once
// Maskenlayout: Polygone auf Lagen, in ganzzahligen Datenbank-Einheiten (1 nm), y nach oben wie in
// Layout-Werkzeugen üblich. Aus einer Feld-Szene oder einem Pixelbild (inverses Design) erzeugt, als GDSII
// geschrieben (gds.hpp). Details und Grenzen: docs/physics/layout.md
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "photonlab/field/inverse_design.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::layout {

struct LayoutError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Point {
  std::int32_t x = 0, y = 0;  // [Datenbank-Einheiten]
  bool operator==(const Point&) const = default;
};

// Geschlossenes Polygon (ohne Wiederholung des ersten Punktes), einfach (ohne Selbstüberschneidung bis auf
// die Schnittkante eines Lochs).
struct Polygon {
  int layer = 1;
  int datatype = 0;
  std::vector<Point> points;
  bool operator==(const Polygon&) const = default;
};

struct LayerInfo {
  int layer = 1;
  double index = 1.0;  // Brechzahl der Formen auf dieser Lage
  std::string name;    // z. B. "n = 3.476"
};

struct Cell {
  std::string name;
  std::vector<Polygon> polygons;
};

struct Layout {
  std::string library = "PHOTONLAB";
  double database_unit = 1e-9;  // [m] je Datenbank-Einheit
  double user_unit = 1e-6;      // [m] je Benutzer-Einheit (Anzeige in Layout-Werkzeugen: um)
  std::vector<Cell> cells;
  std::vector<LayerInfo> layers;
};

struct LayoutSettings {
  double max_deviation = 1e-9;  // größte Abweichung eines Kreisbogens von der Sehne [m]
  int min_segments = 16;        // Kreise mindestens so viele Ecken
  std::string cell_name;        // leer: Name der Szene (bereinigt)
};

// Polygone der Formen einer Feld-Szene. Jede Brechzahl bekommt eine eigene Lage (größte Brechzahl = Lage 1).
// Die Reihenfolge der Formen (spätere überdecken frühere) geht in GDSII verloren; Formen mit der Brechzahl
// des Hintergrunds (Löcher) landen auf einer eigenen Lage. y wird gespiegelt (Szene: y nach unten).
Layout scene_to_layout(const field::FieldScene& scene, const LayoutSettings& settings = {});

// Pixelbild (nx * ny Werte, Zeile für Zeile, Zelle (i, j) = [x0 + i*dx, x0+(i+1)*dx] x [y0 + j*dy, ...] in
// Szenen-Koordinaten) als Polygone: Zellen mit Wert >= threshold, je Zeile zusammenhängende Läufe als
// Rechteck.
std::vector<Polygon> pixels_to_polygons(const std::vector<double>& values, int nx, int ny, double x0,
                                        double y0, double dx, double scene_height, double threshold,
                                        int layer, double database_unit = 1e-9);

// Layout eines inversen Designs: Wellenleiter der Szene plus binäres Designgebiet (Lage des Kerns, Pixel als
// Rechtecke). Zelle "INVERSE_DESIGN".
Layout inverse_design_layout(const field::InverseDesignResult& result);

// Fläche eines Polygons [Datenbank-Einheiten²], positiv gegen den Uhrzeigersinn (Gaußsche Trapezformel).
double signed_area(const Polygon& polygon);

}  // namespace photonlab::layout
