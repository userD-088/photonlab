#pragma once
// Geometrie-Generatoren für photonische Kristalle (Bauplan 4.7): Dreiecks- und Quadratgitter von Löchern,
// Liniendefekte (gerade, 60°-Knick, Y-Verzweigung) und Punktdefekte. Das Ergebnis sind gewöhnliche Formen
// der Feld-Szene; der Generator selbst bleibt parametrisch (Gitterkonstante, Radius, Defektpfad), damit
// Sweeps und später inverses Design möglich sind. Koordinaten wie in der Szene (y nach unten).
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "photonlab/field/scene.hpp"

namespace photonlab::field {

enum class LatticeKind {
  triangular,  // Löcher auf einem Dreiecksgitter: a1 = (a, 0), a2 = (a/2, -a*sqrt(3)/2) (auf dem Bildschirm
               // nach oben)
  square,      // Quadratgitter: a1 = (a, 0), a2 = (0, -a)
};

struct Site {
  int i = 0, j = 0;  // Gitterindizes: Ort = Ursprung + i*a1 + j*a2
  bool operator==(const Site&) const = default;
  bool operator<(const Site& o) const { return i != o.i ? i < o.i : j < o.j; }
};

// Beschreibung eines Kristalls: Gitter, Lochradius, Gebiet und Defekte.
struct CrystalLayout {
  LatticeKind kind = LatticeKind::triangular;
  double a = 430e-9;                      // Gitterkonstante [m]
  double radius = 150e-9;                 // Lochradius [m]
  double origin_x = 0.0, origin_y = 0.0;  // Ort des Gitterpunktes (0, 0) [m]
  // Gebiet, in dem Löcher liegen dürfen: ein Loch gehört dazu, wenn es ganz hineinpasst [m]
  double x_min = 0.0, x_max = 0.0, y_min = 0.0, y_max = 0.0;
  std::vector<Site> removed;               // fehlende Löcher (Defekte)
  std::map<Site, double> radius_override;  // Löcher mit anderem Radius (z. B. Kavität, Taper) [m]
};

// Gitterrichtungen in Schritten von 60° (Dreieck, 0..5) bzw. 90° (Quadrat, 0..3), gegen den Uhrzeigersinn
// auf dem Bildschirm gezählt, 0 = +x. Liefert den Schritt (di, dj); wirft FieldError bei ungültiger Richtung.
std::pair<int, int> lattice_step(LatticeKind kind, int direction);

// Ort eines Gitterpunktes (auf Pikometer gerundet, damit Beispieldateien auf allen Compilern gleich sind).
std::pair<double, double> site_position(const CrystalLayout& layout, Site site);
// Gitterpunkt, der dem Ort am nächsten liegt.
Site nearest_site(const CrystalLayout& layout, double x, double y);

// Defekte: entfernt count Löcher ab start in Richtung direction (start eingeschlossen). Rückgabe: der letzte
// entfernte Punkt. Ein W1-Wellenleiter ist eine entfernte Reihe (Richtung 0).
Site remove_line(CrystalLayout& layout, Site start, int direction, int count);
void remove_site(CrystalLayout& layout, Site site);
void set_radius(CrystalLayout& layout, Site site, double radius);

// Weg aus Teilstücken (Richtung, Länge); die Teilstücke schließen sich an, der Knickpunkt gehört zum ersten.
struct PathLeg {
  int direction = 0;
  int count = 1;
};
// Entfernt Löcher längs des Weges ab start; Rückgabe: Endpunkt. Ein 60°-Knick: {{0, n}, {1, m}}.
Site remove_path(CrystalLayout& layout, Site start, const std::vector<PathLeg>& legs);

// Löcher, die im Gebiet liegen und nicht entfernt wurden (Mittelpunkte und Radien).
struct Hole {
  Site site;
  double x = 0.0, y = 0.0, radius = 0.0;
};
std::vector<Hole> holes(const CrystalLayout& layout);

// Löcher als Kreis-Formen (Namen "<prefix>_<i>_<j>") mit der Brechzahl hole_index.
std::vector<Shape> crystal_shapes(const CrystalLayout& layout, double hole_index,
                                  const std::string& prefix = "loch");

}  // namespace photonlab::field
