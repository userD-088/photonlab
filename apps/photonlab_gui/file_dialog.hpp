#pragma once
// Nativer Dateidialog über SDL3. Das Ergebnis kommt asynchron (je nach Plattform aus einem anderen Thread)
// und wird von der GUI einmal pro Bild abgeholt.
#include <optional>
#include <string>

struct SDL_Window;

namespace photonlab::gui {

enum class DialogKind { open_project, save_project };

struct DialogResult {
  DialogKind kind = DialogKind::open_project;
  std::string path;   // leer bei Abbruch oder Fehler
  std::string error;  // gesetzt, wenn der Dialog nicht angezeigt werden konnte
};

void show_file_dialog(DialogKind kind, SDL_Window* window);
std::optional<DialogResult> poll_file_dialog();

}  // namespace photonlab::gui
