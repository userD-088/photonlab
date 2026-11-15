#pragma once
// Reiter "3D-Wellenleiter" des Bauteil-Labors: gerader Wellenleiter im 3D-FDTD (als Job), n_eff im Vergleich
// mit dem Modenlöser bzw. der analytischen Schicht, Draufsicht, Querschnitt und Phase entlang der Achse.
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/field/waveguide3d.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace photonlab::gui {

class Wave3dView {
 public:
  Wave3dView(ThreadPool& pool, SDL_Renderer* renderer);
  ~Wave3dView();
  Wave3dView(const Wave3dView&) = delete;
  Wave3dView& operator=(const Wave3dView&) = delete;

  void draw();  // Inhalt des Reiters (kein eigenes Fenster)

 private:
  struct Result {
    field::Waveguide3dSettings settings;
    field::Waveguide3dResult result;
    std::string error;
  };
  struct Picture {
    SDL_Texture* texture = nullptr;
    int nx = 0, ny = 0;
    std::vector<std::uint8_t> pixels;
  };
  void start();
  void update();
  void fill(Picture& picture, const std::vector<double>& values, int nx, int ny, bool signed_field);
  void show(const Picture& picture, float width, float height);

  ThreadPool& pool_;
  SDL_Renderer* renderer_;
  field::Waveguide3dSettings settings_;
  std::shared_ptr<JobControl> control_;
  std::future<Result> future_;
  Result result_;
  bool has_result_ = false;
  Picture top_, cross_, mode_;
};

}  // namespace photonlab::gui
