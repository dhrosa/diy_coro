#include "diy/coro/broadcast.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "diy/coro/task.h"

using testing::ElementsAre;
using testing::Eq;
using testing::Optional;
using testing::Pointee;

AsyncGenerator<int> IotaPublisher() {
  for (int i = 0;; ++i) {
    co_yield i;
  }
}

std::optional<int> NextValue(AsyncGenerator<const int>& generator) {
  const int* value = Task(generator).Wait();
  if (value == nullptr) {
    return std::nullopt;
  }
  return *value;
}

TEST(BroadcastTest, NoSubscribers) {
  Broadcast<int> broadcast(IotaPublisher());
}

TEST(BroadcastTest, SingleSubscriber) {
  Broadcast<int> broadcast(IotaPublisher());

  auto s = broadcast.Subscribe();

  EXPECT_THAT(NextValue(s), Optional(0));
  EXPECT_THAT(NextValue(s), Optional(1));
  EXPECT_THAT(NextValue(s), Optional(2));
}

TEST(BroadcastTest, MultipleSubscribers) {
  Broadcast<int> broadcast(IotaPublisher());

  auto a = broadcast.Subscribe();
  auto b = broadcast.Subscribe();
  auto c = broadcast.Subscribe();

  EXPECT_THAT(NextValue(a), Optional(0));
  EXPECT_THAT(NextValue(b), Optional(0));
  EXPECT_THAT(NextValue(c), Optional(0));

  // Try different interleavings of subscriber pulls to make sure there isn't an
  // accidental depedency on a specific ordering.
  EXPECT_THAT(NextValue(c), Optional(1));
  EXPECT_THAT(NextValue(b), Optional(1));
  EXPECT_THAT(NextValue(a), Optional(1));

  EXPECT_THAT(NextValue(b), Optional(2));
  EXPECT_THAT(NextValue(c), Optional(2));
  EXPECT_THAT(NextValue(a), Optional(2));
}

TEST(BroadcastTest, SingleSubscriberForwardsException) {
  Broadcast<int> broadcast([]() -> AsyncGenerator<int> {
    co_yield 1;
    throw std::logic_error("fake error");
  }());

  auto s = broadcast.Subscribe();

  EXPECT_THAT(NextValue(s), Optional(1));
  EXPECT_THROW(NextValue(s), std::logic_error);
}

std::vector<int> ToVector(AsyncGenerator<const int>& gen) {
  return [](AsyncGenerator<const int>& gen) -> Task<std::vector<int>> {
    std::vector<int> out;
    while (const int* value = co_await gen) {
      out.push_back(*value);
    }
    co_return out;
  }(gen)
                                                   .Wait();
}

TEST(BroadcastTest, SingleSubscriberFinite) {
  Broadcast<int> broadcast([]() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  }());

  auto s = broadcast.Subscribe();
  EXPECT_THAT(ToVector(s), ElementsAre(1, 2, 3));
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

  EXPECT_THAT(ToVector(a), ElementsAre(1, 2, 3));
  EXPECT_THAT(ToVector(b), ElementsAre(1, 2, 3));
  EXPECT_THAT(ToVector(c), ElementsAre(1, 2, 3));
}
