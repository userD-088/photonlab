#include "logic_lab.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>

#include "file_dialog.hpp"
#include "implot.h"
#include "photonlab/core/units.hpp"
#include "photonlab/logic/reference_gates.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

const circuit::ComponentRegistry& registry() { return circuit::ComponentRegistry::builtin(); }

ImVec4 rating_color(logic::Rating rating) {
  switch (rating) {
    case logic::Rating::good:
      return ui_color_good;
    case logic::Rating::limited:
      return ui_color_limited;
    case logic::Rating::bad:
      return ui_color_bad;
    case logic::Rating::not_rated:
      return ui_color_neutral;
  }
  return ui_color_neutral;
}

// Ampel: gefüllter Kreis in der Zeilenhöhe des Textes
void traffic_light(logic::Rating rating) {
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddCircleFilled({pos.x + size * 0.5f, pos.y + size * 0.5f}, size * 0.38f,
                                              ImGui::ColorConvertFloat4ToU32(rating_color(rating)));
  ImGui::Dummy(ImVec2(size, size));
}

const circuit::ParamSpec* find_spec(const circuit::Circuit& circuit, const std::string& instance_name,
                                    const std::string& param) {
  const circuit::Instance* instance = circuit.find_instance(instance_name);
  const circuit::ComponentType* type = instance ? registry().find(instance->type) : nullptr;
  return type ? type->find_param(param) : nullptr;
}

// Auswahlliste aller Instanzen; gibt true zurück, wenn sich die Auswahl geändert hat
bool instance_combo(const char* id, const circuit::Circuit& circuit, std::string* selected,
                    bool need_params) {
  bool changed = false;
  if (ImGui::BeginCombo(id, selected->c_str())) {
    for (const auto& instance : circuit.instances) {
      const circuit::ComponentType* type = registry().find(instance.type);
      if (!type || (need_params && type->params.empty())) continue;
      if (ImGui::Selectable(instance.name.c_str(), instance.name == *selected) &&
          instance.name != *selected) {
        *selected = instance.name;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

}  // namespace

LogicLab::LogicLab(ThreadPool& pool, ChipDesigner& designer) : pool_(pool), designer_(designer) {
  designer_.on_save_gate = [this](const std::string& path) {
    if (result_.key.empty() || !result_.error.empty()) {
      designer_.show_status("Kein gültiges Zeugnis zum Exportieren.", true);
      return;
    }
    try {
      io::save_gate_report(result_.gate, result_.report, ui_path_from_utf8(path));
      designer_.show_status("Gatter exportiert: " + path);
    } catch (const std::exception& e) {
      designer_.show_status(e.what(), true);
    }
  };
}

LogicLab::~LogicLab() {
  designer_.on_save_gate = nullptr;
  if (future_.valid()) future_.wait();
}

// Alles, wovon das Zeugnis abhängt: Schaltung (über die Revision), Wellenlänge und Gatter-Definition
std::string LogicLab::key() const {
  const io::Project& project = designer_.project();
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%llu/%.17g", static_cast<unsigned long long>(designer_.revision()),
                project.wavelength);
  std::string key = buffer;
  for (const auto& in : project.gate.inputs) {
    std::snprintf(buffer, sizeof buffer, "/%.17g/%.17g/%d", in.value0, in.value1,
                  static_cast<int>(in.encoding));
    key += "|" + in.name + "/" + in.instance + "/" + in.param + buffer;
  }
  for (const auto& out : project.gate.outputs) {
    key += "|" + out.name + "/" + out.instance + "/" + out.measurement + "/";
    for (int bit : out.expected) key += bit ? '1' : '0';
  }
  return key;
}

void LogicLab::update() {
  if (future_.valid() && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    result_ = future_.get();
  if (future_.valid()) return;
  const io::Project& project = designer_.project();
  const std::string current = key();
  if (project.gate.empty() || result_.key == current) return;

  // Die Charakterisierung läuft als Job; er besitzt Kopien von Schaltung und Definition.
  future_ = pool_.submit(
      [circuit = project.circuit, gate = project.gate, wavelength = project.wavelength, current] {
        Result result;
        result.key = current;
        result.gate = gate;
        try {
          result.report = logic::characterize(circuit, registry(), gate, wavelength);
        } catch (const std::exception& e) {
          result.error = e.what();
        }
        return result;
      });
}

void LogicLab::draw() {
  update();
  ImGui::Begin(window_name);
  io::Project& project = designer_.project();
  if (project.gate.empty()) {
    draw_empty_state();
    ImGui::End();
    return;
  }

  // Links Definition, Messung und Optimierer untereinander (scrollbar), rechts das Zeugnis
  const float width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x;
  ImGui::BeginChild("definition", ImVec2(width * 0.56f, 0.0f), ImGuiChildFlags_Borders);
  draw_definition();
  ImGui::Spacing();
  draw_truth_table();
  draw_optimizer();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("zeugnis", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
  draw_report();
  ImGui::EndChild();
  ImGui::End();
}

void LogicLab::draw_empty_state() {
  io::Project& project = designer_.project();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
  ImGui::TextUnformatted("Das Logik-Labor prüft, ob die Schaltung des Chip-Designers als Logikgatter taugt.");
  ImGui::TextDisabled(
      "Ein logischer Eingang schaltet einen Parameter zwischen zwei Werten um (zum Beispiel die Phase oder "
      "Leistung "
      "eines Lasers), ein Ausgang ist die Leistung an einem Detektor. PhotonLab simuliert alle "
      "Kombinationen, "
      "misst das Kontrastverhältnis und stellt ein Zeugnis nach den Anforderungen an ein Logikelement aus.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (ImGui::Button("Eigenes Gatter aus dieser Schaltung definieren")) {
    project.gate.name = project.circuit.name;
    designer_.mark_dirty();
    // sinnvolle Vorbelegung: erster Laser als Eingang, erster Detektor als Ausgang
    for (const auto& instance : project.circuit.instances) {
      const circuit::ComponentType* type = registry().find(instance.type);
      if (!type) continue;
      if (project.gate.inputs.empty() && type->find_param("power") && type->port_names.size() == 1)
        project.gate.inputs.push_back({"A", instance.name, "power", 0.0,
                                       type->find_param("power")->default_value, logic::Encoding::intensity});
      if (project.gate.outputs.empty() && instance.type == "detector")
        project.gate.outputs.push_back({"Y", instance.name, "Leistung", {}});
    }
    if (project.gate.inputs.empty() && project.gate.outputs.empty()) project.gate.name = "Gatter";
  }
  ImGui::Spacing();
  ImGui::SeparatorText("Beispiele laden");
  struct Example {
    const char* key;
    const char* label;
    const char* text;
  };
  const Example examples[] = {
      {"xor_interference", "Interferenz-XOR/XNOR", "Phasencodiert, hoher Kontrast, nicht kaskadierbar."},
      {"and_interference", "Interferenz-AND", "Intensitätscodiert, nur 6 dB Kontrast: ein Negativbeispiel."},
      {"xor_directed", "Directed Logic mit Ringen", "Elektrisch gesteuert, realistisch, aber schmalbandig."}};
  for (const auto& example : examples) {
    if (ImGui::Button(example.label)) designer_.load_example(example.key);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", example.text);
  }
}

void LogicLab::draw_definition() {
  io::Project& project = designer_.project();
  logic::GateDefinition& gate = project.gate;
  const circuit::Circuit& circuit = project.circuit;
  bool changed = false;

  ImGui::SeparatorText("Gatter-Definition");
  char name[96];
  std::snprintf(name, sizeof name, "%s", gate.name.c_str());
  ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4.0f);
  if (ImGui::InputText("Name", name, sizeof name)) {
    gate.name = name;
    changed = true;
  }

  // --- Eingänge ---
  ImGui::TextDisabled("Eingänge: Bit 0 und Bit 1 setzen einen Parameter");
  int remove_input = -1;
  if (ImGui::BeginTable("eingaenge", 7, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Bauteil", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("Bit 0", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Bit 1", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Codierung", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int i = 0; i < static_cast<int>(gate.inputs.size()); ++i) {
      logic::LogicInput& in = gate.inputs[static_cast<std::size_t>(i)];
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char buffer[32];
      std::snprintf(buffer, sizeof buffer, "%s", in.name.c_str());
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::InputText("##name", buffer, sizeof buffer)) in.name = buffer, changed = true;

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      if (instance_combo("##instanz", circuit, &in.instance, true)) {
        // neuer Baustein: ersten Parameter und dessen Bereich vorschlagen
        const circuit::ComponentType* type = registry().find(circuit.find_instance(in.instance)->type);
        const circuit::ParamSpec& spec = type->params.front();
        in.param = spec.name;
        in.value0 = spec.min_value > 0.0 ? spec.min_value : 0.0;
        in.value1 = spec.default_value != in.value0 ? spec.default_value : spec.max_value;
        changed = true;
      }

      const circuit::Instance* instance = circuit.find_instance(in.instance);
      const circuit::ComponentType* type = instance ? registry().find(instance->type) : nullptr;
      const circuit::ParamSpec* spec = type ? type->find_param(in.param) : nullptr;
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##parameter", spec ? spec->label.c_str() : in.param.c_str())) {
        if (type)
          for (const auto& p : type->params)
            if (ImGui::Selectable(p.label.c_str(), p.name == in.param) && p.name != in.param) {
              in.param = p.name;
              in.value0 = p.display_unit == "rad" ? 0.0 : std::max(p.min_value, 0.0);
              in.value1 = p.display_unit == "rad"        ? constants::pi
                          : p.default_value != in.value0 ? p.default_value
                                                         : p.max_value;
              if (p.display_unit == "rad") in.encoding = logic::Encoding::phase;
              changed = true;
            }
        ImGui::EndCombo();
      }

      // Werte in der Anzeige-Einheit des Parameters
      const double scale = spec ? spec->display_scale : 1.0;
      const std::string format = "%.4g " + (spec ? spec->display_unit : std::string());
      for (double* value : {&in.value0, &in.value1}) {
        ImGui::TableNextColumn();
        ImGui::PushID(value);
        double shown = *value / scale;
        const double low = spec ? spec->min_value / scale : 0.0, high = spec ? spec->max_value / scale : 1.0;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragScalar("##wert", ImGuiDataType_Double, &shown,
                              static_cast<float>(std::max(std::abs(shown) * 0.01, 1e-3)), &low, &high,
                              format.c_str(), ImGuiSliderFlags_AlwaysClamp)) {
          *value = std::clamp(shown, low, high) * scale;
          changed = true;
        }
        ImGui::PopID();
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##codierung", std::string(logic::label(in.encoding)).c_str())) {
        for (logic::Encoding e :
             {logic::Encoding::intensity, logic::Encoding::phase, logic::Encoding::electrical})
          if (ImGui::Selectable(std::string(logic::label(e)).c_str(), e == in.encoding) && e != in.encoding) {
            in.encoding = e;
            changed = true;
          }
        ImGui::EndCombo();
      }
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("x")) remove_input = i;
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (remove_input >= 0) {
    gate.inputs.erase(gate.inputs.begin() + remove_input);
    for (auto& out : gate.outputs) out.expected.clear();  // die Tabellen passen nicht mehr zur Eingangszahl
    changed = true;
  }
  if (gate.inputs.size() < logic::max_inputs && ImGui::SmallButton("+ Eingang")) {
    logic::LogicInput in;
    in.name = std::string(1, static_cast<char>('A' + gate.inputs.size()));
    for (const auto& instance : circuit.instances) {
      const circuit::ComponentType* type = registry().find(instance.type);
      const bool used =
          std::any_of(gate.inputs.begin(), gate.inputs.end(),
                      [&](const logic::LogicInput& other) { return other.instance == instance.name; });
      if (!type || type->params.empty() || used) continue;
      in.instance = instance.name;
      in.param = type->params.front().name;
      in.value0 = std::max(type->params.front().min_value, 0.0);
      in.value1 = type->params.front().default_value != in.value0 ? type->params.front().default_value
                                                                  : type->params.front().max_value;
      break;
    }
    gate.inputs.push_back(in);
    for (auto& out : gate.outputs) out.expected.clear();
    changed = true;
  }

  // --- Ausgänge ---
  ImGui::Spacing();
  ImGui::TextDisabled("Ausgänge: Leistung an einem Detektor");
  int remove_output = -1;
  const std::vector<std::string> functions = logic::function_names(gate.inputs.size());
  if (ImGui::BeginTable("ausgaenge", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Bauteil", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Erwartete Funktion", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int i = 0; i < static_cast<int>(gate.outputs.size()); ++i) {
      logic::LogicOutput& out = gate.outputs[static_cast<std::size_t>(i)];
      ImGui::PushID(100 + i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char buffer[32];
      std::snprintf(buffer, sizeof buffer, "%s", out.name.c_str());
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::InputText("##name", buffer, sizeof buffer)) out.name = buffer, changed = true;
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      if (instance_combo("##instanz", circuit, &out.instance, false)) changed = true;
      ImGui::TableNextColumn();
      const std::string current =
          out.expected.empty() ? std::string("(erkennen)") : logic::identify_function(out.expected);
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##funktion", current.empty() ? "(eigene Tabelle)" : current.c_str())) {
        if (ImGui::Selectable("(erkennen)", out.expected.empty()) && !out.expected.empty()) {
          out.expected.clear();
          changed = true;
        }
        for (const auto& f : functions)
          if (ImGui::Selectable(f.c_str(), f == current) && f != current) {
            out.expected = logic::function_bits(f, gate.inputs.size());
            changed = true;
          }
        ImGui::EndCombo();
      }
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("x")) remove_output = i;
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (remove_output >= 0) {
    gate.outputs.erase(gate.outputs.begin() + remove_output);
    changed = true;
  }
  if (ImGui::SmallButton("+ Ausgang")) {
    logic::LogicOutput out;
    out.name = gate.outputs.empty() ? "Y" : "Y" + std::to_string(gate.outputs.size() + 1);
    for (const auto& instance : circuit.instances) {
      const bool used =
          std::any_of(gate.outputs.begin(), gate.outputs.end(),
                      [&](const logic::LogicOutput& other) { return other.instance == instance.name; });
      if (instance.type == "detector" && !used) {
        out.instance = instance.name;
        break;
      }
    }
    gate.outputs.push_back(out);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Definition entfernen")) {
    gate = {};
    changed = true;
  }
  if (changed) designer_.mark_dirty();
}

void LogicLab::draw_optimizer() {
  io::Project& project = designer_.project();
  if (project.gate.empty()) return;
  const circuit::Circuit& circuit = project.circuit;
  ImGui::Spacing();
  ImGui::SeparatorText("Optimierer");
  ImGui::TextDisabled("Stimmt einen Parameter auf den besten Kontrast ab");

  // Vorbelegung: erster Phasen-Parameter, der kein logischer Eingang ist
  auto is_input = [&](const std::string& instance, const std::string& param) {
    return std::any_of(
        project.gate.inputs.begin(), project.gate.inputs.end(),
        [&](const logic::LogicInput& in) { return in.instance == instance && in.param == param; });
  };
  if (!find_spec(circuit, optimize_instance_, optimize_param_)) {
    optimize_instance_.clear();
    // zuerst Bauteile, die kein logischer Eingang sind (typisch: die Bias-Phase), dann alle
    for (bool skip_input_instances : {true, false})
      for (const auto& instance : circuit.instances) {
        const circuit::ComponentType* type = registry().find(instance.type);
        const bool drives_input =
            std::any_of(project.gate.inputs.begin(), project.gate.inputs.end(),
                        [&](const logic::LogicInput& in) { return in.instance == instance.name; });
        if (!type || (skip_input_instances && drives_input)) continue;
        for (const auto& p : type->params)
          if (optimize_instance_.empty() && !is_input(instance.name, p.name) &&
              (p.display_unit == "rad" || instance.type == "coupler")) {
            optimize_instance_ = instance.name;
            optimize_param_ = p.name;
          }
      }
    if (optimize_instance_.empty()) {
      ImGui::TextDisabled("Kein abstimmbarer Parameter gefunden.");
      return;
    }
  }

  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  ImGui::SetNextItemWidth(half);
  if (instance_combo("##optinstanz", circuit, &optimize_instance_, true))
    optimize_param_ = registry().find(circuit.find_instance(optimize_instance_)->type)->params.front().name;
  const circuit::ComponentType* type = registry().find(circuit.find_instance(optimize_instance_)->type);
  const circuit::ParamSpec* spec = type->find_param(optimize_param_);
  if (!spec) {
    spec = &type->params.front();
    optimize_param_ = spec->name;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(half);
  if (ImGui::BeginCombo("##optparameter", spec->label.c_str())) {
    for (const auto& p : type->params)
      if (ImGui::Selectable(p.label.c_str(), p.name == optimize_param_)) optimize_param_ = p.name;
    ImGui::EndCombo();
  }
  spec = type->find_param(optimize_param_);

  // Suchbereich in Anzeige-Einheiten; Vorschlag wie bei der Kennlinie
  const std::string owner = optimize_instance_ + "/" + optimize_param_;
  const double scale = spec->display_scale;
  if (owner != optimize_range_owner_) {
    optimize_range_owner_ = owner;
    const double low = spec->min_value / scale, high = spec->max_value / scale;
    const double current = param_value(*circuit.find_instance(optimize_instance_), *spec) / scale;
    if (spec->display_unit == "rad") {
      optimize_min_ = -constants::pi, optimize_max_ = constants::pi;
    } else if (high - low <= 1000.0) {
      optimize_min_ = low, optimize_max_ = high;
    } else {
      optimize_min_ = std::max(low, 0.5 * current), optimize_max_ = std::min(high, 1.5 * current);
    }
  }
  const float field = ImGui::GetFontSize() * 5.5f;
  const float speed = static_cast<float>(std::max((optimize_max_ - optimize_min_) * 0.005, 1e-4));
  ImGui::SetNextItemWidth(field);
  ImGui::DragScalar("von", ImGuiDataType_Double, &optimize_min_, speed, nullptr, nullptr, "%.4g");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field);
  ImGui::DragScalar("bis", ImGuiDataType_Double, &optimize_max_, speed, nullptr, nullptr, "%.4g");
  ImGui::SameLine();
  ImGui::TextDisabled("%s", spec->display_unit.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Optimieren")) {
    optimize_error_.clear();
    optimize_result_ = {};
    optimize_result_owner_ = owner;
    try {
      const double low = std::max(optimize_min_ * scale, spec->min_value);
      const double high = std::min(optimize_max_ * scale, spec->max_value);
      optimize_result_ = logic::optimize_parameter(circuit, registry(), project.gate, project.wavelength,
                                                   optimize_instance_, optimize_param_, low, high);
      if (!optimize_result_.found) optimize_error_ = "In diesem Bereich stimmt die Logik nirgends.";
      fit_optimizer_ = true;
    } catch (const std::exception& e) {
      optimize_error_ = e.what();
    }
  }
  if (!optimize_error_.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_bad, "%s", optimize_error_.c_str());
    ImGui::PopTextWrapPos();
  }

  if (optimize_result_owner_ == owner && !optimize_result_.x.empty()) {
    if (optimize_result_.found) {
      char label[128];
      std::snprintf(label, sizeof label, "Optimum übernehmen: %.5g %s (%s)", optimize_result_.best_x / scale,
                    spec->display_unit.c_str(), logic::format_db(optimize_result_.best_contrast_db).c_str());
      if (ImGui::Button(label)) {
        project.circuit.find_instance(optimize_instance_)->params[optimize_param_] =
            std::clamp(optimize_result_.best_x, spec->min_value, spec->max_value);
        designer_.touch_circuit();
      }
    }
    static std::vector<double> xs;
    xs.resize(optimize_result_.x.size());
    std::transform(optimize_result_.x.begin(), optimize_result_.x.end(), xs.begin(),
                   [&](double x) { return x / scale; });
    if (ImPlot::BeginPlot("##optimierer", ImVec2(-1.0f, ImGui::GetFontSize() * 11.0f),
                          ImPlotFlags_NoLegend)) {
      const std::string x_label =
          spec->display_unit.empty() ? spec->label : spec->label + " [" + spec->display_unit + "]";
      ImPlot::SetupAxes(x_label.c_str(), "Kontrast [dB]");
      // feste Achsen nach jeder neuen Rechnung: ganzer Suchbereich, 0 bis 100 dB
      ImPlot::SetupAxesLimits(xs.front(), xs.back(), 0.0, 105.0,
                              fit_optimizer_ ? ImPlotCond_Always : ImPlotCond_Once);
      fit_optimizer_ = false;
      // Lücken (NaN) zeigen, wo die Logik nicht stimmt
      ImPlot::PlotLine("##kontrast", xs.data(), optimize_result_.contrast_db.data(),
                       static_cast<int>(xs.size()));
      const double current = param_value(*circuit.find_instance(optimize_instance_), *spec) / scale;
      ImPlot::PlotInfLines("##aktuell", &current, 1);
      ImPlot::EndPlot();
    }
    ImGui::TextDisabled("Kurve auf 100 dB begrenzt; Lücken: Logik stimmt dort nicht.");
  }
}

void LogicLab::draw_truth_table() {
  ImGui::SeparatorText("Wahrheitstabelle");
  const bool stale = result_.key != key();
  if (result_.key.empty()) {
    ImGui::TextDisabled("Wird gerechnet ...");
    return;
  }
  if (!result_.error.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_bad, "%s", result_.error.c_str());
    ImGui::PopTextWrapPos();
    return;
  }
  if (stale) ImGui::TextDisabled("Wird neu gerechnet ...");
  const logic::GateReport& report = result_.report;
  const logic::GateDefinition& gate = result_.gate;
  ImGui::TextDisabled("gemessen bei %.3f nm", report.wavelength / units::nm);

  const int input_count = static_cast<int>(gate.inputs.size());
  const int output_count = static_cast<int>(gate.outputs.size());
  if (ImGui::BeginTable(
          "wahrheit", input_count + output_count,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
    for (const auto& in : gate.inputs)
      ImGui::TableSetupColumn(in.name.c_str(), ImGuiTableColumnFlags_WidthFixed);
    for (const auto& out : gate.outputs)
      ImGui::TableSetupColumn(out.name.c_str(), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (std::size_t row = 0; row < report.table.rows.size(); ++row) {
      ImGui::TableNextRow();
      for (int bit : report.table.rows[row].bits) {
        ImGui::TableNextColumn();
        ImGui::Text(" %d ", bit);
      }
      for (std::size_t k = 0; k < report.outputs.size(); ++k) {
        const logic::OutputAnalysis& a = report.outputs[k];
        const double p = report.table.rows[row].power[k];
        const bool decided = p > a.threshold;
        const bool correct = decided == (a.bits[row] != 0);
        ImGui::TableNextColumn();
        // Schmaler Balken unter dem Text: Leistung relativ zum größten Pegel, Strich am Entscheidungspegel
        const float fraction = a.p_max > 0.0 ? static_cast<float>(p / a.p_max) : 0.0f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float bar_width = ImGui::GetContentRegionAvail().x, text_height = ImGui::GetTextLineHeight();
        const float top = pos.y + text_height, bottom = top + 3.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({pos.x, top}, {pos.x + bar_width, bottom}, IM_COL32(38, 49, 66, 255));
        draw->AddRectFilled(
            {pos.x, top}, {pos.x + bar_width * fraction, bottom},
            ImGui::ColorConvertFloat4ToU32(correct ? ImVec4(0.53f, 0.66f, 0.89f, 1.0f) : ui_color_bad));
        if (a.p_max > 0.0) {
          const float x = pos.x + bar_width * static_cast<float>(a.threshold / a.p_max);
          draw->AddLine({x, top - 3.0f}, {x, bottom + 2.0f}, IM_COL32(255, 255, 255, 200), 1.0f);
        }
        if (correct)
          ImGui::Text(" %d   %s", decided ? 1 : 0, format_si(p, "W").c_str());
        else
          ImGui::TextColored(ui_color_bad, " %d   %s  FALSCH", decided ? 1 : 0, format_si(p, "W").c_str());
        ImGui::Dummy(ImVec2(1.0f, 4.0f));
      }
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  for (std::size_t k = 0; k < report.outputs.size(); ++k) {
    const logic::OutputAnalysis& a = report.outputs[k];
    ImGui::Text("%s: %s", gate.outputs[k].name.c_str(),
                a.function.empty() ? "unbekannte Funktion" : a.function.c_str());
    if (!a.has_expected) {
      ImGui::SameLine();
      ImGui::TextDisabled("(erkannt)");
    }
    ImGui::TextDisabled("  Schwelle %s | Kontrast %s", format_si(a.threshold, "W").c_str(),
                        logic::format_db(a.contrast_db).c_str());
  }

  // Kaskaden-Test: nur wenn er sich bauen ließ
  if (report.cascade.possible && !report.cascade.table.rows.empty()) {
    ImGui::Spacing();
    ImGui::SeparatorText("Kaskaden-Test");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", report.cascade.reason.c_str());
    ImGui::PopTextWrapPos();
    const logic::GateDefinition& two = report.cascade.gate;
    const double threshold = report.outputs[0].threshold;
    if (ImGui::BeginTable(
            "kaskade", static_cast<int>(two.inputs.size()) + 1,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
      for (const auto& in : two.inputs)
        ImGui::TableSetupColumn(in.name.c_str(), ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn(two.outputs[0].name.c_str(), ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      for (std::size_t row = 0; row < report.cascade.table.rows.size(); ++row) {
        ImGui::TableNextRow();
        for (int bit : report.cascade.table.rows[row].bits) {
          ImGui::TableNextColumn();
          ImGui::Text(" %d ", bit);
        }
        const double p = report.cascade.table.rows[row].power[0];
        const bool decided = p > threshold;
        const bool correct = decided == (two.outputs[0].expected[row] != 0);
        ImGui::TableNextColumn();
        if (correct)
          ImGui::Text(" %d  %s", decided ? 1 : 0, format_si(p, "W").c_str());
        else
          ImGui::TextColored(ui_color_bad, " %d  %s  FALSCH", decided ? 1 : 0, format_si(p, "W").c_str());
      }
      ImGui::EndTable();
    }
  }
}

void LogicLab::draw_report() {
  ImGui::SeparatorText("Gatter-Zeugnis");
  if (result_.key.empty()) {
    ImGui::TextDisabled("Wird gerechnet ...");
    return;
  }
  if (!result_.error.empty()) {
    ImGui::TextDisabled("Kein Zeugnis: die Definition ist fehlerhaft.");
    return;
  }
  const logic::GateReport& report = result_.report;
  ImGui::PushTextWrapPos(0.0f);
  if (report.real_gate)
    ImGui::TextColored(ui_color_good, "Erfüllt alle bewerteten Anforderungen an ein Logikelement.");
  else
    ImGui::TextColored(
        ui_color_limited,
        "%d von %d Kriterien erfüllt: Demonstrator, kein beliebig kaskadierbares Logikelement.",
        report.good_count, report.rated_count);
  ImGui::PopTextWrapPos();
  if (ImGui::SmallButton("Als .gate.json exportieren"))
    show_file_dialog(DialogKind::save_gate, designer_.window());
  ImGui::Spacing();

  for (const auto& c : report.criteria) {
    traffic_light(c.rating);
    ImGui::SameLine();
    ImGui::TextUnformatted(c.name.c_str());
    if (!c.value.empty() && c.value != "-") {
      ImGui::SameLine();
      ImGui::TextColored(rating_color(c.rating), "%s", c.value.c_str());
    }
    ImGui::Indent(ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", c.remark.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Unindent(ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.x);
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Kriterien nach D. A. B. Miller, Nature Photonics 2010. Die Schwellen der Ampeln sind "
      "Festlegungen von PhotonLab (docs/physics/logic.md).");
  ImGui::PopTextWrapPos();
}

}  // namespace photonlab::gui
