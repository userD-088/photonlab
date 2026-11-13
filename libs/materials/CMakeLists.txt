add_library(photonlab_materials
  src/material.cpp)
add_library(photonlab::materials ALIAS photonlab_materials)
target_include_directories(photonlab_materials PUBLIC include)
target_link_libraries(photonlab_materials PUBLIC photonlab::core)
photonlab_warnings(photonlab_materials)
