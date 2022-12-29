#include "diy/coro/async_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diy/coro/generator.h"
#include "diy/coro/task.h"

using testing::ElementsAre;
using testing::Eq;
using testing::Pointee;

template <typename T>
std::vector<T> ToVector(AsyncGenerator<T> gen) {
  return [](AsyncGenerator<T> gen) -> Task<std::vector<T>> {
    std::vector<T> out;
    while (T* value = co_await gen) {
      out.emplace_back(std::move(*value));
    }
    co_return out;
  }(std::move(gen))
                                          .Wait();
}

template <typename T>
T* NextValue(AsyncGenerator<T>& gen) {
  return Task(gen).Wait();
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

  EXPECT_THAT(NextValue(gen), Pointee(Eq(1)));
  EXPECT_THROW(NextValue(gen), std::invalid_argument);
}

TEST(AsyncGeneratorTest, WaitForFirstSuspension) {
  bool body_started = false;
  auto gen = [](bool& body_started) -> AsyncGenerator<int> {
    body_started = true;
    co_yield 1;
  }(body_started);
  EXPECT_FALSE(body_started);
  gen.WaitForFirstSuspension();
  EXPECT_TRUE(body_started);
  EXPECT_THAT(ToVector(std::move(gen)), ElementsAre(1));
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
  EXPECT_THAT(NextValue(gen), Pointee(Eq(1)));
  EXPECT_EQ(destructor_calls, 0);

  // First temporary destructed; second temporary in-flight.
  EXPECT_THAT(NextValue(gen), Pointee(Eq(2)));
  EXPECT_EQ(destructor_calls, 1);

  // Second temporary destructed; third temporary in-flight.
  EXPECT_THAT(NextValue(gen), Pointee(Eq(3)));
  EXPECT_EQ(destructor_calls, 2);

  // Generator is exhausted; the third temporary should be destructed. Note that
  // the promise does not construct any T values .
  EXPECT_EQ(NextValue(gen), nullptr);
  EXPECT_EQ(destructor_calls, 3);
}
