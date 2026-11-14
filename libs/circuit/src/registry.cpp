#include "photonlab/circuit/registry.hpp"

#include <cmath>
#include <string>

namespace photonlab::circuit {

CVector Component::source(double /*wavelength*/) const { return CVector::Zero(port_count()); }

std::vector<Measurement> Component::measure(const CVector& /*a*/, const CVector& /*b*/) const { return {}; }

CMatrix Component::s_matrix_at(double wavelength, const std::vector<double>& /*state*/) const {
  return s_matrix(wavelength);
}

std::vector<double> Component::settle_state(double /*wavelength*/, const CVector& /*a*/,
                                            const std::vector<double>& previous) const {
  return previous;
}

std::vector<Measurement> Component::measure_at(const CVector& a, const CVector& b,
                                               const std::vector<double>& /*state*/) const {
  return measure(a, b);
}

int ComponentType::port_index(std::string_view port) const {
  for (std::size_t i = 0; i < port_names.size(); ++i)
    if (port_names[i] == port) return static_cast<int>(i);
  return -1;
}

bool ComponentType::is_left_port(int port) const {
  if (left_port_count >= 0) return port < left_port_count;
  return port_names[static_cast<std::size_t>(port)].rfind("in", 0) == 0;
}

const ParamSpec* ComponentType::find_param(std::string_view param) const {
  for (const auto& p : params)
    if (p.name == param) return &p;
  return nullptr;
}

ParamMap ComponentType::default_params() const {
  ParamMap result;
  for (const auto& p : params) result[p.name] = p.default_value;
  return result;
}

const ComponentRegistry& ComponentRegistry::builtin() {
  static const ComponentRegistry registry = [] {
    ComponentRegistry r;
    register_builtin_components(r);
    return r;
  }();
  return registry;
}

void ComponentRegistry::add(ComponentType type) {
  if (type.name.empty()) throw CircuitError("Bauteiltyp ohne Namen kann nicht angemeldet werden");
  if (find(type.name)) throw CircuitError("Bauteiltyp '" + type.name + "' ist bereits angemeldet");
  if (!type.create) throw CircuitError("Bauteiltyp '" + type.name + "' hat keine Modellfunktion");
  types_.push_back(std::move(type));
}

const ComponentType* ComponentRegistry::find(std::string_view name) const {
  for (const auto& t : types_)
    if (t.name == name) return &t;
  return nullptr;
}

std::unique_ptr<Component> ComponentRegistry::create(std::string_view type_name,
                                                     const ParamMap& params) const {
  const ComponentType* type = find(type_name);
  if (!type) throw CircuitError("Unbekannter Bauteiltyp '" + std::string(type_name) + "'");

  ParamMap full = type->default_params();
  for (const auto& [key, value] : params) {
    const ParamSpec* spec = type->find_param(key);
    if (!spec) throw CircuitError("Bauteiltyp '" + type->name + "' kennt den Parameter '" + key + "' nicht");
    if (!std::isfinite(value) || value < spec->min_value || value > spec->max_value)
      throw CircuitError("Parameter '" + key + "' von '" + type->name + "' liegt außerhalb von [" +
                         std::to_string(spec->min_value) + ", " + std::to_string(spec->max_value) +
                         "] (SI): " + std::to_string(value));
    full[key] = value;
  }

  auto component = type->create(full);
  if (!component || component->port_count() != static_cast<int>(type->port_names.size()))
    throw CircuitError("Bauteiltyp '" + type->name + "' liefert eine falsche Portanzahl");
  return component;
}

}  // namespace photonlab::circuit
