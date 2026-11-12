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

}  // namespace photonlab::circuit
