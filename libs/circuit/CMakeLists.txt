add_library(photonlab_circuit
  src/analysis.cpp
  src/builtin_components.cpp
  src/monte_carlo.cpp
  src/netlist.cpp
  src/reference_designs.cpp
  src/registry.cpp
  src/solver.cpp
  src/sweep.cpp)
add_library(photonlab::circuit ALIAS photonlab_circuit)
target_include_directories(photonlab_circuit PUBLIC include)
target_link_libraries(photonlab_circuit PUBLIC photonlab::core Eigen3::Eigen)
photonlab_warnings(photonlab_circuit)
