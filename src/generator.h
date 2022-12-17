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
  // Previously co_yielded value.
  T value;
  // Exception thrown by couroutine, if any.
  std::exception_ptr exception;

  Generator<T> get_return_object() {
    return Generator<T>(Handle::from_promise(*this));
  }

  std::suspend_always initial_suspend() { return {}; }
  std::suspend_always final_suspend() noexcept { return {}; }
  void unhandled_exception() { exception = std::current_exception(); }

  template <std::convertible_to<T> U>
  std::suspend_always yield_value(U&& new_value) {
    value = std::forward<U>(new_value);
    return {};
  }

  void return_void() {}
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

  T& operator*() const { return handle.promise().value; }
  T* operator->() const { return &handle.promise().value; }

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
  bool operator==(Iterator) const { return handle.done(); }
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

template <typename T>
constexpr bool kIsSyncGenerator = false;

template <typename T>
constexpr bool kIsSyncGenerator<Generator<T>> = true;
