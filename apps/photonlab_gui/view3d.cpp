#include "view3d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <thread>

#include "implot.h"
#include "photonlab/core/units.hpp"
#include "photonlab/field/gpu.hpp"
#include "photonlab/materials/material.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

bool drag_nm(const char* label, double* value, double low, double high, float speed = 1.0f) {
  double shown = *value / units::nm;
  const double lo = low / units::nm, hi = high / units::nm;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.5f);
  if (ImGui::DragScalar(label, ImGuiDataType_Double, &shown, speed, &lo, &hi, "%.0f nm",
                        ImGuiSliderFlags_AlwaysClamp)) {
    *value = std::clamp(shown, lo, hi) * units::nm;
    return true;
  }
  return false;
}

void material_combo(const char* label, std::string* name, bool with_air) {
  const materials::Material* current = materials::find(*name);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
  if (ImGui::BeginCombo(label, current ? current->name.c_str() : name->c_str())) {
    if (with_air && ImGui::Selectable("Luft", *name == "Luft")) *name = "Luft";
    for (const auto& material : materials::library())
      if (ImGui::Selectable(material.name.c_str(), material.name == *name)) *name = material.name;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && current)
    ImGui::SetTooltip("%s", current->label.c_str());
}

}  // namespace

View3d::View3d(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer)
    : pool_(pool), designer_(designer), renderer_(renderer) {}

View3d::~View3d() {
  stop();
  if (future_.valid()) future_.wait();
  for (Picture* p : {&top_, &side_, &top3d_, &side3d_})
    if (p->texture) SDL_DestroyTexture(p->texture);
}

void View3d::stop() {
  if (control_) control_->cancel_requested = true;
}

void View3d::start() {
  if (future_.valid()) return;
  const field::FieldScene& scene = designer_.project().field;
  computed_scene_ = scene;
  has_run_ = true;
  if (!y_cut_set_) {
    // Seitenansicht durch die erste Quelle (dort liegt meist der Wellenleiter)
    double y = 0.5 * scene.height;
    for (const auto& s : scene.sources)
      if (s.enabled) {
        y = 0.5 * (s.y1 + s.y2);
        break;
      }
    y_cut_->store(y);
  }
  control_ = std::make_shared<JobControl>();
  snapshot_ = std::make_shared<Snapshot>();
  // Der Job besitzt Szene, Löser, Steuerung und Schnappschuss; er greift nie auf die Oberfläche zu.
  future_ = pool_.submit([scene, stack = scene.stack, control = control_, snap = snapshot_, y_cut = y_cut_] {
    const auto start = std::chrono::steady_clock::now();
    auto seconds = [&] {
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    try {
      field::Extruded3d sim(scene, stack);
      const double duration = scene.duration > 0.0 ? scene.duration : 1000 * units::fs;
      const std::size_t total = sim.steps_for(duration);
      auto publish = [&](std::size_t step) {
        const double y = y_cut->load();
        auto top = sim.top_view(false), side = sim.side_view(y, false);
        auto top_i = sim.top_view(true), side_i = sim.side_view(y, true);
        auto side_index = sim.side_index(y);
        std::lock_guard lock(snap->mutex);
        snap->top = std::move(top), snap->side = std::move(side);
        snap->top.insert(snap->top.end(), top_i.begin(), top_i.end());  // Feld, dahinter |E|²
        snap->side.insert(snap->side.end(), side_i.begin(), side_i.end());
        snap->side_index = std::move(side_index);
        snap->y_cut = y;
        snap->monitors = sim.monitors();
        snap->time = sim.time();
        snap->step = step;
        snap->seconds = seconds();
        ++snap->serial;
      };
      {
        std::lock_guard lock(snap->mutex);
        snap->nx = sim.solver().nx(), snap->ny = sim.solver().ny(), snap->nz = sim.solver().nz();
        snap->layer_k = sim.layer_k();
        snap->layer_bottom = sim.layer_bottom();
        snap->gpu = sim.solver().on_gpu();
        snap->cell = sim.solver().cell_size();
        snap->total = total;
        snap->warnings = sim.warnings();
        snap->indices = sim.indices();
        snap->top_index = sim.top_index();
      }
      publish(0);
      std::size_t done = 0;
      double last = seconds();
      while (done < total && !control->cancel_requested) {
        // Grafikkarte: größere Stücke, die Messwerte kommen je Stück zurück
        const std::size_t chunk = std::min<std::size_t>(sim.solver().on_gpu() ? 64 : 8, total - done);
        sim.run(chunk, control.get());
        done += chunk;
        control->progress = static_cast<float>(done) / static_cast<float>(total);
        if (seconds() - last > 0.3 || done >= total) {
          publish(done);
          last = seconds();
        }
      }
    } catch (const std::exception& e) {
      std::lock_guard lock(snap->mutex);
      snap->error = e.what();
      ++snap->serial;
    }
    control->finished = true;
  });
}

// Bild aus Werten (Feld mit Vorzeichen rot/blau, |E|² dunkel nach hell); flach: Kerne aufgehellt, für die
// 3D-Umgebung: Deckkraft nach Feldstärke (schwaches Feld durchsichtig)
void View3d::fill(Picture& picture, const std::vector<float>& values, const std::vector<float>& index, int nx,
                  int ny, bool flip_rows, bool alpha) {
  const std::size_t count = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
  if (values.size() < count || nx <= 0 || ny <= 0) return;
  if (!picture.texture || picture.nx != nx || picture.ny != ny) {
    if (picture.texture) SDL_DestroyTexture(picture.texture);
    picture.nx = nx, picture.ny = ny;
    picture.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, nx, ny);
    if (picture.texture) SDL_SetTextureScaleMode(picture.texture, SDL_SCALEMODE_LINEAR);
    if (picture.texture) SDL_SetTextureBlendMode(picture.texture, SDL_BLENDMODE_BLEND);
  }
  if (!picture.texture) return;
  const std::size_t offset = intensity_ && values.size() >= 2 * count ? count : 0;
  float peak = 0.0f;
  for (std::size_t k = 0; k < count; ++k) peak = std::max(peak, std::abs(values[offset + k]));
  if (peak <= 0.0f) peak = 1.0f;
  float n_low = 1e9f, n_high = 0.0f;
  for (float n : index) n_low = std::min(n_low, n), n_high = std::max(n_high, n);
  picture.pixels.assign(count * 4, 255);
  for (int row = 0; row < ny; ++row)
    for (int col = 0; col < nx; ++col) {
      const std::size_t k =
          static_cast<std::size_t>(col) +
          static_cast<std::size_t>(nx) * static_cast<std::size_t>(flip_rows ? ny - 1 - row : row);
      const float v = values[offset + k] / peak;
      float r, g, b;
      if (offset == 0) {
        const float a = std::min(std::pow(std::abs(v), 0.7f), 1.0f);
        r = v >= 0.0f ? 0.06f + 0.94f * a : 0.06f + 0.3f * a * a;
        g = 0.07f + 0.45f * a * a;
        b = v >= 0.0f ? 0.10f + 0.25f * a * a : 0.10f + 0.90f * a;
      } else {
        const float t = std::clamp(std::sqrt(std::abs(v)), 0.0f, 1.0f);
        r = std::min(1.0f, 1.6f * t), g = t * t,
        b = 0.35f * std::sin(3.1416f * std::min(t * 1.4f, 1.0f)) + 0.6f * t * t * t;
      }
      if (!alpha && k < index.size() && n_high > n_low) {
        const float tint = 0.16f * (index[k] - n_low) / (n_high - n_low);
        r += tint, g += tint, b += tint;
      }
      const std::size_t p =
          4 * (static_cast<std::size_t>(col) + static_cast<std::size_t>(nx) * static_cast<std::size_t>(row));
      picture.pixels[p + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
      picture.pixels[p + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
      picture.pixels[p + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
      if (alpha)
        picture.pixels[p + 3] = static_cast<std::uint8_t>(
            std::clamp(1.4f * std::pow(std::abs(v), offset == 0 ? 0.6f : 0.35f), 0.0f, 0.92f) * 255.0f);
    }
  SDL_UpdateTexture(picture.texture, nullptr, picture.pixels.data(), nx * 4);
}

void View3d::draw_settings() {
  field::FieldScene& scene = designer_.project().field;
  field::Stack3d& stack = scene.stack;
  const field::Stack3d before = stack;
  const bool busy = future_.valid();
  ImGui::BeginDisabled(busy);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Schicht");
  ImGui::SameLine();
  drag_nm("##dicke", &stack.thickness, 20e-9, 3e-6, 2.0f);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Dicke der Bauteilschicht (220 nm: Standard-SOI)");
  ImGui::SameLine();
  material_combo("Kern", &stack.core, false);
  ImGui::SameLine();
  material_combo("unten", &stack.substrate, true);
  ImGui::SameLine();
  material_combo("oben", &stack.cladding, true);
  ImGui::SameLine();
  drag_nm("Zelle", &stack.cell_size, 15e-9, 100e-9, 0.5f);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(
        "Würfelzelle des 3D-Gitters. 40 nm: grob, aber schnell; 20 nm: achtmal so viele Zellen.");
  ImGui::SameLine();
  double margin_um = stack.below / units::um;
  const double lo = 0.2, hi = 3.0;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
  if (ImGui::DragScalar("Mantel", ImGuiDataType_Double, &margin_um, 0.01f, &lo, &hi, "%.2f µm",
                        ImGuiSliderFlags_AlwaysClamp))
    stack.below = stack.above = margin_um * units::um;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Oxid bzw. Luft unter und über der Schicht bis zur Randschicht");
  ImGui::EndDisabled();

  if (!(stack == before)) designer_.mark_dirty();
  const field::Extent3d e = field::extent_3d(scene, stack);
  const double cells = static_cast<double>(e.nx) * e.ny * e.nz;
  const bool too_big = e.memory_bytes > 6e9;
  if (busy) {
    if (ImGui::Button("Stopp", ImVec2(ImGui::GetFontSize() * 4.0f, 0.0f))) stop();
    ImGui::SameLine();
    double t = 0.0, seconds = 0.0;
    std::size_t step = 0, total = 1;
    if (snapshot_) {
      std::lock_guard lock(snapshot_->mutex);
      t = snapshot_->time, seconds = snapshot_->seconds, step = snapshot_->step,
      total = std::max<std::size_t>(1, snapshot_->total);
    }
    char text[96];
    const double rest =
        step > 0 ? seconds * static_cast<double>(total - step) / static_cast<double>(step) : 0.0;
    std::snprintf(text, sizeof text, "%.0f fs, noch etwa %.0f s", t / units::fs, rest);
    ImGui::ProgressBar(control_ ? control_->progress.load() : 0.0f,
                       ImVec2(ImGui::GetFontSize() * 18.0f, 0.0f), text);
  } else {
    ImGui::BeginDisabled(too_big || scene.sources.empty());
    if (ImGui::Button("3D rechnen", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f))) start();
    ImGui::EndDisabled();
  }
  // Rechenzeit: mit der Rate der letzten Rechnung, sonst grob 50 Mio. Zell-Schritte je Sekunde und Kern
  if (snapshot_) {
    std::lock_guard lock(snapshot_->mutex);
    if (snapshot_->seconds > 1.0 && snapshot_->step > 0) {
      rate_ = static_cast<double>(snapshot_->nx) * snapshot_->ny * snapshot_->nz *
              static_cast<double>(snapshot_->step) / snapshot_->seconds;
      rate_gpu_ = snapshot_->gpu;
    }
  }
  if (rate_gpu_ != field::gpu::enabled()) rate_ = 0.0;  // Ansicht > Grafikkarte umgeschaltet
  const double rate = rate_ > 0.0 ? rate_ : 50e6 * std::max(1u, std::thread::hardware_concurrency());
  const double estimate = cells * static_cast<double>(e.steps) / rate;
  ImGui::SameLine();
  if (estimate < 90.0)
    ImGui::TextDisabled("etwa %.0f s", estimate);
  else
    ImGui::TextDisabled("etwa %.0f min", estimate / 60.0);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(rate_ > 0.0 ? "geschätzt mit der Geschwindigkeit der letzten 3D-Rechnung"
                                  : "grobe Schätzung (50 Mio. Zell-Schritte je Sekunde und Kern); nach der "
                                    "ersten Rechnung genauer");
  ImGui::SameLine();
  ImGui::TextDisabled("%d x %d x %d = %.1f Mio. Zellen, %.2f GB, %zu Schritte bis %.0f fs", e.nx, e.ny, e.nz,
                      cells / 1e6, e.memory_bytes / 1e9, e.steps, scene.duration / units::fs);
  if (too_big)
    ImGui::TextColored(ui_color_bad, "Zu groß für den Speicher: Zelle vergrößern oder Szene verkleinern.");
  if (scene.duration <= 0.0)
    ImGui::TextColored(ui_color_limited, "Keine Laufzeit gesetzt: gerechnet wird 1000 fs.");
}

void View3d::draw_pictures(float height) {
  const auto snap = snapshot_;
  if (!snap) return;
  int nx = 0, ny = 0, nz = 0;
  double cell = 0.0, y_cut = 0.0;
  {
    std::lock_guard lock(snap->mutex);
    nx = snap->nx, ny = snap->ny, nz = snap->nz, cell = snap->cell, y_cut = snap->y_cut;
  }
  if (!top_.texture || nx <= 0) return;
  // Draufsicht (y nach unten wie im 2D-Bild), darunter die Seitenansicht (z nach oben), gleicher Maßstab
  const float width = ImGui::GetContentRegionAvail().x;
  const float scale = std::max(
      1.0f, std::min(width / static_cast<float>(nx),
                     (height - 3.0f * ImGui::GetFrameHeightWithSpacing()) / static_cast<float>(ny + nz)));
  ImGui::TextDisabled("Draufsicht in der Mitte der Schicht");
  const ImVec2 top_pos = ImGui::GetCursorScreenPos();
  ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(top_.texture)),
               ImVec2(scale * static_cast<float>(nx), scale * static_cast<float>(ny)));
  // Lage der Seitenansicht als Linie
  const float line_y = top_pos.y + static_cast<float>(y_cut / cell) * scale;
  ImGui::GetWindowDrawList()->AddLine({top_pos.x, line_y},
                                      {top_pos.x + scale * static_cast<float>(nx), line_y},
                                      IM_COL32(255, 255, 255, 120), 1.0f);
  ImGui::TextDisabled("Seitenansicht (Schnitt entlang x)");
  ImGui::SameLine();
  double y_um = y_cut_->load() / units::um;
  const double y_lo = 0.0, y_hi = ny * cell / units::um;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  if (ImGui::SliderScalar("bei y", ImGuiDataType_Double, &y_um, &y_lo, &y_hi, "%.2f µm")) {
    y_cut_->store(y_um * units::um);
    y_cut_set_ = true;
  }
  ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(side_.texture)),
               ImVec2(scale * static_cast<float>(nx), scale * static_cast<float>(nz)));
}

void View3d::draw_monitors() {
  const auto snap = snapshot_;
  if (!snap) return;
  std::vector<field::Monitor3dData> monitors;
  double time = 0.0;
  std::size_t step = 0;
  {
    std::lock_guard lock(snap->mutex);
    monitors = snap->monitors;
    time = snap->time, step = snap->step;
  }
  if (monitors.empty()) return;
  const field::Monitor3dData* reference = nullptr;
  for (const auto& m : monitors)
    if (m.flux && m.mean != 0.0) {
      reference = &m;
      break;
    }
  if (ImGui::BeginTable("monitore3d", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("Monitor");
    ImGui::TableSetupColumn("Mittel (letzte Periode)");
    ImGui::TableSetupColumn(reference ? ("relativ zu " + reference->name).c_str() : "relativ");
    ImGui::TableHeadersRow();
    for (const auto& m : monitors) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(m.name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text(m.flux ? "%.4g W" : "%.4g V/m", m.mean);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m.flux ? "Leistung durch die Fläche, positiv in +x bzw. +y (Quelle mit 1 V/m)"
                                 : "Amplitude der Hauptkomponente in der Mitte der Schicht");
      ImGui::TableNextColumn();
      if (m.flux && reference) ImGui::Text("%.3f", m.mean / reference->mean);
    }
    ImGui::EndTable();
  }
  // Zeitverlauf der Flüsse, gemittelt über je eine Periode (ausgedünnt)
  if (step == 0 || !ImPlot::BeginPlot("##fluss3d", ImVec2(-1.0f, -1.0f))) return;
  ImPlot::SetupAxes("Zeit [fs]", "Fluss, Mittel einer Periode [W]", ImPlotAxisFlags_AutoFit,
                    ImPlotAxisFlags_AutoFit);
  const double dt_fs = time / static_cast<double>(step) / units::fs;
  for (const auto& m : monitors) {
    if (!m.flux || m.mean_history.empty()) continue;
    const std::size_t stride = std::max<std::size_t>(1, m.mean_history.size() / 3000);
    std::vector<double> x, y;
    for (std::size_t k = 0; k < m.mean_history.size(); k += stride) {
      x.push_back(static_cast<double>(k + 1) * dt_fs);
      y.push_back(m.mean_history[k]);
    }
    ImPlot::PlotLine(m.name.c_str(), x.data(), y.data(), static_cast<int>(x.size()));
  }
  ImPlot::EndPlot();
}

void View3d::draw(const ViewportHooks& hooks, float ui_scale) {
  if (future_.valid() && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    future_.get();
  draw_settings();
  const field::FieldScene& scene = designer_.project().field;
  ImGui::SameLine();
  ImGui::Checkbox("|E|²", &intensity_);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Intensität statt der Hauptkomponente (Ey bei Hz-Szenen, Ez bei Ez-Szenen)");

  // Zustand: Fehler, Hinweise, Brechzahlen, geänderte Szene (eine Zeile, wenn alles gut ist)
  if (snapshot_) {
    std::lock_guard lock(snapshot_->mutex);
    ImGui::PushTextWrapPos(0.0f);
    if (!snapshot_->error.empty()) ImGui::TextColored(ui_color_bad, "%s", snapshot_->error.c_str());
    for (const auto& w : snapshot_->warnings) ImGui::TextColored(ui_color_limited, "%s", w.c_str());
    ImGui::PopTextWrapPos();
    if (snapshot_->nx > 0)
      ImGui::TextDisabled("Kern n = %.3f, unten %.3f, oben %.3f", snapshot_->indices.core,
                          snapshot_->indices.substrate, snapshot_->indices.cladding);
  } else {
    ImGui::TextDisabled(
        "Die Szene als Schicht auf einem Chip. Formen mit einer 2D-Brechzahl ab 2 werden zum Kernmaterial; "
        "'3D rechnen' zeigt danach das Feld im Raum.");
  }
  if (has_run_ && !(scene == computed_scene_)) {
    ImGui::SameLine();
    ImGui::TextColored(ui_color_limited, "Szene seit der Rechnung geändert.");
  }

  // Feldschnitte für die 3D-Umgebung (Texturen werden in draw_pictures nachgeführt)
  FieldSlices slices;
  bool have_slices = false;
  if (snapshot_) {
    {
      std::lock_guard lock(snapshot_->mutex);
      if (snapshot_->nx > 0 && (snapshot_->serial != shown_serial_ || intensity_ != shown_intensity_)) {
        shown_serial_ = snapshot_->serial;
        shown_intensity_ = intensity_;
        fill(top_, snapshot_->top, snapshot_->top_index, snapshot_->nx, snapshot_->ny, false);
        fill(side_, snapshot_->side, snapshot_->side_index, snapshot_->nx, snapshot_->nz, true);
        fill(top3d_, snapshot_->top, snapshot_->top_index, snapshot_->nx, snapshot_->ny, false, true);
        fill(side3d_, snapshot_->side, snapshot_->side_index, snapshot_->nx, snapshot_->nz, true, true);
      }
      slices.grid_width = snapshot_->nx * snapshot_->cell;
      slices.grid_height = snapshot_->ny * snapshot_->cell;
      slices.grid_depth = snapshot_->nz * snapshot_->cell;
      slices.layer_bottom = snapshot_->layer_bottom;
      slices.y_cut = snapshot_->y_cut;
    }
    if (top3d_.texture && side3d_.texture) {
      slices.top = static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(top3d_.texture));
      slices.side = static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(side3d_.texture));
      have_slices = true;
    }
  }

  // 3D-Umgebung oben, darunter Monitore und flache Schnitte
  const float available = ImGui::GetContentRegionAvail().y;
  const float view_height = has_run_ ? available * 0.62f : available - ImGui::GetStyle().ItemSpacing.y;
  viewport_.draw(scene, scene.stack, have_slices ? &slices : nullptr, hooks,
                 ImVec2(ImGui::GetContentRegionAvail().x, view_height), ui_scale);
  if (!has_run_) return;
  if (ImGui::BeginTabBar("unten3d")) {
    if (ImGui::BeginTabItem("Monitore")) {
      draw_monitors();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Schnitte (flach)")) {
      ImGui::BeginChild("bilder3d", ImVec2(0.0f, 0.0f));
      draw_pictures(ImGui::GetContentRegionAvail().y);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

}  // namespace photonlab::gui
