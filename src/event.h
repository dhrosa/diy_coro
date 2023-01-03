#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>

// Single-producer single-consumer single-shot event.
class Event {
 public:
  Event();

  // Resumes waiting coroutine, if any. Otherwise, causes the next waiter to
  // resume instantly.
  void Notify();

  // Awaitable that waits for a call to Notify().
  auto operator co_await();

 private:
  enum : int { kCleared, kWaiting, kSet };
  std::atomic_int state_;
  std::coroutine_handle<> waiting_;
};

inline Event::Event() { state_.store(kCleared, std::memory_order::relaxed); }

inline void Event::Notify() {
  std::uintptr_t previous_state =
      state_.exchange(kSet, std::memory_order::acq_rel);
  assert(previous_state != kSet);
  if (previous_state == kCleared) {
    return;
  }
  assert(previous_state == kWaiting);
  waiting_.resume();
}

inline auto Event::operator co_await() {
  struct Waiter {
    Event& event;

    // Cached value of the load in await_ready() to use inside await_suspend().
    int previous_state;

    bool await_ready() {
      previous_state = event.state_.load(std::memory_order::acquire);
      return previous_state == kSet;
    }

    bool await_suspend(std::coroutine_handle<> handle) {
      assert(previous_state == kCleared);
      event.waiting_ = handle;
      if (event.state_.compare_exchange_strong(previous_state, kWaiting,
                                               std::memory_order::acq_rel)) {
        return true;
      }
      // Racing kCleared -> kSet transition. We should resume ourselves.
      assert(previous_state == kSet);
      return false;
    }

    void await_resume() {}
  };

  return Waiter{*this};
}
