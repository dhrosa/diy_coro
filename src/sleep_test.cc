#include "diy/coro/sleep.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

#include "diy/coro/task.h"

TEST(SleepTest, TimeAlreadyElapsed) {
  auto awaitable = Sleep(absl::Now() - absl::Seconds(1));
  EXPECT_TRUE(awaitable.await_ready());
}

TEST(SleepTest, SleepsForCorrectDuration) {
  auto task = []() -> Task<absl::Duration> {
    const absl::Time start = absl::Now();
    co_await Sleep(start + absl::Milliseconds(100));
    co_return absl::Now() - start;
  };

  const absl::Duration elapsed = task().Wait();
  EXPECT_GE(elapsed, absl::Milliseconds(100));
  EXPECT_LE(elapsed, absl::Milliseconds(200));
}

TEST(SleepTest, ResumesOnDifferentThread) {
  const auto task = []() -> Task<std::thread::id> {
    co_await Sleep(absl::Milliseconds(100));
    co_return std::this_thread::get_id();
  };

  EXPECT_NE(task().Wait(), std::this_thread::get_id());
}
