cmake_minimum_required(VERSION 3.24)
set(CMAKE_CXX_STANDARD 20)
project(diy_coro LANGUAGES CXX)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  message(STATUS "Enabling ccache")
  list(APPEND CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
else()
  message(STATUS "ccache not found")
endif()

if(PROJECT_IS_TOP_LEVEL)
  add_compile_options(
    -Og
    -g3
    -ggdb
    -gcolumn-info
    -Wall
    -Werror
    -fno-omit-frame-pointer)
  add_link_options(-latomic)

  option(DIY_ENABLE_TSAN "Enable TSAN instrumentation." ON)
  if(DIY_ENABLE_TSAN)
    message(STATUS "TSAN enabled")
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
  else()
    message(STATUS "TSAN disabled")
  endif()

  include(FetchContent)

  set(ABSL_PROPAGATE_CXX_STD ON)
  FetchContent_Declare(
    absl
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 522606b7fae37836c138e83f6eec0eabb9947dc0
    GIT_PROGRESS ON)

  set(BENCHMARK_ENABLE_TESTING OFF)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.7.1)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
    GIT_PROGRESS ON
    GIT_SHALLOW ON)

  FetchContent_MakeAvailable(absl benchmark googletest)

  include(GoogleTest)
  enable_testing()

  add_custom_target(
    format_cc
    COMMAND find ${PROJECT_SOURCE_DIR} -type f | grep -v ${PROJECT_BINARY_DIR} |
            grep -E "[.](h|cc)$" | xargs -L1 clang-format -i
    VERBATIM)
  add_custom_target(
    format_cmake
    COMMAND find ${PROJECT_SOURCE_DIR} -name CMakeLists.txt | grep -v
            ${PROJECT_BINARY_DIR} | xargs -L1 cmake-format -i
    VERBATIM)
  add_custom_target(format DEPENDS format_cc format_cmake)
else()
  set(BUILD_TESTING OFF)
endif()

add_subdirectory(src)
