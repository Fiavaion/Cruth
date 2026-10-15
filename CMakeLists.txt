cmake_minimum_required(VERSION 3.21)
project(Cruth VERSION 0.1.0 LANGUAGES C CXX)

option(CRUTH_BUILD_TESTS "Build the core test suite" ON)
option(CRUTH_BUILD_CLI "Build the headless cruth-cli tool" ON)
option(CRUTH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CRUTH_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(cruth_warnings INTERFACE)
if(MSVC)
  target_compile_options(cruth_warnings INTERFACE /W4 $<$<BOOL:${CRUTH_WARNINGS_AS_ERRORS}>:/WX>)
else()
  target_compile_options(cruth_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow
    $<$<BOOL:${CRUTH_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

if(CRUTH_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core)

if(CRUTH_BUILD_CLI)
  add_subdirectory(tools/cruth-cli)
endif()

if(CRUTH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
