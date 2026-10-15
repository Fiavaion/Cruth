add_executable(cruth-cli main.cpp)
target_link_libraries(cruth-cli PRIVATE Cruth::core cruth_warnings)
set_target_properties(cruth-cli PROPERTIES CXX_EXTENSIONS OFF)
