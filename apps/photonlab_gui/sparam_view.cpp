#include "sparam_view.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>

#include "file_dialog.hpp"
#include "implot.h"
#include "photonlab/core/units.hpp"
#include "photonlab/io/project.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

// Name für die Bibliothek aus dem Szenennamen: Kleinbuchstaben, Ziffern und Unterstriche
std::string suggested_name(const std::string& scene_name) {
  std::string name;
  for (unsigned char c : scene_name) {
    if (std::isalnum(c))
      name += static_cast<char>(std::tolower(c));
    else if (!name.empty() && name.back() != '_')
      name += '_';
  }
  while (!name.empty() && name.back() == '_') name.pop_back();
  // kurz halten: das erste Wort, falls der ganze Name lang ist ("richtkoppler_mit_s_kurven" ->
  // "richtkoppler")
  if (name.size() > 16 && name.find('_') != std::string::npos) name.resize(name.find('_'));
  return name.empty() ? "bauteil" : name;
}

int instances_using(const io::Project& project, const std::string& table_name) {
  const std::string type = circuit::sparameter_type_name(table_name);
  return static_cast<int>(std::count_if(project.circuit.instances.begin(), project.circuit.instances.end(),
                                        [&](const circuit::Instance& i) { return i.type == type; }));
}

}  // namespace

SParamView::SParamView(ThreadPool& pool, ChipDesigner& designer) : pool_(pool), designer_(designer) {
  designer_.on_save_sparam = [this](const std::string& path) {
    if (!has_table_) return;
    try {
      io::save_sparameters(table_, table_from_extraction_ ? &table_scene_ : nullptr, ui_path_from_utf8(path));
      designer_.show_status("S-Parameter gespeichert: " + path);
    } catch (const std::exception& e) {
      designer_.show_status(e.what(), true);
    }
  };
  designer_.on_open_sparam = [this](const std::string& path) {
    try {
      add_to_library(io::load_sparameters(ui_path_from_utf8(path)));
    } catch (const std::exception& e) {
      designer_.show_status(e.what(), true);
    }
  };
}

SParamView::~SParamView() {
  designer_.on_save_sparam = nullptr;
  designer_.on_open_sparam = nullptr;
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
}

void SParamView::start() {
  if (future_.valid()) return;
  error_.clear();
  control_ = std::make_shared<JobControl>();
  // Der Job besitzt eine Kopie der Szene und greift nicht auf die Oberfläche zu
  future_ = pool_.submit([scene = designer_.project().field, control = control_, pool = &pool_] {
    Job job;
    job.scene = scene;
    try {
      const field::SParameterResult result = field::extract_sparameters(scene, pool, control.get());
      job.cancelled = result.cancelled;
      if (!result.cancelled) {
        job.table = io::make_sparameter_table(scene, result, suggested_name(scene.name));
        job.warnings = result.warnings;
      }
    } catch (const std::exception& e) {
      job.error = e.what();
    }
    control->finished = true;
    return job;
  });
}

void SParamView::update() {
  if (designer_.project_generation() != seen_generation_) {
    // anderes Projekt: alte Anzeige verwerfen
    seen_generation_ = designer_.project_generation();
    has_table_ = false;
    warnings_.clear();
    error_.clear();
    std::snprintf(name_buffer_, sizeof name_buffer_, "%s",
                  suggested_name(designer_.project().field.name).c_str());
  }
  if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  Job job = future_.get();
  control_.reset();
  if (job.cancelled) return;
  if (!job.error.empty()) {
    error_ = job.error;
    return;
  }
  warnings_ = job.warnings;
  show(job.table, &job.scene);
  table_from_extraction_ = true;
}

void SParamView::show(const circuit::SParameterTable& table, const field::FieldScene* scene) {
  table_ = table;
  has_table_ = true;
  table_from_extraction_ = scene != nullptr;
  if (scene) table_scene_ = *scene;
  input_port_ = 0;
  fit_plot_ = true;
  std::snprintf(name_buffer_, sizeof name_buffer_, "%s", table.name.c_str());
}

void SParamView::add_to_library(circuit::SParameterTable table) {
  io::Project& project = designer_.project();
  // gleicher Name: ersetzen (die Bauteile in der Schaltung übernehmen die neuen Werte)
  auto existing = std::find_if(project.sparameters.begin(), project.sparameters.end(),
                               [&](const circuit::SParameterTable& t) { return t.name == table.name; });
  const bool replaced = existing != project.sparameters.end();
  if (existing != project.sparameters.end() && existing->ports != table.ports &&
      instances_using(project, table.name) > 0) {
    designer_.show_status(
        "'" + table.name +
            "' wird in der Schaltung benutzt und hat andere Ports; bitte anderen Namen wählen.",
        true);
    return;
  }
  try {
    circuit::validate(table);
  } catch (const std::exception& e) {
    designer_.show_status(e.what(), true);
    return;
  }
  const std::string name = table.name;
  if (replaced)
    *existing = std::move(table);
  else
    project.sparameters.push_back(std::move(table));
  designer_.library_changed();
  designer_.mark_dirty();
  designer_.show_status(
      std::string(replaced ? "Bauteil ersetzt: " : "Bauteil in die Bibliothek übernommen: ") +
      "sparam:" + name + " (Chip-Designer, Bauteilbibliothek)");
}

void SParamView::draw() {
  update();
  io::Project& project = designer_.project();
  field::FieldScene& scene = project.field;

  ImGui::BeginChild("einstellungen", ImVec2(ImGui::GetFontSize() * 21.0f, 0.0f), ImGuiChildFlags_Borders);
  if (scene.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "Keine Feld-Szene im Projekt. Im Reiter 'Feld' eine Szene anlegen oder ein Beispiel laden "
        "(z. B. 'Feld: Richtkoppler mit S-Kurven').");
    ImGui::PopTextWrapPos();
  } else {
    draw_settings(scene);
  }
  ImGui::Spacing();
  draw_library();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("ergebnis", ImVec2(0.0f, 0.0f));
  draw_result();
  ImGui::EndChild();
}

void SParamView::draw_settings(field::FieldScene& scene) {
  ImGui::SeparatorText("Extraktion");
  ImGui::TextWrapped("Szene: %s", scene.name.c_str());
  if (scene.ports.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_limited,
                       "Die Szene hat keine Ports. Im Reiter 'Feld' mit '+ Port' Linien quer über die "
                       "Wellenleiter an den Rändern des Bauteils legen.");
    ImGui::PopTextWrapPos();
    return;
  }
  ImGui::Text("%zu Ports:", scene.ports.size());
  for (const auto& p : scene.ports) {
    ImGui::SameLine();
    ImGui::TextUnformatted(p.name.c_str());
  }
  auto& settings = scene.sparameters;
  double start_nm = settings.wavelength_start / units::nm, stop_nm = settings.wavelength_stop / units::nm;
  const double lo = 200.0, hi = 20000.0;
  bool changed = false;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  changed |= ImGui::DragScalar("von", ImGuiDataType_Double, &start_nm, 1.0f, &lo, &hi, "%.0f nm",
                               ImGuiSliderFlags_AlwaysClamp);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  changed |= ImGui::DragScalar("bis", ImGuiDataType_Double, &stop_nm, 1.0f, &lo, &hi, "%.0f nm",
                               ImGuiSliderFlags_AlwaysClamp);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  changed |=
      ImGui::DragInt("Wellenlängen", &settings.points, 0.5f, 2, 1000, "%d", ImGuiSliderFlags_AlwaysClamp);
  double max_fs = settings.max_time / units::fs;
  const double t_lo = 50.0, t_hi = 1e6;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  if (ImGui::DragScalar("Höchstdauer", ImGuiDataType_Double, &max_fs, 10.0f, &t_lo, &t_hi, "%.0f fs",
                        ImGuiSliderFlags_AlwaysClamp)) {
    settings.max_time = max_fs * units::fs;
    changed = true;
  }
  if (changed) {
    settings.wavelength_start = start_nm * units::nm;
    settings.wavelength_stop = stop_nm * units::nm;
    designer_.mark_dirty();
  }

  // Cache: gleiche Geometrie und Einstellungen liegen schon in der Bibliothek?
  const std::string hash = io::sparameter_hash(scene);
  const circuit::SParameterTable* cached = nullptr;
  for (const auto& t : designer_.project().sparameters)
    if (t.source_hash == hash) cached = &t;

  if (future_.valid()) {
    if (ImGui::Button("Abbrechen")) control_->cancel_requested = true;
    ImGui::SameLine();
    ImGui::ProgressBar(control_->progress.load(), ImVec2(-1.0f, 0.0f));
    ImGui::TextDisabled("Je Port eine Rechnung, parallel im Threadpool.");
  } else {
    if (cached) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(ui_color_good,
                         "Diese Szene liegt mit gleichen Einstellungen schon als 'sparam:%s' in der "
                         "Bibliothek (Cache).",
                         cached->name.c_str());
      ImGui::PopTextWrapPos();
      if (ImGui::Button("Anzeigen")) show(*cached, nullptr);
      ImGui::SameLine();
    }
    if (ImGui::Button(cached ? "Trotzdem neu rechnen" : "S-Parameter extrahieren")) start();
  }
  if (!error_.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_bad, "%s", error_.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Je Port ein breitbandiger Modenpuls; an allen Ports wird die ein- und auslaufende Mode "
      "gemessen. 2D-Rechnung: Abstrahlung nach oben und unten fehlt, Brechzahlen ohne Dispersion.");
  ImGui::PopTextWrapPos();
}

void SParamView::draw_library() {
  io::Project& project = designer_.project();
  ImGui::SeparatorText("Bauteilbibliothek");
  if (project.sparameters.empty()) ImGui::TextDisabled("Noch keine Bauteile aus S-Parametern.");
  int remove = -1;
  for (int k = 0; k < static_cast<int>(project.sparameters.size()); ++k) {
    const auto& t = project.sparameters[static_cast<std::size_t>(k)];
    ImGui::PushID(k);
    const int used = instances_using(project, t.name);
    ImGui::Text("sparam:%s", t.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu Ports, %.0f-%.0f nm%s", t.ports.size(), t.wavelengths.front() / units::nm,
                        t.wavelengths.back() / units::nm, used > 0 ? ", benutzt" : "");
    if (ImGui::SmallButton("Anzeigen")) show(t, nullptr);
    ImGui::SameLine();
    ImGui::BeginDisabled(used > 0);
    if (ImGui::SmallButton("Entfernen")) remove = k;
    ImGui::EndDisabled();
    if (used > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Wird von %d Bauteil(en) der Schaltung benutzt.", used);
    ImGui::PopID();
  }
  if (remove >= 0) {
    project.sparameters.erase(project.sparameters.begin() + remove);
    designer_.library_changed();
    designer_.mark_dirty();
  }
  if (ImGui::SmallButton("Aus Datei laden (.sparam.json)"))
    show_file_dialog(DialogKind::open_sparam, designer_.window());
}

void SParamView::draw_result() {
  if (!has_table_) {
    ImGui::TextDisabled("Noch kein Ergebnis.");
    return;
  }
  const circuit::SParameterTable& t = table_;
  const auto n = static_cast<int>(t.ports.size());
  ImGui::Text("sparam:%s", t.name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("%s", t.description.c_str());
  for (const auto& w : warnings_) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_limited, "%s", w.c_str());
    ImGui::PopTextWrapPos();
  }

  // Reziprozität und Passivität: gemessen, nicht angenommen
  double asymmetry = 0.0, worst_column = 0.0;
  for (const auto& m : t.s) {
    asymmetry = std::max(asymmetry, (m - m.transpose()).cwiseAbs().maxCoeff());
    for (int j = 0; j < n; ++j) worst_column = std::max(worst_column, m.col(j).squaredNorm());
  }
  ImGui::TextColored(asymmetry < 1e-2 ? ui_color_good : ui_color_limited,
                     "Reziprozität: max |S_ij - S_ji| = %.1e", asymmetry);
  ImGui::SameLine();
  ImGui::TextColored(worst_column <= 1.001 ? ui_color_good : ui_color_bad,
                     "  Passivität: größte Spaltensumme %.4f%s", worst_column,
                     worst_column < 0.999 ? " (Rest wird abgestrahlt)" : "");

  // Übernahme
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  ImGui::InputText("Name", name_buffer_, sizeof name_buffer_);
  ImGui::SameLine();
  if (ImGui::Button("In die Bauteilbibliothek übernehmen")) {
    circuit::SParameterTable copy = t;
    copy.name = name_buffer_;
    copy.label = name_buffer_;
    add_to_library(std::move(copy));
  }
  ImGui::SameLine();
  if (ImGui::Button("Als .sparam.json speichern"))
    show_file_dialog(DialogKind::save_sparam, designer_.window());

  // Plot: alle Ausgänge bei Anregung an einem Port
  input_port_ = std::clamp(input_port_, 0, n - 1);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  if (ImGui::BeginCombo("Anregung an", t.ports[static_cast<std::size_t>(input_port_)].c_str())) {
    for (int j = 0; j < n; ++j)
      if (ImGui::Selectable(t.ports[static_cast<std::size_t>(j)].c_str(), j == input_port_)) {
        input_port_ = j;
        fit_plot_ = true;
      }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("dB", &show_db_)) fit_plot_ = true;
  ImGui::SameLine();
  ImGui::Checkbox("Phase", &show_phase_);

  std::vector<double> x(t.wavelengths.size());
  std::transform(t.wavelengths.begin(), t.wavelengths.end(), x.begin(),
                 [](double w) { return w / units::nm; });
  const float height = show_phase_ ? ImGui::GetContentRegionAvail().y * 0.6f : -1.0f;
  if (fit_plot_) ImPlot::SetNextAxesToFit();
  if (ImPlot::BeginPlot("##betrag", ImVec2(-1.0f, height))) {
    ImPlot::SetupAxes("Wellenlänge [nm]", show_db_ ? "|S|² [dB]" : "|S|²");
    ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
    for (int i = 0; i < n; ++i) {
      std::vector<double> y(t.s.size());
      for (std::size_t k = 0; k < t.s.size(); ++k) {
        const double p = std::norm(t.s[k](i, input_port_));
        y[k] = show_db_ ? 10.0 * std::log10(std::max(p, 1e-12)) : p;
      }
      const std::string label = "S(" + t.ports[static_cast<std::size_t>(i)] + ", " +
                                t.ports[static_cast<std::size_t>(input_port_)] + ")";
      ImPlot::PlotLine(label.c_str(), x.data(), y.data(), static_cast<int>(y.size()));
    }
    ImPlot::EndPlot();
  }
  fit_plot_ = false;
  if (show_phase_ && ImPlot::BeginPlot("##phase", ImVec2(-1.0f, -1.0f))) {
    ImPlot::SetupAxes("Wellenlänge [nm]", "Phase [rad]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
    for (int i = 0; i < n; ++i) {
      std::vector<double> y(t.s.size());
      for (std::size_t k = 0; k < t.s.size(); ++k) y[k] = std::arg(t.s[k](i, input_port_));
      const std::string label = "arg S(" + t.ports[static_cast<std::size_t>(i)] + ", " +
                                t.ports[static_cast<std::size_t>(input_port_)] + ")";
      ImPlot::PlotLine(label.c_str(), x.data(), y.data(), static_cast<int>(y.size()));
    }
    ImPlot::EndPlot();
  }
}

}  // namespace photonlab::gui
