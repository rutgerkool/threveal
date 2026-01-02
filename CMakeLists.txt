cmake_minimum_required(VERSION 3.25)

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

# Core library
add_library(threveal_core
  src/core/topology.cpp
)

target_include_directories(threveal_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(threveal_core
  PUBLIC
    fmt::fmt
)

target_compile_features(threveal_core PUBLIC cxx_std_23)

# Testing
if(THREVEAL_ENABLE_TESTING)
  enable_testing()
  find_package(Catch2 3 CONFIG REQUIRED)
endif()
