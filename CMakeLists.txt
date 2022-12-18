cmake_minimum_required(VERSION 3.24)
set(CMAKE_CXX_STANDARD 20)
project(diy_coro LANGUAGES CXX)

add_compile_options(
  -O0
  -g3
  -ggdb
  -Wall
  -Werror
  -fno-omit-frame-pointer
  -fno-optimize-sibling-calls
  -fsanitize=address)
add_link_options(-fsanitize=address)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  message(STATUS "Enabling ccache")
  list(APPEND CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
else()
  message(STATUS "ccache not found")
endif()

include(FetchContent)

set(ABSL_PROPAGATE_CXX_STD ON)
FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG 522606b7fae37836c138e83f6eec0eabb9947dc0)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1)

FetchContent_MakeAvailable(absl googletest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Diy)

include(GoogleTest)
enable_testing()

add_subdirectory(src diy_coro)

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
