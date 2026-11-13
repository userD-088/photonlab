#include "photonlab/field/fdtd.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "photonlab/core/units.hpp"
#include "photonlab/modes/profile.hpp"

namespace photonlab::field {
namespace {

const double eta0 = constants::mu0 * constants::c0;  // Wellenwiderstand des Vakuums [Ohm]

// CPML-Koeffizienten b und c an der Stelle u (in Zellen) einer Achse mit count Zellen.
// Polynomielles Leitfähigkeitsprofil (Grad 3) nach Roden/Gedney 2000 mit kappa = 1 und alpha = 0;
// sigma_max = 0.8*(m + 1)/(eta0*dx*n) ist der übliche Richtwert (Taflove/Hagness, Kap. 7).
void pml_coefficient(double u, int count, int pml_cells, double dx, double dt, double index, double* b,
                     double* c) {
  *b = 0.0, *c = 0.0;  // außerhalb der Schicht: psi bleibt 0
  double depth = 0.0;
  if (u < pml_cells)
    depth = (pml_cells - u) / pml_cells;
  else if (u > count - pml_cells)
    depth = (u - (count - pml_cells)) / pml_cells;
  if (depth <= 0.0) return;
  depth = std::min(depth, 1.0);
  const double order = 3.0;
  const double sigma_max = 0.8 * (order + 1.0) / (eta0 * dx * index);
  const double sigma = sigma_max * std::pow(depth, order);
  *b = std::exp(-sigma * dt / constants::eps0);
  *c = *b - 1.0;
}

}  // namespace

double Fdtd::signal(const Source& source, double time) {
  const double omega = omega_from_wavelength(source.wavelength);
  if (source.signal == SignalKind::pulse) {
    // Gauß-Puls, Maximum bei t0 = 4*tau: dort ist die Einhüllende beim Start auf e^-16 abgeklungen
    const double t0 = 4.0 * source.pulse_duration;
    const double u = (time - t0) / source.pulse_duration;
    return std::exp(-u * u) * std::sin(omega * (time - t0) + source.phase);
  }
  // Dauerstrich mit sanftem Einschalten (sin²-Rampe), damit kein breitbandiger Einschaltstoß entsteht
  const double ramp_time = source.ramp_periods * 2.0 * constants::pi / omega;
  double ramp = 1.0;
  if (time < ramp_time) {
    const double s = std::sin(0.5 * constants::pi * time / ramp_time);
    ramp = s * s;
  }
  return ramp * std::sin(omega * time + source.phase);
}

Fdtd::Fdtd(const FieldScene& scene) : polarization_(scene.polarization) {
  validate(scene);
  nx_ = grid_nx(scene);
  ny_ = grid_ny(scene);
  dx_ = scene.cell_size;
  dt_ = field::time_step(scene);
  periodic_x_ = scene.boundary_x == Boundary::periodic;
  periodic_y_ = scene.boundary_y == Boundary::periodic;
  pml_x_ = !periodic_x_ && scene.pml_cells > 0;
  pml_y_ = !periodic_y_ && scene.pml_cells > 0;

  const std::size_t n = static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_);
  f_.assign(n, 0.0);
  f_prev_.assign(n, 0.0);
  gx_.assign(n, 0.0);
  gy_.assign(n, 0.0);
  psi_fx_.assign(n, 0.0);
  psi_fy_.assign(n, 0.0);
  psi_gx_.assign(n, 0.0);
  psi_gy_.assign(n, 0.0);
  eps_f_.resize(n);
  zero_row_.assign(static_cast<std::size_t>(nx_), 0.0);

  // Materialverteilung an den Orten der Feldkomponenten abtasten (Treppenstufen, keine Glättung)
  const bool ez = polarization_ == Polarization::ez;
  const double offset = ez ? 0.0 : 0.5;  // Ez auf Knoten, Hz in Zellmitten
  if (ez) cf_.resize(n);
  if (!ez) cgx_.resize(n), cgy_.resize(n);
  for (int j = 0; j < ny_; ++j)
    for (int i = 0; i < nx_; ++i) {
      const std::size_t k = at(i, j);
      const double index = index_at(scene, (i + offset) * dx_, (j + offset) * dx_);
      eps_f_[k] = index * index;
      if (ez) {
        cf_[k] = dt_ / (constants::eps0 * eps_f_[k]);
      } else {
        const double nx_index = index_at(scene, (i + 0.5) * dx_, j * dx_);  // Ex bei (i + 1/2, j)
        const double ny_index = index_at(scene, i * dx_, (j + 0.5) * dx_);  // Ey bei (i, j + 1/2)
        cgx_[k] = dt_ / (constants::eps0 * nx_index * nx_index);
        cgy_[k] = dt_ / (constants::eps0 * ny_index * ny_index);
      }
    }

  // Randschichten
  auto fill = [&](std::vector<double>& b, std::vector<double>& c, int count, bool active, double shift) {
    b.assign(static_cast<std::size_t>(count), 0.0);
    c.assign(static_cast<std::size_t>(count), 0.0);
    if (!active) return;
    for (int i = 0; i < count; ++i)
      pml_coefficient(i + shift, count, scene.pml_cells, dx_, dt_, scene.background_index,
                      &b[static_cast<std::size_t>(i)], &c[static_cast<std::size_t>(i)]);
  };
  fill(bxe_, cxe_, nx_, pml_x_, 0.0);
  fill(bxh_, cxh_, nx_, pml_x_, 0.5);
  fill(bye_, cye_, ny_, pml_y_, 0.0);
  fill(byh_, cyh_, ny_, pml_y_, 0.5);

  // Quellen: Zellen und Gewichte. Eine Linienquelle der Amplitude A strahlt nach beiden Seiten eine
  // ebene Welle mit der E-Feld-Amplitude A ab (Stromschicht; Herleitung in docs/physics/fdtd.md).
  auto cell_of = [&](double x, double y, int* i, int* j) {
    // Ez sitzt auf Knoten (nächster Knoten), Hz in Zellmitten (Zelle, die den Punkt enthält). Der kleine
    // Zuschlag macht Punkte genau auf einer Zellkante unabhängig von Rundungsfehlern.
    const double tie = 1e-6;
    *i = ez ? static_cast<int>(std::lround(x / dx_)) : static_cast<int>(std::floor(x / dx_ + tie));
    *j = ez ? static_cast<int>(std::lround(y / dx_)) : static_cast<int>(std::floor(y / dx_ + tie));
    // an Wänden eine Zelle Abstand halten; in periodischer Richtung zählt jede Zelle
    *i = periodic_x_ ? std::clamp(*i, 0, nx_ - 1) : std::clamp(*i, 1, nx_ - 2);
    *j = periodic_y_ ? std::clamp(*j, 0, ny_ - 1) : std::clamp(*j, 1, ny_ - 2);
  };
  auto weight_at = [&](std::size_t k, double amplitude) {
    const double base = 2.0 * constants::c0 * dt_ / dx_ * amplitude;
    return ez ? base / std::sqrt(eps_f_[k]) : base / eta0;
  };
  // Zellen des Skalarfeldes entlang einer achsenparallelen Linie, in Laufrichtung geordnet
  auto line_cells = [&](double x1, double y1, double x2, double y2) {
    int i1 = 0, j1 = 0, i2 = 0, j2 = 0;
    cell_of(x1, y1, &i1, &j1);
    cell_of(x2, y2, &i2, &j2);
    std::vector<std::size_t> cells;
    if (std::abs(x2 - x1) < 0.5 * dx_)
      for (int j = std::min(j1, j2); j <= std::max(j1, j2); ++j) cells.push_back(at(i1, j));
    else
      for (int i = std::min(i1, i2); i <= std::max(i1, i2); ++i) cells.push_back(at(i, j1));
    return cells;
  };
  // Geführte Mode des Brechzahlprofils unter einer Linie. Im 2D-FDTD ist jeder Wellenleiter eine Schicht
  // in der Ebene: Ez-Polarisation entspricht deren TE-Moden (E parallel zu den Grenzflächen), Hz den
  // TM-Moden.
  const auto slab_polarization = ez ? modes::SlabPolarization::te : modes::SlabPolarization::tm;
  auto mode_under = [&](const std::vector<std::size_t>& cells, double wavelength, int order,
                        const std::string& who) {
    std::vector<double> profile(cells.size());
    for (std::size_t k = 0; k < cells.size(); ++k) profile[k] = std::sqrt(eps_f_[cells[k]]);
    std::vector<modes::ProfileMode> found;
    try {
      found = modes::profile_modes(profile, dx_, wavelength, slab_polarization, order + 1);
    } catch (const modes::ModeError& e) {
      throw FieldError(who + e.what());
    }
    if (static_cast<int>(found.size()) <= order)
      throw FieldError(who + "unter der Linie wird keine Mode der Ordnung " + std::to_string(order) +
                       " geführt (Linie über den Wellenleiter legen und in den Mantel reichen lassen)");
    return found[static_cast<std::size_t>(order)];
  };

  for (const auto& source : scene.sources) {
    ActiveSource active{source, {}};
    int i = 0, j = 0;
    if (source.kind == SourceKind::mode) {
      // Stromschicht mit Modenprofil f: Zuwachs 2*n_eff*c*dt/(n²*dx) * A * f, für Hz zusätzlich 1/eta0.
      // A ist der Spitzenwert des E-Feldes (Ez) bzw. von eta0*Hz der abgestrahlten Mode.
      const auto cells = line_cells(source.x1, source.y1, source.x2, source.y2);
      const modes::ProfileMode mode =
          mode_under(cells, source.wavelength, source.mode_order, "Quelle '" + source.name + "': ");
      for (std::size_t k = 0; k < cells.size(); ++k) {
        const double base =
            2.0 * mode.n_eff * constants::c0 * dt_ / (eps_f_[cells[k]] * dx_) * source.amplitude;
        active.cells.push_back({cells[k], (ez ? base : base / eta0) * mode.field[k]});
      }
    } else if (source.kind == SourceKind::point) {
      cell_of(source.x1, source.y1, &i, &j);
      active.cells.push_back({at(i, j), weight_at(at(i, j), source.amplitude)});
    } else {
      const double length = std::hypot(source.x2 - source.x1, source.y2 - source.y1);
      const int samples = std::max(1, static_cast<int>(std::ceil(length / (0.5 * dx_))));
      std::map<std::size_t, double> cells;  // jede Zelle nur einmal
      for (int s = 0; s <= samples; ++s) {
        const double t = static_cast<double>(s) / samples;
        cell_of(source.x1 + t * (source.x2 - source.x1), source.y1 + t * (source.y2 - source.y1), &i, &j);
        double profile = 1.0;
        if (source.beam_width > 0.0) {
          const double distance = (t - 0.5) * length / source.beam_width;
          profile = std::exp(-distance * distance);
        }
        cells.emplace(at(i, j), profile);
      }
      for (const auto& [k, profile] : cells)
        active.cells.push_back({k, profile * weight_at(k, source.amplitude)});
    }
    sources_.push_back(std::move(active));
  }

  // Monitore
  for (const auto& monitor : scene.monitors) {
    MonitorData data;
    data.name = monitor.name;
    data.kind = monitor.kind;
    data.sample_interval = dt_;
    const std::size_t index = monitors_.size();
    if (monitor.kind == MonitorKind::point) {
      int i = 0, j = 0;
      cell_of(monitor.x1, monitor.y1, &i, &j);
      data.unit = ez ? "V/m" : "A/m";
      data.start_time = dt_;
      probes_.push_back({index, at(i, j)});
    } else if (monitor.kind == MonitorKind::mode) {
      // Modenamplitude a = <Feld, Mode> / <Mode, Mode> mit dem Gewicht, unter dem die Moden orthogonal sind
      double wavelength = monitor.wavelength;
      if (!(wavelength > 0.0)) {
        if (scene.sources.empty())
          throw FieldError("Monitor '" + monitor.name + "': ohne Quelle fehlt die Wellenlänge");
        wavelength = scene.sources.front().wavelength;
      }
      ModeProbe probe;
      probe.monitor = index;
      probe.cells = line_cells(monitor.x1, monitor.y1, monitor.x2, monitor.y2);
      const modes::ProfileMode mode =
          mode_under(probe.cells, wavelength, monitor.mode_order, "Monitor '" + monitor.name + "': ");
      double norm = 0.0;
      probe.weights.resize(probe.cells.size());
      for (std::size_t k = 0; k < probe.cells.size(); ++k) {
        const double w = modes::overlap_weight(std::sqrt(eps_f_[probe.cells[k]]), slab_polarization);
        probe.weights[k] = w * mode.field[k];
        norm += w * mode.field[k] * mode.field[k];
      }
      for (double& w : probe.weights) w /= norm;
      data.unit = ez ? "V/m" : "A/m";
      data.start_time = dt_;
      data.mode_index = mode.n_eff;
      mode_probes_.push_back(std::move(probe));
    } else {
      FluxLine line;
      line.monitor = index;
      line.vertical = std::abs(monitor.x2 - monitor.x1) < 0.5 * dx_;
      const double fixed = line.vertical ? monitor.x1 : monitor.y1;
      const double a = line.vertical ? monitor.y1 : monitor.x1, b = line.vertical ? monitor.y2 : monitor.x2;
      const int fixed_count = line.vertical ? nx_ : ny_, run_count = line.vertical ? ny_ : nx_;
      line.fixed = std::clamp(static_cast<int>(std::lround(fixed / dx_)), 1, fixed_count - 2);
      line.from = std::clamp(static_cast<int>(std::lround(std::min(a, b) / dx_)), 1, run_count - 2);
      line.to = std::clamp(static_cast<int>(std::lround(std::max(a, b) / dx_)), 1, run_count - 2);
      data.unit = "W/m";
      data.start_time = 0.5 * dt_;
      flux_lines_.push_back(line);
    }
    monitors_.push_back(std::move(data));
  }
}

void Fdtd::run(std::size_t steps, JobControl* control) {
  for (std::size_t s = 0; s < steps; ++s) {
    if (control && (s & 15u) == 0) {
      if (control->cancel_requested) return;
      control->progress = static_cast<float>(s) / static_cast<float>(steps);
    }
    step();
  }
  if (control) control->progress = 1.0f;
}

void Fdtd::step() {
  if (polarization_ == Polarization::ez)
    step_ez();
  else
    step_hz();

  // Quellen wirken zur halben Zeit des Schritts (zwischen altem und neuem Skalarfeld)
  const double source_time = (static_cast<double>(steps_) + 0.5) * dt_;
  for (const auto& source : sources_) {
    const double s = signal(source.source, source_time);
    for (const auto& cell : source.cells) f_[cell.index] += cell.weight * s;
  }
  ++steps_;
  record();
}

// Ez auf Knoten (i, j); Hx bei (i, j + 1/2); Hy bei (i + 1/2, j).
void Fdtd::step_ez() {
  const double inv = 1.0 / dx_;
  const double cm = dt_ / constants::mu0;
  const std::size_t nx = static_cast<std::size_t>(nx_);

  // H von n - 1/2 nach n + 1/2 aus Ez zur Zeit n
  for (int j = 0; j < ny_; ++j) {
    const std::size_t row = at(0, j);
    const double* e = &f_[row];
    const double* e_next_row = j + 1 < ny_ ? &f_[at(0, j + 1)] : periodic_y_ ? &f_[0] : zero_row_.data();
    double* hx = &gx_[row];
    double* hy = &gy_[row];
    double* psi_x = &psi_gx_[row];
    double* psi_y = &psi_gy_[row];
    const double by = byh_[static_cast<std::size_t>(j)], cy = cyh_[static_cast<std::size_t>(j)];
    for (std::size_t i = 0; i < nx; ++i) {
      const double d = (e_next_row[i] - e[i]) * inv;  // dEz/dy bei (i, j + 1/2)
      psi_x[i] = by * psi_x[i] + cy * d;
      hx[i] -= cm * (d + psi_x[i]);
    }
    for (std::size_t i = 0; i + 1 < nx; ++i) {
      const double d = (e[i + 1] - e[i]) * inv;  // dEz/dx bei (i + 1/2, j)
      psi_y[i] = bxh_[i] * psi_y[i] + cxh_[i] * d;
      hy[i] += cm * (d + psi_y[i]);
    }
    {
      const std::size_t i = nx - 1;  // rechter Rand: Nachbar ist die Wand (Ez = 0) oder die linke Spalte
      const double d = ((periodic_x_ ? e[0] : 0.0) - e[i]) * inv;
      psi_y[i] = bxh_[i] * psi_y[i] + cxh_[i] * d;
      hy[i] += cm * (d + psi_y[i]);
    }
  }

  // Ez von n nach n + 1; auf dem linken und oberen Rand bleibt Ez = 0 (ideal leitende Wand), außer periodisch
  f_prev_ = f_;
  for (int j = periodic_y_ ? 0 : 1; j < ny_; ++j) {
    const std::size_t row = at(0, j);
    const double* hx = &gx_[row];
    const double* hx_prev_row = j > 0 ? &gx_[at(0, j - 1)] : &gx_[at(0, ny_ - 1)];
    const double* hy = &gy_[row];
    double* e = &f_[row];
    double* psi_x = &psi_fx_[row];
    double* psi_y = &psi_fy_[row];
    const double* cf = &cf_[row];
    const double by = bye_[static_cast<std::size_t>(j)], cy = cye_[static_cast<std::size_t>(j)];
    if (periodic_x_) {
      const double dhy = (hy[0] - hy[nx - 1]) * inv, dhx = (hx[0] - hx_prev_row[0]) * inv;
      psi_x[0] = bxe_[0] * psi_x[0] + cxe_[0] * dhy;
      psi_y[0] = by * psi_y[0] + cy * dhx;
      e[0] += cf[0] * (dhy + psi_x[0] - dhx - psi_y[0]);
    }
    for (std::size_t i = 1; i < nx; ++i) {
      const double dhy = (hy[i] - hy[i - 1]) * inv;       // dHy/dx bei (i, j)
      const double dhx = (hx[i] - hx_prev_row[i]) * inv;  // dHx/dy bei (i, j)
      psi_x[i] = bxe_[i] * psi_x[i] + cxe_[i] * dhy;
      psi_y[i] = by * psi_y[i] + cy * dhx;
      e[i] += cf[i] * (dhy + psi_x[i] - dhx - psi_y[i]);
    }
  }
}

// Hz in Zellmitten (i + 1/2, j + 1/2); Ex bei (i + 1/2, j); Ey bei (i, j + 1/2).
void Fdtd::step_hz() {
  const double inv = 1.0 / dx_;
  const double cm = dt_ / constants::mu0;
  const std::size_t nx = static_cast<std::size_t>(nx_);

  // E von n - 1/2 nach n + 1/2 aus Hz zur Zeit n; tangentiales E auf dem linken und oberen Rand bleibt 0
  for (int j = 0; j < ny_; ++j) {
    const std::size_t row = at(0, j);
    const double* h = &f_[row];
    double* ex = &gx_[row];
    double* ey = &gy_[row];
    double* psi_x = &psi_gx_[row];
    double* psi_y = &psi_gy_[row];
    if (j > 0 || periodic_y_) {
      const double* h_prev_row = j > 0 ? &f_[at(0, j - 1)] : &f_[at(0, ny_ - 1)];
      const double by = bye_[static_cast<std::size_t>(j)], cy = cye_[static_cast<std::size_t>(j)];
      const double* c = &cgx_[row];
      for (std::size_t i = 0; i < nx; ++i) {
        const double d = (h[i] - h_prev_row[i]) * inv;  // dHz/dy bei (i + 1/2, j)
        psi_x[i] = by * psi_x[i] + cy * d;
        ex[i] += c[i] * (d + psi_x[i]);
      }
    }
    const double* c = &cgy_[row];
    if (periodic_x_) {
      const double d = (h[0] - h[nx - 1]) * inv;
      psi_y[0] = bxe_[0] * psi_y[0] + cxe_[0] * d;
      ey[0] -= c[0] * (d + psi_y[0]);
    }
    for (std::size_t i = 1; i < nx; ++i) {
      const double d = (h[i] - h[i - 1]) * inv;  // dHz/dx bei (i, j + 1/2)
      psi_y[i] = bxe_[i] * psi_y[i] + cxe_[i] * d;
      ey[i] -= c[i] * (d + psi_y[i]);
    }
  }

  // Hz von n nach n + 1
  f_prev_ = f_;
  for (int j = 0; j < ny_; ++j) {
    const std::size_t row = at(0, j);
    const double* ex = &gx_[row];
    const double* ex_next_row = j + 1 < ny_ ? &gx_[at(0, j + 1)] : periodic_y_ ? &gx_[0] : zero_row_.data();
    const double* ey = &gy_[row];
    double* h = &f_[row];
    double* psi_x = &psi_fx_[row];
    double* psi_y = &psi_fy_[row];
    const double by = byh_[static_cast<std::size_t>(j)], cy = cyh_[static_cast<std::size_t>(j)];
    for (std::size_t i = 0; i < nx; ++i) {
      const double ey_next = i + 1 < nx ? ey[i + 1] : periodic_x_ ? ey[0] : 0.0;
      const double dex = (ex_next_row[i] - ex[i]) * inv;  // dEx/dy bei (i + 1/2, j + 1/2)
      const double dey = (ey_next - ey[i]) * inv;         // dEy/dx bei (i + 1/2, j + 1/2)
      psi_y[i] = by * psi_y[i] + cy * dex;
      psi_x[i] = bxh_[i] * psi_x[i] + cxh_[i] * dey;
      h[i] += cm * (dex + psi_y[i] - dey - psi_x[i]);
    }
  }
}

void Fdtd::record() {
  for (const auto& probe : probes_) monitors_[probe.monitor].values.push_back(f_[probe.index]);
  for (const auto& probe : mode_probes_) {
    double amplitude = 0.0;
    for (std::size_t k = 0; k < probe.cells.size(); ++k) amplitude += f_[probe.cells[k]] * probe.weights[k];
    monitors_[probe.monitor].values.push_back(amplitude);
  }

  // Poynting-Fluss S = E x H zur halben Zeit: Skalarfeld zeitlich gemittelt, Feld in der Ebene räumlich
  const bool ez = polarization_ == Polarization::ez;
  for (const auto& line : flux_lines_) {
    double sum = 0.0;
    for (int r = line.from; r <= line.to; ++r) {
      if (line.vertical) {
        const int i = line.fixed, j = r;
        if (ez) {  // S_x = -Ez*Hy, Hy liegt bei (i -+ 1/2, j)
          const double e = 0.5 * (f_[at(i, j)] + f_prev_[at(i, j)]);
          sum -= e * 0.5 * (gy_[at(i - 1, j)] + gy_[at(i, j)]);
        } else {  // S_x = Ey*Hz, Hz liegt bei (i -+ 1/2, j + 1/2)
          const double h =
              0.25 * (f_[at(i - 1, j)] + f_[at(i, j)] + f_prev_[at(i - 1, j)] + f_prev_[at(i, j)]);
          sum += gy_[at(i, j)] * h;
        }
      } else {
        const int i = r, j = line.fixed;
        if (ez) {  // S_y = Ez*Hx, Hx liegt bei (i, j -+ 1/2)
          const double e = 0.5 * (f_[at(i, j)] + f_prev_[at(i, j)]);
          sum += e * 0.5 * (gx_[at(i, j - 1)] + gx_[at(i, j)]);
        } else {  // S_y = -Ex*Hz, Hz liegt bei (i + 1/2, j -+ 1/2)
          const double h =
              0.25 * (f_[at(i, j - 1)] + f_[at(i, j)] + f_prev_[at(i, j - 1)] + f_prev_[at(i, j)]);
          sum -= gx_[at(i, j)] * h;
        }
      }
    }
    monitors_[line.monitor].values.push_back(sum * dx_);
  }
}

double Fdtd::energy() const {
  const bool ez = polarization_ == Polarization::ez;
  double scalar = 0.0, in_plane = 0.0;
  for (std::size_t k = 0; k < f_.size(); ++k) {
    if (ez) {
      scalar += eps_f_[k] * f_[k] * f_prev_[k];
      in_plane += gx_[k] * gx_[k] + gy_[k] * gy_[k];
    } else {
      scalar += f_[k] * f_prev_[k];
      // eps0*eps_r an den Orten von Ex und Ey steckt in den Update-Koeffizienten: eps = dt/c
      in_plane += dt_ / cgx_[k] * gx_[k] * gx_[k] + dt_ / cgy_[k] * gy_[k] * gy_[k];
    }
  }
  const double area = dx_ * dx_;
  if (ez) return 0.5 * area * (constants::eps0 * scalar + constants::mu0 * in_plane);
  return 0.5 * area * (constants::mu0 * scalar + in_plane);
}

}  // namespace photonlab::field
