#pragma once
// Szene der Feld-Ebene: Rechengebiet, Geometrie, Quellen und Monitore einer 2D-FDTD-Rechnung.
// Reine Daten in SI; die Physik steht in fdtd.hpp. Koordinaten: x nach rechts, y nach unten (wie auf dem
// Bildschirm), Ursprung in der linken oberen Ecke des Rechengebiets; z = x × y zeigt in die Zeichenebene.
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace photonlab::field {

struct FieldError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Welche Feldkomponenten gerechnet werden. In 2D zerfallen die Maxwell-Gleichungen in zwei unabhängige Sätze.
enum class Polarization {
  ez,  // E senkrecht zur Zeichenebene: Ez, Hx, Hy (oft "TM" genannt; auf einem Chip von oben gesehen
       // TM-artig)
  hz,  // E in der Zeichenebene: Hz, Ex, Ey (oft "TE" genannt; auf einem Chip von oben gesehen TE-artig)
};

enum class Boundary {
  pml,       // absorbierende Randschicht (CPML), dahinter ideal leitende Wand
  periodic,  // was rechts hinausläuft, kommt links wieder herein
};

enum class ShapeKind { rectangle, circle, ring };

// Formen überdecken einander in der Reihenfolge der Liste: spätere liegen oben.
struct Shape {
  std::string name;
  ShapeKind kind = ShapeKind::rectangle;
  double x = 0.0, y = 0.0;       // Mittelpunkt [m]
  double width = 1e-6;           // Rechteck: Ausdehnung in x [m]
  double height = 1e-6;          // Rechteck: Ausdehnung in y [m]
  double rotation = 0.0;         // Rechteck: Drehung um den Mittelpunkt [rad]
  double radius = 1e-6;          // Kreis und Ring: Außenradius [m]
  double inner_radius = 0.5e-6;  // Ring: Innenradius [m]
  double index = 3.476;          // Brechzahl (reell, ohne Dispersion)
  bool operator==(const Shape&) const = default;
};

enum class SourceKind {
  point,  // Stromfaden an einem Punkt
  line,   // Stromschicht entlang einer Linie, gleichmäßig oder mit Gauß-Profil
  mode,   // Stromschicht mit dem Profil einer geführten Mode des Brechzahlprofils unter der Linie
};
enum class SignalKind { continuous, pulse };

struct Source {
  std::string name;
  SourceKind kind = SourceKind::line;
  double x1 = 0.0, y1 = 0.0;  // Punkt bzw. Anfang der Linie [m]
  double x2 = 0.0, y2 = 0.0;  // Ende der Linie [m]
  double beam_width = 0.0;  // Linie: 1/e-Halbbreite eines Gauß-Profils der Amplitude [m]; 0 = gleichmäßig
  SignalKind signal = SignalKind::continuous;
  double wavelength = 1550e-9;     // Vakuumwellenlänge des Trägers [m]
  double amplitude = 1.0;          // Amplitude der abgestrahlten ebenen Welle [V/m] (siehe fdtd.md)
  double phase = 0.0;              // Phase des Trägers [rad]
  double pulse_duration = 15e-15;  // Puls: 1/e-Halbbreite der Feld-Einhüllenden [s]
  double ramp_periods = 4.0;       // Dauerstrich: sanftes Einschalten über so viele Perioden
  int mode_order = 0;              // Modenquelle: Ordnung der Mode (0 = Grundmode); Linie achsenparallel
  bool operator==(const Source&) const = default;
};

enum class MonitorKind {
  point,  // Zeitverlauf des Feldes an einem Punkt
  flux,   // Leistungsfluss durch eine achsenparallele Linie (Poynting-Vektor)
  mode,   // Amplitude einer geführten Mode: Überlapp des Feldes mit dem Modenprofil unter der Linie
};

struct Monitor {
  std::string name;
  MonitorKind kind = MonitorKind::point;
  double x1 = 0.0, y1 = 0.0;  // Punkt bzw. Anfang der Linie [m]
  double x2 = 0.0, y2 = 0.0;  // Ende der Linie [m]; muss waagerecht oder senkrecht liegen
  int mode_order = 0;         // Modenmonitor: Ordnung der Mode
  double wavelength = 0.0;    // Modenmonitor: Wellenlänge des Profils [m]; 0 = die der ersten Quelle
  bool operator==(const Monitor&) const = default;
};

struct FieldScene {
  std::string name;
  double width = 0.0;             // Ausdehnung in x einschließlich Randschichten [m]; 0 = keine Szene
  double height = 0.0;            // Ausdehnung in y [m]
  double cell_size = 25e-9;       // Kantenlänge einer Gitterzelle dx = dy [m]
  double background_index = 1.0;  // Brechzahl des Hintergrunds
  Polarization polarization = Polarization::ez;
  Boundary boundary_x = Boundary::pml;
  Boundary boundary_y = Boundary::pml;
  int pml_cells = 12;     // Dicke der Randschicht in Zellen
  double courant = 0.99;  // Zeitschritt relativ zur Stabilitätsgrenze dx/(c*sqrt(2)), 0 < courant <= 1
  double duration = 0.0;  // Laufzeit für Batch-Rechnungen [s]; 0 = unbegrenzt (nur interaktiv)
  std::vector<Shape> shapes;
  std::vector<Source> sources;
  std::vector<Monitor> monitors;

  bool empty() const { return !(width > 0.0) || !(height > 0.0); }
  bool operator==(const FieldScene&) const = default;
};

// Brechzahl an einem Punkt (oberste Form gewinnt, sonst Hintergrund).
double index_at(const FieldScene& scene, double x, double y);
bool contains(const Shape& shape, double x, double y);

// Prüft die Szene; wirft FieldError mit Klartext (Gittergröße, Courant-Zahl, Lage der Monitore ...).
void validate(const FieldScene& scene);

// Gitterabmessungen, die der Löser aus der Szene ableitet.
int grid_nx(const FieldScene& scene);
int grid_ny(const FieldScene& scene);
double time_step(const FieldScene& scene);  // dt = courant * dx / (c0 * sqrt(2))

// Hinweise zur Qualität der Rechnung (Auflösung, Abstand zur Randschicht ...). Keine Fehler: die
// Rechnung läuft, das Ergebnis ist aber mit Vorsicht zu lesen.
struct Warning {
  std::string text;
  bool severe = false;  // true: Ergebnis voraussichtlich unbrauchbar
};
std::vector<Warning> check(const FieldScene& scene);

// Zellen pro Wellenlänge im optisch dichtesten Material (kürzeste Quellwellenlänge); 0 ohne Quellen.
double cells_per_wavelength(const FieldScene& scene);

// Brechzahl eines Bibliotheksmaterials bei der Wellenlänge der ersten Quelle (sonst 1550 nm), als
// Vorgabe für Formen. Die Rechnung selbst bleibt dispersionsfrei.
double material_index(const FieldScene& scene, std::string_view material);

// Effektiver Index der Grundmode einer Schicht (Kern zwischen zwei gleichen Mänteln) für die Draufsicht
// auf einen Chip (Effektiv-Index-Methode); passend zur Polarisation der Szene: hz -> TE, ez -> TM.
// Liefert die Brechzahl des Mantels, wenn die Schicht keine Mode führt.
double layer_effective_index(const FieldScene& scene, std::string_view core, std::string_view cladding,
                             double thickness);

std::string_view to_string(Polarization p);
std::string_view to_string(Boundary b);
std::string_view to_string(ShapeKind k);
std::string_view to_string(SourceKind k);
std::string_view to_string(SignalKind k);
std::string_view to_string(MonitorKind k);
Polarization polarization_from_string(std::string_view text);  // alle *_from_string werfen FieldError
Boundary boundary_from_string(std::string_view text);
ShapeKind shape_kind_from_string(std::string_view text);
SourceKind source_kind_from_string(std::string_view text);
SignalKind signal_kind_from_string(std::string_view text);
MonitorKind monitor_kind_from_string(std::string_view text);

}  // namespace photonlab::field
