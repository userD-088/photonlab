#pragma once
// Reiter "S-Parameter" des Bauteil-Labors: Extraktion an den Ports der Feld-Szene (als Job), Anzeige der
// S-Matrix über der Wellenlänge, Übernahme in die Bauteilbibliothek des Chip-Designers (Brücke 1).
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "chip_designer.hpp"
#include "photonlab/circuit/sparameter_table.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/field/sparameters.hpp"

namespace photonlab::gui {

class SParamView {
 public:
  SParamView(ThreadPool& pool, ChipDesigner& designer);
  ~SParamView();
  SParamView(const SParamView&) = delete;
  SParamView& operator=(const SParamView&) = delete;

  void draw();  // Inhalt des Reiters

 private:
  struct Job {
    circuit::SParameterTable table;
    std::vector<std::string> warnings;
    field::FieldScene scene;
    std::string error;
    bool cancelled = false;
  };
  void start();
  void update();
  void draw_settings(field::FieldScene& scene);
  void draw_result();
  void draw_library();
  void show(const circuit::SParameterTable& table, const field::FieldScene* scene);
  void add_to_library(circuit::SParameterTable table);

  ThreadPool& pool_;
  ChipDesigner& designer_;
  std::shared_ptr<JobControl> control_;
  std::future<Job> future_;
  std::uint64_t seen_generation_ = 0;

  // angezeigte Tabelle (frisch extrahiert oder aus der Bibliothek)
  bool has_table_ = false;
  circuit::SParameterTable table_;
  bool table_from_extraction_ = false;
  field::FieldScene table_scene_;  // Szene, aus der die frisch extrahierte Tabelle stammt
  std::vector<std::string> warnings_;
  std::string error_;
  char name_buffer_[64] = {};
  int input_port_ = 0;
  bool show_db_ = true;
  bool show_phase_ = false;
  bool fit_plot_ = true;
};

}  // namespace photonlab::gui
