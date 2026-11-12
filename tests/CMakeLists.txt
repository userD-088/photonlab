add_executable(photonlab_tests
  main.cpp
  test_units.cpp
  test_thread_pool.cpp
  test_interference.cpp)
target_link_libraries(photonlab_tests PRIVATE photonlab::core doctest::doctest)
photonlab_warnings(photonlab_tests)
add_test(NAME photonlab_tests COMMAND photonlab_tests)
