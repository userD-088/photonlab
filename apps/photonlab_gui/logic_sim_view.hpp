#pragma once
// Reiter "Logikschaltung" des Logik-Labors (Phase 10): Design und Gatter-Familie wählen, statische Prüfung
// (Codierung, Pegel, Fan-out), ereignisgesteuerte Simulation aller Eingangsvektoren als Job, Fehlerursachen
// und Zeitdiagramm.
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/logic/logic_sim.hpp"

namespace photonlab::gui {

class LogicSimView {
 public:
  explicit LogicSimView(ThreadPool& pool);
  ~LogicSimView();
  LogicSimView(const LogicSimView&) = delete;
  LogicSimView& operator=(const LogicSimView&) = delete;
  void draw();

 private:
  struct Result {
    int design = 0, family = 0;
    logic::LogicCircuit circuit;
    logic::SimResult sim;
    std::string error;
  };
  void start();
  void update();
  void draw_timing(const Result& r);

  ThreadPool& pool_;
  int design_ = 2, family_ = 2;  // 4-Bit-Addierer, passiv
  std::shared_ptr<JobControl> control_;
  std::future<Result> future_;
  Result result_;
  bool has_result_ = false;
  int first_vector_ = 0;  // Zeitdiagramm: erster gezeigter Vektor
  bool fit_ = true;
};

}  // namespace photonlab::gui
