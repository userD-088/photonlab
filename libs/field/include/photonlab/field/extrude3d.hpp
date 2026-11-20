#pragma once
// 3D-Rechnung einer Feld-Szene: die Formen der 2D-Szene werden zu einer Schicht der Dicke thickness
// extrudiert (Silizium auf vergrabenem Oxid als Voreinstellung) und im 3D-FDTD gerechnet. Quellen und
// Monitore der Szene werden übernommen: Linien werden zu Flächen quer durch den Schichtaufbau, Moden-
// quellen bekommen das Profil der Mode quer (wie in 2D) mal das Profil der Schichtmode senkrecht.
// Herleitung und Grenzen: docs/physics/fdtd3d.md
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/field/fdtd3d.hpp"
#include "photonlab/field/scene.hpp"
#include "photonlab/field/sparameters.hpp"

namespace photonlab::field {

// Schichtaufbau Stack3d: siehe scene.hpp (gehört zur Szene und wird mit ihr gespeichert)

// Brechzahlen des Aufbaus bei der Wellenlänge (Materialbibliothek; "Luft" = 1).
struct StackIndices {
  double core = 0.0, substrate = 0.0, cladding = 0.0;
};
StackIndices stack_indices(const Stack3d& stack, double wavelength);

struct Monitor3dData {
  std::string name;
  bool flux = false;                 // Fluss durch eine Fläche [W]; sonst Feld an einem Punkt [V/m]
  std::vector<double> value;         // je Zeitschritt
  std::vector<double> mean_history;  // gleitendes Mittel über eine Periode, je Zeitschritt
  double mean = 0.0;                 // Mittel über die letzte Periode der ersten Quelle
};

// Abmessungen vorab (für die Anzeige von Zellen, Speicher und Schritten vor dem Start).
struct Extent3d {
  int nx = 0, ny = 0, nz = 0, pml_cells = 0;
  double time_step = 0.0;
  std::size_t steps = 0;  // für die Laufzeit der Szene (0, wenn unbegrenzt)
  double memory_bytes = 0.0;
};
Extent3d extent_3d(const FieldScene& scene, const Stack3d& stack);

class Extruded3d {
 public:
  // Wirft FieldError mit Klartext (zu viele Zellen, Quelle in der Randschicht, keine Mode unter der Linie).
  Extruded3d(const FieldScene& scene, const Stack3d& stack);

  // steps Zeitschritte rechnen, Monitore mitschreiben; Abbruch über control (Fortschritt bleibt beim
  // Aufrufer, der die Schritte stückelt). each_step: nach jedem Schritt (z. B. Fourier-Summen der Ports).
  void run(std::size_t steps, JobControl* control = nullptr,
           const std::function<void(const Fdtd3d&)>& each_step = {});

  // Querschnittsmode unter einer achsenparallelen Linie, gerechnet mit dem eigenen Modenlöser auf dem
  // 3D-Gitter und der Brechzahl, die das FDTD am Ort der dominanten E-Komponente sieht. Quer zur Linie
  // bleibt die Randschicht außen vor, in z der ganze Aufbau ohne Randschicht.
  struct PlaneMode {
    bool normal_x = true;             // senkrechte Linie: Fläche quer zu x
    Component3d e = Component3d::ey;  // dominante E-Komponente
    Component3d h = Component3d::hz;  // H-Komponente für den Fluss durch die Fläche
    double sign = 1.0;                // Fluss in +Achsenrichtung = sign * E * H
    int count = 0, rows = 0;          // Punkte quer zur Linie (schnell) und in z
    std::vector<std::size_t> cells;   // Zellen der E-Komponente an der Linie, H liegt eine halbe Zelle weiter
    std::size_t next = 1;             // Indexabstand zur Nachbarzelle in Normalenrichtung
    std::vector<double> profile;      // dominante E-Komponente der Mode, Maximum 1
    double n_eff = 0.0;
    bool guided = false;
  };
  PlaneMode plane_mode(double x1, double y1, double x2, double y2, double wavelength, int order) const;
  void set_threads(int threads) { fdtd_->set_threads(threads); }
  double energy() const { return fdtd_->energy(); }

  const Fdtd3d& solver() const { return *fdtd_; }
  std::size_t steps_for(double duration) const;
  double time() const { return fdtd_->time(); }
  int layer_k() const { return layer_k_; }  // Ebene in der Mitte der Schicht
  double layer_bottom() const { return layer_z0_; }
  double layer_top() const { return layer_z1_; }
  const StackIndices& indices() const { return indices_; }

  // Schnitte der Hauptkomponente (Ey bei Hz-Szenen, Ez bei Ez-Szenen) bzw. |E|²: Draufsicht in der Mitte
  // der Schicht (nx * ny, Zeile für Zeile in y) und Seitenansicht bei y (nx * nz, Zeile für Zeile in z).
  std::vector<float> top_view(bool intensity) const;
  std::vector<float> side_view(double y, bool intensity) const;
  // Brechzahl der Schnitte (für die Umrisse der Geometrie)
  std::vector<float> top_index() const;
  std::vector<float> side_index(double y) const;

  const std::vector<Monitor3dData>& monitors() const { return monitors_; }
  const std::vector<std::string>& warnings() const { return warnings_; }

 private:
  struct FluxPlane {
    std::size_t monitor = 0;
    bool normal_x = true;
    std::vector<std::size_t> cells;  // Zelle (i, j, k) mit dem Fluss zwischen i und i+1 (bzw. j, j+1)
  };
  struct Probe {
    std::size_t monitor = 0;
    std::size_t cell = 0;
  };
  void record();
  double index_in_layer(double x, double y) const;

  FieldScene scene_;
  Stack3d stack_;
  StackIndices indices_;
  std::unique_ptr<Fdtd3d> fdtd_;
  Component3d main_ = Component3d::ey;
  double layer_z0_ = 0.0, layer_z1_ = 0.0;
  int layer_k_ = 0, pml_ = 0;
  std::size_t period_steps_ = 1;
  std::vector<FluxPlane> planes_;
  std::vector<Probe> probes_;
  std::vector<Monitor3dData> monitors_;
  std::vector<std::string> warnings_;
};

// S-Parameter der Szene im Schichtaufbau scene.stack (wie extract_sparameters in 2D, siehe sparameters.md):
// je Port ein breitbandiger Puls mit der Querschnittsmode, an allen Portflächen laufende Fourier-
// Transformation von E und H, Zerlegung in hin- und rücklaufende Mode. Die Anregungen laufen nacheinander,
// jede mit allen Threads. Wirft FieldError, wenn an einem Port keine Mode geführt wird.
SParameterResult extract_sparameters_3d(const FieldScene& scene, JobControl* control = nullptr);

}  // namespace photonlab::field
