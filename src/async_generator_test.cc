#include "diy/coro/async_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::ElementsAre;
using testing::Eq;
using testing::Pointee;

TEST(AsyncGeneratorTest, Empty) {
  auto gen = []() -> AsyncGenerator<int> { co_return; }();
  EXPECT_THAT(gen().Wait(), testing::IsNull());
}

template <typename T>
Task<std::vector<T>> Materialize(AsyncGenerator<T> gen) {
  std::vector<T> out;
  while (T* value = co_await gen) {
    out.emplace_back(std::move(*value));
  }
  co_return out;
}

TEST(AsyncGeneratorTest, Finite) {
  auto gen = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  EXPECT_THAT(Materialize(gen()).Wait(), ElementsAre(1, 2, 3));
}

TEST(AsyncGeneratorTest, PropagatesExceptions) {
  auto gen = []() -> AsyncGenerator<int> {
    co_yield 1;
    throw std::invalid_argument("some error");
  }();

  EXPECT_THAT(gen().Wait(), Pointee(Eq(1)));
  EXPECT_THROW(gen().Wait(), std::invalid_argument);
}

TEST(AsyncGeneratorTest, Nested) {
  auto gen_a = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  auto gen_b = [](AsyncGenerator<int> values) -> AsyncGenerator<int> {
    while (int* value = co_await values) {
      co_yield *value * 2;
    }
  };

  EXPECT_THAT(Materialize(gen_b(gen_a())).Wait(), ElementsAre(2, 4, 6));
}

TEST(AsyncGeneratorTest, ChainAsyncToAsync) {
  auto gen_a = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  auto gen_b = [](AsyncGenerator<int> values) -> AsyncGenerator<int> {
    while (int* value = co_await values) {
      co_yield *value * 2;
    }
  };

  EXPECT_THAT(Materialize(gen_a() | gen_b).Wait(), ElementsAre(2, 4, 6));
}

TEST(AsyncGeneratorTest, ChainSyncToAsync) {
  auto gen_a = []() -> Generator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  auto gen_b = [](AsyncGenerator<int> values) -> AsyncGenerator<int> {
    while (int* value = co_await values) {
      co_yield *value * 2;
    }
  };

  EXPECT_THAT(Materialize(gen_a() | gen_b).Wait(), ElementsAre(2, 4, 6));
}

TEST(AsyncGeneratorTest, Map) {
  auto gen_a = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  EXPECT_THAT(Materialize(gen_a().Map([](int x) { return x * 2; })).Wait(),
              ElementsAre(2, 4, 6));
}

TEST(AsyncGeneratorTest, MapWithExtraArguments) {
  auto gen_a = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  EXPECT_THAT(
      Materialize(gen_a().Map([](int x, int y) { return x * y; }, 2)).Wait(),
      ElementsAre(2, 4, 6));
}

TEST(AsyncGeneratorTest, NonDefaultConstructibleType) {
  struct Value {
    Value() = delete;
    Value(int x, int& destructor_calls)
        : x(x), destructor_calls(&destructor_calls) {}

    ~Value() { ++(*destructor_calls); }

    operator int() const { return x; }

    int x;
    int* destructor_calls;
  };

  int destructor_calls = 0;
  auto gen = [](int& destructor_calls) -> AsyncGenerator<Value> {
    co_yield {1, destructor_calls};
    co_yield {2, destructor_calls};
    co_yield {3, destructor_calls};
  }(destructor_calls);

  // The coroutine is suspended in the middle of co_yield; so the first
  // temporary should not be destructed yet.
  EXPECT_THAT(gen().Wait(), Pointee(Eq(1)));
  EXPECT_EQ(destructor_calls, 0);

  // First temporary destructed; second temporary in-flight.
  EXPECT_THAT(gen().Wait(), Pointee(Eq(2)));
  EXPECT_EQ(destructor_calls, 1);

  // Second temporary destructed; third temporary in-flight.
  EXPECT_THAT(gen().Wait(), Pointee(Eq(3)));
  EXPECT_EQ(destructor_calls, 2);

  // Generator is exhausted; the third temporary should be destructed, as well
  // as the value stored internally in the promise.
  EXPECT_EQ(gen().Wait(), nullptr);
  EXPECT_EQ(destructor_calls, 4);
}
