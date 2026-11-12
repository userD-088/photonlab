#include <doctest/doctest.h>

#include <cmath>

#include "photonlab/circuit/registry.hpp"
#include "photonlab/circuit/solver.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

TEST_CASE("Registry kennt die fünf Bauteile der Phase 1") {
  const auto& reg = ComponentRegistry::builtin();
  for (const char* name : {"waveguide", "coupler", "phase_shifter", "laser", "detector"}) {
    const ComponentType* type = reg.find(name);
    REQUIRE(type != nullptr);
    CHECK(reg.create(name, {})->port_count() == static_cast<int>(type->port_names.size()));
  }
  CHECK(reg.find("gibt_es_nicht") == nullptr);
}

TEST_CASE("Registry lehnt unbekannte Typen, unbekannte und ungültige Parameter ab") {
  const auto& reg = ComponentRegistry::builtin();
  CHECK_THROWS_AS(reg.create("gibt_es_nicht", {}), CircuitError);
  CHECK_THROWS_AS(reg.create("coupler", {{"tippfehler", 0.5}}), CircuitError);
  CHECK_THROWS_AS(reg.create("coupler", {{"power_coupling", 1.5}}), CircuitError);
  CHECK_THROWS_AS(reg.create("waveguide", {{"length", std::nan("")}}), CircuitError);
}

TEST_CASE("Doppelte Anmeldung wird abgelehnt") {
  ComponentRegistry reg;
  register_builtin_components(reg);
  CHECK_THROWS_AS(register_builtin_components(reg), CircuitError);
}

namespace {
// Fremdes Bauteil: Spiegel mit Amplituden-Reflexion r. Der Löser kennt es nicht.
class Mirror final : public Component {
 public:
  explicit Mirror(double r) : r_(r) {}
  int port_count() const override { return 1; }
  CMatrix s_matrix(double) const override { return CMatrix::Constant(1, 1, cplx(r_, 0.0)); }

 private:
  double r_;
};
}  // namespace

TEST_CASE("Neues Bauteil funktioniert ohne Änderung am Löser") {
  ComponentRegistry reg;
  register_builtin_components(reg);
  reg.add({"mirror",
           "Spiegel",
           "Testbauteil",
           {"port"},
           {{"reflectivity", "Reflexion r", 1.0, -1.0, 1.0, "", 1.0}},
           [](const ParamMap& p) { return std::make_unique<Mirror>(p.at("reflectivity")); }});

  Circuit c;
  c.add("laser", "laser", {{"power", 1e-3}});
  c.add("spiegel", "mirror", {{"reflectivity", 0.5}});
  c.connect("laser.out", "spiegel.port");
  const CompiledCircuit compiled(c, reg);
  const Solution solution = compiled.solve(1.55e-6);
  // Der Laser sieht r² der eigenen Leistung als Rückreflexion
  const auto measured = compiled.measurements(solution);
  REQUIRE(measured.size() == 1);
  CHECK(measured[0].instance == "laser");
  CHECK(measured[0].measurement.value == doctest::Approx(0.25e-3).epsilon(1e-14));
}
