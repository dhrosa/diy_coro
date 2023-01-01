#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <latch>
#include <type_traits>
#include <utility>

#include "diy/coro/handle.h"
#include "diy/coro/resume.h"
#include "diy/coro/traits.h"

template <typename T = void>
class Task {
  struct Promise;

  template <typename F, typename... Args>
  using MapResult = std::invoke_result_t<F, T, Args...>;

 public:
  using promise_type = Promise;

  Task() = default;
  Task(Task&& other) noexcept = default;
  Task& operator=(Task&& other) noexcept = default;
  ~Task() = default;

  // Allow implicit converson of Task<T> to Task<>.
  template <typename U>
  Task(Task<U> other)
    requires(std::same_as<T, void> && !std::same_as<U, void>)
      : Task(std::move(other).Map([](auto&&) {})) {}

  // Constructs a Task from an arbitrary awaitable object. This is useful for
  // synchronously calling Task::Wait() on an awaitable in a non-coroutine
  // context.
  template <traits::IsAwaitable A>
  explicit Task(A&& a)
      : Task([](traits::AwaiterType<A> a) -> Task<T> {
          co_return (co_await a);
        }(traits::ToAwaiter(a))) {}

  // Resumes the coroutine until it hands control back to the caller after its
  // first suspension. Calling this method after calling any other methods of
  // this coroutine is undefined behavior.
  void WaitForFirstSuspension() { handle_->resume(); }

  void Resume() { return handle_->resume(); }

  // Creates an awaitable object that awaits the completion of this task.
  auto operator co_await() &&;

  // Synchronously waits for this task to complete, and returns its value.
  T Wait() &&;

  // Creates a new Task whose value is the result of applying `f` to
  // the retuen value of the current task.
  template <typename F, typename... Args>
  Task<MapResult<F, Args...>> Map(F&& f, Args&&... args) &&;

 private:
  static constexpr bool kIsVoidTask = std::same_as<T, void>;

  struct VoidPromiseBase;
  struct ValuePromiseBase;
  struct PromiseBase;
  enum Phase : std::uint64_t {
    kConstructed,
    kRunning,
    kComplete,
  };
  struct State {
    std::uint64_t phase;
    void* waiting;
  };

  Promise& promise() { return handle_.template promise<Promise>(); }

  SharedHandle handle_;
};

// CTAD guide for inferring the Task type when converting from an arbitrary
// awaitable.
template <traits::IsAwaitable A>
explicit Task(A&& a) -> Task<traits::AwaitResult<A>>;

////////////////////
// Implementation //
////////////////////

template <typename T>
struct Task<T>::VoidPromiseBase {
  void return_void() {}
};

template <typename T>
struct Task<T>::ValuePromiseBase {
  T final_value;

  template <typename U = T>
  void return_value(U&& value) {
    final_value = std::move(value);
  }
};

template <typename T>
struct Task<T>::PromiseBase
    : std::conditional_t<kIsVoidTask, VoidPromiseBase, ValuePromiseBase> {
  // The exception thrown by body of the task, if any.
  std::exception_ptr exception;

  void unhandled_exception() { exception = std::current_exception(); }

  T ReturnOrThrow() {
    if (this->exception) {
      std::rethrow_exception(this->exception);
    }
    if constexpr (kIsVoidTask) {
      return;
    } else {
      return std::move(this->final_value);
    }
  }
};

template <typename T>
struct Task<T>::Promise : PromiseBase {
  std::atomic<State> state;
  // Number of live references to our coroutine handle.
  std::atomic_size_t reference_count;

  Promise() {
    state.store({.phase = kConstructed, .waiting = nullptr});
    reference_count.store(0);
  }

  SharedHandle HandleRef() {
    return SharedHandle(std::coroutine_handle<Promise>::from_promise(*this), &reference_count);
  }

  Task<T> get_return_object() {
    Task<T> task;
    task.handle_ = HandleRef();
    return task;
  }

  // Lazy execution. Task body is deferred to the first explicit resume() call.
  auto initial_suspend() noexcept {
    struct InitialSuspend : std::suspend_always {
      std::atomic<State>& state;

      void await_resume() {
        State previous_state = state.load();
        while (true) {
          if (state.compare_exchange_strong(
                  previous_state,
                  {.phase = kRunning, .waiting = previous_state.waiting})) {
            // We were sequenced entirely before the waiter.
            return;
          }
          // The waiter raced with us and registered itself successfully. Redo
          // logic with newly observed state
          assert(previous_state.waiting != nullptr);
          continue;
        }
      }
    };
    return InitialSuspend{.state = state};
  };

  // Resume execution of parent coroutine that was awaiting this task's
  // completion, if any.
  auto final_suspend() noexcept {
    struct FinalSuspend : std::suspend_always {
      Promise& promise;

      std::coroutine_handle<> await_suspend(
          [[maybe_unused]] std::coroutine_handle<> handle) {
        // Transitioning to kComplete phase may instantly trigger a waiting
        // thread to destruct its own copy of our handle while we’re
        // concurrently calling std::atomic::notify_one(), so we keep the handle
        // alive throughout this call.
        SharedHandle handle_ref = promise.HandleRef();
        State previous_state = promise.state.load();
        while (true) {
          if (previous_state.waiting) {
            // Task completion sequenced entirely after waiter.
            promise.state.store({.phase = kComplete, .waiting = nullptr});
            promise.state.notify_one();
            return std::coroutine_handle<>::from_address(
                previous_state.waiting);
          }
          // Waiter not registered yet.
          if (promise.state.compare_exchange_strong(
                  previous_state, {.phase = kComplete, .waiting = nullptr})) {
            promise.state.notify_one();
            // Waiter sequenced entirely after task completion. It's the
            // waiter's responsibility to resume themselves.
            return std::noop_coroutine();
          }
          // Waiter appeared during transition. Redo above logic with newly
          // observed state.
        }
      }
    };
    return FinalSuspend{.promise = *this};
  }
};

template <typename T>
auto Task<T>::operator co_await() && {
  struct Awaiter {
    // The child task whose completion is being awaited.
    Task task;

    // Lets us reuse atomic load from await_ready() in await_suspend().
    State previous_state;

    bool await_ready() {
      previous_state = task.promise().state.load();
      return previous_state.phase == kComplete;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiting) {
      Promise& promise = task.promise();
      std::atomic<State>& state = promise.state;
      while (true) {
        assert(previous_state.waiting == nullptr);
        if (previous_state.phase == kConstructed) {
          // It's our job to start task. No one can race against us at this
          // point because we have consumed the task.
          state.store({.phase = kRunning, .waiting = waiting.address()});
          return task.handle_.get();
        }
        if (previous_state.phase == kComplete) {
          // Task completion sequenced entirely before wait; resume immediately.
          return waiting;
        }
        // Task was already running and isn't complete yet.
        if (state.compare_exchange_strong(previous_state,
                                          {.phase = previous_state.phase,
                                           .waiting = waiting.address()})) {
          // Task completion is sequenced entirely after this wait. It's the
          // task's reponsibility to wake us.
          return std::noop_coroutine();
        }
        // Task phase transition raced with our wait. Redo checks with newly
        // observed state.
        assert(previous_state.waiting != nullptr);
      }
    }
    // Child task has completed; return its final value.
    auto await_resume() { return task.promise().ReturnOrThrow(); }
  };
  return Awaiter{.task = std::move(*this)};
}

template <typename T>
T Task<T>::Wait() && {
  std::atomic<State>& state = promise().state;
  while (true) {
    State previous_state = state.load();
    switch (previous_state.phase) {
      case kConstructed:
        // Task was never started; start it ourselves.
        assert(previous_state.waiting == nullptr);
        handle_->resume();
        continue;
      case kRunning:
        // Task was already running. Wait for it to transition.
        assert(previous_state.waiting == nullptr);
        state.wait(previous_state);
        continue;
      case kComplete:
        return promise().ReturnOrThrow();
    }
  }
}

template <typename T>
template <typename F, typename... Args>
auto Task<T>::Map(F&& f, Args&&... args) && -> Task<MapResult<F, Args...>> {
  return [](Task<T> task, F f, Args... args) -> Task<MapResult<F, Args...>> {
    co_return f((co_await std::move(task)), args...);
  }(std::move(*this), std::move(f), std::move(args)...);
}
