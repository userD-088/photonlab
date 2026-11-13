#pragma once
// Projektdatei .plab.json: Schaltung plus Simulationseinstellungen.
// Gleiche Datei, gleiches Ergebnis – alle Zahlen stehen in SI in der Datei.
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "photonlab/circuit/monte_carlo.hpp"
#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/sweep.hpp"
#include "photonlab/logic/gate.hpp"
#include "photonlab/logic/report.hpp"

namespace photonlab::io {

struct IoError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Project {
  circuit::Circuit circuit;
  double wavelength = 1550e-9;  // Arbeitswellenlänge für Einzelrechnungen [m]
  circuit::SweepSettings sweep;
  circuit::MonteCarloSettings monte_carlo;
  logic::GateDefinition gate;  // Gatter-Definition des Logik-Labors; leer, wenn die Schaltung kein Gatter ist
};

// Projekt aus einem Referenz-Design (photonlab/circuit/reference_designs.hpp); wirft CircuitError.
Project reference_project(std::string_view name);

inline constexpr int project_format_version = 1;

// Text-Varianten (für Tests und Zwischenablage)
std::string project_to_json(const Project& project);
Project project_from_json(std::string_view text);  // wirft IoError mit Klartext-Meldung

// Charakterisiertes Gatter als .gate.json: Definition, gemessene Wahrheitstabelle und Zeugnis.
// Grundlage für die Logik-Ebene (Brücke 2, Bauplan 8.1).
std::string gate_report_to_json(const logic::GateDefinition& gate, const logic::GateReport& report);
void save_gate_report(const logic::GateDefinition& gate, const logic::GateReport& report,
                      const std::filesystem::path& path);

// Datei-Varianten; wirft IoError
void save_project(const Project& project, const std::filesystem::path& path);
Project load_project(const std::filesystem::path& path);

}  // namespace photonlab::io
