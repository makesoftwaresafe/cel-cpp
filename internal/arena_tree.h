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

// ArenaTree is a low level implementation of an RBTree tailored for use with
// google::protobuf::Arena. It avoids needing to deal with using a custom Allocator with
// std::set, std::map, absl::btree_set, or absl::btree_map, where the options
// are manually construct on the arena to avoid registering the destructor
// (technically undefined behavior) or registering the destructor and running it
// even though it is a no-op at runtime. Using absl::btree_set or
// absl::btree_map also does not preserve pointer stability and they have a
// higher overhead for small containers.
//
// Note that you should not use this unless you are really sure you need to. In
// most cases you should prefer std::set, std::map, absl::btree_set, or
// absl::btree_map.

#ifndef THIRD_PARTY_CEL_CPP_INTERNAL_ARENA_TREE_H_
#define THIRD_PARTY_CEL_CPP_INTERNAL_ARENA_TREE_H_

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#include "absl/log/absl_check.h"
#include "google/protobuf/arena.h"

namespace cel::internal {

enum class ArenaTreeNodeColor : uintptr_t {
  kBlack = 0,
  kRed = 1,
};

class ArenaTreeNodeBase;

[[nodiscard]]
const ArenaTreeNodeBase* ArenaTreeNext(const ArenaTreeNodeBase* node);

[[nodiscard]]
const ArenaTreeNodeBase* ArenaTreePrev(const ArenaTreeNodeBase* node);

[[nodiscard]]
const ArenaTreeNodeBase* ArenaTreeMin(const ArenaTreeNodeBase* node);

[[nodiscard]]
const ArenaTreeNodeBase* ArenaTreeMax(const ArenaTreeNodeBase* node);

template <typename T>
using IsDerivedFromArenaTreeNodeBase = std::conjunction<
    std::is_base_of<ArenaTreeNodeBase, T>,
    std::negation<std::is_same<ArenaTreeNodeBase, std::remove_cv_t<T>>>>;

template <typename T>
constexpr bool kIsDerivedFromArenaTreeNodeBase =
    IsDerivedFromArenaTreeNodeBase<T>::value;

template <typename T, typename U = void>
using EnableIfDerivedFromArenaTreeNodeBase =
    std::enable_if_t<kIsDerivedFromArenaTreeNodeBase<T>, U>;

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T*> ArenaTreeNext(T* node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreeNext)(static_cast<const ArenaTreeNodeBase*>(node))));
}

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T*> ArenaTreePrev(T* node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreePrev)(static_cast<const ArenaTreeNodeBase*>(node))));
}

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T*> ArenaTreeMin(T* node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreeMin)(static_cast<const ArenaTreeNodeBase*>(node))));
}

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T*> ArenaTreeMax(T* node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreeMax)(static_cast<const ArenaTreeNodeBase*>(node))));
}

[[nodiscard]]
ArenaTreeNodeBase* ArenaTreeNodeGetParent(const ArenaTreeNodeBase* node);

void ArenaTreeNodeSetParent(ArenaTreeNodeBase* node, ArenaTreeNodeBase* parent);

[[nodiscard]]
ArenaTreeNodeColor ArenaTreeNodeGetColor(const ArenaTreeNodeBase* node);

void ArenaTreeNodeSetColor(ArenaTreeNodeBase* node, ArenaTreeNodeColor color);

void ArenaTreeNodeSet(ArenaTreeNodeBase* node, ArenaTreeNodeBase* parent);

void ArenaTreeNodeCopy(ArenaTreeNodeBase* dst, const ArenaTreeNodeBase* src);

void ArenaTreeNodeClear(ArenaTreeNodeBase* node);

[[nodiscard]]
ArenaTreeNodeBase* ArenaTreeNodeGetLeft(const ArenaTreeNodeBase* node);

ArenaTreeNodeBase* ArenaTreeNodeSetLeft(ArenaTreeNodeBase* node,
                                        ArenaTreeNodeBase* left);

[[nodiscard]]
ArenaTreeNodeBase* ArenaTreeNodeGetRight(const ArenaTreeNodeBase* node);

ArenaTreeNodeBase* ArenaTreeNodeSetRight(ArenaTreeNodeBase* node,
                                         ArenaTreeNodeBase* right);

class ArenaTreeNodeBase {
 private:
  uintptr_t parent_and_color_ = 0;
  ArenaTreeNodeBase* left_ = nullptr;
  ArenaTreeNodeBase* right_ = nullptr;

  friend ArenaTreeNodeBase* ArenaTreeNodeGetParent(
      const ArenaTreeNodeBase* node);
  friend void ArenaTreeNodeSetParent(ArenaTreeNodeBase* node,
                                     ArenaTreeNodeBase* parent);
  friend ArenaTreeNodeColor ArenaTreeNodeGetColor(
      const ArenaTreeNodeBase* node);
  friend void ArenaTreeNodeSetColor(ArenaTreeNodeBase* node,
                                    ArenaTreeNodeColor color);
  friend void ArenaTreeNodeSet(ArenaTreeNodeBase* node,
                               ArenaTreeNodeBase* parent);
  friend void ArenaTreeNodeClear(ArenaTreeNodeBase* node);
  friend void ArenaTreeNodeCopy(ArenaTreeNodeBase* dst,
                                const ArenaTreeNodeBase* src);
  friend ArenaTreeNodeBase* ArenaTreeNodeGetLeft(const ArenaTreeNodeBase* node);
  friend ArenaTreeNodeBase* ArenaTreeNodeSetLeft(ArenaTreeNodeBase* node,
                                                 ArenaTreeNodeBase* left);
  friend ArenaTreeNodeBase* ArenaTreeNodeGetRight(
      const ArenaTreeNodeBase* node);
  friend ArenaTreeNodeBase* ArenaTreeNodeSetRight(ArenaTreeNodeBase* node,
                                                  ArenaTreeNodeBase* right);
};

[[nodiscard]]
inline ArenaTreeNodeBase* ArenaTreeNodeGetParent(
    const ArenaTreeNodeBase* node) {
  return reinterpret_cast<ArenaTreeNodeBase*>(node->parent_and_color_ &
                                              ~uintptr_t{1});
}

inline void ArenaTreeNodeSetParent(ArenaTreeNodeBase* node,
                                   ArenaTreeNodeBase* parent) {
  node->parent_and_color_ =
      static_cast<uintptr_t>(ArenaTreeNodeGetColor(node)) |
      reinterpret_cast<uintptr_t>(parent);
}

[[nodiscard]]
inline ArenaTreeNodeColor ArenaTreeNodeGetColor(const ArenaTreeNodeBase* node) {
  return static_cast<ArenaTreeNodeColor>(node->parent_and_color_ &
                                         uintptr_t{1});
}

inline void ArenaTreeNodeSetColor(ArenaTreeNodeBase* node,
                                  ArenaTreeNodeColor color) {
  node->parent_and_color_ =
      reinterpret_cast<uintptr_t>(ArenaTreeNodeGetParent(node)) |
      static_cast<uintptr_t>(color);
}

inline void ArenaTreeNodeSet(ArenaTreeNodeBase* node,
                             ArenaTreeNodeBase* parent) {
  node->parent_and_color_ = reinterpret_cast<uintptr_t>(parent) |
                            static_cast<uintptr_t>(ArenaTreeNodeColor::kRed);
  node->left_ = node->right_ = nullptr;
}

inline void ArenaTreeNodeClear(ArenaTreeNodeBase* node) {
  node->parent_and_color_ = 0;
  node->left_ = node->right_ = nullptr;
}

inline void ArenaTreeNodeCopy(ArenaTreeNodeBase* dst,
                              const ArenaTreeNodeBase* src) {
  dst->parent_and_color_ = src->parent_and_color_;
  dst->left_ = src->left_;
  dst->right_ = src->right_;
}

[[nodiscard]]
inline ArenaTreeNodeBase* ArenaTreeNodeGetLeft(const ArenaTreeNodeBase* node) {
  return node->left_;
}

inline ArenaTreeNodeBase* ArenaTreeNodeSetLeft(ArenaTreeNodeBase* node,
                                               ArenaTreeNodeBase* left) {
  return node->left_ = left;
}

[[nodiscard]]
inline ArenaTreeNodeBase* ArenaTreeNodeGetRight(const ArenaTreeNodeBase* node) {
  return node->right_;
}

inline ArenaTreeNodeBase* ArenaTreeNodeSetRight(ArenaTreeNodeBase* node,
                                                ArenaTreeNodeBase* right) {
  return node->right_ = right;
}

void ArenaTreeRemove(ArenaTreeNodeBase** head, ArenaTreeNodeBase* elem);

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T> ArenaTreeRemove(T** head,
                                                               T* elem) {
  ArenaTreeNodeBase* head_base = *head;
  (ArenaTreeRemove)(&head_base, static_cast<ArenaTreeNodeBase*>(elem));
  *head = static_cast<T*>(head_base);
}

void ArenaTreeInsertColor(ArenaTreeNodeBase** head, ArenaTreeNodeBase* elem);

template <typename T>
class ArenaTreeNode;

template <typename T>
[[nodiscard]]
const T& ArenaTreeNodeGetValue(const ArenaTreeNode<T>* node);

template <typename T>
class ArenaTreeNode : public ArenaTreeNodeBase {
 public:
  template <typename... Args>
  explicit ArenaTreeNode(Args&&... args)
      : ArenaTreeNodeBase(), value_(std::forward<Args>(args)...) {}

 private:
  template <typename U>
  friend const U& ArenaTreeNodeGetValue(const ArenaTreeNode<U>* node);

  T value_;
};

template <typename T>
[[nodiscard]] inline const T& ArenaTreeNodeGetValue(
    const ArenaTreeNode<T>* node) {
  return node->value_;
}

template <typename T>
class ArenaTreeNodeCrtp : public ArenaTreeNodeBase {
 public:
  using ArenaTreeNodeBase::ArenaTreeNodeBase;
};

template <typename T>
[[nodiscard]] inline const T& ArenaTreeNodeGetValue(
    const ArenaTreeNodeCrtp<T>* node) {
  return *static_cast<const T*>(node);
}

template <typename T, typename Compare>
[[nodiscard]]
T* ArenaTreeInsert(T** head, T* elem, const Compare& compare) {
  T* tmp = *head;
  T* parent = nullptr;
  int diff = 0;
  while (tmp != nullptr) {
    parent = tmp;
    diff = std::invoke(compare, (ArenaTreeNodeGetValue)(elem),
                       (ArenaTreeNodeGetValue)(parent));
    if (diff < 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetLeft)(tmp));
    } else if (diff > 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetRight)(tmp));
    } else {
      return tmp;
    }
  }
  (ArenaTreeNodeSet)(elem, parent);
  if (parent != nullptr) {
    if (diff < 0) {
      (ArenaTreeNodeSetLeft)(parent, elem);
    } else {
      (ArenaTreeNodeSetRight)(parent, elem);
    }
  } else {
    *head = elem;
  }
  ArenaTreeNodeBase* head_base = *head;
  (ArenaTreeInsertColor)(&head_base, elem);
  *head = static_cast<T*>(head_base);
  return elem;
}

template <typename T>
struct ArenaTreeNodeConstructor {
  template <typename... Args>
  void operator()(Args&&... args) const {
    ABSL_DCHECK(*out == nullptr);
    *out = google::protobuf::Arena::Create<T>(arena, std::forward<Args>(args)...);
  }

  google::protobuf::Arena* const arena;
  T** out;
};

template <typename T, typename K, typename Compare, typename Emplacer>
[[nodiscard]]
std::pair<T*, bool> ArenaTreeLazyEmplace(google::protobuf::Arena* arena, T** head,
                                         const K& key, const Compare& compare,
                                         Emplacer&& emplacer) {
  T* tmp = *head;
  T* parent = nullptr;
  int diff = 0;
  while (tmp != nullptr) {
    parent = tmp;
    diff = std::invoke(compare, key, (ArenaTreeNodeGetValue)(parent));
    if (diff < 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetLeft)(tmp));
    } else if (diff > 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetRight)(tmp));
    } else {
      return {tmp, false};
    }
  }
  T* elem = nullptr;
  ArenaTreeNodeConstructor<T> constructor{
      .arena = arena,
      .out = &elem,
  };
  std::invoke(std::forward<Emplacer>(emplacer),
              static_cast<const ArenaTreeNodeConstructor<T>&>(constructor));
  ABSL_DCHECK(elem != nullptr);
  (ArenaTreeNodeSet)(elem, parent);
  if (parent != nullptr) {
    if (diff < 0) {
      (ArenaTreeNodeSetLeft)(parent, elem);
    } else {
      (ArenaTreeNodeSetRight)(parent, elem);
    }
  } else {
    *head = elem;
  }
  ArenaTreeNodeBase* head_base = *head;
  (ArenaTreeInsertColor)(&head_base, elem);
  *head = static_cast<T*>(head_base);
  return {elem, true};
}

template <typename T, typename K, typename Compare>
[[nodiscard]]
const T* ArenaTreeFind(const T* head, const K& key, const Compare& compare) {
  const T* tmp = head;
  while (tmp != nullptr) {
    int diff = std::invoke(compare, key, (ArenaTreeNodeGetValue)(tmp));
    if (diff < 0) {
      tmp = static_cast<const T*>((ArenaTreeNodeGetLeft)(tmp));
    } else if (diff > 0) {
      tmp = static_cast<const T*>((ArenaTreeNodeGetRight)(tmp));
    } else {
      return tmp;
    }
  }
  return nullptr;
}

template <typename T, typename K, typename Compare>
[[nodiscard]]
T* ArenaTreeFind(T* head, const K& key, const Compare& compare) {
  return const_cast<T*>(
      (ArenaTreeFind)(static_cast<const T*>(head), key, compare));
}

}  // namespace cel::internal

#endif  // THIRD_PARTY_CEL_CPP_INTERNAL_ARENA_TREE_H_
