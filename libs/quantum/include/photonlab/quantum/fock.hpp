#pragma once
// Quanten-Modus (Phase 11, Bauplan 9): Photonen in einem linearen Interferometer. Ein Interferometer mit m
// Moden ist eine (sub-)unitäre Matrix U (Spalte = Eingangsmode, Zeile = Ausgangsmode). n Photonen:
// Übergangswahrscheinlichkeiten über Permanenten von Teilmatrizen von U. Gleichungen und Annahmen:
// docs/physics/quantum.md
#include <Eigen/Dense>
#include <complex>
#include <cstdint>
#include <map>
#include <vector>

namespace photonlab::quantum {

using cplx = std::complex<double>;
using Matrix = Eigen::MatrixXcd;
using Pattern = std::vector<int>;  // Besetzung je Mode, z. B. {1, 1, 0}

// Permanente nach Ryser mit Gray-Code (Aufwand ~ 2^n n); leere Matrix: 1.
cplx permanent(const Matrix& a);
// Permanente einer reellen, nichtnegativen Matrix (unterscheidbare Teilchen)
double permanent_real(const Eigen::MatrixXd& a);

// Alle Besetzungen von n Photonen auf m Moden (Anzahl C(m+n-1, n)), in fester Reihenfolge
std::vector<Pattern> patterns(int modes, int photons);

// Amplitude <out| U |in> für ununterscheidbare Photonen (Fock-Zustände): per(U_[out,in]) / sqrt(prod in! prod
// out!)
cplx amplitude(const Matrix& u, const Pattern& in, const Pattern& out);

// Verteilung der Ausgangsbesetzungen. indistinguishability V in [0, 1]: P = V P_ununterscheidbar + (1 - V)
// P_unterscheidbar (für 2 Photonen exakt, für mehr eine Näherung mit gleichem paarweisem Überlapp, siehe
// Doku). Bei Verlusten (U sub-unitär) ist die Summe kleiner als 1: der Rest ist "Photonen verloren".
std::map<Pattern, double> distribution(const Matrix& u, const Pattern& in, double indistinguishability = 1.0);

// Mischung von Eingangszuständen (z. B. Quellen mit Mehrphotonen-Anteil): (Wahrscheinlichkeit, Besetzung)
std::map<Pattern, double> distribution_mixed(const Matrix& u,
                                             const std::vector<std::pair<double, Pattern>>& inputs,
                                             double indistinguishability = 1.0);

// Detektoren: Effizienz eta (jedes Photon unabhängig), Dunkelzählung p_dark je Detektor und Messfenster,
// Schwellwert-Detektoren (klick/kein Klick) oder photonenzahlauflösend. Ergebnis: Verteilung der Zählmuster
// (bei Schwellwert-Detektoren 0/1 je Mode).
struct Detectors {
  double efficiency = 1.0;
  double dark_count = 0.0;
  bool number_resolving = false;
};
std::map<Pattern, double> detect(const std::map<Pattern, double>& dist, int modes,
                                 const Detectors& detectors);

// Haar-zufällige unitäre Matrix (QR einer komplexen Gauß-Matrix, Phasen korrigiert), reproduzierbar über seed
Matrix random_unitary(int n, std::uint64_t seed);

// Strahlteiler auf den Moden (a, b) einer m-Moden-Identität: [[sqrt(R), sqrt(T)], [sqrt(T), -sqrt(R)]]
Matrix beam_splitter(int modes, int a, int b, double reflectivity);
// Phasenschieber exp(i phi) auf Mode a
Matrix phase(int modes, int a, double phi);

}  // namespace photonlab::quantum
