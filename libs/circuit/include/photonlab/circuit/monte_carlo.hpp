#pragma once
// Monte-Carlo über Fertigungs- und Betriebsstreuung: Parameter werden normalverteilt gestört,
// die Schaltung wird für jede Stichprobe neu gelöst. Gleicher Startwert, gleiches Ergebnis –
// unabhängig von Plattform, Threadanzahl und Reihenfolge. Annahmen: docs/physics/monte_carlo.md
#include <cstdint>
#include <string>
#include <vector>

#include "photonlab/circuit/solver.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"

namespace photonlab::circuit {

// Streuung eines Parameters aller Instanzen eines Bauteiltyps.
struct Variation {
  std::string type;       // Bauteiltyp, z. B. "waveguide"
  std::string param;      // Parameter, z. B. "n_eff"
  double sigma = 0.0;     // Standardabweichung: absolut in SI oder relativ zum Nennwert
  bool relative = false;  // true: sigma ist ein Bruchteil des Nennwerts
  bool correlated =
      false;  // true: alle Instanzen erhalten dieselbe Abweichung (z. B. Schichtdicke, Temperatur)
  bool operator==(const Variation&) const = default;
};

struct MonteCarloSettings {
  int samples = 500;
  std::uint64_t seed = 1;
  std::vector<Variation> variations;
};

struct MonteCarloResult {
  std::vector<std::string> names;           // "instanz Messgröße"
  std::vector<std::string> units;           // Einheit je Messgröße
  std::vector<std::vector<double>> values;  // [Messgröße][Stichprobe]; NaN bei nicht lösbarer Stichprobe
  int failed = 0;                           // nicht lösbare Stichproben
  bool cancelled = false;
};

// Prüft die Einstellungen gegen Schaltung und Registry; wirft CircuitError mit Klartext.
void validate(const MonteCarloSettings& settings, const Circuit& circuit, const ComponentRegistry& registry);

// Gestörte Kopie der Schaltung für die Stichprobe sample_index (Werte auf den Parameterbereich begrenzt).
Circuit perturbed_circuit(const Circuit& circuit, const ComponentRegistry& registry,
                          const MonteCarloSettings& settings, std::uint64_t sample_index);

MonteCarloResult monte_carlo(const Circuit& circuit, const ComponentRegistry& registry, double wavelength,
                             const MonteCarloSettings& settings, ThreadPool* pool = nullptr,
                             JobControl* control = nullptr);

struct Statistics {
  int count = 0;  // gültige (endliche) Werte
  double mean = 0.0;
  double stddev = 0.0;  // Stichproben-Standardabweichung (n − 1)
  double min = 0.0;
  double max = 0.0;
};
Statistics statistics(const std::vector<double>& values);

// Ausbeute: Anteil aller Stichproben (auch der nicht lösbaren), deren Wert die Grenze einhält.
double yield_fraction(const std::vector<double>& values, double limit, bool at_least);

// Standardnormalverteilte Zahl als reine Funktion von (seed, a, b, c); Grundlage der Reproduzierbarkeit.
double standard_normal(std::uint64_t seed, std::uint64_t a, std::uint64_t b, std::uint64_t c);

}  // namespace photonlab::circuit
