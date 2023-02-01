# Public library target.
add_library(diy_coro STATIC)

set(headers
    async_generator.h
    async_queue.h
    broadcast.h
    container_generator.h
    event.h
    executor.h
    generator.h
    handle.h
    task.h
    traits.h)

set(sources executor.cc)

set(tests
    async_generator_test.cc
    async_queue_test.cc
    broadcast_test.cc
    container_generator_test.cc
    event_test.cc
    executor_test.cc
    generator_test.cc
    task_test.cc
    traits_test.cc)

set(benchmarks async_generator_benchmark.cc task_benchmark.cc)

# Copy header files into build tree to allow importing from "diy/coro/" without
# importing an awkward directory structure onto this project. This also lets
# both internal implementation and downstream code to use the same include
# syntax.
foreach(header ${headers})
  configure_file(
    ${header} "${CMAKE_CURRENT_BINARY_DIR}/include/diy/coro/${header}" COPYONLY)
endforeach()
target_include_directories(diy_coro
                           PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
target_sources(diy_coro PRIVATE ${sources})
target_link_libraries(diy_coro absl::synchronization)

# Tests
include(GoogleTest)
foreach(test ${tests})
  cmake_path(GET test STEM target_name)
  add_executable(${target_name} ${test} test_main.cc)
  target_link_libraries(${target_name} PRIVATE diy_coro gmock gtest
                                               absl::failure_signal_handler)
  gtest_discover_tests(${target_name})
endforeach()

# Benchmarks
foreach(benchmark ${benchmarks})
  cmake_path(GET benchmark STEM target_name)
  add_executable(${target_name} ${benchmark})
  target_link_libraries(${target_name} PRIVATE diy_coro benchmark::benchmark
                                               benchmark::benchmark_main)
endforeach()
