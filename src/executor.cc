#include "executor.h"

std::jthread SerialExecutor::Run() {
  return std::jthread([this](std::stop_token stop_token) {
    const auto on_stop = std::stop_callback(stop_token, [this] {
      absl::MutexLock lock(&mutex_);
      stop_requested_ = true;
    });

    const auto pending_or_stopping =
        [this]() ABSL_SHARED_LOCKS_REQUIRED(mutex_) -> bool {
      return pending_ || stop_requested_;
    };
    while (true) {
      std::coroutine_handle pending;
      {
        absl::MutexLock lock(&mutex_);
        mutex_.Await(absl::Condition(&pending_or_stopping));
        if (stop_requested_) {
          return;
        }
        std::swap(pending, pending_);
      }
      pending.resume();
    }
  });
}

auto SerialExecutor::Schedule() -> ScheduleAwaiter {
  return ScheduleAwaiter(this);
}

void SerialExecutor::AwaitSuspend(std::coroutine_handle<> pending) {
  const auto idle_or_stopping = [this]()
                                    ABSL_SHARED_LOCKS_REQUIRED(mutex_) -> bool {
    return pending_ == nullptr || stop_requested_;
  };

  absl::MutexLock lock(&mutex_);
  mutex_.Await(absl::Condition(&idle_or_stopping));
  pending_ = pending;
}
