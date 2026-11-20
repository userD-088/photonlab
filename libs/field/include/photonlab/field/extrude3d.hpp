#pragma once
// 3D-Rechnung einer Feld-Szene: die Formen der 2D-Szene werden zu einer Schicht der Dicke thickness
// extrudiert (Silizium auf vergrabenem Oxid als Voreinstellung) und im 3D-FDTD gerechnet. Quellen und
// Monitore der Szene werden übernommen: Linien werden zu Flächen quer durch den Schichtaufbau, Moden-
// quellen bekommen das Profil der Mode quer (wie in 2D) mal das Profil der Schichtmode senkrecht.
// Herleitung und Grenzen: docs/physics/fdtd3d.md
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/field/fdtd3d.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::field {

struct Stack3d {
  double thickness = 220e-9;       // Dicke der Bauteilschicht [m]
  std::string core = "Si";         // Material der Kerne (Materialbibliothek)
  std::string substrate = "SiO2";  // unter der Schicht (vergrabenes Oxid)
  std::string cladding = "SiO2";   // über der Schicht; "Luft" für einen offenen Chip
  double below = 0.7e-6;           // Substrat unter der Schicht bis zur Randschicht [m]
  double above = 0.7e-6;           // Mantel über der Schicht bis zur Randschicht [m]
  double cell_size = 40e-9;        // Würfelzelle [m]
  // Formen (und der Hintergrund) mit einer 2D-Brechzahl ab diesem Wert werden in der Schicht zum Kern.
  // In 2D stehen dort meist effektive Indizes (2,85 für 220 nm Si); in 3D zählt das echte Material.
  double core_threshold = 2.0;
};

// Brechzahlen des Aufbaus bei der Wellenlänge (Materialbibliothek; "Luft" = 1).
struct StackIndices {
  double core = 0.0, substrate = 0.0, cladding = 0.0;
};
StackIndices stack_indices(const Stack3d& stack, double wavelength);

struct Monitor3dData {
  std::string name;
  bool flux = false;          // Fluss durch eine Fläche [W]; sonst Feld an einem Punkt [V/m]
  std::vector<double> value;  // je Zeitschritt
  std::vector<double> mean_history;  // gleitendes Mittel über eine Periode, je Zeitschritt
  double mean = 0.0;          // Mittel über die letzte Periode der ersten Quelle
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
  // Aufrufer, der die Schritte stückelt).
  void run(std::size_t steps, JobControl* control = nullptr);

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
  int layer_k_ = 0;
  std::size_t period_steps_ = 1;
  std::vector<FluxPlane> planes_;
  std::vector<Probe> probes_;
  std::vector<Monitor3dData> monitors_;
  std::vector<std::string> warnings_;
};

}  // namespace photonlab::field
