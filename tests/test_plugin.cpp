// Phase 13: Bauteil-Plugins (plugin_api.h) mit dem Beispiel-Plugin (Bragg-Gitter, MMI)
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <fstream>

#include "photonlab/circuit/plugin.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/io/project.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {

// Das Plugin kommt in die eingebaute Registry (prozessweit) und wird deshalb nur einmal geladen
const PluginInfo& example_plugin() {
  static const PluginInfo info = load_plugin(PHOTONLAB_EXAMPLE_PLUGIN);
  return info;
}

double reflectance(const Component& c, double lambda) { return std::norm(c.s_matrix(lambda)(0, 0)); }

}  // namespace

TEST_CASE("Plugin: Laden meldet die Bauteile an, doppeltes Laden und kaputte Dateien werden abgelehnt") {
  const PluginInfo& info = example_plugin();
  CHECK(info.name == "Beispiel");
  CHECK(info.components == std::vector<std::string>{"bragg_grating", "mmi_1x2"});
  CHECK(info.warnings.empty());  // beide Bauteile passiv
  const ComponentType* type = ComponentRegistry::builtin().find("bragg_grating");
  REQUIRE(type);
  CHECK(type->port_names == std::vector<std::string>{"in", "out"});
  CHECK(type->params.size() == 6);
  CHECK(type->find_param("kappa")->display_unit == "1/mm");
  CHECK(ComponentRegistry::builtin().find("waveguide"));  // eingebaute Typen bleiben

  CHECK_THROWS_AS(load_plugin(PHOTONLAB_EXAMPLE_PLUGIN), CircuitError);  // Namen schon vergeben
  CHECK_THROWS_AS(load_plugin("gibt/es/nicht.so"), CircuitError);
  const std::string fake = "kein_plugin_test.so";
  std::ofstream(fake) << "keine Bibliothek";
  CHECK_THROWS_AS(load_plugin(fake), CircuitError);
  std::remove(fake.c_str());
  CHECK(loaded_plugins().size() == 1);
}

TEST_CASE("Plugin Bragg-Gitter: Spitzenreflexion tanh²(kappa L), Nullstellen, Energieerhaltung") {
  example_plugin();
  const auto& registry = ComponentRegistry::builtin();
  const double kappa = 1e5, length = 20e-6, n_eff = 2.44, n_g = 4.2, lambda0 = 1550e-9;
  const double period = lambda0 / (2.0 * n_eff);  // Bragg-Bedingung bei lambda0
  const ParamMap params = {{"kappa", kappa}, {"length", length}, {"period", period}};
  const auto grating = registry.create("bragg_grating", params);
  CHECK(reflectance(*grating, lambda0) ==
        doctest::Approx(std::pow(std::tanh(kappa * length), 2)).epsilon(1e-12).scale(0.0));

  // delta(lambda) = 2 pi n_eff(lambda)/lambda - pi/period; erste Nullstellen bei delta L = +-sqrt(pi² +
  // (kappa L)²)
  auto delta = [&](double lambda) {
    const double n = n_eff + (n_eff - n_g) * (lambda - lambda0) / lambda0;
    return 2.0 * constants::pi * n / lambda - constants::pi / period;
  };
  const double target = std::sqrt(constants::pi * constants::pi + std::pow(kappa * length, 2)) / length;
  for (double sign : {-1.0, 1.0}) {
    double lo = lambda0, hi = lambda0 - sign * 40e-9;  // delta fällt mit lambda
    for (int k = 0; k < 200; ++k) {
      const double mid = 0.5 * (lo + hi);
      ((delta(mid) - sign * target) * (delta(lo) - sign * target) > 0.0 ? lo : hi) = mid;
    }
    CHECK(reflectance(*grating, 0.5 * (lo + hi)) < 1e-12);
  }
  // Bandkante s = 0 (delta = kappa) ohne Division durch 0
  for (int k = 0; k <= 400; ++k) {
    const double lambda = 1500e-9 + k * 0.25e-9;
    const CMatrix s = grating->s_matrix(lambda);
    CHECK((s.adjoint() * s - CMatrix::Identity(2, 2)).norm() < 1e-12);
    CHECK(std::abs(s(0, 1) - s(1, 0)) < 1e-15);  // reziprok
  }
}

TEST_CASE("Plugin-Bauteil in Schaltung und Projektdatei wie ein eingebautes") {
  example_plugin();
  Circuit c;
  c.add("laser", "laser");
  c.add("teiler", "mmi_1x2", {{"excess_loss_db", 1.0}});
  c.add("gitter", "bragg_grating");
  c.add("d1", "detector");
  c.add("d2", "detector");
  c.connect("laser.out", "teiler.in");
  c.connect("teiler.out1", "gitter.in");
  c.connect("gitter.out", "d1.in");
  c.connect("teiler.out2", "d2.in");
  const CompiledCircuit compiled(c, ComponentRegistry::builtin());
  const double lambda = 1550e-9;
  const Solution solution = compiled.solve(lambda);
  const double p_laser = 1e-3, half = 0.5 * std::pow(10.0, -0.1);
  const double r = std::pow(std::tanh(1e5 * 20e-6), 2);  // Standardwerte: Bragg-Wellenlänge 1550 nm
  CHECK(power(solution.a(compiled.port_index("d2", "in"))) ==
        doctest::Approx(p_laser * half).epsilon(1e-12).scale(0.0));
  CHECK(power(solution.a(compiled.port_index("d1", "in"))) ==
        doctest::Approx(p_laser * half * (1.0 - r)).epsilon(1e-9).scale(0.0));

  io::Project project;
  project.circuit = c;
  const io::Project back = io::project_from_json(io::project_to_json(project));
  CHECK(back.circuit.find_instance("gitter")->type == "bragg_grating");
  CHECK(back.circuit.find_instance("teiler")->params.at("excess_loss_db") == 1.0);
}
