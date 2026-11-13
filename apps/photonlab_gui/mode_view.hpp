#pragma once
// Querschnitt-Ansicht des Bauteil-Labors: Wellenleiter-Querschnitt festlegen, Moden berechnen (als Job),
// Modenprofil als Bild, Kenngrößen als Tabelle, Übergabe von n_eff und n_g an den Chip-Designer.
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "chip_designer.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/modes/cross_section.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace photonlab::gui {

class ModeView {
 public:
  ModeView(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer, float ui_scale);
  ~ModeView();
  ModeView(const ModeView&) = delete;
  ModeView& operator=(const ModeView&) = delete;

  void draw();  // Inhalt des Reiters (kein eigenes Fenster)

 private:
  struct Result {
    modes::CrossSection section;
    modes::CrossSectionResult result;
    std::string error;
  };
  void start();
  void update();
  void update_texture();
  void apply_to_circuit();

  ThreadPool& pool_;
  ChipDesigner& designer_;
  SDL_Renderer* renderer_;
  float ui_scale_;

  std::shared_ptr<JobControl> control_;
  std::future<Result> future_;
  Result result_;
  bool has_result_ = false;
  bool auto_started_ = false;
  int selected_mode_ = 0;

  SDL_Texture* texture_ = nullptr;
  int texture_nx_ = 0, texture_ny_ = 0;
  bool texture_dirty_ = true;
  bool show_intensity_ = false;
  std::vector<std::uint8_t> pixels_;
};

}  // namespace photonlab::gui
