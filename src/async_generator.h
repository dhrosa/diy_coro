#pragma once

#include <stdexcept>
#include <type_traits>

#include "generator.h"
#include "handle.h"
#include "task.h"

template <typename T>
class AsyncGenerator {
  struct Promise;
  template <typename F, typename... Args>
  using MapResult = std::invoke_result_t<F, T, Args...>;

 public:
  using promise_type = Promise;
  using value_type = T;

  // Implicitly convertible from a synchronous Generator
  AsyncGenerator(Generator<T> sync_generator);

  // Moveable.
  AsyncGenerator(AsyncGenerator&&) = default;
  AsyncGenerator& operator=(AsyncGenerator&&) = default;

  // Attempts to produce the next value in the sequence. Returns nullptr if
  // there are no more values, or returns the next value. Any exceptions raised
  // raised by the generator body are raised here.
  Task<T*> operator()();

  // Makes AsyncGneerator awaitable as if by awaiting on operator().
  auto operator co_await() { return (*this)().operator co_await(); }

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

template <typename T>
AsyncGenerator<T>::AsyncGenerator(Generator<T> sync_generator) {
  *this = [](Generator<T> gen) -> AsyncGenerator<T> {
    for (auto&& val : gen) {
      co_yield std::move(val);
    }
  }(std::move(sync_generator));
}

template <typename T>
struct AsyncGenerator<T>::Promise {
  // The last yielded value.
  T value;
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
    exhausted = true;
    return YieldAwaiter{std::exchange(this->parent, nullptr)};
  }

  void unhandled_exception() { exception = std::current_exception(); }
  void return_void() {}

  // Resume execution of the parent to notify it that a new value is available,
  // and then wait for the parent to request a new value.
  template <std::convertible_to<T> U>
  auto yield_value(U&& new_value) {
    value = std::forward<U>(new_value);
    return YieldAwaiter{std::exchange(this->parent, nullptr)};
  }
};

template <typename T>
Task<T*> AsyncGenerator<T>::operator()() {
  co_return (co_await AdvanceAwaiter{this});
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
struct AsyncGenerator<T>::AdvanceAwaiter {
  AsyncGenerator<T>* generator;

  bool await_ready() noexcept { return false; }

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
    return &promise.value;
  }
};

// Awaitable created in the generator coroutine that context switches into the
// parent coroutine's body.
template <typename T>
struct AsyncGenerator<T>::YieldAwaiter {
  std::coroutine_handle<> parent;

  bool await_ready() noexcept { return false; }

  std::coroutine_handle<> await_suspend(
      [[maybe_unused]] std::coroutine_handle<> producer) noexcept {
    if (parent) {
      return parent;
    }
    return std::noop_coroutine();
  }

  void await_resume() noexcept {}
};

template <typename T>
constexpr bool kIsAsyncGenerator = false;

template <typename T>
constexpr bool kIsAsyncGenerator<AsyncGenerator<T>> = true;

template <typename T>
concept IsAsyncGenerator = kIsAsyncGenerator<T>;

template <typename Producer, typename Consumer>
concept Chainable =
    (kIsAsyncGenerator<Producer> || kIsSyncGenerator<Producer>) &&
    requires(Producer producer, Consumer consumer) {
      {
        consumer(std::declval<AsyncGenerator<typename Producer::value_type>>())
      } -> IsAsyncGenerator;
    };

template <typename P, typename C>
auto operator|(P&& p, C&& c)
  requires Chainable<P, C>
{
  return c(std::move(p));
}
