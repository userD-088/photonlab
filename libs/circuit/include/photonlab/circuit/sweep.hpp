#pragma once
// Wellenlängen-Sweep als Job: läuft parallel im Threadpool, meldet Fortschritt, ist abbrechbar.
#include <vector>

#include "photonlab/circuit/solver.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"

namespace photonlab::circuit {

struct SweepSettings {
  double wavelength_start = 1.5e-6;  // [m]
  double wavelength_stop = 1.6e-6;   // [m]
  int points = 501;
};

// Gleichmäßig verteilte Wellenlängen einschließlich beider Endpunkte.
std::vector<double> sweep_wavelengths(const SweepSettings& settings);

// Löst die Schaltung für alle Wellenlängen. pool == nullptr rechnet im aufrufenden Thread.
// Bei Abbruch über control->cancel_requested ist das Ergebnis leer.
std::vector<Solution> sweep(const CompiledCircuit& circuit, const SweepSettings& settings,
                            ThreadPool* pool = nullptr, JobControl* control = nullptr);

// Rampe mit Ast-Verfolgung für nichtlineare Schaltungen: die Punkte werden der Reihe nach gerechnet, der
// Zustand des vorigen Punktes ist Startwert des nächsten. Hin- und Rückweg getrennt aufrufen, um Hysterese zu
// sehen (z. B. Wellenlängen aufsteigend und absteigend). Lineare Schaltungen liefern dasselbe wie sweep().
struct RampResult {
  std::vector<Solution> solutions;
  std::vector<CircuitState> states;
};
// Wellenlängen-Rampe bei fester Quellstärke; state (optional) ist Start- und Endzustand.
RampResult wavelength_ramp(const CompiledCircuit& circuit, const std::vector<double>& wavelengths,
                           double source_scale = 1.0, CircuitState* state = nullptr,
                           JobControl* control = nullptr);
// Leistungsrampe bei fester Wellenlänge: source_scales skaliert die Amplituden aller Quellen (Leistung ~
// Quadrat).
RampResult power_ramp(const CompiledCircuit& circuit, double wavelength,
                      const std::vector<double>& source_scales, CircuitState* state = nullptr,
                      JobControl* control = nullptr);

}  // namespace photonlab::circuit
