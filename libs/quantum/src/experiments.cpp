#include "photonlab/quantum/experiments.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "photonlab/circuit/monte_carlo.hpp"
#include "photonlab/circuit/solver.hpp"

namespace photonlab::quantum {

namespace {
// Koinzidenz an zwei Ausgängen nach den Detektoren
double coincidence(const Matrix& u, const HomSettings& s, double overlap) {
  const double p2 = s.multiphoton;
  const std::vector<std::pair<double, Pattern>> inputs = {
      {(1 - p2) * (1 - p2), {1, 1}}, {p2 * (1 - p2), {2, 1}}, {(1 - p2) * p2, {1, 2}}, {p2 * p2, {2, 2}}};
  const auto dist = distribution_mixed(u, inputs, overlap);
  Detectors d = s.detectors;
  d.number_resolving = false;
  const auto clicks = detect(dist, 2, d);
  const auto it = clicks.find({1, 1});
  return it == clicks.end() ? 0.0 : it->second;
}
}  // namespace

HomResult hong_ou_mandel(const HomSettings& s, int points) {
  const Matrix u = beam_splitter(2, 0, 1, s.reflectivity);
  HomResult r;
  r.coincidence = coincidence(u, s, s.indistinguishability);
  r.coincidence_far = coincidence(u, s, 0.0);
  r.visibility = r.coincidence_far > 0.0 ? 1.0 - r.coincidence / r.coincidence_far : 0.0;
  for (int k = 0; k < points; ++k) {
    const double tau = (-3.0 + 6.0 * k / (points - 1)) * s.coherence_time;
    const double overlap = s.indistinguishability * std::exp(-std::pow(tau / s.coherence_time, 2));
    r.delay.push_back(tau);
    r.curve.push_back(coincidence(u, s, overlap));
  }
  return r;
}

Matrix cnot_unitary(double reflectivity) {
  // Moden: 0 c0, 1 c1, 2 t0, 3 t1, 4 Hilfsmode a, 5 Hilfsmode b
  const int m = 6;
  const Matrix h = beam_splitter(m, 2, 3, 0.5);  // Hadamard auf das Ziel: t+ in Mode 2, t- in Mode 3
  // Kontrolliertes Vorzeichen: c1 trifft t+ auf R = 1/3; c0 und t- werden mit R = 1/3 gleich abgeschwächt
  Matrix bs = beam_splitter(m, 1, 2, reflectivity);
  Matrix att_c0 = beam_splitter(m, 0, 4, reflectivity);
  Matrix att_tm = beam_splitter(m, 3, 5, reflectivity);
  // gleiche Vorzeichen wie t+ (-sqrt R) für den abgeschwächten Arm t-
  att_tm(3, 3) = -std::sqrt(reflectivity);
  att_tm(5, 5) = std::sqrt(reflectivity);
  // fester Phasenschieber pi auf c1 am Ausgang: ohne ihn ist das Gatter CNOT mal Z auf der Kontrolle
  return phase(m, 1, std::numbers::pi) * h * att_tm * att_c0 * bs * h;
}

CnotResult linear_optical_cnot(double reflectivity, double indistinguishability) {
  CnotResult r;
  r.unitary = cnot_unitary(reflectivity);
  // logische Zustände |c t>: c in Mode c, t in Mode 2 + t
  auto logical = [](int c, int t) {
    Pattern p(6, 0);
    p[static_cast<std::size_t>(c)] = 1;
    p[static_cast<std::size_t>(2 + t)] = 1;
    return p;
  };
  const int ideal_out[4] = {0, 1, 3, 2};  // CNOT: 00->00, 01->01, 10->11, 11->10
  double fidelity = 0.0;
  for (int in = 0; in < 4; ++in) {
    const auto dist = distribution(r.unitary, logical(in >> 1, in & 1), indistinguishability);
    double success = 0.0;
    std::array<double, 4> p{};
    for (int out = 0; out < 4; ++out) {
      const auto it = dist.find(logical(out >> 1, out & 1));
      p[static_cast<std::size_t>(out)] = it == dist.end() ? 0.0 : it->second;
      success += p[static_cast<std::size_t>(out)];
    }
    r.success[static_cast<std::size_t>(in)] = success;
    for (int out = 0; out < 4; ++out)
      r.truth[static_cast<std::size_t>(in)][static_cast<std::size_t>(out)] =
          success > 0 ? p[static_cast<std::size_t>(out)] / success : 0.0;
    fidelity += r.truth[static_cast<std::size_t>(in)][static_cast<std::size_t>(ideal_out[in])];
  }
  r.fidelity = fidelity / 4.0;
  // Bell-Zustand: Hadamard (50:50) auf die Kontrolle, Eingang |c0, t0> -> nachselektiert (|00> + |11>)/sqrt2
  const Matrix u_bell = r.unitary * beam_splitter(6, 0, 1, 0.5);
  std::array<cplx, 4> amp{};
  double norm = 0.0;
  for (int out = 0; out < 4; ++out) {
    amp[static_cast<std::size_t>(out)] = amplitude(u_bell, logical(0, 0), logical(out >> 1, out & 1));
    norm += std::norm(amp[static_cast<std::size_t>(out)]);
  }
  const cplx overlap = (amp[0] + amp[3]) / std::sqrt(2.0);
  r.bell_fidelity = norm > 0.0 ? std::norm(overlap) / norm : 0.0;
  return r;
}

BosonSamplingResult boson_sampling(int modes, int photons, std::uint64_t seed, int samples) {
  if (photons > modes || photons < 1) throw std::invalid_argument("Boson Sampling: 1 <= Photonen <= Moden");
  const Matrix u = random_unitary(modes, seed);
  Pattern in(static_cast<std::size_t>(modes), 0);
  for (int k = 0; k < photons; ++k) in[static_cast<std::size_t>(k)] = 1;
  const auto exact = distribution(u, in, 1.0);
  const auto dist = distribution(u, in, 0.0);
  BosonSamplingResult r;
  r.samples = samples;
  for (const Pattern& p : patterns(modes, photons)) {
    r.outcomes.push_back(p);
    const auto e = exact.find(p), d = dist.find(p);
    r.exact.push_back(e == exact.end() ? 0.0 : e->second);
    r.distinguishable.push_back(d == dist.end() ? 0.0 : d->second);
  }
  // Stichprobe: gleichverteilte Zahl aus standard_normal über die Normalverteilung, dann kumulative Suche
  std::vector<double> cumulative(r.exact.size());
  double acc = 0.0;
  for (std::size_t k = 0; k < r.exact.size(); ++k) cumulative[k] = acc += r.exact[k];
  std::vector<int> counts(r.exact.size(), 0);
  for (int s = 0; s < samples; ++s) {
    const double z = circuit::standard_normal(seed, static_cast<std::uint64_t>(s), 77, 0);
    const double uniform = 0.5 * std::erfc(-z / std::sqrt(2.0)) * acc;
    const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), uniform);
    ++counts[static_cast<std::size_t>(
        std::min<std::ptrdiff_t>(it - cumulative.begin(), static_cast<std::ptrdiff_t>(counts.size()) - 1))];
  }
  for (std::size_t k = 0; k < counts.size(); ++k) {
    r.sampled.push_back(samples > 0 ? static_cast<double>(counts[k]) / samples : 0.0);
    r.tvd_sampled += 0.5 * std::abs(r.sampled[k] - r.exact[k]);
    r.tvd_distinguishable += 0.5 * std::abs(r.distinguishable[k] - r.exact[k]);
  }
  return r;
}

CircuitTransfer circuit_transfer(const circuit::Circuit& c, const circuit::ComponentRegistry& registry,
                                 double wavelength) {
  CircuitTransfer t;
  for (const auto& inst : c.instances) {
    if (inst.type == "laser") t.inputs.push_back(inst.name);
    if (inst.type == "detector" || inst.type == "photodiode") t.outputs.push_back(inst.name);
  }
  if (t.inputs.empty() || t.outputs.empty())
    throw circuit::CircuitError("Die Schaltung braucht Laser (Eingänge) und Detektoren (Ausgänge)");
  t.u = Matrix::Zero(static_cast<Eigen::Index>(t.outputs.size()), static_cast<Eigen::Index>(t.inputs.size()));
  for (std::size_t j = 0; j < t.inputs.size(); ++j) {
    circuit::Circuit probe = c;
    for (const auto& name : t.inputs) {
      circuit::Instance* laser = probe.find_instance(name);
      laser->params["power"] = name == t.inputs[j] ? 1.0 : 0.0;
      laser->params["phase"] = 0.0;
    }
    const circuit::CompiledCircuit compiled(probe, registry);
    const circuit::Solution sol = compiled.solve(wavelength);
    for (std::size_t i = 0; i < t.outputs.size(); ++i)
      t.u(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) =
          sol.a(compiled.port_index(t.outputs[i], "in"));
  }
  return t;
}

}  // namespace photonlab::quantum
