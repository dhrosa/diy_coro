#include "traits.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(TraitsTest, IsCoroutineHandle) {
  EXPECT_FALSE(traits::IsCoroutineHandle<int>);

  EXPECT_TRUE(traits::IsCoroutineHandle<std::coroutine_handle<>>);

  struct Promise {};
  EXPECT_TRUE(traits::IsCoroutineHandle<std::coroutine_handle<Promise>>);
}

TEST(TraitsTest, IsAwaiter) {
  struct Awaiter {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<>) {}
    void await_resume(){};
  };

  EXPECT_TRUE(traits::IsAwaiter<Awaiter>);

  struct Awaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_FALSE(traits::IsAwaiter<Awaitable>);
}

TEST(TraitsTest, IsAwaitable) {
  struct Awaiter {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<>) {}
    void await_resume(){};
  };

  EXPECT_TRUE(traits::IsAwaitable<Awaiter>);

  struct Awaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_TRUE(traits::IsAwaitable<Awaitable>);
}

template <typename A>
struct FreeFunctionAwaitable {};

template <typename A>
A operator co_await(FreeFunctionAwaitable<A> a) {
  return {};
}

TEST(TraitsTest, AwaiterType) {
  struct Awaiter {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<>) {}
    void await_resume(){};
  };

  EXPECT_TRUE((std::same_as<traits::AwaiterType<Awaiter>, Awaiter>));

  struct MemberFunctionAwaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_TRUE(
      (std::same_as<traits::AwaiterType<MemberFunctionAwaitable>, Awaiter>));
  EXPECT_TRUE((std::same_as<traits::AwaiterType<FreeFunctionAwaitable<Awaiter>>,
                            Awaiter>));
}
