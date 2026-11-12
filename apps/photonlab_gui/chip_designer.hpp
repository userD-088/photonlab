#pragma once
// Arbeitsbereich Chip-Designer: Raster-Editor, Bauteilbibliothek, Eigenschaften, Spektrum und Kennlinie.
// Die Physik kommt vollständig aus libs/circuit; hier wird nur gezeichnet und bedient.
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/io/project.hpp"

struct SDL_Window;

namespace photonlab::gui {

class ChipDesigner {
 public:
  // Fenstertitel (auch für das Standard-Layout in main.cpp)
  static constexpr const char* window_canvas = "Chip-Designer";
  static constexpr const char* window_library = "Bauteilbibliothek";
  static constexpr const char* window_properties = "Eigenschaften";
  static constexpr const char* window_spectrum = "Spektrum";
  static constexpr const char* window_curve = "Kennlinie";

  ChipDesigner(ThreadPool& pool, SDL_Window* window, float ui_scale);
  ~ChipDesigner();

  void draw();            // alle Fenster des Arbeitsbereichs
  void draw_file_menu();  // Einträge im Menü "Datei"

  bool open_file(const std::string& path);
  void request_quit();  // fragt bei ungespeicherten Änderungen nach
  bool quit_confirmed() const { return quit_confirmed_; }

 private:
  enum class Pending { none, new_project, open_project, load_example, quit };

  struct SeriesData {
    std::vector<double> x;
    std::vector<std::string> names;
    std::vector<std::vector<double>> power_mw;  // je Messgröße eine Kurve
    std::string key;                            // woraus die Daten gerechnet wurden
    std::string error;
  };

  struct PortGeometry {
    ImVec2 position;
    bool left = true;
  };

  // Bearbeiten
  void touch();  // Schaltung wurde physikalisch geändert
  void set_project(io::Project project, std::string path);
  void request(Pending action, std::string argument = {});
  void perform(Pending action, const std::string& argument);
  bool save_file(const std::string& path);
  void save_or_ask();
  void add_instance(const std::string& type, int grid_x, int grid_y);
  void rename_selected(const std::string& new_name);
  void set_status(std::string text, bool is_error = false);

  // Rechnen
  void update_solution();
  void update_sweep();
  void update_curve();
  std::string sweep_key() const;

  // Zeichnen
  void draw_canvas();
  void draw_library();
  void draw_properties();
  void draw_spectrum();
  void draw_curve();
  void draw_confirm_popup();
  void handle_dialog_result();
  std::optional<PortGeometry> port_geometry(const circuit::PortRef& port) const;
  ImVec2 grid_to_screen(float grid_x, float grid_y) const;
  ImU32 wave_color(const circuit::PortRef& a, const circuit::PortRef& b, float* strength) const;
  std::string summary(const circuit::Instance& instance, const circuit::ComponentType& type) const;

  ThreadPool& pool_;
  SDL_Window* window_;
  float ui_scale_;

  io::Project project_;
  std::string file_path_;
  bool dirty_ = false;
  std::uint64_t revision_ = 1;
  std::string status_;
  bool status_is_error_ = false;

  Pending pending_ = Pending::none;
  std::string pending_argument_;
  bool open_confirm_popup_ = false;
  bool quit_confirmed_ = false;

  // Live-Lösung bei der Arbeitswellenlänge
  std::uint64_t solved_revision_ = 0;
  double solved_wavelength_ = 0.0;
  std::shared_ptr<const circuit::CompiledCircuit> compiled_;
  bool has_solution_ = false;
  circuit::Solution solution_;
  std::vector<circuit::InstanceMeasurement> measurements_;
  circuit::PowerBalance balance_;
  double max_wave_power_ = 0.0;
  std::string solve_error_;

  // Editor
  std::string selected_;
  std::string rename_buffer_;
  ImVec2 pan_{60.0f, 60.0f};
  float cell_ = 28.0f;
  int fit_view_ = 3;  // Anzahl Bilder, in denen die Ansicht noch eingepasst wird (das Layout setzt sich erst)
  ImVec2 origin_{0.0f, 0.0f};  // Bildschirmposition der Rasterzelle (0, 0), pro Bild gesetzt
  ImVec2 view_center_grid_{8.0f, 4.0f};
  std::optional<circuit::PortRef> wire_from_;
  int drag_start_x_ = 0, drag_start_y_ = 0;

  // Sweep-Job
  std::shared_ptr<JobControl> sweep_control_;
  std::future<SeriesData> sweep_future_;
  SeriesData sweep_result_;
  bool auto_sweep_ = true;
  bool spectrum_dbm_ = false;
  bool fit_spectrum_ = true;

  // Kennlinie (Parameter-Sweep des ausgewählten Bauteils)
  SeriesData curve_;
  std::string curve_param_;
  std::string curve_unit_;
  double curve_min_ = 0.0, curve_max_ = 1.0;  // in Anzeige-Einheiten
  std::string curve_range_owner_;             // "instanz/parameter", für den die Grenzen gesetzt wurden
  bool fit_curve_ = true;
};

}  // namespace photonlab::gui
