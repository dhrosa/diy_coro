#include "diy/coro/intrusive_linked_list.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::SizeIs;

struct Node {
  Node* next = nullptr;

  Node() = default;
  // Ensure the implementation makes no copies.
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;
};

using List = IntrusiveLinkedList<Node>;

static_assert(std::ranges::borrowed_range<List>);
static_assert(std::ranges::sized_range<List>);
static_assert(std::ranges::common_range<List>);
static_assert(std::ranges::forward_range<List>);

TEST(IntrusiveLinkedListTest, EmptyList) {
  List list;
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.size(), 0);
}

TEST(IntrusiveLinkedListTest, PushBack) {
  List list;
  Node a;
  list.push_back(a);

  EXPECT_THAT(list, SizeIs(1));
  EXPECT_EQ(&(*list.begin()), &a);
  EXPECT_EQ(a.next, nullptr);

  Node b;
  list.push_back(b);

  EXPECT_THAT(list, SizeIs(2));
  EXPECT_EQ(&(*list.begin()), &a);
  EXPECT_EQ(&(*++list.begin()), &b);
  EXPECT_EQ(a.next, &b);
  EXPECT_EQ(b.next, nullptr);
}
