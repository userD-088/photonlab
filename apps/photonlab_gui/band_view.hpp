#pragma once
// Reiter "Bandstruktur" des Bauteil-Labors: Banddiagramm eines photonischen Kristalls (PWE als Job), Lücken
// mit Wellenlängen, Superzelle für Liniendefekte und ein Generator, der Kristall (mit Defekt) in die
// Feld-Szene setzt.
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "chip_designer.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/pwe/pwe.hpp"

namespace photonlab::gui {

class BandView {
 public:
  BandView(ThreadPool& pool, ChipDesigner& designer);
  ~BandView();
  BandView(const BandView&) = delete;
  BandView& operator=(const BandView&) = delete;

  // Inhalt des Reiters; Rückgabe true, wenn die Feld-Szene verändert wurde (Kristall eingesetzt).
  bool draw();

 private:
  struct Settings {
    int lattice = 0;           // 0 Dreieck, 1 Quadrat
    double radius_rel = 0.35;  // r / a
    double eps_background = 8.0, eps_inclusion = 2.0;
    int polarization = 2;  // 0 ez, 1 hz, 2 beide
    double cutoff = 6.0;
    bool line_defect = false;  // Superzelle mit W1 (nur Dreiecksgitter)
    int rows = 10;
    bool operator==(const Settings&) const = default;
  };
  struct Curve {
    pwe::Polarization polarization = pwe::Polarization::ez;
    pwe::BandResult bands;
    std::vector<pwe::Gap> bulk_gaps;  // Lücken des perfekten Kristalls (bei Superzelle zum Einfärben)
  };
  struct Output {
    Settings settings;
    std::vector<Curve> curves;
    std::string error;
  };
  void start();
  void update();
  void apply_preset(int preset);
  bool insert_crystal(int pattern);
  void draw_plot();
  void draw_gaps();

  ThreadPool& pool_;
  ChipDesigner& designer_;
  Settings settings_;
  double a_nm_ = 430.0;
  int defect_pattern_ = 1;  // Auswahl für den Generator: 0 keiner, 1 W1 gerade, 2 60-Grad-Knick
  std::shared_ptr<JobControl> control_;
  std::future<Output> future_;
  Output output_;
  bool has_output_ = false;
  bool presets_ready_ = false;
  bool fit_ = true;
};

}  // namespace photonlab::gui
