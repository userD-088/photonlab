add_executable(photonlab_gui
  main.cpp
  chip_designer.cpp
  field_lab.cpp
  file_dialog.cpp
  logic_lab.cpp
  mode_view.cpp)
target_link_libraries(photonlab_gui PRIVATE photonlab::core photonlab::circuit photonlab::logic photonlab::field photonlab::modes photonlab::materials photonlab::io imgui implot)
photonlab_warnings(photonlab_gui)

# Rauchtest ohne Bildschirm: ein paar Bilder mit dem Dummy-Videotreiber rendern und beenden
if(PHOTONLAB_BUILD_TESTS)
  add_test(NAME gui_smoke COMMAND photonlab_gui --frames 5)
  set_tests_properties(gui_smoke PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    TIMEOUT 60)
endif()
