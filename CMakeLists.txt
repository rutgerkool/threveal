cmake_minimum_required(VERSION 3.25)

# Auto-detect vcpkg if VCPKG_ROOT is not set
if(NOT DEFINED ENV{VCPKG_ROOT})
  if(EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(ENV{VCPKG_ROOT} "$ENV{HOME}/vcpkg")
    message(STATUS "Auto-detected VCPKG_ROOT: $ENV{HOME}/vcpkg")
  endif()
endif()

project(
  threveal
  VERSION 0.1.0
  DESCRIPTION "Hybrid core migration profiler for Intel Alder Lake/Raptor Lake CPUs"
  LANGUAGES CXX
)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Use cmake --preset <preset>")
endif()

# C++23 requirement
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy and IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project options
option(THREVEAL_ENABLE_TESTING "Enable unit tests" ON)
option(THREVEAL_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

# Compiler warnings
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Werror=return-type
  -Wconversion
  -Wsign-conversion
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Woverloaded-virtual
)

# Sanitizers (debug builds)
if(THREVEAL_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Find dependencies
find_package(fmt CONFIG REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Core library
add_library(threveal_core STATIC
  src/core/topology.cpp
  src/core/events.cpp
  src/analysis/event_store.cpp
)
target_link_libraries(threveal_core PUBLIC fmt::fmt)

# Testing
if(THREVEAL_ENABLE_TESTING)
  enable_testing()
  find_package(Catch2 3 CONFIG REQUIRED)

  add_executable(test_topology
    tests/unit/test_topology.cpp
  )
  target_link_libraries(test_topology PRIVATE
    threveal_core
    Catch2::Catch2WithMain
  )

  add_executable(test_events
    tests/unit/test_events.cpp
  )
  target_link_libraries(test_events PRIVATE
    threveal_core
    Catch2::Catch2WithMain
  )

  add_executable(test_event_store
    tests/unit/test_event_store.cpp
  )
  target_link_libraries(test_event_store PRIVATE
    threveal_core
    Catch2::Catch2WithMain
  )

  add_test(NAME topology_tests COMMAND test_topology)
  add_test(NAME events_tests COMMAND test_events)
  add_test(NAME event_store_tests COMMAND test_event_store)

  # Configure AddressSanitizer to work correctly with ctest
  if(THREVEAL_ENABLE_SANITIZERS)
    set_tests_properties(topology_tests events_tests event_store_tests PROPERTIES
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0"
    )
  endif()
endif()
