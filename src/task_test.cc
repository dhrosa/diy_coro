#include "diy/coro/task.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

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

// Awaitable that transfers execution of the coroutine to the given thread.
auto TransferToThread(std::jthread& thread) {
  struct Awaiter {
    std::jthread& thread;

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      // We can't directly assign to this->thread because as soon as we
      // construct the new thread it will concurrently resume the handle,
      // causing a race on our member variables. Instead we create a local
      // variable which is only directly accessed by this scope.
      std::jthread* const pointer = &thread;
      *pointer = std::jthread(handle);
    }

    void await_resume() {}
  };

  return Awaiter{thread};
}

TEST(TaskTest, ThreadTransfer) {
  auto task = [](std::jthread& thread, bool& complete,
                 std::thread::id& task_thread_id) -> Task<> {
    co_await TransferToThread(thread);
    task_thread_id = std::this_thread::get_id();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(100ms);
    complete = true;
  };

  const std::thread::id caller_thread_id = std::this_thread::get_id();
  std::thread::id task_thread_id;
  std::jthread thread;
  bool complete = false;
  task(thread, complete, task_thread_id).Wait();
  EXPECT_TRUE(complete);

  EXPECT_NE(caller_thread_id, task_thread_id);
}

TEST(TaskTest, Map) {
  auto task = []() -> Task<int> { co_return 1; };
  EXPECT_EQ(task().Map([](int x) { return x + 2; }).Wait(), 3);
}

TEST(TaskTest, MapExtraArguments) {
  auto task = []() -> Task<int> { co_return 1; };
  EXPECT_EQ(task().Map([](int x, int y) { return x + y; }, 2).Wait(), 3);
}
