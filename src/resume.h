#pragma once

#include <coroutine>

#include "diy/coro/traits.h"

// Awaitable that transfer control to the given coroutine. If ‘target‘ os
// nullptr, we transfer to a no-op coroutine. This function by itself does not
// offer any provision for transferring control back to the original coroutine.
// This function should be used in locations where the original coroutine will
// be resumed by some other mechanism.
inline traits::HasAwaitResult<void> auto Resume(
    std::coroutine_handle<> target) {
  struct Awaiter : std::suspend_always {
    std::coroutine_handle<> target;

    std::coroutine_handle<> await_suspend(
        [[maybe_unused]] std::coroutine_handle<> handle) {
      if (target) {
        return target;
      }
      return std::noop_coroutine();
    }
  };
  return Awaiter{.target = target};
}
