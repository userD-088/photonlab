// Validierung des linearen Schaltungslösers gegen analytische Lösungen (Bauplan Abschnitt 16).
#include <doctest/doctest.h>

#include <cmath>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/core/units.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {
const ComponentRegistry& reg() { return ComponentRegistry::builtin(); }
const double lambda0 = 1550 * units::nm;

double port_power(const CompiledCircuit& c, const Solution& s, const char* instance, const char* port) {
  return power(s.a(c.port_index(instance, port)));
}
}  // namespace

TEST_CASE("MZI folgt cos²(dphi/2) auf 1e-12 und erhält die Energie") {
  const double laser_power = 1e-3;
  for (int k = 0; k <= 360; ++k) {
    const double dphi = 2.0 * constants::pi * k / 360.0;
    Circuit mzi = reference_mzi();
    mzi.find_instance("phase")->params["phase"] = dphi;
    const CompiledCircuit compiled(mzi, reg());
    const Solution solution = compiled.solve(lambda0);

    const double bar = port_power(compiled, solution, "det_bar", "in") / laser_power;
    const double cross = port_power(compiled, solution, "det_cross", "in") / laser_power;
    CHECK(std::abs(cross - std::pow(std::cos(dphi / 2.0), 2)) < 1e-12);
    CHECK(std::abs(bar - std::pow(std::sin(dphi / 2.0), 2)) < 1e-12);
    CHECK(std::abs(bar + cross - 1.0) < 1e-12);

    const PowerBalance balance = compiled.power_balance(solution);
    CHECK(std::abs(balance.emitted - laser_power) < 1e-15);
    CHECK(std::abs(balance.absorbed - laser_power) < 1e-15);
    CHECK(balance.leaving_open == 0.0);
  }
}

TEST_CASE("MZI mit ungleichen Kopplern: Extinktion nach Formel") {
  // Kreuz-Ausgang: |t1*k2*e^{i*phi} + k1*t2|² ; Minimum (t1*k2 - k1*t2)² bei phi = pi
  const double k1 = 0.5, k2 = 0.4;
  Circuit mzi = reference_mzi();
  mzi.find_instance("koppler1")->params["power_coupling"] = k1;
  mzi.find_instance("koppler2")->params["power_coupling"] = k2;
  mzi.find_instance("phase")->params["phase"] = constants::pi;
  const CompiledCircuit compiled(mzi, reg());
  const Solution solution = compiled.solve(lambda0);
  const double expected = std::pow(std::sqrt((1 - k1) * k2) - std::sqrt(k1 * (1 - k2)), 2);
  CHECK(port_power(compiled, solution, "det_cross", "in") / 1e-3 == doctest::Approx(expected).epsilon(1e-12));
}

TEST_CASE("Unbalanciertes MZI: Spektrum und FSR = lambda1*lambda2/(n_g*dL)") {
  Circuit mzi = reference_mzi_unbalanced();
  const double n_eff = 2.44, n_group = 4.2, d_length = 100 * units::um;
  for (const char* arm : {"arm_oben", "arm_unten"}) mzi.find_instance(arm)->params["loss_db_per_cm"] = 0.0;
  const CompiledCircuit compiled(mzi, reg());

  // Spektrum gegen cos²(dphi/2) mit dphi = 2*pi*n_eff(lambda)*dL/lambda
  for (int k = 0; k <= 200; ++k) {
    const double lambda = (1500.0 + 0.5 * k) * units::nm;
    const double n = n_eff + (n_eff - n_group) * (lambda - lambda0) / lambda0;
    const double dphi = 2.0 * constants::pi * n * d_length / lambda;
    const double cross = port_power(compiled, compiled.solve(lambda), "det_cross", "in") / 1e-3;
    CHECK(std::abs(cross - std::pow(std::cos(dphi / 2.0), 2)) < 1e-9);
  }

  // Periodizität: 1/lambda2 = 1/lambda1 - 1/(n_g*dL), also FSR = lambda1*lambda2/(n_g*dL) ~ 5,7 nm
  const double lambda1 = 1550 * units::nm;
  const double lambda2 = 1.0 / (1.0 / lambda1 - 1.0 / (n_group * d_length));
  CHECK((lambda2 - lambda1) / units::nm == doctest::Approx(5.74).epsilon(5e-3));
  const double p1 = port_power(compiled, compiled.solve(lambda1), "det_cross", "in");
  const double p2 = port_power(compiled, compiled.solve(lambda2), "det_cross", "in");
  CHECK(std::abs(p1 - p2) / 1e-3 < 1e-9);
}

TEST_CASE("Rückkopplung: All-Pass-Ring aus Koppler und Schleife folgt der analytischen Transmission") {
  // T = (a² - 2*r*a*cos(phi) + r²) / (1 - 2*a*r*cos(phi) + (r*a)²)
  const double coupling = 0.1, length = 60 * units::um, loss_db_per_cm = 30.0;
  Circuit ring;
  ring.add("laser", "laser", {{"power", 1.0}});
  ring.add("koppler", "coupler", {{"power_coupling", coupling}});
  ring.add("schleife", "waveguide", {{"length", length}, {"loss_db_per_cm", loss_db_per_cm}});
  ring.add("det", "detector");
  ring.connect("laser.out", "koppler.in1");
  ring.connect("koppler.out1", "det.in");
  ring.connect("koppler.out2", "schleife.in");
  ring.connect("schleife.out", "koppler.in2");
  const CompiledCircuit compiled(ring, reg());

  const double r = std::sqrt(1.0 - coupling);
  const double a = std::pow(10.0, -loss_db_per_cm * (length / 1e-2) / 20.0);
  double t_min = 1.0;
  for (int k = 0; k <= 2000; ++k) {
    const double lambda = (1540.0 + 0.01 * k) * units::nm;
    const double n = 2.44 + (2.44 - 4.2) * (lambda - lambda0) / lambda0;
    const double phi = 2.0 * constants::pi * n * length / lambda;
    const double expected =
        (a * a - 2 * r * a * std::cos(phi) + r * r) / (1 - 2 * a * r * std::cos(phi) + r * r * a * a);
    const double simulated = port_power(compiled, compiled.solve(lambda), "det", "in");
    CHECK(std::abs(simulated - expected) < 1e-11);
    t_min = std::min(t_min, simulated);
  }
  CHECK(t_min < 0.5);  // der Bereich enthält mindestens eine Resonanz
}

TEST_CASE("Externe Anregung an offenen Ports: (I - S*C) b = S a_ext") {
  Circuit c;
  c.add("koppler", "coupler", {{"power_coupling", 0.25}});
  const CompiledCircuit compiled(c, reg());
  CVector a_ext = CVector::Zero(compiled.port_count());
  a_ext(compiled.port_index("koppler", "in1")) = 2.0;  // 4 W
  const Solution solution = compiled.solve(lambda0, &a_ext);
  CHECK(power(solution.b(compiled.port_index("koppler", "out1"))) == doctest::Approx(3.0).epsilon(1e-14));
  CHECK(power(solution.b(compiled.port_index("koppler", "out2"))) == doctest::Approx(1.0).epsilon(1e-14));
  const PowerBalance balance = compiled.power_balance(solution, &a_ext);
  CHECK(balance.external_in == doctest::Approx(4.0));
  CHECK(balance.leaving_open == doctest::Approx(4.0).epsilon(1e-14));
}

TEST_CASE("Fehlerhafte Netzlisten werden mit Klartext abgelehnt") {
  Circuit base = reference_mzi();

  Circuit unknown_type = base;
  unknown_type.add("x", "gibt_es_nicht");
  CHECK_THROWS_AS(CompiledCircuit(unknown_type, reg()), CircuitError);

  Circuit unknown_port = base;
  unknown_port.connect("laser.aus", "det_bar.in");
  CHECK_THROWS_AS(CompiledCircuit(unknown_port, reg()), CircuitError);

  Circuit double_connection = base;
  double_connection.add("det3", "detector");
  double_connection.connect("laser.out", "det3.in");
  CHECK_THROWS_AS(CompiledCircuit(double_connection, reg()), CircuitError);

  Circuit self_connection;
  self_connection.add("wg", "waveguide");
  self_connection.connect("wg.in", "wg.in");
  CHECK_THROWS_AS(CompiledCircuit(self_connection, reg()), CircuitError);

  CHECK_THROWS_AS(base.add("laser", "laser"), CircuitError);  // doppelter Name
  CHECK_THROWS_AS(parse_port_ref("ohne_punkt"), CircuitError);
  CHECK_THROWS_AS(CompiledCircuit(base, reg()).solve(-1.0), CircuitError);
}

namespace {
// Verlustfreier Spiegel, der zusätzlich selbst Licht abgibt – zwei davon bilden eine getriebene,
// verlustfreie Kavität ohne Ausgang: physikalisch unbeschränkt, mathematisch singulär.
class EmittingMirror final : public Component {
 public:
  explicit EmittingMirror(double emission) : emission_(emission) {}
  int port_count() const override { return 1; }
  CMatrix s_matrix(double) const override { return CMatrix::Ones(1, 1); }
  CVector source(double) const override { return CVector::Constant(1, cplx(emission_, 0.0)); }

 private:
  double emission_;
};
}  // namespace

TEST_CASE("Singuläre Schaltung wird gemeldet statt geschönt") {
  ComponentRegistry registry;
  registry.add({"emitting_mirror",
                "Testspiegel",
                "",
                {"port"},
                {{"emission", "Emission", 0.0, 0.0, 1.0, "", 1.0}},
                [](const ParamMap& p) { return std::make_unique<EmittingMirror>(p.at("emission")); }});
  Circuit c;
  c.add("links", "emitting_mirror", {{"emission", 1.0}});
  c.add("rechts", "emitting_mirror");
  c.connect("links.port", "rechts.port");
  CHECK_THROWS_AS(CompiledCircuit(c, registry).solve(lambda0), CircuitError);
}

TEST_CASE("Netzliste: Entfernen und Trennen") {
  Circuit c = reference_mzi();
  CHECK(c.is_connected({"phase", "in"}));
  c.disconnect({"phase", "in"});
  CHECK_FALSE(c.is_connected({"phase", "in"}));
  c.remove_instance("koppler2");
  CHECK(c.find_instance("koppler2") == nullptr);
  CHECK(c.connections.size() == 1);  // nur laser -> koppler1 bleibt
  CHECK(c.unique_name("det_bar") == "det_bar1");
  CHECK_NOTHROW(CompiledCircuit(c, reg()).solve(lambda0));
}
