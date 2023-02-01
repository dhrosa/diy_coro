#include "diy/coro/container_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::ElementsAre;

TEST(VectorGeneratorTest, PropagatesException) {
  auto gen = []() -> VectorGenerator<int> {
    throw std::logic_error("fake error");
  };

  EXPECT_THROW(gen(), std::logic_error);
}

TEST(VectorGeneratorTest, PropagatesValues) {
  const std::vector<int> values = []() -> VectorGenerator<int> {
    co_yield 1;
    co_yield 2;
    co_yield 3;
  }();

  EXPECT_THAT(values, ElementsAre(1, 2, 3));
}
