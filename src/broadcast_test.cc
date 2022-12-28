#include "diy/coro/broadcast.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diy/coro/task.h"

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

  auto subscriber = broadcast.Subscribe();
}
