#pragma once
// Arbeitsbereich Bauteil-Labor: Szene bearbeiten, 2D-FDTD live rechnen, Feld als Farbkarte, Monitore als
// Plots. Die Rechnung läuft als Job im Threadpool; die Oberfläche liest nur fertige Schnappschüsse.
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "band_view.hpp"
#include "chip_designer.hpp"
#include "field_gate_view.hpp"
#include "imgui.h"
#include "inverse_view.hpp"
#include "mode_view.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/field/fdtd.hpp"
#include "sparam_view.hpp"
#include "view3d.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace photonlab::gui {

class FieldLab {
 public:
  static constexpr const char* window_name = "Bauteil-Labor";

  FieldLab(ThreadPool& pool, ChipDesigner& designer, SDL_Renderer* renderer, float ui_scale);
  ~FieldLab();
  FieldLab(const FieldLab&) = delete;
  FieldLab& operator=(const FieldLab&) = delete;

  void draw();

 private:
  // Steuerung des Rechen-Jobs (von der Oberfläche geschrieben, vom Job gelesen)
  struct Control {
    std::atomic<bool> stop{false};
    std::atomic<int> steps_per_frame{4};
    std::atomic<bool> full_speed{false};
    std::atomic<std::size_t> stop_at{0};  // Schrittzahl, bei der der Job von selbst endet; 0 = nie
    std::atomic<bool> finished{false};
  };
  // Letzter fertiger Schnappschuss (Double-Buffering zwischen Job und Oberfläche)
  struct Snapshot {
    std::mutex mutex;
    std::vector<float> field;
    std::vector<field::MonitorData> monitors;
    double time = 0.0;
    std::size_t step = 0;
    double energy = 0.0;
    bool fresh = false;
  };
  enum class ObjectKind { none, shape, source, monitor, port, group };
  enum class DisplayMode { field, intensity };
  enum class NewObject { rectangle, circle, ring, taper, arc, group, source, monitor, port };
  struct ObjectRef {
    ObjectKind kind = ObjectKind::none;
    int index = -1;
    bool operator==(const ObjectRef&) const = default;
  };

  void rebuild();  // Szene geändert: Löser neu aufsetzen
  void start();
  void pause();
  void fetch_snapshot();     // neuesten Schnappschuss des Jobs übernehmen
  void snapshot_from_sim();  // Schnappschuss direkt aus dem ruhenden Löser
  void update_texture();
  void update_spectrum();
  bool running() const { return job_.valid(); }

  void draw_field_tab();
  void draw_solvers_tab();
  static field::FieldScene blank_scene();
  void draw_toolbar();
  void draw_controls();
  void toggle_running();  // Start/Pause (auch Leertaste)
  void reset_run();       // Zurücksetzen (auch Taste R)
  void draw_scene_settings();
  void draw_add_toolbar();
  void add_new(NewObject what);  // neues Objekt in target_group(), ausgewählt
  int target_group() const;
  void move_selection_to_group(int group);  // -1 = oberste Ebene
  void shape_to_end(bool front);
  void rotate_selection(double angle);
  void draw_objects();
  void list_click(ObjectRef ref);  // Klick in der Objektliste: Strg einzeln, Umschalt Bereich
  bool group_combo(const char* label, int* group, int exclude = -1);
  void group_selection();  // Strg+G: neue Gruppe um die Auswahl
  void ungroup_selection();
  void draw_selected_object();
  void draw_canvas(float height);
  void draw_context_items();  // Inhalt des Rechtsklick-Menüs (Feld und Objektliste)

  // Auswahl und Bearbeitung mehrerer Objekte
  bool is_selected(ObjectKind kind, int index) const;
  void select(ObjectRef ref, bool additive);  // additive: Strg/Umschalt, umschalten statt ersetzen
  void clear_selection();
  void select_all();
  void delete_selection();
  void copy_selection();
  void paste(bool from_selection);  // false: aus der Zwischenablage, true: Auswahl duplizieren
  void set_selection_enabled(bool enabled);
  void move_selection(double dx, double dy);
  bool selection_enabled() const;  // true, wenn mindestens ein ausgewähltes Objekt aktiv ist
  void handle_shortcuts();
  void release_keys();   // Leertaste und Pfeiltasten wieder der Oberfläche überlassen
  void track_history();  // Schnappschuss nach jeder abgeschlossenen Änderung (Rückgängig)
  void undo();
  void redo();
  ObjectRef pick(ImVec2 mouse) const;  // Objekt unter dem Mauszeiger (Bildschirmkoordinaten)
  ImVec2 to_screen(double x, double y) const {
    return {canvas_origin_.x + static_cast<float>(x) * px_per_m_,
            canvas_origin_.y + static_cast<float>(y) * px_per_m_};
  }
  void draw_plots();

  ThreadPool& pool_;
  ChipDesigner& designer_;
  SDL_Renderer* renderer_;
  float ui_scale_;
  BandView band_view_;
  ModeView mode_view_;
  View3d view3d_;
  InverseView inverse_view_;
  SParamView sparam_view_;
  FieldGateView field_gate_view_;
  int pending_tab_ = -1;  // Reiter, der im nächsten Bild nach vorn kommt (0 Feld, 1 Bauteil)
  int solver_page_ = 0;  // Reiter "Moden und Bänder": 0 Querschnitt, 1 Bandstruktur
  bool mode3d_ = false;  // Reiter "Feld": 3D-Rechnung der Szene statt 2D

  std::uint64_t seen_generation_ = 0;
  bool scene_changed_ = false;
  std::string error_;

  std::shared_ptr<field::Fdtd> sim_;
  std::shared_ptr<Control> control_;
  std::shared_ptr<Snapshot> snapshot_;
  std::future<void> job_;
  bool want_running_ = true;
  bool gpu_setting_ = false;  // Stand von gpu::enabled() beim Aufsetzen des Lösers  // läuft, sobald der
                              // Arbeitsbereich sichtbar ist
  bool stop_at_duration_ = true;
  int steps_per_frame_ = 8;
  bool full_speed_ = false;

  // Darstellung
  SDL_Texture* texture_ = nullptr;
  int texture_nx_ = 0, texture_ny_ = 0;
  std::vector<std::uint8_t> pixels_;
  std::vector<float> field_;  // angezeigter Schnappschuss
  std::vector<float> tint_;   // Aufhellung je Zelle für die Geometrie (0..1)
  std::vector<field::MonitorData> monitors_;
  double time_ = 0.0, energy_ = 0.0;
  std::size_t step_ = 0;
  DisplayMode display_mode_ = DisplayMode::field;
  bool show_geometry_ = true;
  bool fixed_scale_ = false;
  bool boost_weak_ = true;
  bool flux_average_ = true;  // Flussdiagramm: gleitender Mittelwert über eine Periode
  float scale_ = 1.0f;        // Feldwert, der der vollen Farbe entspricht
  bool texture_dirty_ = true;

  // Auswahl
  ObjectKind selected_kind_ = ObjectKind::none;  // zuletzt angeklicktes Objekt (Eigenschaften-Feld)
  int selected_index_ = -1;
  std::vector<ObjectRef> selection_;   // alle ausgewählten Objekte, das zuletzt angeklickte am Ende
  std::vector<ObjectRef> list_order_;  // Reihenfolge der Objektliste im letzten Bild (Umschalt-Klick)
  ObjectRef list_anchor_;              // Anfang eines Umschalt-Bereichs
  field::FieldScene clipboard_;        // kopierte Objekte (nur die Listen sind belegt)
  bool dragging_ = false;
  bool drag_armed_ = false;     // Linksklick auf ein ausgewähltes Objekt: Ziehen verschiebt die Auswahl
  bool box_selecting_ = false;  // Linksklick ins Leere: Rechteck aufziehen
  ImVec2 box_start_{};

  // Ansicht des Feldes: Zoom um die Mitte der Szene, Verschiebung in Bildpunkten
  float zoom_ = 1.0f;
  ImVec2 pan_{};
  ImVec2 canvas_origin_{};  // Bildschirmort von (0, 0) im letzten Bild
  float px_per_m_ = 1.0f;

  // Rückgängig/Wiederholen: Szenen vor bzw. nach den Änderungen
  std::vector<field::FieldScene> undo_, redo_;
  field::FieldScene last_scene_;
  std::uint64_t history_generation_ = static_cast<std::uint64_t>(-1);
  ImGuiID key_owner_ = 0;

  // Spektrum der Punkt-Monitore
  std::vector<double> spectrum_wavelength_nm_;
  std::vector<std::vector<double>> spectrum_;
  std::vector<std::string> spectrum_names_;
  std::size_t spectrum_step_ = static_cast<std::size_t>(-1);
  int frames_since_spectrum_ = 1000;
};

}  // namespace photonlab::gui
