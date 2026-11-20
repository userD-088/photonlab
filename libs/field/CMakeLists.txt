add_library(photonlab_field
  src/fdtd.cpp
  src/fdtd3d.cpp
  src/gpu.cpp
  src/inverse_design.cpp
  src/waveguide3d.cpp
  src/photonic_crystal.cpp
  src/reference_scenes.cpp
  src/scene.cpp
  src/sparameters.cpp
  src/spectrum.cpp)
add_library(photonlab::field ALIAS photonlab_field)
target_include_directories(photonlab_field PUBLIC include)
target_link_libraries(photonlab_field PUBLIC photonlab::core photonlab::materials photonlab::modes)
photonlab_warnings(photonlab_field)
if(PHOTONLAB_OPENCL)
  target_compile_definitions(photonlab_field PRIVATE PHOTONLAB_HAVE_OPENCL CL_TARGET_OPENCL_VERSION=120)
  target_link_libraries(photonlab_field PRIVATE photonlab_opencl_headers ${CMAKE_DL_LIBS})
endif()
