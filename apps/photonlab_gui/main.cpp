// PhotonLab GUI: Fenster, Docking und die vier Arbeitsbereiche. Chip-Designer (Phase 1 und 2) und
// Logik-Labor (Phase 3) sind nutzbar, Bauteil-Labor und Quanten-Modus sind noch Platzhalter.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include "chip_designer.hpp"
#include "field_lab.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_internal.h"  // DockBuilder für das Standard-Layout
#include "implot.h"
#include "logic_lab.hpp"
#include "photonlab/core/thread_pool.hpp"

namespace {

using photonlab::gui::ChipDesigner;

const char* window_field = photonlab::gui::FieldLab::window_name;
const char* window_logic = photonlab::gui::LogicLab::window_name;
const char* window_quantum = "Quanten-Modus";

void apply_style(float ui_scale) {
  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 6.0f;
  s.FrameRounding = 4.0f;
  s.TabRounding = 4.0f;
  s.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.10f, 0.14f, 1.0f);
  s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.17f, 0.31f, 0.53f, 1.0f);
  s.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.16f, 0.22f, 1.0f);
  s.Colors[ImGuiCol_TabSelected] = ImVec4(0.17f, 0.31f, 0.53f, 1.0f);
  s.ScaleAllSizes(ui_scale);
  ImPlot::StyleColorsDark();
  ImPlot::GetStyle().LineWeight = 2.0f * ui_scale;
  ImPlot::GetStyle().FitPadding = ImVec2(0.0f, 0.06f);  // Kurven bei 0 und Maximum bleiben sichtbar
}

// Gut lesbare Systemschrift, falls vorhanden; sonst bleibt die eingebaute ImGui-Schrift.
void load_font(float ui_scale) {
  ImGuiIO& io = ImGui::GetIO();
  const char* candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                              "/usr/share/fonts/TTF/DejaVuSans.ttf"};
  for (const char* path : candidates) {
    std::error_code error;
    if (std::filesystem::exists(path, error) && io.Fonts->AddFontFromFileTTF(path, 17.0f * ui_scale)) return;
  }
  io.Fonts->AddFontDefault();
  io.FontGlobalScale = ui_scale;
}

void placeholder(const char* title, const char* phase, const char* text) {
  ImGui::Begin(title);
  ImGui::TextDisabled("%s", phase);
  ImGui::Separator();
  ImGui::TextWrapped("%s", text);
  ImGui::End();
}

// Standard-Anordnung: Arbeitsbereiche als Tabs in der Mitte, Bibliothek links, Eigenschaften rechts, Plots
// unten.
void build_default_layout(ImGuiID dock_id, ImVec2 size) {
  ImGui::DockBuilderRemoveNode(dock_id);
  ImGui::DockBuilderAddNode(dock_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock_id, size);
  ImGuiID center = dock_id;
  const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.13f, nullptr, &center);
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.42f, nullptr, &center);
  ImGuiID bottom_right = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.46f, nullptr, &bottom);
  ImGui::DockBuilderDockWindow(ChipDesigner::window_library, left);
  ImGui::DockBuilderDockWindow(ChipDesigner::window_properties, right);
  ImGui::DockBuilderDockWindow(ChipDesigner::window_spectrum, bottom);
  ImGui::DockBuilderDockWindow(ChipDesigner::window_monte_carlo, bottom_right);
  ImGui::DockBuilderDockWindow(ChipDesigner::window_curve, bottom_right);
  ImGui::DockBuilderDockWindow(window_field, center);
  ImGui::DockBuilderDockWindow(ChipDesigner::window_canvas, center);
  ImGui::DockBuilderDockWindow(window_logic, center);
  ImGui::DockBuilderDockWindow(window_quantum, center);
  ImGui::DockBuilderFinish(dock_id);
}

}  // namespace

int main(int argc, char** argv) {
  // Optionen: [projekt.plab.json] [--frames N] [--screenshot bild.bmp]
  std::string project_path, screenshot_path;
  int frame_limit = 0;  // 0 = unbegrenzt
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--frames" && i + 1 < argc)
      frame_limit = std::atoi(argv[++i]);
    else if (arg == "--screenshot" && i + 1 < argc)
      screenshot_path = argv[++i];
    else
      project_path = arg;
  }
  if (!screenshot_path.empty() && frame_limit == 0) frame_limit = 12;
  const bool automated = frame_limit > 0;

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init fehlgeschlagen: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window =
      SDL_CreateWindow("PhotonLab", 1500, 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    std::fprintf(stderr, "Fenster konnte nicht erstellt werden: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    std::fprintf(stderr, "Renderer konnte nicht erstellt werden: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);

  // Skalierung der Oberfläche: Windows meldet sie als Anzeige-Skalierung, macOS als Pixeldichte
  // (dort rechnet ImGui in Punkten, die Skalierung übernimmt SDL_SetRenderScale weiter unten).
  const float pixel_density = SDL_GetWindowPixelDensity(window);
  const float display_scale = SDL_GetWindowDisplayScale(window);
  float ui_scale = pixel_density > 0.0f && display_scale > 0.0f ? display_scale / pixel_density : 1.0f;
  if (ui_scale < 1.0f) ui_scale = 1.0f;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  if (automated) io.IniFilename = nullptr;  // Rauchtest und Bildschirmfoto: immer das Standard-Layout
  apply_style(ui_scale);
  load_font(ui_scale);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  int exit_code = 0;
  {
    photonlab::ThreadPool pool;
    ChipDesigner chip_designer(pool, window, ui_scale);
    photonlab::gui::LogicLab logic_lab(pool, chip_designer);
    photonlab::gui::FieldLab field_lab(pool, chip_designer, renderer, ui_scale);
    if (!project_path.empty() && !chip_designer.open_file(project_path) && automated) exit_code = 1;

    bool running = true, show_demo = false, reset_layout = false;
    int frame_count = 0;
    while (running && !chip_designer.quit_confirmed()) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        if (e.type == SDL_EVENT_QUIT) chip_designer.request_quit();
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))
          chip_designer.request_quit();
      }
      if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        continue;
      }

      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();

      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      const ImGuiID dock_id = ImGui::GetID("PhotonLabDock");
      if (reset_layout || ImGui::DockBuilderGetNode(dock_id) == nullptr) {
        build_default_layout(dock_id, viewport->WorkSize);
        reset_layout = false;
      }
      ImGui::DockSpaceOverViewport(dock_id, viewport);

      if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Datei")) {
          chip_designer.draw_file_menu();
          ImGui::Separator();
          if (ImGui::MenuItem("Beenden")) chip_designer.request_quit();
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Ansicht")) {
          if (ImGui::MenuItem("Standard-Layout wiederherstellen")) reset_layout = true;
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Hilfe")) {
          ImGui::MenuItem("ImGui-Demo anzeigen", nullptr, &show_demo);
          ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
      }

      field_lab.draw();
      chip_designer.draw();
      logic_lab.draw();
      placeholder(window_quantum, "Kommt in Phase 11",
                  "Einzelphotonen, Hong-Ou-Mandel, linear-optisches CNOT, Boson Sampling.");
      // Beim Start liegt der Chip-Designer vorn (einziger schon nutzbarer Arbeitsbereich)
      if (frame_count == 2) ImGui::SetWindowFocus(ChipDesigner::window_curve);
      if (frame_count == 3) {
        // reine Feld-Projekte öffnen im Bauteil-Labor, alles andere im Chip-Designer
        const auto& project = chip_designer.project();
        const bool field_only = !project.field.empty() && project.circuit.instances.empty();
        ImGui::SetWindowFocus(field_only ? window_field : ChipDesigner::window_canvas);
      }
      ++frame_count;
      if (show_demo) ImGui::ShowDemoWindow(&show_demo);

      ImGui::Render();
      SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
      SDL_SetRenderDrawColorFloat(renderer, 0.05f, 0.06f, 0.09f, 1.0f);
      SDL_RenderClear(renderer);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

      if (automated && frame_count >= frame_limit) {
        if (!screenshot_path.empty()) {
          SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
          if (!surface || !SDL_SaveBMP(surface, screenshot_path.c_str())) {
            std::fprintf(stderr, "Bildschirmfoto fehlgeschlagen: %s\n", SDL_GetError());
            exit_code = 1;
          }
          SDL_DestroySurface(surface);
        }
        running = false;
      }
      SDL_RenderPresent(renderer);
    }
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exit_code;
}
