diy_cc_library(
  test_main STATIC
  SOURCES test_main.cc
  LIBRARIES gtest absl::failure_signal_handler)

# Copy header files into build tree to allow importing from "diy/coro/" without
# importing an awkward directory structure onto this project.
file(
  GLOB_RECURSE header_files
  RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
  "*.h")
foreach(header_file ${header_files})
  configure_file(
    ${header_file}
    "${CMAKE_CURRENT_BINARY_DIR}/include/diy/coro/${header_file}" COPYONLY)
endforeach()

include_directories("${CMAKE_CURRENT_BINARY_DIR}/include")

diy_cc_library(traits AUTO)
diy_cc_test(traits_test AUTO)

diy_cc_library(handle AUTO)

diy_cc_library(task AUTO)
diy_cc_test(task_test AUTO)

diy_cc_library(generator AUTO)
diy_cc_test(generator_test AUTO)

diy_cc_library(async_generator AUTO LIBRARIES handle task generator)
diy_cc_test(async_generator_test AUTO)

diy_cc_library(executor AUTO LIBRARIES absl::synchronization)
diy_cc_test(executor_test AUTO task)
