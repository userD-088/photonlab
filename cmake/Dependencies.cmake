# Alle externen Abhängigkeiten an EINER Stelle, Versionen fest gepinnt.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Eigen – lineare Algebra (nur Header). Das eigene CMake von Eigen wird übersprungen
# (SOURCE_SUBDIR zeigt ins Leere), weil es Tests und viele Zusatz-Targets anlegt.
FetchContent_Declare(eigen
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG        3.4.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  photonlab-kein-cmake)
FetchContent_MakeAvailable(eigen)
add_library(photonlab_eigen INTERFACE)
add_library(Eigen3::Eigen ALIAS photonlab_eigen)
target_include_directories(photonlab_eigen SYSTEM INTERFACE ${eigen_SOURCE_DIR})

# nlohmann/json – Projekt-, S-Parameter- und Gatterdateien
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

if(PHOTONLAB_BUILD_GUI)
  # SDL3 – statisch gelinkt, damit unter Windows keine DLL kopiert werden muss
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(SDL3)

  if(TARGET SDL3::SDL3-static)
    set(PHOTONLAB_SDL_TARGET SDL3::SDL3-static CACHE INTERNAL "")
  else()
    set(PHOTONLAB_SDL_TARGET SDL3::SDL3 CACHE INTERNAL "")
  endif()

  # Dear ImGui (Docking-Branch) – hat kein eigenes CMake, daher hier als Bibliothek
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8-docking
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(imgui)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC ${PHOTONLAB_SDL_TARGET})

  # ImPlot – Plots (Spektren, Kennlinien); ebenfalls ohne eigenes CMake
  FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.16
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(implot)

  add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp)
  target_include_directories(implot SYSTEM PUBLIC ${implot_SOURCE_DIR})
  target_link_libraries(implot PUBLIC imgui)
endif()

if(PHOTONLAB_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(doctest)
endif()
