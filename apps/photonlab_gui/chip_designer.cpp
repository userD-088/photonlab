#include "chip_designer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>

#include "file_dialog.hpp"
#include "implot.h"
#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/sweep.hpp"
#include "photonlab/core/units.hpp"

// Hinweis: Alle Texte der Oberfläche bleiben im Latin-1-Zeichenvorrat, damit auch die
// eingebaute ImGui-Schrift sie darstellt (kein Griechisch, keine Pfeile, keine Gedankenstriche).

namespace photonlab::gui {
namespace {

using circuit::ComponentRegistry;
using circuit::ComponentType;
using circuit::Instance;
using circuit::PortRef;

constexpr float component_width = 5.0f;  // Breite eines Bauteils in Rasterzellen
constexpr float port_pitch = 2.0f;       // Abstand übereinander liegender Ports in Rasterzellen
constexpr float body_margin = 0.7f;      // Überstand des Gehäuses über den äußersten Port

const ImU32 color_stage = IM_COL32(10, 14, 20, 255);
const ImU32 color_grid = IM_COL32(38, 49, 66, 255);
const ImU32 color_body = IM_COL32(24, 32, 44, 255);
const ImU32 color_body_selected = IM_COL32(36, 50, 74, 255);
const ImU32 color_border = IM_COL32(58, 70, 88, 255);
const ImU32 color_accent = IM_COL32(134, 169, 227, 255);
const ImU32 color_track = IM_COL32(42, 52, 70, 255);
const ImU32 color_dark_wire = IM_COL32(91, 102, 120, 160);
const ImU32 color_text = IM_COL32(227, 232, 239, 255);
const ImU32 color_muted = IM_COL32(152, 164, 182, 255);
const ImVec4 color_bad = ImVec4(1.0f, 0.48f, 0.42f, 1.0f);
const ImVec4 color_good = ImVec4(0.36f, 0.77f, 0.55f, 1.0f);

const ComponentRegistry& registry() { return ComponentRegistry::builtin(); }

// Editor-Konvention: Ports, deren Name mit "in" beginnt, liegen links, alle anderen rechts.
bool is_left_port(const std::string& port_name) { return port_name.rfind("in", 0) == 0; }

// Zahl mit SI-Vorsatz, z. B. 1.0e-3 W -> "1.000 mW"
std::string format_si(double value, const std::string& unit) {
  struct Prefix {
    double factor;
    const char* symbol;
  };
  static const Prefix prefixes[] = {{1e9, "G"},  {1e6, "M"},         {1e3, "k"},  {1.0, ""},
                                    {1e-3, "m"}, {1e-6, "\xC2\xB5"}, {1e-9, "n"}, {1e-12, "p"}};
  char buffer[64];
  const double magnitude = std::abs(value);
  if (magnitude < 1e-15) {
    std::snprintf(buffer, sizeof buffer, "0 %s", unit.c_str());
    return buffer;
  }
  const Prefix* chosen = &prefixes[std::size(prefixes) - 1];
  for (const Prefix& p : prefixes)
    if (magnitude >= p.factor) {
      chosen = &p;
      break;
    }
  std::snprintf(buffer, sizeof buffer, "%.4g %s%s", value / chosen->factor, chosen->symbol, unit.c_str());
  return buffer;
}

std::string format_param(double si_value, const circuit::ParamSpec& spec) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.4g %s", si_value / spec.display_scale, spec.display_unit.c_str());
  return buffer;
}

double param_value(const Instance& instance, const circuit::ParamSpec& spec) {
  const auto it = instance.params.find(spec.name);
  return it != instance.params.end() ? it->second : spec.default_value;
}

// Messgrößen in Watt eines gelösten Punktes: Namen und Werte in mW
void collect_powers(const circuit::CompiledCircuit& compiled, const circuit::Solution& solution,
                    std::vector<std::string>* names, std::vector<double>* values_mw) {
  for (const auto& m : compiled.measurements(solution)) {
    if (m.measurement.unit != "W") continue;
    if (names) names->push_back(m.instance + " " + m.measurement.name);
    values_mw->push_back(m.measurement.value / units::mW);
  }
}

// Pfade kommen von SDL und aus argv als UTF-8; std::filesystem braucht das unter Windows ausdrücklich.
std::filesystem::path path_from_utf8(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
std::string utf8_from_path(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

double to_dbm(double power_mw) { return 10.0 * std::log10(std::max(power_mw, 1e-12)); }

ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
ImVec2 operator-(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }

}  // namespace

ChipDesigner::ChipDesigner(ThreadPool& pool, SDL_Window* window, float ui_scale)
    : pool_(pool), window_(window), ui_scale_(ui_scale), cell_(28.0f * ui_scale) {
  io::Project project;
  project.circuit = circuit::reference_mzi();
  set_project(std::move(project), {});
  selected_ = "phase";  // damit die Kennlinie beim Start die cos²-Kurve zeigt
  rename_buffer_ = selected_;
}

ChipDesigner::~ChipDesigner() {
  if (sweep_control_) sweep_control_->cancel_requested = true;
  if (sweep_future_.valid()) sweep_future_.wait();
}

// ---------------------------------------------------------------------------------------------
// Bearbeiten
// ---------------------------------------------------------------------------------------------

void ChipDesigner::touch() {
  ++revision_;
  dirty_ = true;
}

void ChipDesigner::set_status(std::string text, bool is_error) {
  status_ = std::move(text);
  status_is_error_ = is_error;
}

void ChipDesigner::set_project(io::Project project, std::string path) {
  project_ = std::move(project);
  file_path_ = std::move(path);
  dirty_ = false;
  ++revision_;
  selected_.clear();
  rename_buffer_.clear();
  wire_from_.reset();
  fit_spectrum_ = true;
  fit_curve_ = true;
  fit_view_ = 3;
}

void ChipDesigner::request(Pending action, std::string argument) {
  if (dirty_) {
    pending_ = action;
    pending_argument_ = std::move(argument);
    open_confirm_popup_ = true;
  } else {
    perform(action, argument);
  }
}

void ChipDesigner::perform(Pending action, const std::string& argument) {
  switch (action) {
    case Pending::new_project: {
      io::Project project;
      project.circuit.name = "Neue Schaltung";
      set_project(std::move(project), {});
      set_status("Neue Schaltung. Rechtsklick auf die Fläche fügt Bauteile hinzu.");
      break;
    }
    case Pending::open_project:
      show_file_dialog(DialogKind::open_project, window_);
      break;
    case Pending::load_example: {
      io::Project project;
      project.circuit = circuit::reference_design(argument);
      set_project(std::move(project), {});
      set_status("Beispiel geladen: " + project_.circuit.name);
      break;
    }
    case Pending::quit:
      quit_confirmed_ = true;
      break;
    case Pending::none:
      break;
  }
}

void ChipDesigner::request_quit() { request(Pending::quit); }

bool ChipDesigner::open_file(const std::string& path) {
  try {
    io::Project project = io::load_project(path_from_utf8(path));
    set_project(std::move(project), path);
    set_status("Geladen: " + path);
    return true;
  } catch (const std::exception& e) {
    set_status(e.what(), true);
    return false;
  }
}

bool ChipDesigner::save_file(const std::string& path) {
  try {
    io::save_project(project_, path_from_utf8(path));
    file_path_ = path;
    dirty_ = false;
    set_status("Gespeichert: " + path);
    return true;
  } catch (const std::exception& e) {
    set_status(e.what(), true);
    return false;
  }
}

void ChipDesigner::save_or_ask() {
  if (file_path_.empty())
    show_file_dialog(DialogKind::save_project, window_);
  else
    save_file(file_path_);
}

void ChipDesigner::handle_dialog_result() {
  const auto result = poll_file_dialog();
  if (!result) return;
  if (!result->error.empty()) {
    set_status(
        "Dateidialog nicht verfügbar: " + result->error + " (Projektdatei als Programmargument übergeben)",
        true);
    return;
  }
  if (result->path.empty()) return;  // abgebrochen
  if (result->kind == DialogKind::open_project) {
    open_file(result->path);
  } else {
    std::string path = result->path;
    if (path_from_utf8(path).extension().empty()) path += ".plab.json";
    save_file(path);
  }
}

void ChipDesigner::add_instance(const std::string& type_name, int grid_x, int grid_y) {
  const ComponentType* type = registry().find(type_name);
  if (!type) return;
  // nach unten ausweichen, bis die Stelle frei ist
  auto occupied = [&](int x, int y) {
    return std::any_of(project_.circuit.instances.begin(), project_.circuit.instances.end(),
                       [&](const Instance& other) {
                         return std::abs(other.grid_x - x) <= static_cast<int>(component_width) &&
                                std::abs(other.grid_y - y) <= 3;
                       });
  };
  while (occupied(grid_x, grid_y)) grid_y += 4;
  const std::string name = project_.circuit.unique_name(type->name);
  project_.circuit.add(name, type->name, type->default_params(), grid_x, grid_y);
  selected_ = name;
  rename_buffer_ = name;
  touch();
}

void ChipDesigner::rename_selected(const std::string& new_name) {
  if (new_name == selected_) return;
  if (new_name.empty() || new_name.find('.') != std::string::npos) {
    set_status("Der Name darf nicht leer sein und keinen Punkt enthalten.", true);
    return;
  }
  if (project_.circuit.find_instance(new_name)) {
    set_status("Der Name '" + new_name + "' ist schon vergeben.", true);
    return;
  }
  Instance* instance = project_.circuit.find_instance(selected_);
  if (!instance) return;
  for (auto& connection : project_.circuit.connections) {
    if (connection.a.instance == selected_) connection.a.instance = new_name;
    if (connection.b.instance == selected_) connection.b.instance = new_name;
  }
  instance->name = new_name;
  selected_ = new_name;
  touch();
}

// ---------------------------------------------------------------------------------------------
// Rechnen
// ---------------------------------------------------------------------------------------------

void ChipDesigner::update_solution() {
  if (solved_revision_ == revision_ && solved_wavelength_ == project_.wavelength) return;
  solved_revision_ = revision_;
  solved_wavelength_ = project_.wavelength;
  has_solution_ = false;
  measurements_.clear();
  solve_error_.clear();
  balance_ = {};
  max_wave_power_ = 0.0;
  try {
    compiled_ = std::make_shared<const circuit::CompiledCircuit>(project_.circuit, registry());
    solution_ = compiled_->solve(project_.wavelength);
    measurements_ = compiled_->measurements(solution_);
    balance_ = compiled_->power_balance(solution_);
    for (Eigen::Index i = 0; i < solution_.b.size(); ++i)
      max_wave_power_ = std::max(max_wave_power_, std::norm(solution_.b(i)));
    has_solution_ = true;
  } catch (const std::exception& e) {
    solve_error_ = e.what();
  }
}

std::string ChipDesigner::sweep_key() const {
  char buffer[160];
  std::snprintf(buffer, sizeof buffer, "%llu/%.17g/%.17g/%d", static_cast<unsigned long long>(revision_),
                project_.sweep.wavelength_start, project_.sweep.wavelength_stop, project_.sweep.points);
  return buffer;
}

void ChipDesigner::update_sweep() {
  // fertigen Job abholen (die GUI liest nur fertige Ergebnisse)
  if (sweep_future_.valid() && sweep_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    SeriesData result = sweep_future_.get();
    const bool cancelled = sweep_control_ && sweep_control_->cancel_requested;
    sweep_control_.reset();
    if (!cancelled) {
      sweep_result_ = std::move(result);
      fit_spectrum_ = true;
    } else {
      auto_sweep_ = false;  // nach einem Abbruch nicht sofort wieder starten
    }
  }
  if (sweep_future_.valid()) return;  // läuft noch
  if (!auto_sweep_ || sweep_result_.key == sweep_key()) return;

  // neuen Job starten; er besitzt seine eigene, unveränderliche Kopie der Schaltung
  auto control = std::make_shared<JobControl>();
  SeriesData failed;
  failed.key = sweep_key();
  if (!compiled_ || !has_solution_) {
    failed.error = solve_error_.empty() ? "Schaltung ist nicht rechenbar." : solve_error_;
    sweep_result_ = std::move(failed);
    return;
  }
  if (!(project_.sweep.wavelength_start < project_.sweep.wavelength_stop) || project_.sweep.points < 2) {
    failed.error = "Sweep braucht Start < Stopp und mindestens 2 Punkte.";
    sweep_result_ = std::move(failed);
    return;
  }
  sweep_control_ = control;
  sweep_future_ = pool_.submit(
      [compiled = compiled_, settings = project_.sweep, key = sweep_key(), control, pool = &pool_] {
        SeriesData data;
        data.key = key;
        try {
          const auto solutions = circuit::sweep(*compiled, settings, pool, control.get());
          data.x.reserve(solutions.size());
          for (const auto& solution : solutions) {
            std::vector<double> values;
            collect_powers(*compiled, solution, data.names.empty() ? &data.names : nullptr, &values);
            if (data.power_mw.empty()) data.power_mw.resize(values.size());
            data.x.push_back(solution.wavelength / units::nm);
            for (std::size_t k = 0; k < values.size(); ++k) data.power_mw[k].push_back(values[k]);
          }
        } catch (const std::exception& e) {
          data.error = e.what();
        }
        control->finished = true;
        return data;
      });
}

void ChipDesigner::update_curve() {
  const Instance* instance = project_.circuit.find_instance(selected_);
  const ComponentType* type = instance ? registry().find(instance->type) : nullptr;
  if (!type || type->params.empty()) {
    curve_ = {};
    return;
  }
  const circuit::ParamSpec* spec = type->find_param(curve_param_);
  if (!spec) {
    spec = &type->params.front();
    curve_param_ = spec->name;
  }
  curve_unit_ = spec->display_unit;

  // Grenzen neu wählen, wenn ein anderes Bauteil oder ein anderer Parameter betrachtet wird
  const std::string owner = selected_ + "/" + curve_param_;
  if (owner != curve_range_owner_) {
    curve_range_owner_ = owner;
    const double low = spec->min_value / spec->display_scale, high = spec->max_value / spec->display_scale;
    const double current = param_value(*instance, *spec) / spec->display_scale;
    if (spec->display_unit == "rad") {
      curve_min_ = 0.0;
      curve_max_ = 2.0 * constants::pi;
    } else if (high - low <= 1000.0) {
      curve_min_ = low;
      curve_max_ = high;
    } else {
      curve_min_ = std::max(low, 0.0);
      curve_max_ =
          std::min(high, current > 0.0 ? 2.0 * current : spec->default_value * 2.0 / spec->display_scale);
    }
    fit_curve_ = true;
  }

  char buffer[200];
  std::snprintf(buffer, sizeof buffer, "%llu/%s/%.17g/%.17g/%.17g",
                static_cast<unsigned long long>(revision_), owner.c_str(), curve_min_, curve_max_,
                project_.wavelength);
  if (curve_.key == buffer) return;
  curve_ = {};
  curve_.key = buffer;
  if (!(curve_min_ < curve_max_)) {
    curve_.error = "Kennlinie braucht Minimum < Maximum.";
    return;
  }

  // Kleine Schaltungen rechnen so schnell, dass die Kennlinie direkt im GUI-Thread entsteht
  constexpr int points = 201;
  circuit::Circuit varied = project_.circuit;
  Instance* target = varied.find_instance(selected_);
  try {
    for (int i = 0; i < points; ++i) {
      const double x = curve_min_ + (curve_max_ - curve_min_) * i / (points - 1);
      target->params[curve_param_] = std::clamp(x * spec->display_scale, spec->min_value, spec->max_value);
      const circuit::CompiledCircuit compiled(varied, registry());
      const circuit::Solution solution = compiled.solve(project_.wavelength);
      std::vector<double> values;
      collect_powers(compiled, solution, curve_.names.empty() ? &curve_.names : nullptr, &values);
      if (curve_.power_mw.empty()) curve_.power_mw.resize(values.size());
      curve_.x.push_back(x);
      for (std::size_t k = 0; k < values.size(); ++k) curve_.power_mw[k].push_back(values[k]);
    }
  } catch (const std::exception& e) {
    curve_.x.clear();
    curve_.power_mw.clear();
    curve_.error = e.what();
  }
}

// ---------------------------------------------------------------------------------------------
// Geometrie und Farben
// ---------------------------------------------------------------------------------------------

ImVec2 ChipDesigner::grid_to_screen(float grid_x, float grid_y) const {
  return {origin_.x + grid_x * cell_, origin_.y + grid_y * cell_};
}

std::optional<ChipDesigner::PortGeometry> ChipDesigner::port_geometry(const PortRef& port) const {
  const Instance* instance = project_.circuit.find_instance(port.instance);
  const ComponentType* type = instance ? registry().find(instance->type) : nullptr;
  if (!type) return std::nullopt;
  int left_row = 0, right_row = 0;
  for (const auto& name : type->port_names) {
    const bool left = is_left_port(name);
    int& row = left ? left_row : right_row;
    if (name == port.port) {
      const float x = static_cast<float>(instance->grid_x) + (left ? 0.0f : component_width);
      const float y = static_cast<float>(instance->grid_y) + port_pitch * static_cast<float>(row);
      return PortGeometry{grid_to_screen(x, y), left};
    }
    ++row;
  }
  return std::nullopt;
}

// Farbe einer Verbindung: Farbton = Phase der stärkeren Welle, Helligkeit = Leistung (relativ zur größten).
ImU32 ChipDesigner::wave_color(const PortRef& a, const PortRef& b, float* strength) const {
  *strength = 0.0f;
  if (!has_solution_ || !compiled_ || max_wave_power_ <= 0.0) return color_dark_wire;
  try {
    const cplx forward = solution_.b(compiled_->port_index(a));
    const cplx backward = solution_.b(compiled_->port_index(b));
    const cplx dominant = std::norm(forward) >= std::norm(backward) ? forward : backward;
    const double total = std::norm(forward) + std::norm(backward);
    if (total < 1e-9 * max_wave_power_) return color_dark_wire;
    const float k = static_cast<float>(std::sqrt(std::min(1.0, total / max_wave_power_)));
    *strength = k;
    const float hue = static_cast<float>((std::arg(dominant) + constants::pi) / (2.0 * constants::pi));
    ImVec4 rgb = ImColor::HSV(hue, 0.80f, 1.0f);
    rgb.w = 0.35f + 0.65f * k;
    return ImGui::ColorConvertFloat4ToU32(rgb);
  } catch (const std::exception&) {
    return color_dark_wire;
  }
}

// Zweite Zeile im Bauteil: erster Messwert, sonst der erste Parameter
std::string ChipDesigner::summary(const Instance& instance, const ComponentType& type) const {
  if (type.port_names.size() == 1 && !is_left_port(type.port_names.front())) {
    // Quelle: der eingestellte Parameter ist aussagekräftiger als die Rückreflexion
  } else {
    for (const auto& m : measurements_)
      if (m.instance == instance.name) return format_si(m.measurement.value, m.measurement.unit);
  }
  if (type.params.empty()) return {};
  return format_param(param_value(instance, type.params.front()), type.params.front());
}

// ---------------------------------------------------------------------------------------------
// Fenster
// ---------------------------------------------------------------------------------------------

void ChipDesigner::draw() {
  handle_dialog_result();
  if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) save_or_ask();

  update_solution();
  update_sweep();
  update_curve();

  draw_library();
  draw_properties();
  draw_spectrum();
  draw_curve();
  draw_canvas();  // zuletzt, damit der Arbeitsbereich beim Start den Fokus hat
  draw_confirm_popup();
}

void ChipDesigner::draw_file_menu() {
  if (ImGui::MenuItem("Neu")) request(Pending::new_project);
  if (ImGui::MenuItem("Öffnen...")) request(Pending::open_project);
  if (ImGui::BeginMenu("Beispiele")) {
    for (const auto& name : circuit::reference_design_names())
      if (ImGui::MenuItem(circuit::reference_design(name).name.c_str())) request(Pending::load_example, name);
    ImGui::EndMenu();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Speichern", "Strg+S")) save_or_ask();
  if (ImGui::MenuItem("Speichern unter...")) show_file_dialog(DialogKind::save_project, window_);
}

void ChipDesigner::draw_confirm_popup() {
  const char* title = "Ungespeicherte Änderungen";
  if (open_confirm_popup_) {
    ImGui::OpenPopup(title);
    open_confirm_popup_ = false;
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Die Schaltung wurde geändert und noch nicht gespeichert.");
    ImGui::Spacing();
    if (ImGui::Button("Änderungen verwerfen")) {
      const Pending action = pending_;
      pending_ = Pending::none;
      dirty_ = false;
      ImGui::CloseCurrentPopup();
      perform(action, pending_argument_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Abbrechen")) {
      pending_ = Pending::none;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void ChipDesigner::draw_library() {
  ImGui::Begin(window_library);
  ImGui::TextDisabled("Klick fügt das Bauteil ein");
  ImGui::Separator();
  for (const auto& type : registry().types()) {
    if (ImGui::Selectable(type.label.c_str()))
      add_instance(type.name, static_cast<int>(std::lround(view_center_grid_.x - component_width / 2)),
                   static_cast<int>(std::lround(view_center_grid_.y)));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
      ImGui::TextUnformatted(type.description.c_str());
      std::string ports = "Ports:";
      for (const auto& port : type.port_names) ports += " " + port;
      ImGui::TextDisabled("%s", ports.c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  }
  ImGui::End();
}

void ChipDesigner::draw_properties() {
  ImGui::Begin(window_properties);

  Instance* instance = project_.circuit.find_instance(selected_);
  const ComponentType* type = instance ? registry().find(instance->type) : nullptr;
  if (!instance || !type) {
    ImGui::TextDisabled("Kein Bauteil ausgewählt.");
  } else {
    ImGui::SeparatorText(type->label.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", type->description.c_str());
    ImGui::PopTextWrapPos();

    char name_buffer[64];
    std::snprintf(name_buffer, sizeof name_buffer, "%s", rename_buffer_.c_str());
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 9.0f);
    if (ImGui::InputText("Name", name_buffer, sizeof name_buffer)) rename_buffer_ = name_buffer;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      rename_selected(rename_buffer_);
      rename_buffer_ = selected_;
      instance = project_.circuit.find_instance(selected_);
    }

    for (const auto& spec : type->params) {
      double shown = param_value(*instance, spec) / spec.display_scale;
      const double low = spec.min_value / spec.display_scale, high = spec.max_value / spec.display_scale;
      const std::string label =
          spec.display_unit.empty() ? spec.label : spec.label + " [" + spec.display_unit + "]";
      ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 9.0f);
      bool changed = false;
      if (high - low <= 1000.0) {
        changed = ImGui::SliderScalar(label.c_str(), ImGuiDataType_Double, &shown, &low, &high, "%.4g",
                                      ImGuiSliderFlags_AlwaysClamp);
      } else {
        const float speed = static_cast<float>(std::max(std::abs(shown) * 0.005, 1e-3));
        changed = ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, &shown, speed, &low, &high, "%.6g",
                                    ImGuiSliderFlags_AlwaysClamp);
      }
      if (changed && std::isfinite(shown)) {
        instance->params[spec.name] = std::clamp(shown * spec.display_scale, spec.min_value, spec.max_value);
        touch();
      }
    }
    if (ImGui::Button("Bauteil löschen")) {
      project_.circuit.remove_instance(selected_);
      selected_.clear();
      touch();
    }
  }

  ImGui::SeparatorText("Messwerte");
  if (!solve_error_.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(color_bad, "%s", solve_error_.c_str());
    ImGui::PopTextWrapPos();
  } else if (measurements_.empty()) {
    ImGui::TextDisabled("Keine Messgeräte (Detektor einfügen).");
  } else if (ImGui::BeginTable("messwerte", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    for (const auto& m : measurements_) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(m.instance.c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", m.measurement.name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(format_si(m.measurement.value, m.measurement.unit).c_str());
    }
    ImGui::EndTable();
  }

  if (has_solution_) {
    ImGui::SeparatorText("Leistungsbilanz");
    ImGui::Text("Quellen:      %s", format_si(balance_.emitted, "W").c_str());
    ImGui::Text("Absorbiert:   %s", format_si(balance_.absorbed, "W").c_str());
    ImGui::Text("Offene Ports: %s", format_si(balance_.leaving_open, "W").c_str());
    if (balance_.leaving_open > 1e-9 * std::max(balance_.emitted, 1e-30)) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("Offene Ports gelten als ideal abgeschlossen: Licht verlässt dort die Schaltung.");
      ImGui::PopTextWrapPos();
    }
  }
  ImGui::End();
}

void ChipDesigner::draw_spectrum() {
  ImGui::Begin(window_spectrum);
  auto& sweep = project_.sweep;
  double start_nm = sweep.wavelength_start / units::nm, stop_nm = sweep.wavelength_stop / units::nm;
  const double low_nm = 100.0, high_nm = 20000.0;
  const float field_width = ImGui::GetFontSize() * 6.5f;
  bool settings_changed = false;
  ImGui::SetNextItemWidth(field_width);
  settings_changed |= ImGui::DragScalar("Start [nm]", ImGuiDataType_Double, &start_nm, 0.1f, &low_nm,
                                        &high_nm, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field_width);
  settings_changed |= ImGui::DragScalar("Stopp [nm]", ImGuiDataType_Double, &stop_nm, 0.1f, &low_nm, &high_nm,
                                        "%.2f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field_width);
  int points = sweep.points;
  if (ImGui::DragInt("Punkte", &points, 10.0f, 2, 1000000, "%d", ImGuiSliderFlags_AlwaysClamp)) {
    sweep.points = points;
    settings_changed = true;
  }
  if (settings_changed) {
    sweep.wavelength_start = start_nm * units::nm;
    sweep.wavelength_stop = stop_nm * units::nm;
    dirty_ = true;
  }
  ImGui::SameLine();
  ImGui::Checkbox("dBm", &spectrum_dbm_);
  ImGui::SameLine();
  ImGui::Checkbox("automatisch", &auto_sweep_);

  // Job-Zeile: Fortschritt und Abbruch, sonst Startknopf und Zustand
  const bool running = sweep_future_.valid();
  if (running) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Abbrechen")) sweep_control_->cancel_requested = true;
    ImGui::SameLine();
    ImGui::ProgressBar(sweep_control_->progress.load(), ImVec2(-1.0f, 0.0f));
  } else {
    ImGui::SameLine();
    if (ImGui::SmallButton("Sweep starten")) {
      sweep_result_.key.clear();
      const bool was_auto = auto_sweep_;
      auto_sweep_ = true;
      update_sweep();
      auto_sweep_ = was_auto;
    }
    if (sweep_result_.key != sweep_key()) {
      ImGui::SameLine();
      ImGui::TextColored(color_bad, "veraltet");
    }
  }

  if (!sweep_result_.error.empty()) {
    ImGui::TextColored(color_bad, "%s", sweep_result_.error.c_str());
  }
  if (fit_spectrum_ && !sweep_result_.x.empty()) {
    ImPlot::SetNextAxesToFit();
    fit_spectrum_ = false;
  }
  if (ImPlot::BeginPlot("##spektrum", ImVec2(-1.0f, -1.0f))) {
    ImPlot::SetupAxes("Wellenlänge [nm]", spectrum_dbm_ ? "Leistung [dBm]" : "Leistung [mW]");
    ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
    static std::vector<double> scratch;
    for (std::size_t k = 0; k < sweep_result_.names.size() && k < sweep_result_.power_mw.size(); ++k) {
      const auto& values = sweep_result_.power_mw[k];
      const double* data = values.data();
      if (spectrum_dbm_) {
        scratch.resize(values.size());
        std::transform(values.begin(), values.end(), scratch.begin(), to_dbm);
        data = scratch.data();
      }
      ImPlot::PlotLine(sweep_result_.names[k].c_str(), sweep_result_.x.data(), data,
                       static_cast<int>(values.size()));
    }
    // Arbeitswellenlänge als verschiebbare Linie
    double working_nm = project_.wavelength / units::nm;
    if (ImPlot::DragLineX(0, &working_nm, ImVec4(1.0f, 0.42f, 0.36f, 1.0f), 1.5f) && working_nm > low_nm &&
        working_nm < high_nm) {
      project_.wavelength = working_nm * units::nm;
      dirty_ = true;
    }
    ImPlot::EndPlot();
  }
  ImGui::End();
}

void ChipDesigner::draw_curve() {
  ImGui::Begin(window_curve);
  const Instance* instance = project_.circuit.find_instance(selected_);
  const ComponentType* type = instance ? registry().find(instance->type) : nullptr;
  if (!type || type->params.empty()) {
    ImGui::TextDisabled(
        "Bauteil auswählen: die Kennlinie zeigt alle Detektorleistungen über einem seiner Parameter.");
    ImGui::End();
    return;
  }
  const circuit::ParamSpec* spec = type->find_param(curve_param_);
  const float field_width = ImGui::GetFontSize() * 6.5f;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
  if (ImGui::BeginCombo("Parameter", spec ? spec->label.c_str() : "")) {
    for (const auto& p : type->params)
      if (ImGui::Selectable(p.label.c_str(), p.name == curve_param_)) curve_param_ = p.name;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field_width);
  const float speed = static_cast<float>(std::max((curve_max_ - curve_min_) * 0.005, 1e-4));
  if (ImGui::DragScalar("von", ImGuiDataType_Double, &curve_min_, speed, nullptr, nullptr, "%.4g"))
    fit_curve_ = true;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(field_width);
  if (ImGui::DragScalar("bis", ImGuiDataType_Double, &curve_max_, speed, nullptr, nullptr, "%.4g"))
    fit_curve_ = true;
  ImGui::SameLine();
  ImGui::TextDisabled("%s  (%s)", curve_unit_.c_str(), selected_.c_str());

  if (!curve_.error.empty()) ImGui::TextColored(color_bad, "%s", curve_.error.c_str());
  if (fit_curve_ && !curve_.x.empty()) {
    ImPlot::SetNextAxesToFit();
    fit_curve_ = false;
  }
  if (ImPlot::BeginPlot("##kennlinie", ImVec2(-1.0f, -1.0f))) {
    const std::string x_label =
        spec ? (curve_unit_.empty() ? spec->label : spec->label + " [" + curve_unit_ + "]") : std::string();
    ImPlot::SetupAxes(x_label.c_str(), "Leistung [mW]");
    ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
    for (std::size_t k = 0; k < curve_.names.size() && k < curve_.power_mw.size(); ++k)
      ImPlot::PlotLine(curve_.names[k].c_str(), curve_.x.data(), curve_.power_mw[k].data(),
                       static_cast<int>(curve_.x.size()));
    if (spec) {
      // aktueller Wert des Parameters
      const double current = param_value(*instance, *spec) / spec->display_scale;
      ImPlot::PlotInfLines("##aktuell", &current, 1);
    }
    ImPlot::EndPlot();
  }
  ImGui::End();
}

void ChipDesigner::draw_canvas() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::Begin(window_canvas);

  // Kopfzeile
  const std::string file_label =
      (file_path_.empty() ? std::string("(unbenannt)")
                          : utf8_from_path(path_from_utf8(file_path_).filename())) +
      (dirty_ ? " *" : "");
  ImGui::TextUnformatted(file_label.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("|  %s  |", project_.circuit.name.c_str());
  ImGui::SameLine();
  double wavelength_nm = project_.wavelength / units::nm;
  const double low_nm = 100.0, high_nm = 20000.0;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  if (ImGui::DragScalar("Wellenlänge", ImGuiDataType_Double, &wavelength_nm, 0.01f, &low_nm, &high_nm,
                        "%.3f nm", ImGuiSliderFlags_AlwaysClamp)) {
    project_.wavelength = wavelength_nm * units::nm;
    dirty_ = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Alles zeigen")) fit_view_ = 1;

  // Zeichenfläche
  const float status_height = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_stage);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::BeginChild(
      "flaeche", ImVec2(0.0f, -status_height), ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
  const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
  const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
  const ImVec2 canvas_max = canvas_min + canvas_size;
  ImDrawList* draw = ImGui::GetWindowDrawList();

  // Zoom um den Mauszeiger
  if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f) {
    const float new_cell =
        std::clamp(cell_ * std::pow(1.12f, io.MouseWheel), 12.0f * ui_scale_, 72.0f * ui_scale_);
    const ImVec2 mouse_grid = {(io.MousePos.x - canvas_min.x - pan_.x) / cell_,
                               (io.MousePos.y - canvas_min.y - pan_.y) / cell_};
    pan_ = {io.MousePos.x - canvas_min.x - mouse_grid.x * new_cell,
            io.MousePos.y - canvas_min.y - mouse_grid.y * new_cell};
    cell_ = new_cell;
  }
  // Ansicht so wählen, dass die ganze Schaltung sichtbar ist (nach dem Laden und auf Knopfdruck)
  if (fit_view_ > 0 && canvas_size.x > 50.0f && canvas_size.y > 50.0f) {
    --fit_view_;
    cell_ = 28.0f * ui_scale_;
    pan_ = ImVec2(60.0f, 60.0f);
    if (!project_.circuit.instances.empty()) {
      float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
      for (const auto& instance : project_.circuit.instances) {
        const ComponentType* type = registry().find(instance.type);
        int left_rows = 0, right_rows = 0;
        if (type)
          for (const auto& port_name : type->port_names) ++(is_left_port(port_name) ? left_rows : right_rows);
        const float rows = static_cast<float>(std::max({left_rows, right_rows, 1}));
        min_x = std::min(min_x, static_cast<float>(instance.grid_x));
        max_x = std::max(max_x, static_cast<float>(instance.grid_x) + component_width);
        min_y = std::min(min_y, static_cast<float>(instance.grid_y) - 1.5f);
        max_y = std::max(max_y, static_cast<float>(instance.grid_y) + port_pitch * (rows - 1.0f) + 1.0f);
      }
      const float margin = 1.5f;  // Rasterzellen Rand
      const float fit = std::min(canvas_size.x / (max_x - min_x + 2.0f * margin),
                                 canvas_size.y / (max_y - min_y + 2.0f * margin));
      cell_ = std::clamp(fit, 12.0f * ui_scale_, 28.0f * ui_scale_);
      pan_ = {(canvas_size.x - (max_x + min_x) * cell_) * 0.5f,
              (canvas_size.y - (max_y + min_y) * cell_) * 0.5f};
    }
  }
  origin_ = canvas_min + pan_;
  view_center_grid_ = {(canvas_size.x * 0.5f - pan_.x) / cell_, (canvas_size.y * 0.5f - pan_.y) / cell_};

  // Raster
  for (float x = std::fmod(pan_.x, cell_); x < canvas_size.x; x += cell_)
    for (float y = std::fmod(pan_.y, cell_); y < canvas_size.y; y += cell_)
      draw->AddRectFilled({canvas_min.x + x - 1.0f, canvas_min.y + y - 1.0f},
                          {canvas_min.x + x + 1.0f, canvas_min.y + y + 1.0f}, color_grid);

  // Änderungen an der Netzliste erst nach den Schleifen ausführen
  std::vector<std::function<void()>> actions;
  const float port_radius = std::max(4.0f * ui_scale_, cell_ * 0.20f);
  const float grab_radius = port_radius * 1.6f;

  struct PortDraw {
    ImVec2 position;
    bool connected, hovered;
  };
  std::vector<PortDraw> port_draws;
  // ImGui meldet auch überdeckte Elemente als "hovered"; deshalb wird der Vorrang hier selbst geführt.
  bool any_port_hovered = false, any_body_hovered = false;

  // --- Bedienung: zuerst Ports (haben Vorrang), dann Gehäuse, zuletzt der Hintergrund ---
  for (const auto& instance : project_.circuit.instances) {
    const ComponentType* type = registry().find(instance.type);
    if (!type) continue;
    ImGui::PushID(instance.name.c_str());
    for (const auto& port_name : type->port_names) {
      const PortRef ref{instance.name, port_name};
      const auto geometry = port_geometry(ref);
      if (!geometry) continue;
      ImGui::SetCursorScreenPos(geometry->position - ImVec2(grab_radius, grab_radius));
      ImGui::InvisibleButton(port_name.c_str(), ImVec2(2.0f * grab_radius, 2.0f * grab_radius),
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) wire_from_ = ref;
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        actions.push_back([this, ref] {
          project_.circuit.disconnect(ref);
          touch();
        });
      if (hovered && !wire_from_ && ImGui::BeginTooltip()) {
        ImGui::Text("%s.%s", instance.name.c_str(), port_name.c_str());
        if (has_solution_ && compiled_) {
          try {
            const int index = compiled_->port_index(ref);
            const cplx out = solution_.b(index), in = solution_.a(index);
            ImGui::Text("auslaufend:  %s, Phase %.1f°", format_si(std::norm(out), "W").c_str(),
                        std::arg(out) * 180.0 / constants::pi);
            ImGui::Text("einlaufend:  %s, Phase %.1f°", format_si(std::norm(in), "W").c_str(),
                        std::arg(in) * 180.0 / constants::pi);
          } catch (const std::exception&) {
          }
        }
        ImGui::TextDisabled("Ziehen: verbinden   Rechtsklick: trennen");
        ImGui::EndTooltip();
      }
      any_port_hovered |= hovered;
      port_draws.push_back({geometry->position, project_.circuit.is_connected(ref), hovered});
    }
    ImGui::PopID();
  }

  struct BodyDraw {
    const Instance* instance;
    const ComponentType* type;
    ImVec2 min, max;
    bool hovered;
  };
  std::vector<BodyDraw> body_draws;
  for (auto& instance : project_.circuit.instances) {
    const ComponentType* type = registry().find(instance.type);
    if (!type) continue;
    int left_rows = 0, right_rows = 0;
    for (const auto& port_name : type->port_names) ++(is_left_port(port_name) ? left_rows : right_rows);
    const float rows = static_cast<float>(std::max({left_rows, right_rows, 1}));
    const float gx = static_cast<float>(instance.grid_x), gy = static_cast<float>(instance.grid_y);
    const ImVec2 body_min = grid_to_screen(gx, gy - body_margin);
    const ImVec2 body_max =
        grid_to_screen(gx + component_width, gy + port_pitch * (rows - 1.0f) + body_margin);

    ImGui::PushID(instance.name.c_str());
    ImGui::SetCursorScreenPos(body_min);
    ImGui::InvisibleButton("##gehaeuse", body_max - body_min);
    if (ImGui::IsItemActivated()) {
      selected_ = instance.name;
      rename_buffer_ = instance.name;
      drag_start_x_ = instance.grid_x;
      drag_start_y_ = instance.grid_y;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
      const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2.0f);
      const int new_x = drag_start_x_ + static_cast<int>(std::lround(delta.x / cell_));
      const int new_y = drag_start_y_ + static_cast<int>(std::lround(delta.y / cell_));
      if (new_x != instance.grid_x || new_y != instance.grid_y) {
        instance.grid_x = new_x;
        instance.grid_y = new_y;
        dirty_ = true;  // nur Layout, die Physik bleibt gleich
      }
    }
    const bool hovered = ImGui::IsItemHovered() && !any_port_hovered;
    any_body_hovered |= hovered;
    if (hovered && !ImGui::IsItemActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
        ImGui::BeginTooltip()) {
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
      ImGui::Text("%s (%s)", instance.name.c_str(), type->label.c_str());
      ImGui::TextDisabled("%s", type->description.c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) ImGui::OpenPopup("##bauteilmenue");
    if (ImGui::BeginPopup("##bauteilmenue")) {
      selected_ = instance.name;
      rename_buffer_ = instance.name;
      const std::string name = instance.name;
      if (ImGui::MenuItem("Verbindungen trennen"))
        actions.push_back([this, name, type] {
          for (const auto& port_name : type->port_names) project_.circuit.disconnect({name, port_name});
          touch();
        });
      if (ImGui::MenuItem("Löschen", "Entf"))
        actions.push_back([this, name] {
          project_.circuit.remove_instance(name);
          if (selected_ == name) selected_.clear();
          touch();
        });
      ImGui::EndPopup();
    }
    ImGui::PopID();
    body_draws.push_back({&instance, type, body_min, body_max, hovered});
  }

  // Hintergrund: ziehen verschiebt die Ansicht, Klick hebt die Auswahl auf, Rechtsklick öffnet das Menü
  ImGui::SetCursorScreenPos(canvas_min);
  ImGui::InvisibleButton("##hintergrund",
                         ImVec2(std::max(canvas_size.x, 1.0f), std::max(canvas_size.y, 1.0f)),
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) ||
                                ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
    pan_.x += io.MouseDelta.x;
    pan_.y += io.MouseDelta.y;
  }
  if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    const ImVec2 moved = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2.0f);
    if (moved.x == 0.0f && moved.y == 0.0f) selected_.clear();
  }
  if (ImGui::IsItemHovered() && !any_port_hovered && !any_body_hovered &&
      ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    ImGui::OpenPopup("##flaechenmenue");
  if (ImGui::BeginPopup("##flaechenmenue")) {
    const ImVec2 click = ImGui::GetMousePosOnOpeningCurrentPopup();
    const int grid_x = static_cast<int>(std::lround((click.x - origin_.x) / cell_));
    const int grid_y = static_cast<int>(std::lround((click.y - origin_.y) / cell_));
    ImGui::TextDisabled("Bauteil hinzufügen");
    ImGui::Separator();
    for (const auto& type : registry().types())
      if (ImGui::MenuItem(type.label.c_str())) {
        const std::string type_name = type.name;
        actions.push_back([this, type_name, grid_x, grid_y] { add_instance(type_name, grid_x, grid_y); });
      }
    ImGui::EndPopup();
  }

  // Entf löscht die Auswahl
  if (!selected_.empty() && !io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Delete)) {
    const std::string name = selected_;
    actions.push_back([this, name] {
      project_.circuit.remove_instance(name);
      selected_.clear();
      touch();
    });
  }

  // --- Zeichnen ---
  draw->PushClipRect(canvas_min, canvas_max, true);

  // Verbindungen: Farbton = Phase, Helligkeit und Breite = Leistung
  auto draw_wire = [&](ImVec2 p1, bool left1, ImVec2 p2, bool left2, ImU32 color, float strength) {
    const float reach = std::max(1.5f * cell_, 0.4f * std::abs(p2.x - p1.x));
    const ImVec2 c1 = {p1.x + (left1 ? -reach : reach), p1.y};
    const ImVec2 c2 = {p2.x + (left2 ? -reach : reach), p2.y};
    draw->AddBezierCubic(p1, c1, c2, p2, color_track, cell_ * 0.34f);
    draw->AddBezierCubic(p1, c1, c2, p2, color, cell_ * (0.08f + 0.16f * strength));
  };
  for (const auto& connection : project_.circuit.connections) {
    const auto g1 = port_geometry(connection.a), g2 = port_geometry(connection.b);
    if (!g1 || !g2) continue;
    float strength = 0.0f;
    const ImU32 color = wave_color(connection.a, connection.b, &strength);
    draw_wire(g1->position, g1->left, g2->position, g2->left, color, strength);
  }

  // Gehäuse mit Beschriftung
  const float font_size = ImGui::GetFontSize();
  for (const auto& body : body_draws) {
    const bool is_selected = body.instance->name == selected_;
    draw->AddRectFilled(body.min, body.max, is_selected ? color_body_selected : color_body, cell_ * 0.22f);
    draw->AddRect(body.min, body.max, is_selected || body.hovered ? color_accent : color_border,
                  cell_ * 0.22f, 0, is_selected ? 2.0f : 1.0f);
    const float width = body.max.x - body.min.x;
    const ImVec2 center = {(body.min.x + body.max.x) * 0.5f, (body.min.y + body.max.y) * 0.5f};
    auto centered = [&](const std::string& text, float y, ImU32 color) {
      const ImVec2 size = ImGui::CalcTextSize(text.c_str());
      if (size.x > width - 2.0f * port_radius) return false;
      draw->AddText({center.x - size.x * 0.5f, y}, color, text.c_str());
      return true;
    };
    const std::string second = summary(*body.instance, *body.type);
    const bool two_lines = body.max.y - body.min.y > 2.3f * font_size && !second.empty();
    if (two_lines) {
      centered(body.type->label, center.y - font_size * 1.05f, color_text);
      centered(second, center.y + font_size * 0.05f, color_muted);
    } else {
      centered(body.type->label, center.y - font_size * 0.5f, color_text);
    }
    // Instanzname über dem Gehäuse
    draw->AddText({body.min.x + 2.0f, body.min.y - font_size - 2.0f}, color_muted,
                  body.instance->name.c_str());
  }

  // Ports
  for (const auto& port : port_draws) {
    draw->AddCircleFilled(port.position, port_radius, port.connected ? color_accent : color_stage);
    draw->AddCircle(port.position, port_radius, port.hovered ? color_text : color_accent, 0,
                    port.hovered ? 2.5f : 1.5f);
  }

  // Gummiband beim Verbinden
  if (wire_from_) {
    const auto from = port_geometry(*wire_from_);
    // nächster fremder Port unter dem Mauszeiger
    std::optional<PortRef> target;
    for (const auto& instance : project_.circuit.instances) {
      const ComponentType* type = registry().find(instance.type);
      if (!type) continue;
      for (const auto& port_name : type->port_names) {
        const PortRef candidate{instance.name, port_name};
        const auto geometry = port_geometry(candidate);
        if (!geometry || candidate == *wire_from_) continue;
        const ImVec2 d = geometry->position - io.MousePos;
        if (d.x * d.x + d.y * d.y <= grab_radius * grab_radius * 2.0f) target = candidate;
      }
    }
    if (from && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      draw->AddLine(from->position, io.MousePos, target ? color_accent : color_muted, 2.0f);
    } else {
      if (target) {
        const PortRef a = *wire_from_, b = *target;
        actions.push_back([this, a, b] {
          project_.circuit.disconnect(a);
          project_.circuit.disconnect(b);
          project_.circuit.connections.push_back({a, b});
          touch();
        });
      }
      wire_from_.reset();
    }
  }

  // Legende und Leerhinweis
  const char* legend = "Farbton = Phase, Helligkeit = Leistung";
  draw->AddText({canvas_min.x + 8.0f, canvas_max.y - font_size - 6.0f}, color_muted, legend);
  if (project_.circuit.instances.empty()) {
    const char* hint = "Rechtsklick auf die Fläche oder Klick in der Bauteilbibliothek fügt ein Bauteil ein.";
    const ImVec2 size = ImGui::CalcTextSize(hint);
    draw->AddText({(canvas_min.x + canvas_max.x - size.x) * 0.5f, (canvas_min.y + canvas_max.y) * 0.5f},
                  color_muted, hint);
  }
  draw->PopClipRect();

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  // Statuszeile
  if (!solve_error_.empty())
    ImGui::TextColored(color_bad, "%s", solve_error_.c_str());
  else if (status_is_error_)
    ImGui::TextColored(color_bad, "%s", status_.c_str());
  else if (!status_.empty())
    ImGui::TextColored(color_good, "%s", status_.c_str());
  else
    ImGui::TextDisabled(
        "Port ziehen: verbinden | Rechtsklick: Menü | Mausrad: Zoom | Fläche ziehen: verschieben");

  ImGui::End();

  for (auto& action : actions) action();
}

}  // namespace photonlab::gui
