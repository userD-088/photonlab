// PhotonLab GUI – Phase 0: Fenster, Docking und die vier Arbeitsbereiche als Platzhalter.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "photonlab/core/complex.hpp"
#include "photonlab/core/units.hpp"

namespace {

void apply_style() {
  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 6.0f;
  s.FrameRounding = 4.0f;
  s.TabRounding = 4.0f;
  s.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.10f, 0.14f, 1.0f);
  s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.17f, 0.31f, 0.53f, 1.0f);
  s.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.16f, 0.22f, 1.0f);
  s.Colors[ImGuiCol_TabSelected] = ImVec4(0.17f, 0.31f, 0.53f, 1.0f);
}

void placeholder(const char* title, const char* phase, const char* text) {
  ImGui::Begin(title);
  ImGui::TextDisabled("%s", phase);
  ImGui::Separator();
  ImGui::TextWrapped("%s", text);
  ImGui::End();
}

// Kleine Vorschau auf Phase 1: Mach-Zehnder-Ausgang als Funktion der Phasendifferenz
void chip_designer_demo() {
  using namespace photonlab;
  static float dphi = 0.0f;
  ImGui::Begin("Chip-Designer");
  ImGui::TextDisabled("Kommt in Phase 1 bis 2");
  ImGui::Separator();
  ImGui::TextWrapped("Vorschau: Mach-Zehnder-Interferometer mit zwei 50:50-Kopplern.");
  ImGui::SliderFloat("Phasendifferenz", &dphi, 0.0f, 2.0f * static_cast<float>(constants::pi), "%.2f rad");

  const double s = std::sqrt(0.5);
  const cplx in = 1.0;                                  // Licht nur in Port 1
  const cplx u1 = s * in, u2 = I * s * in;              // erster Koppler
  const cplx v1 = u1 * expi(static_cast<double>(dphi)); // Phasenschieber im oberen Arm
  const cplx v2 = u2;
  const cplx out1 = s * (v1 + I * v2), out2 = s * (I * v1 + v2);  // zweiter Koppler

  ImGui::ProgressBar(static_cast<float>(power(out1)), ImVec2(-1, 0), "Ausgang 1");
  ImGui::ProgressBar(static_cast<float>(power(out2)), ImVec2(-1, 0), "Ausgang 2");
  ImGui::Text("Summe = %.6f (Energieerhaltung)", power(out1) + power(out2));
  ImGui::End();
}

}  // namespace

int main(int, char**) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init fehlgeschlagen: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("PhotonLab", 1400, 860,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  apply_style();
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  bool running = true, show_demo = false;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL3_ProcessEvent(&e);
      if (e.type == SDL_EVENT_QUIT) running = false;
      if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))
        running = false;
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("Datei")) {
        if (ImGui::MenuItem("Beenden")) running = false;
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Hilfe")) {
        ImGui::MenuItem("ImGui-Demo anzeigen", nullptr, &show_demo);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    placeholder("Bauteil-Labor", "Kommt in Phase 4 bis 8",
                "Feld-Ebene: Geometrie zeichnen, Modenlöser, 2D-FDTD, Bandstrukturen. "
                "Referenz-Prototyp: docs/prototypes/photonische_gatter.html");
    chip_designer_demo();
    placeholder("Logik-Labor", "Kommt in Phase 3 und 10",
                "Wahrheitstabellen automatisch messen, Gatter-Zeugnis, Addierer und Flip-Flops.");
    placeholder("Quanten-Modus", "Kommt in Phase 11",
                "Einzelphotonen, Hong-Ou-Mandel, linear-optisches CNOT, Boson Sampling.");
    if (show_demo) ImGui::ShowDemoWindow(&show_demo);

    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, 0.05f, 0.06f, 0.09f, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
