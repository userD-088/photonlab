#include "photonlab/circuit/solver.hpp"

#include <cmath>

namespace photonlab::circuit {

CompiledCircuit::CompiledCircuit(const Circuit& circuit, const ComponentRegistry& registry) {
  // Bauteile erzeugen und Ports global durchnummerieren
  int next_port = 0;
  for (const auto& inst : circuit.instances) {
    for (const auto& slot : slots_)
      if (slot.name == inst.name) throw CircuitError("Instanzname '" + inst.name + "' ist doppelt vergeben");
    const ComponentType* type = registry.find(inst.type);
    if (!type) throw CircuitError("Instanz '" + inst.name + "': unbekannter Bauteiltyp '" + inst.type + "'");
    try {
      components_.push_back(registry.create(inst.type, inst.params));
    } catch (const CircuitError& e) {
      throw CircuitError("Instanz '" + inst.name + "': " + e.what());
    }
    const int count = static_cast<int>(type->port_names.size());
    slots_.push_back({inst.name, next_port, count});
    for (const auto& port : type->port_names) port_labels_.push_back(inst.name + "." + port);
    next_port += count;
  }

  // Verbindungen: jeder Port höchstens einmal, keine Verbindung eines Ports mit sich selbst
  partner_.assign(port_labels_.size(), -1);
  auto lookup = [&](const PortRef& ref) {
    for (std::size_t i = 0; i < port_labels_.size(); ++i)
      if (port_labels_[i] == to_string(ref)) return static_cast<int>(i);
    throw CircuitError("Verbindung nennt den unbekannten Port '" + to_string(ref) + "'");
  };
  for (const auto& connection : circuit.connections) {
    const int p = lookup(connection.a);
    const int q = lookup(connection.b);
    if (p == q) throw CircuitError("Port '" + to_string(connection.a) + "' ist mit sich selbst verbunden");
    for (int port : {p, q})
      if (partner_[static_cast<std::size_t>(port)] != -1)
        throw CircuitError("Port '" + port_labels_[static_cast<std::size_t>(port)] +
                           "' ist mehrfach verbunden (Verzweigungen brauchen einen Koppler)");
    partner_[static_cast<std::size_t>(p)] = q;
    partner_[static_cast<std::size_t>(q)] = p;
  }
}

int CompiledCircuit::port_index(std::string_view instance, std::string_view port) const {
  const std::string label = std::string(instance) + "." + std::string(port);
  for (std::size_t i = 0; i < port_labels_.size(); ++i)
    if (port_labels_[i] == label) return static_cast<int>(i);
  throw CircuitError("Unbekannter Port '" + label + "'");
}

Solution CompiledCircuit::solve(double wavelength, const CVector* a_ext, double source_scale) const {
  if (!(wavelength > 0.0) || !std::isfinite(wavelength))
    throw CircuitError("Wellenlänge muss positiv und endlich sein");
  const int n = port_count();
  if (a_ext && a_ext->size() != n) throw CircuitError("a_ext hat nicht die Länge der Portanzahl");

  Solution solution;
  solution.wavelength = wavelength;
  solution.a = CVector::Zero(n);
  solution.b = CVector::Zero(n);
  if (n == 0) return solution;

  // Blockdiagonale Bauteil-Matrix S und Quellvektor b_src
  CMatrix s = CMatrix::Zero(n, n);
  CVector rhs = CVector::Zero(n);
  for (std::size_t k = 0; k < components_.size(); ++k) {
    const Slot& slot = slots_[k];
    const CMatrix block = components_[k]->s_matrix(wavelength);
    if (block.rows() != slot.port_count || block.cols() != slot.port_count)
      throw CircuitError("Bauteil '" + slot.name + "' liefert eine S-Matrix falscher Größe");
    s.block(slot.first_port, slot.first_port, slot.port_count, slot.port_count) = block;
    rhs.segment(slot.first_port, slot.port_count) = source_scale * components_[k]->source(wavelength);
  }
  if (a_ext) rhs += s * (*a_ext);

  // M = I − S·C. C ist eine Permutation (a_i = b_partner(i)), daher spaltenweise: (S·C)(:, partner(j)) = S(:,
  // j)
  CMatrix m = CMatrix::Identity(n, n);
  for (int j = 0; j < n; ++j) {
    const int q = partner(j);
    if (q >= 0) m.col(q) -= s.col(j);
  }

  solution.b = m.partialPivLu().solve(rhs);

  const double rhs_norm = rhs.norm();
  solution.residual = (m * solution.b - rhs).norm() / (rhs_norm > 0.0 ? rhs_norm : 1.0);
  if (!solution.b.allFinite() || !(solution.residual < 1e-9))
    throw CircuitError(
        "Schaltung ist bei dieser Wellenlänge singulär (verlustfreie Schleife exakt in Resonanz?)");

  // Einlaufende Wellen: a = C·b + a_ext
  for (int j = 0; j < n; ++j) {
    const int q = partner(j);
    if (q >= 0) solution.a(j) = solution.b(q);
  }
  if (a_ext) solution.a += *a_ext;
  return solution;
}

std::vector<InstanceMeasurement> CompiledCircuit::measurements(const Solution& solution) const {
  std::vector<InstanceMeasurement> result;
  for (std::size_t k = 0; k < components_.size(); ++k) {
    const Slot& slot = slots_[k];
    const CVector a = solution.a.segment(slot.first_port, slot.port_count);
    const CVector b = solution.b.segment(slot.first_port, slot.port_count);
    for (auto& m : components_[k]->measure(a, b)) result.push_back({slot.name, std::move(m)});
  }
  return result;
}

PowerBalance CompiledCircuit::power_balance(const Solution& solution, const CVector* a_ext) const {
  PowerBalance balance;
  for (const Slot& slot : slots_) {
    // Netto aufgenommene Leistung des Bauteils: Summe |a|² − Summe |b|²
    const double net = solution.a.segment(slot.first_port, slot.port_count).squaredNorm() -
                       solution.b.segment(slot.first_port, slot.port_count).squaredNorm();
    if (net >= 0.0)
      balance.absorbed += net;
    else
      balance.emitted -= net;
  }
  for (int j = 0; j < port_count(); ++j) {
    if (partner(j) >= 0) continue;
    balance.leaving_open += std::norm(solution.b(j));
    if (a_ext) balance.external_in += std::norm((*a_ext)(j));
  }
  return balance;
}

}  // namespace photonlab::circuit
