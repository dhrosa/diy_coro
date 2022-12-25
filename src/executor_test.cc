#include "diy/coro/executor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diy/coro/task.h"

TEST(ExecutorTest, ThreadIdMatches) {
  auto task = [](SerialExecutor& executor) -> Task<std::thread::id> {
    co_await executor.Schedule();
    co_return std::this_thread::get_id();
  };

  SerialExecutor executor;
  std::jthread thread = executor.Run();
  EXPECT_EQ(task(executor).Wait(), thread.get_id());
}

// It should be possible for a child coroutine to schedule onto the same
// executor that the parent is running on.
TEST(ExecutorTest, RecursiveScheduling) {
  auto task = [](SerialExecutor& executor, bool& complete) -> Task<> {
    co_await executor.Schedule();
    auto sub_task = [](SerialExecutor& executor, bool& complete) -> Task<> {
      co_await executor.Schedule();
      complete = true;
    };
    co_await sub_task(executor, complete);
  };

  SerialExecutor executor;
  bool complete = false;
  std::jthread thread = executor.Run();
  task(executor, complete).Wait();
  EXPECT_TRUE(complete);
}

TEST(ExecutorTest, SleepsForCorrectDuration) {
  auto task = [](SerialExecutor& executor) -> Task<absl::Duration> {
    co_await executor.Schedule();
    const absl::Time start = absl::Now();
    co_await executor.Sleep(start + absl::Milliseconds(100));
    co_return absl::Now() - start;
  };

  SerialExecutor executor;
  std::jthread thread = executor.Run();
  const absl::Duration elapsed = task(executor).Wait();
  EXPECT_GE(elapsed, absl::Milliseconds(100));
  EXPECT_LE(elapsed, absl::Milliseconds(200));
}
