#include "executor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "task.h"

TEST(ExecutorTest, ThreadIdMatches) {
  SerialExecutor executor;
  auto task = [](SerialExecutor& executor) -> Task<std::thread::id> {
    co_await executor.Schedule();
    co_return std::this_thread::get_id();
  };

  std::jthread thread = executor.Run();
  EXPECT_EQ(task(executor).Wait(), thread.get_id());
}
