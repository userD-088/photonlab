#pragma once
// 3D-Umgebung des Bauteil-Labors: die Szene als Schichtaufbau im Raum (Formen als Körper, Substrat, Mantel,
// Quellen, Monitore und Ports als Flächen, Feldschnitte der 3D-Rechnung als Ebenen). Perspektivische Kamera
// mit Drehen, Verschieben und Zoomen; Objekte anklicken und auf der Schicht verschieben. Gezeichnet wird mit
// ImGui-Dreiecken (Maleralgorithmus: von hinten nach vorn), ohne eigene Grafik-Abhängigkeit.
#include <functional>

#include "imgui.h"
#include "photonlab/field/extrude3d.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::gui {

enum class Pick3d { none, shape, source, monitor, port };

// Verbindung zur Auswahl des Bauteil-Labors
struct ViewportHooks {
  std::function<bool(Pick3d, int)> is_selected;
  std::function<void(Pick3d, int, bool additive)> select;  // Pick3d::none: Auswahl aufheben
  std::function<void(double dx, double dy)> move;          // Auswahl verschieben [m]
  std::function<void()> move_done;                         // Ziehen beendet
  std::function<void()> context_menu;                      // Inhalt des Rechtsklick-Menüs
};

// Feldschnitte der 3D-Rechnung als Texturen (RGBA mit Deckkraft nach Feldstärke)
struct FieldSlices {
  ImTextureID top = 0;                         // Draufsicht in der Mitte der Schicht, nx * ny
  ImTextureID side = 0;                        // Seitenansicht bei y_cut, nx * nz (Zeile 0 oben)
  double grid_width = 0.0, grid_height = 0.0;  // nx * dx, ny * dx [m]
  double grid_depth = 0.0;                     // nz * dx [m]
  double layer_bottom = 0.0;                   // Unterkante der Schicht im Gitter [m]
  double y_cut = 0.0;                          // Lage der Seitenansicht [m]
};

class Viewport3d {
 public:
  // Zeichnet in die Fläche size an der aktuellen Cursorposition.
  void draw(const field::FieldScene& scene, const field::Stack3d& stack, const FieldSlices* slices,
            const ViewportHooks& hooks, ImVec2 size, float ui_scale);
  void fit() { fit_pending_ = true; }

 private:
  struct Camera {
    float yaw = -1.05f, pitch = 0.62f;      // [rad]; Blick schräg von vorn links oben
    float distance = 20.0f;                 // [µm]
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;  // Blickziel [µm]
  };
  Camera camera_;
  float exaggeration_ = 3.0f;  // Überhöhung in z, damit die dünne Schicht sichtbar ist
  bool show_cladding_ = true;
  bool show_field_ = true;
  bool show_labels_ = true;
  bool fit_pending_ = true;
  double fitted_width_ = 0.0, fitted_height_ = 0.0;
  // Ziehen
  bool dragging_object_ = false, moved_ = false;
  float drag_x_ = 0.0f, drag_y_ = 0.0f;  // letzter Punkt auf der Schichtebene [µm, Welt]
};

}  // namespace photonlab::gui
