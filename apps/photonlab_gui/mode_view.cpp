#include "mode_view.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>

#include "photonlab/core/units.hpp"
#include "photonlab/materials/material.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

bool drag_nm(const char* label, double* value, double low, double high, float speed = 1.0f) {
  double shown = *value / units::nm;
  const double lo = low / units::nm, hi = high / units::nm;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragScalar(label, ImGuiDataType_Double, &shown, speed, &lo, &hi, "%.0f nm",
                        ImGuiSliderFlags_AlwaysClamp)) {
    *value = std::clamp(shown, lo, hi) * units::nm;
    return true;
  }
  return false;
}

bool material_combo(const char* label, std::string* name) {
  bool changed = false;
  const materials::Material* current = materials::find(*name);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
  if (ImGui::BeginCombo(label, current ? current->label.c_str() : name->c_str())) {
    for (const auto& material : materials::library()) {
      if (ImGui::Selectable(material.label.c_str(), material.name == *name) && material.name != *name) {
        *name = material.name;
        changed = true;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Quelle: %s", material.source.c_str());
    }
    ImGui::EndCombo();
  }
  return changed;
}

}  // namespace

ModeView::ModeView(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer, float ui_scale)
    : pool_(pool), designer_(designer), renderer_(renderer), ui_scale_(ui_scale) {}

ModeView::~ModeView() {
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
  if (texture_) SDL_DestroyTexture(texture_);
}

void ModeView::start() {
  if (future_.valid()) return;
  control_ = std::make_shared<JobControl>();
  // Der Job besitzt eine Kopie des Querschnitts und greift nicht auf die Oberfläche zu.
  future_ = pool_.submit([section = designer_.project().cross_section, control = control_] {
    Result result;
    result.section = section;
    try {
      result.result = modes::analyze(section, true, control.get());
    } catch (const std::exception& e) {
      result.error = e.what();
    }
    control->finished = true;
    return result;
  });
}

void ModeView::update() {
  if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  Result result = future_.get();
  const bool cancelled = control_ && control_->cancel_requested;
  control_.reset();
  if (cancelled) return;
  result_ = std::move(result);
  has_result_ = true;
  selected_mode_ = 0;
  texture_dirty_ = true;
}

void ModeView::update_texture() {
  if (!texture_dirty_ || !has_result_ || result_.result.modes.empty()) return;
  texture_dirty_ = false;
  const modes::FdeGrid& grid = result_.result.grid;
  if (grid.nx != texture_nx_ || grid.ny != texture_ny_ || !texture_) {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_nx_ = grid.nx, texture_ny_ = grid.ny;
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, texture_nx_,
                                 texture_ny_);
    if (texture_) SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    pixels_.assign(static_cast<std::size_t>(texture_nx_) * static_cast<std::size_t>(texture_ny_) * 4, 255);
  }
  if (!texture_) return;
  const auto index = static_cast<std::size_t>(
      std::clamp(selected_mode_, 0, static_cast<int>(result_.result.modes.size()) - 1));
  const std::vector<double>& field = result_.result.modes[index].field;
  for (std::size_t k = 0; k < field.size() && 4 * k + 3 < pixels_.size(); ++k) {
    const float v = static_cast<float>(field[k]);
    float r, g, b;
    if (show_intensity_) {
      // Intensität: dunkel nach hell über Violett und Orange
      const float t = std::clamp(v * v, 0.0f, 1.0f);
      r = std::min(1.0f, 1.6f * t), g = t * t,
      b = 0.35f * std::sin(3.1416f * std::min(t * 1.4f, 1.0f)) + 0.6f * t * t * t;
    } else {
      // Feld mit Vorzeichen: Rot positiv, Blau negativ, Dunkel null
      const float a = std::min(std::abs(v), 1.0f);
      r = v >= 0.0f ? 0.06f + 0.94f * a : 0.06f + 0.3f * a * a;
      g = 0.07f + 0.45f * a * a;
      b = v >= 0.0f ? 0.10f + 0.25f * a * a : 0.10f + 0.90f * a;
    }
    // Kern leicht aufhellen, damit die Geometrie sichtbar bleibt
    const float tint = 0.16f * static_cast<float>(result_.result.core_fraction[k]);
    r += tint * (0.8f - r), g += tint * (0.8f - g), b += tint * (0.85f - b);
    pixels_[4 * k + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
    pixels_[4 * k + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
    pixels_[4 * k + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
  }
  SDL_UpdateTexture(texture_, nullptr, pixels_.data(), texture_nx_ * 4);
}

// Effektiven Index und Gruppenindex der gewählten Mode in alle Wellenleiter-Modelle der Schaltung schreiben
void ModeView::apply_to_circuit() {
  if (!has_result_ || result_.result.modes.empty()) return;
  const auto& mode = result_.result.modes[static_cast<std::size_t>(selected_mode_)];
  int count = 0;
  for (auto& instance : designer_.project().circuit.instances) {
    const circuit::ComponentType* type = circuit::ComponentRegistry::builtin().find(instance.type);
    if (!type || !type->find_param("n_eff") || !type->find_param("n_group")) continue;
    instance.params["n_eff"] = mode.n_eff;
    instance.params["n_group"] = mode.n_group;
    if (type->find_param("wavelength_ref")) instance.params["wavelength_ref"] = result_.section.wavelength;
    ++count;
  }
  if (count > 0) designer_.touch_circuit();
  char text[160];
  if (count > 0)
    std::snprintf(text, sizeof text, "n_eff = %.4f und n_g = %.4f in %d Bauteile der Schaltung übernommen.",
                  mode.n_eff, mode.n_group, count);
  else
    std::snprintf(text, sizeof text, "Die Schaltung enthält keine Wellenleiter oder Ringe.");
  designer_.show_status(text, count == 0);
}

void ModeView::draw() {
  update();
  modes::CrossSection& section = designer_.project().cross_section;
  if (!auto_started_) {
    auto_started_ = true;
    start();
  }

  ImGui::BeginChild("einstellungen", ImVec2(ImGui::GetFontSize() * 20.0f, 0.0f), ImGuiChildFlags_Borders);
  bool changed = false;
  ImGui::SeparatorText("Querschnitt");
  changed |= material_combo("Kern", &section.core);
  changed |= material_combo("darunter", &section.lower);
  changed |= material_combo("darüber", &section.upper);
  changed |= drag_nm("Breite", &section.width, 50e-9, 5e-6);
  changed |= drag_nm("Höhe", &section.height, 50e-9, 2e-6);
  changed |= drag_nm("Rippenschicht", &section.slab_height, 0.0, std::max(0.0, section.height - 10e-9));
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Höhe einer durchgehenden Kernschicht (Rippenwellenleiter); 0 = Streifen.");
  ImGui::SeparatorText("Rechnung");
  changed |= drag_nm("Wellenlänge", &section.wavelength, 400e-9, 5e-6);
  int polarization = section.polarization == modes::FdePolarization::quasi_te ? 0 : 1;
  const char* polarizations[] = {"quasi-TE (E waagerecht)", "quasi-TM (E senkrecht)"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  if (ImGui::Combo("##polarisation", &polarization, polarizations, 2)) {
    section.polarization =
        polarization == 0 ? modes::FdePolarization::quasi_te : modes::FdePolarization::quasi_tm;
    changed = true;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragInt("Moden", &section.modes, 0.05f, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp))
    changed = true;
  changed |= drag_nm("Zellgröße", &section.cell_size, 5e-9, 100e-9, 0.2f);
  changed |= drag_nm("Mantel seitlich", &section.margin_x, 200e-9, 5e-6, 10.0f);
  changed |= drag_nm("Mantel oben/unten", &section.margin_y, 200e-9, 5e-6, 10.0f);
  if (changed) designer_.mark_dirty();

  const bool stale = !has_result_ || !(result_.section == section);
  if (future_.valid()) {
    if (ImGui::Button("Abbrechen")) control_->cancel_requested = true;
    ImGui::SameLine();
    ImGui::ProgressBar(control_->progress.load(), ImVec2(-1.0f, 0.0f));
  } else {
    if (ImGui::Button("Moden berechnen")) start();
    if (stale && has_result_) {
      ImGui::SameLine();
      ImGui::TextColored(ui_color_bad, "veraltet");
    }
  }
  // Brechzahlen der gewählten Materialien bei dieser Wellenlänge
  ImGui::Spacing();
  for (const std::string* name : {&section.core, &section.lower, &section.upper}) {
    const materials::Material* material = materials::find(*name);
    if (!material) continue;
    ImGui::TextDisabled("%s: n = %.4f%s", material->name.c_str(), material->index(section.wavelength),
                        material->in_valid_range(section.wavelength) ? "" : "  (außerhalb der Gültigkeit!)");
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Semi-vektorieller Löser: gerechnet wird nur die dominante Feldkomponente. Bei stark führenden "
      "Wellenleitern liegt n_eff dadurch einige Prozent über vollvektoriellen Werten "
      "(Si-Streifen 500 x 220 nm: 2,49 statt etwa 2,44). Brechzahlen aus Sellmeier-Gleichungen, "
      "ohne Absorption.");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("ergebnis", ImVec2(0.0f, 0.0f));
  if (has_result_ && !result_.error.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_bad, "%s", result_.error.c_str());
    ImGui::PopTextWrapPos();
  } else if (!has_result_) {
    ImGui::TextDisabled("Wird gerechnet ...");
  } else if (result_.result.modes.empty()) {
    ImGui::TextColored(ui_color_limited,
                       "Keine konvergierte Mode: der Querschnitt führt bei dieser Wellenlänge nichts.");
  } else {
    const auto& found = result_.result.modes;
    selected_mode_ = std::clamp(selected_mode_, 0, static_cast<int>(found.size()) - 1);
    if (ImGui::BeginTable("moden", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Mode");
      ImGui::TableSetupColumn("n_eff");
      ImGui::TableSetupColumn("n_g");
      ImGui::TableSetupColumn("geführt");
      ImGui::TableSetupColumn("Einschluss im Kern");
      ImGui::TableSetupColumn("Modenfläche");
      ImGui::TableHeadersRow();
      for (int k = 0; k < static_cast<int>(found.size()); ++k) {
        const auto& mode = found[static_cast<std::size_t>(k)];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char label[32];
        std::snprintf(label, sizeof label, "%d", k);
        if (ImGui::Selectable(label, k == selected_mode_, ImGuiSelectableFlags_SpanAllColumns)) {
          selected_mode_ = k;
          texture_dirty_ = true;
        }
        ImGui::TableNextColumn();
        ImGui::Text("%.4f", mode.n_eff);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", mode.n_group);
        ImGui::TableNextColumn();
        ImGui::TextColored(mode.guided ? ui_color_good : ui_color_limited, "%s", mode.guided ? "ja" : "nein");
        ImGui::TableNextColumn();
        ImGui::Text("%.1f %%", 100.0 * mode.confinement);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f µm²", mode.effective_area / 1e-12);
      }
      ImGui::EndTable();
    }
    if (ImGui::Checkbox("Intensität statt Feld", &show_intensity_)) texture_dirty_ = true;
    ImGui::SameLine();
    if (ImGui::Button("n_eff und n_g in den Chip-Designer übernehmen")) apply_to_circuit();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip(
          "Schreibt die Werte der gewählten Mode in alle Wellenleiter und Ringe der Schaltung.");
    if (stale) ImGui::TextColored(ui_color_limited, "Das Bild gehört zu den vorigen Einstellungen.");

    update_texture();
    // Bild seitenrichtig einpassen, Kern als Umriss
    const modes::FdeGrid& grid = result_.result.grid;
    const ImVec2 area_min = ImGui::GetCursorScreenPos();
    const ImVec2 area = ImGui::GetContentRegionAvail();
    const double width = grid.nx * grid.dx, height = grid.ny * grid.dy;
    const float px_per_m = static_cast<float>(std::min(area.x / width, area.y / height));
    const ImVec2 size = {static_cast<float>(width) * px_per_m, static_cast<float>(height) * px_per_m};
    const ImVec2 origin = {area_min.x + (area.x - size.x) * 0.5f, area_min.y};
    ImGui::SetCursorScreenPos(origin);
    if (texture_) ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture_)), size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const modes::CrossSection& shown = result_.section;
    const double x0 = 0.5 * (width - shown.width), y0 = 0.5 * (height - shown.height);
    auto at = [&](double x, double y) {
      return ImVec2(origin.x + static_cast<float>(x) * px_per_m, origin.y + static_cast<float>(y) * px_per_m);
    };
    const ImU32 outline = IM_COL32(227, 232, 239, 150);
    draw->AddRect(at(x0, y0), at(x0 + shown.width, y0 + shown.height), outline);
    if (shown.slab_height > 0.0)
      draw->AddRect(at(0.0, y0 + shown.height - shown.slab_height), at(width, y0 + shown.height), outline);
    draw->AddLine(at(0.0, y0 + shown.height), at(width, y0 + shown.height), IM_COL32(152, 164, 182, 90));
    char label[64];
    std::snprintf(label, sizeof label, "%s", shown.upper.c_str());
    draw->AddText({origin.x + 6.0f, origin.y + 4.0f}, IM_COL32(152, 164, 182, 255), label);
    draw->AddText({origin.x + 6.0f, origin.y + size.y - ImGui::GetFontSize() - 4.0f},
                  IM_COL32(152, 164, 182, 255), shown.lower.c_str());
  }
  ImGui::EndChild();
}

}  // namespace photonlab::gui
