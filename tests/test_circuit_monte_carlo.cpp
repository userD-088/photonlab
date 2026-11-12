#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "photonlab/circuit/monte_carlo.hpp"
#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/core/units.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {
const ComponentRegistry& reg() { return ComponentRegistry::builtin(); }

std::size_t index_of(const MonteCarloResult& result, const std::string& name) {
  for (std::size_t i = 0; i < result.names.size(); ++i)
    if (result.names[i] == name) return i;
  FAIL("Messgröße fehlt: " << name);
  return 0;
}
}  // namespace

TEST_CASE("Zufallszahlen: standardnormalverteilt und reine Funktion der Argumente") {
  CHECK(standard_normal(7, 1, 2, 3) == standard_normal(7, 1, 2, 3));
  CHECK(standard_normal(7, 1, 2, 3) != standard_normal(8, 1, 2, 3));
  CHECK(standard_normal(7, 1, 2, 3) != standard_normal(7, 1, 2, 4));
  std::vector<double> values;
  for (std::uint64_t i = 0; i < 100000; ++i) values.push_back(standard_normal(42, i, 0, 0));
  const Statistics s = statistics(values);
  CHECK(std::abs(s.mean) < 4.0 / std::sqrt(100000.0));  // 4 Standardfehler
  CHECK(s.stddev == doctest::Approx(1.0).epsilon(0.02));
  double fourth = 0.0;
  for (double v : values) fourth += v * v * v * v;
  CHECK(fourth / 100000.0 == doctest::Approx(3.0).epsilon(0.05));  // Wölbung der Normalverteilung
}

TEST_CASE("Monte-Carlo am MZI: Mittelwert folgt der analytischen Erwartung") {
  // Phase ~ N(phi0, sigma²)  =>  E[cos²(phi/2)] = (1 + cos(phi0)*exp(-sigma²/2)) / 2
  const double phi0 = 1.0, sigma = 0.3;
  Circuit mzi = reference_mzi();
  mzi.find_instance("phase")->params["phase"] = phi0;
  MonteCarloSettings settings;
  settings.samples = 20000;
  settings.seed = 2026;
  settings.variations = {{"phase_shifter", "phase", sigma, false, false}};
  ThreadPool pool(4);
  const MonteCarloResult result = monte_carlo(mzi, reg(), 1550 * units::nm, settings, &pool);
  CHECK(result.failed == 0);
  CHECK_FALSE(result.cancelled);

  const auto& cross = result.values[index_of(result, "det_cross Leistung")];
  const auto& bar = result.values[index_of(result, "det_bar Leistung")];
  const Statistics s = statistics(cross);
  const double expected = 1e-3 * 0.5 * (1.0 + std::cos(phi0) * std::exp(-sigma * sigma / 2.0));
  CHECK(s.count == 20000);
  CHECK(std::abs(s.mean - expected) < 4.0 * s.stddev / std::sqrt(20000.0));
  // Varianz: Var[cos(phi)]/4 mit E[cos²(phi)] = (1 + cos(2*phi0)*exp(-2*sigma²))/2
  const double mean_cos = std::cos(phi0) * std::exp(-sigma * sigma / 2.0);
  const double variance = 0.25 * (0.5 * (1.0 + std::cos(2 * phi0) * std::exp(-2 * sigma * sigma)) - mean_cos * mean_cos);
  CHECK(s.stddev == doctest::Approx(1e-3 * std::sqrt(variance)).epsilon(0.03));
  // Energie bleibt in jeder Stichprobe erhalten
  for (std::size_t i = 0; i < cross.size(); i += 97) CHECK(cross[i] + bar[i] == doctest::Approx(1e-3).epsilon(1e-12));

  // Ausbeute gegen die Verteilungsfunktion: P(cross >= P0) für die Grenze bei phi = phi0 ist nahe 1/2
  CHECK(yield_fraction(cross, 1e-3 * std::pow(std::cos(phi0 / 2), 2), true) == doctest::Approx(0.5).epsilon(0.03));
}

TEST_CASE("Monte-Carlo: reproduzierbar, unabhängig von der Threadanzahl, abhängig vom Startwert") {
  const Circuit mzi = reference_mzi_unbalanced();
  MonteCarloSettings settings;
  settings.samples = 500;
  settings.seed = 5;
  settings.variations = {{"waveguide", "n_eff", 2e-4, false, false}, {"coupler", "power_coupling", 0.05, true, false}};
  ThreadPool pool(4);
  const MonteCarloResult serial = monte_carlo(mzi, reg(), 1550e-9, settings);
  const MonteCarloResult parallel = monte_carlo(mzi, reg(), 1550e-9, settings, &pool);
  CHECK(serial.values == parallel.values);
  settings.seed = 6;
  CHECK(monte_carlo(mzi, reg(), 1550e-9, settings).values != serial.values);
}

TEST_CASE("Monte-Carlo: korrelierte Streuung trifft alle Instanzen gleich") {
  // Beide Arme gleich lang: eine gemeinsame n_eff-Abweichung ändert die Phasendifferenz nicht.
  Circuit mzi = reference_mzi_unbalanced();
  mzi.find_instance("arm_oben")->params["length"] = 100e-6;
  MonteCarloSettings settings;
  settings.samples = 200;
  settings.variations = {{"waveguide", "n_eff", 1e-3, false, true}};
  const MonteCarloResult correlated = monte_carlo(mzi, reg(), 1550e-9, settings);
  const std::size_t cross = index_of(correlated, "det_cross Leistung");
  CHECK(statistics(correlated.values[cross]).stddev < 1e-15);
  settings.variations[0].correlated = false;
  const MonteCarloResult independent = monte_carlo(mzi, reg(), 1550e-9, settings);
  CHECK(statistics(independent.values[cross]).stddev > 1e-6);

  const Circuit sample = perturbed_circuit(mzi, reg(), {200, 1, {{"waveguide", "n_eff", 1e-3, false, true}}}, 17);
  CHECK(sample.find_instance("arm_oben")->params.at("n_eff") == sample.find_instance("arm_unten")->params.at("n_eff"));
  CHECK(sample.find_instance("arm_oben")->params.at("n_eff") != 2.44);
}

TEST_CASE("Monte-Carlo: Werte bleiben im Parameterbereich, fehlerhafte Einstellungen werden abgelehnt") {
  const Circuit mzi = reference_mzi();
  const Circuit sample = perturbed_circuit(mzi, reg(), {1, 1, {{"coupler", "power_coupling", 10.0, false, false}}}, 0);
  const double k = sample.find_instance("koppler1")->params.at("power_coupling");
  CHECK((k == 0.0 || k == 1.0));  // riesige Streuung wird auf den zulässigen Bereich begrenzt

  CHECK_THROWS_AS(monte_carlo(mzi, reg(), 1550e-9, {0, 1, {}}), CircuitError);
  CHECK_THROWS_AS(monte_carlo(mzi, reg(), 1550e-9, {10, 1, {{"gibt_es_nicht", "x", 1.0, false, false}}}), CircuitError);
  CHECK_THROWS_AS(monte_carlo(mzi, reg(), 1550e-9, {10, 1, {{"coupler", "tippfehler", 1.0, false, false}}}), CircuitError);
  CHECK_THROWS_AS(monte_carlo(mzi, reg(), 1550e-9, {10, 1, {{"coupler", "power_coupling", -1.0, false, false}}}),
                  CircuitError);

  ThreadPool pool(2);
  JobControl control;
  control.cancel_requested = true;
  CHECK(monte_carlo(mzi, reg(), 1550e-9, {100, 1, {}}, &pool, &control).cancelled);
}

TEST_CASE("Statistik und Ausbeute ignorieren bzw. zählen ungültige Stichproben richtig") {
  const double nan = std::nan("");
  const Statistics s = statistics({1.0, 2.0, 3.0, nan});
  CHECK(s.count == 3);
  CHECK(s.mean == doctest::Approx(2.0));
  CHECK(s.stddev == doctest::Approx(1.0));
  CHECK(s.min == 1.0);
  CHECK(s.max == 3.0);
  CHECK(yield_fraction({1.0, 2.0, 3.0, nan}, 2.0, true) == doctest::Approx(0.5));  // NaN zählt als Ausfall
  CHECK(yield_fraction({1.0, 2.0, 3.0, nan}, 2.0, false) == doctest::Approx(0.5));
}
