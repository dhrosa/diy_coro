#include "diy/coro/broadcast.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "diy/coro/task.h"

using testing::Eq;
using testing::Pointee;

AsyncGenerator<int> IotaPublisher() {
  for (int i = 0;; ++i) {
    co_yield i;
  }
}

std::optional<int> NextValue(AsyncGenerator<int>& generator) {
  int* value = Task(generator).Wait();
  if (value == nullptr) {
    return std::nullopt;
  }
  return *value;
}

TEST(BroadcastTest, NoSubscribers) {
  Broadcast<int> broadcast(IotaPublisher());
  broadcast.Publish(1).Wait();
}

TEST(BroadcastTest, SingleSubscriber) {
  Broadcast<int> broadcast(IotaPublisher());

  auto subscriber = broadcast.Subscribe();
  subscriber.Resume();
  // Subscriber now waiting for publisher.

  auto publisher = broadcast.Publish(3);
  publisher.Resume();
  // Publisher now waiting for subscriber to consume value.

  subscriber.Resume();
  EXPECT_THAT(Task(subscriber).Wait(), Pointee(Eq(3)));
}
