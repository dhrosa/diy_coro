#pragma once

#include <cassert>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>

#include "diy/coro/handle.h"
#include "diy/coro/traits.h"

// Coroutine type for asynchronously producing a sequence of values of unknown
// (and potentially unbounded) length. Requires only that `T` be moveable; T
// does not need to be default-constructible.
//
// After the body of the coroutine exits, any internally stored value of T is
// destructed; so leaving an instance of this coroutine in-scope after the final
// value is yielded will not hold onto its last value for an arbitrary amount of
// time.
template <typename T>
class AsyncGenerator {
  struct Promise;
  template <typename F, typename... Args>
  using MapResult = std::invoke_result_t<F, T, Args...>;

 public:
  using promise_type = Promise;
  using value_type = T;

  // Implicitly convertible from a range.
  template <std::ranges::range R>
  AsyncGenerator(R&& range);

  // Moveable.
  AsyncGenerator(AsyncGenerator&&) = default;
  AsyncGenerator& operator=(AsyncGenerator&&) = default;

  // Resumes the coroutine until it hands control back to the caller after its
  // first suspension. Calling this method after calling any other methods of
  // this coroutine is undefined behavior.
  void WaitForFirstSuspension() { handle_->resume(); }

  void Resume() { return handle_->resume(); }

  // Awaitable that attempts to produce the next value in the sequence. Returns
  // nullptr if there are no more values, or returns the next value. Any
  // exceptions raised raised by the generator body are raised here.
  traits::HasAwaitResult<T*> auto operator()();

  // Makes AsyncGneerator awaitable as if by awaiting on operator().
  traits::HasAwaitResult<T*> auto operator co_await();

  // Creates a new AsyncGenerator whose values are the result of applying `f` to
  // each value of the current generator.
  template <typename F, typename... Args>
  AsyncGenerator<MapResult<F, Args...>> Map(F&& f, Args&&... args) &&;

 private:
  AsyncGenerator(Handle handle) : handle_(std::move(handle)) {}

  Promise& promise() { return handle_.template promise<Promise>(); }

  Handle handle_;
};

// CTAD guide for inferring the AsyncGenerator type when converting from a
// range.
template <std::ranges::range R>
AsyncGenerator(R&& range) -> AsyncGenerator<std::ranges::range_value_t<R>>;

////////////////////
// Implementation //
////////////////////

template <typename T>
template <std::ranges::range R>
AsyncGenerator<T>::AsyncGenerator(R&& range)
    : AsyncGenerator([](R range) -> AsyncGenerator<T> {
        for (auto&& value : range) {
          co_yield std::forward<T>(value);
        }
      }(std::forward<R>(range))) {}

template <typename T>
struct AsyncGenerator<T>::Promise {
  // Value currently being yielded by the coroutine body. This is set during
  // co_yield and reset during co_await.
  T* value = nullptr;
  // Signalled when the coroutine body has exited.
  bool exhausted = false;
  // Exception thrown by coroutine body, if any.
  std::exception_ptr exception;
  // The parent coroutine (if any) to resume when a new value or when the
  // coroutine body exists.
  std::coroutine_handle<> parent;

  AsyncGenerator<T> get_return_object() {
    return AsyncGenerator<T>(
        Handle(std::coroutine_handle<Promise>::from_promise(*this)));
  }

  std::suspend_always initial_suspend() { return {}; }

  // Awaitable created in the generator coroutine that context switches into the
  // parent coroutine's body.
  auto Yield() {
    struct YieldAwaiter : std::suspend_always {
      std::coroutine_handle<> parent;

      std::coroutine_handle<> await_suspend(
          [[maybe_unused]] std::coroutine_handle<> producer) noexcept {
        if (parent) {
          return parent;
        }
        return std::noop_coroutine();
      }
    };

    return YieldAwaiter{.parent = std::exchange(parent, nullptr)};
  }

  // Resume execution of the parent at the end of the coroutine body to notify
  // it that we've reached the end of the sequence.
  auto final_suspend() noexcept {
    exhausted = true;
    return Yield();
  }

  void unhandled_exception() { exception = std::current_exception(); }
  void return_void() {}

  // Resume execution of the parent to notify it that a new value is available,
  // and then wait for the parent to request a new value.
  auto yield_value(T& new_value) {
    value = &new_value;
    return Yield();
  }

  // Note: Even though this overload directly matches against T&&, it will also
  // match (const T&), (T), and (U&&), where U is implicitly converitble to T.
  // Any T&& temporaries created as a result of implicit conversion are created
  // by the calling coroutine and will be kept alive across the suspension
  // point.
  auto yield_value(T&& new_value) {
    value = &new_value;
    return Yield();
  }
};

template <typename T>
traits::HasAwaitResult<T*> auto AsyncGenerator<T>::operator()() {
  // Resumes execution of the coroutine body to produce the next value of the
  // sequence.
  struct AdvanceAwaiter {
    AsyncGenerator<T>* generator;

    bool await_ready() {
      Promise& promise = generator->promise();
      return promise.exhausted || promise.exception || promise.value;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> parent) noexcept {
      generator->promise().parent = parent;
      return generator->handle_.get();
    }

    T* await_resume() {
      auto& promise = generator->promise();
      if (promise.exception) {
        std::rethrow_exception(promise.exception);
      }
      if (promise.exhausted) {
        return nullptr;
      }
      return std::exchange(promise.value, nullptr);
    }
  };
  return AdvanceAwaiter{.generator = this};
}

template <typename T>
template <typename F, typename... Args>
auto AsyncGenerator<T>::Map(
    F&& f, Args&&... args) && -> AsyncGenerator<MapResult<F, Args...>> {
  return [](AsyncGenerator<T> gen, F f,
            Args... args) -> AsyncGenerator<MapResult<F, Args...>> {
    while (T* value = co_await gen) {
      co_yield f(std::move(*value), args...);
    }
  }(std::move(*this), std::move(f), std::move(args)...);
}

template <typename T>
traits::HasAwaitResult<T*> auto AsyncGenerator<T>::operator co_await() {
  return traits::ToAwaiter((*this)());
}
