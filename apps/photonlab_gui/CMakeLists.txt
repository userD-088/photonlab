add_executable(photonlab_gui main.cpp)
target_link_libraries(photonlab_gui PRIVATE photonlab::core imgui)
photonlab_warnings(photonlab_gui)
