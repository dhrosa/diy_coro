#include "diy/coro/broadcast.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <thread>

#include "diy/coro/container_generator.h"
#include "diy/coro/task.h"

using testing::ElementsAre;
using testing::Eq;
using testing::Pointee;

AsyncGenerator<int> IotaPublisher() {
  for (int i = 0;; ++i) {
    co_yield i;
  }
}

TEST(BroadcastTest, NoSubscribers) {
  Broadcast<int> broadcast(IotaPublisher());
}

TEST(BroadcastTest, SingleSubscriber) {
  Broadcast<int> broadcast(IotaPublisher());

  auto s = broadcast.Subscribe();

  EXPECT_THAT(s.Wait(), Pointee(0));
  EXPECT_THAT(s.Wait(), Pointee(1));
  EXPECT_THAT(s.Wait(), Pointee(2));
}

TEST(BroadcastTest, MultipleSubscribers) {
  Broadcast<int> broadcast(IotaPublisher());

  auto a = broadcast.Subscribe();
  auto b = broadcast.Subscribe();
  auto c = broadcast.Subscribe();

  EXPECT_THAT(a.Wait(), Pointee(0));
  EXPECT_THAT(b.Wait(), Pointee(0));
  EXPECT_THAT(c.Wait(), Pointee(0));

  // Try different interleavings of subscriber pulls to make sure there isn't an
  // accidental depedency on a specific ordering.
  EXPECT_THAT(c.Wait(), Pointee(1));
  EXPECT_THAT(b.Wait(), Pointee(1));
  EXPECT_THAT(a.Wait(), Pointee(1));

  EXPECT_THAT(b.Wait(), Pointee(2));
  EXPECT_THAT(c.Wait(), Pointee(2));
  EXPECT_THAT(a.Wait(), Pointee(2));
}

TEST(BroadcastTest, SingleSubscriberForwardsException) {
  Broadcast<int> broadcast([]() -> AsyncGenerator<int> {
    co_yield 1;
    throw std::logic_error("fake error");
  }());

  auto s = broadcast.Subscribe();

  EXPECT_THAT(s.Wait(), Pointee(1));
  EXPECT_THROW(s.Wait(), std::logic_error);
}

TEST(BroadcastTest, SingleSubscriberFinite) {
  Broadcast<int> broadcast([]() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  }());

  auto s = broadcast.Subscribe();
  EXPECT_THAT(s.ToVector(), ElementsAre(1, 2, 3));
}

TEST(BroadcastTest, MultipleSubscriberFinite) {
  Broadcast<int> broadcast([]() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  }());

  auto a = broadcast.Subscribe();
  auto b = broadcast.Subscribe();
  auto c = broadcast.Subscribe();

  EXPECT_THAT(a.ToVector(), ElementsAre(1, 2, 3));
  EXPECT_THAT(b.ToVector(), ElementsAre(1, 2, 3));
  EXPECT_THAT(c.ToVector(), ElementsAre(1, 2, 3));
}

TEST(BroadcastTest, Threaded) {
  Broadcast<int> broadcast([]() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  }());

  auto a = broadcast.Subscribe();
  auto b = broadcast.Subscribe();

  auto subscriber_thread = [](AsyncGenerator<const int> gen) {
    EXPECT_THAT(gen.ToVector(), ElementsAre(1, 2, 3));
  };

  std::jthread thread_a(subscriber_thread, std::move(a));
  std::jthread thread_b(subscriber_thread, std::move(b));

  thread_a.join();
  thread_b.join();
}
