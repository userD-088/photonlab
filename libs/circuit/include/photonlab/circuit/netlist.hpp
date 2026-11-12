#pragma once
// Netzliste: reine Beschreibung einer Schaltung (Bauteil-Instanzen und Verbindungen).
// Enthält keine Physik und keine Grafik; die Rasterposition ist nur ein Datum für den Editor.
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/component.hpp"

namespace photonlab::circuit {

struct Instance {
  std::string name;  // eindeutig in der Schaltung, z. B. "koppler1"
  std::string type;  // Schlüssel in der Component-Registry
  ParamMap params;   // SI
  int grid_x = 0;    // Position im Raster-Editor (Zellen)
  int grid_y = 0;
};

struct PortRef {
  std::string instance;
  std::string port;
  bool operator==(const PortRef&) const = default;
};

struct Connection {
  PortRef a;
  PortRef b;
};

struct Circuit {
  std::string name;
  std::vector<Instance> instances;
  std::vector<Connection> connections;

  Instance* find_instance(std::string_view instance_name);
  const Instance* find_instance(std::string_view instance_name) const;

  // Fügt eine Instanz hinzu; wirft CircuitError bei doppeltem Namen.
  Instance& add(std::string instance_name, std::string type, ParamMap params = {}, int grid_x = 0,
                int grid_y = 0);
  void connect(std::string_view from, std::string_view to);  // Schreibweise "instanz.port"

  // Entfernt die Instanz samt aller ihrer Verbindungen.
  void remove_instance(std::string_view instance_name);
  // Entfernt alle Verbindungen, an denen dieser Port beteiligt ist.
  void disconnect(const PortRef& port);
  bool is_connected(const PortRef& port) const;

  // Freier Instanzname der Form "<prefix><n>".
  std::string unique_name(std::string_view prefix) const;
};

// Zerlegt "instanz.port"; wirft CircuitError bei falscher Schreibweise.
PortRef parse_port_ref(std::string_view text);
std::string to_string(const PortRef& port);

}  // namespace photonlab::circuit
