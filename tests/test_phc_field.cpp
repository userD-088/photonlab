// Phase 7: photonische Kristalle im FDTD gegen die PWE-Bandstruktur und die Kristall-Gatter auf Feld-Ebene.
#include <cmath>
#include <vector>

#include "doctest/doctest.h"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/photonic_crystal.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "photonlab/field/spectrum.hpp"
#include "photonlab/logic/field_gate.hpp"
#include "photonlab/pwe/pwe.hpp"

using namespace photonlab;
using namespace photonlab::field;

namespace {

constexpr double kA = 430e-9;

// Mittlerer Fluss durch die Monitorlinie [W/m] im stationären Zustand.
double steady_flux(const FieldScene& scene) {
  Fdtd sim(scene);
  sim.run(static_cast<std::size_t>(scene.duration / sim.time_step()));
  const double period = scene.sources.front().wavelength / constants::c0;
  const MonitorData& m = sim.monitors().front();
  return std::abs(average_over_periods(m.values, m.sample_interval, period, 4));
}

// Kleiner Kristall (18 x 11 Gitterkonstanten) mit Linienquelle links und Flussmonitor rechts; optional W1.
FieldScene crystal_slab(bool with_defect, double wavelength) {
  const FieldScene ref = reference_scene("field_phc_w1");  // Brechzahlen und Gitter wie im Beispiel
  FieldScene s;
  s.width = std::round(18 * kA * 1e12) / 1e12, s.height = std::round(11 * kA * 1e12) / 1e12;
  s.cell_size = std::round(kA / 14.0 * 1e12) / 1e12;
  s.background_index = ref.background_index;
  s.polarization = Polarization::hz;
  s.duration = 2.0e-12;
  CrystalLayout layout;
  layout.a = kA, layout.radius = 150e-9;
  layout.origin_x = 0.0, layout.origin_y = 0.5 * s.height;
  layout.x_min = 0.0, layout.x_max = s.width, layout.y_min = 0.0, layout.y_max = s.height;
  if (with_defect) remove_line(layout, {-2, 0}, 0, 22);
  s.shapes.push_back({"Platte", ShapeKind::rectangle, 0.5 * s.width, 0.5 * s.height, s.width, s.height, 0.0,
                      1e-6, 0.5e-6, ref.shapes.front().index});
  for (Shape& hole : crystal_shapes(layout, ref.background_index)) s.shapes.push_back(std::move(hole));
  Source source;
  source.kind = SourceKind::line;
  source.x1 = source.x2 = 3 * kA;
  source.y1 = layout.origin_y - 1.1e-6, source.y2 = layout.origin_y + 1.1e-6;
  source.beam_width = 0.25e-6;
  source.wavelength = wavelength;
  s.sources = {source};
  s.monitors = {
      {"Fluss", MonitorKind::flux, 14 * kA, layout.origin_y - 1.3e-6, 14 * kA, layout.origin_y + 1.3e-6}};
  return s;
}

}  // namespace

TEST_CASE("Kristall: FDTD-Transmission zeigt die Bandlücke der PWE-Rechnung und den W1-Wellenleiter") {
  // PWE: Lücke des Gitters (a = 430 nm, r = 150 nm, Brechzahlen aus der Feld-Szene) in a/lambda
  const FieldScene ref = reference_scene("field_phc_w1");
  const double eps_slab = ref.shapes.front().index * ref.shapes.front().index;
  const double eps_hole = ref.background_index * ref.background_index;
  const pwe::UnitCell cell = pwe::triangular_cell(150e-9 / kA, eps_slab, eps_hole);
  pwe::BandSettings settings;
  settings.polarization = pwe::Polarization::hz;
  settings.cutoff = 7.0;
  settings.bands = 3;
  const pwe::BandResult bands =
      pwe::solve_bands(cell, settings, pwe::standard_path(cell, pwe::PathKind::triangular, 8));
  REQUIRE(!bands.gaps.empty());
  const pwe::Gap gap = bands.gaps.front();
  REQUIRE(gap.band == 1);
  const double lambda_gap = kA / gap.midgap();        // Mitte der Lücke
  const double lambda_band = kA / (0.8 * gap.lower);  // deutlich unterhalb der Lücke: Band

  ThreadPool pool;
  std::vector<double> flux(3, 0.0);
  const std::vector<FieldScene> scenes = {crystal_slab(false, lambda_gap), crystal_slab(false, lambda_band),
                                          crystal_slab(true, 1580e-9)};
  std::vector<std::future<void>> jobs;
  for (std::size_t i = 0; i < scenes.size(); ++i)
    jobs.push_back(pool.submit([&, i] { flux[i] = steady_flux(scenes[i]); }));
  for (auto& j : jobs) j.get();
  const double in_gap = flux[0], in_band = flux[1], with_defect = flux[2];
  MESSAGE("Fluss in der Lücke " << in_gap << ", im Band " << in_band << ", W1 " << with_defect);
  // In der Lücke dämpft der Kristall um mehr als 20 dB gegenüber dem Band und gegenüber dem W1-Wellenleiter
  CHECK(in_gap < 0.01 * in_band);
  CHECK(in_gap < 0.01 * with_defect);
  CHECK(with_defect > 0.0);
}

TEST_CASE("Feld-Gatter: Y-Kombinierer liefert ODER/UND/XOR mit den erwarteten Pegeln") {
  ThreadPool pool;
  auto coarse = [](logic::FieldGate gate) {
    for (auto& s : gate.scenes) {
      s.cell_size = std::round(kA / 14.0 * 1e12) / 1e12;
      s.duration = 2.0e-12;
    }
    return gate;
  };
  const logic::FieldGateReport and_or =
      logic::characterize_field_gate(coarse(logic::field_gate("phc_or_and")), &pool);
  const logic::FieldGateReport xor_gate =
      logic::characterize_field_gate(coarse(logic::field_gate("phc_xor")), &pool);
  REQUIRE(and_or.table.rows.size() == 4);
  const auto& t = and_or.table.rows;
  // keine Eingabe: kein Licht; A allein = B allein (Symmetrie); beide gleichphasig: |a+b|^2 = 4 |a|^2
  CHECK(t[0].power[0] == 0.0);
  CHECK(t[1].power[0] == doctest::Approx(t[2].power[0]).epsilon(0.03).scale(0.0));
  CHECK(t[3].power[0] / t[1].power[0] == doctest::Approx(4.0).epsilon(0.08).scale(0.0));
  MESSAGE("Y: A " << t[1].power[0] << " B " << t[2].power[0] << " AB " << t[3].power[0] << " UND-CR "
                  << and_or.outputs[1].contrast_db << " dB");
  CHECK(and_or.function_correct);
  CHECK(and_or.outputs[0].contrast_db > 40.0);                                // ODER: 0 ist wirklich dunkel
  CHECK(and_or.outputs[1].contrast_db == doctest::Approx(6.0).epsilon(0.1).scale(0.0));  // UND: begrenzt auf 6 dB
  // XOR: gegenphasig, A = B = 1 löscht aus
  REQUIRE(xor_gate.table.rows.size() == 4);
  MESSAGE("XOR-CR " << xor_gate.outputs[0].contrast_db << " dB");
  CHECK(xor_gate.function_correct);
  CHECK(xor_gate.outputs[0].contrast_db > 20.0);
}
