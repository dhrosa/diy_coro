#include <absl/debugging/failure_signal_handler.h>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
  absl::InstallFailureSignalHandler({});
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
