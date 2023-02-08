#include "diy/coro/async_queue.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

#include "diy/coro/task.h"

using testing::ElementsAre;
using testing::Eq;
using testing::Pointee;

TEST(AsyncQueueTest, PushBeforePop) {
  AsyncQueue<int> queue;
  queue.Push(1);
  queue.Push(2);
  queue.Push(3);

  auto gen = queue.Values();

  EXPECT_THAT(gen.Wait(), Pointee(1));
  EXPECT_THAT(gen.Wait(), Pointee(2));
  EXPECT_THAT(gen.Wait(), Pointee(3));
}

TEST(AsyncQueueTest, ConcurrentPushPop) {
  AsyncQueue<int> queue;

  std::jthread pusher([&] {
    queue.Push(1);
    queue.Push(2);
    queue.Push(3);
  });

  auto gen = queue.Values();

  EXPECT_THAT(gen.Wait(), Pointee(1));
  EXPECT_THAT(gen.Wait(), Pointee(2));
  EXPECT_THAT(gen.Wait(), Pointee(3));

  pusher.join();
}
