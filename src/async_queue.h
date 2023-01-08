#pragma once

#include <coroutine>
#include <mutex>
#include <queue>

#include "diy/coro/async_generator.h"
#include "diy/coro/task.h"
#include "diy/coro/traits.h"

// Single-producer single-consumer queue with an AsyncGenerator pull interface.
template <typename T>
class AsyncQueue {
 public:
  void Push(T value);

  // Stream of values produced by Push().
  AsyncGenerator<T> Values();

 private:
  struct State;

  State state_;
};

template <typename T>
struct AsyncQueue<T>::State {
  std::mutex mutex;
  std::queue<T> values;
  std::coroutine_handle<> waiting;

  auto Pop() {
    struct Awaiter : std::suspend_always {
      State& state;
      std::optional<T> value;

      bool await_suspend(std::coroutine_handle<> handle) {
        auto lock = std::lock_guard(state.mutex);
        if (state.values.empty()) {
          state.waiting = handle;
          return true;
        }
        value = std::move(state.values.front());
        state.values.pop();
        return false;
      }

      T& await_resume() {
        if (not value) {
          auto lock = std::lock_guard(state.mutex);
          value = std::move(state.values.front());
          state.values.pop();
        }
        return *value;
      }
    };

    return Awaiter{.state = *this};
  }
};

template <typename T>
void AsyncQueue<T>::Push(T value) {
  std::coroutine_handle<> waiting;
  {
    auto lock = std::lock_guard(state_.mutex);
    state_.values.push(std::move(value));
    waiting = std::exchange(state_.waiting, nullptr);
  }
  if (waiting) {
    waiting.resume();
  }
}

template <typename T>
AsyncGenerator<T> AsyncQueue<T>::Values() {
  while (std::optional<T> value = co_await state_.Pop()) {
    co_yield *value;
  }
}
