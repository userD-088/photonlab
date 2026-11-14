#pragma once
// Referenz-Experimente des Quanten-Modus (Bauplan 9.2): Hong-Ou-Mandel, linear-optisches CNOT
// (nachselektiert), Bell-Zustand, Boson Sampling; Übertragungsmatrix einer Schaltung des Chip-Designers.
// docs/physics/quantum.md
#include <array>
#include <string>
#include <vector>

#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/quantum/fock.hpp"

namespace photonlab::quantum {

// Hong-Ou-Mandel: zwei Photonen auf einen Strahlteiler (Reflektivität R). Koinzidenz = je ein Klick an beiden
// Ausgängen. Quellen mit Mehrphotonen-Anteil p2 (Wahrscheinlichkeit, dass eine Quelle zwei statt eines
// Photons liefert, z. B. angekündigte Quellen aus spontaner Vierwellenmischung).
struct HomSettings {
  double reflectivity = 0.5;
  double indistinguishability = 1.0;  // Überlapp der Wellenpakete bei Verzögerung 0
  double multiphoton = 0.0;           // p2 je Quelle
  Detectors detectors;                // Standard: ideal, Schwellwert
  double coherence_time = 1e-12;      // Gaußsche Wellenpakete: Überlapp exp(-(tau/tau_c)^2)
};
struct HomResult {
  double coincidence = 0.0;          // bei Verzögerung 0
  double coincidence_far = 0.0;      // bei großer Verzögerung (unterscheidbar)
  double visibility = 0.0;           // 1 - P_c(0) / P_c(fern)
  std::vector<double> delay, curve;  // Koinzidenz über der Verzögerung
};
HomResult hong_ou_mandel(const HomSettings& settings, int points = 101);

// Linear-optisches CNOT im Koinzidenz-Betrieb (Ralph u. a., PRA 65, 062324 (2002); O'Brien u. a., Nature 426,
// 264 (2003)): Dual-Rail-Qubits, drei Strahlteiler mit R = 1/3, nachselektiert auf je ein Photon im Kontroll-
// und Zielpaar. Moden: c0, c1, t0, t1, zwei leere Hilfsmoden.
struct CnotResult {
  Matrix unitary;                   // 6 x 6
  std::array<double, 4> success{};  // Erfolgswahrscheinlichkeit je logischem Eingang |c t>
  std::array<std::array<double, 4>, 4>
      truth{};                 // P(Ausgang | Eingang, Erfolg), Zeile = Eingang 00, 01, 10, 11
  double fidelity = 0.0;       // mittlere Wahrscheinlichkeit des richtigen Ausgangs (Wahrheitstabelle)
  double bell_fidelity = 0.0;  // Kontrolle in (|0> + |1>)/sqrt2: Überlapp mit Phi+ (nur für V = 1)
};
CnotResult linear_optical_cnot(double reflectivity = 1.0 / 3.0, double indistinguishability = 1.0);
Matrix cnot_unitary(double reflectivity = 1.0 / 3.0);

// Boson Sampling: n Photonen in den ersten n Moden eines Haar-zufälligen Interferometers mit m Moden; exakte
// Verteilung (Permanenten), Verteilung unterscheidbarer Photonen, Stichprobe; Abstände (totale Variation).
struct BosonSamplingResult {
  std::vector<Pattern> outcomes;
  std::vector<double> exact, distinguishable, sampled;
  double tvd_sampled = 0.0;          // Abstand Stichprobe - exakt
  double tvd_distinguishable = 0.0;  // Abstand unterscheidbar - exakt
  int samples = 0;
};
BosonSamplingResult boson_sampling(int modes, int photons, std::uint64_t seed, int samples);

// Übertragungsmatrix einer Schaltung: Eingänge = Laser, Ausgänge = Detektoren (Typen detector, photodiode);
// U(i, j) = Feld am Detektor i, wenn nur Laser j mit 1 W und Phase 0 sendet.
struct CircuitTransfer {
  std::vector<std::string> inputs, outputs;
  Matrix u;
};
CircuitTransfer circuit_transfer(const circuit::Circuit& circuit, const circuit::ComponentRegistry& registry,
                                 double wavelength);

}  // namespace photonlab::quantum
