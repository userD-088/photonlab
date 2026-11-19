#include "photonlab/field/fdtd.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "cpml.hpp"
#include "photonlab/core/step_team.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/modes/profile.hpp"

namespace photonlab::field {
namespace {

const double eta0 = constants::mu0 * constants::c0;  // Wellenwiderstand des Vakuums [Ohm]

using detail::pml_coefficient;

}  // namespace

double Fdtd::carrier_density_at(double x, double y) const {
  const int i = static_cast<int>(std::lround(x / dx_)), j = static_cast<int>(std::lround(y / dx_));
  if (i < 0 || j < 0 || i >= nx_ || j >= ny_) return 0.0;
  const std::size_t k = at(i, j);
  for (const auto* list : {&kerr_cells_, &kerr_y_cells_, &kerr_x_cells_})
    for (const KerrCell& cell : *list)
      if (cell.index == k) return cell.n;
  return 0.0;
}

void Fdtd::set_permittivity(std::size_t index, double eps) {
  if (polarization_ != Polarization::ez) throw FieldError("Permittivität setzen: nur bei Ez-Polarisation");
  if (!kerr_cells_.empty()) throw FieldError("Permittivität setzen: nicht mit nichtlinearen Formen");
  if (index >= eps_f_.size() || !(eps >= 1.0))
    throw FieldError("Permittivität setzen: ungültige Zelle oder Wert");
  eps_f_[index] = eps;
  cf_[index] = dt_ / (constants::eps0 * eps);
}

Fdtd::ModeWeights Fdtd::mode_weights(std::size_t monitor) const {
  for (const auto& probe : mode_probes_)
    if (probe.monitor == monitor) return {probe.cells, probe.weights};
  throw FieldError("Monitor " + std::to_string(monitor) + " ist kein Modenmonitor");
}

void Fdtd::add_injection(const std::vector<std::size_t>& cells, const std::vector<double>& weights,
                         const Source& signal) {
  if (cells.size() != weights.size()) throw FieldError("Quelle: Zellen und Gewichte passen nicht zusammen");
  ActiveSource active{signal, {}};
  for (std::size_t k = 0; k < cells.size(); ++k) {
    if (cells[k] >= f_.size()) throw FieldError("Quelle: Zelle außerhalb des Gitters");
    active.cells.push_back({cells[k], weights[k] * signal.amplitude});
  }
  sources_.push_back(std::move(active));
}

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

Fdtd::Fdtd(const FieldScene& input) : polarization_(input.polarization) {
  const FieldScene scene = active_scene(input);  // deaktivierte Objekte wirken nicht
  validate(scene);
  nx_ = grid_nx(scene);
  ny_ = grid_ny(scene);
  dx_ = scene.cell_size;
  dt_ = field::time_step(scene);
  if (!scene.sources.empty())
    hbar_omega_ = constants::h * constants::c0 / scene.sources.front().wavelength;  // hbar*omega = h*c/lambda
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
  // Nichtlineare Zelle: Kerr n2 = 3 chi3 / (4 n0^2 eps0 c); TPA <sigma2 E^4> = beta I^2  =>  sigma2 = 2/3
  // beta n0^2 c^2 eps0^2; Ladungsträger (Silizium, Soref-Bennett linearisiert): dn/dN = -3,6e-27 m^3,
  // Absorption 1,45e-21 m^2 (docs/physics/nonlinear.md)
  auto make_kerr_cell = [&](std::size_t k, double index, const NonlinearParams& nl) {
    KerrCell cell;
    cell.index = k;
    cell.eps = index * index;
    cell.chi3 = 4.0 * cell.eps * constants::eps0 * constants::c0 * nl.n2 / 3.0;
    cell.sigma2 = 2.0 / 3.0 * nl.beta_tpa * cell.eps * constants::c0 * constants::c0 * constants::eps0 *
                  constants::eps0;
    cell.tau = nl.carrier_lifetime;
    cell.sigma_fc = 1.45e-21 * index * constants::c0 * constants::eps0;
    cell.eps_fc = 2.0 * index * -3.6e-27;
    cell.d = 0.0;
    cell.n = 0.0;
    cell.e_old = 0.0;
    return cell;
  };
  for (int j = 0; j < ny_; ++j)
    for (int i = 0; i < nx_; ++i) {
      const std::size_t k = at(i, j);
      const double index = index_at(scene, (i + offset) * dx_, (j + offset) * dx_);
      eps_f_[k] = index * index;
      if (ez) {
        cf_[k] = dt_ / (constants::eps0 * eps_f_[k]);
        const NonlinearParams nl = nonlinear_at(scene, i * dx_, j * dx_);
        if (nl.any()) kerr_cells_.push_back(make_kerr_cell(k, index, nl));
      } else {
        const double nx_index = index_at(scene, (i + 0.5) * dx_, j * dx_);  // Ex bei (i + 1/2, j)
        const double ny_index = index_at(scene, i * dx_, (j + 0.5) * dx_);  // Ey bei (i, j + 1/2)
        cgx_[k] = dt_ / (constants::eps0 * nx_index * nx_index);
        cgy_[k] = dt_ / (constants::eps0 * ny_index * ny_index);
        const NonlinearParams nlx = nonlinear_at(scene, (i + 0.5) * dx_, j * dx_);
        const NonlinearParams nly = nonlinear_at(scene, i * dx_, (j + 0.5) * dx_);
        if (nlx.any()) kerr_x_cells_.push_back(make_kerr_cell(k, nx_index, nlx));
        if (nly.any()) kerr_y_cells_.push_back(make_kerr_cell(k, ny_index, nly));
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
  // Innerer Bereich in x ohne Randschicht: [ix_lo_, ix_hi_)
  auto inside_x = [&](int i) {
    const auto k = static_cast<std::size_t>(i);
    return cxe_[k] == 0.0 && cxh_[k] == 0.0;
  };
  ix_lo_ = 0;
  while (ix_lo_ < nx_ && !inside_x(ix_lo_)) ++ix_lo_;
  ix_hi_ = nx_;
  while (ix_hi_ > ix_lo_ && !inside_x(ix_hi_ - 1)) --ix_hi_;

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

void Fdtd::set_threads(int threads) {
  requested_threads_ = std::max(0, threads);
  team_.reset();
}

int Fdtd::threads() const {
  if (requested_threads_ > 0) return std::min(requested_threads_, std::max(1, ny_ / 4));
  // automatisch: jedes Mitglied soll mindestens etwa 32000 Zellen bekommen, sonst frisst das Verteilen den
  // Gewinn auf
  return StepTeam::suggested_size(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_), 32768);
}

// Verteilt die Zeilen [0, ny) in Bändern auf die Mitglieder der Gruppe; body(j0, j1) rechnet ein Band.
template <class Body>
void Fdtd::for_rows(const Body& body) {
  const int count = threads();
  if (count <= 1) {
    body(0, ny_);
    return;
  }
  if (!team_ || team_->size() != count) team_ = std::make_unique<StepTeam>(count);
  team_->run([&](int member) {
    int j0 = 0, j1 = 0;
    split_range(ny_, count, member, &j0, &j1);
    body(j0, j1);
  });
}

void Fdtd::step() {
  if (polarization_ == Polarization::ez)
    step_ez();
  else
    step_hz();

  // Quellen wirken zur halben Zeit des Schritts (zwischen altem und neuem Skalarfeld)
  const double source_time = (static_cast<double>(steps_) + 0.5) * dt_;
  for (const auto& source : sources_) {
    const double s = source_scale_ * signal(source.source, source_time);
    for (const auto& cell : source.cells) f_[cell.index] += cell.weight * s;
  }
  ++steps_;
  record();
}

// Die Hilfsfelder psi der CPML sind außerhalb der Randschicht exakt 0 (b = c = 0). Dort entfallen sie: Die
// Schleifen rechnen in x-Richtung drei Abschnitte, nur die äußeren mit psi; Zeilen außerhalb der Schicht in
// y ganz ohne. Das Ergebnis ist bitgleich mit der Rechnung mit psi überall (d + 0 = d).

// Ez auf Knoten (i, j); Hx bei (i, j + 1/2); Hy bei (i + 1/2, j).
void Fdtd::step_ez() {
  const double inv = 1.0 / dx_;
  const double cm = dt_ / constants::mu0;
  const std::size_t nx = static_cast<std::size_t>(nx_);
  const std::size_t lo = static_cast<std::size_t>(ix_lo_), hi = static_cast<std::size_t>(ix_hi_);

  // H von n - 1/2 nach n + 1/2 aus Ez zur Zeit n
  for_rows([&](int j0, int j1) {
    for (int j = j0; j < j1; ++j) {
      const std::size_t row = at(0, j);
      const double* PHOTONLAB_RESTRICT e = &f_[row];
      const double* PHOTONLAB_RESTRICT e_next_row = j + 1 < ny_   ? &f_[at(0, j + 1)]
                                                    : periodic_y_ ? &f_[0]
                                                                  : zero_row_.data();
      double* PHOTONLAB_RESTRICT hx = &gx_[row];
      double* PHOTONLAB_RESTRICT hy = &gy_[row];
      double* PHOTONLAB_RESTRICT psi_x = &psi_gx_[row];
      double* PHOTONLAB_RESTRICT psi_y = &psi_gy_[row];
      const double by = byh_[static_cast<std::size_t>(j)], cy = cyh_[static_cast<std::size_t>(j)];
      if (cy != 0.0) {
        for (std::size_t i = 0; i < nx; ++i) {
          const double d = (e_next_row[i] - e[i]) * inv;  // dEz/dy bei (i, j + 1/2)
          psi_x[i] = by * psi_x[i] + cy * d;
          hx[i] -= cm * (d + psi_x[i]);
        }
      } else {
        for (std::size_t i = 0; i < nx; ++i) hx[i] -= cm * ((e_next_row[i] - e[i]) * inv);
      }
      auto hy_pml = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
          const double d = (e[i + 1] - e[i]) * inv;  // dEz/dx bei (i + 1/2, j)
          psi_y[i] = bxh_[i] * psi_y[i] + cxh_[i] * d;
          hy[i] += cm * (d + psi_y[i]);
        }
      };
      const std::size_t last = nx - 1;
      hy_pml(0, std::min(lo, last));
      for (std::size_t i = std::min(lo, last); i < std::min(hi, last); ++i)
        hy[i] += cm * ((e[i + 1] - e[i]) * inv);
      hy_pml(std::min(hi, last), last);
      {
        const std::size_t i = last;  // rechter Rand: Nachbar ist die Wand (Ez = 0) oder die linke Spalte
        const double d = ((periodic_x_ ? e[0] : 0.0) - e[i]) * inv;
        psi_y[i] = bxh_[i] * psi_y[i] + cxh_[i] * d;
        hy[i] += cm * (d + psi_y[i]);
      }
    }
  });

  // Ez von n nach n + 1 in den anderen Puffer (f_prev_ hält danach das alte Feld); auf dem linken und oberen
  // Rand bleibt Ez unverändert (ideal leitende Wand, Ez = 0), außer periodisch
  std::swap(f_, f_prev_);
  for_rows([&](int j0, int j1) {
    for (int j = j0; j < j1; ++j) {
      const std::size_t row = at(0, j);
      const double* PHOTONLAB_RESTRICT e_old = &f_prev_[row];
      double* PHOTONLAB_RESTRICT e = &f_[row];
      if (j == 0 && !periodic_y_) {
        std::copy(e_old, e_old + nx, e);
        continue;
      }
      const double* PHOTONLAB_RESTRICT hx = &gx_[row];
      const double* PHOTONLAB_RESTRICT hx_prev_row = j > 0 ? &gx_[at(0, j - 1)] : &gx_[at(0, ny_ - 1)];
      const double* PHOTONLAB_RESTRICT hy = &gy_[row];
      double* PHOTONLAB_RESTRICT psi_x = &psi_fx_[row];
      double* PHOTONLAB_RESTRICT psi_y = &psi_fy_[row];
      const double* PHOTONLAB_RESTRICT cf = &cf_[row];
      const double by = bye_[static_cast<std::size_t>(j)], cy = cye_[static_cast<std::size_t>(j)];
      if (periodic_x_) {
        const double dhy = (hy[0] - hy[nx - 1]) * inv, dhx = (hx[0] - hx_prev_row[0]) * inv;
        psi_x[0] = bxe_[0] * psi_x[0] + cxe_[0] * dhy;
        psi_y[0] = by * psi_y[0] + cy * dhx;
        e[0] = e_old[0] + cf[0] * (dhy + psi_x[0] - dhx - psi_y[0]);
      } else {
        e[0] = e_old[0];
      }
      auto full = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
          const double dhy = (hy[i] - hy[i - 1]) * inv;       // dHy/dx bei (i, j)
          const double dhx = (hx[i] - hx_prev_row[i]) * inv;  // dHx/dy bei (i, j)
          psi_x[i] = bxe_[i] * psi_x[i] + cxe_[i] * dhy;
          psi_y[i] = by * psi_y[i] + cy * dhx;
          e[i] = e_old[i] + cf[i] * (dhy + psi_x[i] - dhx - psi_y[i]);
        }
      };
      const std::size_t a = std::max<std::size_t>(1, lo), b = std::max(a, hi);
      if (cy != 0.0) {
        full(1, nx);
        continue;
      }
      full(1, a);
      for (std::size_t i = a; i < b; ++i)
        e[i] = e_old[i] + cf[i] * ((hy[i] - hy[i - 1]) * inv - (hx[i] - hx_prev_row[i]) * inv);
      full(b, nx);
    }
  });

  // Kerr-Zellen: der lineare Schritt hat den Zuwachs von D/eps0 mal 1/eps geliefert; D weiterführen und E aus
  // eps*E + chi3*E^3 = D/eps0 lösen (monotone Kubik, Newton ab dem alten Wert)
  for (KerrCell& cell : kerr_cells_) {
    cell.e_old = f_prev_[cell.index];
    f_[cell.index] = nonlinear_update(cell, f_[cell.index], 0.0);
  }
}

double Fdtd::nonlinear_update(KerrCell& cell, double e_lin, double other_sq) const {
  const double e_old = cell.e_old;
  const double intensity_sq = e_old * e_old + other_sq;  // |E|^2 in der Zelle
  // TPA und Absorption an Ladungsträgern als Leitfähigkeit: D/eps0 sinkt um dt/eps0 * sigma * E
  const double sigma = cell.sigma2 * intensity_sq + cell.sigma_fc * cell.n;
  cell.d += cell.eps * (e_lin - e_old) - dt_ / constants::eps0 * sigma * e_old;
  if (cell.tau > 0.0) {  // ein Paar je zwei absorbierten Photonen: dN/dt = sigma2 |E|^4 /(2 hbar w) - N/tau
    cell.n += dt_ * (cell.sigma2 * intensity_sq * intensity_sq / (2.0 * hbar_omega_) - cell.n / cell.tau);
    cell.n = std::max(cell.n, 0.0);
  }
  const double eps = cell.eps + cell.eps_fc * cell.n;
  double e = e_old;
  for (int iteration = 0; iteration < 8; ++iteration) {
    const double residual = eps * e + cell.chi3 * (e * e + other_sq) * e - cell.d;
    const double step = residual / (eps + cell.chi3 * (3.0 * e * e + other_sq));
    e -= step;
    if (std::abs(step) <= 1e-12 * (std::abs(e) + 1e-30)) break;
  }
  return e;
}

// Hz in Zellmitten (i + 1/2, j + 1/2); Ex bei (i + 1/2, j); Ey bei (i, j + 1/2).
void Fdtd::step_hz() {
  const double inv = 1.0 / dx_;
  const double cm = dt_ / constants::mu0;
  const std::size_t nx = static_cast<std::size_t>(nx_);
  const std::size_t lo = static_cast<std::size_t>(ix_lo_), hi = static_cast<std::size_t>(ix_hi_);

  // E von n - 1/2 nach n + 1/2 aus Hz zur Zeit n; tangentiales E auf dem linken und oberen Rand bleibt 0
  for (KerrCell& cell : kerr_x_cells_) cell.e_old = gx_[cell.index];
  for (KerrCell& cell : kerr_y_cells_) cell.e_old = gy_[cell.index];
  for_rows([&](int j0, int j1) {
    for (int j = j0; j < j1; ++j) {
      const std::size_t row = at(0, j);
      const double* PHOTONLAB_RESTRICT h = &f_[row];
      double* PHOTONLAB_RESTRICT ex = &gx_[row];
      double* PHOTONLAB_RESTRICT ey = &gy_[row];
      double* PHOTONLAB_RESTRICT psi_x = &psi_gx_[row];
      double* PHOTONLAB_RESTRICT psi_y = &psi_gy_[row];
      if (j > 0 || periodic_y_) {
        const double* PHOTONLAB_RESTRICT h_prev_row = j > 0 ? &f_[at(0, j - 1)] : &f_[at(0, ny_ - 1)];
        const double by = bye_[static_cast<std::size_t>(j)], cy = cye_[static_cast<std::size_t>(j)];
        const double* PHOTONLAB_RESTRICT c = &cgx_[row];
        if (cy != 0.0) {
          for (std::size_t i = 0; i < nx; ++i) {
            const double d = (h[i] - h_prev_row[i]) * inv;  // dHz/dy bei (i + 1/2, j)
            psi_x[i] = by * psi_x[i] + cy * d;
            ex[i] += c[i] * (d + psi_x[i]);
          }
        } else {
          for (std::size_t i = 0; i < nx; ++i) ex[i] += c[i] * ((h[i] - h_prev_row[i]) * inv);
        }
      }
      const double* PHOTONLAB_RESTRICT c = &cgy_[row];
      if (periodic_x_) {
        const double d = (h[0] - h[nx - 1]) * inv;
        psi_y[0] = bxe_[0] * psi_y[0] + cxe_[0] * d;
        ey[0] -= c[0] * (d + psi_y[0]);
      }
      auto ey_pml = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
          const double d = (h[i] - h[i - 1]) * inv;  // dHz/dx bei (i, j + 1/2)
          psi_y[i] = bxe_[i] * psi_y[i] + cxe_[i] * d;
          ey[i] -= c[i] * (d + psi_y[i]);
        }
      };
      const std::size_t a = std::max<std::size_t>(1, lo), b = std::max(a, hi);
      ey_pml(1, a);
      for (std::size_t i = a; i < b; ++i) ey[i] -= c[i] * ((h[i] - h[i - 1]) * inv);
      ey_pml(b, nx);
    }
  });

  // Nichtlineare Zellen: |E|^2 = Ex^2 + Ey^2, die andere Komponente gemittelt aus den vier Nachbarn
  // (Ex bei (i+1/2, j) hat Ey bei (i, j-+1/2) und (i+1, j-+1/2); Ey bei (i, j+1/2) hat Ex bei (i-+1/2, j) und
  // (i-+1/2, j+1))
  if (!kerr_x_cells_.empty() || !kerr_y_cells_.empty()) {
    auto value = [&](const std::vector<double>& field, int i, int j) {
      if (i < 0 || j < 0 || i >= nx_ || j >= ny_) return 0.0;
      return field[at(i, j)];
    };
    for (KerrCell& cell : kerr_x_cells_) {
      const int i = static_cast<int>(cell.index % nx), j = static_cast<int>(cell.index / nx);
      const double ey =
          0.25 * (value(gy_, i, j) + value(gy_, i + 1, j) + value(gy_, i, j - 1) + value(gy_, i + 1, j - 1));
      gx_[cell.index] = nonlinear_update(cell, gx_[cell.index], ey * ey);
    }
    for (KerrCell& cell : kerr_y_cells_) {
      const int i = static_cast<int>(cell.index % nx), j = static_cast<int>(cell.index / nx);
      const double ex =
          0.25 * (value(gx_, i, j) + value(gx_, i - 1, j) + value(gx_, i, j + 1) + value(gx_, i - 1, j + 1));
      gy_[cell.index] = nonlinear_update(cell, gy_[cell.index], ex * ex);
    }
  }

  // Hz von n nach n + 1 in den anderen Puffer (f_prev_ hält danach das alte Feld)
  std::swap(f_, f_prev_);
  for_rows([&](int j0, int j1) {
    for (int j = j0; j < j1; ++j) {
      const std::size_t row = at(0, j);
      const double* PHOTONLAB_RESTRICT ex = &gx_[row];
      const double* PHOTONLAB_RESTRICT ex_next_row = j + 1 < ny_   ? &gx_[at(0, j + 1)]
                                                     : periodic_y_ ? &gx_[0]
                                                                   : zero_row_.data();
      const double* PHOTONLAB_RESTRICT ey = &gy_[row];
      const double* PHOTONLAB_RESTRICT h_old = &f_prev_[row];
      double* PHOTONLAB_RESTRICT h = &f_[row];
      double* PHOTONLAB_RESTRICT psi_x = &psi_fx_[row];
      double* PHOTONLAB_RESTRICT psi_y = &psi_fy_[row];
      const double by = byh_[static_cast<std::size_t>(j)], cy = cyh_[static_cast<std::size_t>(j)];
      auto full = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
          const double ey_next = i + 1 < nx ? ey[i + 1] : periodic_x_ ? ey[0] : 0.0;
          const double dex = (ex_next_row[i] - ex[i]) * inv;  // dEx/dy bei (i + 1/2, j + 1/2)
          const double dey = (ey_next - ey[i]) * inv;         // dEy/dx bei (i + 1/2, j + 1/2)
          psi_y[i] = by * psi_y[i] + cy * dex;
          psi_x[i] = bxh_[i] * psi_x[i] + cxh_[i] * dey;
          h[i] = h_old[i] + cm * (dex + psi_y[i] - dey - psi_x[i]);
        }
      };
      if (cy != 0.0) {
        full(0, nx);
        continue;
      }
      const std::size_t last = nx - 1;
      const std::size_t a = std::min(lo, last), b = std::max(a, std::min(hi, last));
      full(0, a);
      for (std::size_t i = a; i < b; ++i)
        h[i] = h_old[i] + cm * ((ex_next_row[i] - ex[i]) * inv - (ey[i + 1] - ey[i]) * inv);
      full(b, nx);
    }
  });
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
