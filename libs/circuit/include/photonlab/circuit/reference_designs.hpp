#pragma once
// Referenz-Designs der Schaltungs-Ebene. Jedes ist Demo und Validierungsfall zugleich;
// die Dateien in examples/ werden aus diesen Funktionen erzeugt (photonlab_cli example <name>).
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/monte_carlo.hpp"
#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/sweep.hpp"

namespace photonlab::circuit {

// Design 2: balanciertes Mach-Zehnder-Interferometer mit Phasenschieber im oberen Arm.
// Detektor "det_bar" misst sin²(φ/2), Detektor "det_cross" misst cos²(φ/2) der Laserleistung.
Circuit reference_mzi();

// Design 2b: unbalanciertes MZI mit zwei Wellenleiter-Armen (Längendifferenz 100 µm),
// zeigt das Spektrum mit FSR = λ²/(n_g·ΔL).
Circuit reference_mzi_unbalanced();

// Design 3: Ringresonator an einem Bus (All-Pass), nahe kritischer Kopplung: FSR, Güte, Extinktion.
Circuit reference_ring_allpass();

// Design 3b: Ringresonator zwischen zwei Bussen (Add-Drop) mit Detektoren an through und drop.
Circuit reference_ring_adddrop();

// Design 5: Interferenz-XOR/XNOR mit Phasencodierung (Detektoren "det_xor", "det_xnor").
Circuit reference_xor_interference();

// Lineares AND mit Intensitätscodierung (Aufbau wie das XOR): Negativbeispiel für Kontrast und Kaskade.
Circuit reference_and_interference();

// Design 10: Directed Logic aus zwei elektrisch abgestimmten Add-Drop-Ringen (XOR und XNOR).
Circuit reference_xor_directed();

// Halbaddierer aus drei MZI-Schaltern mit Heizern (Directed Logic): A und B sind Heizleistungen, Summe und
// Übertrag Lichtleistungen an Photodioden ("det_s", "det_c").
Circuit reference_half_adder();

// Ripple-Carry-Addierer mit 1 bis 16 Bit aus MZI-Schaltern (Directed Logic). Der Übertrag läuft als Licht
// auf zwei Schienen (c hell bei 1, n hell bei 0) von Bit zu Bit; je Bit: Schalter g (A), c und n (A, B), s
// (A, B), Abgriffe 10 % für die Summe. Detektoren "det_s<i>" und "det_cout"; Beschreibung in docs/physics/logic.md.
Circuit reference_ripple_adder(int bits);

// Referenz-Design samt passender Simulationseinstellungen (Sweep-Bereich, Beispiel-Streuungen).
struct ReferenceSetup {
  Circuit circuit;
  double wavelength = 1550e-9;
  SweepSettings sweep;
  MonteCarloSettings monte_carlo;
};
ReferenceSetup reference_setup(std::string_view name);  // wirft CircuitError bei unbekanntem Namen

std::vector<std::string> reference_design_names();
Circuit reference_design(std::string_view name);  // wirft CircuitError bei unbekanntem Namen

}  // namespace photonlab::circuit
