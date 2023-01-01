#pragma once

#include <absl/synchronization/mutex.h>
#include <absl/time/time.h>

#include <coroutine>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

#include "diy/coro/task.h"

// Allows transferring a coroutine to a different thread than the caller.
class SerialExecutor {
 public:
  SerialExecutor();
  ~SerialExecutor();

  // Awaitable that resumes execution of the current coroutine on this executor.
  auto Schedule();
  // Awaitable that resumes execution of the current coroutine on this executor
  // after the given time has passed.
  auto Sleep(absl::Time time);

 private:
  struct SharedState;

  bool AwaitReady() const;
  void AwaitSuspend(std::coroutine_handle<> pending);

  // We use a shared_ptr so that we can asynchronously stop our thread when the
  // executor is destructed.
  std::shared_ptr<SharedState> state_;
  std::jthread thread_;
};

inline auto SerialExecutor::Schedule() {
  struct Awaiter {
    SerialExecutor* executor;

    bool await_ready() { return executor->AwaitReady(); }

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
