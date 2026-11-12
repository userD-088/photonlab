add_executable(photonlab_cli main.cpp)
target_link_libraries(photonlab_cli PRIVATE photonlab::core)
photonlab_warnings(photonlab_cli)
