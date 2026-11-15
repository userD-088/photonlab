#include "inverse_view.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

#include "implot.h"
#include "photonlab/core/units.hpp"
#include "photonlab/layout/gds.hpp"
#include "photonlab/layout/layout.hpp"
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

}  // namespace

InverseView::InverseView(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer)
    : pool_(pool), designer_(designer), renderer_(renderer) {}

InverseView::~InverseView() {
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
  if (texture_) SDL_DestroyTexture(texture_);
}

void InverseView::start() {
  if (future_.valid()) return;
  control_ = std::make_shared<JobControl>();
  live_ = std::make_shared<Live>();
  field::InverseDesignSettings settings = settings_;
  settings.objective_wavelengths =
      three_wavelengths_
          ? std::vector<double>{settings.wavelength - 20e-9, settings.wavelength, settings.wavelength + 20e-9}
          : std::vector<double>{settings.wavelength};
  history_.clear();
  // Der Job besitzt Einstellungen, Steuerung und Live-Puffer; er greift nicht auf die Oberfläche zu.
  future_ = pool_.submit([settings, control = control_, live = live_] {
    Outcome outcome;
    try {
      outcome.result =
          field::run_inverse_design(settings, control.get(), [&](const field::InverseDesignProgress& p) {
            std::lock_guard lock(live->mutex);
            live->density = *p.density;
            live->history = *p.history;
            live->fresh = true;
          });
    } catch (const std::exception& e) {
      outcome.error = e.what();
    }
    control->finished = true;
    return outcome;
  });
}

void InverseView::update() {
  if (live_) {
    std::lock_guard lock(live_->mutex);
    if (live_->fresh) {
      live_->fresh = false;
      history_ = live_->history;
      const int n = static_cast<int>(std::lround(std::sqrt(static_cast<double>(live_->density.size()))));
      update_texture(live_->density, n, n);
    }
  }
  if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  Outcome outcome = future_.get();
  const bool cancelled = control_ && control_->cancel_requested;
  control_.reset();
  live_.reset();
  if (cancelled && outcome.error.empty()) {
    designer_.show_status("Inverses Design abgebrochen.");
    return;
  }
  outcome_ = std::move(outcome);
  has_result_ = true;
  if (outcome_.error.empty()) {
    history_ = outcome_.result.history;
    update_texture(outcome_.result.binary, outcome_.result.nx, outcome_.result.ny);
  }
}

void InverseView::update_texture(const std::vector<double>& density, int nx, int ny) {
  if (density.empty() || nx <= 0 || ny <= 0) return;
  if (!texture_ || texture_nx_ != nx || texture_ny_ != ny) {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_nx_ = nx, texture_ny_ = ny;
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, nx, ny);
    if (texture_) SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
  }
  if (!texture_) return;
  pixels_.assign(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * 4, 255);
  for (std::size_t p = 0; p < density.size() && 4 * p + 3 < pixels_.size(); ++p) {
    // Mantel dunkelblau, Kern hellgrau (wie die Formen im Feld-Reiter)
    const float t = static_cast<float>(std::clamp(density[p], 0.0, 1.0));
    pixels_[4 * p + 0] = static_cast<std::uint8_t>(255.0f * (0.08f + 0.78f * t));
    pixels_[4 * p + 1] = static_cast<std::uint8_t>(255.0f * (0.10f + 0.78f * t));
    pixels_[4 * p + 2] = static_cast<std::uint8_t>(255.0f * (0.18f + 0.72f * t));
  }
  SDL_UpdateTexture(texture_, nullptr, pixels_.data(), nx * 4);
}

bool InverseView::draw() {
  update();
  bool scene_replaced = false;
  ImGui::BeginChild("einstellungen_inv", ImVec2(ImGui::GetFontSize() * 20.0f, 0.0f), ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Aufgabe: 90-Grad-Knick");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Si-Wellenleiter in SiO2: Licht kommt von links und soll unten in der Grundmode "
      "herauslaufen. Das quadratische Gebiet dazwischen wird Pixel für Pixel optimiert.");
  ImGui::PopTextWrapPos();
  drag_nm("Wellenlänge", &settings_.wavelength, 1200e-9, 2000e-9);
  ImGui::Checkbox("drei Wellenlängen (+-20 nm)", &three_wavelengths_);
  drag_nm("Designgebiet", &settings_.design_size, 0.8e-6, 6e-6, 20.0f);
  drag_nm("Wellenleiter", &settings_.waveguide_width, 150e-9, 1e-6, 5.0f);
  drag_nm("Pixel (Zelle)", &settings_.cell_size, 20e-9, 80e-9, 0.5f);
  drag_nm("Filterradius", &settings_.filter_radius, 0.0, 300e-9, 2.0f);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Glättet das Design: Strukturen kleiner als etwa der doppelte Radius werden selten.");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  ImGui::DragInt("Iterationen", &settings_.iterations, 0.2f, 1, 300, "%d", ImGuiSliderFlags_AlwaysClamp);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  ImGui::DragScalar("Schrittweite", ImGuiDataType_Double, &settings_.step, 0.005f, nullptr, nullptr, "%.2f");
  settings_.step = std::clamp(settings_.step, 0.01, 0.5);
  if (future_.valid()) {
    if (ImGui::Button("Abbrechen")) control_->cancel_requested = true;
    ImGui::SameLine();
    ImGui::ProgressBar(control_->progress.load(), ImVec2(-1.0f, 0.0f));
  } else if (ImGui::Button("Optimieren")) {
    start();
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Je Iteration zwei 2D-FDTD-Läufe (vorwärts und adjungiert) liefern die Ableitung der Transmission nach "
      "der "
      "Permittivität jedes Pixels. Filter und eine schrittweise schärfere Projektion machen das Design binär "
      "(nur Si oder SiO2). Grenzen: 2D mit Ez (E senkrecht zur Ebene), keine Mindeststrukturgröße "
      "garantiert, "
      "nur ein lokales Optimum.");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("ergebnis_inv", ImVec2(0.0f, 0.0f));
  if (has_result_ && !outcome_.error.empty()) {
    ImGui::TextColored(ui_color_bad, "%s", outcome_.error.c_str());
  } else if (!future_.valid() && !has_result_) {
    ImGui::TextDisabled(
        "Noch keine Optimierung. Links einstellen und \"Optimieren\" drücken (40 Iterationen "
        "dauern etwa eine halbe Minute).");
  }
  const float side = std::min(ImGui::GetContentRegionAvail().x * 0.42f, ImGui::GetFontSize() * 16.0f);
  if (texture_) {
    ImGui::BeginGroup();
    ImGui::TextUnformatted(future_.valid() ? "Dichte (live)" : "Binäres Design");
    // Design mit angedeuteten Wellenleitern: Eingang links, Ausgang unten (wie in der Feld-Szene, y nach
    // unten)
    const float stub = 0.12f * side,
                guide = side * static_cast<float>(settings_.waveguide_width / settings_.design_size);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner = {origin.x + stub, origin.y};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 core = IM_COL32(220, 225, 235, 255);
    draw->AddRectFilled({origin.x, corner.y + 0.5f * (side - guide)},
                        {corner.x, corner.y + 0.5f * (side + guide)}, core);
    draw->AddRectFilled({corner.x + 0.5f * (side - guide), corner.y + side},
                        {corner.x + 0.5f * (side + guide), corner.y + side + stub}, core);
    ImGui::SetCursorScreenPos(corner);
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture_)), ImVec2(side, side));
    ImGui::Dummy(ImVec2(side + stub, stub));
    ImGui::EndGroup();
    ImGui::SameLine();
  }
  if (!history_.empty() &&
      ImPlot::BeginPlot("##verlauf_inv", ImVec2(-1.0f, 1.12f * side + ImGui::GetFontSize()))) {
    ImPlot::SetupAxes("Iteration", "Transmission", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.05, ImPlotCond_Always);
    std::vector<double> x(history_.size());
    for (std::size_t k = 0; k < x.size(); ++k) x[k] = static_cast<double>(k + 1);
    ImPlot::PlotLine("mittlere Transmission", x.data(), history_.data(), static_cast<int>(x.size()));
    ImPlot::EndPlot();
  }
  if (has_result_ && outcome_.error.empty() && !future_.valid()) {
    const auto& r = outcome_.result;
    ImGui::TextWrapped(
        "Transmission in die Grundmode: Start %.3f, stetig %.3f, binär %.3f (ohne Design %.3f)",
        r.transmission_start, r.transmission_final, r.transmission_binary, r.transmission_empty);
    if (r.transmission_final - r.transmission_binary > 0.05)
      ImGui::TextColored(ui_color_limited,
                         "Das binäre Design verliert gegenüber dem stetigen deutlich: mehr "
                         "Iterationen mit scharfer Projektion helfen.");
    if (ImGui::Button("Als GDSII exportieren...")) {
      designer_.request_gds_export([result = r](const std::string& path) {
        layout::write_gds_file(layout::inverse_design_layout(result), path);
      });
    }
    ImGui::SameLine();
    if (ImGui::Button("In die Feld-Szene übernehmen")) {
      designer_.project().field = field::design_scene(r);
      designer_.mark_dirty();
      designer_.show_status("Binäres Design als Feld-Szene übernommen (Reiter \"Feld (2D-FDTD)\").");
      scene_replaced = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Ersetzt die Feld-Szene des Projekts durch Wellenleiter + Design (Rechtecke).");
    if (ImPlot::BeginPlot("##spektrum_inv",
                          ImVec2(-1.0f, std::max(ImGui::GetContentRegionAvail().y, 140.0f)))) {
      ImPlot::SetupAxes("Wellenlänge [nm]", "Transmission (binär)", ImPlotAxisFlags_AutoFit,
                        ImPlotAxisFlags_None);
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.05, ImPlotCond_Always);
      std::vector<double> nm(r.spectrum_wavelengths.size());
      for (std::size_t k = 0; k < nm.size(); ++k) nm[k] = r.spectrum_wavelengths[k] / units::nm;
      ImPlot::PlotLine("Spektrum", nm.data(), r.spectrum_binary.data(), static_cast<int>(nm.size()));
      ImPlot::EndPlot();
    }
  }
  ImGui::EndChild();
  return scene_replaced;
}

}  // namespace photonlab::gui
