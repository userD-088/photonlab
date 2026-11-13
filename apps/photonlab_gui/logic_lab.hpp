#pragma once
// Arbeitsbereich Logik-Labor: Gatter-Definition, automatische Wahrheitstabelle, Zeugnis mit Ampeln,
// Optimierer. Wertet die Schaltung des Chip-Designers aus; die Physik kommt aus libs/logic.
#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include "chip_designer.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/logic/optimize.hpp"
#include "photonlab/logic/report.hpp"

namespace photonlab::gui {

class LogicLab {
 public:
  static constexpr const char* window_name = "Logik-Labor";

  LogicLab(ThreadPool& pool, ChipDesigner& designer);
  ~LogicLab();

  void draw();

 private:
  struct Result {
    std::string key;
    std::string error;
    logic::GateReport report;
    logic::GateDefinition gate;  // Definition, zu der das Zeugnis gehört
  };

  std::string key() const;
  void update();
  void draw_definition();
  void draw_truth_table();
  void draw_report();
  void draw_optimizer();
  void draw_empty_state();

  ThreadPool& pool_;
  ChipDesigner& designer_;

  std::future<Result> future_;
  Result result_;

  // Optimierer
  std::string optimize_instance_, optimize_param_;
  std::string optimize_range_owner_;
  double optimize_min_ = 0.0, optimize_max_ = 1.0;  // Anzeige-Einheiten
  logic::OptimizeResult optimize_result_;
  std::string optimize_result_owner_;
  std::string optimize_error_;
  bool fit_optimizer_ = true;
};

}  // namespace photonlab::gui
