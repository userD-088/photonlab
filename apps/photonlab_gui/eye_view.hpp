#pragma once
// Fenster "Augendiagramm" (Phase 9): Übertragungsstrecke im Zeitbereich (Sender, Modulator, Empfänger) mit
// den Einstellungen des Projekts; rechnet als Job und zeigt Augendiagramm, Q-Faktor, Bitfehlerrate und
// Extinktion.
#include <future>
#include <memory>
#include <string>

#include "chip_designer.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/signal/link.hpp"

namespace photonlab::gui {

class EyeView {
 public:
  static constexpr const char* window_name = "Augendiagramm";
  EyeView(ThreadPool& pool, ChipDesigner& designer);
  ~EyeView();
  EyeView(const EyeView&) = delete;
  EyeView& operator=(const EyeView&) = delete;
  void draw();

 private:
  struct Result {
    std::string key;
    signal::LinkResult link;
    std::string error;
  };
  std::string key() const;
  void update();

  ThreadPool& pool_;
  ChipDesigner& designer_;
  std::shared_ptr<JobControl> control_;
  std::future<Result> future_;
  Result result_;
  bool fit_ = true;
};

}  // namespace photonlab::gui
