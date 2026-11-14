add_library(photonlab_quantum
  src/fock.cpp
  src/experiments.cpp
  src/mesh.cpp)
add_library(photonlab::quantum ALIAS photonlab_quantum)
target_include_directories(photonlab_quantum PUBLIC include)
target_link_libraries(photonlab_quantum PUBLIC photonlab::core photonlab::circuit Eigen3::Eigen)
photonlab_warnings(photonlab_quantum)
