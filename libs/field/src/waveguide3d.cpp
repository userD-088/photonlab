#include "photonlab/field/waveguide3d.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "photonlab/core/complex.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd3d.hpp"
#include "photonlab/materials/material.hpp"
#include "photonlab/modes/fde.hpp"
#include "photonlab/modes/slab.hpp"

namespace photonlab::field {
namespace {

int whole_cells(double length, double dx, const char* what) {
  const double n = length / dx;
  const long rounded = std::lround(n);
  if (std::abs(n - static_cast<double>(rounded)) > 1e-6 || rounded < 4)
    throw FieldError(std::string("3D-Wellenleiter: ") + what +
                     " muss ein ganzzahliges Vielfaches der Zelle sein (mindestens 4 Zellen)");
  return static_cast<int>(rounded);
}

}  // namespace

Waveguide3dResult run_waveguide_3d(const Waveguide3dSettings& settings, JobControl* control) {
  const double dx = settings.cell_size;
  if (!(dx > 0.0)) throw FieldError("3D-Wellenleiter: Zellgröße muss positiv sein");
  const int core_y = settings.slab ? 0 : whole_cells(settings.width, dx, "Breite");
  const int core_z = whole_cells(settings.height, dx, "Höhe");
  const int margin = settings.pml_cells + std::max(4, static_cast<int>(std::lround(settings.clearance / dx)));
  const int nx = std::max(static_cast<int>(std::lround(settings.length / dx)), 2 * settings.pml_cells + 40);
  const int ny = settings.slab ? 2 * settings.pml_cells + 8 : core_y + 2 * margin, nz = core_z + 2 * margin;
  const double n_core = materials::get(settings.core).index(settings.wavelength);
  const double n_clad = materials::get(settings.cladding).index(settings.wavelength);

  Scene3d scene;
  scene.name = "Streifenwellenleiter";
  scene.size_x = nx * dx, scene.size_y = ny * dx, scene.size_z = nz * dx;
  scene.cell_size = dx;
  scene.background_index = n_clad;
  scene.pml_cells = settings.pml_cells;
  const double y0 = margin * dx, z0 = margin * dx;
  if (settings.slab)
    scene.blocks.push_back(
        {"Schicht", -dx, scene.size_x + dx, -dx, scene.size_y + dx, z0, z0 + core_z * dx, n_core});
  else
    scene.blocks.push_back(
        {"Kern", -dx, scene.size_x + dx, y0, y0 + core_y * dx, z0, z0 + core_z * dx, n_core});
  Fdtd3d sim(scene);
  sim.set_threads(settings.threads);

  // Grundmode des Querschnitts (FDE-Zellen mit Mittelpunkten ((p+1/2)dx, (q+1/2)dx) in y und z)
  modes::FdeGrid grid;
  grid.nx = ny, grid.ny = nz, grid.dx = grid.dy = dx;
  grid.index.resize(static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz));
  for (int q = 0; q < nz; ++q)
    for (int p = 0; p < ny; ++p) {
      const bool in_y = settings.slab || (p >= margin && p < margin + core_y);
      const bool in_core = in_y && q >= margin && q < margin + core_z;
      grid.index[static_cast<std::size_t>(p + ny * q)] = in_core ? n_core : n_clad;
    }
  modes::FdeSettings fde;
  fde.wavelength = settings.wavelength;
  fde.polarization = modes::FdePolarization::quasi_te;  // E überwiegend waagerecht quer = Ey
  fde.modes = 1;
  if (settings.slab) fde.x_edges = modes::EdgeCondition::symmetric;  // in y unbegrenzt
  const auto found = modes::solve_modes(grid, fde);
  // "guided" vergleicht mit den Brechzahlen am Gitterrand; bei der Schicht liegt dort der Kern
  if (found.empty() || !(settings.slab ? found.front().n_eff > n_clad : found.front().guided))
    throw FieldError("3D-Wellenleiter: der Modenlöser findet keine geführte Grundmode");
  const modes::FdeMode& mode = found.front();
  // Profil am Ort von Ey (y = (j+1/2)dx, z = k*dx): Mittel der FDE-Zeilen darunter und darüber
  auto mode_at = [&](int j, int k) {
    const int q0 = std::clamp(k - 1, 0, nz - 1), q1 = std::clamp(k, 0, nz - 1);
    return 0.5 * (mode.field[static_cast<std::size_t>(j + ny * q0)] +
                  mode.field[static_cast<std::size_t>(j + ny * q1)]);
  };

  const int i_source = settings.pml_cells + 5;
  Source3d source;
  source.component = Component3d::ey;
  source.signal.signal = SignalKind::continuous;
  source.signal.wavelength = settings.wavelength;
  source.signal.ramp_periods = settings.ramp_periods;
  const double weight = constants::c0 * sim.time_step() / dx;
  for (int k = 1; k < nz; ++k)
    for (int j = 0; j < ny; ++j) {
      const double f = mode_at(j, k);
      if (std::abs(f) < 1e-4) continue;
      source.cells.push_back(sim.at(i_source, j, k));
      source.weights.push_back(weight * f);
    }
  sim.add_source(source);

  // Laufzeit: Rampe, zweimal durch das Gebiet (mit Kernindex als obere Schranke des Gruppenindex), Fenster
  const double omega = 2.0 * constants::pi * constants::c0 / settings.wavelength;
  const double period = settings.wavelength / constants::c0;
  const double settle = settings.ramp_periods * period + 2.0 * nx * dx * n_core / constants::c0;
  const auto settle_steps = static_cast<std::size_t>(std::ceil(settle / sim.time_step()));
  const auto window_steps =
      static_cast<std::size_t>(std::lround(settings.window_periods * period / sim.time_step()));

  Waveguide3dResult result;
  result.nx = nx, result.ny = ny, result.nz = nz, result.cell_size = dx;
  result.n_eff_mode = mode.n_eff;
  result.threads = sim.threads();
  result.steps = settle_steps + window_steps;
  if (settings.slab) {
    const auto slab = modes::slab_modes(n_core, n_clad, n_clad, settings.height, settings.wavelength,
                                        modes::SlabPolarization::te);
    if (slab.empty()) throw FieldError("3D-Wellenleiter: die Schicht führt keine Mode");
    result.n_eff_analytic = slab.front().n_eff;
  }
  const int j_axis =
      settings.slab ? ny / 2
                    : margin + core_y / 2;  // Ey bei (j+1/2)dx: Mitte des Kerns bei ungerader Zellzahl genau
  const int k_axis = margin + core_z / 2;
  const int i_measure = (i_source + nx - settings.pml_cells) / 2;
  std::vector<cplx> line(static_cast<std::size_t>(nx)),
      plane(static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz));

  const auto start = std::chrono::steady_clock::now();
  const auto& ey = sim.component(Component3d::ey);
  for (std::size_t s = 0; s < result.steps; ++s) {
    if (control && (s & 7u) == 0) {
      if (control->cancel_requested) return result;
      control->progress = static_cast<float>(s) / static_cast<float>(result.steps);
    }
    sim.step();
    if (s + 1 > settle_steps) {
      const cplx kernel = std::polar(1.0, -omega * sim.time());
      for (int i = 0; i < nx; ++i)
        line[static_cast<std::size_t>(i)] += ey[sim.at(i, j_axis, k_axis)] * kernel;
      for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
          plane[static_cast<std::size_t>(j + ny * k)] += ey[sim.at(i_measure, j, k)] * kernel;
    }
  }
  result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  result.cells_per_second = static_cast<double>(sim.cell_count()) * static_cast<double>(result.steps) /
                            std::max(result.seconds, 1e-9);

  // Phase entlang der Achse: Messbereich ab 0,4 um hinter der Quelle bis 5 Zellen vor der Randschicht
  const int i0 = i_source + std::max(5, static_cast<int>(std::lround(0.4e-6 / dx)));
  const int i1 = nx - settings.pml_cells - 5;
  if (i1 - i0 < 10) throw FieldError("3D-Wellenleiter: Gebiet zu kurz für die Auswertung");
  double unwrapped = std::arg(line[static_cast<std::size_t>(i0)]), previous = unwrapped;
  double sx = 0, sy = 0, sxx = 0, sxy = 0, amp_min = 1e300, amp_max = 0, amp_sum = 0;
  const double scale = 2.0 / static_cast<double>(window_steps);  // Amplitude des Dauerstrichs
  for (int i = i0; i <= i1; ++i) {
    const double raw = std::arg(line[static_cast<std::size_t>(i)]);
    double delta = raw - previous;
    delta -= 2.0 * constants::pi * std::round(delta / (2.0 * constants::pi));
    if (i > i0) unwrapped += delta;
    previous = raw;
    const double x = (i + 0.5) * dx;  // Ey liegt bei x = i*dx; konstanter Versatz ändert die Steigung nicht
    const double amplitude = std::abs(line[static_cast<std::size_t>(i)]) * scale;
    result.axis_x.push_back(x);
    result.phase.push_back(unwrapped);
    result.amplitude.push_back(amplitude);
    sx += x, sy += unwrapped, sxx += x * x, sxy += x * unwrapped;
    amp_min = std::min(amp_min, amplitude), amp_max = std::max(amp_max, amplitude), amp_sum += amplitude;
  }
  const double count = i1 - i0 + 1;
  const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
  const double beta = std::abs(slope);
  const double k0 = omega / constants::c0;
  result.n_eff_fdtd = beta / k0;
  // Yee-Gitter: eine Welle exp(i(beta x - w t)) erfüllt die Differenzengleichungen mit den Ersatzgrößen
  // (2/dx) sin(beta dx/2) und (2/dt) sin(w dt/2) (docs/physics/fdtd3d.md)
  const double dt = sim.time_step();
  result.n_eff_corrected =
      (2.0 / dx) * std::sin(0.5 * beta * dx) / ((2.0 / (constants::c0 * dt)) * std::sin(0.5 * omega * dt));
  const double reference = settings.slab ? result.n_eff_analytic : result.n_eff_mode;
  result.relative_error = std::abs(result.n_eff_fdtd - reference) / reference;
  result.amplitude_ripple = (amp_max - amp_min) / (amp_sum / count);
  result.fit_from = result.axis_x.front(), result.fit_to = result.axis_x.back();

  // Bilder und Modenüberlapp
  result.top_view.resize(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i)
      result.top_view[static_cast<std::size_t>(i + nx * j)] = ey[sim.at(i, j, k_axis)];
  result.cross_view.resize(plane.size());
  result.cross_mode.resize(plane.size());
  cplx inner(0.0, 0.0);
  double field_norm = 0.0, mode_norm = 0.0;
  for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j) {
      const std::size_t p = static_cast<std::size_t>(j + ny * k);
      const double f = mode_at(j, k);
      result.cross_view[p] = std::abs(plane[p]) * scale;
      result.cross_mode[p] = f;
      inner += plane[p] * f;
      field_norm += std::norm(plane[p]);
      mode_norm += f * f;
    }
  result.mode_overlap = std::norm(inner) / (field_norm * mode_norm);
  result.completed = true;
  if (control) control->progress = 1.0f;
  return result;
}

}  // namespace photonlab::field
