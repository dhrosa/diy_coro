#pragma once

#include <cassert>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>

#include "diy/coro/handle.h"
#include "diy/coro/resume.h"
#include "diy/coro/task.h"
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
  struct GetYielderType {};
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

  // When co-awaited within a AsyncGenerator body, provides a Yielder for the
  // current generator.
  static auto GetYielder() { return GetYielderType{}; }

  // Proxy for co-yielding to a AsyncGenerator from within a separate function
  // as if co_yield was called.
  class Yielder {
   public:
    Yielder(Promise* promise) : promise_(promise) {}

    template <typename U>
    auto Yield(U&& value) {
      return promise_->yield_value(std::forward<U>(value));
    }

   private:
    Promise* promise_;
  };

  // Allows AsyncGenerator to be iterated over as an input range in a
  // non-coroutine-context.
  class SyncRange : std::ranges::view_base {
   public:
    class Iterator;

    using value_type = T;
    Iterator begin() const { return ++Iterator(generator_); }
    Iterator end() const { return Iterator(generator_); }

   private:
    friend class AsyncGenerator;
    SyncRange(AsyncGenerator* generator) : generator_(generator) {}
    AsyncGenerator* generator_;
  };

  SyncRange ToSyncRange() { return SyncRange(this); }

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
  std::coroutine_handle<> generator_handle;

  AsyncGenerator<T> get_return_object() {
    generator_handle = std::coroutine_handle<Promise>::from_promise(*this);
    return AsyncGenerator<T>(Handle(generator_handle));
  }

  std::suspend_always initial_suspend() { return {}; }

  // Awaitable created in the generator coroutine that context switches into the
  // parent coroutine's body.
  auto Yield() {
    struct Awaiter : std::suspend_always {
      Promise& promise;
      std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
        promise.generator_handle = handle;
        if (promise.parent) {
          return std::exchange(promise.parent, nullptr);
        }
        return std::noop_coroutine();
      }
    };
    return Awaiter{.promise = *this};
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
    assert(value == nullptr);
    value = &new_value;
    return Yield();
  }

  // Note: Even though this overload directly matches against T&&, it will also
  // match (const T&), (T), and (U&&), where U is implicitly converitble to T.
  // Any T&& temporaries created as a result of implicit conversion are created
  // by the calling coroutine and will be kept alive across the suspension
  // point.
  auto yield_value(T&& new_value) {
    return yield_value(static_cast<T&>(new_value));
  }

  auto await_transform([[maybe_unused]] GetYielderType) {
    struct Awaiter : std::suspend_never {
      Promise* promise;

      Yielder await_resume() { return Yielder(promise); }
    };
    return Awaiter{.promise = this};
  }

  template <typename U>
  decltype(auto) await_transform(U&& x) {
    return std::forward<U>(x);
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
      return generator->promise().generator_handle;
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

// Input iterator for SyncRange.
template <typename T>
class AsyncGenerator<T>::SyncRange::Iterator {
 public:
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::input_iterator_tag;

  Iterator() = default;

  T& operator*() const { return *value_; }
  T* operator->() const { return value_; }
  Iterator& operator++() {
    value_ = Task(*generator_).Wait();
    return *this;
  }
  Iterator operator++(int) {
    Iterator old = *this;
    ++(*this);
    return old;
  }
  bool operator==(Iterator) const { return value_ == nullptr; }

 private:
  friend class SyncRange;
  Iterator(AsyncGenerator* generator) : generator_(generator) {}

  AsyncGenerator* generator_;
  T* value_;
};
