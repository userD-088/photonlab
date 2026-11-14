#pragma once
// Reiter "Feld-Gatter (Kristall)" des Logik-Labors: Gatter auf Feld-Ebene (Y-Kombinierer im photonischen
// Kristall) auswählen, die Wahrheitstabelle aus FDTD-Rechnungen erzeugen (Job mit Fortschritt) und das
// Zeugnis mit Ampeln zeigen. Die Physik steht in libs/logic/field_gate.*.
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "chip_designer.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/logic/field_gate.hpp"

namespace photonlab::gui {

class FieldGateView {
 public:
  FieldGateView(ThreadPool& pool, ChipDesigner& designer);
  ~FieldGateView();
  FieldGateView(const FieldGateView&) = delete;
  FieldGateView& operator=(const FieldGateView&) = delete;

  void draw();

 private:
  struct Result {
    std::string gate_name;
    logic::FieldGate gate;
    logic::FieldGateReport report;
    std::string error;
    double seconds = 0.0;
    bool cancelled = false;
  };
  void start();
  void update();
  void draw_result();

  ThreadPool& pool_;
  ChipDesigner& designer_;
  std::vector<std::string> names_;
  int selected_ = 0;
  int gate_for_ = -1;  // Auswahl, zu der gate_ gebaut wurde (Szenen nicht in jedem Bild neu erzeugen)
  logic::FieldGate gate_;
  std::shared_ptr<JobControl> control_;
  std::future<Result> future_;
  Result result_;
  bool has_result_ = false;
};

}  // namespace photonlab::gui
