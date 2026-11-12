#include "file_dialog.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <mutex>

namespace photonlab::gui {
namespace {

// Lebt bis zum Programmende, weil der Rückruf auch nach dem Schließen des Fensters eintreffen kann.
std::mutex g_mutex;
std::optional<DialogResult> g_result;

const SDL_DialogFileFilter g_filters[] = {{"PhotonLab-Projekt (*.json)", "json"}, {"Alle Dateien", "*"}};

void SDLCALL on_dialog_done(void* userdata, const char* const* filelist, int /*filter*/) {
  DialogResult result;
  result.kind = static_cast<DialogKind>(reinterpret_cast<std::intptr_t>(userdata));
  if (!filelist)
    result.error = SDL_GetError();
  else if (filelist[0])
    result.path = filelist[0];
  std::lock_guard lock(g_mutex);
  g_result = std::move(result);
}

}  // namespace

void show_file_dialog(DialogKind kind, SDL_Window* window) {
  void* userdata = reinterpret_cast<void*>(static_cast<std::intptr_t>(kind));
  if (kind == DialogKind::open_project)
    SDL_ShowOpenFileDialog(on_dialog_done, userdata, window, g_filters, 2, nullptr, false);
  else
    SDL_ShowSaveFileDialog(on_dialog_done, userdata, window, g_filters, 2, nullptr);
}

std::optional<DialogResult> poll_file_dialog() {
  std::lock_guard lock(g_mutex);
  std::optional<DialogResult> result = std::move(g_result);
  g_result.reset();
  return result;
}

}  // namespace photonlab::gui
