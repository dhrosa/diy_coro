#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace traits {

template <typename T>
constexpr bool kIsCoroutineHandle = false;

template <typename Promise>
constexpr bool kIsCoroutineHandle<std::coroutine_handle<Promise>> = true;

template <typename T>
concept IsCoroutineHandle = kIsCoroutineHandle<T>;

template <typename T>
concept IsAwaiter = requires(T a) {
  { a.await_ready() } -> std::convertible_to<bool>;
  a.await_suspend(std::declval<std::coroutine_handle<>>());
  a.await_resume();
};

template <typename T>
concept HasMemberFunctionCoAwait = requires(T a) {
  { std::move(a).operator co_await() } -> IsAwaiter;
};

template <typename T>
concept HasFreeFunctionCoAwait = requires(T a) {
  { operator co_await(std::move(a)) } -> IsAwaiter;
};

template <typename T>
concept IsIndirectlyAwaitable =
    HasMemberFunctionCoAwait<T> || HasFreeFunctionCoAwait<T>;

template <typename T>
concept IsAwaitable = IsAwaiter<T> || IsIndirectlyAwaitable<T>;

template <typename T>
auto ToAwaiter(T&&) = delete;

template <HasMemberFunctionCoAwait T>
auto ToAwaiter(T&& a) {
  return std::forward<T>(a).operator co_await();
}

template <HasFreeFunctionCoAwait T>
auto ToAwaiter(T&& a) {
  return operator co_await(std::forward<T>(a));
}

template <IsAwaiter T>
auto ToAwaiter(T&& a) {
  return a;
}

template <IsAwaitable T>
using AwaiterType = decltype(ToAwaiter(std::declval<T>()));

}  // namespace traits
