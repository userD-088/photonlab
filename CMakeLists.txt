cmake_minimum_required(VERSION 3.24)
project(PhotonLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # für clangd in VS Code

option(PHOTONLAB_BUILD_GUI   "SDL3/ImGui-Oberfläche bauen" ON)
option(PHOTONLAB_BUILD_TESTS "Tests bauen"                 ON)
# Befehlssatz des eigenen Prozessors (AVX2 usw.): im 3D-FDTD etwa ein Drittel schneller, das Programm läuft
# dann aber nur auf Prozessoren mit denselben Befehlen (docs/performance.md)
option(PHOTONLAB_NATIVE      "Für den eigenen Prozessor optimieren" OFF)
# Grafikkarte über OpenCL für das 2D-FDTD (docs/performance.md). Gebraucht werden nur die Header; die
# OpenCL-Bibliothek des Treibers wird zur Laufzeit geladen, ohne sie rechnet alles auf dem Prozessor.
option(PHOTONLAB_OPENCL      "2D-FDTD auf der Grafikkarte (OpenCL)" ON)
if(PHOTONLAB_NATIVE)
  if(MSVC)
    add_compile_options(/arch:AVX2)
  else()
    add_compile_options(-march=native)
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

if(PHOTONLAB_BUILD_TESTS)
  enable_testing()   # vor den Unterordnern, damit auch apps/ Tests anmelden können
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/materials)
add_subdirectory(libs/modes)
add_subdirectory(libs/pwe)
add_subdirectory(libs/circuit)
add_subdirectory(libs/signal)
add_subdirectory(libs/quantum)
add_subdirectory(libs/logic)
add_subdirectory(libs/field)
add_subdirectory(libs/layout)
add_subdirectory(libs/io)
add_subdirectory(plugins/example)
add_subdirectory(apps/photonlab_cli)

if(PHOTONLAB_BUILD_GUI)
  add_subdirectory(apps/photonlab_gui)
endif()

if(PHOTONLAB_BUILD_TESTS)
  add_subdirectory(tests)
endif()
