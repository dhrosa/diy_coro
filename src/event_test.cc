#include "diy/coro/event.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <latch>
#include <thread>

#include "diy/coro/task.h"

TEST(EventTest, CoroutinePausedUntilNotify) {
  Event event;
  std::latch about_to_wait{1};
  std::latch task_done{1};

  auto task = [](Event& event, std::latch& about_to_wait) -> Task<> {
    about_to_wait.count_down();
    co_await event;
  }(event, about_to_wait);
  std::jthread thread([&] {
    std::move(task).Wait();
    task_done.count_down();
  });

  about_to_wait.wait();
  EXPECT_FALSE(task_done.try_wait());
  event.Notify();
  thread.join();
}
