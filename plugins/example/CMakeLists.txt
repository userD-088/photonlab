# Beispiel-Plugin: wird als eigenständige Bibliothek gebaut und braucht nur den C-Header plugin_api.h.
add_library(photonlab_example_plugin MODULE example_plugin.cpp)
target_include_directories(photonlab_example_plugin PRIVATE ${PROJECT_SOURCE_DIR}/libs/circuit/include)
set_target_properties(photonlab_example_plugin PROPERTIES
  PREFIX ""
  CXX_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/$<0:>)  # $<0:> verhindert Unterordner je Konfiguration
photonlab_warnings(photonlab_example_plugin)
