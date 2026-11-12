// Erster Physik-Test: Mach-Zehnder-Interferometer gegen die analytische Lösung.
#include <doctest/doctest.h>

#include <cmath>

#include "photonlab/core/complex.hpp"
#include "photonlab/core/units.hpp"

using namespace photonlab;

TEST_CASE("MZI: Ausgang 2 folgt cos^2(dphi/2) und Energie bleibt erhalten") {
  const double s = std::sqrt(0.5);
  for (int k = 0; k <= 64; ++k) {
    const double dphi = 2.0 * constants::pi * k / 64.0;
    const cplx in = 1.0;
    const cplx u1 = s * in, u2 = I * s * in;
    const cplx v1 = u1 * expi(dphi), v2 = u2;
    const cplx out1 = s * (v1 + I * v2), out2 = s * (I * v1 + v2);
    const double expect2 = std::pow(std::cos(dphi / 2.0), 2);
    CHECK(power(out2) == doctest::Approx(expect2).epsilon(1e-12));
    CHECK(power(out1) + power(out2) == doctest::Approx(1.0).epsilon(1e-12));
  }
}
