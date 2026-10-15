add_library(cruth_core STATIC
  src/bake.cpp
  src/bvh.cpp
  src/c_api.cpp
  src/curve.cpp
  src/decimate.cpp
  src/document.cpp
  src/half_edge.cpp
  src/image_io.cpp
  src/mesh.cpp
  src/mesh_io.cpp
  src/multires.cpp
  src/primitives.cpp
  src/print_check.cpp
  src/quad_remesh.cpp
  src/raycast.cpp
  src/reproject.cpp
  src/retopo.cpp
  src/sculpt.cpp
  src/subdivide.cpp
  src/uv.cpp
  src/voxel_remesh.cpp
)
add_library(Cruth::core ALIAS cruth_core)

target_include_directories(cruth_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(cruth_core PUBLIC cxx_std_20)
set_target_properties(cruth_core PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(cruth_core PUBLIC Threads::Threads PRIVATE cruth_warnings cruth_quadriflow)
