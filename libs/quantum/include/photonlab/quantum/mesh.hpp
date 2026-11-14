#pragma once
// MZI-Gitter (Bauplan 10): Zerlegung einer unitären N x N-Matrix in N(N-1)/2 Mach-Zehnder-Interferometer nach
// Clements u. a., Optica 3, 1460 (2016); Fehlermodell (ungleiche Koppler, Phasenfehler, Verlust, thermisches
// Übersprechen), Kalibrierung, beliebige Matrizen über die Singulärwertzerlegung, kleine Demo eines
// neuronalen Netzes.
#include <cstdint>
#include <vector>

#include "photonlab/quantum/fock.hpp"

namespace photonlab::quantum {

// MZI auf den Moden (mode, mode + 1): T(theta, phi) = [[e^{i phi} cos theta, -sin theta], [e^{i phi} sin
// theta, cos theta]]
struct Mzi {
  int mode = 0;
  double theta = 0.0, phi = 0.0;
};

struct MeshDecomposition {
  int size = 0;
  std::vector<Mzi> mzis;              // in Reihenfolge vom Eingang her
  std::vector<double> output_phases;  // Phasen am Ausgang (Diagonale D)
};

Matrix mzi_matrix(int size, const Mzi& mzi);
MeshDecomposition clements_decompose(const Matrix& u);  // wirft bei nicht-unitärem U
Matrix reconstruct(const MeshDecomposition& mesh);

// Fehler eines gefertigten Gitters. Jedes MZI besteht physikalisch aus zwei 50:50-Kopplern
// (Teilungsverhältnis 0,5 + Fehler), einem inneren und einem äußeren Phasenschieber. Fehler werden
// reproduzierbar aus seed gezogen.
struct MeshErrors {
  double coupler_sigma = 0.0;  // Standardabweichung des Teilungsverhältnisses je Koppler
  double phase_sigma = 0.0;    // Standardabweichung der Phasen [rad] (fester Versatz)
  double loss_db = 0.0;        // Verlust je MZI [dB]
  double crosstalk =
      0.0;  // Anteil der inneren Phase eines MZI, der auf die Nachbarn in derselben Spalte wirkt
  std::uint64_t seed = 1;
};
// Matrix, die ein gefertigtes Gitter mit diesen Einstellungen tatsächlich umsetzt
Matrix realize(const MeshDecomposition& mesh, const MeshErrors& errors);

// Güte: |Tr(U^dagger V)|^2 / (N Tr(V^dagger V)) (1 = gleich bis auf eine globale Phase, Verlust
// herausgerechnet)
double fidelity(const Matrix& target, const Matrix& realized);

// Kalibrierung: Phasen (theta, phi, Ausgangsphasen) so nachstellen, dass das gefertigte Gitter (bekannte
// Fehler, z. B. gemessen) der Zielmatrix möglichst nahe kommt. Koordinatensuche mit kleiner werdender
// Schrittweite.
MeshDecomposition calibrate(const MeshDecomposition& start, const MeshErrors& errors, const Matrix& target,
                            int sweeps = 30);

// Beliebige reelle Matrix W = U diag(s) V^T: zwei Gitter und Dämpfungsglieder (Skalierung mit s_max)
struct SvdMesh {
  MeshDecomposition u, v;           // v setzt V^T um
  std::vector<double> attenuation;  // s_i / s_max (0..1)
  double scale = 1.0;               // s_max (elektronisch)
};
SvdMesh svd_decompose(const Eigen::MatrixXd& w);
// Komplexe Matrix, die die beiden gefertigten Gitter mit den Dämpfungsgliedern umsetzen (ideal: W reell)
Matrix svd_realize(const SvdMesh& mesh, const MeshErrors& errors_u, const MeshErrors& errors_v);

// Demo: kleines neuronales Netz (4 Merkmale -> 4 verborgene Werte |W1 x|^2 -> 3 Klassen), W1 optisch im
// SVD-Gitter.
struct NeuralDemoResult {
  double accuracy_ideal = 0.0;                                  // W1 exakt
  double accuracy_fabricated = 0.0;                             // W1 im gefertigten Gitter mit Fehlern
  double accuracy_calibrated = 0.0;                             // nach Kalibrierung beider Gitter
  double fidelity_fabricated = 0.0, fidelity_calibrated = 0.0;  // mittlere Güte der beiden Gitter
  int samples = 0;
};
NeuralDemoResult neural_network_demo(const MeshErrors& errors, std::uint64_t seed = 7);

}  // namespace photonlab::quantum
