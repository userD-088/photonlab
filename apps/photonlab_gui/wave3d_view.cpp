#include "wave3d_view.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>

#include "implot.h"
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

void material_combo(const char* label, std::string* name) {
  const materials::Material* current = materials::find(*name);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
  if (ImGui::BeginCombo(label, current ? current->label.c_str() : name->c_str())) {
    for (const auto& material : materials::library())
      if (ImGui::Selectable(material.label.c_str(), material.name == *name)) *name = material.name;
    ImGui::EndCombo();
  }
}

}  // namespace

Wave3dView::Wave3dView(ThreadPool& pool, SDL_Renderer* renderer) : pool_(pool), renderer_(renderer) {}

Wave3dView::~Wave3dView() {
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
  for (Picture* p : {&top_, &cross_, &mode_})
    if (p->texture) SDL_DestroyTexture(p->texture);
}

void Wave3dView::start() {
  if (future_.valid()) return;
  control_ = std::make_shared<JobControl>();
  // Der Job besitzt eine Kopie der Einstellungen und greift nicht auf die Oberfläche zu.
  future_ = pool_.submit([settings = settings_, control = control_] {
    Result result;
    result.settings = settings;
    try {
      result.result = field::run_waveguide_3d(settings, control.get());
    } catch (const std::exception& e) {
      result.error = e.what();
    }
    control->finished = true;
    return result;
  });
}

void Wave3dView::update() {
  if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  Result result = future_.get();
  const bool cancelled = control_ && control_->cancel_requested;
  control_.reset();
  if (cancelled) return;
  result_ = std::move(result);
  has_result_ = true;
  const auto& r = result_.result;
  if (result_.error.empty() && r.completed) {
    fill(top_, r.top_view, r.nx, r.ny, true);
    fill(cross_, r.cross_view, r.ny, r.nz, false);
    fill(mode_, r.cross_mode, r.ny, r.nz, false);
  }
}

// Bild aus Werten: mit Vorzeichen Rot/Blau, sonst Betrag dunkel nach hell; auf das Maximum normiert
void Wave3dView::fill(Picture& picture, const std::vector<double>& values, int nx, int ny,
                      bool signed_field) {
  if (values.empty() || nx <= 0 || ny <= 0) return;
  if (!picture.texture || picture.nx != nx || picture.ny != ny) {
    if (picture.texture) SDL_DestroyTexture(picture.texture);
    picture.nx = nx, picture.ny = ny;
    picture.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, nx, ny);
    if (picture.texture) SDL_SetTextureScaleMode(picture.texture, SDL_SCALEMODE_LINEAR);
  }
  if (!picture.texture) return;
  double peak = 0.0;
  for (double v : values) peak = std::max(peak, std::abs(v));
  if (peak <= 0.0) peak = 1.0;
  picture.pixels.assign(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * 4, 255);
  // Zeile 0 des Bildes ist oben: y bzw. z nach oben zeigen lassen
  for (int row = 0; row < ny; ++row)
    for (int col = 0; col < nx; ++col) {
      const float v = static_cast<float>(values[static_cast<std::size_t>(col + nx * (ny - 1 - row))] / peak);
      float r, g, b;
      if (signed_field) {
        const float a = std::min(std::abs(v), 1.0f);
        r = v >= 0.0f ? 0.06f + 0.94f * a : 0.06f + 0.3f * a * a;
        g = 0.07f + 0.45f * a * a;
        b = v >= 0.0f ? 0.10f + 0.25f * a * a : 0.10f + 0.90f * a;
      } else {
        const float t = std::clamp(std::abs(v), 0.0f, 1.0f);
        r = std::min(1.0f, 1.6f * t), g = t * t,
        b = 0.35f * std::sin(3.1416f * std::min(t * 1.4f, 1.0f)) + 0.6f * t * t * t;
      }
      const std::size_t p =
          4 * (static_cast<std::size_t>(col) + static_cast<std::size_t>(nx) * static_cast<std::size_t>(row));
      picture.pixels[p + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
      picture.pixels[p + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
      picture.pixels[p + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
    }
  SDL_UpdateTexture(picture.texture, nullptr, picture.pixels.data(), nx * 4);
}

void Wave3dView::show(const Picture& picture, float width, float height) {
  if (!picture.texture) return;
  const float scale =
      std::min(width / static_cast<float>(picture.nx), height / static_cast<float>(picture.ny));
  ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(picture.texture)),
               ImVec2(scale * static_cast<float>(picture.nx), scale * static_cast<float>(picture.ny)));
}

void Wave3dView::draw() {
  update();
  ImGui::BeginChild("einstellungen3d", ImVec2(ImGui::GetFontSize() * 20.0f, 0.0f), ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Wellenleiter");
  material_combo("Kern", &settings_.core);
  material_combo("Mantel", &settings_.cladding);
  ImGui::Checkbox("Schicht (in y unbegrenzt)", &settings_.slab);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Vergleich mit der analytischen Lösung des Schichtwellenleiters.");
  if (!settings_.slab) drag_nm("Breite", &settings_.width, 100e-9, 3e-6, 10.0f);
  drag_nm("Höhe", &settings_.height, 60e-9, 2e-6, 10.0f);
  drag_nm("Wellenlänge", &settings_.wavelength, 400e-9, 5e-6);
  ImGui::SeparatorText("Gitter");
  drag_nm("Zellgröße", &settings_.cell_size, 5e-9, 100e-9, 0.5f);
  drag_nm("Länge", &settings_.length, 1e-6, 20e-6, 20.0f);
  drag_nm("Mantel bis Rand", &settings_.clearance, 100e-9, 3e-6, 10.0f);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  ImGui::DragInt("Threads (0 = alle)", &settings_.threads, 0.05f, 0, 64, "%d", ImGuiSliderFlags_AlwaysClamp);

  // Aufwand abschätzen: Zellen, Speicher (9 Felder/Koeffizienten je Zelle + Randschicht), Schritte
  const double dx = settings_.cell_size;
  const double margin = settings_.pml_cells + std::max(4.0, std::round(settings_.clearance / dx));
  const double ny =
      settings_.slab ? 2.0 * settings_.pml_cells + 8 : std::round(settings_.width / dx) + 2 * margin;
  const double nz = std::round(settings_.height / dx) + 2 * margin;
  const double nx = std::max(std::round(settings_.length / dx), 2.0 * settings_.pml_cells + 40);
  const double cells = nx * ny * nz;
  ImGui::TextDisabled("%.2f Mio. Zellen, etwa %.0f MB", cells / 1e6, cells * 9 * 8 / 1e6 * 1.3);
  const bool aligned =
      std::abs(settings_.height / dx - std::round(settings_.height / dx)) < 1e-6 &&
      (settings_.slab || std::abs(settings_.width / dx - std::round(settings_.width / dx)) < 1e-6);
  if (!aligned)
    ImGui::TextColored(ui_color_limited, "Breite und Höhe müssen ganze Vielfache der Zelle sein.");

  if (future_.valid()) {
    if (ImGui::Button("Abbrechen")) control_->cancel_requested = true;
    ImGui::SameLine();
    ImGui::ProgressBar(control_->progress.load(), ImVec2(-1.0f, 0.0f));
  } else if (ImGui::Button("3D-FDTD rechnen")) {
    start();
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Vollvektorielles 3D-FDTD (Yee, CPML, linear, ohne Dispersion): Dauerstrich mit dem Profil der "
      "Grundmode (quasi-TE), n_eff aus der Phase entlang der Achse. Die Rechnung dauert je nach Gitter "
      "Sekunden bis Minuten; Nichtlinearität und Quellen beliebiger Form gibt es nur im 2D-FDTD.");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("ergebnis3d", ImVec2(0.0f, 0.0f));
  if (!has_result_) {
    ImGui::TextDisabled(future_.valid() ? "Wird gerechnet ..."
                                        : "Noch keine Rechnung. Links einstellen und starten.");
    ImGui::EndChild();
    return;
  }
  if (!result_.error.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_bad, "%s", result_.error.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    return;
  }
  const auto& r = result_.result;
  const auto& s = result_.settings;
  if (ImGui::BeginTable("kennzahlen3d", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
    auto row = [](const char* name, const char* format, double value) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(name);
      ImGui::TableNextColumn();
      ImGui::Text(format, value);
    };
    row("n_eff 3D-FDTD", "%.5f", r.n_eff_fdtd);
    row("  ohne Yee-Dispersion längs", "%.5f", r.n_eff_corrected);
    if (s.slab) row("n_eff Schicht analytisch", "%.5f", r.n_eff_analytic);
    row("n_eff Modenlöser (semi-vektoriell)", "%.5f", r.n_eff_mode);
    row(s.slab ? "Abweichung zur analytischen Lösung" : "Abweichung zum Modenlöser", "%.2f %%",
        100.0 * r.relative_error);
    row("Modenüberlapp am Messquerschnitt", "%.4f", r.mode_overlap);
    row("Welligkeit der Amplitude (Reflexionen)", "%.1f %%", 100.0 * r.amplitude_ripple);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Gitter, Schritte");
    ImGui::TableNextColumn();
    ImGui::Text("%d x %d x %d, %zu", r.nx, r.ny, r.nz, r.steps);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Rechenzeit");
    ImGui::TableNextColumn();
    ImGui::Text("%.1f s, %.0f Mio. Zellen/s mit %d Threads", r.seconds, r.cells_per_second / 1e6, r.threads);
    ImGui::EndTable();
  }
  if (!s.slab && s.core == "Si") {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(
        ui_color_neutral,
        "Si-Draht: Der semi-vektorielle Modenlöser liegt etwa 2 %% zu hoch; das 3D-FDTD trifft den "
        "vollvektoriellen Wert (2,44 bei 500 x 220 nm).");
    ImGui::PopTextWrapPos();
  }

  const float avail = ImGui::GetContentRegionAvail().x;
  const float picture_height = ImGui::GetFontSize() * 11.0f;
  ImGui::SeparatorText("Draufsicht: Ey in der Ebene durch den Kern (Momentaufnahme)");
  show(top_, avail, picture_height);
  ImGui::SeparatorText("Querschnitt |Ey| am Messort (links 3D-FDTD, rechts Modenlöser)");
  show(cross_, 0.48f * avail, picture_height);
  ImGui::SameLine();
  show(mode_, 0.48f * avail, picture_height);
  if (ImPlot::BeginPlot("##phase3d", ImVec2(-1.0f, std::max(ImGui::GetContentRegionAvail().y, 120.0f)))) {
    ImPlot::SetupAxes("x [um]", "Phase [rad]");
    std::vector<double> x_um(r.axis_x.size());
    for (std::size_t i = 0; i < x_um.size(); ++i) x_um[i] = r.axis_x[i] / units::um;
    ImPlot::PlotLine("Phase von Ey auf der Achse", x_um.data(), r.phase.data(),
                     static_cast<int>(x_um.size()));
    ImPlot::EndPlot();
  }
  ImGui::EndChild();
}

}  // namespace photonlab::gui
