#pragma once
// Component-Registry: Bauteile melden sich mit Name, Ports, Parametern und Modellfunktion an.
// Der Löser kennt keine konkreten Bauteile, neue Bauteile brauchen keine Änderung am Löser.
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/component.hpp"

namespace photonlab::circuit {

struct CircuitError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct ComponentType {
  std::string name;                     // eindeutiger Schlüssel, z. B. "waveguide"
  std::string label;                    // deutscher Anzeigename
  std::string description;              // Modell und Vereinfachungen in einem Satz
  std::vector<std::string> port_names;  // Reihenfolge = Zeilen/Spalten der S-Matrix
  std::vector<ParamSpec> params;
  std::function<std::unique_ptr<Component>(const ParamMap&)> create;

  int port_index(std::string_view port) const;  // -1, wenn unbekannt
  const ParamSpec* find_param(std::string_view param) const;
  ParamMap default_params() const;
};

class ComponentRegistry {
 public:
  // Leere Registry (für Tests und Plugins).
  ComponentRegistry() = default;

  // Registry mit allen eingebauten Bauteilen.
  static const ComponentRegistry& builtin();

  void add(ComponentType type);  // wirft CircuitError bei doppeltem Namen
  const ComponentType* find(std::string_view name) const;
  const std::vector<ComponentType>& types() const { return types_; }

  // Erzeugt ein Bauteil; fehlende Parameter erhalten Standardwerte, unbekannte oder
  // außerhalb des Wertebereichs liegende Parameter führen zu CircuitError.
  std::unique_ptr<Component> create(std::string_view type_name, const ParamMap& params) const;

 private:
  std::vector<ComponentType> types_;
};

// Meldet Wellenleiter, Richtkoppler, Phasenschieber, Laser und Detektor an.
void register_builtin_components(ComponentRegistry& registry);

}  // namespace photonlab::circuit
