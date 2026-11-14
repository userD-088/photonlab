#include "photonlab/quantum/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "photonlab/circuit/monte_carlo.hpp"

namespace photonlab::quantum {

namespace {

constexpr double kPi = std::numbers::pi;

// Kopplungsmatrix eines Richtkopplers mit Teilungsverhältnis eta (Leistung, die gerade durchgeht)
Eigen::Matrix2cd coupler(double eta) {
  const double a = std::sqrt(std::clamp(eta, 0.0, 1.0)), b = std::sqrt(std::clamp(1.0 - eta, 0.0, 1.0));
  Eigen::Matrix2cd c;
  c << a, cplx(0.0, b), cplx(0.0, b), a;
  return c;
}

// Physikalisches MZI: Eingang mit festem pi auf dem unteren Arm, äußere Phase phi oben, Koppler, symmetrische
// innere Phase +-theta', Koppler, feste Ausgangsphase -pi/2 auf beiden Armen. Mit idealen Kopplern ist das
// genau T(theta, phi) bei theta' = pi/2 - theta (Herleitung in docs/physics/quantum.md).
Eigen::Matrix2cd physical_mzi(double theta, double phi, double eta1, double eta2) {
  const double inner = kPi / 2.0 - theta;
  Eigen::Matrix2cd p_ext, p_int, k_in;
  p_ext << std::exp(cplx(0.0, phi)), 0.0, 0.0, 1.0;
  p_int << std::exp(cplx(0.0, inner)), 0.0, 0.0, std::exp(cplx(0.0, -inner));
  k_in << 1.0, 0.0, 0.0, -1.0;
  return cplx(0.0, -1.0) * coupler(eta2) * p_int * coupler(eta1) * p_ext * k_in;
}

Eigen::Matrix2cd t_block(double theta, double phi) {
  Eigen::Matrix2cd t;
  t << std::exp(cplx(0.0, phi)) * std::cos(theta), -std::sin(theta),
      std::exp(cplx(0.0, phi)) * std::sin(theta), std::cos(theta);
  return t;
}

void apply_left(Matrix& u, int mode, const Eigen::Matrix2cd& b) {  // u <- B u auf den Zeilen (mode, mode+1)
  const Eigen::MatrixXcd rows = u.middleRows(mode, 2);
  u.middleRows(mode, 2) = b * rows;
}

void apply_right(Matrix& u, int mode, const Eigen::Matrix2cd& b) {  // u <- u B auf den Spalten (mode, mode+1)
  const Eigen::MatrixXcd cols = u.middleCols(mode, 2);
  u.middleCols(mode, 2) = cols * b;
}

double normal(std::uint64_t seed, std::uint64_t a, std::uint64_t b) {
  return circuit::standard_normal(seed, a, b, 13);
}

}  // namespace

Matrix mzi_matrix(int size, const Mzi& mzi) {
  Matrix u = Matrix::Identity(size, size);
  u.block(mzi.mode, mzi.mode, 2, 2) = t_block(mzi.theta, mzi.phi);
  return u;
}

MeshDecomposition clements_decompose(const Matrix& u_in) {
  const int n = static_cast<int>(u_in.rows());
  if (u_in.cols() != n) throw std::invalid_argument("Clements-Zerlegung braucht eine quadratische Matrix");
  if (!(u_in.adjoint() * u_in).isIdentity(1e-9))
    throw std::invalid_argument("Clements-Zerlegung braucht eine unitäre Matrix");
  Matrix u = u_in;
  std::vector<Mzi> right, left;  // in Reihenfolge der Anwendung
  for (int i = 0; i < n - 1; ++i) {
    if (i % 2 == 0) {
      for (int j = 0; j <= i; ++j) {
        // Element (n-1-j, i-j) mit T^-1 von rechts auf den Spalten (i-j, i-j+1) auf null
        const int r = n - 1 - j, m = i - j;
        const cplx a = u(r, m), b = u(r, m + 1);
        const double theta = std::atan2(std::abs(a), std::abs(b));
        const double phi = (std::abs(a) > 0.0 && std::abs(b) > 0.0) ? std::arg(a) - std::arg(b) : 0.0;
        apply_right(u, m, t_block(theta, phi).adjoint());
        right.push_back({m, theta, phi});
      }
    } else {
      for (int j = 1; j <= i + 1; ++j) {
        // Element (n+j-i-2, j-1) mit T von links auf den Zeilen (n+j-i-3, n+j-i-2) auf null
        const int m = n + j - i - 3, c = j - 1;
        const cplx a = u(m, c), b = u(m + 1, c);
        const double theta = std::atan2(std::abs(b), std::abs(a));
        const double phi = (std::abs(a) > 0.0 && std::abs(b) > 0.0) ? kPi + std::arg(b) - std::arg(a) : 0.0;
        apply_left(u, m, t_block(theta, phi));
        left.push_back({m, theta, phi});
      }
    }
  }
  // Jetzt ist u diagonal: T_Lk ... T_L1 U0 T_R1^-1 ... T_Rr^-1 = D. Linke T^-1 durch D schieben: T^-1 D = D'
  // T'
  Eigen::VectorXcd d = u.diagonal();
  std::vector<Mzi> moved;  // T'_k, ..., T'_1 (in Reihenfolge der Bildung: zuerst das innerste)
  for (auto it = left.rbegin(); it != left.rend(); ++it) {
    const int m = it->mode;
    Eigen::Matrix2cd dd = Eigen::Matrix2cd::Zero();
    dd(0, 0) = d(m), dd(1, 1) = d(m + 1);
    const Eigen::Matrix2cd b = t_block(it->theta, it->phi).adjoint() * dd;
    // b = D' T'(theta', phi'): |b11| = cos theta', |b01| = sin theta'
    const double theta = std::atan2(std::abs(b(0, 1)), std::abs(b(1, 1)));
    const double c = std::cos(theta), s = std::sin(theta);
    cplx d1, d2, e_phi;
    if (s > 1e-12 && c > 1e-12) {
      d1 = -b(0, 1) / s, d2 = b(1, 1) / c, e_phi = b(0, 0) / (d1 * c);
    } else if (s <= 1e-12) {  // theta' = 0: T' = diag(e^{i phi}, 1)
      d2 = b(1, 1), d1 = b(0, 0), e_phi = 1.0;
    } else {  // theta' = pi/2: T' = [[0, -1], [e^{i phi}, 0]]
      d1 = -b(0, 1), d2 = b(1, 0), e_phi = 1.0;
    }
    d(m) = d1, d(m + 1) = d2;
    moved.push_back({m, theta, std::arg(e_phi)});
  }
  // U0 = D' T'_1 ... T'_k T_Rr ... T_R1: Anwendung vom Eingang: T_R1, ..., T_Rr, T'_k, ..., T'_1
  MeshDecomposition mesh;
  mesh.size = n;
  for (const Mzi& m : right) mesh.mzis.push_back(m);
  for (const Mzi& m : moved) mesh.mzis.push_back(m);  // moved ist T'_k ... T'_1 in Bildungsreihenfolge
  for (int k = 0; k < n; ++k) mesh.output_phases.push_back(std::arg(d(k)));
  return mesh;
}

Matrix reconstruct(const MeshDecomposition& mesh) {
  Matrix u = Matrix::Identity(mesh.size, mesh.size);
  for (const Mzi& m : mesh.mzis) apply_left(u, m.mode, t_block(m.theta, m.phi));
  for (int k = 0; k < mesh.size; ++k)
    u.row(k) *= std::exp(cplx(0.0, mesh.output_phases[static_cast<std::size_t>(k)]));
  return u;
}

Matrix realize(const MeshDecomposition& mesh, const MeshErrors& e) {
  Matrix u = Matrix::Identity(mesh.size, mesh.size);
  const double loss = std::pow(10.0, -e.loss_db / 20.0);
  for (std::size_t k = 0; k < mesh.mzis.size(); ++k) {
    const Mzi& m = mesh.mzis[k];
    // Übersprechen: Nachbarn derselben Spalte (benachbarte Liste, Moden um 2 versetzt) heizen mit
    double leak = 0.0;
    for (const std::size_t j : {k - 1, k + 1})
      if (j < mesh.mzis.size() && std::abs(mesh.mzis[j].mode - m.mode) == 2)
        leak += e.crosstalk * (kPi / 2.0 - mesh.mzis[j].theta);
    const double eta1 = 0.5 + e.coupler_sigma * normal(e.seed, k, 1),
                 eta2 = 0.5 + e.coupler_sigma * normal(e.seed, k, 2);
    const double theta = m.theta + e.phase_sigma * normal(e.seed, k, 3) - leak;
    const double phi = m.phi + e.phase_sigma * normal(e.seed, k, 4);
    apply_left(u, m.mode, loss * physical_mzi(theta, phi, eta1, eta2));
  }
  for (int k = 0; k < mesh.size; ++k)
    u.row(k) *=
        std::exp(cplx(0.0, mesh.output_phases[static_cast<std::size_t>(k)] +
                               e.phase_sigma * normal(e.seed, 100000 + static_cast<std::uint64_t>(k), 5)));
  return u;
}

double fidelity(const Matrix& target, const Matrix& realized) {
  const double n = static_cast<double>(target.rows());
  const double power = (realized.adjoint() * realized).trace().real();
  if (!(power > 0.0)) return 0.0;
  return std::norm((target.adjoint() * realized).trace()) / (n * power);
}

MeshDecomposition calibrate(const MeshDecomposition& start, const MeshErrors& errors, const Matrix& target,
                            int sweeps) {
  MeshDecomposition mesh = start;
  double best = fidelity(target, realize(mesh, errors));
  double step = 0.2;
  for (int s = 0; s < sweeps && best < 1.0 - 1e-12; ++s) {
    bool improved = false;
    auto try_param = [&](double& x) {
      for (const double d : {step, -step}) {
        const double old = x;
        x = old + d;
        const double f = fidelity(target, realize(mesh, errors));
        if (f > best) {
          best = f;
          improved = true;
          return;
        }
        x = old;
      }
    };
    for (Mzi& m : mesh.mzis) {
      try_param(m.theta);
      try_param(m.phi);
    }
    for (double& p : mesh.output_phases) try_param(p);
    if (!improved) step *= 0.5;
    if (step < 1e-6) break;
  }
  return mesh;
}

SvdMesh svd_decompose(const Eigen::MatrixXd& w) {
  Eigen::JacobiSVD<Eigen::MatrixXd> svd(w, Eigen::ComputeFullU | Eigen::ComputeFullV);
  SvdMesh m;
  const Eigen::VectorXd s = svd.singularValues();
  m.scale = s.size() > 0 ? s(0) : 1.0;
  for (int k = 0; k < s.size(); ++k) m.attenuation.push_back(m.scale > 0.0 ? s(k) / m.scale : 0.0);
  m.u = clements_decompose(svd.matrixU().cast<cplx>());
  m.v = clements_decompose(svd.matrixV().transpose().cast<cplx>());
  return m;
}

Matrix svd_realize(const SvdMesh& mesh, const MeshErrors& eu, const MeshErrors& ev) {
  const Matrix u = realize(mesh.u, eu), v = realize(mesh.v, ev);
  Matrix s = Matrix::Zero(u.cols(), v.rows());
  for (std::size_t k = 0; k < mesh.attenuation.size(); ++k)
    s(static_cast<Eigen::Index>(k), static_cast<Eigen::Index>(k)) = mesh.attenuation[k];
  return u * s * v * mesh.scale;
}

NeuralDemoResult neural_network_demo(const MeshErrors& errors, std::uint64_t seed) {
  // Drei Klassen in 4 Merkmalen: Gauß-Wolken um feste Zentren
  const int per_class = 60, classes = 3, features = 4, hidden = 4;
  const double centers[3][4] = {{1.0, 0.2, -0.5, 0.3}, {-0.6, 1.0, 0.4, -0.4}, {0.2, -0.8, 1.0, 0.6}};
  std::vector<Eigen::VectorXd> x;
  std::vector<int> y;
  for (int c = 0; c < classes; ++c)
    for (int k = 0; k < per_class; ++k) {
      Eigen::VectorXd v(features);
      for (int f = 0; f < features; ++f)
        v(f) = centers[c][f] + 0.3 * circuit::standard_normal(seed,
                                                              static_cast<std::uint64_t>(c * per_class + k),
                                                              static_cast<std::uint64_t>(f), 21);
      x.push_back(v);
      y.push_back(c);
    }
  // Netz: h = (W1 x)^2 (Photodetektion), z = W2 h + b; Training mit Gradientenabstieg (Kreuzentropie)
  Eigen::MatrixXd w1(hidden, features), w2(classes, hidden);
  Eigen::VectorXd b2 = Eigen::VectorXd::Zero(classes);
  for (int i = 0; i < hidden; ++i)
    for (int j = 0; j < features; ++j) w1(i, j) = 0.5 * circuit::standard_normal(seed, i, j, 31);
  for (int i = 0; i < classes; ++i)
    for (int j = 0; j < hidden; ++j) w2(i, j) = 0.5 * circuit::standard_normal(seed, i, j, 32);
  const double rate = 0.05;
  for (int epoch = 0; epoch < 1500; ++epoch) {
    Eigen::MatrixXd g1 = Eigen::MatrixXd::Zero(hidden, features), g2 = Eigen::MatrixXd::Zero(classes, hidden);
    Eigen::VectorXd gb = Eigen::VectorXd::Zero(classes);
    for (std::size_t n = 0; n < x.size(); ++n) {
      const Eigen::VectorXd a = w1 * x[n];
      const Eigen::VectorXd h = a.array().square();
      Eigen::VectorXd z = w2 * h + b2;
      z.array() -= z.maxCoeff();
      Eigen::VectorXd p = z.array().exp();
      p /= p.sum();
      p(y[n]) -= 1.0;  // dL/dz
      g2 += p * h.transpose();
      gb += p;
      const Eigen::VectorXd dh = w2.transpose() * p;
      g1 += (2.0 * a.array() * dh.array()).matrix() * x[n].transpose();
    }
    const double inv = 1.0 / static_cast<double>(x.size());
    w1 -= rate * inv * g1;
    w2 -= rate * inv * g2;
    b2 -= rate * inv * gb;
  }
  // Die Photodioden lesen |W x|^2 des komplexen Feldes (bei einem idealen Gitter ist W reell)
  auto accuracy = [&](const Matrix& w) {
    int ok = 0;
    for (std::size_t n = 0; n < x.size(); ++n) {
      const Eigen::VectorXd h = (w * x[n].cast<cplx>()).cwiseAbs2();
      const Eigen::VectorXd z = w2 * h + b2;
      Eigen::Index best = 0;
      z.maxCoeff(&best);
      ok += static_cast<int>(best) == y[n] ? 1 : 0;
    }
    return static_cast<double>(ok) / static_cast<double>(x.size());
  };
  NeuralDemoResult r;
  r.samples = static_cast<int>(x.size());
  r.accuracy_ideal = accuracy(w1.cast<cplx>());
  SvdMesh mesh = svd_decompose(w1);
  MeshErrors eu = errors, ev = errors;
  ev.seed = errors.seed + 1000;
  const Matrix fabricated = svd_realize(mesh, eu, ev);
  r.accuracy_fabricated = accuracy(fabricated);
  const Matrix tu = reconstruct(mesh.u), tv = reconstruct(mesh.v);
  r.fidelity_fabricated = 0.5 * (fidelity(tu, realize(mesh.u, eu)) + fidelity(tv, realize(mesh.v, ev)));
  SvdMesh calibrated = mesh;
  calibrated.u = calibrate(mesh.u, eu, tu);
  calibrated.v = calibrate(mesh.v, ev, tv);
  r.accuracy_calibrated = accuracy(svd_realize(calibrated, eu, ev));
  r.fidelity_calibrated =
      0.5 * (fidelity(tu, realize(calibrated.u, eu)) + fidelity(tv, realize(calibrated.v, ev)));
  return r;
}

}  // namespace photonlab::quantum
