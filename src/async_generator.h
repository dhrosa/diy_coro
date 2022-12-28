#pragma once

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
  struct AdvanceAwaiter;
  struct YieldAwaiter;

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
  // Prveiously yielded value. We use std::optional so that we can support
  // non-default-constructible types, and so that we can manually destroy it.
  std::optional<T> value;

  // Exception thrown by coroutine body, if any.
  std::exception_ptr exception;
  // The parent coroutine (if any) to resume when a new value or when the
  // coroutine body exists.
  std::coroutine_handle<> parent;
  // Set when the coroutine body exits.
  bool exhausted = false;

  AsyncGenerator<T> get_return_object() {
    return AsyncGenerator<T>(
        Handle(std::coroutine_handle<Promise>::from_promise(*this)));
  }

  std::suspend_always initial_suspend() { return {}; }

  // Resume execution of the parent at the end of the coroutine body to notify
  // it that we've reached the end of the sequence.
  YieldAwaiter final_suspend() noexcept {
    value.reset();
    exhausted = true;
    return YieldAwaiter{.parent = std::exchange(this->parent, nullptr)};
  }

  void unhandled_exception() { exception = std::current_exception(); }
  void return_void() {}

  // Resume execution of the parent to notify it that a new value is available,
  // and then wait for the parent to request a new value.
  template <std::convertible_to<T> U = T>
  auto yield_value(U&& new_value) {
    value = std::forward<U>(new_value);
    return YieldAwaiter{.parent = std::exchange(this->parent, nullptr)};
  }
};

template <typename T>
traits::HasAwaitResult<T*> auto AsyncGenerator<T>::operator()() {
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

// Awaitable created in the parent coroutine that context switches into the
// generator coroutine's body.
template <typename T>
struct AsyncGenerator<T>::AdvanceAwaiter : std::suspend_always {
  AsyncGenerator<T>* generator;

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
    return promise.value ? &(*promise.value) : nullptr;
  }
};

template <typename T>
traits::HasAwaitResult<T*> auto AsyncGenerator<T>::operator co_await() {
  return traits::ToAwaiter((*this)());
}

// Awaitable created in the generator coroutine that context switches into the
// parent coroutine's body.
template <typename T>
struct AsyncGenerator<T>::YieldAwaiter : std::suspend_always {
  std::coroutine_handle<> parent;

  std::coroutine_handle<> await_suspend(
      [[maybe_unused]] std::coroutine_handle<> producer) noexcept {
    if (parent) {
      return parent;
    }
    return std::noop_coroutine();
  }
};
