#include "logic_view.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <map>

#include "file_dialog.hpp"
#include "photonlab/core/units.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

const circuit::ComponentRegistry& registry() { return active_registry(); }

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

double param_of(const circuit::Instance& instance, const std::string& param) {
  const auto it = instance.params.find(param);
  if (it != instance.params.end()) return it->second;
  const circuit::ComponentType* type = registry().find(instance.type);
  const circuit::ParamSpec* spec = type ? type->find_param(param) : nullptr;
  return spec ? spec->default_value : 0.0;
}

// Mehrbit-Wörter aus Namen wie A0 ... A7: Buchstaben-Vorsilbe plus Stellenzahl. Namen ohne Zahl (Cin, Cout)
// bleiben einzelne Bits.
struct Word {
  std::string name;
  std::vector<std::pair<int, std::size_t>> bits;  // (Stelle, Index), höchste Stelle zuerst
};
template <class Item>
std::vector<Word> words_of(const std::vector<Item>& items) {
  std::vector<Word> words;
  std::map<std::string, std::size_t> by_prefix;
  for (std::size_t k = 0; k < items.size(); ++k) {
    const std::string& name = items[k].name;
    std::size_t split = name.size();
    while (split > 0 && std::isdigit(static_cast<unsigned char>(name[split - 1]))) --split;
    if (split == 0 || split == name.size() || name.size() - split > 2) {
      words.push_back({name, {{0, k}}});
      continue;
    }
    const std::string prefix = name.substr(0, split);
    const int place = std::atoi(name.c_str() + split);
    const auto [it, fresh] = by_prefix.emplace(prefix, words.size());
    if (fresh) words.push_back({prefix, {}});
    words[it->second].bits.push_back({place, k});
  }
  for (auto& w : words)
    std::sort(w.bits.begin(), w.bits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  return words;
}

}  // namespace

LogicView::LogicView(ThreadPool& pool, ChipDesigner& designer) : pool_(pool), designer_(designer) {
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

LogicView::~LogicView() {
  designer_.on_save_gate = nullptr;
  if (future_.valid()) future_.wait();
}

// Alles, wovon das Zeugnis abhängt: Schaltung (über die Revision), Wellenlänge und Gatter-Definition
std::string LogicView::key() const {
  const io::Project& project = designer_.project();
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%llu/%.17g", static_cast<unsigned long long>(designer_.revision()),
                project.wavelength);
  std::string key = buffer;
  for (const auto& in : project.gate.inputs) {
    std::snprintf(buffer, sizeof buffer, "/%.17g/%.17g/%d", in.value0, in.value1,
                  static_cast<int>(in.encoding));
    key += "|" + in.name + "/" + in.instance + "/" + in.param + buffer;
    for (const auto& more : in.more_instances) key += "+" + more;
  }
  for (const auto& out : project.gate.outputs) {
    key += "|" + out.name + "/" + out.instance + "/" + out.measurement + "/";
    for (int bit : out.expected) key += bit ? '1' : '0';
  }
  return key;
}

void LogicView::update() {
  if (future_.valid() && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    result_ = future_.get();
  if (future_.valid()) return;
  const io::Project& project = designer_.project();
  const std::string current = key();
  // Wahrheitstabelle und Zeugnis nur bis max_inputs Eingänge (2^n Rechnungen plus Toleranzen)
  if (project.gate.empty() || project.gate.inputs.size() > logic::max_inputs || result_.key == current)
    return;

  // Die Charakterisierung läuft als Job; er besitzt Kopien von Schaltung und Definition.
  future_ = pool_.submit([circuit = project.circuit, gate = project.gate, wavelength = project.wavelength,
                          current, registry = active_registry_ptr()] {
    Result result;
    result.key = current;
    result.gate = gate;
    try {
      result.report = logic::characterize(circuit, *registry, gate, wavelength);
    } catch (const std::exception& e) {
      result.error = e.what();
    }
    return result;
  });
}

void LogicView::draw() {
  update();
  // Neues Projekt mit Logik: Fenster nach vorn holen
  if (designer_.project_generation() != seen_generation_) {
    seen_generation_ = designer_.project_generation();
    if (!designer_.project().gate.empty()) ImGui::SetWindowFocus(ChipDesigner::window_logic);
  }
  if (!ImGui::Begin(ChipDesigner::window_logic)) {
    ImGui::End();
    return;
  }
  io::Project& project = designer_.project();
  if (project.gate.empty()) {
    draw_empty_state();
    ImGui::End();
    return;
  }
  // Links: Bits schalten und Definition; rechts: Wahrheitstabelle und Zeugnis. Ohne Tabelle (zu viele
  // Eingänge) bekommt die Eingabe die ganze Breite.
  const bool table = project.gate.inputs.size() <= logic::max_inputs;
  const float width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x;
  ImGui::BeginChild("live", ImVec2(table ? width * 0.5f : 0.0f, 0.0f), ImGuiChildFlags_Borders);
  draw_live();
  if (!table) {
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "%zu Eingänge: die volle Wahrheitstabelle hätte %.0f Zeilen. Tabelle und Zeugnis gibt es bis %zu "
        "Eingänge; größere Schaltungen hier mit Zahlen prüfen (jede Eingabe rechnet die ganze Schaltung).",
        project.gate.inputs.size(), std::ldexp(1.0, static_cast<int>(project.gate.inputs.size())),
        logic::max_inputs);
    ImGui::PopTextWrapPos();
  }
  ImGui::Spacing();
  if (show_definition_) ImGui::SetNextItemOpen(true);
  show_definition_ = false;
  if (ImGui::CollapsingHeader("Definition bearbeiten (Eingänge, Ausgänge)")) draw_definition();
  ImGui::EndChild();
  if (table) {
    ImGui::SameLine();
    ImGui::BeginChild("tabelle", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    draw_truth_table();
    ImGui::Spacing();
    draw_report();
    ImGui::EndChild();
  }
  ImGui::End();
}

void LogicView::draw_empty_state() {
  io::Project& project = designer_.project();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted("Logik: rechnet die Schaltung als Logikgatter.");
  ImGui::TextDisabled(
      "Ein Eingang schaltet einen Parameter zwischen Bit 0 und Bit 1 (Heizleistung, Laserphase, "
      "Laserleistung ...), ein Ausgang ist die Leistung an einem Detektor. Hier lassen sich die Bits von "
      "Hand "
      "schalten; bis 6 Eingänge rechnet PhotonLab die ganze Wahrheitstabelle und ein Zeugnis.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (ImGui::Button("Logik für diese Schaltung festlegen")) {
    project.gate.name = project.circuit.name;
    designer_.mark_dirty();
    // sinnvolle Vorbelegung: erster Laser als Eingang, erster Detektor als Ausgang
    for (const auto& instance : project.circuit.instances) {
      const circuit::ComponentType* type = registry().find(instance.type);
      if (!type) continue;
      if (project.gate.inputs.empty() && type->find_param("power") && type->port_names.size() == 1)
        project.gate.inputs.push_back({"A",
                                       instance.name,
                                       "power",
                                       0.0,
                                       type->find_param("power")->default_value,
                                       logic::Encoding::intensity,
                                       {}});
      if (project.gate.outputs.empty() && (instance.type == "detector" || instance.type == "photodiode"))
        project.gate.outputs.push_back({"Y", instance.name, "Leistung", {}, 0.0});
    }
    if (project.gate.inputs.empty() && project.gate.outputs.empty()) project.gate.name = "Gatter";
    show_definition_ = true;
  }
  ImGui::Spacing();
  ImGui::SeparatorText("Beispiele mit Logik");
  struct Example {
    const char* key;
    const char* label;
    const char* text;
  };
  const Example examples[] = {
      {"half_adder", "Halbaddierer", "3 MZI-Schalter mit Heizern; A und B sind Heizleistungen."},
      {"full_adder", "Volladdierer", "Übertrag als Licht auf zwei Schienen; baubar in Silizium."},
      {"adder2", "2-Bit-Addierer", "Zwei Volladdierer hintereinander, volle Wahrheitstabelle."},
      {"adder4", "4-Bit-Addierer", "Zahlen 0 bis 15 addieren."},
      {"adder8", "8-Bit-Addierer", "Zahlen 0 bis 255 addieren, 68 Bauteile."},
      {"xor_directed", "XOR mit Ringen", "Directed Logic, schmalbandig."},
      {"xor_interference", "Interferenz-XOR", "Phasencodiert, nicht kaskadierbar."}};
  for (const auto& example : examples) {
    if (ImGui::Button(example.label)) designer_.load_example(example.key);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", example.text);
  }
}

int LogicView::input_bit(std::size_t input) const {
  const io::Project& project = designer_.project();
  const logic::LogicInput& in = project.gate.inputs[input];
  const circuit::Instance* instance = project.circuit.find_instance(in.instance);
  if (!instance) return 0;
  const double value = param_of(*instance, in.param);
  return std::abs(value - in.value1) < std::abs(value - in.value0) ? 1 : 0;
}

void LogicView::set_input_bit(std::size_t input, int bit) {
  io::Project& project = designer_.project();
  const logic::LogicInput& in = project.gate.inputs[input];
  for (const auto& name : logic::targets(in))
    if (circuit::Instance* instance = project.circuit.find_instance(name))
      instance->params[in.param] = bit ? in.value1 : in.value0;
  designer_.touch_circuit();
}

double LogicView::output_threshold(std::size_t output) const {
  const logic::GateDefinition& gate = designer_.project().gate;
  if (gate.outputs[output].threshold > 0.0) return gate.outputs[output].threshold;
  // sonst aus der gemessenen Wahrheitstabelle (dieselbe Definition, Eingangswerte spielen dort keine Rolle)
  if (result_.error.empty() && result_.report.outputs.size() == gate.outputs.size() &&
      result_.gate.outputs.size() == gate.outputs.size() &&
      result_.gate.outputs[output].instance == gate.outputs[output].instance)
    return result_.report.outputs[output].threshold;
  return 0.0;
}

void LogicView::draw_live() {
  io::Project& project = designer_.project();
  const logic::GateDefinition& gate = project.gate;
  ImGui::SeparatorText(gate.name.empty() ? "Logik" : gate.name.c_str());

  // --- Eingänge: Wörter als Zahl und Bits, einzelne Bits als Schalter ---
  ImGui::TextDisabled("Eingänge schalten die Bauteile der Schaltung (siehe Zeichenfläche)");
  std::map<std::string, long long> input_values;
  for (const Word& word : words_of(gate.inputs)) {
    ImGui::PushID(word.name.c_str());
    if (word.bits.size() == 1 && word.bits[0].first == 0 &&
        word.name == gate.inputs[word.bits[0].second].name) {
      const std::size_t k = word.bits[0].second;
      bool on = input_bit(k) != 0;
      if (ImGui::Checkbox(word.name.c_str(), &on)) set_input_bit(k, on ? 1 : 0);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        std::string list;
        for (const auto& name : logic::targets(gate.inputs[k])) list += (list.empty() ? "" : ", ") + name;
        ImGui::SetTooltip("%s an %s", gate.inputs[k].param.c_str(), list.c_str());
      }
      input_values[word.name] = on ? 1 : 0;
      ImGui::PopID();
      continue;
    }
    long long value = 0;
    for (const auto& [place, k] : word.bits)
      if (input_bit(k)) value += 1LL << place;
    input_values[word.name] = value;
    int shown = static_cast<int>(value);
    const int top = static_cast<int>((1LL << (word.bits.front().first + 1)) - 1);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s =", word.name.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::InputInt("##zahl", &shown, 1, 10)) {
      shown = std::clamp(shown, 0, top);
      for (const auto& [place, k] : word.bits) {
        const int bit = (shown >> place) & 1;
        if (bit != input_bit(k)) set_input_bit(k, bit);
      }
    }
    for (const auto& [place, k] : word.bits) {
      ImGui::SameLine(0.0f, 2.0f);
      bool on = input_bit(k) != 0;
      ImGui::PushID(static_cast<int>(k));
      if (ImGui::Checkbox("##bit", &on)) set_input_bit(k, on ? 1 : 0);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s (Stelle %d)", gate.inputs[k].name.c_str(), place);
      ImGui::PopID();
    }
    ImGui::PopID();
  }

  // --- Ausgänge: Bits nach der Schwelle, Wörter als Zahl, darunter die Leistungen ---
  ImGui::Spacing();
  ImGui::TextDisabled("Ausgänge (Bit = Leistung am Detektor über der Schwelle)");
  const auto& measurements = designer_.measurements();
  std::vector<double> power(gate.outputs.size(), -1.0), threshold(gate.outputs.size(), 0.0);
  std::vector<int> out_bits(gate.outputs.size(), -1);
  for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
    for (const auto& m : measurements)
      if (m.instance == gate.outputs[k].instance && m.measurement.name == gate.outputs[k].measurement)
        power[k] = m.measurement.value;
    threshold[k] = output_threshold(k);
    if (power[k] >= 0.0 && threshold[k] > 0.0) out_bits[k] = power[k] > threshold[k] ? 1 : 0;
  }
  auto bit_box = [&](std::size_t k) {
    const char* text = out_bits[k] < 0 ? "?" : out_bits[k] ? "1" : "0";
    const ImVec4 color = out_bits[k] > 0 ? ui_color_good : ui_color_neutral;
    ImGui::PushStyleColor(ImGuiCol_Button,
                          out_bits[k] > 0 ? ImVec4(color.x * 0.45f, color.y * 0.45f, color.z * 0.45f, 1.0f)
                                          : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushID(static_cast<int>(k));
    ImGui::SmallButton(text);
    ImGui::PopID();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s: %s (Schwelle %s)", gate.outputs[k].name.c_str(),
                        power[k] >= 0.0 ? format_si(power[k], "W").c_str() : "-",
                        threshold[k] > 0.0 ? format_si(threshold[k], "W").c_str() : "unbekannt");
  };
  std::map<std::string, long long> output_values;
  bool complete = true;
  for (const Word& word : words_of(gate.outputs)) {
    long long value = 0;
    for (const auto& [place, k] : word.bits) {
      if (out_bits[k] < 0) complete = false;
      if (out_bits[k] > 0) value += 1LL << place;
    }
    output_values[word.name] = value;
    ImGui::AlignTextToFramePadding();
    if (word.bits.size() > 1)
      ImGui::Text("%s = %lld", word.name.c_str(), value);
    else
      ImGui::TextUnformatted(word.name.c_str());
    for (const auto& [place, k] : word.bits) {
      ImGui::SameLine(0.0f, 2.0f);
      bit_box(k);
    }
  }
  // Addierer (Wörter A, B, S, dazu Cin und Cout): Probe A + B + Cin
  if (complete && input_values.count("A") && input_values.count("B") && output_values.count("S") &&
      output_values.count("Cout")) {
    int width = 0;
    for (const Word& word : words_of(gate.outputs))
      if (word.name == "S") width = static_cast<int>(word.bits.size());
    const long long cin = input_values.count("Cin") ? input_values["Cin"] : 0;
    const long long expected = input_values["A"] + input_values["B"] + cin;
    const long long got = output_values["S"] + (output_values["Cout"] << width);
    ImGui::Text("Ergebnis Cout S = %lld", got);
    ImGui::SameLine();
    if (got == expected)
      ImGui::TextColored(ui_color_good, "= %lld + %lld%s, stimmt", input_values["A"], input_values["B"],
                         cin ? " + 1" : "");
    else
      ImGui::TextColored(ui_color_bad, "FALSCH, erwartet %lld", expected);
  }
  if (!complete) ImGui::TextDisabled("Ohne Schwelle kein Bit: Schwelle in der Definition setzen.");

  // Heizleistung der Schaltung bei diesen Bits; Bit 1 der Heizer-Eingänge gegen P_pi der Bauteile prüfen
  double heating = 0.0;
  for (const auto& m : measurements)
    if (m.measurement.name == "Heizleistung") heating += m.measurement.value;
  if (heating > 0.0) ImGui::TextDisabled("Heizleistung bei diesen Bits: %s", format_si(heating, "W").c_str());
  std::vector<std::pair<std::size_t, double>> mismatched;  // Eingang, P_pi
  for (std::size_t k = 0; k < gate.inputs.size(); ++k) {
    const logic::LogicInput& in = gate.inputs[k];
    if (in.param != "power_a" && in.param != "power_b" && in.param != "power_el") continue;
    for (const auto& m : measurements)
      if (m.instance == in.instance && m.measurement.name == "P_pi" &&
          std::abs(in.value1 - m.measurement.value) > 0.01 * m.measurement.value)
        mismatched.emplace_back(k, m.measurement.value);
  }
  if (!mismatched.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_limited, "Bit 1 von %s (%s) ist nicht P_pi der Heizer (%s).",
                       gate.inputs[mismatched.front().first].name.c_str(),
                       format_si(gate.inputs[mismatched.front().first].value1, "W").c_str(),
                       format_si(mismatched.front().second, "W").c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton("Bit-1-Werte auf P_pi stellen")) {
      for (const auto& [k, p_pi] : mismatched) {
        const int bit = input_bit(k);
        project.gate.inputs[k].value1 = p_pi;
        set_input_bit(k, bit);
      }
      designer_.mark_dirty();
    }
  }

  if (!ImGui::TreeNodeEx("Leistungen und Schwellen",
                         gate.outputs.size() <= 3 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
    return;
  if (ImGui::BeginTable("ausgaenge_live", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Ausgang", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Leistung", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Schwelle", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();
    for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(gate.outputs[k].name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(power[k] >= 0.0 ? format_si(power[k], "W").c_str() : "-");
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", threshold[k] > 0.0 ? format_si(threshold[k], "W").c_str() : "unbekannt");
    }
    ImGui::EndTable();
  }
  ImGui::TreePop();
}

void LogicView::draw_definition() {
  io::Project& project = designer_.project();
  logic::GateDefinition& gate = project.gate;
  const circuit::Circuit& circuit = project.circuit;
  bool changed = false;

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
        in.more_instances.clear();
        changed = true;
      }

      // weitere Bauteile am selben Steuersignal (z. B. alle Heizer eines Bits)
      if (!in.more_instances.empty()) {
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextDisabled("+%zu", in.more_instances.size());
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) || ImGui::IsItemClicked())
        ImGui::OpenPopup("weitere");
      if (ImGui::BeginPopup("weitere")) {
        ImGui::TextDisabled("Dieses Bit setzt '%s' auch an:", in.param.c_str());
        for (const auto& other : circuit.instances) {
          const circuit::ComponentType* other_type = registry().find(other.type);
          if (other.name == in.instance || !other_type || !other_type->find_param(in.param)) continue;
          bool on = std::ranges::count(in.more_instances, other.name) > 0;
          if (ImGui::Checkbox(other.name.c_str(), &on)) {
            if (on)
              in.more_instances.push_back(other.name);
            else
              std::erase(in.more_instances, other.name);
            changed = true;
          }
        }
        ImGui::EndPopup();
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
  if (ImGui::SmallButton("+ Eingang")) {
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
  if (ImGui::BeginTable("ausgaenge", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Bauteil", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Erwartete Funktion", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("Schwelle", ImGuiTableColumnFlags_WidthStretch, 1.2f);
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
      ImGui::BeginDisabled(gate.inputs.size() > logic::max_inputs);
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
      ImGui::EndDisabled();
      ImGui::TableNextColumn();
      double threshold_uw = out.threshold / 1e-6;
      const double zero = 0.0, big = 1e6;
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::DragScalar("##schwelle", ImGuiDataType_Double, &threshold_uw, 1.0f, &zero, &big,
                            threshold_uw > 0.0 ? "%.3g µW" : "aus Tabelle", ImGuiSliderFlags_AlwaysClamp)) {
        out.threshold = threshold_uw * 1e-6;
        changed = true;
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Entscheidungsschwelle für das Schalten von Hand; 0 = aus der Wahrheitstabelle.");
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
      if ((instance.type == "detector" || instance.type == "photodiode") && !used) {
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

void LogicView::draw_truth_table() {
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

void LogicView::draw_report() {
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
