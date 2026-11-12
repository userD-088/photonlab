#include <doctest/doctest.h>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/sweep.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

TEST_CASE("Sweep: Stützstellen enthalten beide Endpunkte") {
  const auto wavelengths = sweep_wavelengths({1.5e-6, 1.6e-6, 11});
  REQUIRE(wavelengths.size() == 11);
  CHECK(wavelengths.front() == 1.5e-6);
  CHECK(wavelengths.back() == 1.6e-6);
  CHECK(wavelengths[5] == doctest::Approx(1.55e-6));
  CHECK_THROWS_AS(sweep_wavelengths({1.5e-6, 1.6e-6, 0}), CircuitError);
}

TEST_CASE("Sweep: parallel und seriell liefern identische Ergebnisse") {
  const CompiledCircuit compiled(reference_mzi_unbalanced(), ComponentRegistry::builtin());
  const SweepSettings settings{1.5e-6, 1.6e-6, 2001};
  ThreadPool pool(4);
  JobControl control;
  const auto serial = sweep(compiled, settings);
  const auto parallel = sweep(compiled, settings, &pool, &control);
  REQUIRE(serial.size() == 2001);
  REQUIRE(parallel.size() == serial.size());
  CHECK(control.progress == 1.0f);
  for (std::size_t i = 0; i < serial.size(); ++i) {
    CHECK(parallel[i].wavelength == serial[i].wavelength);
    CHECK(parallel[i].b == serial[i].b);  // bitgleich: Reihenfolge der Threads darf nichts ändern
  }
}

TEST_CASE("Sweep: Abbruch liefert kein Teilergebnis") {
  const CompiledCircuit compiled(reference_mzi(), ComponentRegistry::builtin());
  ThreadPool pool(2);
  JobControl control;
  control.cancel_requested = true;
  CHECK(sweep(compiled, {1.5e-6, 1.6e-6, 1000}, &pool, &control).empty());
}

TEST_CASE("Sweep: läuft auch als Job im selben Pool ohne Verklemmung") {
  const CompiledCircuit compiled(reference_mzi(), ComponentRegistry::builtin());
  ThreadPool pool(1);  // der einzige Arbeiter rechnet den Job selbst
  auto job = pool.submit([&] { return sweep(compiled, {1.5e-6, 1.6e-6, 200}, &pool).size(); });
  CHECK(job.get() == 200);
}
