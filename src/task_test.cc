#include "diy/coro/task.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(TaskTest, ReturnValue) {
  auto task = [](bool& called) -> Task<int> {
    called = true;
    co_return 4;
  };

  bool called = false;
  EXPECT_EQ(task(called).Wait(), 4);
  EXPECT_TRUE(called);
}

TEST(TaskTest, ReturnVoid) {
  auto task = [](bool& called) -> Task<> {
    called = true;
    co_return;
  };

  bool called = false;
  task(called).Wait();
  EXPECT_TRUE(called);
}

TEST(TaskTest, ChainValues) {
  auto task_a = []() -> Task<int> { co_return 1; };
  auto task_b = [](Task<int> a) -> Task<int> {
    int val_a = co_await std::move(a);
    co_return val_a + 2;
  };

  EXPECT_EQ(task_b(task_a()).Wait(), 3);
}

TEST(TaskTest, ChainVoid) {
  auto task_a = [](int& value) -> Task<> {
    value = 1;
    co_return;
  };
  auto task_b = [](int& value, Task<> a) -> Task<> {
    co_await std::move(a);
    value += 2;
    co_return;
  };

  int value = 0;
  task_b(value, task_a(value)).Wait();
  EXPECT_EQ(value, 3);
}

TEST(TaskTest, ValueToVoidConversion) {
  auto task = [](bool& called) -> Task<int> {
    called = true;
    co_return 3;
  };

  bool called = false;
  Task<>(task(called)).Wait();
  EXPECT_TRUE(called);
}

TEST(TaskTest, Map) {
  auto task = []() -> Task<int> { co_return 1; };
  EXPECT_EQ(task().Map([](int x) { return x + 2; }).Wait(), 3);
}

TEST(TaskTest, MapExtraArguments) {
  auto task = []() -> Task<int> { co_return 1; };
  EXPECT_EQ(task().Map([](int x, int y) { return x + y; }, 2).Wait(), 3);
}

TEST(TaskTest, ConversionFromAwaitable) {
  struct Awaitable : std::suspend_never {
    int await_resume() { return 3; }
  };

  auto task = Task(Awaitable());
  static_assert(std::same_as<decltype(task), Task<int>>);

  EXPECT_EQ(std::move(task).Wait(), 3);
}
