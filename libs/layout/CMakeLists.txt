add_library(photonlab_layout
  src/circuit_layout.cpp
  src/gds.cpp
  src/layout.cpp)
add_library(photonlab::layout ALIAS photonlab_layout)
target_include_directories(photonlab_layout PUBLIC include)
target_link_libraries(photonlab_layout PUBLIC photonlab::core photonlab::field photonlab::circuit)
photonlab_warnings(photonlab_layout)
