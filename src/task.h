#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <latch>
#include <type_traits>
#include <utility>

#include "diy/coro/handle.h"
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
  void WaitForFirstSuspension() { handle_-> resume(); }
  
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

  Promise& promise() { return handle_.template promise<Promise>(); }

  Handle handle_;
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
    this->final_value = std::move(value);
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
  // The suspended coroutine awaiting this task's completion, if any.
  std::coroutine_handle<> parent;

  Task<T> get_return_object() {
    Task<T> task;
    task.handle_ = Handle(std::coroutine_handle<Promise>::from_promise(*this));
    return task;
  }

  // Lazy execution. Task body is deferred to the first explicit resume() call.
  std::suspend_always initial_suspend() noexcept { return {}; }

  // Resume execution of parent coroutine that was awaiting this task's
  // completion, if any.
  auto final_suspend() noexcept {
    struct FinalAwaiter : std::suspend_always {
      std::coroutine_handle<> parent;

      std::coroutine_handle<> await_suspend(
          [[maybe_unused]] std::coroutine_handle<> task_handle) noexcept {
        if (parent) {
          return parent;
        }
        return std::noop_coroutine();
      }
    };
    return FinalAwaiter{.parent = std::exchange(parent, nullptr)};
  }
};

template <typename T>
auto Task<T>::operator co_await() && {
  struct Awaiter {
    // The child task whose completion is being awaited.
    Task task;

    bool await_ready() {
      return task.handle_->done();
    }
    
    // Tell the child task to resume the parent (current task) when it
    // completes. Then context switch into the child task.
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) {
      task.promise().parent = parent;
      return task.handle_.get();
    }

    // Child task has completed; return its final value.
    auto await_resume() { return task.promise().ReturnOrThrow(); }
  };
  return Awaiter{.task = std::move(*this)};
}

template <typename T>
T Task<T>::Wait() && {
  // We create a custom coroutine that synchronously notifies the caller when
  // its complete.
  struct SyncPromise;

  struct SyncTask {
    Handle handle;

    using promise_type = SyncPromise;
  };

  struct SyncPromise : PromiseBase {
    std::latch complete{1};

    SyncTask get_return_object() {
      return {Handle(std::coroutine_handle<SyncPromise>::from_promise(*this))};
    }

    auto initial_suspend() { return std::suspend_never{}; }

    // We can't signal completion directly inside final_suspend, as this member
    // is called while the coroutine is still executing. We instead utilize the
    // fact that when  an awaiter's await_suspend() is called,  the coroutine is
    // guaranteed to be suspended.
    auto final_suspend() noexcept {
      struct FinalAwaiter : std::suspend_always {
        std::latch& complete;
        void await_suspend(std::coroutine_handle<>) noexcept {
          complete.count_down();
        }
      };
      return FinalAwaiter{.complete = complete};
    }
  };

  auto sync_task = [](Task<T> task) -> SyncTask {
    co_return (co_await std::move(task));
  }(std::move(*this));
  SyncPromise& promise = sync_task.handle.template promise<SyncPromise>();
  promise.complete.wait();
  return promise.ReturnOrThrow();
}

template <typename T>
template <typename F, typename... Args>
auto Task<T>::Map(F&& f, Args&&... args) && -> Task<MapResult<F, Args...>> {
  return [](Task<T> task, F f, Args... args) -> Task<MapResult<F, Args...>> {
    co_return f((co_await std::move(task)), args...);
  }(std::move(*this), std::move(f), std::move(args)...);
}
