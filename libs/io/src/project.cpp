#include "photonlab/io/project.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/field/reference_scenes.hpp"
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
      gate.outputs.push_back(
          {out.at("name").get<std::string>(), out.at("instance").get<std::string>(),
           out.value("measurement", "Leistung"),
           out.contains("expected") ? out.at("expected").get<std::vector<int>>() : std::vector<int>{}});
  return gate;
}

ordered_json scene_to_json(const field::FieldScene& scene) {
  ordered_json shapes = ordered_json::array(), sources = ordered_json::array(),
               monitors = ordered_json::array();
  for (const auto& sh : scene.shapes) {
    ordered_json shape = {{"name", sh.name},
                          {"kind", std::string(field::to_string(sh.kind))},
                          {"x", sh.x},
                          {"y", sh.y},
                          {"width", sh.width},
                          {"height", sh.height},
                          {"rotation", sh.rotation},
                          {"radius", sh.radius},
                          {"inner_radius", sh.inner_radius},
                          {"index", sh.index}};
    // nur wenn nichtlinear: bestehende Dateien bleiben gleich
    if (sh.n2 != 0.0) shape["n2"] = sh.n2;
    if (sh.beta_tpa != 0.0) shape["beta_tpa"] = sh.beta_tpa;
    if (sh.carrier_lifetime != 0.0) shape["carrier_lifetime"] = sh.carrier_lifetime;
    shapes.push_back(std::move(shape));
  }
  for (const auto& q : scene.sources)
    sources.push_back({{"name", q.name},
                       {"kind", std::string(field::to_string(q.kind))},
                       {"x1", q.x1},
                       {"y1", q.y1},
                       {"x2", q.x2},
                       {"y2", q.y2},
                       {"beam_width", q.beam_width},
                       {"signal", std::string(field::to_string(q.signal))},
                       {"wavelength", q.wavelength},
                       {"amplitude", q.amplitude},
                       {"phase", q.phase},
                       {"pulse_duration", q.pulse_duration},
                       {"ramp_periods", q.ramp_periods},
                       {"mode_order", q.mode_order}});
  for (const auto& m : scene.monitors)
    monitors.push_back({{"name", m.name},
                        {"kind", std::string(field::to_string(m.kind))},
                        {"x1", m.x1},
                        {"y1", m.y1},
                        {"x2", m.x2},
                        {"y2", m.y2},
                        {"mode_order", m.mode_order},
                        {"wavelength", m.wavelength}});
  ordered_json ports = ordered_json::array();
  for (const auto& p : scene.ports)
    ports.push_back({{"name", p.name},
                     {"x1", p.x1},
                     {"y1", p.y1},
                     {"x2", p.x2},
                     {"y2", p.y2},
                     {"mode_order", p.mode_order}});
  const auto& sp = scene.sparameters;
  return {{"name", scene.name},
          {"width", scene.width},
          {"height", scene.height},
          {"cell_size", scene.cell_size},
          {"background_index", scene.background_index},
          {"polarization", std::string(field::to_string(scene.polarization))},
          {"boundary_x", std::string(field::to_string(scene.boundary_x))},
          {"boundary_y", std::string(field::to_string(scene.boundary_y))},
          {"pml_cells", scene.pml_cells},
          {"courant", scene.courant},
          {"duration", scene.duration},
          {"shapes", std::move(shapes)},
          {"sources", std::move(sources)},
          {"monitors", std::move(monitors)},
          {"ports", std::move(ports)},
          {"sparameters",
           {{"wavelength_start", sp.wavelength_start},
            {"wavelength_stop", sp.wavelength_stop},
            {"points", sp.points},
            {"source_offset", sp.source_offset},
            {"decay", sp.decay},
            {"max_time", sp.max_time}}}};
}

field::FieldScene scene_from_json(const ordered_json& json) {
  field::FieldScene scene;
  const field::FieldScene defaults;
  scene.name = json.value("name", "");
  scene.width = json.value("width", 0.0);
  scene.height = json.value("height", 0.0);
  scene.cell_size = json.value("cell_size", defaults.cell_size);
  scene.background_index = json.value("background_index", defaults.background_index);
  scene.polarization = field::polarization_from_string(json.value("polarization", "ez"));
  scene.boundary_x = field::boundary_from_string(json.value("boundary_x", "pml"));
  scene.boundary_y = field::boundary_from_string(json.value("boundary_y", "pml"));
  scene.pml_cells = json.value("pml_cells", defaults.pml_cells);
  scene.courant = json.value("courant", defaults.courant);
  scene.duration = json.value("duration", 0.0);
  if (json.contains("shapes"))
    for (const auto& j : json.at("shapes")) {
      field::Shape sh;
      sh.name = j.value("name", "");
      sh.kind = field::shape_kind_from_string(j.value("kind", "rectangle"));
      sh.x = j.at("x").get<double>();
      sh.y = j.at("y").get<double>();
      sh.width = j.value("width", sh.width);
      sh.height = j.value("height", sh.height);
      sh.rotation = j.value("rotation", 0.0);
      sh.radius = j.value("radius", sh.radius);
      sh.inner_radius = j.value("inner_radius", sh.inner_radius);
      sh.index = j.value("index", sh.index);
      sh.n2 = j.value("n2", 0.0);
      sh.beta_tpa = j.value("beta_tpa", 0.0);
      sh.carrier_lifetime = j.value("carrier_lifetime", 0.0);
      scene.shapes.push_back(std::move(sh));
    }
  if (json.contains("sources"))
    for (const auto& j : json.at("sources")) {
      field::Source q;
      q.name = j.value("name", "");
      q.kind = field::source_kind_from_string(j.value("kind", "line"));
      q.x1 = j.at("x1").get<double>();
      q.y1 = j.at("y1").get<double>();
      q.x2 = j.value("x2", q.x1);
      q.y2 = j.value("y2", q.y1);
      q.beam_width = j.value("beam_width", 0.0);
      q.signal = field::signal_kind_from_string(j.value("signal", "continuous"));
      q.wavelength = j.value("wavelength", q.wavelength);
      q.amplitude = j.value("amplitude", q.amplitude);
      q.phase = j.value("phase", 0.0);
      q.pulse_duration = j.value("pulse_duration", q.pulse_duration);
      q.ramp_periods = j.value("ramp_periods", q.ramp_periods);
      q.mode_order = j.value("mode_order", 0);
      scene.sources.push_back(std::move(q));
    }
  if (json.contains("monitors"))
    for (const auto& j : json.at("monitors")) {
      field::Monitor m;
      m.name = j.value("name", "");
      m.kind = field::monitor_kind_from_string(j.value("kind", "point"));
      m.x1 = j.at("x1").get<double>();
      m.y1 = j.at("y1").get<double>();
      m.x2 = j.value("x2", m.x1);
      m.y2 = j.value("y2", m.y1);
      m.mode_order = j.value("mode_order", 0);
      m.wavelength = j.value("wavelength", 0.0);
      scene.monitors.push_back(std::move(m));
    }
  if (json.contains("ports"))
    for (const auto& j : json.at("ports"))
      scene.ports.push_back({j.value("name", ""), j.at("x1").get<double>(), j.at("y1").get<double>(),
                             j.at("x2").get<double>(), j.at("y2").get<double>(), j.value("mode_order", 0)});
  if (json.contains("sparameters")) {
    const auto& j = json.at("sparameters");
    auto& sp = scene.sparameters;
    sp.wavelength_start = j.value("wavelength_start", sp.wavelength_start);
    sp.wavelength_stop = j.value("wavelength_stop", sp.wavelength_stop);
    sp.points = j.value("points", sp.points);
    sp.source_offset = j.value("source_offset", sp.source_offset);
    sp.decay = j.value("decay", sp.decay);
    sp.max_time = j.value("max_time", sp.max_time);
  }
  return scene;
}

ordered_json table_to_json(const circuit::SParameterTable& t) {
  ordered_json matrices = ordered_json::array();
  for (const auto& m : t.s) {
    // Zeile für Zeile, je Eintrag [Realteil, Imaginärteil]
    ordered_json rows = ordered_json::array();
    for (Eigen::Index i = 0; i < m.rows(); ++i) {
      ordered_json row = ordered_json::array();
      for (Eigen::Index j = 0; j < m.cols(); ++j) row.push_back({m(i, j).real(), m(i, j).imag()});
      rows.push_back(std::move(row));
    }
    matrices.push_back(std::move(rows));
  }
  return {{"name", t.name},
          {"label", t.label},
          {"description", t.description},
          {"ports", t.ports},
          {"left_port_count", t.left_port_count},
          {"source_hash", t.source_hash},
          {"wavelengths", t.wavelengths},
          {"s", std::move(matrices)}};
}

circuit::SParameterTable table_from_json(const ordered_json& j) {
  circuit::SParameterTable t;
  t.name = j.at("name").get<std::string>();
  t.label = j.value("label", "");
  t.description = j.value("description", "");
  t.ports = j.at("ports").get<std::vector<std::string>>();
  t.left_port_count = j.value("left_port_count", -1);
  t.source_hash = j.value("source_hash", "");
  t.wavelengths = j.at("wavelengths").get<std::vector<double>>();
  const auto n = static_cast<Eigen::Index>(t.ports.size());
  for (const auto& rows : j.at("s")) {
    circuit::CMatrix m(n, n);
    if (static_cast<Eigen::Index>(rows.size()) != n) throw IoError("S-Matrix hat die falsche Zeilenzahl");
    for (Eigen::Index r = 0; r < n; ++r) {
      const auto& row = rows.at(static_cast<std::size_t>(r));
      if (static_cast<Eigen::Index>(row.size()) != n) throw IoError("S-Matrix hat die falsche Spaltenzahl");
      for (Eigen::Index c = 0; c < n; ++c)
        m(r, c) = {row.at(static_cast<std::size_t>(c)).at(0).get<double>(),
                   row.at(static_cast<std::size_t>(c)).at(1).get<double>()};
    }
    t.s.push_back(std::move(m));
  }
  circuit::validate(t);
  return t;
}

ordered_json cross_section_to_json(const modes::CrossSection& c) {
  return {{"core", c.core},
          {"lower", c.lower},
          {"upper", c.upper},
          {"width", c.width},
          {"height", c.height},
          {"slab_height", c.slab_height},
          {"margin_x", c.margin_x},
          {"margin_y", c.margin_y},
          {"cell_size", c.cell_size},
          {"wavelength", c.wavelength},
          {"polarization", c.polarization == modes::FdePolarization::quasi_te ? "quasi_te" : "quasi_tm"},
          {"modes", c.modes}};
}

modes::CrossSection cross_section_from_json(const ordered_json& j) {
  modes::CrossSection c;
  c.core = j.value("core", c.core);
  c.lower = j.value("lower", c.lower);
  c.upper = j.value("upper", c.upper);
  c.width = j.value("width", c.width);
  c.height = j.value("height", c.height);
  c.slab_height = j.value("slab_height", c.slab_height);
  c.margin_x = j.value("margin_x", c.margin_x);
  c.margin_y = j.value("margin_y", c.margin_y);
  c.cell_size = j.value("cell_size", c.cell_size);
  c.wavelength = j.value("wavelength", c.wavelength);
  const std::string polarization = j.value("polarization", "quasi_te");
  if (polarization != "quasi_te" && polarization != "quasi_tm")
    throw IoError("Unbekannte Polarisation des Querschnitts: '" + polarization + "'");
  c.polarization =
      polarization == "quasi_te" ? modes::FdePolarization::quasi_te : modes::FdePolarization::quasi_tm;
  c.modes = j.value("modes", c.modes);
  return c;
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
  ordered_json library = ordered_json::array();
  for (const auto& table : project.sparameters) library.push_back(table_to_json(table));

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
                             {"logic", logic_json},
                             {"field", scene_to_json(project.field)},
                             {"cross_section", cross_section_to_json(project.cross_section)},
                             {"sparameters", std::move(library)}};
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
    if (root.contains("field")) project.field = scene_from_json(root.at("field"));
    if (root.contains("cross_section"))
      project.cross_section = cross_section_from_json(root.at("cross_section"));
    if (root.contains("sparameters"))
      for (const auto& table : root.at("sparameters")) project.sparameters.push_back(table_from_json(table));
    return project;
  } catch (const nlohmann::json::exception& e) {
    throw IoError(std::string("Projektdatei fehlerhaft: ") + e.what());
  } catch (const circuit::CircuitError& e) {
    throw IoError(std::string("Projektdatei fehlerhaft: ") + e.what());
  } catch (const field::FieldError& e) {
    throw IoError(std::string("Projektdatei fehlerhaft: ") + e.what());
  }
}

std::vector<std::string> reference_project_names() {
  std::vector<std::string> names = field::reference_scene_names();
  for (const auto& name : circuit::reference_design_names()) names.push_back(name);
  return names;
}

Project reference_project(std::string_view name) {
  if (name.rfind("field_", 0) == 0) {
    Project project;
    project.field = field::reference_scene(name);
    project.circuit.name = project.field.name;
    return project;
  }
  circuit::ReferenceSetup setup = circuit::reference_setup(name);
  Project project;
  project.circuit = std::move(setup.circuit);
  project.wavelength = setup.wavelength;
  project.sweep = setup.sweep;
  project.monte_carlo = std::move(setup.monte_carlo);
  if (logic::has_reference_gate(name)) project.gate = logic::reference_gate(name);
  return project;
}

std::shared_ptr<const circuit::ComponentRegistry> make_registry(const Project& project) {
  auto registry = std::make_shared<circuit::ComponentRegistry>(circuit::ComponentRegistry::builtin());
  try {
    for (const auto& table : project.sparameters)
      circuit::register_sparameter_table(*registry, std::make_shared<const circuit::SParameterTable>(table));
  } catch (const circuit::CircuitError& e) {
    throw IoError(std::string("Bauteilbibliothek fehlerhaft: ") + e.what());
  }
  return registry;
}

std::string sparameter_hash(const field::FieldScene& scene) {
  // Name und Laufzeit ändern das Ergebnis nicht; Quellen und Monitore spielen bei der Extraktion keine Rolle
  field::FieldScene canonical = scene;
  canonical.name.clear();
  canonical.duration = 0.0;
  canonical.sources.clear();
  canonical.monitors.clear();
  for (auto& port : canonical.ports) port.name.clear();
  const std::string text = scene_to_json(canonical).dump();
  std::uint64_t hash = 1469598103934665603ull;  // FNV-1a, 64 Bit
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  char buffer[24];
  std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
  return buffer;
}

circuit::SParameterTable make_sparameter_table(const field::FieldScene& scene,
                                               const field::SParameterResult& result,
                                               const std::string& name) {
  circuit::SParameterTable table;
  table.name = name;
  table.label = name;
  char text[256];
  std::snprintf(text, sizeof text, "Aus dem 2D-FDTD extrahiert: Szene '%s', Gitter %.0f nm, Polarisation %s.",
                scene.name.c_str(), scene.cell_size * 1e9,
                scene.polarization == field::Polarization::ez ? "Ez" : "Hz");
  table.description = text;
  table.ports = result.ports;
  table.left_port_count = result.left_port_count;
  table.wavelengths = result.wavelengths;
  const auto n = static_cast<Eigen::Index>(result.ports.size());
  for (const auto& values : result.s) {
    circuit::CMatrix m(n, n);
    for (Eigen::Index i = 0; i < n; ++i)
      for (Eigen::Index j = 0; j < n; ++j) m(i, j) = values[static_cast<std::size_t>(i * n + j)];
    table.s.push_back(std::move(m));
  }
  table.source_hash = sparameter_hash(scene);
  try {
    circuit::validate(table);
  } catch (const circuit::CircuitError& e) {
    throw IoError(e.what());
  }
  return table;
}

std::string sparameter_to_json(const circuit::SParameterTable& table, const field::FieldScene* scene) {
  ordered_json root = {
      {"format", "photonlab.sparam"}, {"version", 1}, {"units", "SI"}, {"table", table_to_json(table)}};
  if (scene) root["scene"] = scene_to_json(*scene);
  return root.dump(2) + "\n";
}

circuit::SParameterTable sparameter_from_json(std::string_view text) {
  try {
    const ordered_json root = ordered_json::parse(text);
    if (root.value("format", "") != "photonlab.sparam")
      throw IoError("Keine PhotonLab-S-Parameter-Datei (Feld \"format\" fehlt oder ist falsch)");
    if (root.value("version", 0) != 1) throw IoError("Nicht unterstützte Version der S-Parameter-Datei");
    return table_from_json(root.at("table"));
  } catch (const nlohmann::json::exception& e) {
    throw IoError(std::string("S-Parameter-Datei fehlerhaft: ") + e.what());
  } catch (const circuit::CircuitError& e) {
    throw IoError(std::string("S-Parameter-Datei fehlerhaft: ") + e.what());
  }
}

void save_sparameters(const circuit::SParameterTable& table, const field::FieldScene* scene,
                      const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw IoError("Datei kann nicht geschrieben werden: " + path.string());
  out << sparameter_to_json(table, scene);
  if (!out.flush()) throw IoError("Schreiben fehlgeschlagen: " + path.string());
}

circuit::SParameterTable load_sparameters(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw IoError("Datei kann nicht gelesen werden: " + path.string());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return sparameter_from_json(buffer.str());
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
  const ordered_json root = {{"format", gate_format_name},
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
                             {"report",
                              {{"real_gate", report.real_gate},
                               {"good", report.good_count},
                               {"rated", report.rated_count},
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
