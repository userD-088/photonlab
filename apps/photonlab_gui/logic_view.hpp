#pragma once
// Fenster "Logik" des Chip-Designers: logische Eingänge und Ausgänge der Schaltung festlegen, Bits live
// schalten (die Schaltung auf der Zeichenfläche folgt), Wahrheitstabelle und Gatter-Zeugnis. Die Physik
// kommt aus libs/logic.
#include <future>
#include <string>

#include "chip_designer.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/logic/report.hpp"

namespace photonlab::gui {

class LogicView {
 public:
  LogicView(ThreadPool& pool, ChipDesigner& designer);
  ~LogicView();
  LogicView(const LogicView&) = delete;
  LogicView& operator=(const LogicView&) = delete;

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
  void draw_live();  // Bits schalten, Ausgänge lesen
  void draw_definition();
  void draw_truth_table();
  void draw_report();
  void draw_empty_state();
  int input_bit(std::size_t input) const;  // aktuelles Bit aus den Parametern der Schaltung
  void set_input_bit(std::size_t input, int bit);
  double output_threshold(std::size_t output) const;  // 0 = unbekannt

  ThreadPool& pool_;
  ChipDesigner& designer_;
  std::future<Result> future_;
  Result result_;
  bool show_definition_ = false;
  std::uint64_t seen_generation_ = 0;
};

}  // namespace photonlab::gui
