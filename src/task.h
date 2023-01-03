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
struct Task<T>::Promise
    : std::conditional_t<kIsVoidTask, VoidPromiseBase, ValuePromiseBase> {
  // Used to wake synchronous waiter.
  std::atomic_flag complete;
  // The coroutine waiting on this task's completion.
  std::coroutine_handle<> waiting;
  // Number of live references to our coroutine handle.
  std::atomic_size_t handle_reference_count;
  // The exception thrown by body of the task, if any.
  std::exception_ptr exception;

  SharedHandle handle_ref;

  Promise() {
    handle_reference_count.store(0, std::memory_order::relaxed);
    handle_ref =
        SharedHandle(std::coroutine_handle<Promise>::from_promise(*this),
                     &handle_reference_count);
  }

  Task<T> get_return_object() {
    Task<T> task;
    task.handle_ = handle_ref;
    return task;
  }

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

  // Lazy execution. Task body is deferred to the first explicit resume() call.
  auto initial_suspend() noexcept {
    return std::suspend_always();
  };

  // Resume execution of parent coroutine that was awaiting this task's
  // completion, if any.
  auto final_suspend() noexcept {
    struct FinalSuspend : std::suspend_always {
      Promise& promise;

      std::coroutine_handle<> await_suspend(
          [[maybe_unused]] std::coroutine_handle<> handle) {
        SharedHandle handle_ref = std::move(promise.handle_ref);
        if (promise.waiting) {
          return promise.waiting;
        }
        // Wait synchronous waiter.
        promise.complete.test_and_set();
        promise.complete.notify_one();
        return std::noop_coroutine();
      }
    };
    return FinalSuspend{.promise = *this};
  }
};

template <typename T>
auto Task<T>::operator co_await() && {
  struct Awaiter : std::suspend_always {
    // The child task whose completion is being awaited.
    Task task;

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiting) {
      task.promise().waiting = waiting;
      return task.handle_.get();
    }

    // Child task has completed; return its final value.
    auto await_resume() { return task.promise().ReturnOrThrow(); }
  };
  return Awaiter{.task = std::move(*this)};
}

template <typename T>
T Task<T>::Wait() && {
  handle_->resume();
  promise().complete.wait(false);
  return promise().ReturnOrThrow();
}

template <typename T>
template <typename F, typename... Args>
auto Task<T>::Map(F&& f, Args&&... args) && -> Task<MapResult<F, Args...>> {
  return [](Task<T> task, F f, Args... args) -> Task<MapResult<F, Args...>> {
    co_return f((co_await std::move(task)), args...);
  }(std::move(*this), std::move(f), std::move(args)...);
}
