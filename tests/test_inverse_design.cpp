// Phase 13: inverses Design mit der Adjoint-Methode (2D-FDTD, Ez)
#include <doctest/doctest.h>

#include <cmath>

#include "photonlab/field/inverse_design.hpp"
#include "photonlab/layout/gds.hpp"
#include "photonlab/layout/layout.hpp"

using namespace photonlab;
using namespace photonlab::field;

TEST_CASE("Filter und Projektion: Grenzwerte, Ableitung, Mittelwert erhalten") {
  CHECK(project(0.0, 8.0) == doctest::Approx(0.0));
  CHECK(project(1.0, 8.0) == doctest::Approx(1.0));
  CHECK(project(0.5, 8.0) == doctest::Approx(0.5));
  for (double x : {0.1, 0.4, 0.6, 0.93}) {
    const double h = 1e-6;
    CHECK(project_derivative(x, 4.0) ==
          doctest::Approx((project(x + h, 4.0) - project(x - h, 4.0)) / (2 * h)).epsilon(1e-6).scale(0.0));
  }
  const std::vector<double> flat(36, 0.3);
  for (double v : cone_filter(flat, 6, 6, 2.5)) CHECK(v == doctest::Approx(0.3));
}

TEST_CASE("Abnahme Phase 13: Adjoint-Gradient = finite Differenzen") {
  InverseDesignSettings settings;
  const BendProblem problem(settings);
  const int n = problem.nx();
  const auto samples = check_gradient(settings, {n * n / 2 + n / 2, 5 + 7 * n, n * n - 3 - 4 * n}, 1e-2);
  for (const auto& s : samples) {
    CAPTURE(s.pixel);
    MESSAGE("Pixel " << s.pixel << ": Adjoint " << s.adjoint << ", Differenzen " << s.finite_difference);
    CHECK(s.adjoint == doctest::Approx(s.finite_difference).epsilon(0.01).scale(0.0));
  }
}

TEST_CASE("Abnahme Phase 13: inverses Design eines 90-Grad-Knicks, binär und als GDSII") {
  InverseDesignSettings settings;
  settings.iterations = 16;
  int calls = 0;
  const InverseDesignResult r =
      run_inverse_design(settings, nullptr, [&](const InverseDesignProgress&) { ++calls; });
  REQUIRE(r.completed);
  MESSAGE("Transmission: Start " << r.transmission_start << ", stetig " << r.transmission_final << ", binär "
                                 << r.transmission_binary << ", leeres Gebiet " << r.transmission_empty);
  CHECK(calls == 16);
  CHECK(r.history.size() == 16);
  CHECK(r.transmission_start < 0.05);
  CHECK(r.transmission_empty < 0.05);
  CHECK(r.transmission_binary > 0.9);
  for (double t : r.spectrum_binary) CHECK((t >= 0.0 && t <= 1.02));  // passiv (bis auf Gitterfehler)
  // Layout: Fläche der Design-Polygone = Zahl der Kern-Pixel
  const layout::Layout lay = layout::inverse_design_layout(r);
  double core_pixels = 0.0;
  for (double b : r.binary) core_pixels += b;
  double area = 0.0;
  for (std::size_t k = 2; k < lay.cells[0].polygons.size(); ++k)
    area += layout::signed_area(lay.cells[0].polygons[k]);
  const double pixel_nm = r.pixel_size / 1e-9;
  CHECK(area == doctest::Approx(core_pixels * pixel_nm * pixel_nm).epsilon(1e-9).scale(0.0));
  CHECK(layout::read_gds(layout::write_gds(lay)).cells[0].polygons.size() == lay.cells[0].polygons.size());
  // Feld-Szene des Designs gibt an jedem Knoten des Designgebiets genau das binäre Material wieder
  const FieldScene scene = design_scene(r);
  int mismatches = 0;
  for (int j = 0; j < r.ny; ++j)
    for (int i = 0; i < r.nx; ++i) {
      const double x = r.pixel_x0 + (i + 0.5) * r.pixel_size, y = r.pixel_y0 + (j + 0.5) * r.pixel_size;
      const bool core = index_at(scene, x, y) > 2.0;
      if (core != (r.binary[static_cast<std::size_t>(i + r.nx * j)] > 0.5)) ++mismatches;
    }
  CHECK(mismatches == 0);
  CHECK(scene.duration > 0.0);
}
