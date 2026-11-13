#include "photonlab/modes/cross_section.hpp"

#include <algorithm>
#include <cmath>

#include "photonlab/materials/material.hpp"

namespace photonlab::modes {
namespace {

// Länge der Überdeckung der Intervalle [a0, a1] und [b0, b1]
double overlap(double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); }

}  // namespace

void validate(const CrossSection& section) {
  for (const std::string* name : {&section.core, &section.lower, &section.upper})
    if (!materials::find(*name)) throw ModeError("Unbekanntes Material '" + *name + "'");
  if (!(section.width > 0.0) || !(section.height > 0.0)) throw ModeError("Breite und Höhe des Kerns müssen positiv sein");
  if (!(section.slab_height >= 0.0) || section.slab_height >= section.height)
    throw ModeError("Die Rippenschicht muss dünner sein als der Kern");
  if (!(section.cell_size > 0.0) || !(section.margin_x > 0.0) || !(section.margin_y > 0.0))
    throw ModeError("Zellgröße und Mantelbreiten müssen positiv sein");
  if (!(section.wavelength > 0.0)) throw ModeError("Die Wellenlänge muss positiv sein");
  if (section.modes < 1 || section.modes > 20) throw ModeError("Zwischen 1 und 20 Moden sind möglich");
  const double cells = (section.width + 2 * section.margin_x) / section.cell_size *
                       ((section.height + 2 * section.margin_y) / section.cell_size);
  if (cells > 400000.0) throw ModeError("Das Gitter hätte mehr als 400.000 Zellen: Zellgröße oder Mantel anpassen");
  if (section.width / section.cell_size < 4 || section.height / section.cell_size < 4)
    throw ModeError("Der Kern braucht mindestens 4 Zellen in Breite und Höhe");
}

FdeGrid build_grid(const CrossSection& section, double wavelength, std::vector<double>* core_fraction) {
  validate(section);
  const double h = section.cell_size;
  FdeGrid grid;
  grid.dx = grid.dy = h;
  grid.nx = static_cast<int>(std::ceil((section.width + 2 * section.margin_x) / h));
  grid.ny = static_cast<int>(std::ceil((section.height + 2 * section.margin_y) / h));
  const double total_x = grid.nx * h, total_y = grid.ny * h;

  // y zeigt nach unten (wie im Bild): oben die Deckschicht, unten das Substrat
  const double core_x0 = 0.5 * (total_x - section.width), core_x1 = core_x0 + section.width;
  const double core_y0 = 0.5 * (total_y - section.height), core_y1 = core_y0 + section.height;
  const double slab_y0 = core_y1 - section.slab_height;  // Rippenschicht reicht über die ganze Breite

  const double eps_core = std::pow(materials::get(section.core).index(wavelength), 2);
  const double eps_lower = std::pow(materials::get(section.lower).index(wavelength), 2);
  const double eps_upper = std::pow(materials::get(section.upper).index(wavelength), 2);

  grid.index.resize(static_cast<std::size_t>(grid.nx) * static_cast<std::size_t>(grid.ny));
  if (core_fraction) core_fraction->assign(grid.index.size(), 0.0);
  for (int j = 0; j < grid.ny; ++j)
    for (int i = 0; i < grid.nx; ++i) {
      const double x0 = i * h, x1 = x0 + h, y0 = j * h, y1 = y0 + h;
      // Flächenanteile: Kernstreifen, Rippenschicht außerhalb des Streifens, Substrat, Rest = Deckschicht
      const double strip = overlap(x0, x1, core_x0, core_x1) * overlap(y0, y1, core_y0, core_y1);
      const double slab = section.slab_height > 0.0
                              ? h * overlap(y0, y1, slab_y0, core_y1) -
                                    overlap(x0, x1, core_x0, core_x1) * overlap(y0, y1, slab_y0, core_y1)
                              : 0.0;
      const double below = h * overlap(y0, y1, core_y1, total_y);
      const double core_part = (strip + slab) / (h * h), lower_part = below / (h * h);
      const double upper_part = std::max(0.0, 1.0 - core_part - lower_part);
      const std::size_t k = static_cast<std::size_t>(i) + static_cast<std::size_t>(grid.nx) * static_cast<std::size_t>(j);
      grid.index[k] = std::sqrt(core_part * eps_core + lower_part * eps_lower + upper_part * eps_upper);
      if (core_fraction) (*core_fraction)[k] = core_part;
    }
  return grid;
}

CrossSectionResult analyze(const CrossSection& section, bool with_group_index, JobControl* control) {
  CrossSectionResult result;
  auto progress = [&](float value) {
    if (control) control->progress = value;
  };
  auto cancelled = [&] { return control && control->cancel_requested; };
  progress(0.0f);

  FdeSettings settings;
  settings.wavelength = section.wavelength;
  settings.polarization = section.polarization;
  settings.modes = section.modes;
  result.grid = build_grid(section, section.wavelength, &result.core_fraction);
  const std::vector<FdeMode> modes = solve_modes(result.grid, settings);
  progress(with_group_index ? 0.34f : 1.0f);
  if (cancelled()) return {};

  // Gruppenindex aus zwei Nachbarwellenlängen; die Moden werden über ihre Reihenfolge zugeordnet
  std::vector<FdeMode> shorter, longer;
  const double step = 2e-3 * section.wavelength;
  if (with_group_index) {
    settings.wavelength = section.wavelength - step;
    shorter = solve_modes(build_grid(section, settings.wavelength), settings);
    progress(0.67f);
    if (cancelled()) return {};
    settings.wavelength = section.wavelength + step;
    longer = solve_modes(build_grid(section, settings.wavelength), settings);
    progress(1.0f);
    if (cancelled()) return {};
  }

  const double cell_area = result.grid.dx * result.grid.dy;
  for (std::size_t k = 0; k < modes.size(); ++k) {
    CrossSectionMode mode;
    mode.n_eff = modes[k].n_eff;
    mode.guided = modes[k].guided;
    mode.field = modes[k].field;
    double sum2 = 0.0, sum4 = 0.0, core2 = 0.0;
    for (std::size_t c = 0; c < mode.field.size(); ++c) {
      const double e2 = mode.field[c] * mode.field[c];
      sum2 += e2;
      sum4 += e2 * e2;
      core2 += e2 * result.core_fraction[c];
    }
    mode.confinement = sum2 > 0.0 ? core2 / sum2 : 0.0;
    mode.effective_area = sum4 > 0.0 ? sum2 * sum2 / sum4 * cell_area : 0.0;
    if (with_group_index && k < shorter.size() && k < longer.size())
      mode.n_group = mode.n_eff - section.wavelength * (longer[k].n_eff - shorter[k].n_eff) / (2.0 * step);
    result.modes.push_back(std::move(mode));
  }
  return result;
}

}  // namespace photonlab::modes
