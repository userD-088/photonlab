#include "photonlab/io/project.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/logic/reference_gates.hpp"

namespace photonlab::io {

using nlohmann::ordered_json;

namespace {
constexpr const char* format_name = "photonlab.project";
constexpr const char* gate_format_name = "photonlab.gate";

ordered_json gate_to_json(const logic::GateDefinition& gate) {
  ordered_json inputs = ordered_json::array(), outputs = ordered_json::array();
  for (const auto& in : gate.inputs)
    inputs.push_back({{"name", in.name},
                      {"instance", in.instance},
                      {"param", in.param},
                      {"value0", in.value0},
                      {"value1", in.value1},
                      {"encoding", std::string(logic::to_string(in.encoding))}});
  for (const auto& out : gate.outputs)
    outputs.push_back({{"name", out.name},
                       {"instance", out.instance},
                       {"measurement", out.measurement},
                       {"expected", out.expected}});
  return {{"name", gate.name}, {"inputs", std::move(inputs)}, {"outputs", std::move(outputs)}};
}

logic::GateDefinition gate_from_json(const ordered_json& json) {
  logic::GateDefinition gate;
  gate.name = json.value("name", "");
  if (json.contains("inputs"))
    for (const auto& in : json.at("inputs"))
      gate.inputs.push_back({in.at("name").get<std::string>(), in.at("instance").get<std::string>(),
                             in.at("param").get<std::string>(), in.at("value0").get<double>(),
                             in.at("value1").get<double>(),
                             logic::encoding_from_string(in.value("encoding", "intensity"))});
  if (json.contains("outputs"))
    for (const auto& out : json.at("outputs"))
      gate.outputs.push_back({out.at("name").get<std::string>(), out.at("instance").get<std::string>(),
                              out.value("measurement", "Leistung"),
                              out.contains("expected") ? out.at("expected").get<std::vector<int>>()
                                                       : std::vector<int>{}});
  return gate;
}

// JSON kennt weder unendlich noch NaN: null steht für "nicht messbar bzw. unendlich"
ordered_json finite_or_null(double value) {
  return std::isfinite(value) ? ordered_json(value) : ordered_json(nullptr);
}
}  // namespace

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

  const ordered_json logic_json = gate_to_json(project.gate);

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
                                 {"variations", std::move(variations)}}}}},
                             {"logic", logic_json}};
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
    if (root.contains("logic")) project.gate = gate_from_json(root.at("logic"));
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
  if (logic::has_reference_gate(name)) project.gate = logic::reference_gate(name);
  return project;
}

std::string gate_report_to_json(const logic::GateDefinition& gate, const logic::GateReport& report) {
  ordered_json rows = ordered_json::array();
  for (const auto& row : report.table.rows) rows.push_back({{"inputs", row.bits}, {"power", row.power}});
  ordered_json outputs = ordered_json::array();
  for (std::size_t k = 0; k < report.outputs.size() && k < gate.outputs.size(); ++k) {
    const auto& a = report.outputs[k];
    outputs.push_back({{"name", gate.outputs[k].name},
                       {"function", a.function},
                       {"bits", a.bits},
                       {"threshold", a.threshold},
                       {"p1_min", a.p1_min},
                       {"p0_max", a.p0_max},
                       {"contrast_db", finite_or_null(a.contrast_db)},
                       {"extinction_db", finite_or_null(a.extinction_db)}});
  }
  ordered_json criteria = ordered_json::array();
  for (const auto& c : report.criteria)
    criteria.push_back({{"key", c.key},
                        {"name", c.name},
                        {"rating", std::string(logic::to_string(c.rating))},
                        {"value", c.value},
                        {"remark", c.remark}});
  const ordered_json root = {
      {"format", gate_format_name},
      {"version", 1},
      {"units", "SI"},
      {"gate", gate_to_json(gate)},
      {"wavelength", report.wavelength},
      {"truth_table", std::move(rows)},
      {"outputs", std::move(outputs)},
      {"measured",
       {{"latency", report.latency},
        {"input_power", report.input_power},
        {"fan_out", report.fan_out},
        {"isolation_db", finite_or_null(report.isolation_db)},
        {"phase_tolerance", finite_or_null(report.phase_tolerance)},
        {"wavelength_tolerance", report.wavelength_tolerance},
        {"level_slope_db_per_db", report.level_slope_db_per_db},
        {"cascade_possible", report.cascade.possible},
        {"cascade_logic_preserved", report.cascade.logic_preserved}}},
      {"report", {{"real_gate", report.real_gate}, {"good", report.good_count}, {"rated", report.rated_count},
                  {"criteria", std::move(criteria)}}}};
  return root.dump(2) + "\n";
}

void save_gate_report(const logic::GateDefinition& gate, const logic::GateReport& report,
                      const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw IoError("Datei kann nicht geschrieben werden: " + path.string());
  out << gate_report_to_json(gate, report);
  if (!out.flush()) throw IoError("Schreiben fehlgeschlagen: " + path.string());
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
