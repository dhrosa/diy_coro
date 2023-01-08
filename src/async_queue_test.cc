#include "diy/coro/async_queue.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

#include "diy/coro/task.h"

using testing::ElementsAre;
using testing::Eq;
using testing::Pointee;

template <typename T>
T* NextValue(AsyncGenerator<T>& gen) {
  return Task(gen).Wait();
}

TEST(AsyncQueueTest, PushBeforePop) {
  AsyncQueue<int> queue;
  queue.Push(1);
  queue.Push(2);
  queue.Push(3);

  auto gen = queue.Values();

  EXPECT_THAT(NextValue(gen), Pointee(1));
  EXPECT_THAT(NextValue(gen), Pointee(2));
  EXPECT_THAT(NextValue(gen), Pointee(3));
}

TEST(AsyncQueueTest, ConcurrentPushPop) {
  AsyncQueue<int> queue;

  std::jthread pusher([&] {
    queue.Push(1);
    queue.Push(2);
    queue.Push(3);
  });

  auto gen = queue.Values();

  EXPECT_THAT(NextValue(gen), Pointee(1));
  EXPECT_THAT(NextValue(gen), Pointee(2));
  EXPECT_THAT(NextValue(gen), Pointee(3));

  pusher.join();
}
