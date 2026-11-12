#include <doctest/doctest.h>

#include "photonlab/core/units.hpp"

using namespace photonlab;

TEST_CASE("Einheiten werden in SI umgerechnet") {
  CHECK(1550 * units::nm == doctest::Approx(1.55e-6));
  CHECK(1 * units::um == doctest::Approx(1000 * units::nm));
}

TEST_CASE("1550 nm entsprechen etwa 193,4 THz") {
  CHECK(frequency_from_wavelength(1550 * units::nm) / 1e12 == doctest::Approx(193.41).epsilon(1e-4));
}

TEST_CASE("Vakuum: c0 = 1/sqrt(eps0*mu0)") {
  CHECK(constants::eps0 * constants::mu0 * constants::c0 * constants::c0 == doctest::Approx(1.0));
}

TEST_CASE("Dezibel-Umrechnung") {
  CHECK(to_db(100.0) == doctest::Approx(20.0));
  CHECK(from_db(-3.0) == doctest::Approx(0.501187).epsilon(1e-5));
}
