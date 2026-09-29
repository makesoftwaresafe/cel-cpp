// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "internal/arena_tree.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

#include "internal/testing.h"
#include "google/protobuf/arena.h"

namespace cel::internal {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsNull;

using TestNode = ArenaTreeNode<int>;

struct TestNodeCompare {
  int operator()(int lhs, int rhs) const {
    if (lhs < rhs) {
      return -1;
    }
    if (lhs > rhs) {
      return 1;
    }
    return 0;
  }
};

struct TestNodeCrtp : ArenaTreeNodeCrtp<TestNodeCrtp> {
  explicit TestNodeCrtp(int value) : ArenaTreeNodeCrtp(), value(value) {}

  int value;
};

struct TestNodeCrtpCompare {
  int operator()(int lhs, int rhs) const {
    if (lhs < rhs) {
      return -1;
    }
    if (lhs > rhs) {
      return 1;
    }
    return 0;
  }

  int operator()(int lhs, const TestNodeCrtp& rhs) const {
    return (*this)(lhs, rhs.value);
  }

  int operator()(const TestNodeCrtp& lhs, int rhs) const {
    return (*this)(lhs.value, rhs);
  }

  int operator()(const TestNodeCrtp& lhs, const TestNodeCrtp& rhs) const {
    return (*this)(lhs.value, rhs.value);
  }
};

template <typename T, typename Compare>
int VerifySubtree(const T* node, const T* expected_parent, const T* lower_bound,
                  const T* upper_bound, const Compare& compare) {
  if (node == nullptr) {
    return 1;
  }
  EXPECT_EQ(ArenaTreeNodeGetParent(node), expected_parent);
  if (lower_bound != nullptr) {
    EXPECT_LT(compare(ArenaTreeNodeGetValue(lower_bound),
                      ArenaTreeNodeGetValue(node)),
              0);
  }
  if (upper_bound != nullptr) {
    EXPECT_LT(compare(ArenaTreeNodeGetValue(node),
                      ArenaTreeNodeGetValue(upper_bound)),
              0);
  }
  const auto* left = static_cast<const T*>(ArenaTreeNodeGetLeft(node));
  const auto* right = static_cast<const T*>(ArenaTreeNodeGetRight(node));
  if (ArenaTreeNodeGetColor(node) == ArenaTreeNodeColor::kRed) {
    if (left != nullptr) {
      EXPECT_EQ(ArenaTreeNodeGetColor(left), ArenaTreeNodeColor::kBlack);
    }
    if (right != nullptr) {
      EXPECT_EQ(ArenaTreeNodeGetColor(right), ArenaTreeNodeColor::kBlack);
    }
  }
  int left_black_height = VerifySubtree(left, node, lower_bound, node, compare);
  int right_black_height =
      VerifySubtree(right, node, node, upper_bound, compare);
  EXPECT_EQ(left_black_height, right_black_height);
  return left_black_height +
         (ArenaTreeNodeGetColor(node) == ArenaTreeNodeColor::kBlack ? 1 : 0);
}

template <typename T, typename Compare>
std::vector<int> VerifyTree(T* head, const Compare& compare) {
  if (head == nullptr) {
    EXPECT_THAT(ArenaTreeMin(head), IsNull());
    EXPECT_THAT(ArenaTreeMax(head), IsNull());
    EXPECT_THAT(ArenaTreeNext(head), IsNull());
    EXPECT_THAT(ArenaTreePrev(head), IsNull());
    return {};
  }
  EXPECT_EQ(ArenaTreeNodeGetColor(head), ArenaTreeNodeColor::kBlack);
  VerifySubtree(static_cast<const T*>(head), static_cast<const T*>(nullptr),
                static_cast<const T*>(nullptr), static_cast<const T*>(nullptr),
                compare);

  std::vector<const T*> forward;
  for (const T* curr = ArenaTreeMin(static_cast<const T*>(head));
       curr != nullptr; curr = static_cast<const T*>(ArenaTreeNext(curr))) {
    forward.push_back(curr);
    EXPECT_EQ(ArenaTreeFind(static_cast<const T*>(head),
                            ArenaTreeNodeGetValue(curr), compare),
              curr);
    EXPECT_EQ(ArenaTreeFind(head, ArenaTreeNodeGetValue(curr), compare), curr);
  }
  EXPECT_FALSE(forward.empty());
  EXPECT_EQ(forward.front(), ArenaTreeMin(head));
  EXPECT_EQ(forward.back(), ArenaTreeMax(head));

  std::vector<T*> backward;
  for (T* curr = ArenaTreeMax(head); curr != nullptr;
       curr = ArenaTreePrev(curr)) {
    backward.push_back(curr);
  }
  std::reverse(backward.begin(), backward.end());
  EXPECT_EQ(forward.size(), backward.size());
  for (size_t i = 0; i < forward.size(); ++i) {
    EXPECT_EQ(forward[i], backward[i]);
  }

  std::vector<int> values;
  values.reserve(forward.size());
  for (const T* node : forward) {
    if constexpr (std::is_same_v<T, TestNode>) {
      values.push_back(ArenaTreeNodeGetValue(node));
    } else {
      values.push_back(ArenaTreeNodeGetValue(node).value);
    }
  }
  return values;
}

TEST(ArenaTree, Empty) {
  TestNode* head = nullptr;
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, Single) {
  TestNode* head = nullptr;
  auto node1 = std::make_unique<TestNode>(1);
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(head), nullptr);
  EXPECT_EQ(ArenaTreeNext(head), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());
  EXPECT_EQ(ArenaTreeFind(head, 1, TestNodeCompare{}), node1.get());
  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
  EXPECT_THAT(ArenaTreeFind(head, 1, TestNodeCompare{}), IsNull());
}

TEST(ArenaTree, Couple) {
  TestNode* head = nullptr;
  auto node1 = std::make_unique<TestNode>(1);
  auto node2 = std::make_unique<TestNode>(2);

  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node2.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());

  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, CrtpEmpty) {
  TestNodeCrtp* head = nullptr;
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, CrtpSingle) {
  TestNodeCrtp* head = nullptr;
  auto node1 = std::make_unique<TestNodeCrtp>(1);
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCrtpCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(head), nullptr);
  EXPECT_EQ(ArenaTreeNext(head), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());
  EXPECT_EQ(ArenaTreeFind(head, 1, TestNodeCrtpCompare{}), node1.get());
  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
  EXPECT_THAT(ArenaTreeFind(head, 1, TestNodeCrtpCompare{}), IsNull());
}

TEST(ArenaTree, CrtpCouple) {
  TestNodeCrtp* head = nullptr;
  auto node1 = std::make_unique<TestNodeCrtp>(1);
  auto node2 = std::make_unique<TestNodeCrtp>(2);

  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCrtpCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCrtpCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node2.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());

  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCrtpCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCrtpCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, InsertDuplicateReturnsExistingNode) {
  TestNode* head = nullptr;
  auto node2 = std::make_unique<TestNode>(2);
  auto node1 = std::make_unique<TestNode>(1);
  auto node3 = std::make_unique<TestNode>(3);
  auto dup2 = std::make_unique<TestNode>(2);
  auto dup1 = std::make_unique<TestNode>(1);
  auto dup3 = std::make_unique<TestNode>(3);

  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node3.get(), TestNodeCompare{}),
            node3.get());

  EXPECT_EQ(ArenaTreeInsert(&head, dup2.get(), TestNodeCompare{}), node2.get());
  EXPECT_EQ(ArenaTreeInsert(&head, dup1.get(), TestNodeCompare{}), node1.get());
  EXPECT_EQ(ArenaTreeInsert(&head, dup3.get(), TestNodeCompare{}), node3.get());
  EXPECT_THAT(VerifyTree(head, TestNodeCompare{}), ElementsAre(1, 2, 3));
}

TEST(ArenaTree, LazyEmplace) {
  google::protobuf::Arena arena;
  TestNode* head = nullptr;

  int emplace_calls = 0;
  auto emplace_val = [&](int key) {
    return ArenaTreeLazyEmplace(&arena, &head, key, TestNodeCompare{},
                                [&](const auto& ctor) {
                                  ++emplace_calls;
                                  ctor(key);
                                });
  };

  auto [node2, inserted2] = emplace_val(2);
  EXPECT_TRUE(inserted2);
  EXPECT_EQ(ArenaTreeNodeGetValue(node2), 2);
  EXPECT_EQ(emplace_calls, 1);

  auto [node1, inserted1] = emplace_val(1);
  EXPECT_TRUE(inserted1);
  EXPECT_EQ(ArenaTreeNodeGetValue(node1), 1);
  EXPECT_EQ(emplace_calls, 2);

  auto [node3, inserted3] = emplace_val(3);
  EXPECT_TRUE(inserted3);
  EXPECT_EQ(ArenaTreeNodeGetValue(node3), 3);
  EXPECT_EQ(emplace_calls, 3);

  // Duplicate keys should not invoke the emplacer.
  auto [dup2, dup_inserted2] = emplace_val(2);
  EXPECT_FALSE(dup_inserted2);
  EXPECT_EQ(dup2, node2);
  EXPECT_EQ(emplace_calls, 3);

  auto [dup1, dup_inserted1] = emplace_val(1);
  EXPECT_FALSE(dup_inserted1);
  EXPECT_EQ(dup1, node1);
  EXPECT_EQ(emplace_calls, 3);

  auto [dup3, dup_inserted3] = emplace_val(3);
  EXPECT_FALSE(dup_inserted3);
  EXPECT_EQ(dup3, node3);
  EXPECT_EQ(emplace_calls, 3);

  EXPECT_THAT(VerifyTree(head, TestNodeCompare{}), ElementsAre(1, 2, 3));
}

TEST(ArenaTree, CrtpLazyEmplace) {
  google::protobuf::Arena arena;
  TestNodeCrtp* head = nullptr;

  int emplace_calls = 0;
  auto emplace_val = [&](int key) {
    return ArenaTreeLazyEmplace(&arena, &head, key, TestNodeCrtpCompare{},
                                [&](const auto& ctor) {
                                  ++emplace_calls;
                                  ctor(key);
                                });
  };

  auto [node2, inserted2] = emplace_val(2);
  EXPECT_TRUE(inserted2);
  EXPECT_EQ(ArenaTreeNodeGetValue(node2).value, 2);
  EXPECT_EQ(emplace_calls, 1);

  auto [node1, inserted1] = emplace_val(1);
  EXPECT_TRUE(inserted1);
  EXPECT_EQ(ArenaTreeNodeGetValue(node1).value, 1);
  EXPECT_EQ(emplace_calls, 2);

  auto [node3, inserted3] = emplace_val(3);
  EXPECT_TRUE(inserted3);
  EXPECT_EQ(ArenaTreeNodeGetValue(node3).value, 3);
  EXPECT_EQ(emplace_calls, 3);

  auto [dup2, dup_inserted2] = emplace_val(2);
  EXPECT_FALSE(dup_inserted2);
  EXPECT_EQ(dup2, node2);
  EXPECT_EQ(emplace_calls, 3);

  EXPECT_THAT(VerifyTree(head, TestNodeCrtpCompare{}), ElementsAre(1, 2, 3));
}

TEST(ArenaTree, FindLeftRightAndMissing) {
  google::protobuf::Arena arena;
  TestNode* head = nullptr;
  for (int key : {8, 4, 12, 2, 6, 10, 14}) {
    auto [node, inserted] =
        ArenaTreeLazyEmplace(&arena, &head, key, TestNodeCompare{},
                             [key](const auto& ctor) { ctor(key); });
    EXPECT_TRUE(inserted);
  }

  const TestNode* const_head = head;
  for (int key : {2, 4, 6, 8, 10, 12, 14}) {
    TestNode* found = ArenaTreeFind(head, key, TestNodeCompare{});
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(ArenaTreeNodeGetValue(found), key);
    EXPECT_EQ(ArenaTreeFind(const_head, key, TestNodeCompare{}), found);
  }
  for (int missing : {1, 3, 5, 7, 9, 11, 13, 15}) {
    EXPECT_THAT(ArenaTreeFind(head, missing, TestNodeCompare{}), IsNull());
    EXPECT_THAT(ArenaTreeFind(const_head, missing, TestNodeCompare{}),
                IsNull());
  }
}

TEST(ArenaTree, InsertAndRemoveAscendingAndDescending) {
  constexpr int kCount = 64;
  std::vector<std::unique_ptr<TestNode>> nodes;
  nodes.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    nodes.push_back(std::make_unique<TestNode>(i));
  }

  // Insert ascending, remove ascending.
  TestNode* head = nullptr;
  for (int i = 0; i < kCount; ++i) {
    EXPECT_EQ(ArenaTreeInsert(&head, nodes[i].get(), TestNodeCompare{}),
              nodes[i].get());
    VerifyTree(head, TestNodeCompare{});
  }
  for (int i = 0; i < kCount; ++i) {
    ArenaTreeRemove(&head, nodes[i].get());
    VerifyTree(head, TestNodeCompare{});
  }
  EXPECT_THAT(head, IsNull());

  // Insert descending, remove descending.
  for (int i = kCount - 1; i >= 0; --i) {
    EXPECT_EQ(ArenaTreeInsert(&head, nodes[i].get(), TestNodeCompare{}),
              nodes[i].get());
    VerifyTree(head, TestNodeCompare{});
  }
  for (int i = kCount - 1; i >= 0; --i) {
    ArenaTreeRemove(&head, nodes[i].get());
    VerifyTree(head, TestNodeCompare{});
  }
  EXPECT_THAT(head, IsNull());

  // Insert ascending, remove descending.
  for (int i = 0; i < kCount; ++i) {
    EXPECT_EQ(ArenaTreeInsert(&head, nodes[i].get(), TestNodeCompare{}),
              nodes[i].get());
  }
  VerifyTree(head, TestNodeCompare{});
  for (int i = kCount - 1; i >= 0; --i) {
    ArenaTreeRemove(&head, nodes[i].get());
    VerifyTree(head, TestNodeCompare{});
  }
  EXPECT_THAT(head, IsNull());

  // Insert descending, remove ascending.
  for (int i = kCount - 1; i >= 0; --i) {
    EXPECT_EQ(ArenaTreeInsert(&head, nodes[i].get(), TestNodeCompare{}),
              nodes[i].get());
  }
  VerifyTree(head, TestNodeCompare{});
  for (int i = 0; i < kCount; ++i) {
    ArenaTreeRemove(&head, nodes[i].get());
    VerifyTree(head, TestNodeCompare{});
  }
  EXPECT_THAT(head, IsNull());
}

TEST(ArenaTree, InsertZigZagAndRemoveInternalNodes) {
  constexpr int kCount = 63;
  std::vector<std::unique_ptr<TestNode>> nodes;
  nodes.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    nodes.push_back(std::make_unique<TestNode>(i));
  }

  // Insert in a zig-zag pattern (alternating low and high values) to exercise
  // Left-Right and Right-Left triangle rotations in ArenaTreeInsertColor.
  TestNode* head = nullptr;
  int lo = 0;
  int hi = kCount - 1;
  while (lo <= hi) {
    EXPECT_EQ(ArenaTreeInsert(&head, nodes[lo].get(), TestNodeCompare{}),
              nodes[lo].get());
    VerifyTree(head, TestNodeCompare{});
    if (lo < hi) {
      EXPECT_EQ(ArenaTreeInsert(&head, nodes[hi].get(), TestNodeCompare{}),
                nodes[hi].get());
      VerifyTree(head, TestNodeCompare{});
    }
    ++lo;
    --hi;
  }

  // Repeatedly remove left child of root, right child of root, and root itself
  // to cover 2-child removals where old_parent is null, old is a left child,
  // and old is a right child.
  int step = 0;
  while (head != nullptr) {
    TestNode* target = head;
    if (step % 3 == 0 && ArenaTreeNodeGetLeft(head) != nullptr) {
      target = static_cast<TestNode*>(ArenaTreeNodeGetLeft(head));
    } else if (step % 3 == 1 && ArenaTreeNodeGetRight(head) != nullptr) {
      target = static_cast<TestNode*>(ArenaTreeNodeGetRight(head));
    }
    ArenaTreeRemove(&head, target);
    VerifyTree(head, TestNodeCompare{});
    ++step;
  }
}

TEST(ArenaTree, RemoveSingleLeftAndSingleRightChildCases) {
  // Specifically exercise ArenaTreeRemove when the removed node has only a left
  // child or only a right child (as root, left child of parent, and right child
  // of parent).
  std::vector<std::unique_ptr<TestNode>> nodes;
  for (int i = 0; i < 10; ++i) {
    nodes.push_back(std::make_unique<TestNode>(i));
  }

  // Case 1: Node with only a left child as left child of parent and right child
  // of parent.
  TestNode* head = nullptr;
  for (int idx : {5, 2, 8, 1, 7}) {
    EXPECT_EQ(ArenaTreeInsert(&head, nodes[idx].get(), TestNodeCompare{}),
              nodes[idx].get());
  }
  // Node 2 has only left child 1; Node 8 has only left child 7.
  ArenaTreeRemove(&head, nodes[2].get());
  VerifyTree(head, TestNodeCompare{});
  ArenaTreeRemove(&head, nodes[8].get());
  VerifyTree(head, TestNodeCompare{});
  while (head != nullptr) {
    ArenaTreeRemove(&head, head);
  }

  // Case 2: Node with only a right child as left child of parent and right
  // child of parent.
  for (int idx : {5, 2, 8, 3, 9}) {
    EXPECT_EQ(ArenaTreeInsert(&head, nodes[idx].get(), TestNodeCompare{}),
              nodes[idx].get());
  }
  // Node 2 has only right child 3; Node 8 has only right child 9.
  ArenaTreeRemove(&head, nodes[2].get());
  VerifyTree(head, TestNodeCompare{});
  ArenaTreeRemove(&head, nodes[8].get());
  VerifyTree(head, TestNodeCompare{});
  while (head != nullptr) {
    ArenaTreeRemove(&head, head);
  }
}

TEST(ArenaTree, AllPermutationsInsertAndRemove) {
  constexpr int kPermSize = 6;
  std::vector<std::unique_ptr<TestNode>> nodes;
  nodes.reserve(kPermSize);
  std::vector<int> insert_order(kPermSize);
  for (int i = 0; i < kPermSize; ++i) {
    nodes.push_back(std::make_unique<TestNode>(i));
    insert_order[i] = i;
  }

  do {
    TestNode* head = nullptr;
    for (int idx : insert_order) {
      EXPECT_EQ(ArenaTreeInsert(&head, nodes[idx].get(), TestNodeCompare{}),
                nodes[idx].get());
    }
    EXPECT_THAT(VerifyTree(head, TestNodeCompare{}),
                ElementsAre(0, 1, 2, 3, 4, 5));

    // Remove in insertion order.
    for (int idx : insert_order) {
      ArenaTreeRemove(&head, nodes[idx].get());
    }
    EXPECT_THAT(VerifyTree(head, TestNodeCompare{}), IsEmpty());

    // Re-insert and remove in reverse insertion order.
    for (int idx : insert_order) {
      EXPECT_EQ(ArenaTreeInsert(&head, nodes[idx].get(), TestNodeCompare{}),
                nodes[idx].get());
    }
    for (int i = kPermSize - 1; i >= 0; --i) {
      ArenaTreeRemove(&head, nodes[insert_order[i]].get());
    }
    EXPECT_THAT(VerifyTree(head, TestNodeCompare{}), IsEmpty());
  } while (std::next_permutation(insert_order.begin(), insert_order.end()));
}

}  // namespace
}  // namespace cel::internal
