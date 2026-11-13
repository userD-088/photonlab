add_library(photonlab_io
  src/project.cpp)
add_library(photonlab::io ALIAS photonlab_io)
target_include_directories(photonlab_io PUBLIC include)
target_link_libraries(photonlab_io
  PUBLIC photonlab::circuit photonlab::logic photonlab::field
  PRIVATE nlohmann_json::nlohmann_json)
photonlab_warnings(photonlab_io)
