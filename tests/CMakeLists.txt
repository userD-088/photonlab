add_executable(photonlab_tests
  main.cpp
  test_units.cpp
  test_thread_pool.cpp
  test_interference.cpp
  test_circuit_components.cpp
  test_circuit_monte_carlo.cpp
  test_circuit_registry.cpp
  test_circuit_resonators.cpp
  test_circuit_solver.cpp
  test_circuit_sweep.cpp
  test_io_project.cpp)
target_link_libraries(photonlab_tests PRIVATE photonlab::core photonlab::circuit photonlab::io doctest::doctest)
target_compile_definitions(photonlab_tests PRIVATE
  PHOTONLAB_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
photonlab_warnings(photonlab_tests)
add_test(NAME photonlab_tests COMMAND photonlab_tests)

# Validierung über die Kommandozeile (Bauplan Abschnitt 16)
add_test(NAME cli_validate COMMAND photonlab_cli validate)
