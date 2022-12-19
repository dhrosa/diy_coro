#pragma once

#include <absl/synchronization/mutex.h>

#include <coroutine>
#include <stop_token>
#include <thread>
#include <vector>

class SerialExecutor {
 public:
  class ScheduleAwaiter;

  std::jthread Run();

  ScheduleAwaiter Schedule();

 private:
  void AwaitSuspend(std::coroutine_handle<> pending);

  absl::Mutex mutex_;
  std::coroutine_handle<> pending_ ABSL_GUARDED_BY(mutex_);
  bool stop_requested_ ABSL_GUARDED_BY(mutex_) = false;
};

class SerialExecutor::ScheduleAwaiter {
 public:
  constexpr bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> pending) {
    executor_->AwaitSuspend(pending);
  }

  constexpr void await_resume() {}

 private:
  friend class SerialExecutor;
  explicit ScheduleAwaiter(SerialExecutor* executor) : executor_(executor) {}
  SerialExecutor* const executor_;
};
