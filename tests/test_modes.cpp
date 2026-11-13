// Phase 5: Materialien und Modenlöser gegen Literaturwerte und analytische Lösungen.
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "photonlab/core/units.hpp"
#include "photonlab/materials/material.hpp"
#include "photonlab/modes/cross_section.hpp"
#include "photonlab/modes/fde.hpp"
#include "photonlab/modes/profile.hpp"
#include "photonlab/modes/slab.hpp"

using namespace photonlab;
using namespace photonlab::modes;

namespace {
const double lambda0 = 1550 * units::nm;
const double pi = constants::pi;
}  // namespace

TEST_CASE("Sellmeier: Brechzahlen bei 1550 nm entsprechen den Literaturwerten") {
  // Werte der jeweiligen Originalarbeit bzw. ihrer Sellmeier-Gleichung bei 1,55 µm
  CHECK(materials::get("SiO2").index(lambda0) == doctest::Approx(1.4440).epsilon(1e-4));   // Malitson 1965
  CHECK(materials::get("Si3N4").index(lambda0) == doctest::Approx(1.9963).epsilon(1e-4));  // Luke 2015
  CHECK(materials::get("Si").index(lambda0) == doctest::Approx(3.4777).epsilon(1e-4));     // Salzberg/Villa 1957
  CHECK(materials::get("Luft").index(lambda0) == 1.0);
  // weitere Stützstellen: Quarzglas bei der Natrium-D-Linie (n_D = 1,4584) und bei 1,3 µm
  CHECK(materials::get("SiO2").index(589.3 * units::nm) == doctest::Approx(1.4584).epsilon(1e-4));
  CHECK(materials::get("SiO2").index(1300 * units::nm) == doctest::Approx(1.4469).epsilon(1e-4));
  CHECK_THROWS_AS(materials::get("Unobtainium"), materials::MaterialError);
  CHECK(materials::get("Si").in_valid_range(lambda0));
  CHECK_FALSE(materials::get("Si").in_valid_range(800 * units::nm));  // unter der Bandkante: Gleichung ungültig
}

TEST_CASE("Gruppenindex: analytische Ableitung stimmt mit der numerischen, Quarzglas 1,4626 bei 1550 nm") {
  for (const auto& material : materials::library()) {
    const double h = 1e-11;
    const double numeric = material.index(lambda0) - lambda0 * (material.index(lambda0 + h) - material.index(lambda0 - h)) / (2 * h);
    CHECK(material.group_index(lambda0) == doctest::Approx(numeric).epsilon(1e-7));
  }
  CHECK(materials::get("SiO2").group_index(lambda0) == doctest::Approx(1.4626).epsilon(2e-4));
  CHECK(materials::get("Si").group_index(lambda0) == doctest::Approx(3.60).epsilon(0.01));
  // thermische Drift in linearer Näherung
  CHECK(materials::get("Si").index(lambda0, 10.0) - materials::get("Si").index(lambda0) == doctest::Approx(1.8e-3));
}

TEST_CASE("Schichtwellenleiter: Moden erfüllen die Eigenwertgleichung, Zahl der Moden nach V-Parameter") {
  const double n_core = 3.4777, n_clad = 1.444, thickness = 220 * units::nm;
  for (SlabPolarization polarization : {SlabPolarization::te, SlabPolarization::tm}) {
    const auto modes = slab_modes(n_core, n_clad, n_clad, thickness, lambda0, polarization);
    // symmetrische Schicht: Zahl der Moden = floor(V/pi) + 1 mit V = k0*d*sqrt(n1² - n2²)
    const double v = 2 * pi / lambda0 * thickness * std::sqrt(n_core * n_core - n_clad * n_clad);
    CHECK(static_cast<int>(modes.size()) == static_cast<int>(std::floor(v / pi)) + 1);
    for (const auto& mode : modes) {
      CHECK(mode.n_eff > n_clad);
      CHECK(mode.n_eff < n_core);
      CHECK(std::abs(slab_dispersion_residual(mode.n_eff, mode.order, n_core, n_clad, n_clad, thickness, lambda0, polarization)) < 1e-9);
    }
  }
  // Literaturwert: TE0 der 220-nm-Siliziumschicht in SiO2 bei 1550 nm liegt bei etwa 2,85
  const auto te = slab_modes(n_core, n_clad, n_clad, thickness, lambda0, SlabPolarization::te);
  CHECK(te.front().n_eff == doctest::Approx(2.85).epsilon(0.01));
  const auto tm = slab_modes(n_core, n_clad, n_clad, thickness, lambda0, SlabPolarization::tm);
  CHECK(tm.front().n_eff < te.front().n_eff);  // TM ist schwächer geführt
  // kein Indexsprung, keine Mode; sehr dünne asymmetrische Schicht liegt unter der Grenzfrequenz
  CHECK(slab_modes(1.444, 1.444, 1.444, thickness, lambda0, SlabPolarization::te).empty());
  CHECK(slab_modes(1.5, 1.444, 1.0, 50 * units::nm, lambda0, SlabPolarization::te).empty());
}

TEST_CASE("Schichtwellenleiter: Feld ist stetig, klingt außen ab und hat so viele Knoten wie die Ordnung") {
  const double n_core = 2.0, n_lower = 1.444, n_upper = 1.0, thickness = 1.5 * units::um;
  const auto modes = slab_modes(n_core, n_lower, n_upper, thickness, lambda0, SlabPolarization::te);
  REQUIRE(modes.size() >= 2);
  for (const auto& mode : modes) {
    auto f = [&](double y) { return slab_field(mode, y, n_core, n_lower, n_upper, thickness, lambda0, SlabPolarization::te); };
    CHECK(f(-1e-12) == doctest::Approx(f(1e-12)).epsilon(1e-4));
    CHECK(f(thickness - 1e-12) == doctest::Approx(f(thickness + 1e-12)).epsilon(1e-4));
    CHECK(std::abs(f(-3e-6)) < 1e-3);
    CHECK(std::abs(f(thickness + 3e-6)) < 1e-3);
    int sign_changes = 0;
    double previous = f(0.0);
    for (int i = 1; i <= 400; ++i) {
      const double value = f(thickness * i / 400.0);
      if (value * previous < 0.0) ++sign_changes;
      previous = value;
    }
    CHECK(sign_changes == mode.order);
  }
}

TEST_CASE("Profil-Löser (finite Differenzen) trifft die analytischen Schichtmoden") {
  // Schicht 0,45 µm mit n = 2,85 in n = 1,444: die Geometrie der Wellenleiter im 2D-FDTD
  const double n_core = 2.85, n_clad = 1.444, thickness = 450 * units::nm, spacing = 5 * units::nm;
  const int points = 800;  // 4 µm
  // Punkt i steht für die Zelle [(i - 1/2)*h, (i + 1/2)*h]: 90 Kernzellen ergeben genau 450 nm
  std::vector<double> index(points, n_clad);
  for (int i = 355; i < 445; ++i) index[static_cast<std::size_t>(i)] = n_core;
  const double start = 354.5 * spacing;  // untere Kante des Kerns
  for (SlabPolarization polarization : {SlabPolarization::te, SlabPolarization::tm}) {
    const auto exact = slab_modes(n_core, n_clad, n_clad, thickness, lambda0, polarization);
    const auto numeric = profile_modes(index, spacing, lambda0, polarization, 8);
    REQUIRE(numeric.size() == exact.size());
    for (std::size_t k = 0; k < exact.size(); ++k) {
      CHECK(numeric[k].n_eff == doctest::Approx(exact[k].n_eff).epsilon(5e-3));
      CHECK(numeric[k].order == static_cast<int>(k));
    }
    // Feldprofil der Grundmode gegen die analytische Form
    double worst = 0.0;
    for (int i = 0; i < points; i += 4) {
      const double expected = slab_field(exact[0], i * spacing - start, n_core, n_clad, n_clad, thickness, lambda0, polarization);
      worst = std::max(worst, std::abs(numeric[0].field[static_cast<std::size_t>(i)] - expected));
    }
    CHECK(worst < 0.03);
    // Orthogonalität der Moden unter dem passenden Gewicht
    if (numeric.size() >= 2) {
      double cross = 0.0, norm0 = 0.0, norm1 = 0.0;
      for (int i = 0; i < points; ++i) {
        const double w = overlap_weight(index[static_cast<std::size_t>(i)], polarization);
        cross += w * numeric[0].field[static_cast<std::size_t>(i)] * numeric[1].field[static_cast<std::size_t>(i)];
        norm0 += w * std::pow(numeric[0].field[static_cast<std::size_t>(i)], 2);
        norm1 += w * std::pow(numeric[1].field[static_cast<std::size_t>(i)], 2);
      }
      CHECK(std::abs(cross) / std::sqrt(norm0 * norm1) < 1e-9);
    }
  }
}

TEST_CASE("Modenlöser im Grenzfall der Schicht: quasi-TE und quasi-TM treffen die analytischen Werte") {
  // In x gleichförmige Schicht mit Spiegelrändern: der 2D-Löser muss die Schichtmoden liefern.
  const double n_core = 3.4777, n_clad = 1.444, thickness = 220 * units::nm, h = 5 * units::nm;
  FdeGrid grid;
  grid.nx = 4, grid.ny = 400;  // 2 µm hoch
  grid.dx = grid.dy = h;
  grid.index.assign(static_cast<std::size_t>(grid.nx * grid.ny), n_clad);
  for (int j = 178; j < 222; ++j)  // 44 Zellen = 220 nm, mittig
    for (int i = 0; i < grid.nx; ++i) grid.index[static_cast<std::size_t>(i + grid.nx * j)] = n_core;
  FdeSettings settings;
  settings.x_edges = EdgeCondition::symmetric;
  settings.modes = 1;
  // quasi-TE (E waagerecht, parallel zur Schicht) = TE der Schicht; quasi-TM (E senkrecht) = TM der Schicht
  settings.polarization = FdePolarization::quasi_te;
  const double te = slab_modes(n_core, n_clad, n_clad, thickness, lambda0, SlabPolarization::te).front().n_eff;
  CHECK(solve_modes(grid, settings).front().n_eff == doctest::Approx(te).epsilon(2e-3));
  settings.polarization = FdePolarization::quasi_tm;
  const double tm = slab_modes(n_core, n_clad, n_clad, thickness, lambda0, SlabPolarization::tm).front().n_eff;
  CHECK(solve_modes(grid, settings).front().n_eff == doctest::Approx(tm).epsilon(2e-3));
}

TEST_CASE("Abnahme Phase 5: Silizium-Streifen 500 x 220 nm, n_eff der TE0-Mode im Bereich der Literatur") {
  CrossSection section;  // Standard: Si in SiO2, 500 x 220 nm, 1550 nm, quasi-TE, 20-nm-Gitter
  const CrossSectionResult result = analyze(section, true);
  REQUIRE_FALSE(result.modes.empty());
  const CrossSectionMode& te0 = result.modes.front();
  INFO("n_eff = ", te0.n_eff, ", n_g = ", te0.n_group, ", Einschluss = ", te0.confinement);
  CHECK(te0.guided);
  // Literatur (vollvektoriell): n_eff etwa 2,44 und n_g etwa 4,2 (Chrostowski/Hochberg 2015, Kap. 3).
  // Der semi-vektorielle Löser liegt bei diesem stark führenden Streifen systematisch etwa 2 % zu hoch
  // im effektiven Index und etwa 4 % zu tief im Gruppenindex (docs/physics/modes.md).
  CHECK(te0.n_eff == doctest::Approx(2.44).epsilon(0.03));
  CHECK(te0.n_group == doctest::Approx(4.2).epsilon(0.05));
  CHECK(te0.confinement > 0.6);
  CHECK(te0.confinement < 1.0);
  CHECK(te0.effective_area > 0.03e-12);
  CHECK(te0.effective_area < 0.5e-12);
  // Grundmode ohne Knoten: Feld überall gleichen Vorzeichens (bis auf Rauschen am Rand)
  double most_negative = 0.0;
  for (double v : te0.field) most_negative = std::min(most_negative, v);
  CHECK(most_negative > -1e-3);

  // quasi-TM ist schwächer geführt
  section.polarization = FdePolarization::quasi_tm;
  section.modes = 1;
  const CrossSectionResult tm = analyze(section, false);
  REQUIRE_FALSE(tm.modes.empty());
  CHECK(tm.modes.front().n_eff < te0.n_eff);
  CHECK(tm.modes.front().n_eff > 1.444);
}

TEST_CASE("Modenlöser konvergiert mit dem Gitter; Effektiv-Index-Methode liegt in der Nähe") {
  CrossSection coarse, fine;
  coarse.cell_size = 40e-9;
  fine.cell_size = 20e-9;
  coarse.modes = fine.modes = 1;
  const double n_coarse = analyze(coarse, false).modes.front().n_eff;
  const double n_fine = analyze(fine, false).modes.front().n_eff;
  CHECK(std::abs(n_coarse - n_fine) < 0.03);
  // Effektiv-Index-Methode: grobe Näherung, für diesen stark führenden Streifen einige Prozent daneben
  const double n_si = materials::get("Si").index(lambda0), n_ox = materials::get("SiO2").index(lambda0);
  const double eim = effective_index_method(n_si, n_ox, n_ox, n_ox, 500e-9, 220e-9, lambda0, SlabPolarization::te);
  CHECK(eim == doctest::Approx(n_fine).epsilon(0.05));
}

TEST_CASE("Querschnitt: Rippe, Flächenanteile und Fehlermeldungen") {
  CrossSection rib;
  rib.slab_height = 90e-9;
  rib.cell_size = 20e-9;
  std::vector<double> core;
  const FdeGrid grid = build_grid(rib, lambda0, &core);
  double area = 0.0;
  for (double f : core) area += f * grid.dx * grid.dy;
  // Kernfläche = Streifen + Rippenschicht links und rechts
  const double expected = 500e-9 * 220e-9 + (grid.nx * grid.dx - 500e-9) * 90e-9;
  CHECK(area == doctest::Approx(expected).epsilon(1e-9));
  // die Rippe führt stärker als der Streifen (mehr Silizium)
  rib.modes = 1;
  CrossSection strip = rib;
  strip.slab_height = 0.0;
  const auto rib_modes = analyze(rib, false).modes;
  const auto strip_modes = analyze(strip, false).modes;
  REQUIRE_FALSE(rib_modes.empty());
  REQUIRE_FALSE(strip_modes.empty());
  CHECK(rib_modes.front().n_eff > strip_modes.front().n_eff);
  // Führung der Rippe: n_eff liegt über dem der 90-nm-Schicht am Rand (sonst liefe das Licht seitlich weg)
  const double n_si = materials::get("Si").index(lambda0), n_ox = materials::get("SiO2").index(lambda0);
  const double n_slab = slab_modes(n_si, n_ox, n_ox, 90e-9, lambda0, SlabPolarization::te).front().n_eff;
  CHECK(rib_modes.front().guided);
  CHECK(rib_modes.front().n_eff > n_slab);

  CrossSection bad = rib;
  bad.core = "Unobtainium";
  CHECK_THROWS_AS(analyze(bad), ModeError);
  bad = rib, bad.slab_height = 300e-9;
  CHECK_THROWS_AS(analyze(bad), ModeError);
  bad = rib, bad.cell_size = 1e-10;
  CHECK_THROWS_AS(analyze(bad), ModeError);
}
