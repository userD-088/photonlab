#pragma once
// S-Parameter-Extraktion aus dem 2D-FDTD (Brücke 1, Bauplan Abschnitt 6): je Port eine Anregung mit
// breitbandigem Modenpuls, an allen Ports laufende Fourier-Transformation von Skalarfeld und Querfeld,
// Zerlegung in ein- und auslaufende Mode. Herleitung und Grenzen: docs/physics/sparameters.md
#include <complex>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::field {

struct SParameterResult {
  std::vector<std::string> ports;  // Reihenfolge der Matrix: Ports mit Richtung +x (linke Seite) zuerst
  int left_port_count = 0;         // so viele der ersten Ports liegen auf der linken Seite des Bauteils
  std::vector<double> wavelengths;  // [m], aufsteigend
  // S[l][i * n + j]: auslaufende Welle an Port i bei Anregung an Port j, leistungsnormiert (|S|² = Leistung),
  // Phase bezogen auf die Portlinien.
  std::vector<std::vector<std::complex<double>>> s;
  std::vector<std::vector<double>> n_eff;  // [Port][Wellenlänge], effektiver Index der Portmode auf dem Gitter
  std::vector<std::string> warnings;
  std::size_t steps = 0;           // Summe der Zeitschritte aller Anregungen
  double simulated_time = 0.0;     // längste simulierte Zeit einer Anregung [s]
  bool cancelled = false;

  std::size_t port_count() const { return ports.size(); }
  std::complex<double> at(std::size_t wavelength, std::size_t out, std::size_t in) const {
    return s[wavelength][out * ports.size() + in];
  }
};

// Prüft Ports und Einstellungen gegen die Szene; wirft FieldError mit Klartext.
void validate_sparameter_setup(const FieldScene& scene);

// Rechnet eine Anregung je Port (parallel im Pool, falls angegeben). Quellen und Monitore der Szene werden
// dabei ignoriert. control: Fortschritt und Abbruch (dann ist cancelled gesetzt und s leer).
SParameterResult extract_sparameters(const FieldScene& scene, ThreadPool* pool = nullptr,
                                     JobControl* control = nullptr);

// Richtung ins Bauteil: +1, wenn sie entlang der positiven Achse zeigt (senkrechte Linie links, waagerechte oben).
int port_inward_sign(const FieldScene& scene, const Port& port);
bool port_is_vertical(const FieldScene& scene, const Port& port);

}  // namespace photonlab::field
