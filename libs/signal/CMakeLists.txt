add_library(photonlab_signal
  src/link.cpp)
add_library(photonlab::signal ALIAS photonlab_signal)
target_include_directories(photonlab_signal PUBLIC include)
target_link_libraries(photonlab_signal PUBLIC photonlab::core photonlab::circuit)
photonlab_warnings(photonlab_signal)
