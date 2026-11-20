add_executable(photonlab_tests
  main.cpp
  test_units.cpp
  test_active.cpp
  test_thread_pool.cpp
  test_interference.cpp
  test_inverse_design.cpp
  test_circuit_components.cpp
  test_circuit_monte_carlo.cpp
  test_circuit_nonlinear.cpp
  test_circuit_registry.cpp
  test_circuit_resonators.cpp
  test_circuit_solver.cpp
  test_circuit_sweep.cpp
  test_fdtd3d.cpp
  test_field_fdtd.cpp
  test_gpu.cpp
  test_layout.cpp
  test_io_project.cpp
  test_kerr_cavity.cpp
  test_logic.cpp
  test_modes.cpp
  test_nonlinear_field.cpp
  test_pcm.cpp
  test_plugin.cpp
  test_photonic_crystal.cpp
  test_phc_field.cpp
  test_pwe.cpp
  test_sparameters.cpp)
target_link_libraries(photonlab_tests PRIVATE photonlab::core photonlab::circuit photonlab::logic photonlab::field photonlab::modes photonlab::pwe photonlab::io photonlab::layout doctest::doctest)
target_compile_definitions(photonlab_tests PRIVATE
  PHOTONLAB_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
  PHOTONLAB_EXAMPLE_PLUGIN="$<TARGET_FILE:photonlab_example_plugin>")
add_dependencies(photonlab_tests photonlab_example_plugin)
photonlab_warnings(photonlab_tests)
add_test(NAME photonlab_tests COMMAND photonlab_tests)

# Validierung über die Kommandozeile
add_test(NAME cli_validate COMMAND photonlab_cli validate)
