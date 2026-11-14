#include "eye_view.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>

#include "implot.h"
#include "photonlab/core/units.hpp"
#include "photonlab/io/project.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

bool drag(const char* label, double* value, double scale, double low, double high, float speed,
          const char* format) {
  double shown = *value / scale;
  const double lo = low / scale, hi = high / scale;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
  if (ImGui::DragScalar(label, ImGuiDataType_Double, &shown, speed, &lo, &hi, format,
                        ImGuiSliderFlags_AlwaysClamp)) {
    *value = std::clamp(shown, lo, hi) * scale;
    return true;
  }
  return false;
}

}  // namespace

EyeView::EyeView(ThreadPool& pool, ChipDesigner& designer) : pool_(pool), designer_(designer) {}

EyeView::~EyeView() {
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
}

std::string EyeView::key() const {
  // alle Einstellungen als Text: ändert sich etwas, wird neu gerechnet
  io::Project probe;
  probe.link = designer_.project().link;
  probe.has_link = true;
  return io::project_to_json(probe);
}

void EyeView::update() {
  if (future_.valid() && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    Result r = future_.get();
    const bool cancelled = control_ && control_->cancel_requested;
    control_.reset();
    if (!cancelled) {
      result_ = std::move(r);
      fit_ = true;
    }
  }
  if (future_.valid() || !designer_.project().has_link) return;
  const std::string current = key();
  if (current == result_.key) return;
  control_ = std::make_shared<JobControl>();
  future_ = pool_.submit([settings = designer_.project().link, current, control = control_] {
    Result r;
    r.key = current;
    try {
      r.link = signal::simulate_link(settings, control.get());
    } catch (const std::exception& e) {
      r.error = e.what();
    }
    control->finished = true;
    return r;
  });
}

void EyeView::draw() {
  update();
  ImGui::Begin(window_name);
  io::Project& project = designer_.project();
  if (!project.has_link) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "Zeitbereich einer Übertragungsstrecke: Pseudozufallsdaten steuern einen Modulator (Ring oder "
        "Mach-Zehnder mit PN-Übergang), eine Photodiode mit Rauschen empfängt. Ergebnis: Augendiagramm, "
        "Q-Faktor, "
        "Bitfehlerrate.");
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Strecke anlegen")) {
      project.has_link = true;
      designer_.mark_dirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Beispiel Ring-Modulator laden")) designer_.load_example("ring_modulator");
    ImGui::End();
    return;
  }
  signal::LinkSettings& s = project.link;
  bool changed = false;
  ImGui::BeginChild("einstellungen", ImVec2(ImGui::GetFontSize() * 17.0f, 0.0f), ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Daten");
  changed |= drag("Bitrate [Gbit/s]", &s.bit_rate, 1e9, 0.1e9, 200e9, 0.1f, "%.2f");
  int order = s.prbs_order;
  const char* orders[] = {"PRBS 7", "PRBS 9", "PRBS 11"};
  int sel = order == 7 ? 0 : order == 9 ? 1 : 2;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
  if (ImGui::Combo("Folge", &sel, orders, 3)) s.prbs_order = sel == 0 ? 7 : sel == 1 ? 9 : 11, changed = true;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
  if (ImGui::DragInt("Bits", &s.bits, 4.0f, 64, 8192, "%d", ImGuiSliderFlags_AlwaysClamp)) changed = true;
  ImGui::SeparatorText("Sender und Modulator");
  changed |= drag("Laser [mW]", &s.laser_power, 1e-3, 1e-6, 0.1, 0.01f, "%.3f");
  int kind = s.modulator == signal::ModulatorKind::ring ? 0 : 1;
  const char* kinds[] = {"Ring-Modulator", "Mach-Zehnder (Gegentakt)"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
  if (ImGui::Combo("##modulator", &kind, kinds, 2)) {
    s.modulator = kind == 0 ? signal::ModulatorKind::ring : signal::ModulatorKind::mzm;
    changed = true;
  }
  changed |= drag("Spannung 0 [V]", &s.v_low, 1.0, -0.5, 20.0, 0.02f, "%.2f");
  changed |= drag("Spannung 1 [V]", &s.v_high, 1.0, -0.5, 20.0, 0.02f, "%.2f");
  if (s.modulator == signal::ModulatorKind::ring) {
    changed |= drag("Verstimmung [pm]", &s.ring_detuning, 1e-12, -200e-12, 200e-12, 0.2f, "%.1f");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Laserwellenlänge minus Resonanz bei 'Spannung 0'.");
    changed |= drag("Kopplung", &s.ring_coupling, 1.0, 0.001, 0.5, 0.0005f, "%.4f");
    changed |= drag("Radius [um]", &s.ring_radius, 1e-6, 2e-6, 100e-6, 0.05f, "%.1f");
  } else {
    changed |= drag("Länge [mm]", &s.mzm_length, 1e-3, 0.1e-3, 20e-3, 0.01f, "%.2f");
    changed |= drag("Arbeitspunkt [rad]", &s.mzm_bias_phase, 1.0, -6.3, 6.3, 0.01f, "%.2f");
  }
  changed |= drag("Treiber [GHz]", &s.driver_bandwidth, 1e9, 0.0, 500e9, 0.1f, "%.1f");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("3-dB-Bandbreite des Treibers; 0 = ideal.");
  if (ImGui::Checkbox("RC der Sperrschicht", &s.junction_rc)) changed = true;
  ImGui::SeparatorText("Empfänger");
  changed |= drag("Bandbreite [GHz]", &s.receiver_bandwidth, 1e9, 0.0, 500e9, 0.1f, "%.1f");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("0 = 0,7 x Bitrate.");
  changed |= drag("Streckenverlust [dB]", &s.link_loss_db, 1.0, 0.0, 60.0, 0.1f, "%.1f");
  if (ImGui::Checkbox("Rauschen", &s.noise)) changed = true;
  changed |= drag("RIN [dB/Hz]", &s.rin_db_per_hz, 1.0, -1000.0, -100.0, 0.5f, "%.0f");
  changed |= drag("Last [Ohm]", &s.load_resistance, 1.0, 1.0, 1e6, 1.0f, "%.0f");
  if (changed) designer_.mark_dirty();
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Ring als Coupled-Mode-Modell (Photonenlebensdauer), Mach-Zehnder quasistatisch; PN-Physik wie die "
      "Bauteile. "
      "Rauschen weiß, gaußförmig, in der Rauschbandbreite des Empfängers.");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("auge", ImVec2(0.0f, 0.0f));
  if (future_.valid()) {
    ImGui::ProgressBar(control_->progress.load(), ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("rechnet ...");
  }
  if (!result_.error.empty()) {
    ImGui::TextColored(ui_color_bad, "%s", result_.error.c_str());
  } else if (!result_.link.eye.empty()) {
    const signal::LinkResult& r = result_.link;
    const ImVec4 q_color = r.q_factor >= 7.0   ? ui_color_good
                           : r.q_factor >= 4.0 ? ui_color_limited
                                               : ui_color_bad;
    ImGui::TextColored(q_color, "Q = %.2f", r.q_factor);
    ImGui::SameLine();
    ImGui::TextColored(q_color, "BER = %.1e", r.ber);
    ImGui::SameLine();
    ImGui::Text("| Extinktion %.1f dB | Augenöffnung (ohne Rauschen) %.0f %%", r.extinction_db,
                100.0 * r.eye_opening);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip(
          "Augenöffnung: (kleinste 1 - größte 0) / (mittlere 1 - mittlere 0) ohne Rauschen; 100 %% = keine "
          "Intersymbol-Interferenz.");
    const std::size_t width = r.eye.front().size();
    std::vector<double> t(width);
    for (std::size_t k = 0; k < width; ++k) t[k] = static_cast<double>(k) * r.sample_interval / 1e-12;
    if (fit_) ImPlot::SetNextAxesToFit();
    fit_ = false;
    if (ImPlot::BeginPlot("##auge", ImVec2(-1.0f, -1.0f))) {
      ImPlot::SetupAxes("Zeit [ps]", "Photostrom [uA]");
      const std::size_t shown = std::min<std::size_t>(r.eye.size(), 300);
      std::vector<double> y(width);
      ImPlot::SetNextLineStyle(ImVec4(0.35f, 0.75f, 1.0f, 0.18f), 1.0f);
      for (std::size_t e = 0; e < shown; ++e) {
        for (std::size_t k = 0; k < width; ++k) y[k] = r.eye[e][k] / 1e-6;
        ImPlot::SetNextLineStyle(ImVec4(0.35f, 0.75f, 1.0f, 0.18f), 1.0f);
        ImPlot::PlotLine("##spur", t.data(), y.data(), static_cast<int>(width));
      }
      // Abtastzeitpunkt und Pegel
      const double center = t[width / 2];
      ImPlot::DragLineX(0, const_cast<double*>(&center), ImVec4(1.0f, 0.6f, 0.2f, 0.8f), 1.0f,
                        ImPlotDragToolFlags_NoInputs);
      double levels[2] = {r.mean1 / 1e-6, r.mean0 / 1e-6};
      ImPlot::DragLineY(1, &levels[0], ImVec4(0.4f, 0.9f, 0.5f, 0.6f), 1.0f, ImPlotDragToolFlags_NoInputs);
      ImPlot::DragLineY(2, &levels[1], ImVec4(0.4f, 0.9f, 0.5f, 0.6f), 1.0f, ImPlotDragToolFlags_NoInputs);
      ImPlot::EndPlot();
    }
  } else if (!future_.valid()) {
    ImGui::TextDisabled("Noch kein Ergebnis.");
  }
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace photonlab::gui
