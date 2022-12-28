// Largely follows the Generator example from
// https://en.cppreference.com/w/cpp/language/coroutines

#pragma once

#include <coroutine>
#include <exception>
#include <iterator>
#include <memory>

// Coroutine for synchronously yielding a stream of values of tyoe
// T. Concurrent calls to any method or iterator methods is undefined
// behavior.
template <typename T>
class Generator {
  struct Promise;
  using Handle = std::coroutine_handle<Promise>;
  struct HandleCleanup;
  struct Iterator;

 public:
  using promise_type = Promise;
  using value_type = T;

  Generator() = default;
  Generator(Handle handle)
      : shared_handle_(std::make_shared<HandleCleanup>(handle)),
        iter_{handle} {}

  // Produces the next value of the sequence.
  T& operator()() { return *++iter_; }

  // Allows for iteration over the stream of values. Equality
  // comparison between iterators is only meaningful against end().
  // If begin() has already been called, the next call will correspond
  // to the next value of the sequence, not the original first value.
  std::input_iterator auto begin() const { return ++iter_; }
  std::input_iterator auto end() const { return Iterator{}; }

 private:
  std::shared_ptr<HandleCleanup> shared_handle_;
  mutable Iterator iter_;
};

// Allow direct use as a view in std::ranges library.
template <typename T>
inline constexpr bool std::ranges::enable_view<Generator<T>> = true;

template <typename T>
struct Generator<T>::Promise {
  // Previously yielded value. Is nullptr before the first co_yield, and after
  // the final co_yield. Otherwise this value is only meaningful if we are
  // currently suspended inside a co_yield statement.
  T* value = nullptr;

  // Exception thrown by couroutine, if any.
  std::exception_ptr exception;

  Generator<T> get_return_object() {
    return Generator<T>(Handle::from_promise(*this));
  }

  std::suspend_always initial_suspend() { return {}; }
  std::suspend_always final_suspend() noexcept {
    value = nullptr;
    return {};
  }
  void unhandled_exception() { exception = std::current_exception(); }

  // Resume execution of the parent to notify it that a new value is available,
  // and then wait for the parent to request a new value.
  std::suspend_always yield_value(T& new_value) {
    value = &new_value;
    return {};
  }

  // Note: Even though this overload directly matches against T&&, it will also
  // match (const T&), (T), and (U&&), where U is implicitly converitble to T.
  // Any T&& temporaries created as a result of implicit conversion are created
  // by the calling coroutine and will be kept alive across the suspension
  // point.
  std::suspend_always yield_value(T&& new_value) {
    value = &new_value;
    return {};
  }

  void return_void() {}

  // Disallow co_await within the coroutine body; generators must be
  // synchronous.
  template <typename A>
  auto await_transform(A&&) = delete;
};

// Models an input iterator.
template <typename T>
struct Generator<T>::Iterator {
  Handle handle;

  using value_type = T;
  // Needed for std::weakly_incrementable.
  using difference_type = std::ptrdiff_t;
  // Without this std::iterator_traits assumes the category is
  // std::forward_iterator_tag, which supports multi-pass. This
  // iterator can only be passed over once.
  using iterator_category = std::input_iterator_tag;

  T& operator*() const { return *handle.promise().value; }
  T* operator->() const { return handle.promise().value; }

  Iterator& operator++() {
    handle.resume();
    if (handle.promise().exception) {
      std::rethrow_exception(handle.promise().exception);
    }
    return *this;
  }

  // Needed for std::weakly_incrementable.
  Iterator& operator++(int) { return this->operator++(); }

  // Input iterators only have to support == comparison against
  // end(). Our iterators don't truly 'point' to individual elements
  // of the sequence anyway. When the sequence is exhausted, this
  // operator always returns true regardless of the other
  // operand. This is still correct behavior for an input iterator,
  // as input iterator' operator== only has to be meaningful for
  // comparison against end(). This iterator will happen to compare
  // true to any other iterator when the sequence is exhausted, but
  // that's okay since only comparison to end() is needed.
  bool operator==(Iterator) const { return handle.promise().value == nullptr; }
};

// Destroys the coroutine handle on destruction. This is not a true
// RAII type (copies and moves would cause multiple destruction), but
// this is only used in a private shared-ptr, where only one instance
// of this struct is instantiated per handle.
template <typename T>
struct Generator<T>::HandleCleanup {
  Handle handle;

  // Explicit constructor needed for std::make_shared
  HandleCleanup(Handle handle) : handle(handle) {}
  ~HandleCleanup() { handle.destroy(); }
};
