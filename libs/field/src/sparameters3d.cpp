// S-Parameter einer Feld-Szene im 3D-Schichtaufbau (Gleichungen: docs/physics/sparameters.md, Abschnitt 3D).
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#include "photonlab/core/units.hpp"
#include "photonlab/field/extrude3d.hpp"

namespace photonlab::field {
namespace {

using cplx = std::complex<double>;

// Wert bei x aus drei Stützstellen (x0, x1, x2) mit quadratischer Interpolation
double quadratic(double x, const double (&xs)[3], const double (&ys)[3]) {
  double y = 0.0;
  for (int i = 0; i < 3; ++i) {
    double l = 1.0;
    for (int j = 0; j < 3; ++j)
      if (j != i) l *= (x - xs[j]) / (xs[i] - xs[j]);
    y += ys[i] * l;
  }
  return y;
}

struct Port3d {
  Extruded3d::PlaneMode mode;
  double weight = 0.0;   // Summe f²
  double n_eff[3] = {};  // bei Start, Mitte und Ende des Bereichs
  int inward = 1;
  std::ptrdiff_t offset = 0;  // Indexabstand zur zweiten Messfläche (ins Bauteil hinein)
  double distance = 0.0;      // Abstand der zweiten Fläche entlang der +Achse [m] (mit Vorzeichen)
};

}  // namespace

SParameterResult extract_sparameters_3d(const FieldScene& input, JobControl* control) {
  const FieldScene scene = active_scene(input);
  validate_sparameter_setup(scene);
  const SParameterSettings& settings = scene.sparameters;
  const std::size_t port_count = scene.ports.size();

  SParameterResult result;
  // Reihenfolge wie in 2D: Ports der linken Seite (Richtung +x) zuerst
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
                                 (settings.wavelength_stop - settings.wavelength_start) *
                                     static_cast<double>(k) / static_cast<double>(frequencies - 1));
  const double f_low = constants::c0 / settings.wavelength_stop,
               f_high = constants::c0 / settings.wavelength_start;
  const double center_wavelength = constants::c0 / (0.5 * (f_low + f_high));
  const double pulse_duration = 2.0 / (constants::pi * (f_high - f_low));
  const double sample_wavelengths[3] = {settings.wavelength_start, center_wavelength,
                                        settings.wavelength_stop};

  std::vector<Port3d> ports(port_count);
  std::vector<double> omega(frequencies);
  for (std::size_t k = 0; k < frequencies; ++k)
    omega[k] = 2.0 * constants::pi * constants::c0 / result.wavelengths[k];
  bool cancelled = false;

  // Eine Rechnung: Szene mit der Anregung rechnen, bis das Feld abgeklungen ist, an allen Ports in ein- und
  // auslaufende Mode zerlegen. Beim ersten Aufruf werden die Portmoden bestimmt.
  struct Waves {
    std::vector<cplx> incoming, outgoing;  // [Port * Frequenz]
    std::vector<cplx> far;                 // Referenz: Welle am fernen Ende der Zuleitung, je Frequenz
  };
  // far_port: nur in der Referenz, Index des angeregten Ports (Messung am fernen Ende der geraden Zuleitung)
  auto measure = [&](const FieldScene& run_scene, const std::string& driven_name, float progress_from,
                     float progress_to, int far_port) {
    Waves waves;
    Extruded3d sim(run_scene, scene.stack);
    const double dt = sim.solver().time_step(), dx = sim.solver().cell_size();
    if (ports.front().mode.cells.empty()) {
      // Moden der Ports (Geometrie ist in allen Anregungen gleich): Profil in der Mitte des Bereichs,
      // effektiver Index an drei Stellen für die Dispersion
      for (const auto& w : sim.warnings())
        if (w.find("Modenlöser") != std::string::npos) result.warnings.push_back(w);
      for (std::size_t p = 0; p < port_count; ++p) {
        const Port& port = scene.ports[order[p]];
        Port3d& q = ports[p];
        q.inward = port_inward_sign(scene, port);
        for (int s = 0; s < 3; ++s) {
          const auto mode =
              sim.plane_mode(port.x1, port.y1, port.x2, port.y2, sample_wavelengths[s], port.mode_order);
          if (!mode.guided)
            throw FieldError("Port '" + port.name +
                             "': im 3D-Aufbau wird keine Mode geführt (Linie über den "
                             "Wellenleiter legen, Schicht und Material prüfen)");
          q.n_eff[s] = mode.n_eff;
          if (s == 1) q.mode = mode;
        }
        for (double v : q.mode.profile) q.weight += v * v;
        // zweite Fläche etwa lambda/(4 n_eff) weiter ins Bauteil: dort ist die Zerlegung am besten bestimmt
        const int cells =
            std::max(1, static_cast<int>(std::lround(center_wavelength / (4.0 * q.n_eff[1] * dx))));
        q.offset = static_cast<std::ptrdiff_t>(q.inward * cells) * static_cast<std::ptrdiff_t>(q.mode.next);
        q.distance = q.inward * cells * dx;
        result.n_eff.emplace_back();
        for (double w : result.wavelengths)
          result.n_eff.back().push_back(quadratic(w, sample_wavelengths, q.n_eff));
      }
    }

    // Messfläche am fernen Ende der geraden Zuleitung: gespiegelt zur Portfläche, gleiches Profil
    std::vector<Port3d> planes = ports;
    if (far_port >= 0) {
      Port3d far = ports[static_cast<std::size_t>(far_port)];
      const Fdtd3d& f = sim.solver();
      const int n_axis = far.mode.normal_x ? f.nx() : f.ny();
      const auto first = far.mode.cells.front();
      const int fixed = far.mode.normal_x ? static_cast<int>(first % static_cast<std::size_t>(f.nx()))
                                          : static_cast<int>((first / static_cast<std::size_t>(f.nx())) %
                                                             static_cast<std::size_t>(f.ny()));
      const int mirrored = n_axis - 1 - fixed;
      const auto shift =
          static_cast<std::ptrdiff_t>(mirrored - fixed) * static_cast<std::ptrdiff_t>(far.mode.next);
      for (auto& cell : far.mode.cells)
        cell = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(cell) + shift);
      // zweite Fläche zur Mitte hin (weg von der Randschicht)
      far.offset = -far.offset, far.distance = -far.distance;
      planes.push_back(far);
    }
    const std::size_t plane_count = planes.size();

    // Fourier-Summen der Projektionen von E auf die Portmode, an der Portfläche und an der zweiten Fläche
    std::vector<cplx> first_sum(plane_count * frequencies), second_sum(plane_count * frequencies);
    std::size_t step = 0;
    auto accumulate = [&](const Fdtd3d& f) {
      ++step;
      const double t = static_cast<double>(step) * dt;
      for (std::size_t p = 0; p < plane_count; ++p) {
        const Port3d& q = planes[p];
        const auto& e = f.component(q.mode.e);
        double first = 0.0, second = 0.0;
        for (std::size_t c = 0; c < q.mode.cells.size(); ++c) {
          const std::size_t cell = q.mode.cells[c];
          const double w = q.mode.profile[c];
          first += w * e[cell];
          second += w * e[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(cell) + q.offset)];
        }
        for (std::size_t k = 0; k < frequencies; ++k) {
          const cplx phasor = std::polar(1.0, omega[k] * t);
          first_sum[p * frequencies + k] += first * phasor;
          second_sum[p * frequencies + k] += second * phasor;
        }
      }
    };
    const double pulse_end = 8.0 * pulse_duration;
    const auto max_steps = static_cast<std::size_t>(std::ceil(settings.max_time / dt));
    double peak_energy = 0.0;
    bool decayed = false;
    while (step < max_steps) {
      sim.run(64, control, accumulate);
      if (control && control->cancel_requested) {
        cancelled = true;
        return waves;
      }
      const double energy = sim.energy();
      peak_energy = std::max(peak_energy, energy);
      const double time = static_cast<double>(step) * dt;
      float fraction = static_cast<float>(time / settings.max_time);
      if (time > pulse_end && peak_energy > 0.0) {
        fraction = std::max(fraction, static_cast<float>(std::log(std::max(energy / peak_energy, 1e-300)) /
                                                         std::log(settings.decay)));
        if (energy < settings.decay * peak_energy) {
          decayed = true;
          break;
        }
      }
      if (control)
        control->progress = progress_from + (progress_to - progress_from) * std::min(fraction, 0.99f);
    }
    result.steps += step;
    result.simulated_time = std::max(result.simulated_time, static_cast<double>(step) * dt);
    if (!decayed)
      result.warnings.push_back("Anregung an Port '" + driven_name +
                                "': das Feld ist bis zur Höchstdauer nicht abgeklungen; das Spektrum ist "
                                "abgeschnitten.");

    // Zerlegung mit zwei Flächen: E(x) = A+ exp(i beta (x - x0)) + A- exp(-i beta (x - x0)) mit der
    // Ausbreitungskonstante des Gitters, beta_g = 2 asin(beta dx / 2) / dx (beta aus dem Modenlöser)
    waves.incoming.resize(port_count * frequencies);
    waves.outgoing.resize(port_count * frequencies);
    waves.far.resize(frequencies);
    for (std::size_t p = 0; p < plane_count; ++p) {
      const Port3d& q = planes[p];
      const std::size_t mode_of = p < port_count ? p : static_cast<std::size_t>(far_port);
      for (std::size_t k = 0; k < frequencies; ++k) {
        const double beta = 2.0 * constants::pi * result.n_eff[mode_of][k] / result.wavelengths[k];
        const double beta_grid = 2.0 * std::asin(std::min(0.5 * beta * dx, 1.0)) / dx;
        const cplx e0 = first_sum[p * frequencies + k] / q.weight,
                   e1 = second_sum[p * frequencies + k] / q.weight;
        const cplx ahead = std::polar(1.0, beta_grid * q.distance), behind = std::conj(ahead);
        const cplx backward = (e1 - e0 * ahead) / (behind - ahead);
        const cplx forward = e0 - backward;
        // Leistung je |a|² für die Normierung zwischen verschiedenen Ports: n_eff/(2 Z0) * Summe f² * Fläche
        const double root =
            std::sqrt(0.5 * result.n_eff[mode_of][k] * constants::eps0 * constants::c0 * q.weight * dx * dx);
        if (p >= port_count) {
          waves.far[k] = (q.inward > 0 ? forward : backward) * root;  // läuft weiter in Richtung des Ports
          continue;
        }
        waves.incoming[p * frequencies + k] = (q.inward > 0 ? forward : backward) * root;
        waves.outgoing[p * frequencies + k] = (q.inward > 0 ? backward : forward) * root;
      }
    }
    return waves;
  };

  // Je Port: Rechnung mit dem Bauteil und eine Referenz mit der geraden Zuleitung (Querschnitt unter der
  // Portlinie über das ganze Gebiet). Die Referenz liefert die einlaufende Welle ohne die Störung der nahen
  // Quelle; ihre scheinbare Reflexion wird von der des Bauteils abgezogen.
  std::vector<Waves> device(port_count), reference(port_count);
  for (std::size_t run = 0; run < port_count; ++run) {
    const Port& driven = scene.ports[order[run]];
    FieldScene run_scene = scene;
    run_scene.monitors.clear();
    Source source;
    source.name = "Anregung " + driven.name;
    source.kind = SourceKind::mode;
    const double back = -port_inward_sign(scene, driven) * settings.source_offset;
    const bool vertical = port_is_vertical(scene, driven);
    source.x1 = driven.x1 + (vertical ? back : 0.0), source.x2 = driven.x2 + (vertical ? back : 0.0);
    source.y1 = driven.y1 + (vertical ? 0.0 : back), source.y2 = driven.y2 + (vertical ? 0.0 : back);
    source.signal = SignalKind::pulse;
    source.wavelength = center_wavelength;
    source.pulse_duration = pulse_duration;
    source.mode_order = driven.mode_order;
    run_scene.sources = {source};
    const float share = 1.0f / static_cast<float>(port_count);
    device[run] = measure(run_scene, driven.name, share * static_cast<float>(run),
                          share * (static_cast<float>(run) + 0.6f), -1);
    if (cancelled) break;

    // gerade Zuleitung: Abschnitte gleicher Brechzahl entlang der ganzen Portachse
    FieldScene straight = run_scene;
    straight.shapes.clear();
    straight.groups.clear();
    const double length = vertical ? scene.height : scene.width, step = 0.25 * scene.cell_size;
    double start = 0.0,
           current = vertical ? index_at(scene, driven.x1, 0.0) : index_at(scene, 0.0, driven.y1);
    for (double t = step; t <= length + 0.5 * step; t += step) {
      const double n = t > length ? -1.0
                       : vertical ? index_at(scene, driven.x1, t)
                                  : index_at(scene, t, driven.y1);
      if (n == current) continue;
      if (current != scene.background_index) {
        Shape piece;
        piece.name = "Zuleitung";
        piece.index = current;
        piece.x = vertical ? 0.5 * scene.width : 0.5 * (start + t);
        piece.y = vertical ? 0.5 * (start + t) : 0.5 * scene.height;
        piece.width = vertical ? scene.width : t - start;
        piece.height = vertical ? t - start : scene.height;
        straight.shapes.push_back(piece);
      }
      start = t, current = n;
    }
    reference[run] = measure(straight, driven.name, share * (static_cast<float>(run) + 0.6f),
                             share * (static_cast<float>(run) + 1.0f), static_cast<int>(run));
    if (cancelled) break;
  }
  if (cancelled) {
    result.cancelled = true;
    return result;
  }

  result.s.assign(frequencies, std::vector<cplx>(port_count * port_count));
  double weakest = 1e300, strongest = 0.0;
  for (std::size_t j = 0; j < port_count; ++j)
    for (std::size_t k = 0; k < frequencies; ++k) {
      // Betrag der einlaufenden Welle vom fernen Ende der Zuleitung (dort ist das Feld eine reine Mode, die
      // Zuleitung ist verlustfrei), Phase an der Portfläche
      const cplx near = reference[j].incoming[j * frequencies + k];
      const cplx incident = std::polar(std::abs(reference[j].far[k]), std::arg(near));
      weakest = std::min(weakest, std::abs(incident));
      strongest = std::max(strongest, std::abs(incident));
      // Ports auf der Seite der Quelle (gleiche Lage und Richtung wie der angeregte): abzüglich dessen, was
      // die Referenz dort sieht (Streulicht der nahen Quelle, scheinbare Reflexion). Auf der Gegenseite trägt
      // die Referenz das durchgehende Licht der Zuleitung, dort wird nichts abgezogen.
      for (std::size_t i = 0; i < port_count; ++i) {
        const bool source_side =
            ports[i].mode.normal_x == ports[j].mode.normal_x && ports[i].inward == ports[j].inward;
        cplx out = device[j].outgoing[i * frequencies + k];
        if (source_side) out -= reference[j].outgoing[i * frequencies + k];
        result.s[k][i * port_count + j] = out / incident;
      }
    }
  if (weakest < 1e-3 * strongest)
    result.warnings.push_back(
        "Das Spektrum der Anregung ist an den Rändern des Bereichs sehr schwach; dort sind die S-Parameter "
        "ungenau.");
  if (control) control->progress = 1.0f;
  return result;
}

}  // namespace photonlab::field
