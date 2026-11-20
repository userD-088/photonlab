#pragma once
// Layout einer Schaltung des Chip-Designers: je Bauteil eine parametrische Zelle (Wellenleiter, Koppler,
// MZI-Schalter mit Heizern, Ringe, Gitterkoppler, Photodiode ...), platziert nach dem Raster des Editors,
// verbundene Ports durch Wellenleiter mit Bögen. Ein Entwurf für die Fertigung: Prozessregeln, Verdrahtung
// der Heizer zu Bondflächen und eine Prüfung der Abstände (DRC) fehlen. Lagen und Annahmen:
// docs/physics/layout.md
#include <string>
#include <vector>

#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/circuit/subcircuit.hpp"
#include "photonlab/layout/layout.hpp"

namespace photonlab::layout {

// Lagen eines einfachen Silizium-Prozesses (Nummern frei gewählt, in docs/physics/layout.md erklärt)
namespace layers {
inline constexpr int waveguide = 1;      // Si 220 nm, voll geätzt
inline constexpr int grating = 2;        // flache Ätzung der Gitterkoppler
inline constexpr int heater = 11;        // TiN-Heizer
inline constexpr int metal = 12;         // Kontakte und Leitungen
inline constexpr int germanium = 20;     // Ge der Photodioden
inline constexpr int doping_p = 21;      // p-Dotierung (PN-Modulatoren)
inline constexpr int doping_n = 22;      // n-Dotierung
inline constexpr int phase_change = 23;  // GST
inline constexpr int gain = 24;          // III-V-Verstärker (hybrid)
inline constexpr int outline = 99;       // Umriss von Bauteilen ohne eigene Geometrie
}  // namespace layers

struct CircuitLayoutSettings {
  double pitch = 30e-6;             // Rasterzelle des Editors auf dem Chip [m]
  double waveguide_width = 0.5e-6;  // [m]
  double bend_radius = 5e-6;        // [m]
  double coupler_gap = 0.2e-6;      // Spalt der Richtkoppler und Ringe [m]
  // Kopplungskoeffizient zweier 500-nm-Streifen mit 200 nm Spalt (Annahme, Größenordnung für Si 220 nm):
  // gekoppelte Leistung sin²(kappa L); im Bauteil-Labor nachmessen
  double coupling_per_length = 0.12e6;  // [1/m]
  double heater_width = 2e-6;           // [m]
  double contact_size = 20e-6;          // Kontaktflächen der Heizer und Dioden [m]
};

struct CircuitLayoutReport {
  int crossings = 0;                 // Kreuzungen zwischen geführten Wellenleitern
  int instances = 0;                 // gezeichnete Bauteile
  double width = 0.0, height = 0.0;  // Ausdehnung des Chips [m]
  std::vector<std::string> notes;    // Hinweise (Laser außerhalb, Bauteile als Kasten ...)
};

// Länge eines Richtkopplers für die gekoppelte Leistung kappa2: L = asin(sqrt(kappa2)) / kappa [m]
double coupler_length(double kappa2, const CircuitLayoutSettings& settings);

// Layout der Schaltung als Zelle "CHIP"; Unterschaltungen (subcircuits) erscheinen als Kasten und bekommen
// eine eigene Zelle mit ihrem Inneren. Wirft LayoutError bei unbekanntem Bauteiltyp in der Registry.
Layout circuit_to_layout(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                         const std::vector<circuit::Subcircuit>& subcircuits = {},
                         const CircuitLayoutSettings& settings = {}, CircuitLayoutReport* report = nullptr);

}  // namespace photonlab::layout
