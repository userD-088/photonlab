#include "band_view.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>

#include "implot.h"
#include "photonlab/core/units.hpp"
#include "photonlab/field/photonic_crystal.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "ui_util.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

pwe::UnitCell make_cell(double radius_rel, double eps_bg, double eps_in, bool square) {
  return square ? pwe::square_cell(radius_rel, eps_bg, eps_in)
                : pwe::triangular_cell(radius_rel, eps_bg, eps_in);
}

bool drag_double(const char* label, double* value, double low, double high, float speed, const char* format,
                 float width_em = 7.0f) {
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * width_em);
  return ImGui::DragScalar(label, ImGuiDataType_Double, value, speed, &low, &high, format,
                           ImGuiSliderFlags_AlwaysClamp);
}

const char* polarization_name(pwe::Polarization p) {
  return p == pwe::Polarization::ez ? "ez (E senkrecht, TM)" : "hz (H senkrecht, TE)";
}

}  // namespace

BandView::BandView(ThreadPool& pool, ChipDesigner& designer) : pool_(pool), designer_(designer) {}

BandView::~BandView() {
  if (control_) control_->cancel_requested = true;
  if (future_.valid()) future_.wait();
}

// Voreinstellungen: 0 = Si-Platte mit Oxidlöchern wie die Beispiel-Szenen, 1 = Luftlöcher in eps 13
// (Joannopoulos), 2 = Stäbe in Luft (eps 8,9, Quadratgitter, Joannopoulos)
void BandView::apply_preset(int preset) {
  if (preset == 0) {
    const field::FieldScene ref = field::reference_scene("field_phc_w1");
    settings_ = Settings{};
    settings_.eps_background = ref.shapes.front().index * ref.shapes.front().index;  // Platte
    settings_.eps_inclusion = ref.background_index * ref.background_index;           // Loch = Oxid
    settings_.radius_rel = 0.35;
    settings_.polarization = 1;
    a_nm_ = 430.0;
  } else if (preset == 1) {
    settings_ = Settings{};
    settings_.eps_background = 13.0, settings_.eps_inclusion = 1.0, settings_.radius_rel = 0.48;
    settings_.cutoff = 7.0;
    a_nm_ = 0.0;
  } else {
    settings_ = Settings{};
    settings_.lattice = 1, settings_.eps_background = 1.0, settings_.eps_inclusion = 8.9;
    settings_.radius_rel = 0.2, settings_.polarization = 0;
    a_nm_ = 0.0;
  }
}

void BandView::start() {
  if (future_.valid()) return;
  control_ = std::make_shared<JobControl>();
  future_ = pool_.submit([settings = settings_, control = control_, pool = &pool_] {
    Output out;
    out.settings = settings;
    try {
      const bool square = settings.lattice == 1;
      for (const pwe::Polarization pol : {pwe::Polarization::ez, pwe::Polarization::hz}) {
        if (settings.polarization == 0 && pol != pwe::Polarization::ez) continue;
        if (settings.polarization == 1 && pol != pwe::Polarization::hz) continue;
        Curve curve;
        curve.polarization = pol;
        pwe::BandSettings bs;
        bs.polarization = pol;
        bs.cutoff = settings.cutoff;
        const pwe::UnitCell bulk =
            make_cell(settings.radius_rel, settings.eps_background, settings.eps_inclusion, square);
        const pwe::PathKind kind = square ? pwe::PathKind::square : pwe::PathKind::triangular;
        if (settings.line_defect && !square) {
          // Superzelle: Wellenvektor entlang x von Gamma bis zum Zonenrand
          const pwe::UnitCell cell = pwe::triangular_line_defect_cell(
              settings.radius_rel, settings.eps_background, settings.eps_inclusion, settings.rows);
          bs.bands = 4 * settings.rows;
          pwe::BandPath path;
          for (int i = 0; i <= 20; ++i) {
            path.points.push_back({0.5 * i / 20.0, 0.0});
            path.distance.push_back(0.5 * i / 20.0);
          }
          path.marks = {{0, "Gamma"}, {20, "X"}};
          curve.bands = pwe::solve_bands(cell, bs, path, pool, control.get());
          if (control->cancel_requested) return out;
          // Lücke des perfekten Kristalls zum Einfärben der Defektmoden
          pwe::BandSettings bulk_settings = bs;
          bulk_settings.bands = 6;
          curve.bulk_gaps =
              pwe::solve_bands(bulk, bulk_settings, pwe::standard_path(bulk, kind, 10), pool, control.get())
                  .gaps;
        } else {
          bs.bands = 8;
          curve.bands = pwe::solve_bands(bulk, bs, pwe::standard_path(bulk, kind, 14), pool, control.get());
        }
        if (control->cancel_requested) return out;
        out.curves.push_back(std::move(curve));
      }
    } catch (const std::exception& e) {
      out.error = e.what();
    }
    control->finished = true;
    return out;
  });
}

void BandView::update() {
  if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  Output out = future_.get();
  const bool cancelled = control_ && control_->cancel_requested;
  control_.reset();
  if (cancelled) return;
  output_ = std::move(out);
  has_output_ = true;
  fit_ = true;
}

// Setzt Platte und Löcher in die Feld-Szene (ersetzt frühere Kristall-Formen); Muster: 0 ohne Defekt,
// 1 gerader W1, 2 60-Grad-Knick in Szenenmitte.
bool BandView::insert_crystal(int pattern) {
  field::FieldScene& scene = designer_.project().field;
  if (scene.empty()) {
    designer_.show_status("Die Feld-Szene hat kein Rechengebiet.", true);
    return false;
  }
  const double a = a_nm_ * units::nm;
  if (!(a > 0.0)) {
    designer_.show_status("Gitterkonstante a eingeben (z. B. 430 nm).", true);
    return false;
  }
  try {
    field::CrystalLayout layout;
    layout.kind = settings_.lattice == 1 ? field::LatticeKind::square : field::LatticeKind::triangular;
    layout.a = a;
    layout.radius = settings_.radius_rel * a;
    layout.origin_x = 0.0;
    layout.origin_y = 0.5 * scene.height;
    layout.x_min = 0.0, layout.x_max = scene.width, layout.y_min = 0.0, layout.y_max = scene.height;
    const int columns = static_cast<int>(scene.width / a) + 4;
    if (pattern == 1) {
      field::remove_line(layout, {-2, 0}, 0, columns);
    } else if (pattern == 2 && layout.kind == field::LatticeKind::triangular) {
      const int bend = columns / 2;
      const int rise = static_cast<int>(0.5 * scene.height / (0.5 * std::sqrt(3.0) * a));
      field::remove_path(layout, {-2, 0}, {{0, bend + 3}, {1, rise}});
    }
    const double n_plate = std::sqrt(settings_.eps_background), n_hole = std::sqrt(settings_.eps_inclusion);
    std::vector<field::Shape> shapes;
    shapes.push_back({"Platte", field::ShapeKind::rectangle, 0.5 * scene.width, 0.5 * scene.height,
                      scene.width, scene.height, 0.0, 1e-6, 0.5e-6, n_plate});
    for (field::Shape& hole : field::crystal_shapes(layout, n_hole)) shapes.push_back(std::move(hole));
    auto generated = [](const field::Shape& s) {
      return s.name == "Platte" || s.name.rfind("loch_", 0) == 0;
    };
    scene.shapes.erase(std::remove_if(scene.shapes.begin(), scene.shapes.end(), generated),
                       scene.shapes.end());
    scene.shapes.insert(scene.shapes.begin(), shapes.begin(), shapes.end());
    designer_.mark_dirty();
    char text[160];
    std::snprintf(text, sizeof text,
                  "Kristall mit %zu Löchern in die Feld-Szene gesetzt (Quelle und Monitore selbst setzen).",
                  shapes.size() - 1);
    designer_.show_status(text);
    return true;
  } catch (const std::exception& e) {
    designer_.show_status(e.what(), true);
    return false;
  }
}

void BandView::draw_plot() {
  const bool defect = output_.settings.line_defect && output_.settings.lattice == 0;
  if (fit_) ImPlot::SetNextAxesToFit();
  if (!ImPlot::BeginPlot("##baender", ImVec2(-1.0f, ImGui::GetContentRegionAvail().y * 0.62f))) return;
  ImPlot::SetupAxes(defect ? "Wellenvektor k [2 pi/a]" : "Wellenvektor entlang des Pfades",
                    "Frequenz a/lambda");
  ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
  if (!output_.curves.empty() && !output_.curves.front().bands.path.marks.empty()) {
    const auto& path = output_.curves.front().bands.path;
    std::vector<double> positions;
    std::vector<std::string> names;
    for (const auto& [index, name] : path.marks) {
      positions.push_back(path.distance[static_cast<std::size_t>(index)]);
      names.push_back(name);
    }
    std::vector<const char*> labels;
    for (const auto& n : names) labels.push_back(n.c_str());
    ImPlot::SetupAxisTicks(ImAxis_X1, positions.data(), static_cast<int>(positions.size()), labels.data());
  }
  const ImVec4 colors[2] = {ImVec4(0.38f, 0.65f, 1.0f, 1.0f), ImVec4(1.0f, 0.62f, 0.25f, 1.0f)};
  for (const Curve& curve : output_.curves) {
    const int idx = curve.polarization == pwe::Polarization::ez ? 0 : 1;
    const auto& path = curve.bands.path;
    const int n = static_cast<int>(path.points.size());
    if (curve.bands.frequency.empty() || n == 0) continue;
    const double x0 = path.distance.front(), x1 = path.distance.back();
    // Lücken als Bänder
    const std::vector<pwe::Gap>& gaps = defect ? curve.bulk_gaps : curve.bands.gaps;
    for (const pwe::Gap& g : gaps) {
      const double xs[2] = {x0, x1}, upper[2] = {g.upper, g.upper};
      ImPlot::SetNextFillStyle(colors[idx], 0.16f);
      char label[48];
      std::snprintf(label, sizeof label, "Lücke %s %d/%d", idx == 0 ? "ez" : "hz", g.band, g.band + 1);
      ImPlot::PlotShaded(label, xs, upper, 2, g.lower);
    }
    const int bands = static_cast<int>(curve.bands.frequency.front().size());
    std::vector<double> x(static_cast<std::size_t>(n)), y(static_cast<std::size_t>(n)),
        y_in(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = path.distance[static_cast<std::size_t>(i)];
    // Bänder, die in der Lücke des perfekten Kristalls liegen (Defektmoden), hervorgehoben
    const pwe::Gap* gap = nullptr;
    if (defect && !curve.bulk_gaps.empty())
      gap = &*std::max_element(curve.bulk_gaps.begin(), curve.bulk_gaps.end(),
                               [](const pwe::Gap& p, const pwe::Gap& q) { return p.ratio() < q.ratio(); });
    for (int b = 0; b < bands; ++b) {
      for (int i = 0; i < n; ++i) {
        const double f = curve.bands.frequency[static_cast<std::size_t>(i)][static_cast<std::size_t>(b)];
        y[static_cast<std::size_t>(i)] = f;
        y_in[static_cast<std::size_t>(i)] =
            (gap && f > gap->lower && f < gap->upper) ? f : std::numeric_limits<double>::quiet_NaN();
      }
      ImPlot::SetNextLineStyle(colors[idx], defect ? 0.6f : 1.5f);
      ImPlot::PlotLine(b == 0 ? (idx == 0 ? "ez" : "hz") : (idx == 0 ? "##ez" : "##hz"), x.data(), y.data(),
                       n);
      if (gap) {
        ImPlot::SetNextLineStyle(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), 2.8f);
        ImPlot::PlotLine(b == 0 ? "Defektmode" : "##defekt", x.data(), y_in.data(), n,
                         ImPlotLineFlags_SkipNaN);
      }
    }
  }
  ImPlot::EndPlot();
  fit_ = false;
}

void BandView::draw_gaps() {
  const double a = a_nm_ * units::nm;
  const bool defect = output_.settings.line_defect && output_.settings.lattice == 0;
  if (output_.curves.empty()) return;
  if (defect)
    ImGui::TextDisabled(
        "Superzelle: alle Bänder sind gefaltet; grün = Bänder in der Lücke des perfekten Kristalls "
        "(Defektmoden).");
  // Lücken je Polarisation
  for (const Curve& curve : output_.curves) {
    const std::vector<pwe::Gap>& gaps = defect ? curve.bulk_gaps : curve.bands.gaps;
    ImGui::SeparatorText(polarization_name(curve.polarization));
    if (gaps.empty()) {
      ImGui::TextDisabled("keine vollständige Lücke unter den berechneten Bändern");
      continue;
    }
    if (ImGui::BeginTable(curve.polarization == pwe::Polarization::ez ? "lez" : "lhz", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("zwischen Band");
      ImGui::TableSetupColumn("a/lambda");
      ImGui::TableSetupColumn("relative Breite");
      ImGui::TableSetupColumn(a > 0.0 ? "Wellenlänge" : "a für 1550 nm");
      ImGui::TableHeadersRow();
      for (const pwe::Gap& g : gaps) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%d und %d", g.band, g.band + 1);
        ImGui::TableNextColumn();
        ImGui::Text("%.4f ... %.4f", g.lower, g.upper);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f %%", 100.0 * g.ratio());
        ImGui::TableNextColumn();
        if (a > 0.0)
          ImGui::Text("%.0f ... %.0f nm", a / g.upper / units::nm, a / g.lower / units::nm);
        else
          ImGui::Text("%.0f nm (Mitte)", 1550.0 * g.midgap());
      }
      ImGui::EndTable();
    }
  }
  // vollständige Lücke: Überlapp der Lücken beider Polarisationen
  if (!defect && output_.curves.size() == 2) {
    double lower = 0.0, upper = 0.0;
    for (const pwe::Gap& g1 : output_.curves[0].bands.gaps)
      for (const pwe::Gap& g2 : output_.curves[1].bands.gaps) {
        const double lo = std::max(g1.lower, g2.lower), hi = std::min(g1.upper, g2.upper);
        if (hi - lo > upper - lower) lower = lo, upper = hi;
      }
    ImGui::Spacing();
    if (upper > lower)
      ImGui::TextColored(ui_color_good,
                         "Vollständige Lücke (beide Polarisationen): a/lambda %.4f ... %.4f (%.1f %%)", lower,
                         upper, 100.0 * (upper - lower) / (0.5 * (upper + lower)));
    else
      ImGui::TextDisabled("Die Lücken beider Polarisationen überlappen nicht: keine vollständige Lücke.");
  }
}

bool BandView::draw() {
  update();
  if (!presets_ready_) {
    presets_ready_ = true;
    apply_preset(0);
  }
  bool scene_modified = false;
  ImGui::BeginChild("einstellungen", ImVec2(ImGui::GetFontSize() * 21.0f, 0.0f), ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Voreinstellung");
  if (ImGui::Button("Si-Platte, Oxidlöcher")) apply_preset(0);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Brechzahlen wie in den Kristall-Beispielen der Feld-Ebene (n = 2,85 / 1,444).");
  if (ImGui::Button("Luftlöcher eps 13")) apply_preset(1);
  ImGui::SameLine();
  if (ImGui::Button("Stäbe eps 8,9")) apply_preset(2);
  ImGui::SeparatorText("Kristall");
  const char* lattices[] = {"Dreiecksgitter", "Quadratgitter"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
  ImGui::Combo("##gitter", &settings_.lattice, lattices, 2);
  drag_double("r / a", &settings_.radius_rel, 0.05, 0.49, 0.002f, "%.3f");
  drag_double("eps Hintergrund", &settings_.eps_background, 1.0, 30.0, 0.02f, "%.3f");
  drag_double("eps Loch/Stab", &settings_.eps_inclusion, 1.0, 30.0, 0.02f, "%.3f");
  ImGui::SeparatorText("Rechnung");
  const char* pols[] = {"ez (E senkrecht, TM)", "hz (H senkrecht, TE)", "beide"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
  ImGui::Combo("##pol", &settings_.polarization, pols, 3);
  drag_double("Abschneidegrenze", &settings_.cutoff, 3.0, 12.0, 0.05f, "%.1f", 6.0f);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Größte ebene Welle |G| in 2 pi/a. Mehr Wellen = genauer, aber langsamer.");
  if (settings_.lattice == 0) {
    if (ImGui::Checkbox("Liniendefekt (W1-Superzelle)", &settings_.line_defect) && settings_.line_defect)
      settings_.cutoff = std::min(settings_.cutoff, 4.5);  // die Superzelle hat viele ebene Wellen
    if (settings_.line_defect) {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
      ImGui::DragInt("Lochreihen", &settings_.rows, 0.1f, 4, 14, "%d", ImGuiSliderFlags_AlwaysClamp);
      settings_.rows += settings_.rows % 2;  // gerade Zahl
      if (settings_.cutoff > 5.0)
        ImGui::TextColored(ui_color_limited, "Superzelle: Abschneidegrenze 4-5 genügt.");
    }
  }
  ImGui::SeparatorText("Gitterkonstante");
  drag_double("a [nm]", &a_nm_, 0.0, 3000.0, 1.0f, "%.0f");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("0 = ohne Wellenlängen. Die Tabelle zeigt sonst die Lücke in nm.");

  ImGui::SeparatorText("In die Feld-Szene einsetzen");
  const char* patterns[] = {"ohne Defekt", "gerader W1 (eine Reihe fehlt)", "60-Grad-Knick im W1"};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  ImGui::Combo("##muster", &defect_pattern_, patterns, 3);
  if (ImGui::Button("Kristall einsetzen")) scene_modified = insert_crystal(defect_pattern_);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(
        "Ersetzt Platte und Löcher der Feld-Szene durch den Kristall (Gitterkonstante a, r/a, eps wie hier). "
        "Quelle und Monitore setzt du im Reiter Feld selbst. Kristall-Beispiele: Datei > Beispiele.");
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Ebene-Wellen-Entwicklung für unendlich hohe 2D-Kristalle: keine Abstrahlung nach oben und unten und "
      "keine Materialdispersion. Die Brechzahl der Platte ist eine effektive Brechzahl.");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  const bool stale = !has_output_ || !(output_.settings == settings_);
  if (stale && !future_.valid()) start();  // läuft die Rechnung noch, folgt der nächste Lauf danach

  ImGui::SameLine();
  ImGui::BeginChild("ergebnis", ImVec2(0.0f, 0.0f));
  if (future_.valid()) {
    ImGui::ProgressBar(control_->progress.load(), ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("rechnet ...");
  }
  if (!has_output_) {
    ImGui::TextDisabled("Wird gerechnet ...");
  } else if (!output_.error.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui_color_bad, "%s", output_.error.c_str());
    ImGui::PopTextWrapPos();
  } else {
    draw_plot();
    draw_gaps();
  }
  ImGui::EndChild();
  return scene_modified;
}

}  // namespace photonlab::gui
