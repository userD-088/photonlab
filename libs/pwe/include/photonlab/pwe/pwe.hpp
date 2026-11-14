#pragma once
// Bandstruktur photonischer Kristalle in 2D durch Entwicklung in ebenen Wellen (PWE, Plane-Wave Expansion).
// Alle Längen in Einheiten der Gitterkonstante a, Wellenvektoren in 2*pi/a, Frequenzen als a/lambda =
// omega*a/(2*pi*c). Damit gilt die Rechnung für jede Gitterkonstante; die physikalische Wellenlänge folgt
// aus lambda = a / Frequenz (siehe docs/physics/pwe.md).
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"

namespace photonlab::pwe {

struct PweError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Polarisation nach der Feldkomponente senkrecht zur Zeichenebene (wie in der Feld-Ebene).
enum class Polarization {
  ez,  // E senkrecht zur Ebene ("TM" bei Joannopoulos): -Laplace(E) = (omega/c)^2 * eps * E
  hz,  // H senkrecht zur Ebene ("TE" bei Joannopoulos): div(1/eps grad H) = -(omega/c)^2 * H
};

struct Circle {
  double x = 0.0, y = 0.0;  // Mittelpunkt [a]
  double radius = 0.3;      // [a]
  double epsilon = 1.0;     // Permittivität innerhalb
  bool operator==(const Circle&) const = default;
};

struct Rectangle {          // achsenparallel
  double x = 0.0, y = 0.0;  // Mittelpunkt [a]
  double width = 0.5, height = 1.0;
  double epsilon = 1.0;
  bool operator==(const Rectangle&) const = default;
};

// Elementarzelle (oder Superzelle). Formen dürfen sich nicht überlappen: ihre Beiträge zur Permittivität
// werden als Abweichung vom Hintergrund addiert.
struct UnitCell {
  double a1x = 1.0, a1y = 0.0;
  double a2x = 0.0, a2y = 1.0;
  double epsilon_background = 1.0;
  std::vector<Circle> circles;
  std::vector<Rectangle> rectangles;
};

// Elementarzellen mit einem Loch bzw. Stab im Zentrum. radius in [a].
UnitCell triangular_cell(double radius, double epsilon_background, double epsilon_inclusion);
UnitCell square_cell(double radius, double epsilon_background, double epsilon_inclusion);

struct KPoint {
  double kx = 0.0, ky = 0.0;  // [2*pi/a]
};

struct BandPath {
  std::vector<KPoint> points;
  std::vector<double> distance;                    // Weglänge entlang des Pfades [2*pi/a]
  std::vector<std::pair<int, std::string>> marks;  // Index des Punktes, Name ("Gamma", "M", ...)
};

enum class PathKind { triangular, square };  // Gamma-M-K-Gamma bzw. Gamma-X-M-Gamma
// Pfad am Rand der irreduziblen Brillouin-Zone der Elementarzelle; segment_points = Punkte je Teilstück.
BandPath standard_path(const UnitCell& cell, PathKind kind, int segment_points = 12);

struct BandSettings {
  Polarization polarization = Polarization::ez;
  double cutoff = 4.0;  // größte ebene Welle |G| in 2*pi/a (Zahl der Wellen wächst mit cutoff^2)
  int bands = 6;
  bool operator==(const BandSettings&) const = default;
};

struct Gap {
  int band = 0;        // Lücke zwischen Band `band` und `band + 1` (1-basiert)
  double lower = 0.0;  // Oberkante des tieferen Bandes [a/lambda]
  double upper = 0.0;  // Unterkante des höheren Bandes [a/lambda]
  double midgap() const { return 0.5 * (lower + upper); }
  double ratio() const { return (upper - lower) / midgap(); }  // relative Lückenbreite
};

struct BandResult {
  BandPath path;
  std::vector<std::vector<double>> frequency;  // [k-Punkt][Band] in a/lambda, aufsteigend
  std::vector<Gap> gaps;                       // vollständige Lücken entlang des Pfades
  int plane_waves = 0;
};

// Prüft die Eingaben; wirft PweError mit Klartext.
void validate(const UnitCell& cell, const BandSettings& settings);

// Frequenzen an den Punkten des Pfades. pool/control optional (Fortschritt, Abbruch; bei Abbruch ist
// das Ergebnis leer).
BandResult solve_bands(const UnitCell& cell, const BandSettings& settings, const BandPath& path,
                       ThreadPool* pool = nullptr, JobControl* control = nullptr);

// Frequenzen an einem einzelnen Wellenvektor (aufsteigend, bands Stück).
std::vector<double> solve_k(const UnitCell& cell, const BandSettings& settings, KPoint k);

// Lücken zwischen aufeinanderfolgenden Bändern über alle Punkte; nur Lücken größerer relativer Breite
// als min_ratio.
std::vector<Gap> find_gaps(const std::vector<std::vector<double>>& frequency, double min_ratio = 1e-3);

}  // namespace photonlab::pwe
