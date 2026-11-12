#include "photonlab/circuit/netlist.hpp"

#include <algorithm>

#include "photonlab/circuit/registry.hpp"

namespace photonlab::circuit {

Instance* Circuit::find_instance(std::string_view instance_name) {
  for (auto& inst : instances)
    if (inst.name == instance_name) return &inst;
  return nullptr;
}

const Instance* Circuit::find_instance(std::string_view instance_name) const {
  for (const auto& inst : instances)
    if (inst.name == instance_name) return &inst;
  return nullptr;
}

Instance& Circuit::add(std::string instance_name, std::string type, ParamMap params, int grid_x,
                       int grid_y) {
  if (instance_name.empty() || instance_name.find('.') != std::string::npos)
    throw CircuitError("Ungültiger Instanzname '" + instance_name + "' (leer oder mit Punkt)");
  if (find_instance(instance_name))
    throw CircuitError("Instanzname '" + instance_name + "' ist doppelt vergeben");
  instances.push_back({std::move(instance_name), std::move(type), std::move(params), grid_x, grid_y});
  return instances.back();
}

void Circuit::connect(std::string_view from, std::string_view to) {
  connections.push_back({parse_port_ref(from), parse_port_ref(to)});
}

void Circuit::remove_instance(std::string_view instance_name) {
  std::erase_if(connections, [&](const Connection& c) {
    return c.a.instance == instance_name || c.b.instance == instance_name;
  });
  std::erase_if(instances, [&](const Instance& inst) { return inst.name == instance_name; });
}

void Circuit::disconnect(const PortRef& port) {
  std::erase_if(connections, [&](const Connection& c) { return c.a == port || c.b == port; });
}

bool Circuit::is_connected(const PortRef& port) const {
  return std::any_of(connections.begin(), connections.end(),
                     [&](const Connection& c) { return c.a == port || c.b == port; });
}

std::string Circuit::unique_name(std::string_view prefix) const {
  for (int n = 1;; ++n) {
    std::string candidate = std::string(prefix) + std::to_string(n);
    if (!find_instance(candidate)) return candidate;
  }
}

PortRef parse_port_ref(std::string_view text) {
  const auto dot = text.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size())
    throw CircuitError("Portangabe '" + std::string(text) + "' hat nicht die Form 'instanz.port'");
  return {std::string(text.substr(0, dot)), std::string(text.substr(dot + 1))};
}

std::string to_string(const PortRef& port) { return port.instance + "." + port.port; }

}  // namespace photonlab::circuit
