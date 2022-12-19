diy_cc_library(
  test_main STATIC
  SOURCES test_main.cc
  LIBRARIES gtest absl::failure_signal_handler)

diy_cc_library(handle AUTO)

diy_cc_library(task AUTO)
diy_cc_test(task_test AUTO)

diy_cc_library(generator AUTO)
diy_cc_test(generator_test AUTO)

diy_cc_library(async_generator AUTO LIBRARIES handle task generator)
diy_cc_test(async_generator_test AUTO)

diy_cc_library(executor AUTO LIBRARIES absl::synchronization)
diy_cc_test(executor_test AUTO task)
