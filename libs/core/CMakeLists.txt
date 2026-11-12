add_library(photonlab_core
  src/parallel_for.cpp
  src/thread_pool.cpp
  src/units.cpp)
add_library(photonlab::core ALIAS photonlab_core)
target_include_directories(photonlab_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(photonlab_core PUBLIC Threads::Threads)
photonlab_warnings(photonlab_core)
