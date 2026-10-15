add_executable(cruth-acceptance main.cpp)
target_link_libraries(cruth-acceptance PRIVATE Cruth::core cruth_warnings)
set_target_properties(cruth-acceptance PROPERTIES CXX_EXTENSIONS OFF)
