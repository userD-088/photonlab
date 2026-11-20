#include "photonlab/field/extrude3d.hpp"

#include <algorithm>
#include <cmath>

#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/gpu.hpp"
#include "photonlab/materials/material.hpp"
#include "photonlab/modes/fde.hpp"
#include "photonlab/modes/profile.hpp"
#include "photonlab/modes/slab.hpp"

namespace photonlab::field {
namespace {

double material_index(const std::string& name, double wavelength) {
  if (name == "Luft" || name == "air") return 1.0;
  try {
    return materials::get(name).index(wavelength);
  } catch (const std::exception& e) {
    throw FieldError(std::string("3D-Schichtaufbau: ") + e.what());
  }
}

int pml_cells_3d(const FieldScene& scene, double dx) {
  // nicht dicker als die Randschicht der 2D-Szene, damit Quellen und Monitore außerhalb bleiben
  const double pml_2d = scene.pml_cells * scene.cell_size;
  return std::max(5, static_cast<int>(std::floor(pml_2d / dx + 1e-9)));
}

double first_wavelength(const FieldScene& scene) {
  return scene.sources.empty() ? 1550e-9 : scene.sources.front().wavelength;
}

}  // namespace

StackIndices stack_indices(const Stack3d& stack, double wavelength) {
  return {material_index(stack.core, wavelength), material_index(stack.substrate, wavelength),
          material_index(stack.cladding, wavelength)};
}

Extent3d extent_3d(const FieldScene& scene, const Stack3d& stack) {
  Extent3d e;
  const double dx = stack.cell_size;
  if (!(dx > 0.0) || scene.empty()) return e;
  e.pml_cells = pml_cells_3d(scene, dx);
  e.nx = static_cast<int>(std::lround(scene.width / dx));
  e.ny = static_cast<int>(std::lround(scene.height / dx));
  e.nz = static_cast<int>(std::lround((stack.below + stack.thickness + stack.above) / dx)) + 2 * e.pml_cells;
  e.time_step = 0.99 * dx / (constants::c0 * std::sqrt(3.0));
  e.steps = scene.duration > 0.0 ? static_cast<std::size_t>(std::ceil(scene.duration / e.time_step)) : 0;
  // 6 Felder, 3 Koeffizienten, dazu die Hilfsfelder der Randschicht (grob)
  e.memory_bytes = static_cast<double>(e.nx) * e.ny * e.nz * 8.0 * 9.0 * 1.2;
  return e;
}

Extruded3d::Extruded3d(const FieldScene& input, const Stack3d& stack)
    : scene_(active_scene(input)), stack_(stack) {
  validate(scene_);
  if (!(stack.thickness > 0.0) || !(stack.below >= 0.0) || !(stack.above >= 0.0))
    throw FieldError("3D-Schichtaufbau: Dicke muss positiv sein, Abstände dürfen nicht negativ sein");
  const double wavelength = first_wavelength(scene_);
  indices_ = stack_indices(stack, wavelength);
  const Extent3d e = extent_3d(scene_, stack);
  const double dx = stack.cell_size;
  const int pml = e.pml_cells;
  pml_ = pml;
  layer_z0_ = pml * dx + stack.below;
  layer_z1_ = layer_z0_ + stack.thickness;

  for (const auto& s : scene_.shapes)
    if (s.n2 != 0.0 || s.beta_tpa != 0.0 || s.carrier_lifetime > 0.0) {
      warnings_.push_back("Nichtlineare Formen rechnen in 3D linear (Kerr, TPA nur im 2D-FDTD).");
      break;
    }
  if (scene_.boundary_x == Boundary::periodic || scene_.boundary_y == Boundary::periodic)
    warnings_.push_back("Periodische Ränder gibt es in 3D nicht; dort liegt eine Randschicht.");

  Scene3d s3;
  s3.name = scene_.name + " (3D)";
  s3.size_x = e.nx * dx, s3.size_y = e.ny * dx, s3.size_z = e.nz * dx;
  s3.cell_size = dx;
  s3.background_index = indices_.cladding;
  s3.pml_cells = pml;
  // Brechzahl in der Schicht auf einem Gitter mit halbem Zellabstand, versetzt um eine Viertelzelle: genau
  // dort tastet Fdtd3d die Zellen ab (Mittel über 2x2x2 Punkte im Abstand dx/4)
  const int mx = 2 * e.nx + 2, my = 2 * e.ny + 2;
  std::vector<float> map(static_cast<std::size_t>(mx) * static_cast<std::size_t>(my));
  for (int q = 0; q < my; ++q)
    for (int p = 0; p < mx; ++p)
      map[static_cast<std::size_t>(p) + static_cast<std::size_t>(mx) * static_cast<std::size_t>(q)] =
          static_cast<float>(index_in_layer((0.5 * p - 0.25) * dx, (0.5 * q - 0.25) * dx));
  const double z0 = layer_z0_, z1 = layer_z1_, n_sub = indices_.substrate, n_clad = indices_.cladding;
  s3.index_function = [map = std::move(map), mx, my, dx, z0, z1, n_sub, n_clad](double x, double y,
                                                                                double z) {
    if (z < z0) return n_sub;
    if (z > z1) return n_clad;
    const int p = std::clamp(static_cast<int>(std::lround(2.0 * x / dx + 0.5)), 0, mx - 1);
    const int q = std::clamp(static_cast<int>(std::lround(2.0 * y / dx + 0.5)), 0, my - 1);
    return static_cast<double>(
        map[static_cast<std::size_t>(p) + static_cast<std::size_t>(mx) * static_cast<std::size_t>(q)]);
  };
  fdtd_ = std::make_unique<Fdtd3d>(s3);
  const Fdtd3d& f = *fdtd_;
  layer_k_ = std::clamp(static_cast<int>(0.5 * (z0 + z1) / dx), 0, f.nz() - 1);
  period_steps_ = std::max<std::size_t>(
      1, static_cast<std::size_t>(std::lround(wavelength / constants::c0 / f.time_step())));

  const bool ez = scene_.polarization == Polarization::ez;
  // Hz-Szenen (E in der Ebene, TE-artig auf dem Chip): Hauptkomponente Ey; Ez-Szenen: Ez
  main_ = ez ? Component3d::ez : Component3d::ey;

  // Profil der Schichtmode senkrecht zur Schicht: Hz-Szenen TE (E parallel zur Schicht), Ez-Szenen TM
  const auto vertical_pol = ez ? modes::SlabPolarization::tm : modes::SlabPolarization::te;
  std::vector<double> profile_z(static_cast<std::size_t>(f.nz()), 0.0);
  {
    const auto slab = modes::slab_modes(indices_.core, indices_.substrate, indices_.cladding, stack.thickness,
                                        wavelength, vertical_pol);
    for (int k = 0; k < f.nz(); ++k) {
      const double z = (k + (ez ? 0.5 : 0.0)) * dx;
      const double n = z < z0 ? indices_.substrate : z > z1 ? indices_.cladding : indices_.core;
      if (slab.empty()) {
        profile_z[static_cast<std::size_t>(k)] = z >= z0 && z <= z1 ? 1.0 : 0.0;
        continue;
      }
      double g = modes::slab_field(slab.front(), z - z0, indices_.core, indices_.substrate, indices_.cladding,
                                   stack.thickness, wavelength, vertical_pol);
      if (ez) g /= n * n;  // TM: E senkrecht zur Schicht ~ H / n²
      profile_z[static_cast<std::size_t>(k)] = std::abs(g) < 1e-3 ? 0.0 : g;
    }
    if (slab.empty())
      warnings_.push_back("Die Schicht führt keine Mode; Quellen speisen gleichmäßig über die Dicke.");
  }

  auto cell_index = [&](double v, int n) {
    return std::clamp(static_cast<int>(std::floor(v / dx)), 0, n - 1);
  };
  auto check_inside = [&](int i, int j, const std::string& who) {
    if (i < pml || j < pml || i >= f.nx() - pml || j >= f.ny() - pml)
      throw FieldError(who + " liegt in der Randschicht der 3D-Rechnung (" + std::to_string(pml) +
                       " Zellen zu " +
                       std::to_string(static_cast<int>(std::lround(stack.cell_size / units::nm))) + " nm)");
  };
  // Linien enden spätestens am Rand der Randschicht (der Teil darin bleibt weg)
  auto inside_x = [&](int i) { return std::clamp(i, pml, f.nx() - 1 - pml); };
  auto inside_y = [&](int j) { return std::clamp(j, pml, f.ny() - 1 - pml); };

  for (const auto& source : scene_.sources) {
    if (!source.trigger.empty()) {
      warnings_.push_back("Regenerator '" + source.name + "' rechnet nur in 2D und bleibt in 3D aus.");
      continue;
    }
    const std::string who = "Quelle '" + source.name + "'";
    Source3d s;
    s.signal = source;
    const bool vertical = std::abs(source.x2 - source.x1) < 0.5 * dx;
    if (source.kind == SourceKind::point) {
      const int i = cell_index(source.x1, f.nx()), j = cell_index(source.y1, f.ny());
      check_inside(i, j, who);
      s.component = main_;
      s.cells = {f.at(i, j, layer_k_)};
      s.weights = {2.0 * constants::c0 * f.time_step() / dx * source.amplitude / indices_.core};
      fdtd_->add_source(std::move(s));
      continue;
    }
    // Linie: Zellen quer, Profil quer (Mode wie in 2D bzw. gleichmäßig/Gauß) mal Profil senkrecht
    s.component = ez ? Component3d::ez : (vertical ? Component3d::ey : Component3d::ex);
    int i1 = cell_index(std::min(source.x1, source.x2), f.nx()),
        i2 = cell_index(std::max(source.x1, source.x2), f.nx());
    int j1 = cell_index(std::min(source.y1, source.y2), f.ny()),
        j2 = cell_index(std::max(source.y1, source.y2), f.ny());
    if (vertical)
      check_inside(i1, inside_y(j1), who), j1 = inside_y(j1), j2 = inside_y(j2);
    else
      check_inside(inside_x(i1), j1, who), i1 = inside_x(i1), i2 = inside_x(i2);
    const int count = vertical ? j2 - j1 + 1 : i2 - i1 + 1;
    std::vector<double> lateral(static_cast<std::size_t>(count), 1.0);
    double n_eff = indices_.core;
    // Lage der eingespeisten Komponente quer zur Linie: Ey bei (j + 1/2), Ex bei (i + 1/2), Ez bei j bzw. i
    const double half = ez ? 0.0 : 0.5;
    // Modenquelle: echte Mode des Querschnitts unter der Linie (Modenlöser auf dem 3D-Gitter); nur wenn
    // er keine geführte Mode findet, das getrennte Profil (Mode in der Ebene mal Schichtmode)
    std::vector<double> section;  // Feld je (c, k) außerhalb der Randschicht in z, c schnell
    const int k_lo = pml, k_hi = f.nz() - pml;
    if (source.kind == SourceKind::mode) {
      const PlaneMode mode =
          plane_mode(source.x1, source.y1, source.x2, source.y2, source.wavelength, source.mode_order);
      if (mode.guided && mode.count == count && mode.rows == k_hi - k_lo) {
        section = mode.profile;
        n_eff = mode.n_eff;
      } else {
        warnings_.push_back(
            who + ": Modenlöser findet keine geführte 3D-Mode; Profil aus 2D-Mode mal Schichtmode.");
      }
    }
    if (source.kind == SourceKind::mode && section.empty()) {
      std::vector<double> index_2d(static_cast<std::size_t>(count));
      for (int c = 0; c < count; ++c) {
        const double t = ((vertical ? j1 : i1) + c + half) * dx;
        index_2d[static_cast<std::size_t>(c)] =
            vertical ? index_at(scene_, source.x1, t) : index_at(scene_, t, source.y1);
      }
      std::vector<modes::ProfileMode> found;
      try {
        found = modes::profile_modes(index_2d, dx, source.wavelength,
                                     ez ? modes::SlabPolarization::te : modes::SlabPolarization::tm,
                                     source.mode_order + 1);
      } catch (const modes::ModeError& error) {
        throw FieldError(who + ": " + error.what());
      }
      if (static_cast<int>(found.size()) <= source.mode_order)
        throw FieldError(who + ": unter der Linie wird keine Mode geführt (3D-Gitter " +
                         std::to_string(static_cast<int>(stack.cell_size / units::nm)) + " nm)");
      const auto& mode = found[static_cast<std::size_t>(source.mode_order)];
      n_eff = mode.n_eff;
      for (int c = 0; c < count; ++c) {
        const double n = index_2d[static_cast<std::size_t>(c)];
        // Hz-Szenen: das Profil ist H (TM in der Ebene), das eingespeiste E quer dazu ~ H / n²
        lateral[static_cast<std::size_t>(c)] =
            ez ? mode.field[static_cast<std::size_t>(c)] : mode.field[static_cast<std::size_t>(c)] / (n * n);
      }
      const double peak = *std::max_element(lateral.begin(), lateral.end(),
                                            [](double a, double b) { return std::abs(a) < std::abs(b); });
      for (double& v : lateral) v /= std::abs(peak) > 0.0 ? std::abs(peak) : 1.0;
    } else if (source.beam_width > 0.0) {
      const double center = vertical ? 0.5 * (source.y1 + source.y2) : 0.5 * (source.x1 + source.x2);
      for (int c = 0; c < count; ++c) {
        const double t = ((vertical ? j1 : i1) + c + half) * dx;
        const double d = (t - center) / source.beam_width;
        lateral[static_cast<std::size_t>(c)] = std::exp(-d * d);
      }
    }
    // einseitig wie in 2D: zweite Schicht d Zellen in Strahlrichtung mit der Phase pi + k*d
    int shift = 0;
    double scale = 1.0;
    double extra_phase = 0.0;
    if (source.direction != 0 && source.kind == SourceKind::mode) {
      shift = std::max(1, static_cast<int>(std::lround(source.wavelength / (4.0 * n_eff * dx))));
      const double kd = 2.0 * constants::pi * n_eff / source.wavelength * shift * dx;
      extra_phase = constants::pi + kd;
      scale = 1.0 / (2.0 * std::abs(std::sin(kd)));
      shift *= source.direction;
    }
    const int i0 = cell_index(source.x1, f.nx()), j0 = cell_index(source.y1, f.ny());
    Source3d second = s;
    second.signal.phase += extra_phase;
    for (int k = 0; k < f.nz(); ++k) {
      const double g =
          section.empty() ? profile_z[static_cast<std::size_t>(k)] : (k >= k_lo && k < k_hi ? 1.0 : 0.0);
      if (g == 0.0) continue;
      for (int c = 0; c < count; ++c) {
        const double p = section.empty()
                             ? lateral[static_cast<std::size_t>(c)]
                             : section[static_cast<std::size_t>(c) +
                                       static_cast<std::size_t>(count) * static_cast<std::size_t>(k - k_lo)];
        if (std::abs(p) < 1e-4) continue;
        const int i = vertical ? i0 : i1 + c, j = vertical ? j1 + c : j0;
        const std::size_t cell = f.at(i, j, k);
        const double eps = f.permittivity(s.component, cell);
        const double weight =
            2.0 * n_eff * constants::c0 * f.time_step() / (eps * dx) * source.amplitude * scale * p * g;
        s.cells.push_back(cell);
        s.weights.push_back(weight);
        if (shift != 0) {
          const int i_2 = vertical ? i + shift : i, j_2 = vertical ? j : j + shift;
          check_inside(i_2, j_2, who + " (zweite Schicht)");
          second.cells.push_back(f.at(i_2, j_2, k));
          second.weights.push_back(weight);
        }
      }
    }
    fdtd_->add_source(std::move(s));
    if (shift != 0) fdtd_->add_source(std::move(second));
  }

  bool mode_warned = false;
  for (const auto& monitor : scene_.monitors) {
    const std::size_t index = monitors_.size();
    Monitor3dData data;
    data.name = monitor.name;
    const std::string who = "Monitor '" + monitor.name + "'";
    if (monitor.kind == MonitorKind::point) {
      const int i = cell_index(monitor.x1, f.nx()), j = cell_index(monitor.y1, f.ny());
      probes_.push_back({index, f.at(i, j, layer_k_)});
      monitors_.push_back(std::move(data));
      continue;
    }
    if (monitor.kind == MonitorKind::mode && !mode_warned) {
      warnings_.push_back("Modenmonitore messen in 3D den ganzen Leistungsfluss durch ihre Fläche.");
      mode_warned = true;
    }
    data.flux = true;
    FluxPlane plane;
    plane.monitor = index;
    plane.normal_x = std::abs(monitor.x2 - monitor.x1) < 0.5 * dx;
    const int i1 = cell_index(std::min(monitor.x1, monitor.x2), f.nx() - 1),
              i2 = cell_index(std::max(monitor.x1, monitor.x2), f.nx() - 1);
    const int j1 = cell_index(std::min(monitor.y1, monitor.y2), f.ny() - 1),
              j2 = cell_index(std::max(monitor.y1, monitor.y2), f.ny() - 1);
    for (int k = pml; k < f.nz() - pml; ++k)
      for (int j = j1; j <= (plane.normal_x ? j2 : j1); ++j)
        for (int i = i1; i <= (plane.normal_x ? i1 : i2); ++i) plane.cells.push_back(f.at(i, j, k));
    if (plane.cells.empty()) throw FieldError(who + ": Fläche ohne Zellen");
    planes_.push_back(std::move(plane));
    monitors_.push_back(std::move(data));
  }
  // Werte je Schritt für record: je Flusszelle sechs Komponenten, danach die Punkte
  const auto row = static_cast<std::size_t>(f.nx());
  using C = Component3d;
  for (const FluxPlane& plane : planes_)
    for (std::size_t c : plane.cells) {
      if (plane.normal_x)  // Sx = Ey Hz - Ez Hy an (i + 1/2)
        probe_list_.insert(probe_list_.end(),
                           {{C::ey, c}, {C::ey, c + 1}, {C::hz, c}, {C::ez, c}, {C::ez, c + 1}, {C::hy, c}});
      else  // Sy = Ez Hx - Ex Hz an (j + 1/2)
        probe_list_.insert(
            probe_list_.end(),
            {{C::ez, c}, {C::ez, c + row}, {C::hx, c}, {C::ex, c}, {C::ex, c + row}, {C::hz, c}});
    }
  for (const Probe& probe : probes_) probe_list_.push_back({main_, probe.cell});
  if (gpu::enabled()) fdtd_->use_gpu(true);
}

double Extruded3d::index_in_layer(double x, double y) const {
  // oberste Form gewinnt (wie index_at); außerhalb der Formen der Hintergrund der 2D-Szene
  double n = -1.0;
  for (auto it = scene_.shapes.rbegin(); it != scene_.shapes.rend(); ++it)
    if (contains(*it, x, y)) {
      n = it->index;
      break;
    }
  if (n < 0.0) return scene_.background_index >= stack_.core_threshold ? indices_.core : indices_.cladding;
  if (n >= stack_.core_threshold) return indices_.core;
  if (std::abs(n - scene_.background_index) < 1e-6) return indices_.cladding;
  return n;  // Luftlöcher (1,0), Oxid, Nitrid ... bleiben, wie sie sind
}

std::size_t Extruded3d::steps_for(double duration) const {
  return duration > 0.0 ? static_cast<std::size_t>(std::ceil(duration / fdtd_->time_step())) : 0;
}

void Extruded3d::run(std::size_t steps, JobControl* control,
                     const std::function<void(const Fdtd3d&)>& each_step) {
  if (each_step) fdtd_->use_gpu(false);  // braucht nach jedem Schritt alle Felder
  const std::size_t m = probe_list_.size();
  std::vector<double> values;
  for (std::size_t done = 0; done < steps;) {
    const std::size_t n = each_step ? 1 : std::min<std::size_t>(256, steps - done);
    const std::size_t before = fdtd_->step_count();
    values.clear();
    fdtd_->run_probed(n, probe_list_, &values, control);
    const std::size_t ran = fdtd_->step_count() - before;
    for (std::size_t s = 0; s < ran; ++s) record(values.data() + s * m);
    if (ran < n) return;  // abgebrochen
    if (each_step) each_step(*fdtd_);
    done += n;
  }
}

Extruded3d::PlaneMode Extruded3d::plane_mode(double x1, double y1, double x2, double y2, double wavelength,
                                             int order) const {
  const Fdtd3d& f = *fdtd_;
  const double dx = f.cell_size();
  const bool ez = scene_.polarization == Polarization::ez;
  PlaneMode m;
  m.normal_x = std::abs(x2 - x1) < 0.5 * dx;
  // dominante E-Komponente und das H für den Fluss (S_x = Ey Hz - Ez Hy, S_y = Ez Hx - Ex Hz)
  if (m.normal_x) {
    m.e = ez ? Component3d::ez : Component3d::ey;
    m.h = ez ? Component3d::hy : Component3d::hz;
    m.sign = ez ? -1.0 : 1.0;
    m.next = 1;
  } else {
    m.e = ez ? Component3d::ez : Component3d::ex;
    m.h = ez ? Component3d::hx : Component3d::hz;
    m.sign = ez ? 1.0 : -1.0;
    m.next = static_cast<std::size_t>(f.nx());
  }
  auto cell = [&](double v, int n) { return std::clamp(static_cast<int>(std::floor(v / dx)), 0, n - 1); };
  const int fixed =
      m.normal_x ? std::min(cell(x1, f.nx()), f.nx() - 2) : std::min(cell(y1, f.ny()), f.ny() - 2);
  const int n_run = m.normal_x ? f.ny() : f.nx();
  const double lo = m.normal_x ? std::min(y1, y2) : std::min(x1, x2),
               hi = m.normal_x ? std::max(y1, y2) : std::max(x1, x2);
  const int from = std::clamp(cell(lo, n_run), pml_, n_run - 1 - pml_),
            to = std::clamp(cell(hi, n_run), pml_, n_run - 1 - pml_);
  const int k_lo = pml_, k_hi = f.nz() - pml_;
  m.count = to - from + 1, m.rows = k_hi - k_lo;
  if (m.count < 3 || m.rows < 3) return m;
  modes::FdeGrid grid;
  grid.nx = m.count, grid.ny = m.rows;
  grid.dx = grid.dy = dx;
  for (int k = k_lo; k < k_hi; ++k)
    for (int c = from; c <= to; ++c) {
      const std::size_t index = m.normal_x ? f.at(fixed, c, k) : f.at(c, fixed, k);
      m.cells.push_back(index);
      grid.index.push_back(std::sqrt(f.permittivity(m.e, index)));
    }
  modes::FdeSettings settings;
  settings.wavelength = wavelength;
  settings.polarization = ez ? modes::FdePolarization::quasi_tm : modes::FdePolarization::quasi_te;
  settings.modes = order + 1;
  try {
    const auto found = modes::solve_modes(grid, settings);
    if (static_cast<int>(found.size()) > order && found[static_cast<std::size_t>(order)].guided) {
      m.profile = found[static_cast<std::size_t>(order)].field;
      m.n_eff = found[static_cast<std::size_t>(order)].n_eff;
      m.guided = true;
    }
  } catch (const modes::ModeError&) {
  }
  return m;
}

void Extruded3d::record(const double* v) {
  const double area = fdtd_->cell_size() * fdtd_->cell_size();
  for (const FluxPlane& plane : planes_) {
    double sum = 0.0;
    for (std::size_t n = 0; n < plane.cells.size(); ++n, v += 6)
      sum += 0.5 * (v[0] + v[1]) * v[2] - 0.5 * (v[3] + v[4]) * v[5];
    monitors_[plane.monitor].value.push_back(sum * area);
  }
  for (const Probe& probe : probes_) monitors_[probe.monitor].value.push_back(*v++);
  for (auto& m : monitors_) {
    const std::size_t n = std::min(period_steps_, m.value.size());
    double sum = 0.0;
    for (std::size_t k = m.value.size() - n; k < m.value.size(); ++k)
      sum += m.flux ? m.value[k] : m.value[k] * m.value[k];
    m.mean =
        n > 0 ? (m.flux ? sum / static_cast<double>(n) : std::sqrt(2.0 * sum / static_cast<double>(n))) : 0.0;
    m.mean_history.push_back(m.mean);
  }
}

std::vector<float> Extruded3d::top_view(bool intensity) const {
  const Fdtd3d& f = *fdtd_;
  std::vector<float> out(static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(f.ny()));
  for (int j = 0; j < f.ny(); ++j)
    for (int i = 0; i < f.nx(); ++i) {
      const std::size_t c = f.at(i, j, layer_k_);
      double v = f.component(main_)[c];
      if (intensity) {
        const double ex = f.component(Component3d::ex)[c], ey = f.component(Component3d::ey)[c],
                     ez = f.component(Component3d::ez)[c];
        v = ex * ex + ey * ey + ez * ez;
      }
      out[static_cast<std::size_t>(i) + static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(j)] =
          static_cast<float>(v);
    }
  return out;
}

std::vector<float> Extruded3d::side_view(double y, bool intensity) const {
  const Fdtd3d& f = *fdtd_;
  const int j = std::clamp(static_cast<int>(y / f.cell_size()), 0, f.ny() - 1);
  std::vector<float> out(static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(f.nz()));
  for (int k = 0; k < f.nz(); ++k)
    for (int i = 0; i < f.nx(); ++i) {
      const std::size_t c = f.at(i, j, k);
      double v = f.component(main_)[c];
      if (intensity) {
        const double ex = f.component(Component3d::ex)[c], ey = f.component(Component3d::ey)[c],
                     ez = f.component(Component3d::ez)[c];
        v = ex * ex + ey * ey + ez * ez;
      }
      out[static_cast<std::size_t>(i) + static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(k)] =
          static_cast<float>(v);
    }
  return out;
}

std::vector<float> Extruded3d::top_index() const {
  const Fdtd3d& f = *fdtd_;
  std::vector<float> out(static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(f.ny()));
  for (int j = 0; j < f.ny(); ++j)
    for (int i = 0; i < f.nx(); ++i)
      out[static_cast<std::size_t>(i) + static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(j)] =
          static_cast<float>(index_in_layer((i + 0.5) * f.cell_size(), (j + 0.5) * f.cell_size()));
  return out;
}

std::vector<float> Extruded3d::side_index(double y) const {
  const Fdtd3d& f = *fdtd_;
  std::vector<float> out(static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(f.nz()));
  for (int k = 0; k < f.nz(); ++k)
    for (int i = 0; i < f.nx(); ++i) {
      const double z = (k + 0.5) * f.cell_size();
      const double n = z < layer_z0_   ? indices_.substrate
                       : z > layer_z1_ ? indices_.cladding
                                       : index_in_layer((i + 0.5) * f.cell_size(), y);
      out[static_cast<std::size_t>(i) + static_cast<std::size_t>(f.nx()) * static_cast<std::size_t>(k)] =
          static_cast<float>(n);
    }
  return out;
}

}  // namespace photonlab::field
