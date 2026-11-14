#include "photonlab/quantum/fock.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <stdexcept>

#include "photonlab/circuit/monte_carlo.hpp"

namespace photonlab::quantum {

namespace {
double factorial(int n) {
  double f = 1.0;
  for (int k = 2; k <= n; ++k) f *= k;
  return f;
}
double norm_factor(const Pattern& p) {
  double f = 1.0;
  for (const int k : p) f *= factorial(k);
  return f;
}
// Liste der Moden je Photon: {2, 0, 1} -> {0, 0, 2}
std::vector<int> expand(const Pattern& p) {
  std::vector<int> modes;
  for (std::size_t m = 0; m < p.size(); ++m)
    for (int k = 0; k < p[m]; ++k) modes.push_back(static_cast<int>(m));
  return modes;
}
int total(const Pattern& p) {
  int n = 0;
  for (const int k : p) n += k;
  return n;
}
}  // namespace

cplx permanent(const Matrix& a) {
  const int n = static_cast<int>(a.rows());
  if (a.cols() != n) throw std::invalid_argument("Permanente braucht eine quadratische Matrix");
  if (n == 0) return 1.0;
  if (n > 30) throw std::invalid_argument("Permanente: höchstens 30 x 30");
  // Ryser mit Gray-Code: per(A) = (-1)^n sum_S (-1)^|S| prod_i sum_{j in S} a_ij
  std::vector<cplx> row_sum(static_cast<std::size_t>(n), 0.0);
  cplx result = 0.0;
  std::uint64_t gray = 0;
  const std::uint64_t subsets = std::uint64_t{1} << n;
  for (std::uint64_t k = 1; k < subsets; ++k) {
    const std::uint64_t next = k ^ (k >> 1);
    const std::uint64_t changed = next ^ gray;
    const int j = std::countr_zero(changed);
    const double sign = (next & changed) ? 1.0 : -1.0;
    for (int i = 0; i < n; ++i) row_sum[static_cast<std::size_t>(i)] += sign * a(i, j);
    gray = next;
    cplx prod = 1.0;
    for (int i = 0; i < n; ++i) prod *= row_sum[static_cast<std::size_t>(i)];
    const int bits = std::popcount(gray);
    result += ((n - bits) % 2 == 0 ? 1.0 : -1.0) * prod;
  }
  return result;
}

double permanent_real(const Eigen::MatrixXd& a) { return permanent(a.cast<cplx>()).real(); }

std::vector<Pattern> patterns(int modes, int photons) {
  std::vector<Pattern> list;
  Pattern p(static_cast<std::size_t>(modes), 0);
  std::function<void(int, int)> rec = [&](int mode, int left) {
    if (mode == modes - 1) {
      p[static_cast<std::size_t>(mode)] = left;
      list.push_back(p);
      return;
    }
    for (int k = left; k >= 0; --k) {
      p[static_cast<std::size_t>(mode)] = k;
      rec(mode + 1, left - k);
    }
  };
  if (modes > 0) rec(0, photons);
  return list;
}

cplx amplitude(const Matrix& u, const Pattern& in, const Pattern& out) {
  if (total(in) != total(out)) return 0.0;
  const std::vector<int> cols = expand(in), rows = expand(out);
  const int n = static_cast<int>(cols.size());
  Matrix sub(n, n);
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      sub(i, j) = u(rows[static_cast<std::size_t>(i)], cols[static_cast<std::size_t>(j)]);
  return permanent(sub) / std::sqrt(norm_factor(in) * norm_factor(out));
}

std::map<Pattern, double> distribution(const Matrix& u, const Pattern& in, double indistinguishability) {
  const int m = static_cast<int>(u.rows());
  if (static_cast<int>(in.size()) != u.cols())
    throw std::invalid_argument("Eingangsbesetzung passt nicht zu U");
  const int n = total(in);
  const double v = std::clamp(indistinguishability, 0.0, 1.0);
  std::map<Pattern, double> dist;
  const std::vector<int> cols = expand(in);
  for (const Pattern& out : patterns(m, n)) {
    const std::vector<int> rows = expand(out);
    Matrix sub(n, n);
    Eigen::MatrixXd sub_abs(n, n);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        sub(i, j) = u(rows[static_cast<std::size_t>(i)], cols[static_cast<std::size_t>(j)]);
        sub_abs(i, j) = std::norm(sub(i, j));
      }
    const double norm = norm_factor(in) * norm_factor(out);
    const double p_ind = std::norm(permanent(sub)) / norm;
    // Unterscheidbare Photonen: jedes Photon läuft für sich; Wahrscheinlichkeit = per(|U_sub|^2) / (prod in!
    // prod out!)
    const double p_dist = v < 1.0 ? permanent_real(sub_abs) / norm : 0.0;
    const double p = v * p_ind + (1.0 - v) * p_dist;
    if (p > 1e-300) dist[out] = p;
  }
  return dist;
}

std::map<Pattern, double> distribution_mixed(const Matrix& u,
                                             const std::vector<std::pair<double, Pattern>>& inputs,
                                             double indistinguishability) {
  std::map<Pattern, double> dist;
  for (const auto& [weight, in] : inputs)
    for (const auto& [out, p] : distribution(u, in, indistinguishability)) dist[out] += weight * p;
  return dist;
}

std::map<Pattern, double> detect(const std::map<Pattern, double>& dist, int modes, const Detectors& d) {
  // Je Mode: k Photonen -> j erkannt mit Binomial(k, eta); dann Dunkelzählung (ein zusätzliches Ereignis mit
  // p_dark); Schwellwert-Detektor: Klick, wenn mindestens ein Ereignis
  std::map<Pattern, double> result;
  for (const auto& [pattern, p] : dist) {
    // Verteilung je Mode über erkannte Zählungen, dann Produkt über die Moden
    std::vector<std::vector<double>> per_mode(static_cast<std::size_t>(modes));
    for (int m = 0; m < modes; ++m) {
      const int k = pattern[static_cast<std::size_t>(m)];
      std::vector<double> counts(static_cast<std::size_t>(k + 2), 0.0);
      for (int j = 0; j <= k; ++j) {
        double binom = 1.0;
        for (int i = 0; i < j; ++i) binom *= static_cast<double>(k - i) / (i + 1);
        const double pj = binom * std::pow(d.efficiency, j) * std::pow(1.0 - d.efficiency, k - j);
        counts[static_cast<std::size_t>(j)] += pj * (1.0 - d.dark_count);
        counts[static_cast<std::size_t>(j + 1)] += pj * d.dark_count;
      }
      if (!d.number_resolving) {
        double click = 0.0;
        for (std::size_t j = 1; j < counts.size(); ++j) click += counts[j];
        counts = {counts[0], click};
      }
      per_mode[static_cast<std::size_t>(m)] = counts;
    }
    std::function<void(int, Pattern&, double)> rec = [&](int m, Pattern& clicks, double prob) {
      if (prob < 1e-300) return;
      if (m == modes) {
        result[clicks] += prob;
        return;
      }
      const auto& c = per_mode[static_cast<std::size_t>(m)];
      for (std::size_t j = 0; j < c.size(); ++j) {
        clicks[static_cast<std::size_t>(m)] = static_cast<int>(j);
        rec(m + 1, clicks, prob * c[j]);
      }
    };
    Pattern clicks(static_cast<std::size_t>(modes), 0);
    rec(0, clicks, p);
  }
  return result;
}

Matrix random_unitary(int n, std::uint64_t seed) {
  Matrix g(n, n);
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      g(i, j) = cplx(circuit::standard_normal(seed, static_cast<std::uint64_t>(i),
                                              static_cast<std::uint64_t>(j), 0),
                     circuit::standard_normal(seed, static_cast<std::uint64_t>(i),
                                              static_cast<std::uint64_t>(j), 1)) /
                std::sqrt(2.0);
  Eigen::HouseholderQR<Matrix> qr(g);
  Matrix q = qr.householderQ();
  const Matrix r = qr.matrixQR().triangularView<Eigen::Upper>();
  // Phasen der Diagonale von R abspalten (Mezzadri 2007), sonst ist die Verteilung nicht Haar-gleichverteilt
  for (int j = 0; j < n; ++j) {
    const cplx d = r(j, j);
    q.col(j) *= d / std::abs(d);
  }
  return q;
}

Matrix beam_splitter(int modes, int a, int b, double reflectivity) {
  Matrix u = Matrix::Identity(modes, modes);
  const double r = std::sqrt(reflectivity), t = std::sqrt(1.0 - reflectivity);
  u(a, a) = r;
  u(a, b) = t;
  u(b, a) = t;
  u(b, b) = -r;
  return u;
}

Matrix phase(int modes, int a, double phi) {
  Matrix u = Matrix::Identity(modes, modes);
  u(a, a) = std::exp(cplx(0.0, phi));
  return u;
}

}  // namespace photonlab::quantum
