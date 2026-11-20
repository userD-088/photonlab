#include "photonlab/field/fdtd3d.hpp"

#include <algorithm>
#include <cmath>

#include "cpml.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"

namespace photonlab::field {
namespace {

constexpr int ex = 0, ey = 1, ez = 2, hx = 3, hy = 4, hz = 5;

double index_at(const Scene3d& scene, double x, double y, double z) {
  if (scene.index_function) return scene.index_function(x, y, z);
  double n = scene.background_index;
  for (const Block& b : scene.blocks)
    if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1 && z >= b.z0 && z <= b.z1) n = b.index;
  return n;
}

int cells_along(double size, double dx) { return static_cast<int>(std::lround(size / dx)); }

}  // namespace

void validate(const Scene3d& scene) {
  if (!(scene.cell_size > 0.0)) throw FieldError("3D-Szene: Zellgröße muss positiv sein");
  if (!(scene.background_index >= 1.0)) throw FieldError("3D-Szene: Hintergrund-Brechzahl muss >= 1 sein");
  if (!(scene.courant > 0.0 && scene.courant <= 1.0))
    throw FieldError("3D-Szene: Courant-Anteil muss in (0, 1] liegen");
  if (scene.pml_cells < 0) throw FieldError("3D-Szene: Randschicht darf nicht negativ sein");
  const double dx = scene.cell_size;
  for (double size : {scene.size_x, scene.size_y, scene.size_z}) {
    const int n = cells_along(size, dx);
    if (n < 2 * scene.pml_cells + 4)
      throw FieldError(
          "3D-Szene: Gebiet zu klein für die Randschichten (mindestens 2*Randschicht + 4 Zellen)");
  }
  const double cells = static_cast<double>(cells_along(scene.size_x, dx)) * cells_along(scene.size_y, dx) *
                       cells_along(scene.size_z, dx);
  if (cells > 60e6)
    throw FieldError("3D-Szene: mehr als 60 Millionen Zellen (über 3 GB Speicher); Zelle vergrößern");
  for (const Block& b : scene.blocks) {
    if (!(b.index >= 1.0)) throw FieldError("3D-Szene: Quader '" + b.name + "' braucht Brechzahl >= 1");
    if (!(b.x1 > b.x0 && b.y1 > b.y0 && b.z1 > b.z0))
      throw FieldError("3D-Szene: Quader '" + b.name + "' hat keine Ausdehnung");
  }
}

Fdtd3d::Fdtd3d(const Scene3d& scene) {
  validate(scene);
  dx_ = scene.cell_size;
  nx_ = cells_along(scene.size_x, dx_);
  ny_ = cells_along(scene.size_y, dx_);
  nz_ = cells_along(scene.size_z, dx_);
  pml_ = scene.pml_cells;
  dt_ = scene.courant * dx_ / (constants::c0 * std::sqrt(3.0));
  const std::size_t n =
      static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_);
  for (auto& field : f_) field.assign(n, 0.0);
  zero_row_.assign(static_cast<std::size_t>(nx_), 0.0);

  // Permittivität je E-Komponente: Mittel von eps über 2x2x2 Punkte im Abstand dx/4 um den Ort der
  // Komponente. Liegen Quaderflächen auf Zellkanten, sitzen nur tangentiale Komponenten auf einer
  // Grenzfläche; für sie ist das arithmetische Mittel die richtige Näherung erster Ordnung (Farjadpour u. a.,
  // Opt. Lett. 31, 2006).
  const std::array<std::array<double, 3>, 3> offset = {{{0.5, 0.0, 0.0}, {0.0, 0.5, 0.0}, {0.0, 0.0, 0.5}}};
  for (int c = 0; c < 3; ++c) {
    coef_[static_cast<std::size_t>(c)].resize(n);
    for (int k = 0; k < nz_; ++k)
      for (int j = 0; j < ny_; ++j)
        for (int i = 0; i < nx_; ++i) {
          const double x = (i + offset[static_cast<std::size_t>(c)][0]) * dx_;
          const double y = (j + offset[static_cast<std::size_t>(c)][1]) * dx_;
          const double z = (k + offset[static_cast<std::size_t>(c)][2]) * dx_;
          double eps = 0.0;
          for (int s = 0; s < 8; ++s) {
            const double q = 0.25 * dx_;
            const double nidx =
                index_at(scene, x + ((s & 1) ? q : -q), y + ((s & 2) ? q : -q), z + ((s & 4) ? q : -q));
            eps += nidx * nidx;
          }
          coef_[static_cast<std::size_t>(c)][at(i, j, k)] = dt_ / (constants::eps0 * eps / 8.0);
        }
  }

  // CPML-Profile
  const std::array<int, 3> count = {nx_, ny_, nz_};
  for (std::size_t a = 0; a < 3; ++a) {
    be_[a].assign(static_cast<std::size_t>(count[a]), 0.0);
    ce_[a] = bh_[a] = ch_[a] = be_[a];
    for (int i = 0; i < count[a] && pml_ > 0; ++i) {
      const auto u = static_cast<std::size_t>(i);
      detail::pml_coefficient(i, count[a], pml_, dx_, dt_, scene.background_index, &be_[a][u], &ce_[a][u]);
      detail::pml_coefficient(i + 0.5, count[a], pml_, dx_, dt_, scene.background_index, &bh_[a][u],
                              &ch_[a][u]);
    }
  }
  if (pml_ > 0) {
    // (Ziel, Quelle, Achse, Vorzeichen) wie in den Update-Gleichungen (docs/physics/fdtd3d.md):
    // E += c * Vorzeichen * psi, H -= dt/mu0 * Vorzeichen * psi
    const int terms[12][4] = {{ex, hz, 1, +1}, {ex, hy, 2, -1}, {ey, hx, 2, +1}, {ey, hz, 0, -1},
                              {ez, hy, 0, +1}, {ez, hx, 1, -1}, {hx, ez, 1, +1}, {hx, ey, 2, -1},
                              {hy, ex, 2, +1}, {hy, ez, 0, -1}, {hz, ey, 0, +1}, {hz, ex, 1, -1}};
    for (const auto& t : terms) {
      PmlTerm term{t[0], t[1], t[2], static_cast<double>(t[3]), {}};
      const std::size_t slab = 2 * static_cast<std::size_t>(pml_) * n /
                               static_cast<std::size_t>(count[static_cast<std::size_t>(t[2])]);
      term.psi.assign(slab, 0.0);
      pml_terms_.push_back(std::move(term));
    }
  }
}

void Fdtd3d::add_source(Source3d source) {
  if (source.cells.size() != source.weights.size())
    throw FieldError("3D-Quelle: Zellen und Gewichte passen nicht zusammen");
  if (static_cast<int>(source.component) > ez) throw FieldError("3D-Quelle: nur E-Komponenten");
  for (std::size_t c : source.cells)
    if (c >= cell_count()) throw FieldError("3D-Quelle: Zelle außerhalb des Gitters");
  const bool gpu = on_gpu();
  use_gpu(false);  // Quellen liegen auf der Grafikkarte fest: neu aufsetzen
  sources_.push_back(std::move(source));
  if (gpu) use_gpu(true);
}

void Fdtd3d::set_threads(int threads) {
  requested_threads_ = std::max(0, threads);
  team_.reset();
}

int Fdtd3d::threads() const {
  const int limit = std::max(1, nz_ / 2);
  if (requested_threads_ > 0) return std::min(requested_threads_, limit);
  return std::min(limit, StepTeam::suggested_size(cell_count(), 32768));
}

template <class Body>
void Fdtd3d::for_planes(const Body& body) {
  const int count = threads();
  if (count <= 1) {
    for (int k = 0; k < nz_; ++k) body(k);
    return;
  }
  if (!team_ || team_->size() != count) team_ = std::make_unique<StepTeam>(count);
  team_->run([&](int member) {
    int k0 = 0, k1 = 0;
    split_range(nz_, count, member, &k0, &k1);
    for (int k = k0; k < k1; ++k) body(k);
  });
}

Fdtd3d::~Fdtd3d() = default;

void Fdtd3d::run(std::size_t steps, JobControl* control) {
  for (std::size_t s = 0; s < steps; ++s) {
    if (control && (s & 7u) == 0) {
      if (control->cancel_requested) return;
      control->progress = static_cast<float>(s) / static_cast<float>(steps);
    }
    step();
  }
  if (control) control->progress = 1.0f;
}

void Fdtd3d::run_probed(std::size_t steps, const std::vector<Probe3d>& probes, std::vector<double>* values,
                        JobControl* control) {
  for (const Probe3d& p : probes)
    if (p.cell >= cell_count()) throw FieldError("3D-Probe: Zelle außerhalb des Gitters");
  if (gpu_) return gpu_run_probed(steps, probes, values, control);
  values->reserve(values->size() + steps * probes.size());
  for (std::size_t s = 0; s < steps; ++s) {
    if (control && (s & 7u) == 0 && control->cancel_requested) return;
    step();
    for (const Probe3d& p : probes) values->push_back(f_[static_cast<std::size_t>(p.component)][p.cell]);
  }
}

void Fdtd3d::step() {
  if (gpu_) {
    gpu_step();
    return;
  }
  // H von n - 1/2 nach n + 1/2, dann E von n nach n + 1
  for_planes([&](int k) {
    update_h(k);
    if (pml_ > 0) pml_plane(k, false);
  });
  for_planes([&](int k) {
    update_e(k);
    if (pml_ > 0) pml_plane(k, true);
  });
  const double source_time = (static_cast<double>(steps_) + 0.5) * dt_;
  for (const Source3d& source : sources_) {
    const double s = Fdtd::signal(source.signal, source_time);
    auto& field = f_[static_cast<std::size_t>(source.component)];
    for (std::size_t c = 0; c < source.cells.size(); ++c) field[source.cells[c]] += source.weights[c] * s;
  }
  ++steps_;
}

// H aus E (Vorwärtsdifferenzen; E jenseits des Gitters = 0)
void Fdtd3d::update_h(int k) {
  const double cm = dt_ / constants::mu0 / dx_;
  const std::size_t nx = static_cast<std::size_t>(nx_);
  const double* zero = zero_row_.data();
  for (int j = 0; j < ny_; ++j) {
    const std::size_t row = at(0, j, k);
    const bool top_j = j + 1 >= ny_, top_k = k + 1 >= nz_;
    const double* PHOTONLAB_RESTRICT exr = &f_[ex][row];
    const double* PHOTONLAB_RESTRICT eyr = &f_[ey][row];
    const double* PHOTONLAB_RESTRICT ezr = &f_[ez][row];
    const double* PHOTONLAB_RESTRICT ex_j1 = top_j ? zero : &f_[ex][at(0, j + 1, k)];
    const double* PHOTONLAB_RESTRICT ez_j1 = top_j ? zero : &f_[ez][at(0, j + 1, k)];
    const double* PHOTONLAB_RESTRICT ex_k1 = top_k ? zero : &f_[ex][at(0, j, k + 1)];
    const double* PHOTONLAB_RESTRICT ey_k1 = top_k ? zero : &f_[ey][at(0, j, k + 1)];
    double* PHOTONLAB_RESTRICT hxr = &f_[hx][row];
    double* PHOTONLAB_RESTRICT hyr = &f_[hy][row];
    double* PHOTONLAB_RESTRICT hzr = &f_[hz][row];
    for (std::size_t i = 0; i < nx; ++i) hxr[i] -= cm * ((ez_j1[i] - ezr[i]) - (ey_k1[i] - eyr[i]));
    for (std::size_t i = 0; i + 1 < nx; ++i) {
      hyr[i] -= cm * ((ex_k1[i] - exr[i]) - (ezr[i + 1] - ezr[i]));
      hzr[i] -= cm * ((eyr[i + 1] - eyr[i]) - (ex_j1[i] - exr[i]));
    }
    const std::size_t i = nx - 1;
    hyr[i] -= cm * ((ex_k1[i] - exr[i]) - (0.0 - ezr[i]));
    hzr[i] -= cm * ((0.0 - eyr[i]) - (ex_j1[i] - exr[i]));
  }
}

// E aus H (Rückwärtsdifferenzen); tangentiales E auf den Wänden i = 0, j = 0, k = 0 bleibt 0
void Fdtd3d::update_e(int k) {
  const double inv = 1.0 / dx_;
  const std::size_t nx = static_cast<std::size_t>(nx_);
  for (int j = 0; j < ny_; ++j) {
    const std::size_t row = at(0, j, k);
    const double* PHOTONLAB_RESTRICT hxr = &f_[hx][row];
    const double* PHOTONLAB_RESTRICT hyr = &f_[hy][row];
    const double* PHOTONLAB_RESTRICT hzr = &f_[hz][row];
    double* PHOTONLAB_RESTRICT exr = &f_[ex][row];
    double* PHOTONLAB_RESTRICT eyr = &f_[ey][row];
    double* PHOTONLAB_RESTRICT ezr = &f_[ez][row];
    const double* PHOTONLAB_RESTRICT cx = &coef_[0][row];
    const double* PHOTONLAB_RESTRICT cy = &coef_[1][row];
    const double* PHOTONLAB_RESTRICT cz = &coef_[2][row];
    if (j > 0 && k > 0) {
      const double* PHOTONLAB_RESTRICT hz_j0 = &f_[hz][at(0, j - 1, k)];
      const double* PHOTONLAB_RESTRICT hy_k0 = &f_[hy][at(0, j, k - 1)];
      for (std::size_t i = 0; i < nx; ++i)
        exr[i] += cx[i] * inv * ((hzr[i] - hz_j0[i]) - (hyr[i] - hy_k0[i]));
    }
    if (k > 0) {
      const double* PHOTONLAB_RESTRICT hx_k0 = &f_[hx][at(0, j, k - 1)];
      for (std::size_t i = 1; i < nx; ++i)
        eyr[i] += cy[i] * inv * ((hxr[i] - hx_k0[i]) - (hzr[i] - hzr[i - 1]));
    }
    if (j > 0) {
      const double* PHOTONLAB_RESTRICT hx_j0 = &f_[hx][at(0, j - 1, k)];
      for (std::size_t i = 1; i < nx; ++i)
        ezr[i] += cz[i] * inv * ((hyr[i] - hyr[i - 1]) - (hxr[i] - hx_j0[i]));
    }
  }
}

// Randschicht-Korrekturen einer Ebene k: psi = b*psi + c*Ableitung, danach E += c_E * Vorzeichen * psi bzw.
// H -= dt/mu0 * Vorzeichen * psi. Besucht werden nur Zellen in der Schicht der Ableitungsrichtung,
// zeilenweise mit zusammenhängenden Schleifen über i.
void Fdtd3d::pml_plane(int k, bool e_phase) {
  const double inv = 1.0 / dx_;
  const double cm = dt_ / constants::mu0;
  const std::size_t p2 = 2 * static_cast<std::size_t>(pml_);
  const auto nx = static_cast<std::size_t>(nx_), ny = static_cast<std::size_t>(ny_);
  auto in_slab = [&](int u, int count) { return u < pml_ || u >= count - pml_; };
  auto local = [&](int u, int count) {
    return static_cast<std::size_t>(u < pml_ ? u : u - (count - 2 * pml_));
  };
  const auto& bb = e_phase ? be_ : bh_;
  const auto& cc = e_phase ? ce_ : ch_;
  for (PmlTerm& term : pml_terms_) {
    const bool is_e = term.target <= ez;
    if (is_e != e_phase) continue;
    const int axis = term.axis;
    if (axis == 2 && !in_slab(k, nz_)) continue;
    if (is_e && k == 0 && term.target != ez) continue;  // Ex, Ey auf der Wand k = 0
    const std::vector<double>& src = f_[static_cast<std::size_t>(term.source)];
    std::vector<double>& dst = f_[static_cast<std::size_t>(term.target)];
    const double* coef = is_e ? coef_[static_cast<std::size_t>(term.target)].data() : nullptr;
    const std::size_t i_first = (is_e && term.target != ex) ? 1 : 0;  // Ey, Ez auf der Wand i = 0
    for (int j = 0; j < ny_; ++j) {
      if (axis == 1 && !in_slab(j, ny_)) continue;
      if (is_e && j == 0 && term.target != ey) continue;  // Ex, Ez auf der Wand j = 0
      const std::size_t row = at(0, j, k);
      const double* PHOTONLAB_RESTRICT s0 = &src[row];
      double* PHOTONLAB_RESTRICT out = &dst[row];
      const double* PHOTONLAB_RESTRICT cf = coef ? coef + row : nullptr;
      if (axis == 0) {
        double* psi = &term.psi[p2 * (static_cast<std::size_t>(j) + ny * static_cast<std::size_t>(k))];
        const std::size_t ranges[2][2] = {{0, static_cast<std::size_t>(pml_)},
                                          {nx - static_cast<std::size_t>(pml_), nx}};
        for (const auto& range : ranges)
          for (std::size_t i = std::max(range[0], i_first); i < range[1]; ++i) {
            const double d =
                e_phase ? (s0[i] - s0[i - 1]) * inv : ((i + 1 < nx ? s0[i + 1] : 0.0) - s0[i]) * inv;
            double& q = psi[local(static_cast<int>(i), nx_)];
            q = bb[0][i] * q + cc[0][i] * d;
            out[i] += e_phase ? cf[i] * term.sign * q : -cm * term.sign * q;
          }
        continue;
      }
      // Achse y oder z: Nachbarzeile und Profilwert sind für die ganze Zeile fest
      const int u = axis == 1 ? j : k;
      const int count = axis == 1 ? ny_ : nz_;
      const double b = bb[static_cast<std::size_t>(axis)][static_cast<std::size_t>(u)];
      const double c = cc[static_cast<std::size_t>(axis)][static_cast<std::size_t>(u)];
      const double* PHOTONLAB_RESTRICT s1;
      if (e_phase)
        s1 = &src[axis == 1 ? at(0, j - 1, k) : at(0, j, k - 1)];
      else if (u + 1 < count)
        s1 = &src[axis == 1 ? at(0, j + 1, k) : at(0, j, k + 1)];
      else
        s1 = zero_row_.data();
      double* PHOTONLAB_RESTRICT psi =
          axis == 1 ? &term.psi[nx * (local(j, ny_) + p2 * static_cast<std::size_t>(k))]
                    : &term.psi[nx * (static_cast<std::size_t>(j) + ny * local(k, nz_))];
      if (e_phase) {
        const double sign = term.sign;
        for (std::size_t i = i_first; i < nx; ++i) {
          psi[i] = b * psi[i] + c * ((s0[i] - s1[i]) * inv);
          out[i] += cf[i] * sign * psi[i];
        }
      } else {
        const double factor = -cm * term.sign;
        for (std::size_t i = 0; i < nx; ++i) {
          psi[i] = b * psi[i] + c * ((s1[i] - s0[i]) * inv);
          out[i] += factor * psi[i];
        }
      }
    }
  }
}

double Fdtd3d::permittivity(Component3d c, std::size_t index) const {
  const auto ci = static_cast<std::size_t>(c);
  if (ci > 2) return 1.0;
  return dt_ / (constants::eps0 * coef_[ci][index]);
}

double Fdtd3d::energy() const {
  gpu_sync();
  double electric = 0.0, magnetic = 0.0;
  for (std::size_t c = 0; c < 3; ++c)
    for (std::size_t n = 0; n < f_[c].size(); ++n) electric += dt_ / coef_[c][n] * f_[c][n] * f_[c][n];
  for (std::size_t c = 3; c < 6; ++c)
    for (double v : f_[c]) magnetic += v * v;
  return 0.5 * dx_ * dx_ * dx_ * (electric + constants::mu0 * magnetic);
}

}  // namespace photonlab::field
