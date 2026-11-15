#pragma once
// Inverses Design mit der Adjoint-Methode im 2D-FDTD (Ez-Polarisation): Ein quadratisches Designgebiet
// zwischen einem waagerechten Eingangs- und einem senkrechten Ausgangswellenleiter (90-Grad-Knick) wird so
// verändert, dass möglichst viel Leistung in die Grundmode des Ausgangs läuft. Je Iteration zwei FDTD-Läufe
// (vorwärts und adjungiert, gleichzeitig), daraus der Gradient nach der Permittivität jedes Pixels.
// Herleitung, Filter, Projektion und Grenzen: docs/physics/inverse_design.md
#include <complex>
#include <functional>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::field {

struct InverseDesignSettings {
  std::string core = "Si";        // Material der Wellenleiter und des Designs (Materialbibliothek)
  std::string cladding = "SiO2";  // Mantel
  double wavelength = 1550e-9;    // Mitte; Ziel ist das Mittel der Transmission über objective_wavelengths
  std::vector<double> objective_wavelengths = {1530e-9, 1550e-9, 1570e-9};
  double cell_size = 40e-9;
  double waveguide_width = 400e-9;
  double design_size = 2.4e-6;  // Kantenlänge des quadratischen Designgebiets
  double lead = 1.6e-6;         // Abstand Designgebiet - Randschicht
  int pml_cells = 12;
  int iterations = 40;
  double step = 0.15;            // größte Änderung der Designvariablen je Iteration
  double filter_radius = 80e-9;  // Kegelfilter (Mindeststrukturgröße etwa doppelt so groß)
  double initial = 0.5;          // Startwert aller Designvariablen (0 = Mantel, 1 = Kern)
  double pulse_duration = 15e-15;
  int threads = 0;  // je FDTD-Lauf; 0 = automatisch
};

// Ein FDTD-Problem mit fester Geometrie außerhalb des Designgebiets.
class BendProblem {
 public:
  explicit BendProblem(const InverseDesignSettings& settings);

  int nx() const { return design_nx_; }  // Pixel des Designgebiets (Knoten des Gitters)
  int ny() const { return design_ny_; }
  double eps_core() const { return eps_core_; }
  double eps_cladding() const { return eps_clad_; }
  const FieldScene& scene() const { return scene_; }  // ohne Designgebiet (dort Mantel)
  double pixel_x0() const;  // Szenen-Koordinate der linken Kante des ersten Pixels [m]
  double pixel_y0() const;
  // Transmission je Wellenlänge (auf die Grundmode bezogen, Bezug: gerader Wellenleiter) für die relative
  // Permittivität eps (nx*ny Werte, Zeile für Zeile). Mit gradient != nullptr zusätzlich dT_i/d eps je Pixel
  // (gradient->[i][p]) aus dem adjungierten Lauf.
  std::vector<double> evaluate(const std::vector<double>& eps, std::vector<std::vector<double>>* gradient,
                               const std::vector<double>& wavelengths) const;
  // Transmission über weitere Wellenlängen (Spektrum) ohne Gradient
  std::vector<double> spectrum(const std::vector<double>& eps, const std::vector<double>& wavelengths) const;

 private:
  std::vector<std::complex<double>> reference_amplitudes(const std::vector<double>& wavelengths) const;
  InverseDesignSettings settings_;
  FieldScene scene_, reference_scene_;
  double eps_core_ = 1.0, eps_clad_ = 1.0;
  int i0_ = 0, j0_ = 0, design_nx_ = 0, design_ny_ = 0;
  std::size_t steps_ = 0;
};

struct InverseDesignResult {
  bool completed = false;
  std::vector<double> history;  // mittlere Transmission je Iteration (stetiges Design)
  std::vector<double> beta;     // Steilheit der Projektion je Iteration
  std::vector<double> density;  // projizierte Dichte 0..1 (nx*ny), letzter Stand
  std::vector<double> binary;   // Dichte nach Schwelle 0,5 (0 oder 1)
  int nx = 0, ny = 0;
  double pixel_size = 0.0, pixel_x0 = 0.0, pixel_y0 = 0.0;  // Lage in Szenen-Koordinaten [m]
  FieldScene scene;  // Wellenleiter ohne Design (für Layout und Darstellung)
  double eps_core = 1.0, eps_cladding = 1.0;
  double transmission_start = 0.0;                            // mittlere Transmission des Startdesigns
  double transmission_final = 0.0;                            // stetiges Enddesign
  double transmission_binary = 0.0;                           // binäres Enddesign (herstellbar)
  double transmission_empty = 0.0;                            // Designgebiet nur Mantel (Bezug)
  std::vector<double> spectrum_wavelengths, spectrum_binary;  // Transmission des binären Designs
};

struct InverseDesignProgress {
  int iteration = 0;
  const std::vector<double>* density = nullptr;
  const std::vector<double>* history = nullptr;
};

// Optimierung; on_iteration (optional) wird nach jeder Iteration im rechnenden Thread gerufen.
// Abbruch über control liefert completed = false mit dem bisherigen Stand.
InverseDesignResult run_inverse_design(
    const InverseDesignSettings& settings, JobControl* control = nullptr,
    const std::function<void(const InverseDesignProgress&)>& on_iteration = {});

// Feld-Szene des binären Designs: Wellenleiter, Quelle und Monitor der Optimierung plus Rechtecke für die
// Kern-Pixel (gleiche Läufe in benachbarten Zeilen zusammengefasst), Laufzeit gesetzt. Damit lässt sich das
// Ergebnis im normalen 2D-FDTD nachrechnen und weiterbearbeiten.
FieldScene design_scene(const InverseDesignResult& result);

// Gradientenprüfung: dT/d eps an einzelnen Pixeln aus der Adjoint-Methode und aus zentralen Differenzen
struct GradientSample {
  int pixel = 0;
  double adjoint = 0.0, finite_difference = 0.0;
};
std::vector<GradientSample> check_gradient(const InverseDesignSettings& settings,
                                           const std::vector<int>& pixels, double delta_eps);

// Filter und Projektion (öffentlich für Tests): Kegelfilter mit Radius in Pixeln; Projektion
// (tanh(b*eta) + tanh(b*(x - eta))) / (tanh(b*eta) + tanh(b*(1 - eta))) mit eta = 0,5
std::vector<double> cone_filter(const std::vector<double>& x, int nx, int ny, double radius_pixels);
double project(double x, double beta);
double project_derivative(double x, double beta);

}  // namespace photonlab::field
