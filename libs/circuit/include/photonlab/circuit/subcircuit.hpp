#pragma once
// Unterschaltung: eine Schaltung, die im Chip-Designer als ein Bauteil "sub:<name>" erscheint (Hierarchie,
// z. B. eine Volladdierer-Zelle, die ein 8-Bit-Addierer achtmal benutzt). Ihre S-Matrix rechnet die innere
// Schaltung mit Einspeisung an den nach außen geführten Ports; der Löser behandelt sie wie jedes Bauteil.
// Herleitung und Grenzen: docs/physics/circuit_solver.md
#include <memory>
#include <string>
#include <vector>

#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"

namespace photonlab::circuit {

// Port nach außen: ein offener Port eines inneren Bauteils
struct SubcircuitPort {
  std::string name;
  PortRef inner;
  bool operator==(const SubcircuitPort&) const = default;
};

// Parameter nach außen: setzt param an allen genannten inneren Instanzen (z. B. alle Heizer eines Bits)
struct SubcircuitParam {
  std::string name;
  std::string param;
  std::vector<std::string> instances;
  double default_value = 0.0;
  bool operator==(const SubcircuitParam&) const = default;
};

struct Subcircuit {
  std::string name;  // Schlüssel: Bauteiltyp "sub:<name>"
  std::string label;
  std::string description;
  Circuit circuit;
  std::vector<SubcircuitPort> ports;
  std::vector<SubcircuitParam> params;
  bool operator==(const Subcircuit&) const = default;
};

std::string subcircuit_type_name(const std::string& name);

// Unterschaltung aus einer Schaltung: alle offenen Ports nach außen (Name "<instanz>_<port>"), linke Ports
// zuerst; Parameter wie angegeben. Wirft CircuitError, wenn die Schaltung ungültig ist.
Subcircuit make_subcircuit(const Circuit& circuit, const std::string& name,
                           std::vector<SubcircuitParam> params, const ComponentRegistry& registry);

// Meldet die Unterschaltung als Bauteil an. Alle inneren Typen müssen in registry schon vorhanden sein (die
// Unterschaltung behält eine Kopie davon); nichtlineare Bauteile im Inneren sind nicht erlaubt. Wirft
// CircuitError mit Klartext.
void register_subcircuit(ComponentRegistry& registry, std::shared_ptr<const Subcircuit> sub);

}  // namespace photonlab::circuit
