#include "diy/coro/traits.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace traits {

TEST(TraitsTest, IsAwaiter) {
  struct Awaiter : std::suspend_always {};

  EXPECT_TRUE(IsAwaiter<Awaiter>);

  struct Awaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_FALSE(IsAwaiter<Awaitable>);
}

TEST(TraitsTest, IsAwaitable) {
  struct Awaiter : std::suspend_always {};

  EXPECT_TRUE(IsAwaitable<Awaiter>);

  struct Awaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_TRUE(IsAwaitable<Awaitable>);
}

template <typename A>
struct FreeFunctionAwaitable {};

template <typename A>
A operator co_await(FreeFunctionAwaitable<A> a) {
  return {};
}

TEST(TraitsTest, AwaiterType) {
  struct Awaiter : std::suspend_always {};

  EXPECT_TRUE((std::same_as<AwaiterType<Awaiter>, Awaiter>));

  struct MemberFunctionAwaitable {
    Awaiter operator co_await() { return Awaiter{}; }
  };

  EXPECT_TRUE((std::same_as<AwaiterType<MemberFunctionAwaitable>, Awaiter>));
  EXPECT_TRUE(
      (std::same_as<AwaiterType<FreeFunctionAwaitable<Awaiter>>, Awaiter>));
}

TEST(TraitsTest, AwaitResult) {
  struct Awaiter : std::suspend_always {
    int await_resume() { return 0; }
  };
  EXPECT_TRUE((std::same_as<AwaitResult<Awaiter>, int>));

  struct Awaitable {
    Awaiter operator co_await() { return {}; }
  };
  EXPECT_TRUE((std::same_as<AwaitResult<Awaitable>, int>));
}

TEST(TraitsTest, HasAwaitResult) {
  struct Awaiter : std::suspend_always {
    int await_resume() { return 0; }
  };
  EXPECT_TRUE((HasAwaitResult<Awaiter, int>));

  struct Awaitable {
    Awaiter operator co_await() { return {}; }
  };
  EXPECT_TRUE((HasAwaitResult<Awaitable, int>));
}

}  // namespace traits
