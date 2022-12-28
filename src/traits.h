#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace traits {

// Satisfied for any type that has the appropriate await_ready() /
// await_suspend() / await_resume() member functions. This doesn't support
// await_suspend() implementations that take a Promise-specific
// std::coroutine_handle.
template <typename T>
concept IsAwaiter = requires(T a) {
  { a.await_ready() } -> std::convertible_to<bool>;
  a.await_suspend(std::declval<std::coroutine_handle<>>());
  a.await_resume();
};

// Satisfied for any type that defines a co_await() member function operator.
template <typename T>
concept HasMemberFunctionCoAwait = requires(T a) {
  { std::move(a).operator co_await() } -> IsAwaiter;
};

// Satisfied for any type that has a free function co_await() operator defined.
template <typename T>
concept HasFreeFunctionCoAwait = requires(T a) {
  { operator co_await(std::move(a)) } -> IsAwaiter;
};

// Satisfied for any type that has a co_await operator.
template <typename T>
concept IsIndirectlyAwaitable =
    HasMemberFunctionCoAwait<T> || HasFreeFunctionCoAwait<T>;

// Satisfied for any type that is directly awaitable, or can be converted to one
// via the co_await operator.
template <typename T>
concept IsAwaitable = IsAwaiter<T> || IsIndirectlyAwaitable<T>;

// ToAwaiter() takes an arbitrary awaitable object and returns an object that
// directly satisfies IsAwaiter.

template <typename T>
auto ToAwaiter(T&&) = delete;

template <HasMemberFunctionCoAwait T>
IsAwaiter auto ToAwaiter(T&& a) {
  return std::forward<T>(a).operator co_await();
}

template <HasFreeFunctionCoAwait T>
IsAwaiter auto ToAwaiter(T&& a) {
  return operator co_await(std::forward<T>(a));
}

template <IsAwaiter T>
IsAwaiter auto ToAwaiter(T&& a) {
  return a;
}

// The awaiter type of a co_await expression.
template <IsAwaitable T>
using AwaiterType = decltype(ToAwaiter(std::declval<T>()));

// The result type of a co_await expression.
template <IsAwaitable T>
using AwaitResult = decltype(std::declval<AwaiterType<T>>().await_resume());

// Satisfied if awaiting on A results in a value of type T.
template <typename A, typename T>
concept HasAwaitResult = IsAwaitable<A> && std::same_as<AwaitResult<A>, T>;

}  // namespace traits
