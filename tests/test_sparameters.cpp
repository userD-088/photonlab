// Phase 6: S-Parameter-Extraktion aus dem FDTD und ihre Verwendung als Bauteil in der Schaltung.
#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <filesystem>
#include <memory>

#include "photonlab/circuit/solver.hpp"
#include "photonlab/circuit/sparameter_table.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "photonlab/field/sparameters.hpp"
#include "photonlab/io/project.hpp"

using namespace photonlab;

namespace {
using cplx = std::complex<double>;

// Kleine Tabelle mit bekanntem Verlauf: Zweitor mit Transmission exp(i*phi(lambda)) und Reflexion r
circuit::SParameterTable test_table() {
  circuit::SParameterTable t;
  t.name = "test";
  t.ports = {"a", "b"};
  t.left_port_count = 1;
  for (int k = 0; k <= 10; ++k) {
    const double lambda = (1500.0 + 10.0 * k) * units::nm;
    const double phase = 2.0 * constants::pi * 2.4 * 5e-6 / lambda;
    circuit::CMatrix m(2, 2);
    m << 0.1, 0.9 * std::polar(1.0, phase), 0.9 * std::polar(1.0, phase), 0.1;
    t.wavelengths.push_back(lambda);
    t.s.push_back(m);
  }
  return t;
}
}  // namespace

TEST_CASE("S-Parameter-Bauteil: Interpolation von Betrag und Phase, keine Extrapolation") {
  const circuit::SParameterTable table = test_table();
  // an den Stützstellen exakt
  CHECK((circuit::interpolate(table, table.wavelengths[3]) - table.s[3]).norm() < 1e-14);
  // dazwischen: Betrag bleibt 0,9, Phase liegt zwischen den Nachbarn (lineare Interpolation)
  const double middle = 0.5 * (table.wavelengths[3] + table.wavelengths[4]);
  const cplx t = circuit::interpolate(table, middle)(1, 0);
  CHECK(std::abs(t) == doctest::Approx(0.9).epsilon(1e-12));
  const double expected = std::arg(table.s[3](1, 0)) +
                          0.5 * std::remainder(std::arg(table.s[4](1, 0)) - std::arg(table.s[3](1, 0)), 2 * constants::pi);
  CHECK(std::abs(std::remainder(std::arg(t) - expected, 2 * constants::pi)) < 1e-12);
  CHECK_THROWS_AS(circuit::interpolate(table, 1400e-9), circuit::CircuitError);
  CHECK_THROWS_AS(circuit::interpolate(table, 1700e-9), circuit::CircuitError);

  circuit::SParameterTable broken = table;
  broken.wavelengths[2] = broken.wavelengths[1];
  CHECK_THROWS_AS(circuit::validate(broken), circuit::CircuitError);
  broken = table;
  broken.s[0] = circuit::CMatrix::Zero(3, 3);
  CHECK_THROWS_AS(circuit::validate(broken), circuit::CircuitError);
}

TEST_CASE("S-Parameter-Bauteil in der Schaltung: der Löser liefert die Tabelle zurück") {
  io::Project project;
  project.sparameters = {test_table()};
  const auto registry = io::make_registry(project);
  REQUIRE(registry->find("sparam:test") != nullptr);
  CHECK(registry->find("waveguide") != nullptr);  // eingebaute Bauteile bleiben verfügbar

  circuit::Circuit c;
  c.add("bauteil", "sparam:test");
  const circuit::CompiledCircuit compiled(c, *registry);
  circuit::CVector a = circuit::CVector::Zero(2);
  a(compiled.port_index("bauteil", "a")) = 1.0;
  const double lambda = 1534.5e-9;
  const circuit::Solution solution = compiled.solve(lambda, &a);
  const circuit::CMatrix expected = circuit::interpolate(project.sparameters[0], lambda);
  CHECK(std::abs(solution.b(compiled.port_index("bauteil", "b")) - expected(1, 0)) < 1e-14);
  CHECK(std::abs(solution.b(compiled.port_index("bauteil", "a")) - expected(0, 0)) < 1e-14);
}

TEST_CASE("Gerader Wellenleiter: |S21| = 1, keine Reflexion, Phase nach dem Index der Portmode") {
  for (field::Polarization polarization : {field::Polarization::ez, field::Polarization::hz}) {
    field::FieldScene scene = field::reference_scene("field_gap");
    scene.shapes.resize(1);  // nur der Kern, kein Spalt
    scene.polarization = polarization;
    scene.sparameters.points = 6;
    ThreadPool pool(2);
    const field::SParameterResult r = field::extract_sparameters(scene, &pool);
    REQUIRE(r.port_count() == 2);
    CHECK(r.ports[0] == "ein");
    CHECK(r.left_port_count == 1);
    const double distance = scene.ports[1].x1 - scene.ports[0].x1;
    for (std::size_t k = 0; k < r.wavelengths.size(); ++k) {
      CHECK(std::norm(r.at(k, 1, 0)) == doctest::Approx(1.0).epsilon(1e-3));
      CHECK(std::norm(r.at(k, 0, 0)) < 1e-6);  // < -60 dB
      CHECK(std::abs(r.at(k, 1, 0) - r.at(k, 0, 1)) < 1e-3);
      const double expected_phase = 2.0 * constants::pi * r.n_eff[0][k] * distance / r.wavelengths[k];
      CHECK(std::abs(std::remainder(std::arg(r.at(k, 1, 0)) - expected_phase, 2 * constants::pi)) < 5e-3);
    }
  }
}

TEST_CASE("Abnahme Phase 6: zwei extrahierte Spalte in der Schaltung = FDTD des Gesamtaufbaus innerhalb 1 %") {
  ThreadPool pool(2);
  field::FieldScene single = field::reference_scene("field_gap");
  field::FieldScene pair = field::reference_scene("field_gap_pair");
  single.sparameters.points = pair.sparameters.points = 21;
  const field::SParameterResult one = field::extract_sparameters(single, &pool);
  const field::SParameterResult both = field::extract_sparameters(pair, &pool);

  // Einzelner Spalt: reziprok und passiv, der Rest wird abgestrahlt
  for (std::size_t k = 0; k < one.wavelengths.size(); ++k) {
    CHECK(std::abs(one.at(k, 1, 0) - one.at(k, 0, 1)) < 1e-3);
    CHECK(std::norm(one.at(k, 0, 0)) + std::norm(one.at(k, 1, 0)) < 1.0);
    CHECK(std::norm(one.at(k, 0, 0)) > 0.05);  // deutliche Reflexion: die Kaskade ist kein trivialer Fall
  }

  // Schaltung aus zwei Kopien, direkt verbunden: die Portlinien fallen im Gesamtaufbau zusammen
  io::Project project;
  project.sparameters = {io::make_sparameter_table(single, one, "spalt")};
  const auto registry = io::make_registry(project);
  circuit::Circuit c;
  c.add("a", "sparam:spalt");
  c.add("b", "sparam:spalt");
  c.connect("a.aus", "b.ein");
  const circuit::CompiledCircuit compiled(c, *registry);

  double worst_power = 0.0, worst_field = 0.0;
  for (std::size_t k = 0; k < one.wavelengths.size(); ++k) {
    circuit::CVector a = circuit::CVector::Zero(compiled.port_count());
    a(compiled.port_index("a", "ein")) = 1.0;
    const circuit::Solution solution = compiled.solve(one.wavelengths[k], &a);
    const cplx t_circuit = solution.b(compiled.port_index("b", "aus"));
    const cplx r_circuit = solution.b(compiled.port_index("a", "ein"));
    const cplx t_fdtd = both.at(k, 1, 0), r_fdtd = both.at(k, 0, 0);
    worst_power = std::max({worst_power, std::abs(std::norm(t_circuit) - std::norm(t_fdtd)),
                            std::abs(std::norm(r_circuit) - std::norm(r_fdtd))});
    worst_field = std::max({worst_field, std::abs(t_circuit - t_fdtd), std::abs(r_circuit - r_fdtd)});
  }
  INFO("größte Abweichung: Leistung ", worst_power, ", komplexe Amplitude ", worst_field);
  CHECK(worst_power < 0.01);
  CHECK(worst_field < 0.02);
}

TEST_CASE("Bauteilbibliothek: Projektdatei und .sparam.json verlustfrei, Fingerabdruck reagiert auf die Geometrie") {
  io::Project project;
  project.field = field::reference_scene("field_gap");
  project.sparameters = {test_table()};
  const io::Project loaded = io::project_from_json(io::project_to_json(project));
  REQUIRE(loaded.sparameters.size() == 1);
  CHECK(loaded.sparameters[0] == project.sparameters[0]);
  CHECK(loaded.field.ports == project.field.ports);
  CHECK(loaded.field.sparameters == project.field.sparameters);

  const auto path = std::filesystem::temp_directory_path() / "photonlab_test.sparam.json";
  io::save_sparameters(project.sparameters[0], &project.field, path);
  CHECK(io::load_sparameters(path) == project.sparameters[0]);
  std::filesystem::remove(path);
  CHECK_THROWS_AS(io::sparameter_from_json("{}"), io::IoError);

  const std::string hash = io::sparameter_hash(project.field);
  field::FieldScene renamed = project.field;
  renamed.name = "anderer Name";
  renamed.duration = 1e-12;
  CHECK(io::sparameter_hash(renamed) == hash);  // Name und Laufzeit ändern das Ergebnis nicht
  field::FieldScene moved = project.field;
  moved.shapes[1].x += 25e-9;
  CHECK(io::sparameter_hash(moved) != hash);
  field::FieldScene finer = project.field;
  finer.sparameters.points = 99;
  CHECK(io::sparameter_hash(finer) != hash);

  // doppelte Namen in der Bibliothek werden gemeldet
  project.sparameters.push_back(test_table());
  CHECK_THROWS_AS(io::make_registry(project), io::IoError);
}

TEST_CASE("S-Parameter-Extraktion lehnt ungültige Ports mit Klartext ab") {
  field::FieldScene scene = field::reference_scene("field_gap");
  field::FieldScene broken = scene;
  broken.ports.clear();
  CHECK_THROWS_AS(field::extract_sparameters(broken), field::FieldError);
  broken = scene;
  broken.ports[0].y1 = 0.1e-6;  // reicht in die Randschicht
  CHECK_THROWS_AS(field::extract_sparameters(broken), field::FieldError);
  broken = scene;
  broken.ports[0].x1 = broken.ports[0].x2 = 0.45e-6;  // kein Platz für die Anregung
  CHECK_THROWS_AS(field::extract_sparameters(broken), field::FieldError);
  broken = scene;
  broken.ports[1].name = broken.ports[0].name;
  CHECK_THROWS_AS(field::extract_sparameters(broken), field::FieldError);
  broken = scene;
  broken.ports[0].y1 = 1.3e-6, broken.ports[0].y2 = 1.7e-6;  // Linie zu kurz für die Mode
  CHECK_THROWS_AS(field::extract_sparameters(broken), field::FieldError);
  broken = scene;
  broken.sparameters.wavelength_stop = broken.sparameters.wavelength_start;
  CHECK_THROWS_AS(field::extract_sparameters(broken), field::FieldError);

  // Abbruch liefert kein Ergebnis
  JobControl control;
  control.cancel_requested = true;
  CHECK(field::extract_sparameters(scene, nullptr, &control).cancelled);
}
