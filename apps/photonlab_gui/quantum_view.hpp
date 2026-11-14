#pragma once
// Arbeitsbereich "Quanten-Modus" (Phase 11): Hong-Ou-Mandel, linear-optisches CNOT, Boson Sampling,
// MZI-Gitter (Clements, Fehler, Kalibrierung, Netz-Demo) und die Schaltung des Chip-Designers als
// Interferometer. Alle Rechnungen sind klein und laufen direkt im GUI-Thread, wenn sich eine Einstellung
// ändert.
#include <string>

#include "chip_designer.hpp"
#include "photonlab/quantum/experiments.hpp"
#include "photonlab/quantum/mesh.hpp"

namespace photonlab::gui {

class QuantumView {
 public:
  static constexpr const char* window_name = "Quanten-Modus";
  explicit QuantumView(ChipDesigner& designer);
  void draw();

 private:
  void draw_hom();
  void draw_cnot();
  void draw_boson();
  void draw_mesh();
  void draw_circuit();

  ChipDesigner& designer_;
  // HOM
  quantum::HomSettings hom_;
  quantum::HomResult hom_result_;
  bool hom_dirty_ = true;
  // CNOT
  double cnot_reflectivity_ = 1.0 / 3.0, cnot_indist_ = 1.0;
  quantum::CnotResult cnot_;
  bool cnot_dirty_ = true;
  // Boson Sampling
  int bs_modes_ = 6, bs_photons_ = 3, bs_samples_ = 5000, bs_seed_ = 42;
  quantum::BosonSamplingResult bs_;
  bool bs_dirty_ = true;
  // MZI-Gitter
  int mesh_size_ = 6, mesh_target_ = 0, mesh_seed_ = 1;
  quantum::MeshErrors mesh_errors_;
  quantum::Matrix mesh_u_;
  quantum::MeshDecomposition mesh_, mesh_calibrated_;
  double mesh_fidelity_ = 1.0, mesh_fidelity_cal_ = 0.0, mesh_reconstruction_ = 0.0;
  bool mesh_dirty_ = true, mesh_has_cal_ = false;
  quantum::NeuralDemoResult nn_;
  bool nn_valid_ = false;
  // Schaltung
  quantum::CircuitTransfer transfer_;
  std::string transfer_error_;
  std::uint64_t transfer_revision_ = ~0ull;
};

}  // namespace photonlab::gui
