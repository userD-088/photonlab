add_executable(photonlab_cli main.cpp)
target_link_libraries(photonlab_cli PRIVATE photonlab::core photonlab::circuit photonlab::logic photonlab::field photonlab::io)
photonlab_warnings(photonlab_cli)
