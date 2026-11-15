#pragma once
// Abnahme-Experiment Phase 12: gerader Streifenwellenleiter im 3D-FDTD. Eine Quelle mit dem Profil der
// Grundmode (quasi-TE, Ey) speist Dauerstrich ein; aus der Phase entlang der Achse folgt n_eff, das mit dem
// Modenlöser (gleiches Gitter) verglichen wird. Details: docs/physics/fdtd3d.md
#include <cstddef>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"

namespace photonlab::field {

struct Waveguide3dSettings {
  std::string core = "Si";        // Material des Kerns (Materialbibliothek)
  std::string cladding = "SiO2";  // Mantel ringsum
  double width = 500e-9;          // Kern in y [m] (ganzzahliges Vielfaches der Zelle)
  double height = 220e-9;         // Kern in z [m] (ganzzahliges Vielfaches der Zelle)
  double wavelength = 1550e-9;
  double cell_size = 20e-9;
  double length = 2.4e-6;     // Gebiet in Ausbreitungsrichtung x [m]
  double clearance = 0.4e-6;  // Mantel zwischen Kern und Randschicht, quer [m]
  int pml_cells = 10;
  double ramp_periods = 5.0;    // Einschaltrampe der Quelle
  double window_periods = 4.0;  // Fenster der Fourier-Auswertung am Ende
  int threads = 0;              // 0 = automatisch
  // true: Schicht der Höhe height, in y unbegrenzt (Kern füllt die ganze Breite); Referenz ist dann die
  // analytische Lösung des Schichtwellenleiters (TE: E parallel zur Schicht)
  bool slab = false;
};

struct Waveguide3dResult {
  bool completed = false;
  double n_eff_fdtd = 0.0;       // aus der Phasensteigung entlang der Achse
  double n_eff_corrected = 0.0;  // Yee-Dispersion in Ausbreitungsrichtung herausgerechnet
  double n_eff_mode = 0.0;       // Modenlöser (semi-vektoriell) auf dem Querschnittsgitter des FDTD
  double n_eff_analytic = 0.0;   // nur Schicht: analytische Eigenwertgleichung
  double relative_error = 0.0;  // |n_eff_fdtd - Referenz| / Referenz (Schicht: analytisch, sonst Modenlöser)
  double mode_overlap = 0.0;    // |<Feld, Mode>|² / (<Feld, Feld> <Mode, Mode>) am Messquerschnitt
  double amplitude_ripple = 0.0;  // (max - min) / Mittel der Amplitude im Messbereich (Reflexionen)
  // Phase [rad] und Amplitude [V/m] entlang der Achse im Messbereich
  std::vector<double> axis_x, phase, amplitude;
  double fit_from = 0.0, fit_to = 0.0;  // Messbereich [m]
  // Bilder: Draufsicht Ey (Momentaufnahme, Ebene durch den Kern, nx * ny), Querschnitt |Ey| (ny * nz) und
  // das Profil des Modenlösers auf demselben Querschnitt (ny * nz), Zeile für Zeile
  std::vector<double> top_view, cross_view, cross_mode;
  int nx = 0, ny = 0, nz = 0;
  double cell_size = 0.0;
  std::size_t steps = 0;
  int threads = 1;
  double seconds = 0.0;           // Rechenzeit der Zeitschleife
  double cells_per_second = 0.0;  // Zellen * Schritte / Sekunde
};

// Wirft FieldError bei ungültigen Einstellungen. Abbruch über control liefert completed = false.
Waveguide3dResult run_waveguide_3d(const Waveguide3dSettings& settings, JobControl* control = nullptr);

}  // namespace photonlab::field
