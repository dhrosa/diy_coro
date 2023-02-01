#pragma once

#include <concepts>
#include <coroutine>
#include <stdexcept>
#include <vector>

#include "diy/coro/handle.h"

// Coroutine for generating a vector of elements. Each 'co_yield' within the
// coroutine body appends to the eventual output.
template <typename T>
class VectorGenerator {
  struct Promise;

 public:
  using promise_type = Promise;

  // Consume the coroutine output.
  operator std::vector<T>() &&;

 private:
  VectorGenerator(Handle handle) : handle_(std::move(handle)) {}

  Promise& promise() { return handle_.template promise<Promise>(); }

  Handle handle_;
};

template <typename T>
struct VectorGenerator<T>::Promise {
  std::vector<T> values;
  std::exception_ptr exception;

  auto get_return_object() {
    return VectorGenerator<T>(
        Handle(std::coroutine_handle<Promise>::from_promise(*this)));
  }

  auto initial_suspend() { return std::suspend_never{}; }

  template <std::convertible_to<T> U>
  auto yield_value(U&& value) {
    values.push_back(std::forward<U>(value));
    return std::suspend_never{};
  }

  void unhandled_exception() { exception = std::current_exception(); }

  auto final_suspend() noexcept { return std::suspend_always{}; }
};

template <typename T>
VectorGenerator<T>::operator std::vector<T>() && {
  Promise& p = promise();
  if (p.exception) {
    std::rethrow_exception(p.exception);
  }
  return std::move(p.values);
}
