#pragma once
// Referenz-Designs der Schaltungs-Ebene (Bauplan Abschnitt 11). Jedes ist Demo und Validierungsfall zugleich;
// die Dateien in examples/ werden aus diesen Funktionen erzeugt (photonlab_cli example <name>).
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/netlist.hpp"

namespace photonlab::circuit {

// Design 2: balanciertes Mach-Zehnder-Interferometer mit Phasenschieber im oberen Arm.
// Detektor "det_bar" misst sin²(φ/2), Detektor "det_cross" misst cos²(φ/2) der Laserleistung.
Circuit reference_mzi();

// Design 2b: unbalanciertes MZI mit zwei Wellenleiter-Armen (Längendifferenz 100 µm),
// zeigt das Spektrum mit FSR = λ²/(n_g·ΔL).
Circuit reference_mzi_unbalanced();

std::vector<std::string> reference_design_names();
Circuit reference_design(std::string_view name);  // wirft CircuitError bei unbekanntem Namen

}  // namespace photonlab::circuit
