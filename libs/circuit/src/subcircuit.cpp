#include "photonlab/circuit/subcircuit.hpp"

#include <algorithm>
#include <map>

#include "photonlab/circuit/solver.hpp"

namespace photonlab::circuit {
namespace {

// Bauteil aus einer inneren Schaltung: S-Matrix durch Einspeisen an je einem äußeren Port (innere Quellen
// aus), Quellen und Messwerte aus der inneren Lösung.
class SubcircuitComponent final : public Component {
 public:
  SubcircuitComponent(std::shared_ptr<const Subcircuit> sub,
                      const std::shared_ptr<const ComponentRegistry>& registry, const ParamMap& params)
      : sub_(std::move(sub)) {
    Circuit circuit = sub_->circuit;
    for (const auto& param : sub_->params)
      for (const auto& name : param.instances)
        if (Instance* instance = circuit.find_instance(name))
          instance->params[param.param] = params.at(param.name);
    compiled_ = std::make_shared<const CompiledCircuit>(circuit, *registry);
    for (const auto& port : sub_->ports) indices_.push_back(compiled_->port_index(port.inner));
  }

  int port_count() const override { return static_cast<int>(indices_.size()); }

  CMatrix s_matrix(double wavelength) const override {
    const auto n = static_cast<Eigen::Index>(indices_.size());
    CMatrix s(n, n);
    CVector a = CVector::Zero(compiled_->port_count());
    for (Eigen::Index j = 0; j < n; ++j) {
      a.setZero();
      a(indices_[static_cast<std::size_t>(j)]) = 1.0;
      const Solution solution = compiled_->solve(wavelength, &a, 0.0);
      for (Eigen::Index i = 0; i < n; ++i) s(i, j) = solution.b(indices_[static_cast<std::size_t>(i)]);
    }
    return s;
  }

  CVector source(double wavelength) const override {
    const Solution solution = compiled_->solve(wavelength, nullptr, 1.0);
    CVector out(static_cast<Eigen::Index>(indices_.size()));
    for (std::size_t i = 0; i < indices_.size(); ++i)
      out(static_cast<Eigen::Index>(i)) = solution.b(indices_[i]);
    return out;
  }

  // Messwerte der inneren Bauteile als "<instanz>: <messgröße>", dazu die Heizleistung der ganzen Zelle
  std::vector<Measurement> measure_at_wavelength(double wavelength, const CVector& a,
                                                 const CVector& /*b*/) const override {
    CVector external = CVector::Zero(compiled_->port_count());
    for (std::size_t i = 0; i < indices_.size(); ++i) external(indices_[i]) = a(static_cast<Eigen::Index>(i));
    const Solution solution = compiled_->solve(wavelength, &external, 1.0);
    std::vector<Measurement> result;
    double heating = 0.0;
    bool heated = false;
    for (const auto& m : compiled_->measurements(solution)) {
      if (m.measurement.name == "Heizleistung") heating += m.measurement.value, heated = true;
      result.push_back({m.instance + ": " + m.measurement.name, m.measurement.value, m.measurement.unit});
    }
    if (heated) result.insert(result.begin(), {"Heizleistung", heating, "W"});
    return result;
  }

 private:
  std::shared_ptr<const Subcircuit> sub_;
  std::shared_ptr<const CompiledCircuit> compiled_;
  std::vector<int> indices_;
};

}  // namespace

std::string subcircuit_type_name(const std::string& name) { return "sub:" + name; }

Subcircuit make_subcircuit(const Circuit& circuit, const std::string& name,
                           std::vector<SubcircuitParam> params, const ComponentRegistry& registry) {
  const CompiledCircuit check(circuit, registry);  // wirft bei ungültiger Schaltung
  (void)check;
  Subcircuit sub;
  sub.name = name;
  sub.label = name;
  sub.circuit = circuit;
  sub.params = std::move(params);
  // offene Ports: links liegende zuerst, jeweils von oben nach unten (so liegen sie auch am Kasten)
  struct Candidate {
    bool left;
    int y;
    SubcircuitPort port;
  };
  std::vector<Candidate> candidates;
  for (const auto& instance : circuit.instances) {
    const ComponentType* type = registry.find(instance.type);
    for (int k = 0; k < static_cast<int>(type->port_names.size()); ++k) {
      const PortRef ref{instance.name, type->port_names[static_cast<std::size_t>(k)]};
      if (circuit.is_connected(ref)) continue;
      candidates.push_back(
          {type->is_left_port(k), instance.grid_y + 2 * k, {instance.name + "_" + ref.port, ref}});
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.left != b.left ? a.left : a.y < b.y;
  });
  for (auto& c : candidates) sub.ports.push_back(std::move(c.port));
  return sub;
}

void register_subcircuit(ComponentRegistry& registry, std::shared_ptr<const Subcircuit> sub) {
  if (!sub) throw CircuitError("Leere Unterschaltung");
  const std::string who = "Unterschaltung '" + sub->name + "': ";
  if (sub->name.empty()) throw CircuitError("Unterschaltung ohne Namen");
  if (sub->ports.empty()) throw CircuitError(who + "kein Port führt nach außen");
  // Kopie der Registry, wie sie jetzt ist: die Unterschaltung kann sich so nicht selbst enthalten
  auto inner = std::make_shared<const ComponentRegistry>(registry);
  std::shared_ptr<const CompiledCircuit> compiled;
  try {
    compiled = std::make_shared<const CompiledCircuit>(sub->circuit, *inner);
  } catch (const CircuitError& e) {
    throw CircuitError(who + e.what());
  }
  if (compiled->nonlinear())
    throw CircuitError(who + "nichtlineare Bauteile im Inneren werden nicht unterstützt");

  std::vector<std::string> port_names;
  int left = 0;
  bool right_seen = false;
  for (const auto& port : sub->ports) {
    int index = -1;
    try {
      index = compiled->port_index(port.inner);
    } catch (const CircuitError& e) {
      throw CircuitError(who + "Port '" + port.name + "': " + e.what());
    }
    if (compiled->partner(index) >= 0)
      throw CircuitError(who + "Port '" + port.name + "' ist im Inneren schon verbunden");
    if (std::count(port_names.begin(), port_names.end(), port.name) > 0)
      throw CircuitError(who + "Port '" + port.name + "' gibt es doppelt");
    port_names.push_back(port.name);
    const Instance* instance = sub->circuit.find_instance(port.inner.instance);
    const ComponentType* type = inner->find(instance->type);
    const auto k = std::find(type->port_names.begin(), type->port_names.end(), port.inner.port) -
                   type->port_names.begin();
    if (type->is_left_port(static_cast<int>(k)) && !right_seen)
      ++left;
    else
      right_seen = true;
  }

  // Parameter: Bereich und Anzeige vom ersten inneren Ziel
  std::vector<ParamSpec> specs;
  for (const auto& param : sub->params) {
    if (param.instances.empty()) throw CircuitError(who + "Parameter '" + param.name + "' setzt nichts");
    for (const auto& name : param.instances) {
      const Instance* instance = sub->circuit.find_instance(name);
      if (!instance)
        throw CircuitError(who + "Parameter '" + param.name + "': Instanz '" + name + "' gibt es nicht");
      if (!inner->find(instance->type)->find_param(param.param))
        throw CircuitError(who + "Parameter '" + param.name + "': '" + name + "' hat keinen Parameter '" +
                           param.param + "'");
    }
    const ParamSpec* spec =
        inner->find(sub->circuit.find_instance(param.instances.front())->type)->find_param(param.param);
    ParamSpec copy = *spec;
    copy.name = param.name;
    copy.label = param.name + " (" + spec->label + ")";
    copy.default_value = std::clamp(param.default_value, spec->min_value, spec->max_value);
    specs.push_back(copy);
  }
  ComponentType type{subcircuit_type_name(sub->name),
                     sub->label.empty() ? sub->name : sub->label,
                     (sub->description.empty() ? std::string("Unterschaltung") : sub->description) + " (" +
                         std::to_string(sub->circuit.instances.size()) +
                         " Bauteile im Inneren; Messwerte als '<bauteil>: <messgröße>').",
                     port_names,
                     specs,
                     [sub, inner](const ParamMap& params) {
                       return std::make_unique<SubcircuitComponent>(sub, inner, params);
                     }};
  type.left_port_count = left;
  registry.add(std::move(type));
}

}  // namespace photonlab::circuit
