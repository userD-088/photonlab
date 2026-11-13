add_library(photonlab_logic
  src/gate.cpp
  src/optimize.cpp
  src/reference_gates.cpp
  src/report.cpp
  src/truth_table.cpp)
add_library(photonlab::logic ALIAS photonlab_logic)
target_include_directories(photonlab_logic PUBLIC include)
target_link_libraries(photonlab_logic PUBLIC photonlab::circuit)
photonlab_warnings(photonlab_logic)
