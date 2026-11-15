// Phase 11: Quanten-Modus (Permanenten, Fock-Verteilungen, HOM, CNOT, Boson Sampling) und MZI-Gitter
// (Clements, Fehler, Kalibrierung, SVD, neuronales Netz).
#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/quantum/experiments.hpp"
#include "photonlab/quantum/fock.hpp"
#include "photonlab/quantum/mesh.hpp"

using namespace photonlab::quantum;

TEST_CASE("Permanente: Einsen-Matrix n!, Einheitsmatrix 1, 3x3 gegen die Definition") {
  for (int n = 1; n <= 8; ++n) {
    double fact = 1.0;
    for (int k = 2; k <= n; ++k) fact *= k;
    CHECK(std::abs(permanent(Matrix::Ones(n, n)) - fact) < 1e-9 * fact);
    CHECK(std::abs(permanent(Matrix::Identity(n, n)) - 1.0) < 1e-12);
  }
  const Matrix a = random_unitary(3, 5);
  // Summe über alle 6 Permutationen
  const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  cplx sum = 0.0;
  for (const auto& p : perms) sum += a(0, p[0]) * a(1, p[1]) * a(2, p[2]);
  CHECK(std::abs(permanent(a) - sum) < 1e-12);
}

TEST_CASE("Fock-Verteilung: Anzahl der Besetzungen und Normierung (ununterscheidbar und unterscheidbar)") {
  CHECK(patterns(5, 3).size() == 35);  // C(7, 3)
  const Matrix u = random_unitary(5, 11);
  for (const double v : {1.0, 0.0, 0.6}) {
    double sum = 0.0;
    for (const auto& [p, prob] : distribution(u, {1, 1, 1, 0, 0}, v)) sum += prob;
    CHECK(sum == doctest::Approx(1.0).epsilon(1e-10).scale(0.0));
  }
  CHECK((random_unitary(6, 3).adjoint() * random_unitary(6, 3)).isIdentity(1e-12));
}

TEST_CASE(
    "Hong-Ou-Mandel: Koinzidenz 0 bei idealen Photonen, Sichtbarkeit = Ununterscheidbarkeit (Abnahme)") {
  HomSettings s;
  HomResult r = hong_ou_mandel(s, 21);
  CHECK(r.coincidence < 1e-15);
  CHECK(r.coincidence_far == doctest::Approx(0.5).epsilon(1e-12).scale(0.0));
  CHECK(r.visibility == doctest::Approx(1.0).epsilon(1e-12).scale(0.0));
  s.indistinguishability = 0.8;
  CHECK(hong_ou_mandel(s, 3).visibility == doctest::Approx(0.8).epsilon(1e-12).scale(0.0));
  // Strahlteiler R = 1/3: Koinzidenz (1 - 2R)^2 = 1/9 statt R^2 + T^2 = 5/9 -> Sichtbarkeit 0,8
  s = HomSettings{};
  s.reflectivity = 1.0 / 3.0;
  r = hong_ou_mandel(s, 3);
  CHECK(r.coincidence == doctest::Approx(1.0 / 9.0).epsilon(1e-12).scale(0.0));
  CHECK(r.visibility == doctest::Approx(0.8).epsilon(1e-12).scale(0.0));
  // Detektor-Effizienz senkt die Koinzidenzen, nicht die Sichtbarkeit; Mehrphotonen und Dunkelzählungen
  // senken sie
  s = HomSettings{};
  s.indistinguishability = 0.9;
  s.detectors.efficiency = 0.5;
  r = hong_ou_mandel(s, 3);
  CHECK(r.coincidence_far == doctest::Approx(0.125).epsilon(1e-12).scale(0.0));
  CHECK(r.visibility == doctest::Approx(0.9).epsilon(1e-9).scale(0.0));
  s = HomSettings{};
  s.multiphoton = 0.05;
  CHECK(hong_ou_mandel(s, 3).visibility < 0.95);
  s = HomSettings{};
  s.detectors.dark_count = 0.01;
  CHECK(hong_ou_mandel(s, 3).visibility < 1.0);
  // Verzögerungskurve: Minimum in der Mitte
  s = HomSettings{};
  r = hong_ou_mandel(s, 41);
  CHECK(r.curve[20] < 1e-15);
  CHECK(r.curve.front() == doctest::Approx(0.5).epsilon(1e-3).scale(0.0));
}

TEST_CASE("Linear-optisches CNOT: Erfolg 1/9, Wahrheitstabelle exakt, Bell-Zustand (Abnahme)") {
  const CnotResult r = linear_optical_cnot();
  CHECK((r.unitary.adjoint() * r.unitary).isIdentity(1e-12));
  const int ideal[4] = {0, 1, 3, 2};
  for (int in = 0; in < 4; ++in) {
    CAPTURE(in);
    CHECK(r.success[static_cast<std::size_t>(in)] == doctest::Approx(1.0 / 9.0).epsilon(1e-12).scale(0.0));
    for (int out = 0; out < 4; ++out)
      CHECK(r.truth[static_cast<std::size_t>(in)][static_cast<std::size_t>(out)] ==
            doctest::Approx(out == ideal[in] ? 1.0 : 0.0).epsilon(1e-12).scale(1.0));
  }
  CHECK(r.fidelity == doctest::Approx(1.0).epsilon(1e-12).scale(0.0));
  CHECK(r.bell_fidelity == doctest::Approx(1.0).epsilon(1e-12).scale(0.0));
  // logische Amplituden: genau CNOT mal -1/3
  const Matrix u = cnot_unitary();
  Pattern in00 = {1, 0, 1, 0, 0, 0}, in10 = {0, 1, 1, 0, 0, 0}, out11 = {0, 1, 0, 1, 0, 0};
  CHECK(std::abs(amplitude(u, in00, in00) + 1.0 / 3.0) < 1e-12);
  CHECK(std::abs(amplitude(u, in10, out11) + 1.0 / 3.0) < 1e-12);
  // teilweise unterscheidbare Photonen: Wahrheitstabelle verschlechtert sich
  CHECK(linear_optical_cnot(1.0 / 3.0, 0.8).fidelity < 0.99);
  // falsche Reflektivität: Gatter wird ungenau
  CHECK(linear_optical_cnot(0.4).fidelity < 0.99);
}

TEST_CASE("Boson Sampling: Stichprobe folgt den Permanenten, unterscheidbare Photonen nicht") {
  const BosonSamplingResult r = boson_sampling(6, 3, 42, 40000);
  double sum = 0.0;
  for (const double p : r.exact) sum += p;
  CHECK(sum == doctest::Approx(1.0).epsilon(1e-10).scale(0.0));
  CHECK(r.outcomes.size() == 56);
  MESSAGE("Abstand Stichprobe " << r.tvd_sampled << ", unterscheidbar " << r.tvd_distinguishable);
  CHECK(r.tvd_sampled < 0.05);
  CHECK(r.tvd_distinguishable > r.tvd_sampled);
}

TEST_CASE("Quanten-Modus: Übertragungsmatrix einer Schaltung (Koppler) und HOM damit") {
  photonlab::circuit::Circuit c;
  c.add("la", "laser");
  c.add("lb", "laser");
  c.add("k", "coupler", {{"power_coupling", 0.5}});
  c.add("d1", "detector");
  c.add("d2", "detector");
  c.connect("la.out", "k.in1");
  c.connect("lb.out", "k.in2");
  c.connect("k.out1", "d1.in");
  c.connect("k.out2", "d2.in");
  const CircuitTransfer t = circuit_transfer(c, photonlab::circuit::ComponentRegistry::builtin(), 1550e-9);
  REQUIRE(t.u.rows() == 2);
  CHECK((t.u.adjoint() * t.u).isIdentity(1e-12));
  const auto dist = distribution(t.u, {1, 1});
  // HOM am Koppler aus dem Chip-Designer; je nach Compiler bleibt ein Rundungsrest statt exakt 0
  const auto coincidence = dist.find({1, 1});
  CHECK((coincidence == dist.end() ? 0.0 : coincidence->second) < 1e-20);
}

TEST_CASE("Clements-Zerlegung: N(N-1)/2 MZIs, Rekonstruktion auf Maschinengenauigkeit (Abnahme)") {
  for (int n = 2; n <= 8; ++n) {
    CAPTURE(n);
    const Matrix u = random_unitary(n, static_cast<std::uint64_t>(100 + n));
    const MeshDecomposition mesh = clements_decompose(u);
    CHECK(static_cast<int>(mesh.mzis.size()) == n * (n - 1) / 2);
    CHECK((reconstruct(mesh) - u).norm() < 1e-11);
    // physikalisches MZI ohne Fehler = T(theta, phi)
    CHECK((realize(mesh, MeshErrors{}) - u).norm() < 1e-11);
  }
  // Fourier-Matrix 4 x 4 und Permutation
  Matrix f(4, 4);
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) f(i, j) = std::exp(cplx(0.0, 2.0 * std::numbers::pi * i * j / 4.0)) / 2.0;
  CHECK((reconstruct(clements_decompose(f)) - f).norm() < 1e-11);
  Matrix perm = Matrix::Zero(4, 4);
  perm(0, 2) = perm(1, 0) = perm(2, 3) = perm(3, 1) = 1.0;
  CHECK((reconstruct(clements_decompose(perm)) - perm).norm() < 1e-11);
  CHECK_THROWS(clements_decompose(Matrix::Ones(3, 3)));
}

TEST_CASE("MZI-Gitter: Fertigungsfehler senken die Güte, Kalibrierung holt sie zurück") {
  const Matrix u = random_unitary(6, 9);
  const MeshDecomposition mesh = clements_decompose(u);
  MeshErrors phase_only;
  phase_only.phase_sigma = 0.1;
  const double f0 = fidelity(u, realize(mesh, phase_only));
  const MeshDecomposition cal = calibrate(mesh, phase_only, u, 60);
  const double f1 = fidelity(u, realize(cal, phase_only));
  MESSAGE("Phasenfehler 0,1 rad: Güte " << f0 << " -> kalibriert " << f1);
  CHECK(f0 < 0.98);
  CHECK(f1 > 0.9999);
  MeshErrors couplers;
  couplers.coupler_sigma = 0.03;
  const double g0 = fidelity(u, realize(mesh, couplers));
  const double g1 = fidelity(u, realize(calibrate(mesh, couplers, u, 60), couplers));
  MESSAGE("Kopplerfehler 3 %: Güte " << g0 << " -> kalibriert " << g1);
  CHECK(g0 < 1.0 - 1e-6);
  CHECK(g1 > g0);
  // Verlust wird in der Güte herausgerechnet, Übersprechen wirkt wie Phasenfehler
  // Verlust: die Wege durch das rechteckige Gitter laufen durch verschieden viele MZIs, daher etwas Unwucht
  MeshErrors loss;
  loss.loss_db = 0.5;
  CHECK(fidelity(u, realize(mesh, loss)) > 0.99);
  MeshErrors xt;
  xt.crosstalk = 0.05;
  CHECK(fidelity(u, realize(mesh, xt)) < 0.9999);
}

TEST_CASE("SVD-Gitter und neuronales Netz: optische Matrixmultiplikation mit Fehlern und Kalibrierung") {
  Eigen::MatrixXd w(3, 3);
  w << 0.5, -1.2, 0.3, 2.0, 0.1, -0.7, -0.4, 0.9, 1.5;
  const SvdMesh m = svd_decompose(w);
  CHECK((svd_realize(m, MeshErrors{}, MeshErrors{}).real() - w).norm() < 1e-10);
  CHECK(svd_realize(m, MeshErrors{}, MeshErrors{}).imag().norm() < 1e-10);
  MeshErrors e;
  e.phase_sigma = 0.15;
  e.coupler_sigma = 0.02;
  const NeuralDemoResult r = neural_network_demo(e);
  MESSAGE("Genauigkeit ideal " << r.accuracy_ideal << ", gefertigt " << r.accuracy_fabricated
                               << ", kalibriert " << r.accuracy_calibrated << " (Güte "
                               << r.fidelity_fabricated << " -> " << r.fidelity_calibrated << ")");
  CHECK(r.accuracy_ideal > 0.9);
  CHECK(r.fidelity_calibrated > r.fidelity_fabricated);
  CHECK(r.accuracy_calibrated >= r.accuracy_fabricated);
  CHECK(r.accuracy_calibrated >= r.accuracy_ideal - 0.03);
}
