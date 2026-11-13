#pragma once
// Linearer Frequenzlöser der Schaltungs-Ebene: (I − S·C)·b = S·a_ext + b_src.
// Herleitung und Annahmen: docs/physics/circuit_solver.md
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"

namespace photonlab::circuit {

// Lösung für eine Wellenlänge: Wellen an allen Ports in globaler Portnummerierung.
struct Solution {
  double wavelength = 0.0;  // [m]
  CVector a;                // einlaufende Wellen [sqrt(W)]
  CVector b;                // auslaufende Wellen [sqrt(W)]
  double residual = 0.0;    // relatives Residuum des linearen Systems
};

struct InstanceMeasurement {
  std::string instance;
  Measurement measurement;
};

// Leistungsbilanz einer Lösung, alle Werte in Watt.
struct PowerBalance {
  double emitted = 0.0;       // von Quellen-Bauteilen netto abgegeben
  double absorbed = 0.0;      // in Bauteilen absorbiert (Detektoren, Verluste)
  double external_in = 0.0;   // über a_ext an offenen Ports eingespeist
  double leaving_open = 0.0;  // verlässt die Schaltung über offene Ports
};

// Geprüfte, rechenbereite Schaltung. Unveränderlich und damit aus mehreren Threads nutzbar.
class CompiledCircuit {
 public:
  // Prüft Netzliste gegen die Registry und baut Bauteile sowie Verbindungsmatrix.
  // Wirft CircuitError mit deutscher Klartext-Meldung.
  CompiledCircuit(const Circuit& circuit, const ComponentRegistry& registry);

  int port_count() const { return static_cast<int>(port_labels_.size()); }
  int instance_count() const { return static_cast<int>(components_.size()); }

  // Globaler Portindex; wirft CircuitError, wenn Instanz oder Port fehlen.
  int port_index(std::string_view instance, std::string_view port) const;
  int port_index(const PortRef& port) const { return port_index(port.instance, port.port); }
  const std::string& port_label(int port) const { return port_labels_[static_cast<std::size_t>(port)]; }
  // Index des verbundenen Ports oder -1 für einen offenen Port.
  int partner(int port) const { return partner_[static_cast<std::size_t>(port)]; }

  // Löst die Schaltung bei einer Wellenlänge. a_ext (optional, Länge port_count) speist offene Ports.
  // Wirft CircuitError, wenn das System singulär ist (verlustfreie Schleife exakt in Resonanz).
  // source_scale skaliert die Amplituden aller Quellen-Bauteile (1 = Nennwert); damit lässt sich messen,
  // wie Ausgangspegel auf weniger Eingangslicht reagieren, ohne einzelne Bauteile zu kennen.
  Solution solve(double wavelength, const CVector* a_ext = nullptr, double source_scale = 1.0) const;

  std::vector<InstanceMeasurement> measurements(const Solution& solution) const;
  PowerBalance power_balance(const Solution& solution, const CVector* a_ext = nullptr) const;

 private:
  struct Slot {
    std::string name;
    int first_port = 0;
    int port_count = 0;
  };
  std::vector<std::shared_ptr<const Component>> components_;
  std::vector<Slot> slots_;
  std::vector<std::string> port_labels_;
  std::vector<int> partner_;
};

}  // namespace photonlab::circuit
