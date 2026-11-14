#include "field_gate_view.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>

#include "photonlab/core/units.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

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

void traffic_light(logic::Rating rating) {
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddCircleFilled({pos.x + size * 0.5f, pos.y + size * 0.5f}, size * 0.38f,
                                              ImGui::ColorConvertFloat4ToU32(rating_color(rating)));
  ImGui::Dummy(ImVec2(size, size));
}

// Beispiel-Szene (Referenz-Projekt), die zum Gatter gehört
const char* scene_key(const std::string& gate) {
  return gate == "phc_or_and" ? "field_phc_y" : "field_phc_y_xor";
}

}  // namespace

FieldGateView::FieldGateView(ThreadPool& pool, ChipDesigner& designer)
    : pool_(pool), designer_(designer), names_(logic::field_gate_names()) {}

FieldGateView::~FieldGateView() {
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
}

void FieldGateView::start() {
  if (future_.valid()) return;
  control_ = std::make_shared<JobControl>();
  // Der Job besitzt Gatter und Steuerung und greift nie auf die Oberfläche zu.
  future_ =
      pool_.submit([name = names_[static_cast<std::size_t>(selected_)], control = control_, pool = &pool_] {
        Result result;
        result.gate_name = name;
        const auto t0 = std::chrono::steady_clock::now();
        try {
          result.gate = logic::field_gate(name);
          result.report = logic::characterize_field_gate(result.gate, pool, control.get());
          result.cancelled = result.report.table.rows.empty();
        } catch (const std::exception& e) {
          result.error = e.what();
        }
        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        control->finished = true;
        return result;
      });
}

void FieldGateView::update() {
  if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  Result result = future_.get();
  control_.reset();
  if (result.cancelled) return;
  result_ = std::move(result);
  has_result_ = true;
}

void FieldGateView::draw_result() {
  const logic::FieldGate& gate = result_.gate;
  const logic::FieldGateReport& report = result_.report;
  ImGui::SeparatorText("Wahrheitstabelle");
  const int input_count = static_cast<int>(gate.inputs.size());
  const int output_count = static_cast<int>(gate.outputs.size());
  if (ImGui::BeginTable("tabelle", input_count + 2 * output_count,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    for (const auto& in : gate.inputs) ImGui::TableSetupColumn(in.name.c_str());
    for (const auto& out : gate.outputs) {
      ImGui::TableSetupColumn(out.name.c_str());
      ImGui::TableSetupColumn("soll");
    }
    ImGui::TableHeadersRow();
    for (std::size_t r = 0; r < report.table.rows.size(); ++r) {
      ImGui::TableNextRow();
      for (const int bit : report.table.rows[r].bits) {
        ImGui::TableNextColumn();
        ImGui::Text("%d", bit);
      }
      for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
        const logic::OutputAnalysis& a = report.outputs[k];
        const double level = report.table.rows[r].power[k];
        const int expected = gate.outputs[k].expected.empty() ? -1 : gate.outputs[k].expected[r];
        const bool decided_one = level > a.threshold;
        const bool right = expected < 0 || (decided_one == (expected == 1));
        ImGui::TableNextColumn();
        ImGui::TextColored(right ? ui_color_good : ui_color_bad, "%.4f", level / a.p_max);
        ImGui::TableNextColumn();
        ImGui::Text("%d", expected);
      }
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled(
      "Pegel relativ zum größten Pegel des Ausgangs; grün = richtig entschieden (Schwelle mittig).");
  ImGui::Spacing();
  for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
    const logic::OutputAnalysis& a = report.outputs[k];
    ImGui::Text("%s:", gate.outputs[k].name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(a.separable ? ui_color_good : ui_color_bad, "%s",
                       a.separable ? "Funktion richtig" : "Funktion falsch");
    ImGui::SameLine();
    ImGui::Text("Kontrastverhältnis %s", logic::format_db(a.contrast_db).c_str());
  }

  ImGui::SeparatorText("Zeugnis");
  for (const auto& c : report.criteria) {
    traffic_light(c.rating);
    ImGui::SameLine();
    ImGui::TextUnformatted(c.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(rating_color(c.rating), "%s", c.value.c_str());
    ImGui::Indent(ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", c.remark.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Unindent(ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.x);
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Wellenlänge %.0f nm, Rechenzeit %.0f s. Schwellen der Ampeln wie im Logik-Labor der Schaltung "
      "(Festlegung von PhotonLab, docs/physics/logic.md). Die Leistung ist der gemittelte Fluss durch die "
      "Monitorlinie; nur Verhältnisse haben Bedeutung.",
      report.wavelength / units::nm, result_.seconds);
  ImGui::PopTextWrapPos();
}

void FieldGateView::draw() {
  update();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 44.0f);
  ImGui::TextUnformatted(
      "Gatter im photonischen Kristall: jede Eingangskombination wird im 2D-FDTD gerechnet, die "
      "Wahrheitstabelle "
      "entsteht automatisch aus dem Fluss im Ausgangsarm.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();

  const bool running = future_.valid();
  ImGui::BeginDisabled(running);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  if (ImGui::BeginCombo("Gatter", names_[static_cast<std::size_t>(selected_)].c_str())) {
    for (int i = 0; i < static_cast<int>(names_.size()); ++i)
      if (ImGui::Selectable(names_[static_cast<std::size_t>(i)].c_str(), i == selected_)) {
        selected_ = i;
        has_result_ = false;
      }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  if (gate_for_ != selected_) {
    gate_ = logic::field_gate(names_[static_cast<std::size_t>(selected_)]);
    gate_for_ = selected_;
  }
  const logic::FieldGate& gate = gate_;
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 44.0f);
  ImGui::TextDisabled("%s", gate.description.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Spacing();

  if (running) {
    if (ImGui::Button("Abbrechen")) control_->cancel_requested = true;
    ImGui::SameLine();
    ImGui::ProgressBar(control_->progress.load(), ImVec2(ImGui::GetFontSize() * 14.0f, 0.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("FDTD-Rechnungen laufen (je etwa 40 bis 70 s, parallel) ...");
  } else {
    if (ImGui::Button("Wahrheitstabelle erzeugen")) start();
    ImGui::SameLine();
    if (ImGui::Button("Szene im Bauteil-Labor öffnen")) designer_.load_example(scene_key(gate.name));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip(
          "Lädt die Feld-Szene als Beispiel (ersetzt das Projekt, fragt bei ungespeicherten Änderungen).");
  }
  if (has_result_ && result_.gate_name == gate.name) {
    if (!result_.error.empty()) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(ui_color_bad, "%s", result_.error.c_str());
      ImGui::PopTextWrapPos();
    } else {
      draw_result();
    }
  }
}

}  // namespace photonlab::gui
