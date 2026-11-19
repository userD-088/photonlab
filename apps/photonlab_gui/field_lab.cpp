#include "field_lab.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <thread>

#include "implot.h"
#include "photonlab/core/units.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "photonlab/field/sparameters.hpp"
#include "photonlab/field/spectrum.hpp"
#include "photonlab/materials/material.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

using field::FieldScene;
using units::um;

const ImU32 field_lab_color_source = IM_COL32(255, 176, 74, 255);
const ImU32 field_lab_color_monitor = IM_COL32(93, 220, 140, 255);
const ImU32 color_source = field_lab_color_source;
const ImU32 color_monitor = field_lab_color_monitor;
const ImU32 color_disabled = IM_COL32(150, 150, 150, 90);  // deaktivierte Objekte
const ImU32 color_port = IM_COL32(214, 132, 255, 255);
const ImU32 color_outline = IM_COL32(227, 232, 239, 110);
const ImU32 color_selected = IM_COL32(255, 255, 255, 255);
const ImU32 color_pml = IM_COL32(152, 164, 182, 120);

struct Rgb {
  float r, g, b;
};

// Divergierende Skala für das Feld: Blau (negativ) über Dunkel (null) nach Rot (positiv).
Rgb diverging(float v) {
  const float a = std::min(std::abs(v), 1.0f);
  const float glow = a * a * 0.55f;  // sehr starke Felder hellen zur Mitte hin auf
  if (v >= 0.0f) return {0.06f + 0.94f * a, 0.07f + glow, 0.10f + glow * 0.5f};
  return {0.06f + glow * 0.6f, 0.07f + 0.55f * a, 0.10f + 0.90f * a};
}

// Helligkeitsskala für die Intensität, an "Inferno" angelehnt (fünf Stützfarben, linear gemischt).
Rgb inferno(float v) {
  static const Rgb stops[] = {{0.0f, 0.0f, 0.016f},
                              {0.341f, 0.063f, 0.431f},
                              {0.737f, 0.216f, 0.329f},
                              {0.976f, 0.557f, 0.035f},
                              {0.988f, 1.0f, 0.643f}};
  const float x = std::clamp(v, 0.0f, 1.0f) * 4.0f;
  const int i = std::min(static_cast<int>(x), 3);
  const float t = x - static_cast<float>(i);
  return {stops[i].r + t * (stops[i + 1].r - stops[i].r), stops[i].g + t * (stops[i + 1].g - stops[i].g),
          stops[i].b + t * (stops[i + 1].b - stops[i].b)};
}

double distance_to_segment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double length2 = dx * dx + dy * dy;
  double t = length2 > 0.0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2 : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Eingabefeld für eine Länge in Mikrometern; true bei Änderung
bool drag_um(const char* label, double* value, double low, double high) {
  double shown = *value / um;
  const double lo = low / um, hi = high / um;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragScalar(label, ImGuiDataType_Double, &shown, 0.01f, &lo, &hi, "%.3f µm",
                        ImGuiSliderFlags_AlwaysClamp)) {
    *value = std::clamp(shown, lo, hi) * um;
    return true;
  }
  return false;
}

// Brechzahl mit Schnellwahl aus der Materialbibliothek (bei der Wellenlänge der ersten Quelle)
bool drag_index(const char* label, double* value, const FieldScene& scene) {
  const double lo = 1.0, hi = 5.0;
  bool changed = false;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragScalar(label, ImGuiDataType_Double, value, 0.005f, &lo, &hi, "%.3f",
                        ImGuiSliderFlags_AlwaysClamp))
    changed = true;
  ImGui::SameLine();
  ImGui::PushID(label);
  if (ImGui::SmallButton("Material")) ImGui::OpenPopup("material");
  if (ImGui::BeginPopup("material")) {
    auto entry = [&](const std::string& name, double index, const std::string& note) {
      char text[128];
      std::snprintf(text, sizeof text, "%s  (n = %.3f)", name.c_str(), index);
      if (ImGui::MenuItem(text)) {
        *value = index;
        changed = true;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", note.c_str());
    };
    for (const auto& material : materials::library())
      entry(material.label, field::material_index(scene, material.name),
            "Quelle: " + material.source +
                "\nBei der Wellenlänge der ersten Quelle; die Rechnung selbst hat keine Dispersion.");
    ImGui::Separator();
    // Draufsicht auf einen Chip: effektiver Index der Schicht statt der Brechzahl des Materials
    const bool te = scene.polarization == field::Polarization::hz;
    const std::string note =
        std::string("Effektiver Index der ") + (te ? "TE" : "TM") +
        "0-Mode der Schicht in SiO2 (Effektiv-Index-Methode),\npassend zur Polarisation der Szene.";
    entry("Si-Schicht 220 nm, von oben gesehen", field::layer_effective_index(scene, "Si", "SiO2", 220e-9),
          note);
    entry("Si3N4-Schicht 400 nm, von oben gesehen",
          field::layer_effective_index(scene, "Si3N4", "SiO2", 400e-9), note);
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return changed;
}

}  // namespace

FieldLab::FieldLab(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer, float ui_scale)
    : pool_(pool),
      designer_(designer),
      renderer_(renderer),
      ui_scale_(ui_scale),
      band_view_(pool, designer),
      mode_view_(pool, designer, renderer, ui_scale),
      wave3d_view_(pool, renderer),
      inverse_view_(pool, designer, renderer),
      sparam_view_(pool, designer) {}

FieldLab::~FieldLab() {
  pause();
  if (texture_) SDL_DestroyTexture(texture_);
}

// ---------------------------------------------------------------------------------------------
// Rechen-Job
// ---------------------------------------------------------------------------------------------

void FieldLab::pause() {
  if (!job_.valid()) return;
  control_->stop = true;
  job_.wait();
  job_ = {};
  fetch_snapshot();
}

void FieldLab::start() {
  if (job_.valid() || !sim_) return;
  const FieldScene& scene = designer_.project().field;
  control_ = std::make_shared<Control>();
  const std::size_t limit = stop_at_duration_ && scene.duration > 0.0
                                ? static_cast<std::size_t>(std::ceil(scene.duration / sim_->time_step()))
                                : 0;
  if (limit != 0 && sim_->step_count() >= limit) return;  // Laufzeit schon erreicht
  control_->stop_at = limit;
  control_->steps_per_frame = steps_per_frame_;
  control_->full_speed = full_speed_;
  if (!snapshot_) snapshot_ = std::make_shared<Snapshot>();
  // Der Job besitzt Löser, Steuerung und Schnappschuss über shared_ptr und greift nie auf FieldLab zu.
  job_ = pool_.submit([sim = sim_, control = control_, snapshot = snapshot_] {
    while (!control->stop) {
      const int steps = std::max(1, control->steps_per_frame.load());
      const std::size_t last = control->stop_at;
      for (int k = 0; k < steps && !(last != 0 && sim->step_count() >= last); ++k) sim->step();
      {
        std::lock_guard lock(snapshot->mutex);
        const auto& f = sim->field();
        snapshot->field.resize(f.size());
        std::transform(f.begin(), f.end(), snapshot->field.begin(),
                       [](double v) { return static_cast<float>(v); });
        snapshot->monitors = sim->monitors();
        snapshot->time = sim->time();
        snapshot->step = sim->step_count();
        snapshot->energy = sim->energy();
        snapshot->fresh = true;
      }
      if (last != 0 && sim->step_count() >= last) {
        control->finished = true;
        break;
      }
      // im Takt der Bildwiederholung rechnen: warten, bis die Oberfläche den Schnappschuss geholt hat
      while (!control->stop && !control->full_speed) {
        {
          std::lock_guard lock(snapshot->mutex);
          if (!snapshot->fresh) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  });
}

void FieldLab::fetch_snapshot() {
  if (!snapshot_) return;
  std::lock_guard lock(snapshot_->mutex);
  if (!snapshot_->fresh) return;
  field_.swap(snapshot_->field);
  monitors_ = snapshot_->monitors;
  time_ = snapshot_->time;
  step_ = snapshot_->step;
  energy_ = snapshot_->energy;
  snapshot_->fresh = false;
  texture_dirty_ = true;
}

void FieldLab::snapshot_from_sim() {
  if (!sim_) return;
  const auto& f = sim_->field();
  field_.resize(f.size());
  std::transform(f.begin(), f.end(), field_.begin(), [](double v) { return static_cast<float>(v); });
  monitors_ = sim_->monitors();
  time_ = sim_->time();
  step_ = sim_->step_count();
  energy_ = sim_->energy();
  texture_dirty_ = true;
}

void FieldLab::rebuild() {
  pause();
  scene_changed_ = false;
  error_.clear();
  sim_.reset();
  snapshot_ = std::make_shared<Snapshot>();
  field_.clear();
  monitors_.clear();
  spectrum_.clear();
  spectrum_step_ = static_cast<std::size_t>(-1);
  time_ = energy_ = 0.0;
  step_ = 0;
  if (!fixed_scale_) scale_ = 0.0f;
  const FieldScene& scene = designer_.project().field;
  if (scene.empty()) return;
  try {
    sim_ = std::make_shared<field::Fdtd>(scene);
  } catch (const std::exception& e) {
    error_ = e.what();
    return;
  }
  // Geometrie-Aufhellung: Brechzahl relativ zum Bereich der Szene
  const auto& eps = sim_->permittivity();
  const auto [low, high] = std::minmax_element(eps.begin(), eps.end());
  const double n_low = std::sqrt(*low), n_high = std::sqrt(*high);
  tint_.resize(eps.size());
  for (std::size_t k = 0; k < eps.size(); ++k)
    tint_[k] = n_high > n_low ? static_cast<float>((std::sqrt(eps[k]) - n_low) / (n_high - n_low)) : 0.0f;

  if (sim_->nx() != texture_nx_ || sim_->ny() != texture_ny_ || !texture_) {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_nx_ = sim_->nx(), texture_ny_ = sim_->ny();
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, texture_nx_,
                                 texture_ny_);
    if (texture_) SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    pixels_.assign(static_cast<std::size_t>(texture_nx_) * static_cast<std::size_t>(texture_ny_) * 4, 255);
  }
  snapshot_from_sim();
}

void FieldLab::update_texture() {
  if (!texture_ || !texture_dirty_ || field_.size() * 4 != pixels_.size()) return;
  texture_dirty_ = false;
  // Farbskala folgt dem größten Betrag und fällt nur langsam ab, damit das Bild nicht flackert
  if (!fixed_scale_) {
    float peak = 0.0f;
    for (float v : field_) peak = std::max(peak, std::abs(v));
    scale_ = std::max(peak, scale_ * 0.995f);
  }
  const float inverse = scale_ > 0.0f ? 1.0f / scale_ : 0.0f;
  const bool intensity = display_mode_ == DisplayMode::intensity;
  for (std::size_t k = 0; k < field_.size(); ++k) {
    float v = field_[k] * inverse;
    // schwache Felder anheben: Betrag hoch 0,6 (nur die Darstellung, nicht die Rechnung)
    if (boost_weak_) v = std::copysign(std::pow(std::abs(v), 0.6f), v);
    Rgb c = intensity ? inferno(v * v) : diverging(v);
    if (show_geometry_) {
      const float t = 0.20f * tint_[k];  // Material als graue Aufhellung
      c = {c.r + t * (0.75f - c.r), c.g + t * (0.78f - c.g), c.b + t * (0.82f - c.b)};
    }
    pixels_[4 * k + 0] = static_cast<std::uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
    pixels_[4 * k + 1] = static_cast<std::uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
    pixels_[4 * k + 2] = static_cast<std::uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
  }
  SDL_UpdateTexture(texture_, nullptr, pixels_.data(), texture_nx_ * 4);
}

void FieldLab::update_spectrum() {
  if (++frames_since_spectrum_ < 30 || step_ == spectrum_step_) return;
  frames_since_spectrum_ = 0;
  spectrum_step_ = step_;
  const FieldScene& scene = designer_.project().field;
  spectrum_.clear();
  spectrum_names_.clear();
  if (scene.sources.empty()) return;
  // Wellenlängen um den Träger der ersten Quelle
  const double center = scene.sources.front().wavelength;
  const int points = 161;
  std::vector<double> frequencies(points);
  spectrum_wavelength_nm_.resize(points);
  for (int i = 0; i < points; ++i) {
    const double wavelength = center * (0.6 + 1.0 * i / (points - 1));
    spectrum_wavelength_nm_[static_cast<std::size_t>(i)] = wavelength / units::nm;
    frequencies[static_cast<std::size_t>(i)] = constants::c0 / wavelength;
  }
  for (const auto& monitor : monitors_) {
    if (monitor.kind == field::MonitorKind::flux || monitor.values.empty()) continue;
    // höchstens die letzten 30000 Proben, damit die Oberfläche flüssig bleibt
    const std::size_t count = std::min<std::size_t>(monitor.values.size(), 30000);
    const std::vector<double> tail(monitor.values.end() - static_cast<std::ptrdiff_t>(count),
                                   monitor.values.end());
    spectrum_.push_back(field::amplitude_spectrum(tail, monitor.sample_interval, frequencies));
    spectrum_names_.push_back(monitor.name);
  }
}

// ---------------------------------------------------------------------------------------------
// Oberfläche
// ---------------------------------------------------------------------------------------------

void FieldLab::draw() {
  // Neues Projekt geladen: Löser neu aufsetzen und bei reinen Feld-Projekten nach vorn kommen
  if (designer_.project_generation() != seen_generation_) {
    seen_generation_ = designer_.project_generation();
    clear_selection();
    want_running_ = true;
    rebuild();
    const io::Project& project = designer_.project();
    if (!project.field.empty() && project.circuit.instances.empty()) ImGui::SetWindowFocus(window_name);
  }
  if (scene_changed_ && !dragging_) {
    const bool was_wanted = want_running_;
    rebuild();
    want_running_ = was_wanted;
  }

  const bool visible = ImGui::Begin(window_name);
  if (!visible) {
    pause();  // unsichtbar: keine Rechenzeit verbrauchen
    ImGui::End();
    return;
  }
  // Ansichten: das Feld von oben (2D-FDTD), der Querschnitt eines Wellenleiters (Modenlöser), ein gerader
  // Wellenleiter im 3D-FDTD, Bandstruktur und S-Parameter
  if (ImGui::BeginTabBar("ansichten")) {
    if (ImGui::BeginTabItem("Feld (2D-FDTD)")) {
      draw_field_tab();
      ImGui::EndTabItem();
    } else {
      pause();  // im anderen Reiter keine Rechenzeit verbrauchen
    }
    if (ImGui::BeginTabItem("Querschnitt (Moden)")) {
      mode_view_.draw();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("3D-Wellenleiter")) {
      wave3d_view_.draw();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Inverses Design")) {
      if (inverse_view_.draw()) {
        scene_changed_ = true;
        clear_selection();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Bandstruktur")) {
      if (band_view_.draw()) {
        scene_changed_ = true;
        clear_selection();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("S-Parameter")) {
      sparam_view_.draw();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void FieldLab::draw_field_tab() {
  FieldScene& scene = designer_.project().field;
  if (scene.empty()) {
    draw_empty_state();
    return;
  }

  if (running() && control_->finished) {
    pause();
    want_running_ = false;
  }
  if (want_running_ && !running() && sim_) start();
  if (running()) fetch_snapshot();
  update_texture();

  const float side_width = ImGui::GetFontSize() * 19.0f;
  ImGui::BeginChild("seite", ImVec2(side_width, 0.0f), ImGuiChildFlags_Borders);
  // Reihenfolge: Rechnung, neue Objekte, Eigenschaften der Auswahl, Objektliste, Rechengebiet
  draw_controls();
  draw_add_toolbar();
  draw_selected_object();
  draw_objects();
  draw_scene_settings();
  ImGui::EndChild();
  handle_shortcuts();
  ImGui::SameLine();
  ImGui::BeginChild("haupt", ImVec2(0.0f, 0.0f));
  const float available = ImGui::GetContentRegionAvail().y;
  const float plot_height =
      monitors_.empty() ? 0.0f : std::max(available * 0.34f, ImGui::GetFontSize() * 9.0f);
  draw_canvas(available - plot_height - (monitors_.empty() ? 0.0f : ImGui::GetStyle().ItemSpacing.y));
  if (!monitors_.empty()) draw_plots();
  ImGui::EndChild();
}

void FieldLab::draw_empty_state() {
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 42.0f);
  ImGui::TextUnformatted("Das Bauteil-Labor rechnet Licht als elektromagnetisches Feld (2D-FDTD).");
  ImGui::TextDisabled(
      "Formen aus Materialien mit verschiedener Brechzahl, Quellen mit einstellbarer Phase und Monitore; das "
      "Feld "
      "breitet sich live aus. Gerechnet wird in zwei Dimensionen: was auf einem echten Chip nach oben und "
      "unten "
      "abstrahlt, fehlt, und die Brechzahlen haben keine Dispersion.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::SeparatorText("Beispiele laden");
  for (const auto& name : field::reference_scene_names()) {
    if (ImGui::Button(field::reference_scene(name).name.c_str())) designer_.load_example(name);
  }
  ImGui::Spacing();
  if (ImGui::Button("Leere Szene in dieses Projekt einfügen")) {
    FieldScene scene;
    scene.name = "Neue Szene";
    scene.width = 10 * um, scene.height = 6 * um;
    scene.cell_size = 40e-9;
    field::Source source;
    source.name = "Quelle";
    source.kind = field::SourceKind::point;
    source.x1 = 3 * um, source.y1 = 3 * um;
    scene.sources = {source};
    designer_.project().field = scene;
    designer_.mark_dirty();
    rebuild();
  }
}

void FieldLab::draw_controls() {
  FieldScene& scene = designer_.project().field;
  ImGui::SeparatorText("Rechnung");
  if (!error_.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_bad, "%s", error_.c_str());
    ImGui::PopTextWrapPos();
    return;
  }
  if (ImGui::Button(want_running_ && running() ? "Pause" : "Start")) {
    if (running()) {
      pause();
      want_running_ = false;
    } else {
      // am Ende der Laufzeit beginnt Start von vorn
      if (control_ && control_->finished) rebuild();
      want_running_ = true;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Zurücksetzen")) {
    const bool wanted = want_running_;
    rebuild();
    want_running_ = wanted;
  }
  ImGui::SameLine();
  ImGui::Text("%.1f fs", time_ / units::fs);

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  ImGui::SliderInt("Schritte je Bild", &steps_per_frame_, 1, 60);
  ImGui::Checkbox("volle Geschwindigkeit", &full_speed_);
  if (control_) {
    control_->steps_per_frame = steps_per_frame_;
    control_->full_speed = full_speed_;
  }
  double duration_fs = scene.duration / units::fs;
  const double zero = 0.0, long_time = 1e5;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragScalar("Laufzeit", ImGuiDataType_Double, &duration_fs, 1.0f, &zero, &long_time, "%.0f fs",
                        ImGuiSliderFlags_AlwaysClamp)) {
    scene.duration = duration_fs * units::fs;
    designer_.mark_dirty();  // ändert die Physik nicht, nur wann die Rechnung endet
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Simulierte Zeit, nach der die Rechnung anhält; 0 = läuft, bis Pause gedrückt wird.");
  if (scene.duration > 0.0) {
    ImGui::SameLine();
    if (ImGui::Checkbox("anhalten", &stop_at_duration_) && running()) {
      pause();  // neu starten, damit die Grenze gilt
    }
  }
  if (sim_) {
    ImGui::TextDisabled("Schritt %zu, dt = %.4f fs", step_, sim_->time_step() / units::fs);
    ImGui::TextDisabled("Energie %.3e J/m", energy_);
  }

  ImGui::SeparatorText("Darstellung");
  int mode = display_mode_ == DisplayMode::field ? 0 : 1;
  const char* field_name = scene.polarization == field::Polarization::ez ? "Feld Ez" : "Feld Hz";
  const char* modes[] = {field_name, "Intensität (Feld²)"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  if (ImGui::Combo("##modus", &mode, modes, 2)) {
    display_mode_ = mode == 0 ? DisplayMode::field : DisplayMode::intensity;
    texture_dirty_ = true;
  }
  if (ImGui::Checkbox("Geometrie zeigen", &show_geometry_)) texture_dirty_ = true;
  if (ImGui::Checkbox("schwache Felder anheben", &boost_weak_)) texture_dirty_ = true;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(
        "Nichtlineare Farbskala (Betrag hoch 0,6): macht Streulicht und\nschwache Wellen sichtbar. Die "
        "Farben sind dann nicht mehr proportional zum Feld.");
  if (ImGui::Checkbox("Farbskala fest", &fixed_scale_)) texture_dirty_ = true;
  if (fixed_scale_) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::DragFloat("Vollausschlag", &scale_, std::max(scale_ * 0.01f, 1e-9f), 0.0f, 1e9f, "%.3g"))
      texture_dirty_ = true;
  } else {
    ImGui::TextDisabled("Vollausschlag %.3g %s", static_cast<double>(scale_),
                        scene.polarization == field::Polarization::ez ? "V/m" : "A/m");
  }

  // Hinweise zur Qualität der Rechnung
  const auto warnings = field::check(scene);
  ImGui::SeparatorText("Hinweise");
  ImGui::PushTextWrapPos(0.0f);
  for (const auto& w : warnings)
    ImGui::TextColored(w.severe ? ui_color_bad : ui_color_limited, "%s", w.text.c_str());
  ImGui::TextDisabled(
      "2D statt 3D: Abstrahlung nach oben und unten fehlt, Brechzahlen ohne Dispersion, "
      "Kanten in Treppenstufen. Flüsse gelten je Meter Bauteilhöhe.");
  ImGui::PopTextWrapPos();
}

void FieldLab::draw_scene_settings() {
  FieldScene& scene = designer_.project().field;
  if (!ImGui::CollapsingHeader("Rechengebiet (Größe, Gitter, Ränder)")) return;
  bool changed = false;
  changed |= drag_um("Breite", &scene.width, 1 * um, 200 * um);
  changed |= drag_um("Höhe", &scene.height, 1 * um, 200 * um);
  double cell_nm = scene.cell_size / units::nm;
  const double lo = 5.0, hi = 500.0;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragScalar("Zellgröße", ImGuiDataType_Double, &cell_nm, 0.2f, &lo, &hi, "%.1f nm",
                        ImGuiSliderFlags_AlwaysClamp)) {
    scene.cell_size = cell_nm * units::nm;
    changed = true;
  }
  changed |= drag_index("Hintergrund n", &scene.background_index, scene);

  int polarization = scene.polarization == field::Polarization::ez ? 0 : 1;
  const char* polarizations[] = {"Ez (E senkrecht zur Ebene)", "Hz (E in der Ebene)"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  if (ImGui::Combo("##polarisation", &polarization, polarizations, 2)) {
    scene.polarization = polarization == 0 ? field::Polarization::ez : field::Polarization::hz;
    changed = true;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(
        "In der Draufsicht auf einen Chip entspricht Hz der TE-artigen,\nEz der TM-artigen Mode.");
  const char* boundaries[] = {"absorbierend (CPML)", "periodisch"};
  for (auto [label, boundary] :
       {std::pair{"Rand in x", &scene.boundary_x}, std::pair{"Rand in y", &scene.boundary_y}}) {
    int b = *boundary == field::Boundary::pml ? 0 : 1;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::Combo(label, &b, boundaries, 2)) {
      *boundary = b == 0 ? field::Boundary::pml : field::Boundary::periodic;
      changed = true;
    }
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragInt("Randschicht [Zellen]", &scene.pml_cells, 0.2f, 0, 60, "%d",
                     ImGuiSliderFlags_AlwaysClamp))
    changed = true;
  if (sim_) {
    ImGui::TextDisabled("Gitter %d x %d Zellen", sim_->nx(), sim_->ny());
    const double cells = field::cells_per_wavelength(scene);
    if (cells > 0.0) ImGui::TextDisabled("%.1f Zellen je Wellenlänge im Material", cells);
  }
  if (changed) {
    scene_changed_ = true;
    designer_.mark_dirty();
  }
}

void FieldLab::draw_add_toolbar() {
  FieldScene& scene = designer_.project().field;
  ImGui::SeparatorText("Neues Objekt");
  bool changed = false;
  const double cx = scene.width / 2, cy = scene.height / 2;
  if (ImGui::SmallButton("+ Rechteck")) {
    field::Shape s;
    s.name = "Rechteck " + std::to_string(scene.shapes.size() + 1);
    s.x = cx, s.y = cy, s.width = scene.width / 4, s.height = 0.45 * um;
    s.index = 2.85;
    scene.shapes.push_back(s);
    select({ObjectKind::shape, static_cast<int>(scene.shapes.size()) - 1}, false);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+ Kreis")) {
    field::Shape s;
    s.name = "Kreis " + std::to_string(scene.shapes.size() + 1);
    s.kind = field::ShapeKind::circle;
    s.x = cx, s.y = cy, s.radius = std::min(scene.width, scene.height) / 10;
    s.index = 2.85;
    scene.shapes.push_back(s);
    select({ObjectKind::shape, static_cast<int>(scene.shapes.size()) - 1}, false);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+ Ring")) {
    field::Shape s;
    s.name = "Ring " + std::to_string(scene.shapes.size() + 1);
    s.kind = field::ShapeKind::ring;
    s.x = cx, s.y = cy, s.radius = std::min(scene.width, scene.height) / 5;
    s.inner_radius = s.radius - 0.45 * um > 0.0 ? s.radius - 0.45 * um : s.radius / 2;
    s.index = 2.85;
    scene.shapes.push_back(s);
    select({ObjectKind::shape, static_cast<int>(scene.shapes.size()) - 1}, false);
    changed = true;
  }
  if (ImGui::SmallButton("+ Quelle")) {
    field::Source s;
    s.name = "Quelle " + std::to_string(scene.sources.size() + 1);
    s.kind = field::SourceKind::point;
    s.x1 = s.x2 = scene.width / 4, s.y1 = s.y2 = cy;
    if (!scene.sources.empty()) s.wavelength = scene.sources.front().wavelength;
    scene.sources.push_back(s);
    select({ObjectKind::source, static_cast<int>(scene.sources.size()) - 1}, false);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+ Monitor")) {
    field::Monitor m;
    m.name = "Monitor " + std::to_string(scene.monitors.size() + 1);
    m.x1 = m.x2 = 0.75 * scene.width, m.y1 = m.y2 = cy;
    scene.monitors.push_back(m);
    select({ObjectKind::monitor, static_cast<int>(scene.monitors.size()) - 1}, false);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+ Port")) {
    // neuer Port senkrecht über die Mitte der linken bzw. rechten Hälfte; Ports ändern die Rechnung nicht
    field::Port p;
    p.name = "p" + std::to_string(scene.ports.size() + 1);
    const bool right = scene.ports.size() % 2 == 1;
    p.x1 = p.x2 = right ? 0.8 * scene.width : 0.2 * scene.width;
    p.y1 = std::max(0.0, cy - 1 * um), p.y2 = std::min(scene.height, cy + 1 * um);
    scene.ports.push_back(p);
    select({ObjectKind::port, static_cast<int>(scene.ports.size()) - 1}, false);
    designer_.mark_dirty();
  }
  if (changed) {
    scene_changed_ = true;
    designer_.mark_dirty();
  }
}

void FieldLab::draw_objects() {
  FieldScene& scene = designer_.project().field;
  ImGui::SeparatorText("Objekte");
  ImGui::TextDisabled("Strg+Klick: mehrere, Rechtsklick: Menü");
  auto row = [&](ObjectKind kind, int index, const std::string& name, bool enabled, const char* prefix,
                 ImU32 color) {
    ImGui::PushID(static_cast<int>(kind) * 100000 + index);
    ImGui::PushStyleColor(ImGuiCol_Text, enabled ? color : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    const std::string label =
        std::string(prefix) + (name.empty() ? "(ohne Namen)" : name) + (enabled ? "" : "  (aus)");
    if (ImGui::Selectable(label.c_str(), is_selected(kind, index))) {
      const ImGuiIO& io = ImGui::GetIO();
      select({kind, index}, io.KeyCtrl || io.KeyShift);
    }
    ImGui::PopStyleColor();
    if (ImGui::BeginPopupContextItem("kontext")) {
      if (!is_selected(kind, index)) select({kind, index}, false);
      draw_context_items();
      ImGui::EndPopup();
    }
    ImGui::PopID();
  };
  for (int i = 0; i < static_cast<int>(scene.shapes.size()); ++i) {
    const auto& s = scene.shapes[static_cast<std::size_t>(i)];
    row(ObjectKind::shape, i, s.name, s.enabled, "Form: ", IM_COL32(227, 232, 239, 255));
  }
  for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i) {
    const auto& s = scene.sources[static_cast<std::size_t>(i)];
    row(ObjectKind::source, i, s.name, s.enabled, "Quelle: ", color_source);
  }
  for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i) {
    const auto& m = scene.monitors[static_cast<std::size_t>(i)];
    row(ObjectKind::monitor, i, m.name, m.enabled, "Monitor: ", color_monitor);
  }
  for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i)
    row(ObjectKind::port, i, scene.ports[static_cast<std::size_t>(i)].name, true, "Port: ", color_port);
}

void FieldLab::draw_selected_object() {
  FieldScene& scene = designer_.project().field;
  if (selected_kind_ == ObjectKind::none || selected_index_ < 0) return;
  const auto index = static_cast<std::size_t>(selected_index_);
  bool changed = false, removed = false;
  auto name_field = [&](std::string* name) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%s", name->c_str());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::InputText("Name", buffer, sizeof buffer)) {
      *name = buffer;
      designer_.mark_dirty();
    }
  };
  ImGui::SeparatorText("Ausgewählt");
  ImGui::PushID("auswahl");
  const double w = scene.width, h = scene.height;
  if (selection_.size() > 1) {
    // mehrere Objekte: gemeinsam verschieben, schalten, löschen; Werte einzeln über die Liste
    ImGui::Text("%zu Objekte ausgewählt", selection_.size());
    double dx = 0.0, dy = 0.0;
    if (drag_um("x verschieben", &dx, -w, w) | drag_um("y verschieben", &dy, -h, h)) move_selection(dx, dy);
    const bool any_enabled = selection_enabled();
    if (ImGui::SmallButton(any_enabled ? "Deaktivieren" : "Aktivieren")) set_selection_enabled(!any_enabled);
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplizieren")) paste(true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Löschen")) delete_selection();
    ImGui::PopID();
    return;
  }
  auto enabled_box = [&](bool* enabled) {
    if (ImGui::Checkbox("aktiv", enabled)) changed = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Deaktivierte Objekte bleiben in der Szene, wirken aber nicht auf die Rechnung.");
  };
  if (selected_kind_ == ObjectKind::shape && index < scene.shapes.size()) {
    field::Shape& s = scene.shapes[index];
    name_field(&s.name);
    enabled_box(&s.enabled);
    changed |= drag_um("x", &s.x, 0.0, w);
    changed |= drag_um("y", &s.y, 0.0, h);
    if (s.kind == field::ShapeKind::rectangle) {
      changed |= drag_um("Breite", &s.width, 10e-9, 2 * w);
      changed |= drag_um("Höhe", &s.height, 10e-9, 2 * h);
      double degrees = s.rotation * 180.0 / constants::pi;
      const double lo = -180.0, hi = 180.0;
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
      if (ImGui::DragScalar("Drehung", ImGuiDataType_Double, &degrees, 0.5f, &lo, &hi, "%.1f°",
                            ImGuiSliderFlags_AlwaysClamp)) {
        s.rotation = degrees * constants::pi / 180.0;
        changed = true;
      }
    } else {
      changed |= drag_um("Radius", &s.radius, 10e-9, std::max(w, h));
      if (s.kind == field::ShapeKind::ring) {
        changed |= drag_um("Innenradius", &s.inner_radius, 0.0, s.radius - 1e-9);
        s.inner_radius = std::min(s.inner_radius, s.radius - 1e-9);
      }
    }
    changed |= drag_index("Brechzahl", &s.index, scene);
    if (ImGui::SmallButton("Form löschen")) {
      delete_selection();
      removed = true;
    }
  } else if (selected_kind_ == ObjectKind::source && index < scene.sources.size()) {
    field::Source& s = scene.sources[index];
    name_field(&s.name);
    enabled_box(&s.enabled);
    int kind = static_cast<int>(s.kind);
    const char* kinds[] = {"Punkt", "Linie", "Mode"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::Combo("Art", &kind, kinds, 3)) {
      s.kind = static_cast<field::SourceKind>(kind);
      if (s.kind != field::SourceKind::point && s.x1 == s.x2 && s.y1 == s.y2) {
        s.y1 = std::max(0.0, s.y1 - 1 * um);
        s.y2 = std::min(h, s.y2 + 1 * um);
      }
      changed = true;
    }
    changed |= drag_um(s.kind == field::SourceKind::point ? "x" : "x Anfang", &s.x1, 0.0, w);
    changed |= drag_um(s.kind == field::SourceKind::point ? "y" : "y Anfang", &s.y1, 0.0, h);
    if (s.kind != field::SourceKind::point) {
      changed |= drag_um("x Ende", &s.x2, 0.0, w);
      changed |= drag_um("y Ende", &s.y2, 0.0, h);
    }
    if (s.kind == field::SourceKind::line) {
      changed |= drag_um("Strahlbreite", &s.beam_width, 0.0, std::max(w, h));
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("1/e-Halbbreite eines Gauß-Profils entlang der Linie; 0 = gleichmäßig.");
    }
    if (s.kind == field::SourceKind::mode) {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
      if (ImGui::DragInt("Modenordnung", &s.mode_order, 0.05f, 0, 9, "%d", ImGuiSliderFlags_AlwaysClamp))
        changed = true;
      ImGui::TextDisabled(
          "Linie quer über den Wellenleiter legen\n(waagerecht oder senkrecht, bis in den Mantel).");
    }
    double wavelength_nm = s.wavelength / units::nm;
    const double lo = 200.0, hi = 20000.0;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::DragScalar("Wellenlänge", ImGuiDataType_Double, &wavelength_nm, 1.0f, &lo, &hi, "%.1f nm",
                          ImGuiSliderFlags_AlwaysClamp)) {
      s.wavelength = wavelength_nm * units::nm;
      changed = true;
    }
    double degrees = s.phase * 180.0 / constants::pi;
    const double p_lo = -360.0, p_hi = 360.0;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::DragScalar("Phase", ImGuiDataType_Double, &degrees, 1.0f, &p_lo, &p_hi, "%.0f°",
                          ImGuiSliderFlags_AlwaysClamp)) {
      s.phase = degrees * constants::pi / 180.0;
      changed = true;
    }
    const double a_lo = 0.0, a_hi = 1e9;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::DragScalar("Amplitude", ImGuiDataType_Double, &s.amplitude, 0.01f, &a_lo, &a_hi, "%.3g V/m",
                          ImGuiSliderFlags_AlwaysClamp))
      changed = true;
    int signal = s.signal == field::SignalKind::continuous ? 0 : 1;
    const char* signals[] = {"Dauerstrich", "Puls"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::Combo("Signal", &signal, signals, 2)) {
      s.signal = signal == 0 ? field::SignalKind::continuous : field::SignalKind::pulse;
      changed = true;
    }
    if (s.signal == field::SignalKind::pulse) {
      double fs = s.pulse_duration / units::fs;
      const double f_lo = 1.0, f_hi = 1000.0;
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
      if (ImGui::DragScalar("Pulsdauer", ImGuiDataType_Double, &fs, 0.2f, &f_lo, &f_hi, "%.1f fs",
                            ImGuiSliderFlags_AlwaysClamp)) {
        s.pulse_duration = fs * units::fs;
        changed = true;
      }
    }
    if (ImGui::SmallButton("Quelle löschen")) {
      delete_selection();
      removed = true;
    }
  } else if (selected_kind_ == ObjectKind::monitor && index < scene.monitors.size()) {
    field::Monitor& m = scene.monitors[index];
    name_field(&m.name);
    enabled_box(&m.enabled);
    int kind = static_cast<int>(m.kind);
    const char* kinds[] = {"Feld an einem Punkt", "Leistungsfluss durch eine Linie", "Amplitude einer Mode"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
    if (ImGui::Combo("##art", &kind, kinds, 3)) {
      m.kind = static_cast<field::MonitorKind>(kind);
      if (m.kind != field::MonitorKind::point && m.x1 == m.x2 && m.y1 == m.y2) {
        m.x2 = m.x1;
        m.y1 = std::max(0.0, m.y1 - 1 * um);
        m.y2 = std::min(h, m.y2 + 1 * um);
      }
      changed = true;
    }
    changed |= drag_um(m.kind == field::MonitorKind::point ? "x" : "x Anfang", &m.x1, 0.0, w);
    changed |= drag_um(m.kind == field::MonitorKind::point ? "y" : "y Anfang", &m.y1, 0.0, h);
    if (m.kind != field::MonitorKind::point) {
      changed |= drag_um("x Ende", &m.x2, 0.0, w);
      changed |= drag_um("y Ende", &m.y2, 0.0, h);
    }
    if (m.kind == field::MonitorKind::flux)
      ImGui::TextDisabled("Linie waagerecht oder senkrecht;\nFluss zählt nach rechts bzw. unten positiv.");
    if (m.kind == field::MonitorKind::mode) {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
      if (ImGui::DragInt("Modenordnung", &m.mode_order, 0.05f, 0, 9, "%d", ImGuiSliderFlags_AlwaysClamp))
        changed = true;
      ImGui::TextDisabled(
          "Überlapp des Feldes mit dem Modenprofil:\nzeigt, wie viel in dieser Mode steckt\n(beide "
          "Laufrichtungen zusammen).");
    }
    if (ImGui::SmallButton("Monitor löschen")) {
      delete_selection();
      removed = true;
    }
  } else if (selected_kind_ == ObjectKind::port && index < scene.ports.size()) {
    // Ports wirken nur bei der S-Parameter-Extraktion: Änderungen starten die Feldrechnung nicht neu
    field::Port& p = scene.ports[index];
    name_field(&p.name);
    bool port_changed = false;
    port_changed |= drag_um("x Anfang", &p.x1, 0.0, w);
    port_changed |= drag_um("y Anfang", &p.y1, 0.0, h);
    port_changed |= drag_um("x Ende", &p.x2, 0.0, w);
    port_changed |= drag_um("y Ende", &p.y2, 0.0, h);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    port_changed |=
        ImGui::DragInt("Modenordnung", &p.mode_order, 0.05f, 0, 9, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextDisabled(
        "Linie quer über den Wellenleiter, bis in den\nMantel; waagerecht oder senkrecht. Die Richtung\nins "
        "Bauteil folgt aus der Lage (Pfeil).");
    if (port_changed) designer_.mark_dirty();
    if (ImGui::SmallButton("Port löschen")) {
      delete_selection();
    }
  }
  ImGui::PopID();
  if (changed && !removed) {
    scene_changed_ = true;
    designer_.mark_dirty();
  }
}

void FieldLab::draw_canvas(float height) {
  FieldScene& scene = designer_.project().field;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::BeginChild(
      "bild", ImVec2(0.0f, height), ImGuiChildFlags_None,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
  const ImVec2 area_min = ImGui::GetCursorScreenPos();
  const ImVec2 area = ImGui::GetContentRegionAvail();
  const ImVec2 area_max = {area_min.x + area.x, area_min.y + area.y};
  // Eingepasst bei Zoom 1; die Mitte der Szene liegt in der Mitte der Fläche, verschoben um pan_
  const float fit = static_cast<float>(std::min(area.x / scene.width, area.y / scene.height));
  const float px_per_m = fit * zoom_;
  const ImVec2 center = {area_min.x + 0.5f * area.x + pan_.x, area_min.y + 0.5f * area.y + pan_.y};
  const ImVec2 origin = {center.x - 0.5f * static_cast<float>(scene.width) * px_per_m,
                         center.y - 0.5f * static_cast<float>(scene.height) * px_per_m};
  const ImVec2 size = {static_cast<float>(scene.width) * px_per_m,
                       static_cast<float>(scene.height) * px_per_m};
  canvas_origin_ = origin;
  px_per_m_ = px_per_m;
  ImDrawList* draw = ImGui::GetWindowDrawList();

  draw->PushClipRect(area_min, area_max, true);
  if (texture_ && error_.empty())
    draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture_)), origin,
                   {origin.x + size.x, origin.y + size.y});
  else
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(10, 14, 20, 255));
  draw->PopClipRect();
  ImGui::SetNextItemAllowOverlap();  // die Knöpfe der Ansicht liegen über der Fläche
  ImGui::InvisibleButton("##flaeche", ImVec2(std::max(area.x, 1.0f), std::max(area.y, 1.0f)),
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const double mouse_x = (io.MousePos.x - origin.x) / px_per_m,
               mouse_y = (io.MousePos.y - origin.y) / px_per_m;

  // Mausrad: Zoom um den Mauszeiger; mittlere Maustaste: verschieben
  if (hovered && io.MouseWheel != 0.0f) {
    const float new_zoom = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), 0.25f, 50.0f);
    const float new_px = fit * new_zoom;
    // der Punkt unter dem Mauszeiger bleibt stehen
    const ImVec2 new_origin = {io.MousePos.x - static_cast<float>(mouse_x) * new_px,
                               io.MousePos.y - static_cast<float>(mouse_y) * new_px};
    pan_ = {new_origin.x + 0.5f * static_cast<float>(scene.width) * new_px - area_min.x - 0.5f * area.x,
            new_origin.y + 0.5f * static_cast<float>(scene.height) * new_px - area_min.y - 0.5f * area.y};
    zoom_ = new_zoom;
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
    pan_.x += io.MouseDelta.x;
    pan_.y += io.MouseDelta.y;
  }

  // Linksklick: auswählen (Strg/Umschalt: hinzufügen), ins Leere: Rechteck aufziehen
  if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ObjectRef hit = pick(io.MousePos);
    const bool additive = io.KeyCtrl || io.KeyShift;
    drag_armed_ = false;
    if (hit.kind == ObjectKind::none) {
      if (!additive) clear_selection();
      box_selecting_ = true;
      box_start_ = io.MousePos;
    } else if (additive) {
      select(hit, true);
    } else {
      if (!is_selected(hit.kind, hit.index)) {
        select(hit, false);
      } else {
        // schon ausgewählt: Auswahl behalten (zum gemeinsamen Ziehen), Eigenschaften dieses Objekts zeigen
        std::erase(selection_, hit);
        selection_.push_back(hit);
        selected_kind_ = hit.kind, selected_index_ = hit.index;
      }
      drag_armed_ = true;
    }
  }
  // Ziehen verschiebt alle ausgewählten Objekte; neu gerechnet wird beim Loslassen
  if (ImGui::IsItemActive() && drag_armed_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) &&
      (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    move_selection(io.MouseDelta.x / px_per_m, io.MouseDelta.y / px_per_m);
    dragging_ = true;
  }
  if (box_selecting_) {
    const ImVec2 lo = {std::min(box_start_.x, io.MousePos.x), std::min(box_start_.y, io.MousePos.y)};
    const ImVec2 hi = {std::max(box_start_.x, io.MousePos.x), std::max(box_start_.y, io.MousePos.y)};
    draw->AddRectFilled(lo, hi, IM_COL32(120, 170, 255, 40));
    draw->AddRect(lo, hi, IM_COL32(120, 170, 255, 200));
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      box_selecting_ = false;
      if (hi.x - lo.x > 3.0f || hi.y - lo.y > 3.0f) {
        auto inside = [&](double x, double y) {
          const ImVec2 p = to_screen(x, y);
          return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
        };
        auto add = [&](ObjectKind kind, int i) {
          if (!is_selected(kind, i)) select({kind, i}, true);
        };
        for (int i = 0; i < static_cast<int>(scene.shapes.size()); ++i)
          if (const auto& o = scene.shapes[static_cast<std::size_t>(i)]; inside(o.x, o.y))
            add(ObjectKind::shape, i);
        for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i)
          if (const auto& o = scene.sources[static_cast<std::size_t>(i)];
              inside(0.5 * (o.x1 + o.x2), 0.5 * (o.y1 + o.y2)))
            add(ObjectKind::source, i);
        for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i)
          if (const auto& o = scene.monitors[static_cast<std::size_t>(i)];
              inside(0.5 * (o.x1 + o.x2), 0.5 * (o.y1 + o.y2)))
            add(ObjectKind::monitor, i);
        for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i)
          if (const auto& o = scene.ports[static_cast<std::size_t>(i)];
              inside(0.5 * (o.x1 + o.x2), 0.5 * (o.y1 + o.y2)))
            add(ObjectKind::port, i);
      }
    }
  }
  if (ImGui::IsItemDeactivated()) {
    dragging_ = false;
    drag_armed_ = false;
  }

  // Rechtsklick: Menü für das Objekt unter dem Mauszeiger bzw. die Auswahl
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    const ObjectRef hit = pick(io.MousePos);
    if (hit.kind != ObjectKind::none && !is_selected(hit.kind, hit.index)) select(hit, false);
    ImGui::OpenPopup("kontext");
  }
  if (ImGui::BeginPopup("kontext")) {
    draw_context_items();
    ImGui::EndPopup();
  }

  if (hovered && sim_ && !field_.empty() && mouse_x >= 0.0 && mouse_y >= 0.0 && mouse_x <= scene.width &&
      mouse_y <= scene.height && !ImGui::IsAnyMouseDown()) {
    // Feldwert unter dem Mauszeiger
    const int i = std::clamp(static_cast<int>(mouse_x / scene.cell_size), 0, sim_->nx() - 1);
    const int j = std::clamp(static_cast<int>(mouse_y / scene.cell_size), 0, sim_->ny() - 1);
    const std::size_t k =
        static_cast<std::size_t>(i) + static_cast<std::size_t>(sim_->nx()) * static_cast<std::size_t>(j);
    if (k < field_.size())
      ImGui::SetTooltip("x = %.2f µm, y = %.2f µm\nn = %.3f\n%s = %.3g %s", mouse_x / um, mouse_y / um,
                        field::index_at(scene, mouse_x, mouse_y),
                        scene.polarization == field::Polarization::ez ? "Ez" : "Hz",
                        static_cast<double>(field_[k]),
                        scene.polarization == field::Polarization::ez ? "V/m" : "A/m");
  }

  // Überlagerungen: Randschicht, Formen, Quellen, Monitore
  draw->PushClipRect(area_min, area_max, true);
  const double pml = scene.pml_cells * scene.cell_size;
  if (pml > 0.0 && (scene.boundary_x == field::Boundary::pml || scene.boundary_y == field::Boundary::pml)) {
    const double x0 = scene.boundary_x == field::Boundary::pml ? pml : 0.0;
    const double y0 = scene.boundary_y == field::Boundary::pml ? pml : 0.0;
    draw->AddRect(to_screen(x0, y0), to_screen(scene.width - x0, scene.height - y0), color_pml, 0.0f, 0,
                  1.0f);
    draw->AddText({to_screen(x0, y0).x + 4.0f, to_screen(x0, y0).y + 2.0f}, color_pml,
                  "Randschicht außerhalb");
  }
  for (int i = 0; i < static_cast<int>(scene.shapes.size()); ++i) {
    const auto& s = scene.shapes[static_cast<std::size_t>(i)];
    const bool selected = is_selected(ObjectKind::shape, i);
    const ImU32 color = selected ? color_selected : s.enabled ? color_outline : color_disabled;
    const float thickness = selected ? 2.0f : 1.0f;
    if (!show_geometry_ && !selected) continue;
    if (s.kind == field::ShapeKind::rectangle) {
      const double c = std::cos(s.rotation), sn = std::sin(s.rotation);
      ImVec2 corners[4];
      const double hx[4] = {-0.5, 0.5, 0.5, -0.5}, hy[4] = {-0.5, -0.5, 0.5, 0.5};
      for (int k = 0; k < 4; ++k) {
        const double u = hx[k] * s.width, v = hy[k] * s.height;
        corners[k] = to_screen(s.x + c * u - sn * v, s.y + sn * u + c * v);
      }
      draw->AddPolyline(corners, 4, color, ImDrawFlags_Closed, thickness);
    } else {
      draw->AddCircle(to_screen(s.x, s.y), static_cast<float>(s.radius) * px_per_m, color, 96, thickness);
      if (s.kind == field::ShapeKind::ring)
        draw->AddCircle(to_screen(s.x, s.y), static_cast<float>(s.inner_radius) * px_per_m, color, 96,
                        thickness);
    }
  }
  for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i) {
    const auto& s = scene.sources[static_cast<std::size_t>(i)];
    const bool selected = is_selected(ObjectKind::source, i);
    const ImU32 color_source = s.enabled ? field_lab_color_source : color_disabled;
    const ImVec2 a = to_screen(s.x1, s.y1);
    if (s.kind != field::SourceKind::point)
      draw->AddLine(a, to_screen(s.x2, s.y2), color_source, selected ? 3.5f : 2.0f);
    draw->AddCircleFilled(a, (selected ? 5.5f : 4.0f) * ui_scale_, color_source);
    draw->AddText({a.x + 7.0f, a.y - ImGui::GetFontSize() - 2.0f}, color_source, s.name.c_str());
  }
  for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i) {
    const auto& m = scene.monitors[static_cast<std::size_t>(i)];
    const bool selected = is_selected(ObjectKind::monitor, i);
    const ImU32 color_monitor = m.enabled ? field_lab_color_monitor : color_disabled;
    const ImVec2 a = to_screen(m.x1, m.y1);
    const float r = (selected ? 6.5f : 5.0f) * ui_scale_;
    if (m.kind != field::MonitorKind::point) {
      draw->AddLine(a, to_screen(m.x2, m.y2), color_monitor, selected ? 3.5f : 2.0f);
    } else {
      draw->AddLine({a.x - r, a.y}, {a.x + r, a.y}, color_monitor, 2.0f);
      draw->AddLine({a.x, a.y - r}, {a.x, a.y + r}, color_monitor, 2.0f);
    }
    draw->AddText({a.x + 7.0f, a.y + 3.0f}, color_monitor, m.name.c_str());
  }
  for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i) {
    const auto& p = scene.ports[static_cast<std::size_t>(i)];
    const bool selected = is_selected(ObjectKind::port, i);
    const ImVec2 a = to_screen(p.x1, p.y1), b = to_screen(p.x2, p.y2);
    draw->AddLine(a, b, color_port, selected ? 3.5f : 2.0f);
    // Pfeil in der Mitte, Richtung ins Bauteil
    const bool vertical = std::abs(p.x2 - p.x1) < 0.5 * scene.cell_size;
    const int inward = field::port_inward_sign(scene, p);
    const ImVec2 mid = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
    const float length = 14.0f * ui_scale_, head = 5.0f * ui_scale_;
    const ImVec2 dir =
        vertical ? ImVec2(static_cast<float>(inward), 0.0f) : ImVec2(0.0f, static_cast<float>(inward));
    const ImVec2 tip = {mid.x + dir.x * length, mid.y + dir.y * length};
    draw->AddLine(mid, tip, color_port, 2.0f);
    draw->AddTriangleFilled(tip, {tip.x - dir.x * head - dir.y * head, tip.y - dir.y * head - dir.x * head},
                            {tip.x - dir.x * head + dir.y * head, tip.y - dir.y * head + dir.x * head},
                            color_port);
    draw->AddText({b.x + 5.0f, b.y - ImGui::GetFontSize()}, color_port, p.name.c_str());
  }
  // Maßstab unten links
  {
    const double nice[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50};
    double bar_um = nice[0];
    for (double n : nice)
      if (n * um * px_per_m < area.x * 0.25f) bar_um = n;
    const ImVec2 start = {area_min.x + 12.0f, area_max.y - 14.0f};
    const float length = static_cast<float>(bar_um * um) * px_per_m;
    draw->AddLine(start, {start.x + length, start.y}, IM_COL32(255, 255, 255, 220), 2.0f);
    char label[32];
    std::snprintf(label, sizeof label, "%g µm", bar_um);
    draw->AddText({start.x, start.y - ImGui::GetFontSize() - 2.0f}, IM_COL32(255, 255, 255, 220), label);
  }
  draw->PopClipRect();

  // Ansicht: Zoomstufe und Einpassen (oben rechts)
  char zoom_text[48];
  std::snprintf(zoom_text, sizeof zoom_text, "%.0f %%", 100.0 * zoom_);
  const float button_width = ImGui::CalcTextSize("Einpassen (F)").x + 2.0f * ImGui::GetStyle().FramePadding.x;
  ImGui::SetCursorScreenPos(
      {area_max.x - button_width - ImGui::CalcTextSize(zoom_text).x - 20.0f, area_min.y + 6.0f});
  ImGui::TextUnformatted(zoom_text);
  ImGui::SameLine();
  if (ImGui::SmallButton("Einpassen (F)")) zoom_ = 1.0f, pan_ = {};
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Mausrad: zoomen\nmittlere Maustaste: verschieben\nRechtsklick: Menü");
  ImGui::EndChild();
}

void FieldLab::draw_plots() {
  const FieldScene& scene = designer_.project().field;
  if (!ImGui::BeginTabBar("plots")) return;
  // Lange Zeitreihen ausgedünnt zeichnen (höchstens etwa 4000 Punkte je Kurve)
  auto plot_series = [&](bool flux, const char* y_label) {
    if (ImPlot::BeginPlot("##zeit", ImVec2(-1.0f, -1.0f))) {
      ImPlot::SetupAxes("Zeit [fs]", y_label, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      ImPlot::SetupLegend(ImPlotLocation_NorthWest);
      for (const auto& m : monitors_) {
        if ((m.kind == field::MonitorKind::flux) != flux || m.values.empty()) continue;
        const int stride = std::max(1, static_cast<int>(m.values.size() / 4000));
        ImPlot::PlotLine(m.name.c_str(), m.values.data(), static_cast<int>(m.values.size()) / stride,
                         m.sample_interval * stride / units::fs, m.start_time / units::fs, 0, 0,
                         stride * static_cast<int>(sizeof(double)));
      }
      ImPlot::EndPlot();
    }
  };
  const bool has_points = std::any_of(monitors_.begin(), monitors_.end(),
                                      [](const auto& m) { return m.kind != field::MonitorKind::flux; });
  const bool has_flux = std::any_of(monitors_.begin(), monitors_.end(),
                                    [](const auto& m) { return m.kind == field::MonitorKind::flux; });
  if (has_points && ImGui::BeginTabItem("Feld und Moden")) {
    plot_series(false, scene.polarization == field::Polarization::ez ? "Ez [V/m]" : "Hz [A/m]");
    ImGui::EndTabItem();
  }
  if (has_flux && ImGui::BeginTabItem("Leistungsfluss")) {
    // Mittelwert über die letzten Perioden der ersten Quelle
    if (!scene.sources.empty()) {
      const double period = scene.sources.front().wavelength / constants::c0;
      std::string line = "Mittel der letzten 4 Perioden:";
      for (const auto& m : monitors_)
        if (m.kind == field::MonitorKind::flux) {
          char text[96];
          std::snprintf(text, sizeof text, "  %s %.3e W/m", m.name.c_str(),
                        field::average_over_periods(m.values, m.sample_interval, period, 4));
          line += text;
        }
      ImGui::TextDisabled("%s", line.c_str());
    }
    plot_series(true, "Fluss [W/m]");
    ImGui::EndTabItem();
  }
  if (has_points && ImGui::BeginTabItem("Spektrum")) {
    update_spectrum();
    if (ImPlot::BeginPlot("##spektrum", ImVec2(-1.0f, -1.0f))) {
      ImPlot::SetupAxes("Wellenlänge [nm]", "Amplitude", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      ImPlot::SetupLegend(ImPlotLocation_NorthWest);
      for (std::size_t k = 0; k < spectrum_.size(); ++k)
        ImPlot::PlotLine(spectrum_names_[k].c_str(), spectrum_wavelength_nm_.data(), spectrum_[k].data(),
                         static_cast<int>(spectrum_[k].size()));
      ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

// ---------------------------------------------------------------------------------------------
// Auswahl, Zwischenablage, Rechtsklick-Menü, Tastenkürzel
// ---------------------------------------------------------------------------------------------

bool FieldLab::is_selected(ObjectKind kind, int index) const {
  return std::find(selection_.begin(), selection_.end(), ObjectRef{kind, index}) != selection_.end();
}

void FieldLab::select(ObjectRef ref, bool additive) {
  if (!additive) {
    selection_ = {ref};
  } else if (is_selected(ref.kind, ref.index)) {
    std::erase(selection_, ref);
  } else {
    selection_.push_back(ref);
  }
  const ObjectRef primary = selection_.empty() ? ObjectRef{} : selection_.back();
  selected_kind_ = primary.kind, selected_index_ = primary.index;
}

void FieldLab::clear_selection() {
  selection_.clear();
  selected_kind_ = ObjectKind::none, selected_index_ = -1;
}

void FieldLab::select_all() {
  const FieldScene& scene = designer_.project().field;
  clear_selection();
  for (int i = 0; i < static_cast<int>(scene.shapes.size()); ++i) select({ObjectKind::shape, i}, true);
  for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i) select({ObjectKind::source, i}, true);
  for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i) select({ObjectKind::monitor, i}, true);
  for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i) select({ObjectKind::port, i}, true);
}

void FieldLab::delete_selection() {
  FieldScene& scene = designer_.project().field;
  if (selection_.empty()) return;
  // von hinten löschen, damit die übrigen Indizes gültig bleiben
  std::vector<ObjectRef> refs = selection_;
  std::sort(refs.begin(), refs.end(),
            [](const ObjectRef& a, const ObjectRef& b) { return a.index > b.index; });
  auto erase = [](auto& list, int index) {
    if (index >= 0 && static_cast<std::size_t>(index) < list.size()) list.erase(list.begin() + index);
  };
  for (const ObjectRef& r : refs) {
    if (r.kind == ObjectKind::shape) erase(scene.shapes, r.index);
    if (r.kind == ObjectKind::source) erase(scene.sources, r.index);
    if (r.kind == ObjectKind::monitor) erase(scene.monitors, r.index);
    if (r.kind == ObjectKind::port) erase(scene.ports, r.index);
  }
  clear_selection();
  scene_changed_ = true;
  designer_.mark_dirty();
}

void FieldLab::copy_selection() {
  const FieldScene& scene = designer_.project().field;
  clipboard_ = {};
  for (const ObjectRef& r : selection_) {
    const auto i = static_cast<std::size_t>(r.index);
    if (r.kind == ObjectKind::shape && i < scene.shapes.size()) clipboard_.shapes.push_back(scene.shapes[i]);
    if (r.kind == ObjectKind::source && i < scene.sources.size())
      clipboard_.sources.push_back(scene.sources[i]);
    if (r.kind == ObjectKind::monitor && i < scene.monitors.size())
      clipboard_.monitors.push_back(scene.monitors[i]);
    if (r.kind == ObjectKind::port && i < scene.ports.size()) clipboard_.ports.push_back(scene.ports[i]);
  }
}

void FieldLab::paste(bool from_selection) {
  if (from_selection) copy_selection();
  FieldScene& scene = designer_.project().field;
  if (clipboard_.shapes.empty() && clipboard_.sources.empty() && clipboard_.monitors.empty() &&
      clipboard_.ports.empty())
    return;
  // um eine halbe Mikrometer versetzt, damit die Kopie sichtbar neben dem Original liegt
  const double offset = 0.5 * um;
  clear_selection();
  for (auto s : clipboard_.shapes) {
    s.name += " Kopie";
    s.x += offset, s.y += offset;
    scene.shapes.push_back(s);
    select({ObjectKind::shape, static_cast<int>(scene.shapes.size()) - 1}, true);
  }
  for (auto s : clipboard_.sources) {
    s.name += " Kopie";
    s.x1 += offset, s.x2 += offset, s.y1 += offset, s.y2 += offset;
    scene.sources.push_back(s);
    select({ObjectKind::source, static_cast<int>(scene.sources.size()) - 1}, true);
  }
  for (auto m : clipboard_.monitors) {
    m.name += " Kopie";
    m.x1 += offset, m.x2 += offset, m.y1 += offset, m.y2 += offset;
    scene.monitors.push_back(m);
    select({ObjectKind::monitor, static_cast<int>(scene.monitors.size()) - 1}, true);
  }
  for (auto p : clipboard_.ports) {
    p.name += "_kopie";
    p.x1 += offset, p.x2 += offset, p.y1 += offset, p.y2 += offset;
    scene.ports.push_back(p);
    select({ObjectKind::port, static_cast<int>(scene.ports.size()) - 1}, true);
  }
  move_selection(0.0, 0.0);  // in das Rechengebiet zurückholen
  scene_changed_ = true;
  designer_.mark_dirty();
}

void FieldLab::set_selection_enabled(bool enabled) {
  FieldScene& scene = designer_.project().field;
  for (const ObjectRef& r : selection_) {
    const auto i = static_cast<std::size_t>(r.index);
    if (r.kind == ObjectKind::shape && i < scene.shapes.size()) scene.shapes[i].enabled = enabled;
    if (r.kind == ObjectKind::source && i < scene.sources.size()) scene.sources[i].enabled = enabled;
    if (r.kind == ObjectKind::monitor && i < scene.monitors.size()) scene.monitors[i].enabled = enabled;
  }
  scene_changed_ = true;
  designer_.mark_dirty();
}

bool FieldLab::selection_enabled() const {
  const FieldScene& scene = designer_.project().field;
  for (const ObjectRef& r : selection_) {
    const auto i = static_cast<std::size_t>(r.index);
    if (r.kind == ObjectKind::shape && i < scene.shapes.size() && scene.shapes[i].enabled) return true;
    if (r.kind == ObjectKind::source && i < scene.sources.size() && scene.sources[i].enabled) return true;
    if (r.kind == ObjectKind::monitor && i < scene.monitors.size() && scene.monitors[i].enabled) return true;
  }
  return false;
}

void FieldLab::move_selection(double dx, double dy) {
  FieldScene& scene = designer_.project().field;
  auto move = [&](double* x, double* y) {
    *x = std::clamp(*x + dx, 0.0, scene.width);
    *y = std::clamp(*y + dy, 0.0, scene.height);
  };
  bool physics = false;  // Ports ändern die Feldrechnung nicht
  for (const ObjectRef& r : selection_) {
    const auto i = static_cast<std::size_t>(r.index);
    if (r.kind == ObjectKind::shape && i < scene.shapes.size()) {
      move(&scene.shapes[i].x, &scene.shapes[i].y);
      physics = true;
    } else if (r.kind == ObjectKind::source && i < scene.sources.size()) {
      move(&scene.sources[i].x1, &scene.sources[i].y1);
      move(&scene.sources[i].x2, &scene.sources[i].y2);
      physics = true;
    } else if (r.kind == ObjectKind::monitor && i < scene.monitors.size()) {
      move(&scene.monitors[i].x1, &scene.monitors[i].y1);
      move(&scene.monitors[i].x2, &scene.monitors[i].y2);
      physics = true;
    } else if (r.kind == ObjectKind::port && i < scene.ports.size()) {
      move(&scene.ports[i].x1, &scene.ports[i].y1);
      move(&scene.ports[i].x2, &scene.ports[i].y2);
    }
  }
  if (physics) scene_changed_ = true;
  designer_.mark_dirty();
}

FieldLab::ObjectRef FieldLab::pick(ImVec2 mouse) const {
  // Quellen vor Ports vor Monitoren vor Formen, oberste Form zuerst
  const FieldScene& scene = designer_.project().field;
  const float grab = 7.0f * ui_scale_;
  for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i) {
    const auto& s = scene.sources[static_cast<std::size_t>(i)];
    const ImVec2 a = to_screen(s.x1, s.y1),
                 b = s.kind != field::SourceKind::point ? to_screen(s.x2, s.y2) : a;
    if (distance_to_segment(mouse, a, b) <= grab) return {ObjectKind::source, i};
  }
  for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i) {
    const auto& p = scene.ports[static_cast<std::size_t>(i)];
    if (distance_to_segment(mouse, to_screen(p.x1, p.y1), to_screen(p.x2, p.y2)) <= grab)
      return {ObjectKind::port, i};
  }
  for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i) {
    const auto& m = scene.monitors[static_cast<std::size_t>(i)];
    const ImVec2 a = to_screen(m.x1, m.y1),
                 b = m.kind != field::MonitorKind::point ? to_screen(m.x2, m.y2) : a;
    if (distance_to_segment(mouse, a, b) <= grab) return {ObjectKind::monitor, i};
  }
  const double x = (mouse.x - canvas_origin_.x) / px_per_m_, y = (mouse.y - canvas_origin_.y) / px_per_m_;
  for (int i = static_cast<int>(scene.shapes.size()) - 1; i >= 0; --i)
    if (field::contains(scene.shapes[static_cast<std::size_t>(i)], x, y)) return {ObjectKind::shape, i};
  return {};
}

void FieldLab::draw_context_items() {
  const bool any = !selection_.empty();
  if (any) {
    ImGui::TextDisabled(selection_.size() == 1 ? "1 Objekt" : "%zu Objekte", selection_.size());
    const bool enabled = selection_enabled();
    if (ImGui::MenuItem(enabled ? "Deaktivieren" : "Aktivieren", "H")) set_selection_enabled(!enabled);
    if (ImGui::MenuItem("Duplizieren", "Strg+D")) paste(true);
    if (ImGui::MenuItem("Kopieren", "Strg+C")) copy_selection();
    if (ImGui::MenuItem("Ausschneiden", "Strg+X")) copy_selection(), delete_selection();
  }
  const bool can_paste = !(clipboard_.shapes.empty() && clipboard_.sources.empty() &&
                           clipboard_.monitors.empty() && clipboard_.ports.empty());
  if (ImGui::MenuItem("Einfügen", "Strg+V", false, can_paste)) paste(false);
  if (ImGui::MenuItem("Alles auswählen", "Strg+A")) select_all();
  if (any && selected_kind_ == ObjectKind::shape) {
    // Formen überdecken einander in Listenreihenfolge
    FieldScene& scene = designer_.project().field;
    const int i = selected_index_;
    const int last = static_cast<int>(scene.shapes.size()) - 1;
    if (ImGui::MenuItem("Ganz nach vorn", nullptr, false, selection_.size() == 1 && i < last)) {
      std::rotate(scene.shapes.begin() + i, scene.shapes.begin() + i + 1, scene.shapes.end());
      select({ObjectKind::shape, last}, false);
      scene_changed_ = true;
      designer_.mark_dirty();
    }
    if (ImGui::MenuItem("Ganz nach hinten", nullptr, false, selection_.size() == 1 && i > 0)) {
      std::rotate(scene.shapes.begin(), scene.shapes.begin() + i, scene.shapes.begin() + i + 1);
      select({ObjectKind::shape, 0}, false);
      scene_changed_ = true;
      designer_.mark_dirty();
    }
  }
  if (any) {
    ImGui::Separator();
    if (ImGui::MenuItem("Löschen", "Entf")) delete_selection();
  }
}

void FieldLab::handle_shortcuts() {
  // nur, wenn das Bauteil-Labor den Fokus hat und kein Textfeld bearbeitet wird
  if (ImGui::GetIO().WantTextInput || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) select_all();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) copy_selection();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) copy_selection(), delete_selection();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) paste(false);
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) paste(true);
  if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) delete_selection();
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) clear_selection();
  if (ImGui::IsKeyChordPressed(ImGuiKey_H) && !selection_.empty())
    set_selection_enabled(!selection_enabled());
  if (ImGui::IsKeyChordPressed(ImGuiKey_F)) zoom_ = 1.0f, pan_ = {};
}

}  // namespace photonlab::gui
