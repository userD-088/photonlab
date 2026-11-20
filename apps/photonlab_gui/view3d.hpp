#pragma once
// 3D-Ansicht des Reiters "Feld": dieselbe Szene als Schicht (z. B. 220 nm Si auf Oxid) im 3D-FDTD.
// Die Rechnung läuft als Job; Löser, Steuerung und Schnappschuss gehören dem Job (shared_ptr), die
// Oberfläche liest nur fertige Schnappschüsse. Physik: libs/field/extrude3d.*
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "chip_designer.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/field/extrude3d.hpp"
#include "viewport3d.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace photonlab::gui {

class View3d {
 public:
  View3d(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer);
  ~View3d();
  View3d(const View3d&) = delete;
  View3d& operator=(const View3d&) = delete;

  // Hauptbereich des Reiters "Feld" im 3D-Modus: 3D-Umgebung, Rechnung, Monitore und flache Schnitte
  void draw(const ViewportHooks& hooks, float ui_scale);
  void fit_view() { viewport_.fit(); }
  void stop();  // laufende Rechnung abbrechen
  bool running() const { return future_.valid(); }

 private:
  struct Snapshot {
    std::mutex mutex;
    int nx = 0, ny = 0, nz = 0, layer_k = 0;
    double cell = 0.0, time = 0.0, y_cut = 0.0, layer_bottom = 0.0;
    std::size_t step = 0, total = 0;
    std::vector<float> top, side, top_index, side_index;
    std::vector<field::Monitor3dData> monitors;
    std::vector<std::string> warnings;
    field::StackIndices indices;
    std::string error;
    double seconds = 0.0;  // Rechenzeit bisher
    std::uint64_t serial = 0;
  };
  struct Picture {
    SDL_Texture* texture = nullptr;
    int nx = 0, ny = 0;
    std::vector<std::uint8_t> pixels;
  };
  void start();
  // alpha: Deckkraft nach Feldstärke (für die Ebenen in der 3D-Umgebung), sonst Kerne aufgehellt
  void fill(Picture& picture, const std::vector<float>& values, const std::vector<float>& index, int nx,
            int ny, bool flip_rows, bool alpha = false);
  void draw_settings();
  void draw_pictures(float height);
  void draw_monitors();

  ThreadPool& pool_;
  ChipDesigner& designer_;
  SDL_Renderer* renderer_;
  field::Stack3d stack_;
  bool intensity_ = false;
  std::shared_ptr<JobControl> control_;
  std::shared_ptr<Snapshot> snapshot_;
  std::shared_ptr<std::atomic<double>> y_cut_ = std::make_shared<std::atomic<double>>(0.0);
  bool y_cut_set_ = false;
  std::future<void> future_;
  field::FieldScene computed_scene_;  // Szene der letzten Rechnung (für "Szene geändert")
  bool has_run_ = false;
  double rate_ = 0.0;  // gemessene Zellen * Schritte je Sekunde der letzten Rechnung (Zeitschätzung)
  std::uint64_t shown_serial_ = 0;
  bool shown_intensity_ = false;
  Picture top_, side_, top3d_, side3d_;
  Viewport3d viewport_;
};

}  // namespace photonlab::gui
