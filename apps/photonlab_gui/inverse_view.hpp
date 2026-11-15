#pragma once
// Reiter "Inverses Design" des Bauteil-Labors: 90-Grad-Knick per Adjoint-Methode optimieren (Job), Dichte und
// Verlauf live, Ergebnis mit Spektrum, Export als GDSII und Übernahme in die Feld-Szene.
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "chip_designer.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/field/inverse_design.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace photonlab::gui {

class InverseView {
 public:
  InverseView(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer);
  ~InverseView();
  InverseView(const InverseView&) = delete;
  InverseView& operator=(const InverseView&) = delete;

  // Rückgabe true, wenn die Feld-Szene des Projekts ersetzt wurde
  bool draw();

 private:
  struct Live {  // vom Job geschrieben, von der Oberfläche gelesen
    std::mutex mutex;
    std::vector<double> density, history;
    int nx = 0, ny = 0;
    bool fresh = false;
  };
  struct Outcome {
    field::InverseDesignResult result;
    std::string error;
  };
  void start();
  void update();
  void update_texture(const std::vector<double>& density, int nx, int ny);

  ThreadPool& pool_;
  ChipDesigner& designer_;
  SDL_Renderer* renderer_;
  field::InverseDesignSettings settings_;
  bool three_wavelengths_ = true;
  std::shared_ptr<JobControl> control_;
  std::shared_ptr<Live> live_;
  std::future<Outcome> future_;
  Outcome outcome_;
  bool has_result_ = false;
  std::vector<double> history_;  // Anzeige (live oder fertig)
  SDL_Texture* texture_ = nullptr;
  int texture_nx_ = 0, texture_ny_ = 0;
  std::vector<std::uint8_t> pixels_;
};

}  // namespace photonlab::gui
