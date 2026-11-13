add_library(photonlab_modes
  src/cross_section.cpp
  src/fde.cpp
  src/profile.cpp
  src/slab.cpp)
add_library(photonlab::modes ALIAS photonlab_modes)
target_include_directories(photonlab_modes PUBLIC include)
target_link_libraries(photonlab_modes PUBLIC photonlab::core photonlab::materials Eigen3::Eigen)
photonlab_warnings(photonlab_modes)
