#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/io/project.hpp"

using namespace photonlab;

TEST_CASE("Projekt: Speichern und Laden ist verlustfrei") {
  io::Project project;
  project.circuit = circuit::reference_mzi_unbalanced();
  project.circuit.find_instance("arm_oben")->params["length"] = 123.456789012345e-6;
  project.wavelength = 1551.23e-9;
  project.sweep = {1.52e-6, 1.58e-6, 777};

  const std::string text = io::project_to_json(project);
  const io::Project loaded = io::project_from_json(text);
  CHECK(io::project_to_json(loaded) == text);
  CHECK(loaded.wavelength == project.wavelength);
  CHECK(loaded.sweep.points == 777);
  CHECK(loaded.circuit.find_instance("arm_oben")->params.at("length") == 123.456789012345e-6);
  CHECK(loaded.circuit.find_instance("det_cross")->grid_y == 5);

  // Gleiche Datei, gleiches Ergebnis: bitgleiche Lösung vor und nach dem Laden
  const auto& reg = circuit::ComponentRegistry::builtin();
  const auto before = circuit::CompiledCircuit(project.circuit, reg).solve(project.wavelength);
  const auto after = circuit::CompiledCircuit(loaded.circuit, reg).solve(loaded.wavelength);
  CHECK(before.b == after.b);
}

TEST_CASE("Projekt: Datei schreiben und lesen") {
  const auto path = std::filesystem::temp_directory_path() / "photonlab_test_projekt.plab.json";
  io::Project project;
  project.circuit = circuit::reference_mzi();
  io::save_project(project, path);
  CHECK(io::project_to_json(io::load_project(path)) == io::project_to_json(project));
  std::filesystem::remove(path);
  CHECK_THROWS_AS(io::load_project(path), io::IoError);
}

TEST_CASE("Projekt: Beispieldateien entsprechen den Referenz-Designs") {
  const std::filesystem::path dir = PHOTONLAB_EXAMPLES_DIR;
  for (const auto& [file, name] : {std::pair{"02_mach_zehnder.plab.json", "mzi"},
                                   std::pair{"02b_mach_zehnder_unbalanciert.plab.json", "mzi_unbalanced"}}) {
    io::Project expected;
    expected.circuit = circuit::reference_design(name);
    CHECK(io::project_to_json(io::load_project(dir / file)) == io::project_to_json(expected));
  }
}

TEST_CASE("Projekt: fehlerhafte Dateien werden mit Klartext abgelehnt") {
  CHECK_THROWS_AS(io::project_from_json("kein json"), io::IoError);
  CHECK_THROWS_AS(io::project_from_json("{}"), io::IoError);
  CHECK_THROWS_AS(io::project_from_json(R"({"format":"photonlab.project","version":99})"), io::IoError);
  CHECK_THROWS_AS(io::project_from_json(R"({"format":"photonlab.project","version":1})"), io::IoError);
  // doppelter Instanzname
  CHECK_THROWS_AS(io::project_from_json(R"({"format":"photonlab.project","version":1,"circuit":{"instances":[
      {"name":"a","type":"laser"},{"name":"a","type":"laser"}]}})"),
                  io::IoError);
}
