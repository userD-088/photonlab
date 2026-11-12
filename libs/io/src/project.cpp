#include "photonlab/io/project.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "photonlab/circuit/reference_designs.hpp"

namespace photonlab::io {

using nlohmann::ordered_json;

namespace {
constexpr const char* format_name = "photonlab.project";
}

std::string project_to_json(const Project& project) {
  ordered_json instances = ordered_json::array();
  for (const auto& inst : project.circuit.instances) {
    ordered_json params = ordered_json::object();
    for (const auto& [key, value] : inst.params) params[key] = value;
    instances.push_back({{"name", inst.name},
                         {"type", inst.type},
                         {"params", std::move(params)},
                         {"grid", {inst.grid_x, inst.grid_y}}});
  }
  ordered_json connections = ordered_json::array();
  for (const auto& c : project.circuit.connections)
    connections.push_back({{"from", circuit::to_string(c.a)}, {"to", circuit::to_string(c.b)}});

  ordered_json variations = ordered_json::array();
  for (const auto& v : project.monte_carlo.variations)
    variations.push_back({{"type", v.type},
                          {"param", v.param},
                          {"sigma", v.sigma},
                          {"relative", v.relative},
                          {"correlated", v.correlated}});

  const ordered_json root = {{"format", format_name},
                             {"version", project_format_version},
                             {"units", "SI"},
                             {"circuit",
                              {{"name", project.circuit.name},
                               {"instances", std::move(instances)},
                               {"connections", std::move(connections)}}},
                             {"simulation",
                              {{"wavelength", project.wavelength},
                               {"sweep",
                                {{"wavelength_start", project.sweep.wavelength_start},
                                 {"wavelength_stop", project.sweep.wavelength_stop},
                                 {"points", project.sweep.points}}},
                               {"monte_carlo",
                                {{"samples", project.monte_carlo.samples},
                                 {"seed", project.monte_carlo.seed},
                                 {"variations", std::move(variations)}}}}}};
  return root.dump(2) + "\n";
}

Project project_from_json(std::string_view text) {
  try {
    const ordered_json root = ordered_json::parse(text);
    if (root.value("format", "") != format_name)
      throw IoError("Keine PhotonLab-Projektdatei (Feld \"format\" fehlt oder ist falsch)");
    const int version = root.value("version", 0);
    if (version != project_format_version)
      throw IoError("Nicht unterstützte Projektversion " + std::to_string(version));

    Project project;
    const auto& circuit_json = root.at("circuit");
    project.circuit.name = circuit_json.value("name", "");
    for (const auto& inst : circuit_json.at("instances")) {
      circuit::ParamMap params;
      if (inst.contains("params"))
        for (const auto& [key, value] : inst.at("params").items()) params[key] = value.get<double>();
      int grid_x = 0, grid_y = 0;
      if (inst.contains("grid")) {
        grid_x = inst.at("grid").at(0).get<int>();
        grid_y = inst.at("grid").at(1).get<int>();
      }
      project.circuit.add(inst.at("name").get<std::string>(), inst.at("type").get<std::string>(),
                          std::move(params), grid_x, grid_y);
    }
    if (circuit_json.contains("connections"))
      for (const auto& c : circuit_json.at("connections"))
        project.circuit.connect(c.at("from").get<std::string>(), c.at("to").get<std::string>());

    if (root.contains("simulation")) {
      const auto& sim = root.at("simulation");
      project.wavelength = sim.value("wavelength", project.wavelength);
      if (sim.contains("sweep")) {
        const auto& sw = sim.at("sweep");
        project.sweep.wavelength_start = sw.value("wavelength_start", project.sweep.wavelength_start);
        project.sweep.wavelength_stop = sw.value("wavelength_stop", project.sweep.wavelength_stop);
        project.sweep.points = sw.value("points", project.sweep.points);
      }
      if (sim.contains("monte_carlo")) {
        const auto& mc = sim.at("monte_carlo");
        project.monte_carlo.samples = mc.value("samples", project.monte_carlo.samples);
        project.monte_carlo.seed = mc.value("seed", project.monte_carlo.seed);
        if (mc.contains("variations"))
          for (const auto& v : mc.at("variations"))
            project.monte_carlo.variations.push_back(
                {v.at("type").get<std::string>(), v.at("param").get<std::string>(),
                 v.at("sigma").get<double>(), v.value("relative", false), v.value("correlated", false)});
      }
    }
    return project;
  } catch (const nlohmann::json::exception& e) {
    throw IoError(std::string("Projektdatei fehlerhaft: ") + e.what());
  } catch (const circuit::CircuitError& e) {
    throw IoError(std::string("Projektdatei fehlerhaft: ") + e.what());
  }
}

Project reference_project(std::string_view name) {
  circuit::ReferenceSetup setup = circuit::reference_setup(name);
  Project project;
  project.circuit = std::move(setup.circuit);
  project.wavelength = setup.wavelength;
  project.sweep = setup.sweep;
  project.monte_carlo = std::move(setup.monte_carlo);
  return project;
}

void save_project(const Project& project, const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw IoError("Datei kann nicht geschrieben werden: " + path.string());
  out << project_to_json(project);
  if (!out.flush()) throw IoError("Schreiben fehlgeschlagen: " + path.string());
}

Project load_project(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw IoError("Datei kann nicht gelesen werden: " + path.string());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return project_from_json(buffer.str());
}

}  // namespace photonlab::io
