// Phase 10: Logik-Ebene - Event-Simulator, Brücke 2 (Blöcke aus Schaltungen), Addierer, Flip-Flop, Pegel- und
// Codierungs-Prüfung.
#include <doctest/doctest.h>

#include <string>

#include "photonlab/logic/logic_sim.hpp"

using namespace photonlab::logic;

namespace {
bool has_issue(const std::vector<Issue>& issues, Issue::Severity sev, const std::string& where_part,
               const std::string& text_part) {
  for (const auto& i : issues)
    if (i.severity == sev && i.where.find(where_part) != std::string::npos &&
        i.text.find(text_part) != std::string::npos)
      return true;
  return false;
}
int value_of(const std::vector<int>& bits, std::size_t from, std::size_t count) {
  int v = 0;
  for (std::size_t k = 0; k < count; ++k) v = 2 * v + bits[from + k];
  return v;
}
}  // namespace

TEST_CASE(
    "4-Bit-Addierer: ideale und regenerierte Gatter rechnen alle 512 Fälle richtig (Abnahme Phase 10)") {
  const LogicCircuit adder = ripple_adder(4);
  const auto vectors = all_vectors(adder.inputs.size());
  REQUIRE(vectors.size() == 512);
  for (const Family family : {Family::ideal, Family::regenerated}) {
    CAPTURE(std::string(to_string(family)));
    SimSettings settings;
    settings.keep_traces = false;
    const SimResult r = simulate(adder, family, vectors, settings);
    for (const auto& i : r.static_issues) CHECK(i.severity != Issue::Severity::error);
    CHECK(r.correct == 512);
    CHECK(r.checked == 512);
    // die ideale Logik ist wirklich die Addition: S + 16 Cout = A + B + Cin
    for (const auto& v : r.vectors) {
      const int a = value_of(v.inputs, 0, 4), b = value_of(v.inputs, 4, 4), cin = v.inputs[8];
      int sum = 0;
      for (int i = 0; i < 4; ++i) sum += v.expected[static_cast<std::size_t>(i)] << i;
      sum += v.expected[4] << 4;
      CHECK(sum == a + b + cin);
    }
  }
}

TEST_CASE("Logik-Ebene: Laufzeit und Energie der regenerierten Gatter") {
  const LogicCircuit adder = ripple_adder(4);
  SimSettings settings;
  settings.keep_traces = false;
  const SimResult r = simulate(adder, Family::regenerated, all_vectors(adder.inputs.size()), settings);
  const double delay = make_block(Family::regenerated, "AND")->delay;
  // Übertragskette: mindestens 6, höchstens 9 Gatterlaufzeiten bis zum letzten Ausgang
  CHECK(r.latency >= 6.0 * delay - 1e-15);
  CHECK(r.latency <= 9.0 * delay + 1e-15);
  CHECK(r.energy_per_vector > 20.0 * 2e-3 * settings.period * 0.99);  // mindestens die statische Leistung
  const SimResult ideal = simulate(adder, Family::ideal, all_vectors(adder.inputs.size()), settings);
  CHECK(ideal.energy_per_vector == 0.0);
}

TEST_CASE("Passive Interferenz-Gatter: Codierungsfehler werden gemeldet und die Ursache benannt") {
  const LogicCircuit ha = half_adder();
  const std::vector<Issue> issues = check(ha, Family::passive);
  CHECK(has_issue(issues, Issue::Severity::error, "xor.A", "Codierung passt nicht"));
  CHECK(has_issue(issues, Issue::Severity::error, "in:A", "unterschiedlicher Codierung"));
  const SimResult r = simulate(ha, Family::passive, all_vectors(2));
  CHECK(r.correct < 4);
  bool explained = false;
  for (const auto& v : r.vectors)
    if (!v.correct) explained = explained || v.first_error.find("xor.") != std::string::npos;
  CHECK(explained);
}

TEST_CASE("Passive UND-Kette: einzeln richtig, in der Kaskade wandern die Pegel (Kaskadierungsproblem)") {
  // Ein einzelnes Interferenz-UND mit Nenneingängen arbeitet (Schwelle zwischen 1/2 und 2 P)
  const SimResult one = simulate(and_chain(1), Family::passive, all_vectors(2));
  CHECK(one.correct == 4);
  // In der Kette ist die "0" eines UND nicht dunkel (P/2): das nächste UND liest daraus falsche Pegel
  const LogicCircuit chain = and_chain(4);
  const std::vector<Issue> issues = check(chain, Family::passive);
  CHECK(has_issue(issues, Issue::Severity::error, "und2.A", "Pegel der 0 zu gro"));
  CHECK(has_issue(issues, Issue::Severity::warning, "und2.A", "regeneriert nicht"));
  const SimResult r = simulate(chain, Family::passive, all_vectors(chain.inputs.size()));
  CHECK(r.correct < r.checked);
  int level_explained = 0;
  for (const auto& v : r.vectors)
    if (!v.correct && v.first_error.find("Pegel") != std::string::npos) ++level_explained;
  CHECK(level_explained == r.checked - r.correct);  // jeder Fehler mit Pegel und Ort erklärt
  // regeneriert: dieselbe Kette rechnet richtig
  const SimResult regen = simulate(chain, Family::regenerated, all_vectors(chain.inputs.size()));
  CHECK(regen.correct == regen.checked);
}

TEST_CASE("SR-Flip-Flop: setzen, halten, rücksetzen - ideal und mit Kerr-bistabilem Ring") {
  const std::vector<std::vector<int>> seq = {{0, 0}, {1, 0}, {0, 0}, {0, 0}, {0, 1}, {0, 0}, {1, 0}, {0, 0}};
  SimSettings settings;
  settings.check_idle_only = true;
  for (const Family family : {Family::ideal, Family::regenerated, Family::passive}) {
    CAPTURE(std::string(to_string(family)));
    const SimResult r = simulate(sr_latch(), family, seq, settings);
    CHECK(r.checked == 5);
    CHECK(r.correct == 5);
    // Qn nach dem Setzen 0, nach dem Rücksetzen 1 (Hysterese hält den Zustand ohne Pulse)
    CHECK(r.vectors[2].expected[0] == 0);
    CHECK(r.vectors[2].decoded[0] == 0);
    CHECK(r.vectors[5].decoded[0] == 1);
    CHECK(r.vectors[7].decoded[0] == 0);
  }
  const BlockPtr kerr = make_block(Family::passive, "SR");
  CHECK(kerr->note.find("Klappen") != std::string::npos);
  CHECK(kerr->static_power > 10e-3);  // Bistabilität braucht hier Haltelicht im Bereich 10 mW (ehrlich teuer)
}

TEST_CASE("Logik-Ebene: offene Eingänge, Fan-out und falsche Anschlüsse") {
  LogicCircuit c;
  c.inputs = {"A"};
  c.outputs = {"Y"};
  c.instances = {{"g", "AND"}};
  c.wires = {{"in:A", "g.A"}, {"g.out", "out:Y"}};
  CHECK(has_issue(check(c, Family::ideal), Issue::Severity::error, "g", "offen"));
  c.wires = {{"in:A", "g.A"}, {"in:A", "g.B"}, {"g.out", "out:Y"}};
  CHECK(has_issue(check(c, Family::ideal), Issue::Severity::info, "in:A", "Fan-out 1:2"));
  c.wires.push_back({"in:A", "g.A"});
  CHECK_THROWS(check(c, Family::ideal));  // doppelt angeschlossen
  CHECK_THROWS(make_block(Family::passive, "NOT"));
  CHECK_THROWS(design("gibt_es_nicht"));
}
