#include "diy/coro/broadcast.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "diy/coro/task.h"

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

  EXPECT_THAT(NextValue(c), Optional(1));
  EXPECT_THAT(NextValue(b), Optional(1));
  EXPECT_THAT(NextValue(a), Optional(1));

  EXPECT_THAT(NextValue(b), Optional(2));
  EXPECT_THAT(NextValue(c), Optional(2));
  EXPECT_THAT(NextValue(a), Optional(2));
}
