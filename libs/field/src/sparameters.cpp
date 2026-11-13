#include "photonlab/field/sparameters.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>

#include <Eigen/Dense>

#include "photonlab/core/parallel_for.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"

namespace photonlab::field {
namespace {

using cplx = std::complex<double>;

// Lage einer Portlinie auf dem Gitter und die Indizes der Feldwerte, aus denen die Mode gemessen wird.
struct PortGrid {
  bool vertical = true;
  int inward = +1;                      // +1: Richtung ins Bauteil entlang der positiven Achse
  int fixed = 0;                        // Spalte (senkrecht) bzw. Zeile (waagerecht) der Linie
  int from = 0, to = 0;                 // Bereich der Querkoordinate, einschließlich
  std::vector<std::size_t> scalar_a, scalar_b;  // Skalarfeld: Mittel aus a und b (bei Ez ist a = b)
  std::vector<std::size_t> plane_a, plane_b;    // Querfeld in der Ebene: Mittel aus a und b (bei Hz a = b)
  std::vector<double> eps_point;        // relative Permittivität an den Messpunkten (siehe Physik-Notiz)
  std::vector<double> eps_between;      // TM: zwischen den Punkten k-1 und k (Größe m + 1)
  std::size_t count() const { return scalar_a.size(); }
};

// Mode einer Portlinie bei einer Frequenz, auf dem Gitter gerechnet (siehe Physik-Notiz).
struct PortMode {
  std::vector<double> profile;          // Maximum = +1
  double beta_numeric = 0.0;            // beta' = 2*sin(beta_grid*dx/2)/dx
  double n_eff = 0.0;                   // beta_grid / k0 (Index auf dem Gitter, mit numerischer Dispersion)
  double impedance = 0.0;               // Ez: Admittanz Y [S]; Hz: Z0 [Ohm] (siehe Physik-Notiz)
  double norm = 0.0;                    // Leistung je |a|² [W/m]
  double weight_sum = 0.0;              // Summe f² * w
};

PortGrid build_port_grid(const FieldScene& scene, const Fdtd& sim, const Port& port) {
  PortGrid g;
  const double dx = scene.cell_size;
  const bool ez = scene.polarization == Polarization::ez;
  g.vertical = port_is_vertical(scene, port);
  g.inward = port_inward_sign(scene, port);
  const int n_fixed = g.vertical ? sim.nx() : sim.ny();
  const int n_run = g.vertical ? sim.ny() : sim.nx();
  const double fixed_coord = g.vertical ? port.x1 : port.y1;
  const double a = g.vertical ? std::min(port.y1, port.y2) : std::min(port.x1, port.x2);
  const double b = g.vertical ? std::max(port.y1, port.y2) : std::max(port.x1, port.x2);
  g.fixed = std::clamp(static_cast<int>(std::lround(fixed_coord / dx)), 2, n_fixed - 3);
  // Ez liegt auf Knoten (k*dx), Hz in Zellmitten ((k + 1/2)*dx)
  const double shift = ez ? 0.0 : 0.5;
  g.from = std::clamp(static_cast<int>(std::ceil(a / dx - shift - 1e-9)), 1, n_run - 2);
  g.to = std::clamp(static_cast<int>(std::floor(b / dx - shift + 1e-9)), 1, n_run - 2);
  if (g.to - g.from < 4) throw FieldError("Port '" + port.name + "': die Linie ist zu kurz");

  auto at = [&](int along, int across) {  // along = Querkoordinate der Linie, across = feste Koordinate
    const int i = g.vertical ? across : along, j = g.vertical ? along : across;
    return static_cast<std::size_t>(i) + static_cast<std::size_t>(sim.nx()) * static_cast<std::size_t>(j);
  };
  auto eps_at = [&](double x, double y) {
    const double n = index_at(scene, x, y);
    return n * n;
  };
  const double line = g.fixed * dx;
  for (int k = g.from; k <= g.to; ++k) {
    if (ez) {
      // Ez am Knoten; das Querfeld (Hy bzw. Hx) liegt eine halbe Zelle davor und dahinter
      g.scalar_a.push_back(at(k, g.fixed));
      g.scalar_b.push_back(at(k, g.fixed));
      g.plane_a.push_back(at(k, g.fixed - 1));
      g.plane_b.push_back(at(k, g.fixed));
      g.eps_point.push_back(g.vertical ? eps_at(line, k * dx) : eps_at(k * dx, line));
    } else {
      // Hz in den Zellen davor und dahinter; das Querfeld (Ey bzw. Ex) liegt genau auf der Linie
      g.scalar_a.push_back(at(k, g.fixed - 1));
      g.scalar_b.push_back(at(k, g.fixed));
      g.plane_a.push_back(at(k, g.fixed));
      g.plane_b.push_back(at(k, g.fixed));
      g.eps_point.push_back(g.vertical ? eps_at(line, (k + 0.5) * dx) : eps_at((k + 0.5) * dx, line));
    }
  }
  if (!ez)
    for (int k = g.from; k <= g.to + 1; ++k)
      g.eps_between.push_back(g.vertical ? eps_at(line, k * dx) : eps_at(k * dx, line));
  return g;
}

// Mode der Portlinie bei der Vakuumwellenlänge wavelength. Die Gleichung ist genau die des Yee-Gitters für
// eine in Ausbreitungsrichtung gleichförmige Struktur: k0 und beta werden durch ihre Gitterformen ersetzt.
PortMode solve_port_mode(const PortGrid& g, Polarization polarization, double wavelength, double dx, double dt,
                         int order, const std::string& who) {
  const auto m = static_cast<Eigen::Index>(g.count());
  const double omega = omega_from_wavelength(wavelength);
  const double omega_numeric = 2.0 * std::sin(0.5 * omega * dt) / dt;
  const double k0 = omega_numeric / constants::c0;
  const double inv_h2 = 1.0 / (dx * dx);
  const bool ez = polarization == Polarization::ez;

  Eigen::VectorXd diagonal(m), off(m - 1), scale(m);
  for (Eigen::Index k = 0; k < m; ++k) {
    const double ep = g.eps_point[static_cast<std::size_t>(k)];
    if (ez) {
      diagonal(k) = -2.0 * inv_h2 + k0 * k0 * ep;
      scale(k) = 1.0;
    } else {
      const double left = 1.0 / g.eps_between[static_cast<std::size_t>(k)];
      const double right = 1.0 / g.eps_between[static_cast<std::size_t>(k + 1)];
      diagonal(k) = ep * (-(left + right) * inv_h2 + k0 * k0);
      scale(k) = std::sqrt(ep);
    }
  }
  for (Eigen::Index k = 0; k + 1 < m; ++k)
    off(k) = ez ? inv_h2 : scale(k) * scale(k + 1) * inv_h2 / g.eps_between[static_cast<std::size_t>(k + 1)];

  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver;
  solver.computeFromTridiagonal(diagonal, off);
  if (solver.info() != Eigen::Success) throw FieldError(who + "das Eigenwertproblem der Portmode ist nicht konvergiert");
  const Eigen::Index index = m - 1 - order;
  const double beta2 = index >= 0 ? solver.eigenvalues()(index) : -1.0;
  const double cladding = std::max(g.eps_point.front(), g.eps_point.back());
  char text[160];
  std::snprintf(text, sizeof text, "bei %.1f nm wird keine Mode der Ordnung %d geführt", wavelength * 1e9, order);
  if (!(beta2 > k0 * k0 * cladding)) throw FieldError(who + text);

  PortMode mode;
  mode.beta_numeric = std::sqrt(beta2);
  const double half = 0.5 * mode.beta_numeric * dx;
  if (half >= 1.0) throw FieldError(who + "das Gitter ist für diese Mode zu grob");
  const double cos_half = std::sqrt(1.0 - half * half);  // cos(beta_grid * dx / 2)
  mode.n_eff = 2.0 * std::asin(half) / dx / (omega / constants::c0);
  mode.profile.resize(static_cast<std::size_t>(m));
  double peak = 0.0;
  for (Eigen::Index k = 0; k < m; ++k) {
    const double value = scale(k) * solver.eigenvectors()(k, index);
    mode.profile[static_cast<std::size_t>(k)] = value;
    if (std::abs(value) > std::abs(peak)) peak = value;
  }
  for (double& v : mode.profile) v /= peak;
  // Die Mode muss innerhalb der Linie abklingen; sonst schneidet die Linie sie ab und das Ergebnis ist falsch
  if (std::max(std::abs(mode.profile.front()), std::abs(mode.profile.back())) > 0.05)
    throw FieldError(who + "die Linie ist zu kurz: die Mode reicht über ihre Enden hinaus (Linie verlängern)");
  for (Eigen::Index k = 0; k < m; ++k) {
    const double f = mode.profile[static_cast<std::size_t>(k)];
    mode.weight_sum += ez ? f * f : f * f / g.eps_point[static_cast<std::size_t>(k)];
  }
  if (ez) {
    mode.impedance = mode.beta_numeric * cos_half / (constants::mu0 * omega_numeric);  // Y
    mode.norm = 0.5 * mode.impedance * mode.weight_sum * dx;
  } else {
    mode.impedance = mode.beta_numeric / (omega_numeric * constants::eps0 * cos_half);  // Z0
    mode.norm = 0.5 * mode.impedance * mode.weight_sum * dx;
  }
  return mode;
}

// Laufende Fourier-Transformation entlang einer Portlinie für alle Frequenzen.
struct PortDft {
  std::vector<cplx> scalar, plane;  // [Frequenz * m + Punkt]
};

struct RunResult {
  std::vector<std::vector<cplx>> incoming, outgoing;  // [Port][Frequenz], leistungsnormiert (sqrt(W/m))
  std::size_t steps = 0;
  double time = 0.0;
};

}  // namespace

bool port_is_vertical(const FieldScene& scene, const Port& port) {
  return std::abs(port.x2 - port.x1) < 0.5 * scene.cell_size;
}

int port_inward_sign(const FieldScene& scene, const Port& port) {
  if (port_is_vertical(scene, port)) return port.x1 < 0.5 * scene.width ? +1 : -1;
  return port.y1 < 0.5 * scene.height ? +1 : -1;
}

void validate_sparameter_setup(const FieldScene& scene) {
  validate(scene);
  const SParameterSettings& s = scene.sparameters;
  if (scene.ports.empty()) throw FieldError("Für die S-Parameter fehlt mindestens ein Port");
  if (scene.ports.size() > 8) throw FieldError("Mehr als 8 Ports werden nicht unterstützt");
  if (!(s.wavelength_start > 0.0) || !(s.wavelength_stop > s.wavelength_start))
    throw FieldError("Der Wellenlängenbereich braucht Start < Stopp");
  if (s.points < 2 || s.points > 1000) throw FieldError("Zwischen 2 und 1000 Wellenlängen sind möglich");
  if (!(s.decay > 0.0 && s.decay < 1.0)) throw FieldError("Die Abklingschwelle muss zwischen 0 und 1 liegen");
  if (!(s.max_time > 0.0)) throw FieldError("Die Höchstdauer muss positiv sein");
  if (!(s.source_offset >= 2.0 * scene.cell_size))
    throw FieldError("Die Anregung braucht mindestens zwei Zellen Abstand zur Portlinie");

  const double pml = scene.pml_cells * scene.cell_size;
  auto in_pml = [&](double x, double y) {
    return (scene.boundary_x == Boundary::pml && (x < pml || x > scene.width - pml)) ||
           (scene.boundary_y == Boundary::pml && (y < pml || y > scene.height - pml));
  };
  for (std::size_t p = 0; p < scene.ports.size(); ++p) {
    const Port& port = scene.ports[p];
    const std::string who = "Port '" + port.name + "': ";
    for (std::size_t q = 0; q < p; ++q)
      if (scene.ports[q].name == port.name) throw FieldError(who + "der Name ist doppelt vergeben");
    if (in_pml(port.x1, port.y1) || in_pml(port.x2, port.y2))
      throw FieldError(who + "die Linie reicht in die Randschicht; kürzer machen oder nach innen legen");
    // Anregung hinter der Linie, nach außen versetzt
    const double back = -port_inward_sign(scene, port) * s.source_offset;
    const bool vertical = port_is_vertical(scene, port);
    const double sx = vertical ? back : 0.0, sy = vertical ? 0.0 : back;
    if (in_pml(port.x1 + sx, port.y1 + sy) || in_pml(port.x2 + sx, port.y2 + sy) ||
        port.x1 + sx < 0.0 || port.x1 + sx > scene.width || port.y1 + sy < 0.0 || port.y1 + sy > scene.height)
      throw FieldError(who + "hinter der Linie ist kein Platz für die Anregung vor der Randschicht");
  }
}

SParameterResult extract_sparameters(const FieldScene& scene, ThreadPool* pool, JobControl* control) {
  validate_sparameter_setup(scene);
  const SParameterSettings& settings = scene.sparameters;
  const std::size_t port_count = scene.ports.size();
  const bool ez = scene.polarization == Polarization::ez;

  SParameterResult result;
  // Reihenfolge: Ports der linken Seite (Richtung +x) zuerst, sonst wie angegeben
  std::vector<std::size_t> order(port_count);
  std::iota(order.begin(), order.end(), std::size_t{0});
  auto left = [&](std::size_t p) {
    return port_is_vertical(scene, scene.ports[p]) && port_inward_sign(scene, scene.ports[p]) > 0;
  };
  std::stable_partition(order.begin(), order.end(), left);
  for (std::size_t p : order) {
    result.ports.push_back(scene.ports[p].name);
    if (left(p)) ++result.left_port_count;
  }
  const auto frequencies = static_cast<std::size_t>(settings.points);
  for (std::size_t k = 0; k < frequencies; ++k)
    result.wavelengths.push_back(settings.wavelength_start +
                                 (settings.wavelength_stop - settings.wavelength_start) * static_cast<double>(k) /
                                     static_cast<double>(frequencies - 1));

  // Breitbandiger Puls: Trägerfrequenz in der Mitte des Bereichs, Spektrum an den Rändern auf etwa 1/e
  const double f_low = constants::c0 / settings.wavelength_stop, f_high = constants::c0 / settings.wavelength_start;
  const double center_wavelength = constants::c0 / (0.5 * (f_low + f_high));
  const double half_width = constants::pi * (f_high - f_low);  // halbe Breite in omega
  const double pulse_duration = 2.0 / half_width;

  // Hinweise zur Qualität
  {
    FieldScene probe = scene;
    Source source;
    source.wavelength = settings.wavelength_start;
    probe.sources = {source};
    for (const auto& w : check(probe))
      if (w.text.find("Zellen pro Wellenlänge") != std::string::npos) result.warnings.push_back(w.text);
  }

  // Gitter und Moden der Ports (für alle Anregungen gleich)
  std::vector<PortGrid> grids;
  std::vector<std::vector<PortMode>> port_modes(port_count);  // [Port][Frequenz]
  double dt = 0.0;
  {
    FieldScene empty = scene;
    empty.sources.clear();
    empty.monitors.clear();
    const Fdtd sim(empty);
    dt = sim.time_step();
    for (std::size_t p : order) {
      grids.push_back(build_port_grid(scene, sim, scene.ports[p]));
      const std::string who = "Port '" + scene.ports[p].name + "': ";
      for (double wavelength : result.wavelengths)
        port_modes[grids.size() - 1].push_back(
            solve_port_mode(grids.back(), scene.polarization, wavelength, scene.cell_size, dt, scene.ports[p].mode_order, who));
    }
  }
  for (std::size_t p = 0; p < port_count; ++p) {
    std::vector<double> n;
    for (const auto& mode : port_modes[p]) n.push_back(mode.n_eff);
    result.n_eff.push_back(std::move(n));
  }

  // Eine Rechnung je angeregtem Port
  std::vector<RunResult> runs(port_count);
  std::vector<std::atomic<float>> progress(port_count);
  for (auto& p : progress) p = 0.0f;
  std::atomic<bool> cancelled{false};
  auto report = [&] {
    if (!control) return;
    float sum = 0.0f;
    for (const auto& p : progress) sum += p.load();
    control->progress = sum / static_cast<float>(port_count);
  };

  parallel_for(
      port_count,
      [&](std::size_t run_index) {
        const Port& driven = scene.ports[order[run_index]];
        FieldScene run_scene = scene;
        run_scene.sources.clear();
        run_scene.monitors.clear();
        Source source;
        source.name = "Anregung " + driven.name;
        source.kind = SourceKind::mode;
        const double back = -port_inward_sign(scene, driven) * settings.source_offset;
        const bool vertical = port_is_vertical(scene, driven);
        source.x1 = driven.x1 + (vertical ? back : 0.0);
        source.x2 = driven.x2 + (vertical ? back : 0.0);
        source.y1 = driven.y1 + (vertical ? 0.0 : back);
        source.y2 = driven.y2 + (vertical ? 0.0 : back);
        source.signal = SignalKind::pulse;
        source.wavelength = center_wavelength;
        source.pulse_duration = pulse_duration;
        source.mode_order = driven.mode_order;
        run_scene.sources = {source};
        Fdtd sim(run_scene);

        // Fourier-Summen aller Ports
        std::vector<PortDft> dft(port_count);
        for (std::size_t p = 0; p < port_count; ++p) {
          dft[p].scalar.assign(frequencies * grids[p].count(), cplx(0.0, 0.0));
          dft[p].plane.assign(frequencies * grids[p].count(), cplx(0.0, 0.0));
        }
        std::vector<double> omega(frequencies);
        std::vector<cplx> rotation(frequencies), half_back(frequencies), phasor(frequencies);
        for (std::size_t k = 0; k < frequencies; ++k) {
          omega[k] = omega_from_wavelength(result.wavelengths[k]);
          rotation[k] = std::polar(1.0, omega[k] * dt);
          half_back[k] = std::polar(1.0, -0.5 * omega[k] * dt);  // Querfeld liegt einen halben Schritt früher
        }
        std::vector<double> scalar_values, plane_values;

        const double pulse_end = 8.0 * pulse_duration;
        const auto max_steps = static_cast<std::size_t>(std::ceil(settings.max_time / dt));
        double peak_energy = 0.0;
        std::size_t step = 0;
        while (step < max_steps) {
          sim.step();
          ++step;
          // Zeit des Skalarfeldes nach dem Schritt: step*dt; Drehzeiger alle 1024 Schritte neu, gegen Rundungsdrift
          for (std::size_t k = 0; k < frequencies; ++k)
            phasor[k] = (step & 1023u) == 0 ? std::polar(1.0, omega[k] * dt * static_cast<double>(step))
                                            : (step == 1 ? rotation[k] : phasor[k] * rotation[k]);
          const auto& f = sim.field();
          for (std::size_t p = 0; p < port_count; ++p) {
            const PortGrid& g = grids[p];
            // Querfeld: senkrechte Linie -> y-Komponente (Hy bzw. Ey), waagerechte -> x-Komponente
            const auto& transverse = g.vertical ? sim.in_plane_y() : sim.in_plane_x();
            const std::size_t m = g.count();
            scalar_values.resize(m);
            plane_values.resize(m);
            for (std::size_t c = 0; c < m; ++c) {
              scalar_values[c] = 0.5 * (f[g.scalar_a[c]] + f[g.scalar_b[c]]);
              plane_values[c] = 0.5 * (transverse[g.plane_a[c]] + transverse[g.plane_b[c]]);
            }
            for (std::size_t k = 0; k < frequencies; ++k) {
              const cplx ps = phasor[k], pp = phasor[k] * half_back[k];
              cplx* s = &dft[p].scalar[k * m];
              cplx* q = &dft[p].plane[k * m];
              for (std::size_t c = 0; c < m; ++c) {
                s[c] += scalar_values[c] * ps;
                q[c] += plane_values[c] * pp;
              }
            }
          }
          if (step % 64 == 0) {
            if (control && control->cancel_requested) {
              cancelled = true;
              return;
            }
            const double energy = sim.energy();
            peak_energy = std::max(peak_energy, energy);
            const double time = static_cast<double>(step) * dt;
            float fraction = static_cast<float>(time / settings.max_time);
            if (time > pulse_end && peak_energy > 0.0) {
              const double ratio = std::max(energy / peak_energy, 1e-300);
              fraction = std::max(fraction, static_cast<float>(std::log(ratio) / std::log(settings.decay)));
              if (energy < settings.decay * peak_energy) break;
            }
            progress[run_index] = std::min(fraction, 0.99f);
            report();
          }
        }

        // Zerlegung in ein- und auslaufende Mode an jedem Port
        RunResult& run = runs[run_index];
        run.steps = step;
        run.time = static_cast<double>(step) * dt;
        run.incoming.assign(port_count, std::vector<cplx>(frequencies));
        run.outgoing.assign(port_count, std::vector<cplx>(frequencies));
        for (std::size_t p = 0; p < port_count; ++p) {
          const PortGrid& g = grids[p];
          const std::size_t m = g.count();
          // Vorzeichen s der Querfeld-Relation für eine Welle in +Achsenrichtung (Physik-Notiz)
          const double s = ez ? (g.vertical ? -1.0 : +1.0) : (g.vertical ? +1.0 : -1.0);
          for (std::size_t k = 0; k < frequencies; ++k) {
            const PortMode& mode = port_modes[p][k];
            cplx scalar_projection = 0.0, plane_projection = 0.0;
            for (std::size_t c = 0; c < m; ++c) {
              const double fc = mode.profile[c];
              const double w = ez ? 1.0 : 1.0 / g.eps_point[c];
              scalar_projection += w * fc * dft[p].scalar[k * m + c];
              plane_projection += fc * dft[p].plane[k * m + c];
            }
            scalar_projection /= mode.weight_sum;
            // Ez: Querfeld H = s*Y*E  ->  h/Y;  Hz: Querfeld E = s*(Z0/eps)*H  ->  Projektion / Z0
            const cplx transverse = plane_projection / (mode.weight_sum * mode.impedance);
            const cplx forward = 0.5 * (scalar_projection + s * transverse);
            const cplx backward = 0.5 * (scalar_projection - s * transverse);
            const double root = std::sqrt(mode.norm);
            run.incoming[p][k] = (g.inward > 0 ? forward : backward) * root;
            run.outgoing[p][k] = (g.inward > 0 ? backward : forward) * root;
          }
        }
        progress[run_index] = 1.0f;
        report();
      },
      pool, nullptr);

  if (cancelled) {
    result.cancelled = true;
    return result;
  }
  result.s.assign(frequencies, std::vector<cplx>(port_count * port_count));
  double weakest = 1e300, strongest = 0.0;
  for (std::size_t j = 0; j < port_count; ++j) {
    result.steps += runs[j].steps;
    result.simulated_time = std::max(result.simulated_time, runs[j].time);
    for (std::size_t k = 0; k < frequencies; ++k) {
      const cplx incident = runs[j].incoming[j][k];
      weakest = std::min(weakest, std::abs(incident));
      strongest = std::max(strongest, std::abs(incident));
      for (std::size_t i = 0; i < port_count; ++i) result.s[k][i * port_count + j] = runs[j].outgoing[i][k] / incident;
    }
    if (runs[j].time >= settings.max_time * 0.999)
      result.warnings.push_back("Anregung an Port '" + result.ports[j] +
                                "': das Feld ist bis zur Höchstdauer nicht abgeklungen (Resonanz?); das Spektrum "
                                "ist abgeschnitten.");
  }
  if (weakest < 1e-3 * strongest)
    result.warnings.push_back("Das Spektrum der Anregung ist an den Rändern des Bereichs sehr schwach; dort sind "
                              "die S-Parameter ungenau.");
  return result;
}

}  // namespace photonlab::field
