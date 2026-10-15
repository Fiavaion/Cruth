# Vendored third-party code. Built without our warning flags; licences are in
# each directory and listed in spec/dependencies.md.

# QuadriFlow (BSD-3-Clause) with lemon (Boost Software License) and Eigen
# (MPL-2.0 only). See quadriflow/CRUTH_PATCHES.md for local changes.
add_library(cruth_quadriflow STATIC
  quadriflow/src/adjacent-matrix.cpp
  quadriflow/src/dedge.cpp
  quadriflow/src/hierarchy.cpp
  quadriflow/src/loader.cpp
  quadriflow/src/localsat.cpp
  quadriflow/src/merge-vertex.cpp
  quadriflow/src/optimizer.cpp
  quadriflow/src/parametrizer.cpp
  quadriflow/src/parametrizer-flip.cpp
  quadriflow/src/parametrizer-int.cpp
  quadriflow/src/parametrizer-mesh.cpp
  quadriflow/src/parametrizer-scale.cpp
  quadriflow/src/parametrizer-sing.cpp
  quadriflow/src/subdivide.cpp
)
target_include_directories(cruth_quadriflow SYSTEM PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/quadriflow/src
  ${CMAKE_CURRENT_SOURCE_DIR}/quadriflow/3rd
  ${CMAKE_CURRENT_SOURCE_DIR}/eigen
  ${CMAKE_CURRENT_SOURCE_DIR}/lemon)
target_compile_definitions(cruth_quadriflow PUBLIC EIGEN_MPL2_ONLY QUADRIFLOW_NO_EXTERNAL_SAT)
target_compile_features(cruth_quadriflow PUBLIC cxx_std_17)
set_target_properties(cruth_quadriflow PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
  target_compile_options(cruth_quadriflow PRIVATE -w)
endif()

# MikkTSpace reference tangent generator (zlib licence), plain C, unmodified.
add_library(cruth_mikktspace STATIC mikktspace/mikktspace.c)
target_include_directories(cruth_mikktspace SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mikktspace)
set_target_properties(cruth_mikktspace PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(cruth_mikktspace PRIVATE -w)
  if(CRUTH_SANITIZE)
    # The unmodified upstream shifts a 32-bit seed by 32 when its rotation is 0
    # (mikktspace.c, QuickSort): undefined in C, harmless on every target we ship.
    target_compile_options(cruth_mikktspace PRIVATE -fno-sanitize=shift)
  endif()
endif()
