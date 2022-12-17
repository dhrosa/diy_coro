#include "generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <ranges>

using testing::ElementsAre;

TEST(GeneratorTest, Iota) {
  auto gen = []() -> Generator<int> {
    for (int i = 0; true; ++i) {
      co_yield i;
    }
  }();

  EXPECT_EQ(gen(), 0);
  EXPECT_EQ(gen(), 1);
  EXPECT_EQ(gen(), 2);
}

TEST(GeneratorTest, ExceptionProagated) {
  auto gen = []() -> Generator<int> {
    co_yield 0;
    co_yield 1;
    throw std::runtime_error("fake exception");
  }();

  EXPECT_EQ(gen(), 0);
  EXPECT_EQ(gen(), 1);
  EXPECT_THROW(gen(), std::runtime_error);
}

template <std::ranges::range R>
auto ToVector(R&& gen) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> values(gen.begin(), gen.end());
  // std::ranges::copy(gen, std::back_inserter(values));
  return values;
}

TEST(IteratorTest, Finite) {
  auto gen = []() -> Generator<int> { co_yield 0; }();

  EXPECT_THAT(ToVector(gen), ElementsAre(0));
}

TEST(IteratorTest, Iota) {
  auto gen = []() -> Generator<int> {
    for (int i = 0; true; ++i) {
      co_yield i;
    }
  }();

  // EXPECT_THAT(ToVector(gen | std::views::take(3)), ElementsAre(0, 1, 2));
}
