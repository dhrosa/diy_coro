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
  // `state_' contains either a waiting coroutine address, or one of the special
  // values below.
  enum : std::uintptr_t {
    kCleared = 1,
    kSet = 2,
  };
  std::atomic_uintptr_t state_;
};

inline Event::Event() { state_.store(kCleared, std::memory_order::relaxed); }

inline void Event::Notify() {
  std::uintptr_t previous_state =
      state_.exchange(kSet, std::memory_order::acq_rel);
  assert(previous_state != kSet);
  if (previous_state == kCleared) {
    return;
  }
  // Resume the waiter.
  std::coroutine_handle<>::from_address(reinterpret_cast<void*>(previous_state))
      .resume();
}

inline auto Event::operator co_await() {
  struct Waiter {
    std::atomic_uintptr_t& state;

    // Cached value of th load in await_ready() to use inside await_suspend().
    std::uintptr_t previous_state;

    bool await_ready() {
      previous_state = state.load(std::memory_order::acquire);
      return previous_state == kSet;
    }

    bool await_suspend(std::coroutine_handle<> handle) {
      assert(previous_state == kCleared);
      if (state.compare_exchange_strong(
              previous_state,
              reinterpret_cast<std::uintptr_t>(handle.address()),
              std::memory_order::acq_rel)) {
        // kCleared -> waiting transition.
        return true;
      }
      // Racing kCleared -> kSet transition. We should resume ourselves.
      assert(previous_state == kSet);
      return false;
    }

    void await_resume() {}
  };

  return Waiter{state_};
}
