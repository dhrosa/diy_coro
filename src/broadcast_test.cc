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
