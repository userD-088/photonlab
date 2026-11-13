add_library(photonlab_field
  src/fdtd.cpp
  src/reference_scenes.cpp
  src/scene.cpp
  src/spectrum.cpp)
add_library(photonlab::field ALIAS photonlab_field)
target_include_directories(photonlab_field PUBLIC include)
target_link_libraries(photonlab_field PUBLIC photonlab::core)
photonlab_warnings(photonlab_field)
