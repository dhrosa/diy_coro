#pragma once

#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <coroutine>
#include <thread>

// An awaitable that suspends the current coroutine until at least the given
// `time`, at which point the coroutine is resumed (possibly on on another
// thread). This spawns a new thread for each sleep, so this is inefficient for
// short duration sleeps.
auto Sleep(absl::Time time) {
  struct Awaiter {
    absl::Time time;

    bool await_ready() const { return time < absl::Now(); }

    void await_suspend(std::coroutine_handle<> handle) {
      std::thread([handle, time = time] {
        absl::SleepFor(time - absl::Now());
        handle.resume();
      }).detach();
    }

    void await_resume() {}
  };
  return Awaiter{time};
}

// Convenience overload for fixed-duration sleeps.
auto Sleep(absl::Duration duration) { return Sleep(absl::Now() + duration); }
