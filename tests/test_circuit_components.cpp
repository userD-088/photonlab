// Kompaktmodelle der Phase 1 gegen ihre Formeln: Phase, Verlust, Gruppenindex, Unitarität, Reziprozität.
#include <doctest/doctest.h>

#include <cmath>

#include "photonlab/circuit/registry.hpp"
#include "photonlab/core/units.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {
const ComponentRegistry& reg() { return ComponentRegistry::builtin(); }
const double lambda0 = 1550 * units::nm;

double unitarity_error(const CMatrix& s) {
  return (s.adjoint() * s - CMatrix::Identity(s.rows(), s.cols())).norm();
}
}  // namespace

TEST_CASE("Verlustfreie Bauteile sind unitär, alle Bauteile reziprok") {
  const auto wg = reg().create("waveguide", {{"loss_db_per_cm", 0.0}});
  const auto ps = reg().create("phase_shifter", {{"phase", 1.234}});
  for (double k : {0.0, 0.1, 0.5, 0.9, 1.0}) {
    const auto dc = reg().create("coupler", {{"power_coupling", k}});
    CHECK(unitarity_error(dc->s_matrix(lambda0)) < 1e-14);
    CHECK((dc->s_matrix(lambda0) - dc->s_matrix(lambda0).transpose()).norm() == 0.0);
  }
  CHECK(unitarity_error(wg->s_matrix(lambda0)) < 1e-14);
  CHECK(unitarity_error(ps->s_matrix(lambda0)) < 1e-14);
  CHECK((wg->s_matrix(lambda0) - wg->s_matrix(lambda0).transpose()).norm() == 0.0);
}

TEST_CASE("Richtkoppler: S = [[t, i*kappa], [i*kappa, t]] mit t² + kappa² = 1") {
  const auto dc = reg().create("coupler", {{"power_coupling", 0.3}});
  const CMatrix s = dc->s_matrix(lambda0);
  // Ports: in1, in2, out1, out2
  CHECK(s(2, 0).real() == doctest::Approx(std::sqrt(0.7)).epsilon(1e-14));
  CHECK(s(2, 0).imag() == 0.0);
  CHECK(s(3, 0).imag() == doctest::Approx(std::sqrt(0.3)).epsilon(1e-14));
  CHECK(s(3, 0).real() == 0.0);
  CHECK(s(2, 1) == s(3, 0));
  CHECK(s(3, 1) == s(2, 0));
  CHECK(std::abs(s(0, 0)) == 0.0);  // keine Reflexion
}

TEST_CASE("Wellenleiter: Phase 2*pi*n_eff*L/lambda bei der Bezugswellenlänge") {
  const double length = 10 * units::um, n_eff = 2.44;
  const auto wg = reg().create("waveguide", {{"length", length}, {"n_eff", n_eff}, {"loss_db_per_cm", 0.0}});
  const cplx expected = expi(2.0 * constants::pi * n_eff * length / lambda0);
  CHECK(std::abs(wg->s_matrix(lambda0)(1, 0) - expected) < 1e-12);
}

TEST_CASE("Wellenleiter: Verlust in dB/cm") {
  const auto wg = reg().create("waveguide", {{"length", 1e-2}, {"loss_db_per_cm", 3.0}});
  CHECK(std::norm(wg->s_matrix(lambda0)(1, 0)) == doctest::Approx(from_db(-3.0)).epsilon(1e-12));
}

TEST_CASE("Wellenleiter: Gruppenlaufzeit entspricht dem Gruppenindex") {
  // d(phi)/d(omega) = n_g * L / c0  (Gruppenlaufzeit), numerisch über zentrale Differenz
  const double length = 100 * units::um, n_group = 4.2;
  const auto wg =
      reg().create("waveguide", {{"length", length}, {"n_group", n_group}, {"loss_db_per_cm", 0.0}});
  const double d_lambda = 1e-12;
  const cplx s_plus = wg->s_matrix(lambda0 + d_lambda)(1, 0);
  const cplx s_minus = wg->s_matrix(lambda0 - d_lambda)(1, 0);
  const double d_phase = std::arg(s_plus / s_minus);
  const double d_omega =
      omega_from_wavelength(lambda0 + d_lambda) - omega_from_wavelength(lambda0 - d_lambda);
  CHECK(d_phase / d_omega == doctest::Approx(n_group * length / constants::c0).epsilon(1e-6));
}

TEST_CASE("Phasenschieber: e^{i*phi} und Einfügeverlust") {
  const auto ps = reg().create("phase_shifter", {{"phase", 0.75}, {"insertion_loss_db", 1.0}});
  const cplx tau = ps->s_matrix(lambda0)(1, 0);
  CHECK(std::arg(tau) == doctest::Approx(0.75).epsilon(1e-14));
  CHECK(std::norm(tau) == doctest::Approx(from_db(-1.0)).epsilon(1e-12));
}

TEST_CASE("Laser und Detektor: Leistung, Phase, Photostrom") {
  const auto laser = reg().create("laser", {{"power", 2e-3}, {"phase", constants::pi / 2}});
  const CVector b = laser->source(lambda0);
  CHECK(std::norm(b(0)) == doctest::Approx(2e-3).epsilon(1e-14));
  CHECK(std::arg(b(0)) == doctest::Approx(constants::pi / 2).epsilon(1e-14));

  const auto detector = reg().create("detector", {{"responsivity", 0.8}, {"dark_current", 1e-9}});
  CVector a(1), none = CVector::Zero(1);
  a(0) = std::sqrt(1e-3);
  const auto measured = detector->measure(a, none);
  REQUIRE(measured.size() == 2);
  CHECK(measured[0].value == doctest::Approx(1e-3).epsilon(1e-14));
  CHECK(measured[1].value == doctest::Approx(0.8e-3 + 1e-9).epsilon(1e-14));
}
