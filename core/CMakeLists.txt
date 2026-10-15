add_library(cruth_core STATIC
  src/c_api.cpp
  src/curve.cpp
  src/document.cpp
  src/half_edge.cpp
  src/mesh.cpp
  src/mesh_io.cpp
  src/primitives.cpp
  src/print_check.cpp
  src/raycast.cpp
  src/sculpt.cpp
  src/subdivide.cpp
)
add_library(Cruth::core ALIAS cruth_core)

target_include_directories(cruth_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(cruth_core PUBLIC cxx_std_20)
set_target_properties(cruth_core PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_link_libraries(cruth_core PRIVATE cruth_warnings)
