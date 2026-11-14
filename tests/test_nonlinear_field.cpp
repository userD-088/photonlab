// Phase 8, Abnahme: Kerr-Resonator im FDTD gegen das Coupled-Mode-Kompaktmodell (kerr_cavity).
// Bragg-Resonator (Schichtstapel, Ez-Polarisation) mit Kerr-Spacer. Aus linearen FDTD-Läufen werden Resonanz,
// Linienbreite und Kopplung gefittet und aus dem Modenprofil die Kerr-Konstante gerechnet; der nichtlineare
// FDTD-Lauf bei fester Verstimmung muss die Energie des Kompaktmodells treffen.
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "doctest/doctest.h"
#include "photonlab/circuit/kerr_cavity.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"

using namespace photonlab;
using namespace photonlab::field;

namespace {

constexpr double kLambda0 = 1550e-9, kIndexHigh = 3.0, kIndexLow = 1.5;

struct Cavity {
  FieldScene scene;
  double spacer_x0 = 0.0, spacer_x1 = 0.0;
};

// (H L)^N H | Spacer (L, optische Dicke lambda/2) | H (L H)^N, Hintergrund L
Cavity build_cavity(double wavelength, double e0, double n2) {
  const double dh = kLambda0 / (4.0 * kIndexHigh), dl = kLambda0 / (4.0 * kIndexLow);
  const double spacer = kLambda0 / (2.0 * kIndexLow);
  const int pairs = 3;
  Cavity c;
  FieldScene& s = c.scene;
  const double margin = 2.0e-6;
  double x = margin;
  std::vector<Shape> shapes;
  auto layer = [&](double thickness, double index, double kerr, const char* name) {
    Shape sh{name, ShapeKind::rectangle, x + 0.5 * thickness, 0.15e-6, thickness, 1e-6, 0.0, 1e-6, 0.5e-6,
             index};
    sh.n2 = kerr;
    shapes.push_back(sh);
    x += thickness;
  };
  for (int i = 0; i < pairs; ++i) {
    layer(dh, kIndexHigh, 0.0, "H");
    layer(dl, kIndexLow, 0.0, "L");
  }
  layer(dh, kIndexHigh, 0.0, "H");
  c.spacer_x0 = x;
  layer(spacer, kIndexLow, n2, "Spacer");
  c.spacer_x1 = x;
  layer(dh, kIndexHigh, 0.0, "H");
  for (int i = 0; i < pairs; ++i) {
    layer(dl, kIndexLow, 0.0, "L");
    layer(dh, kIndexHigh, 0.0, "H");
  }
  s.width = x + margin;
  s.height = 0.2e-6;
  s.cell_size = 20e-9;
  s.background_index = kIndexLow;
  s.polarization = Polarization::ez;
  s.boundary_y = Boundary::periodic;
  s.shapes = shapes;
  Source q;
  q.kind = SourceKind::line;
  q.x1 = q.x2 = 0.8e-6, q.y1 = 0.0, q.y2 = s.height;
  q.amplitude = e0;
  q.wavelength = wavelength;
  q.ramp_periods = 40.0;  // sanftes Einschalten: wenig Anregung anderer Frequenzen
  s.sources = {q};
  return c;
}

struct Measurement {
  double energy = 0.0;              // U' = 1/2 eps0 Integral eps |E|^2 dx [J/m^2]
  double kerr_integral = 0.0;       // Integral im Spacer |E|^4 dx
  double eps_field_integral = 0.0;  // Integral eps |E|^2 dx
};

// Fährt die Rechnung bis zum stationären Zustand und mittelt E^2 über 6 Perioden: |E|^2 = 2 <E^2>.
Measurement run_cavity(const Cavity& c, double settle_time) {
  Fdtd sim(c.scene);
  sim.run(static_cast<std::size_t>(settle_time / sim.time_step()));
  const int nx = sim.nx(), j = sim.ny() / 2;
  std::vector<double> acc(static_cast<std::size_t>(nx), 0.0);
  const double period = c.scene.sources.front().wavelength / constants::c0;
  const std::size_t steps = static_cast<std::size_t>(std::lround(6.0 * period / sim.time_step()));
  for (std::size_t n = 0; n < steps; ++n) {
    sim.step();
    for (int i = 0; i < nx; ++i) {
      const double e = sim.field()[static_cast<std::size_t>(i + nx * j)];
      acc[static_cast<std::size_t>(i)] += e * e;
    }
  }
  Measurement m;
  const double dx = sim.cell_size();
  for (int i = 0; i < nx; ++i) {
    const double amp2 = 2.0 * acc[static_cast<std::size_t>(i)] / static_cast<double>(steps);  // |E|^2
    const double eps = sim.permittivity()[static_cast<std::size_t>(i + nx * j)];
    const double x = i * dx;
    m.eps_field_integral += eps * amp2 * dx;
    if (x >= c.spacer_x0 && x < c.spacer_x1) m.kerr_integral += amp2 * amp2 * dx;
  }
  m.energy = 0.5 * constants::eps0 * m.eps_field_integral;
  return m;
}

}  // namespace

TEST_CASE(
    "Abnahme Phase 8: Kerr-Resonator im FDTD folgt dem Coupled-Mode-Bild (Resonanz verschiebt sich um "
    "beta*U)") {
  const double e_lin = 1e6;       // kleine Amplitude für die lineare Vermessung
  const double settle = 2.5e-12;  // viele Abklingzeiten 2/gamma (ca. 0,3 ps)
  const double p_ref = kIndexLow * constants::c0 * constants::eps0 * e_lin * e_lin / 2.0;
  auto transfer = [&](double omega) {  // U'/P' aus dem linearen Lauf [s]
    const double lambda = 2.0 * constants::pi * constants::c0 / omega;
    return run_cavity(build_cavity(lambda, e_lin, 0.0), settle).energy / p_ref;
  };
  // Grobsuche der Resonanz (Übertragungsfunktion U/P der linearen Struktur) und Halbwertsbreite
  const double omega_center = 2.0 * constants::pi * constants::c0 / kLambda0;
  std::vector<double> omegas, t_values;
  for (int i = -10; i <= 10; ++i) {
    omegas.push_back(omega_center * (1.0 + 0.0015 * i));
    t_values.push_back(transfer(omegas.back()));
  }
  const std::size_t peak =
      static_cast<std::size_t>(std::max_element(t_values.begin(), t_values.end()) - t_values.begin());
  REQUIRE(peak > 0);
  REQUIRE(peak + 1 < omegas.size());
  // Parabel durch die drei Punkte um das Maximum (in 1/T, dort fast exakt eine Parabel)
  const double y0 = 1.0 / t_values[peak - 1], y1 = 1.0 / t_values[peak], y2 = 1.0 / t_values[peak + 1];
  const double h = omegas[peak] - omegas[peak - 1];
  const double curvature = (y0 - 2.0 * y1 + y2) / (h * h);  // = 2 a
  const double slope = (y2 - y0) / (2.0 * h);
  const double omega0 = omegas[peak] - slope / curvature;
  const double t_peak = 1.0 / (y1 - 0.5 * slope * slope / curvature);
  // Halbwertsbreite aus 1/T = (delta^2 + gamma^2/4)/kappa => gamma/2 = sqrt(y_min / (curvature/2))
  const double y_min = 1.0 / t_peak;
  const double gamma = 2.0 * std::sqrt(y_min / (0.5 * curvature));
  MESSAGE("omega0 = " << omega0 << ", gamma = " << gamma << ", Q = " << omega0 / gamma);
  CHECK(omega0 / gamma > 50.0);  // genug hohe Güte für die Ein-Moden-Näherung
  CHECK(omega0 / gamma < 5000.0);

  // Modenprofil bei der Resonanz und daraus die Kerr-Konstante beta' = Delta w / U'
  const double n2 = 1e-15;  // künstlich groß; die Leistung wird unten daran angepasst
  const Measurement profile =
      run_cavity(build_cavity(2.0 * constants::pi * constants::c0 / omega0, e_lin, 0.0), settle);
  const double chi3 = 4.0 * kIndexLow * kIndexLow * constants::eps0 * constants::c0 * n2 / 3.0;
  const double kerr = omega0 * 0.75 * chi3 * profile.kerr_integral /
                      (constants::eps0 * profile.eps_field_integral * profile.eps_field_integral);

  // Nichtlinearer Lauf: Verstimmung 0,5 gamma unterhalb der Resonanz, Kerr-Verschiebung etwa 0,3 gamma
  const double detuning = 0.5 * gamma, omega_l = omega0 - detuning;
  const double t_cold = transfer(omega_l);
  const double p_in = 0.3 * gamma / (kerr * t_cold);
  const double e0 = std::sqrt(2.0 * p_in / (kIndexLow * constants::c0 * constants::eps0));
  // Vorhersage: U = P * T_lin(w_L + beta U): die gemessene lineare Übertragungsfunktion wird um beta*U
  // verschoben (Fixpunkt mit Dämpfung; die gemessene Linienform ersetzt die Lorentz-Annahme)
  double u_pred = p_in * t_cold;
  for (int iteration = 0; iteration < 12; ++iteration) {
    const double next = p_in * transfer(omega_l + kerr * u_pred);
    if (std::abs(next - u_pred) < 1e-3 * u_pred) {
      u_pred = next;
      break;
    }
    u_pred = 0.5 * (u_pred + next);
  }
  const Measurement nonlinear =
      run_cavity(build_cavity(2.0 * constants::pi * constants::c0 / omega_l, e0, n2), settle);
  MESSAGE("E0 = " << e0 << " V/m, U_FDTD = " << nonlinear.energy << ", U_Vorhersage = " << u_pred
                  << ", U_kalt = " << p_in * t_cold << ", kerr = " << kerr
                  << ", Verschiebung/gamma = " << kerr * nonlinear.energy / gamma);
  CHECK(u_pred > 1.15 * p_in * t_cold);  // die Verschiebung verändert die Energie deutlich
  CHECK(nonlinear.energy == doctest::Approx(u_pred).epsilon(0.08).scale(0.0));

  // Kompaktmodell mit Lorentz-Linie aus denselben Messwerten: gleicher Trend (gröbere Annahme, daher 25 %)
  circuit::KerrCavity cmt;
  cmt.omega0 = omega0;
  cmt.kappa_in = t_peak * gamma * gamma / 4.0;
  cmt.kappa_0 = gamma - cmt.kappa_in;
  cmt.kerr = kerr;
  REQUIRE(cmt.kappa_0 > 0.0);
  const auto states = circuit::steady_states(cmt, omega_l, p_in);
  REQUIRE(!states.empty());
  MESSAGE("Kompaktmodell (Lorentz): U = " << states[0].energy);
  CHECK(states[0].energy == doctest::Approx(nonlinear.energy).epsilon(0.25).scale(0.0));
}

// Hysterese im Feld: langsame Leistungsrampe hinauf und hinunter über einen Kerr-Resonator bei großer
// Verstimmung. Der Zustand klappt hinauf bei höherer Leistung als hinunter; die Klappleistungen folgen dem
// Kompaktmodell.
TEST_CASE("Abnahme Phase 8: Hysterese des Kerr-Resonators im FDTD") {
  const double e_lin = 1e6, settle = 2.5e-12;
  const double p_ref = kIndexLow * constants::c0 * constants::eps0 * e_lin * e_lin / 2.0;
  auto transfer = [&](double omega) {
    const double lambda = 2.0 * constants::pi * constants::c0 / omega;
    return run_cavity(build_cavity(lambda, e_lin, 0.0), settle).energy / p_ref;
  };
  // Resonanz und Linienbreite wie oben (Grobsuche + Parabel in 1/T)
  const double omega_center = 2.0 * constants::pi * constants::c0 / kLambda0;
  std::vector<double> omegas, t_values;
  for (int i = -2; i <= 10; ++i) {
    omegas.push_back(omega_center * (1.0 + 0.0015 * i));
    t_values.push_back(transfer(omegas.back()));
  }
  const std::size_t peak =
      static_cast<std::size_t>(std::max_element(t_values.begin(), t_values.end()) - t_values.begin());
  REQUIRE(peak > 0);
  REQUIRE(peak + 1 < omegas.size());
  const double y0 = 1.0 / t_values[peak - 1], y1 = 1.0 / t_values[peak], y2 = 1.0 / t_values[peak + 1];
  const double h = omegas[peak] - omegas[peak - 1];
  const double curvature = (y0 - 2.0 * y1 + y2) / (h * h), slope = (y2 - y0) / (2.0 * h);
  const double omega0 = omegas[peak] - slope / curvature;
  const double t_peak = 1.0 / (y1 - 0.5 * slope * slope / curvature);
  const double gamma = 2.0 * std::sqrt((1.0 / t_peak) / (0.5 * curvature));
  const double n2 = 1e-15;
  const Measurement profile =
      run_cavity(build_cavity(2.0 * constants::pi * constants::c0 / omega0, e_lin, 0.0), settle);
  const double chi3 = 4.0 * kIndexLow * kIndexLow * constants::eps0 * constants::c0 * n2 / 3.0;
  const double kerr = omega0 * 0.75 * chi3 * profile.kerr_integral /
                      (constants::eps0 * profile.eps_field_integral * profile.eps_field_integral);

  circuit::KerrCavity cmt;
  cmt.omega0 = omega0;
  cmt.kappa_in = t_peak * gamma * gamma / 4.0;
  cmt.kappa_0 = gamma - cmt.kappa_in;
  cmt.kerr = kerr;

  // Vorhersage der Klappleistungen: P(U) = U / T_lin(w_L + beta U) mit der gemessenen Linienform
  // (Verschiebung der gemessenen linearen Übertragungsfunktion um beta*U), Verstimmung 1,5 gamma (> sqrt(3)/2
  // gamma)
  const double omega_l = omega0 - 1.5 * gamma;
  std::vector<double> grid_omega, grid_inv;  // 1/T auf einem Raster von 0,2 gamma
  for (int k = -9; k <= 8; ++k) {
    grid_omega.push_back(omega0 + 0.2 * gamma * k);
    grid_inv.push_back(1.0 / transfer(grid_omega.back()));
  }
  auto inverse_transfer = [&](double omega) {  // kubische Catmull-Rom-Interpolation von 1/T
    const double pos = (omega - grid_omega.front()) / (0.2 * gamma);
    const int n = static_cast<int>(grid_omega.size());
    const int i1 = std::clamp(static_cast<int>(std::floor(pos)), 1, n - 3);
    const double f = pos - i1;
    const double p0 = grid_inv[static_cast<std::size_t>(i1 - 1)], p1 = grid_inv[static_cast<std::size_t>(i1)],
                 p2 = grid_inv[static_cast<std::size_t>(i1 + 1)],
                 p3 = grid_inv[static_cast<std::size_t>(i1 + 2)];
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * f + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * f * f +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * f * f * f);
  };
  double p_up = 0.0, p_down = 0.0, prev = 0.0, prev_slope = 1.0;
  bool have_up = false, have_down = false;
  const double u_scan = 1.6 * gamma / kerr * 1.5;  // bis zu 2,4 gamma Verschiebung, innerhalb des Rasters
  for (int i = 1; i <= 4000; ++i) {
    const double u = u_scan * i / 4000.0;
    const double omega = omega_l + kerr * u;
    if (omega > grid_omega.back() - 0.2 * gamma) break;
    const double p = u * inverse_transfer(omega);
    const double d = p - prev;
    if (i > 1 && prev_slope > 0.0 && d < 0.0 && !have_up) p_up = prev, have_up = true;
    if (i > 1 && prev_slope < 0.0 && d > 0.0 && !have_down) p_down = prev, have_down = true;
    prev = p, prev_slope = d;
  }
  REQUIRE(have_up);
  REQUIRE(have_down);
  REQUIRE(p_up > 1.3 * p_down);
  const double p_max = 1.5 * p_up;
  const double e_max = std::sqrt(2.0 * p_max / (kIndexLow * constants::c0 * constants::eps0));

  // FDTD mit Rampe: Quellenfaktor sqrt(P(t)/P_max), 10 ps hinauf, 10 ps hinunter; Energie je Periode
  // gemittelt
  const Cavity cav = build_cavity(2.0 * constants::pi * constants::c0 / omega_l, e_max, n2);
  Fdtd sim(cav.scene);
  const double ramp = 10e-12;
  const double period = 2.0 * constants::pi / omega_l;
  const std::size_t block = static_cast<std::size_t>(std::lround(period / sim.time_step()));
  const std::size_t total = static_cast<std::size_t>(2.0 * ramp / sim.time_step());
  const int nx = sim.nx(), jrow = sim.ny() / 2;
  std::vector<double> block_p, block_u;
  double acc = 0.0;
  for (std::size_t n = 0; n < total; ++n) {
    const double t = static_cast<double>(n) * sim.time_step();
    const double p = p_max * (t < ramp ? t / ramp : 2.0 - t / ramp);
    sim.set_source_scale(std::sqrt(std::max(p, 0.0) / p_max));
    sim.step();
    double row = 0.0;
    for (int i = 0; i < nx; ++i) {
      const std::size_t k = static_cast<std::size_t>(i + nx * jrow);
      row += sim.permittivity()[k] * sim.field()[k] * sim.field()[k];
    }
    acc += row;
    if ((n + 1) % block == 0) {
      block_p.push_back(p);
      block_u.push_back(0.5 * constants::eps0 * acc / static_cast<double>(block) * 2.0 *
                        sim.cell_size());  // 1/2 eps0 Int eps |E|^2
      acc = 0.0;
    }
  }
  // Sprungstellen: steilster Anstieg auf dem Hinweg, steilster Abfall auf dem Rückweg (die ersten 8 % des
  // Rückwegs ausgenommen: dort sinkt die Leistung am Umkehrpunkt schnell)
  auto jumps = [&](const std::vector<double>& power, const std::vector<double>& energy, double* up,
                   double* down) {
    const std::size_t half = power.size() / 2;
    double best_up = 0.0, best_down = 0.0;
    for (std::size_t i = 1; i < half; ++i)
      if (energy[i] - energy[i - 1] > best_up) best_up = energy[i] - energy[i - 1], *up = power[i];
    for (std::size_t i = half + half / 12; i < power.size(); ++i)
      if (energy[i - 1] - energy[i] > best_down) best_down = energy[i - 1] - energy[i], *down = power[i];
  };
  double jump_up = 0.0, jump_down = 0.0;
  jumps(block_p, block_u, &jump_up, &jump_down);

  // Kompaktmodell (Lorentz-Linie aus den Messwerten) mit derselben Rampe: Dynamik inklusive Verzögerung am
  // Klapp
  const circuit::HysteresisLoop loop = circuit::hysteresis_sweep(cmt, omega_l, p_max, ramp, 800);
  std::vector<double> cmt_p = loop.up.power, cmt_u = loop.up.energy;
  cmt_p.insert(cmt_p.end(), loop.down.power.begin(), loop.down.power.end());
  cmt_u.insert(cmt_u.end(), loop.down.energy.begin(), loop.down.energy.end());
  double cmt_up = 0.0, cmt_down = 0.0;
  jumps(cmt_p, cmt_u, &cmt_up, &cmt_down);
  MESSAGE("Klappleistung hinauf: FDTD " << jump_up << " W/m^2, Kompaktmodell (Rampe) " << cmt_up
                                        << ", statisch " << p_up << "; hinunter: FDTD " << jump_down
                                        << ", Rampe " << cmt_down << ", statisch " << p_down
                                        << "; Q = " << omega0 / gamma);
  CHECK(jump_up > 1.2 * jump_down);  // Hysterese: hinauf klappt es bei höherer Leistung als hinunter
  CHECK(jump_up > p_up);             // die endliche Rampe verzögert das Klappen hinauf
  CHECK(jump_up == doctest::Approx(cmt_up).epsilon(0.15).scale(0.0));
  CHECK(jump_down == doctest::Approx(cmt_down).epsilon(0.25).scale(0.0));
}
