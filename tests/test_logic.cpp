// Phase 3: Logik-Labor. Wahrheitstabellen, Kontrastverhältnis und Gatter-Zeugnis gegen analytische
// Erwartungen.
#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/io/project.hpp"
#include "photonlab/logic/optimize.hpp"
#include "photonlab/logic/reference_gates.hpp"
#include "photonlab/logic/report.hpp"

using namespace photonlab;
using namespace photonlab::logic;

namespace {
const circuit::ComponentRegistry& reg() { return circuit::ComponentRegistry::builtin(); }
const double lambda0 = 1550 * units::nm;

Rating rating_of(const GateReport& report, const char* key) {
  const Criterion* c = report.find(key);
  REQUIRE(c != nullptr);
  return c->rating;
}
}  // namespace

TEST_CASE("Kontrastverhältnis CR = 10*log10(P1_min / P0_max)") {
  CHECK(contrast_ratio_db(1e-3, 1e-5) == doctest::Approx(20.0));
  CHECK(contrast_ratio_db(2e-3, 0.5e-3) == doctest::Approx(6.0206).epsilon(1e-5));
  CHECK(std::isinf(contrast_ratio_db(1e-3, 0.0)));
  CHECK(std::isnan(contrast_ratio_db(0.0, 0.0)));
}

TEST_CASE("Funktionen erkennen und Bits zuordnen") {
  CHECK(identify_function({0, 1, 1, 0}) == "XOR");
  CHECK(identify_function({1, 0}) == "NOT");
  CHECK(identify_function({0, 1, 0, 1, 0, 1, 0, 1}).empty());
  CHECK(function_bits("NAND", 2) == std::vector<int>{1, 1, 1, 0});
  CHECK(function_bits("NAND", 1).empty());
  CHECK(function_names(2).size() == 14);

  const GateDefinition gate = reference_gate("xor_interference");
  CHECK(gate.row_count() == 4);
  CHECK(input_bit(gate, 2, 0) == 1);  // Zeile 2 = "10": erster Eingang ist das höchstwertige Bit
  CHECK(input_bit(gate, 2, 1) == 0);
}

TEST_CASE("Schwellwert-Analyse: mit Erwartung und mit Erkennung aus der größten Lücke") {
  TruthTable table;
  for (double p : {0.1e-3, 0.9e-3, 1.0e-3, 0.2e-3}) table.rows.push_back({{}, {p}});
  const OutputAnalysis expected = analyze_output(table, 0, {0, 1, 1, 0});
  CHECK(expected.separable);
  CHECK(expected.p1_min == 0.9e-3);
  CHECK(expected.p0_max == 0.2e-3);
  CHECK(expected.threshold == doctest::Approx(0.55e-3));
  CHECK(expected.contrast_db == doctest::Approx(10.0 * std::log10(4.5)));
  CHECK(expected.extinction_db == doctest::Approx(10.0));

  const OutputAnalysis inferred = analyze_output(table, 0, {});
  CHECK_FALSE(inferred.has_expected);
  CHECK(inferred.bits == std::vector<int>{0, 1, 1, 0});
  CHECK(inferred.function == "XOR");

  const OutputAnalysis wrong = analyze_output(table, 0, {0, 0, 0, 1});  // als AND erwartet: nicht trennbar
  CHECK_FALSE(wrong.separable);
  CHECK(logic_holds(table, 0, {0, 1, 1, 0}, 0.55e-3));
  CHECK_FALSE(logic_holds(table, 0, {0, 1, 1, 0}, 0.95e-3));
}

TEST_CASE("Abnahme Phase 3: Interferenz-XOR mit CR > 20 dB im Idealfall") {
  const io::Project project = io::reference_project("xor_interference");
  const TruthTable table = run_truth_table(project.circuit, reg(), project.gate, lambda0);
  REQUIRE(table.rows.size() == 4);
  // Zwei Laser mit je 1 mW: XOR-Ausgang 2 mW * sin²(dphi/2), XNOR-Ausgang 2 mW * cos²(dphi/2)
  const double expected_xor[] = {0.0, 2e-3, 2e-3, 0.0};
  for (std::size_t row = 0; row < 4; ++row) {
    CHECK(std::abs(table.rows[row].power[0] - expected_xor[row]) < 1e-15);
    CHECK(std::abs(table.rows[row].power[0] + table.rows[row].power[1] - 2e-3) < 1e-15);
  }
  const OutputAnalysis xor_out = analyze_output(table, 0, project.gate.outputs[0].expected);
  CHECK(xor_out.separable);
  CHECK(xor_out.contrast_db > 20.0);
  CHECK(xor_out.function == "XOR");
  CHECK(analyze_output(table, 1, {}).function == "XNOR");
}

TEST_CASE("XOR mit ungleichem Koppler: CR = 20*log10((t + k)/(t - k))") {
  io::Project project = io::reference_project("xor_interference");
  const double coupling = 0.4;
  project.circuit.find_instance("koppler")->params["power_coupling"] = coupling;
  const TruthTable table = run_truth_table(project.circuit, reg(), project.gate, lambda0);
  const double t = std::sqrt(1 - coupling), k = std::sqrt(coupling);
  const OutputAnalysis a = analyze_output(table, 0, project.gate.outputs[0].expected);
  CHECK(a.contrast_db == doctest::Approx(20.0 * std::log10((t + k) / (t - k))).epsilon(1e-10));
  CHECK(a.p1_min == doctest::Approx(1e-3 * (t + k) * (t + k)).epsilon(1e-12));
}

TEST_CASE("Abnahme Phase 3: das Zeugnis markiert fehlende Regeneration und Kaskadierbarkeit des XOR") {
  const io::Project project = io::reference_project("xor_interference");
  const GateReport report = characterize(project.circuit, reg(), project.gate, lambda0);
  CHECK(report.function_correct);
  CHECK(rating_of(report, "function") == Rating::good);
  CHECK(rating_of(report, "contrast") == Rating::good);
  CHECK(rating_of(report, "regeneration") == Rating::bad);
  CHECK(rating_of(report, "cascade") == Rating::bad);
  CHECK_FALSE(report.cascade.possible);  // phasencodierter Eingang, intensitätscodierter Ausgang
  CHECK(rating_of(report, "loss") == Rating::bad);
  CHECK(report.level_slope_db_per_db ==
        doctest::Approx(1.0).epsilon(1e-9));  // linear: Pegel folgt der Quelle
  CHECK(report.fan_out == doctest::Approx(2.0).epsilon(1e-12));
  CHECK(rating_of(report, "fan_out") == Rating::limited);
  CHECK(rating_of(report, "energy") == Rating::not_rated);
  CHECK_FALSE(report.real_gate);

  // Kennlinie über der Eingangsphase: P = 2 mW * sin²(pi*x/2), an den Pegeln flach, in der Mitte Steigung
  // pi/2
  CHECK(report.end_slope < 0.1);
  CHECK(report.mid_slope == doctest::Approx(constants::pi / 2).epsilon(1e-3));

  // Phasentoleranz: drei Phasen (zwei Laser, Bias) mit ungünstigsten Vorzeichen, Bit kippt bei 3*delta = 90°
  CHECK(report.phase_parameter_count == 3);
  CHECK(report.phase_tolerance < constants::pi / 6);
  CHECK(report.phase_tolerance > constants::pi / 6 / 1.3);  // Raster der Suche: Faktor 1,23
  CHECK(report.wavelength_tolerance >= 19.9e-9);            // alle Bauteile wellenlängenunabhängig
}

TEST_CASE("Lineares AND: 6 dB Kontrast, Kaskade wird gebaut und scheitert") {
  const io::Project project = io::reference_project("and_interference");
  const GateReport report = characterize(project.circuit, reg(), project.gate, lambda0);
  // Pegel 0, P/2, P/2, 2P mit P = 1 mW
  const double expected[] = {0.0, 0.5e-3, 0.5e-3, 2e-3};
  for (std::size_t row = 0; row < 4; ++row)
    CHECK(std::abs(report.table.rows[row].power[0] - expected[row]) < 1e-15);
  CHECK(report.function_correct);
  CHECK(report.worst_contrast_db == doctest::Approx(10.0 * std::log10(4.0)).epsilon(1e-10));
  CHECK(rating_of(report, "contrast") == Rating::bad);

  REQUIRE(report.cascade.possible);
  CHECK(report.cascade.gate.inputs.size() == 3);  // A1, B1 und B2
  REQUIRE(report.cascade.table.rows.size() == 8);
  CHECK(report.cascade.gate.outputs[0].expected ==
        std::vector<int>{0, 0, 0, 0, 0, 0, 0, 1});  // (A1 AND B1) AND B2
  // Zeile A1=0, B1=1, B2=1: Stufe 1 liefert P/2 statt 0, Stufe 2 macht daraus (sqrt(P/2) + sqrt(P))²/2 =
  // 1,457 mW
  CHECK(report.cascade.table.rows[3].power[0] ==
        doctest::Approx(1e-3 * std::pow(std::sqrt(0.5) + 1.0, 2) / 2).epsilon(1e-12));
  CHECK_FALSE(report.cascade.logic_preserved);  // 1,457 mW liegt über dem Entscheidungspegel 1,25 mW
  CHECK(rating_of(report, "cascade") == Rating::bad);
  CHECK(rating_of(report, "regeneration") == Rating::bad);
  CHECK(report.end_slope > 1.0);  // Wurzel-Kennlinie: unendlich steil bei kleinem Eingangspegel
}

TEST_CASE("Directed Logic: XOR/XNOR richtig, aber schmalbandig und nur über Elektronik kaskadierbar") {
  const io::Project project = io::reference_project("xor_directed");
  const GateReport report = characterize(project.circuit, reg(), project.gate, project.wavelength);
  CHECK(report.function_correct);
  CHECK(report.outputs[0].function == "XOR");
  CHECK(report.outputs[1].function == "XNOR");
  CHECK(report.worst_contrast_db > 20.0);
  CHECK(rating_of(report, "cascade") == Rating::bad);
  CHECK(rating_of(report, "fan_out") == Rating::not_rated);
  CHECK(rating_of(report, "regeneration") == Rating::limited);
  CHECK(rating_of(report, "bias") == Rating::bad);
  CHECK(report.wavelength_tolerance > 1e-12);
  CHECK(report.wavelength_tolerance < 0.1e-9);  // schmaler als die Resonanzbreite
  CHECK(report.latency > 1e-12);                // Licht verweilt in den Ringen
}

TEST_CASE("Latenz ist die Gruppenlaufzeit n_g*L/c0; ein Wellenleiter ist kein regenerierendes Gatter") {
  const double length = 1 * units::mm;
  circuit::Circuit c;
  c.add("laser", "laser", {{"power", 1e-3}});
  c.add("leitung", "waveguide", {{"length", length}, {"loss_db_per_cm", 0.0}});
  c.add("det", "detector");
  c.connect("laser.out", "leitung.in");
  c.connect("leitung.out", "det.in");
  const GateDefinition gate{"Leitung",
                            {{"A", "laser", "power", 0.0, 1e-3, Encoding::intensity}},
                            {{"Y", "det", "Leistung", function_bits("BUFFER", 1)}}};
  const GateReport report = characterize(c, reg(), gate, lambda0);
  CHECK(report.latency == doctest::Approx(4.2 * length / constants::c0).epsilon(1e-6));
  CHECK(report.outputs[0].function == "BUFFER");
  CHECK(report.end_slope == doctest::Approx(1.0).epsilon(1e-6));  // y = x: Störungen gehen 1:1 durch
  CHECK(rating_of(report, "regeneration") == Rating::bad);
  CHECK(report.fan_out == doctest::Approx(1.0).epsilon(1e-9));
  CHECK(rating_of(report, "fan_out") == Rating::bad);
  // Eine verlustfreie Leitung lässt sich beliebig oft hintereinanderschalten
  CHECK(report.cascade.possible);
  CHECK(report.cascade.logic_preserved);
}

TEST_CASE("Zeugnis bei falscher Erwartung: Funktion rot, abhängige Kriterien nicht bewertet") {
  io::Project project = io::reference_project("xor_interference");
  project.gate.outputs[0].expected = function_bits("AND", 2);
  const GateReport report = characterize(project.circuit, reg(), project.gate, lambda0);
  CHECK_FALSE(report.function_correct);
  CHECK(rating_of(report, "function") == Rating::bad);
  CHECK(rating_of(report, "cascade") == Rating::not_rated);
  CHECK(rating_of(report, "bias") == Rating::not_rated);
  CHECK_FALSE(report.real_gate);
}

TEST_CASE("Optimierer findet die Bias-Phase pi/2 des XOR") {
  io::Project project = io::reference_project("xor_interference");
  // Verstimmter Bias: Pegel (1 -+ sin(bias)) mW, also CR = 10*log10((1 + sin)/(1 - sin)) = 2,7 dB bei 0,3 rad
  project.circuit.find_instance("bias")->params["phase"] = 0.3;
  CHECK(worst_contrast_db(project.circuit, reg(), project.gate, lambda0) ==
        doctest::Approx(10.0 * std::log10((1 + std::sin(0.3)) / (1 - std::sin(0.3)))).epsilon(1e-10));
  // Bias 0: beide Ausgänge zeigen immer 1 mW, keine Logik
  project.circuit.find_instance("bias")->params["phase"] = 0.0;
  CHECK(std::isnan(worst_contrast_db(project.circuit, reg(), project.gate, lambda0)));
  project.circuit.find_instance("bias")->params["phase"] = 0.3;
  const OptimizeResult result =
      optimize_parameter(project.circuit, reg(), project.gate, lambda0, "bias", "phase", 0.0, constants::pi);
  REQUIRE(result.found);
  CHECK(result.best_x == doctest::Approx(constants::pi / 2).epsilon(1e-3));
  CHECK(result.best_contrast_db > 60.0);
  CHECK(result.x.size() == 121);
  CHECK_THROWS_AS(
      optimize_parameter(project.circuit, reg(), project.gate, lambda0, "laser_a", "phase", 0.0, 1.0),
      circuit::CircuitError);  // logischer Eingang
}

TEST_CASE("Fehlerhafte Gatter-Definitionen werden mit Klartext abgelehnt") {
  const io::Project project = io::reference_project("xor_interference");
  auto broken = [&](auto&& change) {
    GateDefinition gate = project.gate;
    change(gate);
    CHECK_THROWS_AS(run_truth_table(project.circuit, reg(), gate, lambda0), circuit::CircuitError);
  };
  broken([](GateDefinition& g) { g.inputs.clear(); });
  broken([](GateDefinition& g) { g.outputs.clear(); });
  broken([](GateDefinition& g) { g.inputs[0].instance = "gibt_es_nicht"; });
  broken([](GateDefinition& g) { g.inputs[0].param = "tippfehler"; });
  broken([](GateDefinition& g) { g.inputs[0].value1 = 1e9; });  // außerhalb des Bereichs
  broken([](GateDefinition& g) { g.inputs[0].value1 = g.inputs[0].value0; });
  broken([](GateDefinition& g) { g.outputs[0].expected = {0, 1}; });           // falsche Länge
  broken([](GateDefinition& g) { g.outputs[0].measurement = "Photostrom"; });  // keine Leistung
  broken([](GateDefinition& g) { g.outputs[0].instance = "koppler"; });        // keine Messgröße
  CHECK_THROWS_AS(encoding_from_string("morse"), circuit::CircuitError);
}

TEST_CASE("Projektdatei und .gate.json enthalten die Gatter-Definition") {
  const io::Project project = io::reference_project("xor_directed");
  const io::Project loaded = io::project_from_json(io::project_to_json(project));
  CHECK(loaded.gate == project.gate);
  CHECK(io::project_from_json(io::project_to_json(io::reference_project("mzi"))).gate.empty());

  const GateReport report = characterize(project.circuit, reg(), project.gate, project.wavelength);
  const std::string json = io::gate_report_to_json(project.gate, report);
  CHECK(json.find("\"format\": \"photonlab.gate\"") != std::string::npos);
  CHECK(json.find("\"function\": \"XOR\"") != std::string::npos);
  CHECK(json.find("\"key\": \"regeneration\"") != std::string::npos);
  // unendliche Werte (Isolation) werden als null geschrieben, nicht als ungültiges JSON
  CHECK(json.find("\"isolation_db\": null") != std::string::npos);
  CHECK(json.find("inf") == std::string::npos);
}
