cmake_minimum_required(VERSION 3.24)
project(PhotonLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # für clangd in VS Code

option(PHOTONLAB_BUILD_GUI   "SDL3/ImGui-Oberfläche bauen" ON)
option(PHOTONLAB_BUILD_TESTS "Tests bauen"                 ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

add_subdirectory(libs/core)
add_subdirectory(apps/photonlab_cli)

if(PHOTONLAB_BUILD_GUI)
  add_subdirectory(apps/photonlab_gui)
endif()

if(PHOTONLAB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
