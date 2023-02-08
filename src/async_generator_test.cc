#include "diy/coro/async_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ranges>

#include "diy/coro/container_generator.h"
#include "diy/coro/generator.h"
#include "diy/coro/task.h"

using testing::ElementsAre;
using testing::Eq;
using testing::Pointee;

template <typename T>
std::vector<T> ToVector(AsyncGenerator<T> gen) {
  return [](AsyncGenerator<T> gen) -> VectorGenerator<T> {
    while (T* value = gen.Wait()) {
      co_yield *value;
    }
  }(std::move(gen));
}

TEST(AsyncGeneratorTest, Empty) {
  auto gen = []() -> AsyncGenerator<int> { co_return; };
  EXPECT_THAT(ToVector(gen()), testing::IsEmpty());
}

TEST(AsyncGeneratorTest, Finite) {
  auto gen = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  EXPECT_THAT(ToVector(gen()), ElementsAre(1, 2, 3));
}

TEST(AsyncGeneratorTest, PropagatesExceptions) {
  auto gen = []() -> AsyncGenerator<int> {
    co_yield 1;
    throw std::invalid_argument("some error");
  }();

  EXPECT_THAT(gen.Wait(), Pointee(Eq(1)));
  EXPECT_THROW(gen.Wait(), std::invalid_argument);
}

TEST(AsyncGeneratorTest, FromVector) {
  EXPECT_THAT(ToVector(AsyncGenerator(std::vector<int>({1, 2, 3}))),
              ElementsAre(1, 2, 3));
}

TEST(AsyncGeneratorTest, FromSyncGenerator) {
  auto gen = []() -> Generator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };
  EXPECT_THAT(ToVector(AsyncGenerator(gen())), ElementsAre(1, 2, 3));
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

  EXPECT_THAT(ToVector(gen_b(gen_a())), ElementsAre(2, 4, 6));
}

TEST(AsyncGeneratorTest, Map) {
  auto gen_a = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  EXPECT_THAT(ToVector(gen_a().Map([](int x) { return x * 2; })),
              ElementsAre(2, 4, 6));
}

TEST(AsyncGeneratorTest, MapWithExtraArguments) {
  auto gen_a = []() -> AsyncGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  };

  EXPECT_THAT(ToVector(gen_a().Map([](int x, int y) { return x * y; }, 2)),
              ElementsAre(2, 4, 6));
}

// We should be able to use Yielder to yield values from within a nested
// function call.
TEST(AsyncGenerator, Yielder) {
  auto gen = []() -> AsyncGenerator<int> {
    co_yield 1;
    auto inner = [](AsyncGenerator<int>::Yielder yielder) -> Task<> {
      co_await yielder.Yield(2);
      co_await yielder.Yield(3);
    };
    co_await inner(co_await AsyncGenerator<int>::GetYielder());
    co_yield 4;
  };
  EXPECT_THAT(ToVector(gen()), ElementsAre(1, 2, 3, 4));
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
  EXPECT_THAT(gen.Wait(), Pointee(Eq(1)));
  EXPECT_EQ(destructor_calls, 0);

  // First temporary destructed; second temporary in-flight.
  EXPECT_THAT(gen.Wait(), Pointee(Eq(2)));
  EXPECT_EQ(destructor_calls, 1);

  // Second temporary destructed; third temporary in-flight.
  EXPECT_THAT(gen.Wait(), Pointee(Eq(3)));
  EXPECT_EQ(destructor_calls, 2);

  // Generator is exhausted; the third temporary should be destructed. Note that
  // the promise does not construct any T values .
  EXPECT_EQ(gen.Wait(), nullptr);
  EXPECT_EQ(destructor_calls, 3);
}
