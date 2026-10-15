add_executable(cruth_tests
  main.cpp
  test_mesh.cpp
  test_sculpt.cpp
  test_topology.cpp
  test_texture.cpp
  test_retopo.cpp
  c_header_check.c
)
target_link_libraries(cruth_tests PRIVATE Cruth::core cruth_warnings)
set_target_properties(cruth_tests PROPERTIES CXX_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED ON)
add_test(NAME core COMMAND cruth_tests)

# End-to-end CLI chain on a generated asset: create, sculpt, subdivide, check.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(TARGET cruth-cli)
  add_test(NAME cli_chain
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:cruth-cli>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/data/head_blockout.stroke
      -DRETOPO_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/data/retopo_capsule.stroke
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_chain
      -DPYTHON=$<$<BOOL:${Python3_FOUND}>:${Python3_EXECUTABLE}>
      -DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/tools/check_images.py
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_chain.cmake)
endif()
