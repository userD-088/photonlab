#include "quantum_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <numbers>
#include <numeric>

#include "implot.h"
#include "photonlab/io/project.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

bool slider(const char* label, double* v, double lo, double hi, const char* fmt = "%.3f") {
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  return ImGui::SliderScalar(label, ImGuiDataType_Double, v, &lo, &hi, fmt, ImGuiSliderFlags_AlwaysClamp);
}

std::string pattern_text(const quantum::Pattern& p) {
  std::string s;
  for (const int k : p) s += static_cast<char>('0' + std::min(k, 9));
  return s;
}

}  // namespace

QuantumView::QuantumView(ChipDesigner& designer) : designer_(designer) {}

void QuantumView::draw() {
  ImGui::Begin(window_name);
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 52.0f);
  ImGui::TextDisabled(
      "Einzelne Photonen statt klassischer Felder: ein Interferometer ist eine unitäre Matrix U, die "
      "Wahrscheinlichkeiten "
      "für n Photonen sind Permanenten von Teilmatrizen von U. Exakt bis etwa 10 Photonen - genau deshalb "
      "ist "
      "Quantenphotonik als Rechenmodell interessant.");
  ImGui::PopTextWrapPos();
  if (ImGui::BeginTabBar("quanten")) {
    if (ImGui::BeginTabItem("Hong-Ou-Mandel")) {
      draw_hom();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("CNOT")) {
      draw_cnot();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Boson Sampling")) {
      draw_boson();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("MZI-Gitter")) {
      draw_mesh();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Schaltung als Interferometer")) {
      draw_circuit();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void QuantumView::draw_hom() {
  ImGui::BeginChild("hom_einst", ImVec2(ImGui::GetFontSize() * 20.0f, 0.0f), ImGuiChildFlags_Borders);
  hom_dirty_ |= slider("Reflektivität", &hom_.reflectivity, 0.0, 1.0);
  hom_dirty_ |= slider("Ununterscheidbarkeit", &hom_.indistinguishability, 0.0, 1.0);
  hom_dirty_ |= slider("Mehrphotonen p2", &hom_.multiphoton, 0.0, 0.2, "%.4f");
  hom_dirty_ |= slider("Detektor-Effizienz", &hom_.detectors.efficiency, 0.01, 1.0);
  hom_dirty_ |= slider("Dunkelzählung", &hom_.detectors.dark_count, 0.0, 0.05, "%.5f");
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Zwei Photonen auf einen Strahlteiler. Ununterscheidbare Photonen verlassen ihn gemeinsam: die "
      "Koinzidenzen "
      "verschwinden. Mehrphotonen-Anteil (angekündigte Quellen) und Dunkelzählungen füllen den Einbruch "
      "auf.");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
  if (hom_dirty_) {
    hom_result_ = quantum::hong_ou_mandel(hom_, 121);
    hom_dirty_ = false;
  }
  ImGui::SameLine();
  ImGui::BeginChild("hom_plot", ImVec2(0.0f, 0.0f));
  const ImVec4 c = hom_result_.visibility > 0.9   ? ui_color_good
                   : hom_result_.visibility > 0.5 ? ui_color_limited
                                                  : ui_color_bad;
  ImGui::TextColored(c, "Sichtbarkeit %.4f", hom_result_.visibility);
  ImGui::SameLine();
  ImGui::Text("| Koinzidenz bei 0: %.4f, ohne Überlapp: %.4f", hom_result_.coincidence,
              hom_result_.coincidence_far);
  std::vector<double> x(hom_result_.delay.size());
  for (std::size_t k = 0; k < x.size(); ++k) x[k] = hom_result_.delay[k] / hom_.coherence_time;
  ImPlot::SetNextAxesToFit();
  if (ImPlot::BeginPlot("##hom", ImVec2(-1.0f, -1.0f))) {
    ImPlot::SetupAxes("Verzögerung [Kohärenzzeiten]", "Koinzidenz-Wahrscheinlichkeit");
    ImPlot::PlotLine("Koinzidenz", x.data(), hom_result_.curve.data(), static_cast<int>(x.size()));
    ImPlot::EndPlot();
  }
  ImGui::EndChild();
}

void QuantumView::draw_cnot() {
  cnot_dirty_ |= slider("Reflektivität der 1/3-Teiler", &cnot_reflectivity_, 0.2, 0.5);
  cnot_dirty_ |= slider("Ununterscheidbarkeit", &cnot_indist_, 0.0, 1.0);
  if (cnot_dirty_) {
    cnot_ = quantum::linear_optical_cnot(cnot_reflectivity_, cnot_indist_);
    cnot_dirty_ = false;
  }
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 52.0f);
  ImGui::TextDisabled(
      "Ralph u. a. 2002 / O'Brien u. a. 2003: Kontroll- und Ziel-Qubit je als Photon in zwei Wellenleitern, "
      "drei "
      "Strahlteiler mit R = 1/3. Gültig ist ein Durchlauf nur, wenn in beiden Paaren je ein Photon ankommt "
      "(Nachselektion): im Idealfall in 1/9 der Fälle.");
  ImGui::PopTextWrapPos();
  const char* names[4] = {"|00>", "|01>", "|10>", "|11>"};
  if (ImGui::BeginTable("cnot", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("Eingang");
    for (const char* n : names) ImGui::TableSetupColumn(n);
    ImGui::TableSetupColumn("Erfolg");
    ImGui::TableHeadersRow();
    for (int in = 0; in < 4; ++in) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(names[in]);
      for (int out = 0; out < 4; ++out) {
        ImGui::TableNextColumn();
        const double p = cnot_.truth[static_cast<std::size_t>(in)][static_cast<std::size_t>(out)];
        ImGui::TextColored(p > 0.9    ? ui_color_good
                           : p > 0.05 ? ui_color_limited
                                      : ui_color_neutral,
                           "%.3f", p);
      }
      ImGui::TableNextColumn();
      ImGui::Text("%.4f", cnot_.success[static_cast<std::size_t>(in)]);
    }
    ImGui::EndTable();
  }
  ImGui::Text("Güte der Wahrheitstabelle %.4f | Erfolg 1/9 = %.4f", cnot_.fidelity, 1.0 / 9.0);
  if (cnot_indist_ >= 1.0)
    ImGui::Text("Bell-Zustand (Kontrolle in Überlagerung): Güte mit Phi+ %.4f", cnot_.bell_fidelity);
}

void QuantumView::draw_boson() {
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  bs_dirty_ |= ImGui::SliderInt("Moden", &bs_modes_, 2, 8);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  bs_dirty_ |= ImGui::SliderInt("Photonen", &bs_photons_, 1, std::min(bs_modes_, 4));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  bs_dirty_ |= ImGui::SliderInt("Stichproben", &bs_samples_, 100, 100000, "%d", ImGuiSliderFlags_Logarithmic);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  bs_dirty_ |= ImGui::InputInt("Zufall", &bs_seed_);
  bs_photons_ = std::min(bs_photons_, bs_modes_);
  if (bs_dirty_) {
    bs_ = quantum::boson_sampling(bs_modes_, bs_photons_, static_cast<std::uint64_t>(std::max(bs_seed_, 0)),
                                  bs_samples_);
    bs_dirty_ = false;
  }
  ImGui::Text(
      "%zu mögliche Ergebnisse | Abstand Stichprobe - Permanenten %.4f | unterscheidbare Photonen - "
      "Permanenten %.4f",
      bs_.outcomes.size(), bs_.tvd_sampled, bs_.tvd_distinguishable);
  // die 24 wahrscheinlichsten Ergebnisse
  std::vector<std::size_t> order(bs_.outcomes.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return bs_.exact[a] > bs_.exact[b]; });
  const std::size_t shown = std::min<std::size_t>(order.size(), 24);
  std::vector<double> e, d, s, pos;
  std::vector<std::string> labels;
  for (std::size_t k = 0; k < shown; ++k) {
    e.push_back(bs_.exact[order[k]]);
    d.push_back(bs_.distinguishable[order[k]]);
    s.push_back(bs_.sampled[order[k]]);
    pos.push_back(static_cast<double>(k));
    labels.push_back(pattern_text(bs_.outcomes[order[k]]));
  }
  std::vector<const char*> label_ptr;
  for (const auto& l : labels) label_ptr.push_back(l.c_str());
  ImPlot::SetNextAxesToFit();
  if (ImPlot::BeginPlot("##bs", ImVec2(-1.0f, -1.0f))) {
    ImPlot::SetupAxes("Ausgangsbesetzung", "Wahrscheinlichkeit");
    if (!pos.empty())
      ImPlot::SetupAxisTicks(ImAxis_X1, pos.data(), static_cast<int>(pos.size()), label_ptr.data());
    ImPlot::PlotBars("Permanenten (exakt)", e.data(), static_cast<int>(e.size()), 0.25, -0.27);
    ImPlot::PlotBars("Stichprobe", s.data(), static_cast<int>(s.size()), 0.25, 0.0);
    ImPlot::PlotBars("unterscheidbar", d.data(), static_cast<int>(d.size()), 0.25, 0.27);
    ImPlot::EndPlot();
  }
}

void QuantumView::draw_mesh() {
  ImGui::BeginChild("mesh_einst", ImVec2(ImGui::GetFontSize() * 21.0f, 0.0f), ImGuiChildFlags_Borders);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  mesh_dirty_ |= ImGui::SliderInt("Größe N", &mesh_size_, 2, 10);
  const char* targets[] = {"zufällig (Haar)", "Fourier (DFT)", "Permutation (Zyklus)"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
  mesh_dirty_ |= ImGui::Combo("Ziel", &mesh_target_, targets, 3);
  if (mesh_target_ == 0) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    mesh_dirty_ |= ImGui::InputInt("Zufall", &mesh_seed_);
  }
  ImGui::SeparatorText("Fertigungsfehler");
  mesh_dirty_ |= slider("Phasen [rad]", &mesh_errors_.phase_sigma, 0.0, 0.5);
  mesh_dirty_ |= slider("Koppler (Teilung)", &mesh_errors_.coupler_sigma, 0.0, 0.1);
  mesh_dirty_ |= slider("Verlust je MZI [dB]", &mesh_errors_.loss_db, 0.0, 1.0);
  mesh_dirty_ |= slider("Übersprechen", &mesh_errors_.crosstalk, 0.0, 0.2);
  if (mesh_dirty_) {
    const int n = mesh_size_;
    if (mesh_target_ == 0) {
      mesh_u_ = quantum::random_unitary(n, static_cast<std::uint64_t>(std::max(mesh_seed_, 0)));
    } else if (mesh_target_ == 1) {
      mesh_u_.resize(n, n);
      for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
          mesh_u_(i, j) = std::exp(quantum::cplx(0.0, 2.0 * std::numbers::pi * i * j / n)) /
                          std::sqrt(static_cast<double>(n));
    } else {
      mesh_u_ = quantum::Matrix::Zero(n, n);
      for (int i = 0; i < n; ++i) mesh_u_((i + 1) % n, i) = 1.0;
    }
    mesh_ = quantum::clements_decompose(mesh_u_);
    mesh_reconstruction_ = (quantum::reconstruct(mesh_) - mesh_u_).norm();
    mesh_fidelity_ = quantum::fidelity(mesh_u_, quantum::realize(mesh_, mesh_errors_));
    mesh_has_cal_ = false;
    nn_valid_ = false;
    mesh_dirty_ = false;
  }
  ImGui::Spacing();
  if (ImGui::Button("Kalibrieren")) {
    mesh_calibrated_ = quantum::calibrate(mesh_, mesh_errors_, mesh_u_, 60);
    mesh_fidelity_cal_ = quantum::fidelity(mesh_u_, quantum::realize(mesh_calibrated_, mesh_errors_));
    mesh_has_cal_ = true;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(
        "Phasen nachstellen, bis das gefertigte Gitter dem Ziel am nächsten kommt (Fehler als gemessen "
        "angenommen).");
  ImGui::SameLine();
  if (ImGui::Button("Netz-Demo")) {
    nn_ = quantum::neural_network_demo(mesh_errors_);
    nn_valid_ = true;
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Netz-Demo: 3 Klassen, 4 Merkmale; erste Schicht |W x|^2 optisch im SVD-Gitter (zwei Clements-Gitter "
      "und Dämpfungsglieder), zweite Schicht elektronisch.");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("mesh_bild", ImVec2(0.0f, 0.0f));
  ImGui::Text("%zu MZIs, Rekonstruktion: Fehler %.1e", mesh_.mzis.size(), mesh_reconstruction_);
  ImGui::TextColored(mesh_fidelity_ > 0.999  ? ui_color_good
                     : mesh_fidelity_ > 0.98 ? ui_color_limited
                                             : ui_color_bad,
                     "Güte gefertigt: %.5f", mesh_fidelity_);
  if (mesh_has_cal_) {
    ImGui::SameLine();
    ImGui::TextColored(mesh_fidelity_cal_ > 0.999 ? ui_color_good : ui_color_limited, "| kalibriert: %.5f",
                       mesh_fidelity_cal_);
  }
  if (nn_valid_)
    ImGui::Text("Netz-Demo (%d Proben): ideal %.1f %%, gefertigt %.1f %%, kalibriert %.1f %%", nn_.samples,
                100.0 * nn_.accuracy_ideal, 100.0 * nn_.accuracy_fabricated, 100.0 * nn_.accuracy_calibrated);
  // Gitter zeichnen: Moden waagerecht, MZIs als Kästchen, Farbe = Teilungsverhältnis cos^2(theta)
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 area = ImGui::GetContentRegionAvail();
  const int n = mesh_.size;
  if (n >= 2 && area.x > 50.0f && area.y > 50.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Spalte je MZI: frühestmögliche Spalte, in der die beiden Moden frei sind
    std::vector<int> free(static_cast<std::size_t>(n), 0), column;
    int columns = 0;
    for (const auto& m : mesh_.mzis) {
      const int c =
          std::max(free[static_cast<std::size_t>(m.mode)], free[static_cast<std::size_t>(m.mode + 1)]);
      column.push_back(c);
      free[static_cast<std::size_t>(m.mode)] = free[static_cast<std::size_t>(m.mode + 1)] = c + 1;
      columns = std::max(columns, c + 1);
    }
    const float dy = std::min(area.y / static_cast<float>(n + 1), 40.0f);
    const float dx = (area.x - 40.0f) / static_cast<float>(columns + 1);
    for (int k = 0; k < n; ++k) {
      const float y = origin.y + dy * (static_cast<float>(k) + 1.0f);
      dl->AddLine({origin.x + 10.0f, y}, {origin.x + area.x - 10.0f, y}, IM_COL32(120, 130, 150, 160), 2.0f);
    }
    const auto& shown = mesh_has_cal_ ? mesh_calibrated_ : mesh_;
    for (std::size_t i = 0; i < shown.mzis.size(); ++i) {
      const auto& m = shown.mzis[i];
      const float x = origin.x + 20.0f + dx * (static_cast<float>(column[i]) + 0.5f);
      const float y0 = origin.y + dy * (static_cast<float>(m.mode) + 1.0f), y1 = y0 + dy;
      const float t = static_cast<float>(std::pow(std::cos(m.theta), 2));
      const ImU32 fill =
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f + 0.7f * t, 0.35f, 0.9f - 0.6f * t, 0.85f));
      dl->AddRectFilled({x - dx * 0.3f, y0 - 4.0f}, {x + dx * 0.3f, y1 + 4.0f}, fill, 4.0f);
    }
    ImGui::Dummy(area);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Farbe = Teilungsverhältnis cos^2(theta): rot = gerade durch, blau = über Kreuz");
  }
  ImGui::EndChild();
}

void QuantumView::draw_circuit() {
  const io::Project& project = designer_.project();
  const std::uint64_t rev = std::hash<std::string>{}(io::project_to_json(project));
  if (rev != transfer_revision_) {
    transfer_revision_ = rev;
    transfer_error_.clear();
    try {
      transfer_ = quantum::circuit_transfer(project.circuit, active_registry(), project.wavelength);
    } catch (const std::exception& e) {
      transfer_ = {};
      transfer_error_ = e.what();
    }
  }
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 52.0f);
  ImGui::TextDisabled(
      "Die Schaltung des Chip-Designers als Interferometer: Laser sind Eingänge, Detektoren Ausgänge. Je ein "
      "Photon in "
      "die ersten beiden Laser-Eingänge.");
  ImGui::PopTextWrapPos();
  if (!transfer_error_.empty()) {
    ImGui::TextColored(ui_color_limited, "%s", transfer_error_.c_str());
    return;
  }
  const auto& u = transfer_.u;
  if (ImGui::BeginTable("u", static_cast<int>(u.cols()) + 1,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("|U|, Phase");
    for (const auto& in : transfer_.inputs) ImGui::TableSetupColumn(in.c_str());
    ImGui::TableHeadersRow();
    for (int i = 0; i < u.rows(); ++i) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(transfer_.outputs[static_cast<std::size_t>(i)].c_str());
      for (int j = 0; j < u.cols(); ++j) {
        ImGui::TableNextColumn();
        ImGui::Text("%.3f, %+.2f", std::abs(u(i, j)), std::arg(u(i, j)));
      }
    }
    ImGui::EndTable();
  }
  const double loss = 1.0 - (u.adjoint() * u).trace().real() / static_cast<double>(u.cols());
  ImGui::Text("mittlerer Verlust je Photon: %.3f", loss);
  if (u.cols() >= 2 && u.rows() >= 1) {
    quantum::Pattern in(static_cast<std::size_t>(u.cols()), 0);
    in[0] = in[1] = 1;
    ImGui::SeparatorText("Zwei Photonen (ununterscheidbar)");
    for (const auto& [p, prob] : quantum::distribution(u, in)) {
      std::string text;
      for (std::size_t k = 0; k < p.size(); ++k)
        text += transfer_.outputs[k] + "=" + std::to_string(p[k]) + "  ";
      ImGui::Text("%s %.4f", text.c_str(), prob);
    }
  }
}

}  // namespace photonlab::gui
