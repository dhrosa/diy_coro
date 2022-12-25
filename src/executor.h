#pragma once

#include <absl/synchronization/mutex.h>
#include <absl/time/time.h>

#include <coroutine>
#include <stop_token>
#include <thread>
#include <vector>

#include "diy/coro/task.h"

class SerialExecutor {
 public:
  std::jthread Run();

  // Awaitable that resumes execution of the current coroutine on this executor.
  auto Schedule();
  // Awaitable that resumes execution of the current coroutine on this executor
  // after the given time has passed.
  auto Sleep(absl::Time time);

 private:
  void AwaitSuspend(std::coroutine_handle<> pending);

  absl::Mutex mutex_;
  std::coroutine_handle<> pending_ ABSL_GUARDED_BY(mutex_);
  bool stop_requested_ ABSL_GUARDED_BY(mutex_) = false;
};

inline auto SerialExecutor::Schedule() {
  struct Awaiter {
    SerialExecutor* executor;

    constexpr bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> pending) {
      executor->AwaitSuspend(pending);
    }

    constexpr void await_resume() {}
  };
  return Awaiter{this};
};

inline auto SerialExecutor::Sleep(absl::Time time) {
  return [](SerialExecutor& executor, absl::Time time) -> Task<> {
    co_await executor.Schedule();
    absl::SleepFor(time - absl::Now());
  }(*this, time);
}
