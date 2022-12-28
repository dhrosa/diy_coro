#include "diy/coro/traits.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(TraitsTest, IsAwaiter) {
  struct Awaiter : std::suspend_always {};

  EXPECT_TRUE(traits::IsAwaiter<Awaiter>);

  struct Awaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_FALSE(traits::IsAwaiter<Awaitable>);
}

TEST(TraitsTest, IsAwaitable) {
  struct Awaiter : std::suspend_always {};

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
  struct Awaiter : std::suspend_always {};

  EXPECT_TRUE((std::same_as<traits::AwaiterType<Awaiter>, Awaiter>));

  struct MemberFunctionAwaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_TRUE(
      (std::same_as<traits::AwaiterType<MemberFunctionAwaitable>, Awaiter>));
  EXPECT_TRUE((std::same_as<traits::AwaiterType<FreeFunctionAwaitable<Awaiter>>,
                            Awaiter>));
}

TEST(TraitsTest, AwaitResult) {
  struct Awaiter : std::suspend_always {
    int await_resume() { return 0; }
  };
  EXPECT_TRUE((std::same_as<traits::AwaitResult<Awaiter>, int>));

  struct Awaitable {
    Awaiter operator co_await() { return {}; }
  };
  EXPECT_TRUE((std::same_as<traits::AwaitResult<Awaitable>, int>));
}
