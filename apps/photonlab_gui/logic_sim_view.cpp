#include "logic_sim_view.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>

#include "implot.h"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

const char* design_labels[] = {"Halbaddierer", "Volladdierer", "4-Bit-Addierer", "UND-Kette (4 Stufen)",
                               "SR-Flip-Flop"};
const logic::Family families[] = {logic::Family::ideal, logic::Family::regenerated, logic::Family::passive};

std::vector<std::vector<int>> vectors_for(const std::string& design, const logic::LogicCircuit& c) {
  if (design == "sr_latch")
    return {{0, 0}, {1, 0}, {0, 0}, {0, 0}, {0, 1}, {0, 0}, {1, 0}, {0, 0}, {0, 1}, {0, 0}};
  return logic::all_vectors(c.inputs.size());
}

std::string bits_text(const std::vector<int>& bits) {
  std::string s;
  for (const int b : bits) s += static_cast<char>('0' + b);
  return s;
}

}  // namespace

LogicSimView::LogicSimView(ThreadPool& pool) : pool_(pool) {}

LogicSimView::~LogicSimView() {
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
}

void LogicSimView::start() {
  if (future_.valid()) return;
  control_ = std::make_shared<JobControl>();
  future_ = pool_.submit([design = design_, family = family_, control = control_] {
    Result r;
    r.design = design, r.family = family;
    try {
      const std::string name = logic::design_names()[static_cast<std::size_t>(design)];
      r.circuit = logic::design(name);
      logic::SimSettings settings;
      settings.check_idle_only = name == "sr_latch";
      r.sim =
          logic::simulate(r.circuit, families[family], vectors_for(name, r.circuit), settings, control.get());
    } catch (const std::exception& e) {
      r.error = e.what();
    }
    control->finished = true;
    return r;
  });
}

void LogicSimView::update() {
  if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  Result r = future_.get();
  control_.reset();
  if (r.sim.cancelled) return;
  result_ = std::move(r);
  has_result_ = true;
  first_vector_ = 0;
  fit_ = true;
}

void LogicSimView::draw_timing(const Result& r) {
  // Zeitdiagramm: primäre Ein- und Ausgänge über 8 Vektoren, Leistung normiert auf den größten Wert des
  // Netzes; gestrichelt die ideale Logik
  const auto& traces = r.sim.traces;
  if (traces.empty()) return;
  const double period = 1e-9;
  const int count = 8;
  const int max_first = std::max(0, static_cast<int>(r.sim.vectors.size()) - count);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  ImGui::SliderInt("erster Vektor", &first_vector_, 0, std::min(max_first, 56));
  const double t_from = first_vector_ * period, t_to = (first_vector_ + count) * period;
  std::vector<std::string> names;
  for (const auto& in : r.circuit.inputs) names.push_back("in:" + in);
  std::vector<std::size_t> nets;
  for (std::size_t k = 0; k < traces.size(); ++k)
    if (std::find(names.begin(), names.end(), traces[k].name) != names.end()) nets.push_back(k);
  // Ausgänge: Netze, die an primäre Ausgänge gehen, erkennt man an den Verbindungen
  for (const auto& w : r.circuit.wires)
    if (w.to.rfind("out:", 0) == 0) {
      std::string from = w.from;
      if (from.size() > 4 && from.substr(from.size() - 4) == ".out") {
        // Name des Netzes ist "<Instanz>.<Ausgangsname>": über die Spuren suchen
        const std::string inst = from.substr(0, from.size() - 4) + ".";
        for (std::size_t k = 0; k < traces.size(); ++k)
          if (traces[k].name.rfind(inst, 0) == 0) {
            nets.push_back(k);
            break;
          }
      } else {
        for (std::size_t k = 0; k < traces.size(); ++k)
          if (traces[k].name == from) nets.push_back(k);
      }
    }
  if (fit_) ImPlot::SetNextAxesToFit();
  fit_ = false;
  if (!ImPlot::BeginPlot("##zeit", ImVec2(-1.0f, -1.0f))) return;
  ImPlot::SetupAxes("Zeit [ns]", "Netz (Leistung normiert)", 0, ImPlotAxisFlags_NoTickLabels);
  ImPlot::SetupAxisLimits(ImAxis_X1, t_from / 1e-9, t_to / 1e-9, ImPlotCond_Always);
  ImPlot::SetupLegend(ImPlotLocation_East, ImPlotLegendFlags_Outside);
  double offset = 0.0;
  for (auto it = nets.rbegin(); it != nets.rend(); ++it) {
    const auto& tr = traces[*it];
    double peak = 1e-15;
    for (const double p : tr.power) peak = std::max(peak, p);
    std::vector<double> x, y, yi;
    for (std::size_t i = 0; i < tr.time.size(); ++i) {
      const double t = tr.time[i] / 1e-9;
      if (!x.empty()) {  // Stufen: alter Wert bis zur neuen Zeit
        x.push_back(t);
        y.push_back(y.back());
        yi.push_back(yi.back());
      }
      x.push_back(t);
      y.push_back(offset + 0.8 * tr.power[i] / peak);
      yi.push_back(offset + 0.8 * tr.bit[i]);
    }
    x.push_back(t_to / 1e-9 + 100.0);
    y.push_back(y.back());
    yi.push_back(yi.back());
    ImPlot::SetNextLineStyle(ImVec4(0.6f, 0.6f, 0.6f, 0.6f), 1.0f);
    ImPlot::PlotLine(("##ideal" + tr.name).c_str(), x.data(), yi.data(), static_cast<int>(x.size()));
    ImPlot::PlotLine(tr.name.c_str(), x.data(), y.data(), static_cast<int>(x.size()));
    offset += 1.0;
  }
  ImPlot::EndPlot();
}

int LogicSimView::design_count() { return IM_ARRAYSIZE(design_labels); }
const char* LogicSimView::design_label(int design) { return design_labels[design]; }

void LogicSimView::draw() {
  update();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 48.0f);
  ImGui::TextUnformatted(
      "Logik-Ebene: charakterisierte Gatter werden zu Logikblöcken verschaltet. Der ereignisgesteuerte "
      "Simulator "
      "rechnet Logik und Physik zugleich und meldet, wo ein Gatter einen Pegel falsch liest.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  const bool running = future_.valid();
  ImGui::BeginDisabled(running);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  bool changed = ImGui::Combo("Design", &design_, design_labels, IM_ARRAYSIZE(design_labels));
  ImGui::SameLine();
  const char* family_labels[] = {"ideal (Referenz)", "regeneriert (O/E/O)", "passiv optisch"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  changed |= ImGui::Combo("Gatter", &family_, family_labels, 3);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(
        "ideal: Referenz ohne Kosten. regeneriert: jedes Gatter liest optisch, entscheidet elektrisch und "
        "sendet "
        "neu (30 ps, 1 pJ je Wechsel, 2 mW dauernd; Annahmen). passiv: die Interferenz-Gatter aus dem "
        "Chip-Designer bzw. ein Kerr-bistabiler Ring, Ausgang über den Schaltungslöser.");
  ImGui::EndDisabled();
  if (running) {
    ImGui::SameLine();
    ImGui::ProgressBar(control_->progress.load(), ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f));
  } else if (changed || !has_result_ || result_.design != design_ || result_.family != family_) {
    start();
  }
  if (!has_result_ || result_.design != design_ || result_.family != family_) return;
  const Result& r = result_;
  if (!r.error.empty()) {
    ImGui::TextColored(ui_color_bad, "%s", r.error.c_str());
    return;
  }
  const auto& sim = r.sim;
  const bool all = sim.correct == sim.checked;
  ImGui::TextColored(all ? ui_color_good : ui_color_bad, "%d von %d Vektoren richtig", sim.correct,
                     sim.checked);
  ImGui::SameLine();
  ImGui::Text("| %d Blöcke | längster Pfad %.0f ps | Energie je Vektor %s", sim.block_count,
              sim.latency / 1e-12, format_si(sim.energy_per_vector, "J").c_str());

  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::BeginChild("pruefung", ImVec2(width * 0.5f, 0.0f), ImGuiChildFlags_Borders);
  int errors = 0, warnings = 0;
  for (const auto& i : sim.static_issues) {
    if (i.severity == logic::Issue::Severity::error) ++errors;
    if (i.severity == logic::Issue::Severity::warning) ++warnings;
  }
  ImGui::SeparatorText("Prüfung der Verbindungen");
  ImGui::TextColored(errors     ? ui_color_bad
                     : warnings ? ui_color_limited
                                : ui_color_good,
                     "%d Fehler, %d Warnungen", errors, warnings);
  ImGui::PushTextWrapPos(0.0f);
  for (const auto& i : sim.static_issues) {
    const ImVec4 color = i.severity == logic::Issue::Severity::error     ? ui_color_bad
                         : i.severity == logic::Issue::Severity::warning ? ui_color_limited
                                                                         : ui_color_neutral;
    ImGui::TextColored(color, "%s:", i.where.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", i.text.c_str());
  }
  ImGui::SeparatorText("Falsche Vektoren (erste Ursache)");
  int listed = 0;
  for (const auto& v : sim.vectors) {
    if (!v.checked || v.correct) continue;
    if (++listed > 40) {
      ImGui::TextDisabled("...");
      break;
    }
    ImGui::Text("%s -> erwartet %s, gelesen %s", bits_text(v.inputs).c_str(), bits_text(v.expected).c_str(),
                bits_text(v.decoded).c_str());
    ImGui::TextDisabled("   %s", v.first_error.c_str());
  }
  if (listed == 0) ImGui::TextColored(ui_color_good, "keine");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("zeit", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Zeitdiagramm (grau: ideale Logik)");
  draw_timing(r);
  ImGui::EndChild();
}

}  // namespace photonlab::gui
