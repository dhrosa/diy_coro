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

diy_cc_library(resume AUTO)

diy_cc_library(task AUTO LIBRARIES traits)
diy_cc_test(task_test AUTO)

diy_cc_library(generator AUTO)
diy_cc_test(generator_test AUTO)

diy_cc_library(async_generator AUTO LIBRARIES handle generator)
diy_cc_test(async_generator_test AUTO LIBRARIES task generator)

diy_cc_library(executor AUTO LIBRARIES absl::synchronization absl::time)
diy_cc_test(executor_test AUTO LIBRARIES task)

diy_cc_library(intrusive_linked_list AUTO)
diy_cc_test(intrusive_linked_list_test AUTO)

diy_cc_library(broadcast AUTO LIBRARIES async_generator absl::synchronization absl::flat_hash_map absl::log)
diy_cc_test(broadcast_test AUTO LIBRARIES task)

diy_cc_library(event AUTO LIBRARIES absl::synchronization)
diy_cc_test(event_test AUTO LIBRARIES task)

# Top-level target for public consumption.
add_library(diy_coro INTERFACE)
target_link_libraries(diy_coro INTERFACE traits handle task generator
                                         async_generator executor)
target_include_directories(diy_coro
                           INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/include")
