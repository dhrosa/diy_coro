#pragma once

#include <absl/synchronization/mutex.h>

#include <coroutine>
#include <utility>

// Single-producer single-consumer single-shot event.
class Event {
 public:
  // Resumes waiting coroutine, if any. Otherwise, causes the next waiter to
  // resume instantly.
  void Notify();

  // Awaitable that waits for a call to Notify().
  auto operator co_await();

 private:
  absl::Mutex mutex_;
  bool notified_ ABSL_GUARDED_BY(mutex_) = false;
  std::coroutine_handle<> waiter_ ABSL_GUARDED_BY(mutex_);
};

inline void Event::Notify() {
  std::coroutine_handle<> waiter;
  {
    absl::MutexLock lock(&mutex_);
    notified_ = true;
    std::swap(waiter_, waiter);
  }
  if (waiter) {
    waiter.resume();
  }
}

inline auto Event::operator co_await() {
  struct Waiter {
    Event& event;

    bool await_ready() {
      absl::MutexLock lock(&event.mutex_);
      return event.notified_;
    }

    bool await_suspend(std::coroutine_handle<> handle) {
      absl::MutexLock lock(&event.mutex_);
      if (event.notified_) {
        return false;
      }
      event.waiter_ = handle;
      return true;
    }

    void await_resume() {}
  };

  return Waiter{*this};
}
