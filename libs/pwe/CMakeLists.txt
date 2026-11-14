add_library(photonlab_pwe
  src/pwe.cpp)
add_library(photonlab::pwe ALIAS photonlab_pwe)
target_include_directories(photonlab_pwe PUBLIC include)
target_link_libraries(photonlab_pwe PUBLIC photonlab::core Eigen3::Eigen)
photonlab_warnings(photonlab_pwe)
