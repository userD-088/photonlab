#pragma once
// Bauteil aus einer gemessenen S-Matrix (z. B. aus dem 2D-FDTD extrahiert, Brücke 1): die Matrix liegt an
// Stützwellenlängen vor und wird dazwischen interpoliert (Betrag und Phase getrennt, linear).
#include <complex>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/circuit/registry.hpp"

namespace photonlab::circuit {

struct SParameterTable {
  std::string name;                 // Schlüssel; der Bauteiltyp heißt "sparam:<name>"
  std::string label;                // Anzeigename
  std::string description;          // Herkunft, z. B. Szene, Gitter, Polarisation
  std::vector<std::string> ports;   // Reihenfolge der Matrix
  int left_port_count = -1;         // Schaltplan: so viele der ersten Ports links (-1 = Namenskonvention)
  std::vector<double> wavelengths;  // [m], streng steigend
  std::vector<CMatrix> s;           // je Wellenlänge eine Matrix ports x ports
  std::string source_hash;          // Fingerabdruck der Eingabe (Szene + Einstellungen), für den Cache
  bool operator==(const SParameterTable& other) const;
};

// Typname in der Registry.
std::string sparameter_type_name(const std::string& table_name);

// Prüft Größen und Wellenlängen; wirft CircuitError mit Klartext.
void validate(const SParameterTable& table);

// Interpolierte Matrix; außerhalb des Wellenlängenbereichs der Tabelle CircuitError (keine Extrapolation).
CMatrix interpolate(const SParameterTable& table, double wavelength);

// Meldet die Tabelle als Bauteiltyp an (ohne Parameter). Wirft CircuitError bei ungültiger Tabelle oder
// doppeltem Namen.
void register_sparameter_table(ComponentRegistry& registry, std::shared_ptr<const SParameterTable> table);

}  // namespace photonlab::circuit
