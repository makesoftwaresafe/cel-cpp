// Copyright 2022 Google LLC
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

// IWYU pragma: private, include "base/type.h"

#ifndef THIRD_PARTY_CEL_CPP_BASE_INTERNAL_TYPE_POST_H_
#define THIRD_PARTY_CEL_CPP_BASE_INTERNAL_TYPE_POST_H_

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/hash/hash.h"
#include "absl/numeric/bits.h"
#include "base/handle.h"
#include "internal/rtti.h"

namespace cel {

namespace base_internal {

// Base implementation of persistent and transient handles for types. This
// contains implementation details shared among both, but is never used
// directly. The derived classes are responsible for defining appropriate
// constructors and assignments.
class TypeHandleBase {
 public:
  constexpr TypeHandleBase() = default;

  // Used by derived classes to bypass default construction to perform their own
  // construction.
  explicit TypeHandleBase(HandleInPlace) {}

  // Called by `Transient` and `Persistent` to implement the same operator. They
  // will handle enforcing const correctness.
  Type& operator*() const { return get(); }

  // Called by `Transient` and `Persistent` to implement the same operator. They
  // will handle enforcing const correctness.
  Type* operator->() const { return std::addressof(get()); }

  // Called by internal accessors `base_internal::IsXHandle`.
  constexpr bool IsManaged() const {
    return (rep_ & kTypeHandleUnmanaged) == 0;
  }

  // Called by internal accessors `base_internal::IsXHandle`.
  constexpr bool IsUnmanaged() const {
    return (rep_ & kTypeHandleUnmanaged) != 0;
  }

  // Called by internal accessors `base_internal::IsXHandle`.
  constexpr bool IsInlined() const { return false; }

  // Called by `Transient` and `Persistent` to implement the same function.
  template <typename T>
  bool Is() const {
    return static_cast<bool>(*this) && T::Is(static_cast<const Type&>(**this));
  }

  // Called by `Transient` and `Persistent` to implement the same operator.
  explicit operator bool() const { return (rep_ & kTypeHandleMask) != 0; }

  // Called by `Transient` and `Persistent` to implement the same operator.
  friend bool operator==(const TypeHandleBase& lhs, const TypeHandleBase& rhs) {
    if (static_cast<bool>(lhs) != static_cast<bool>(rhs) ||
        !static_cast<bool>(lhs)) {
      return false;
    }
    const Type& lhs_type = lhs.get();
    const Type& rhs_type = rhs.get();
    return lhs_type.Equals(rhs_type);
  }

  // Called by `Transient` and `Persistent` to implement std::swap.
  friend void swap(TypeHandleBase& lhs, TypeHandleBase& rhs) {
    std::swap(lhs.rep_, rhs.rep_);
  }

  template <typename H>
  friend H AbslHashValue(H state, const TypeHandleBase& handle) {
    if (ABSL_PREDICT_TRUE(static_cast<bool>(handle))) {
      handle.get().HashValue(absl::HashState::Create(&state));
    }
    return state;
  }

 private:
  template <HandleType H>
  friend class TypeHandle;

  void Unref() const {
    if ((rep_ & kTypeHandleUnmanaged) == 0) {
      get().Unref();
    }
  }

  uintptr_t Ref() const {
    if ((rep_ & kTypeHandleUnmanaged) == 0) {
      get().Ref();
    }
    return rep_;
  }

  Type& get() const { return *reinterpret_cast<Type*>(rep_ & kTypeHandleMask); }

  // There are no inlined types, so we represent everything as a pointer and use
  // tagging to differentiate between reference counted and arena-allocated.
  uintptr_t rep_ = kTypeHandleUnmanaged;
};

// All methods are called by `Persistent`.
template <>
class TypeHandle<HandleType::kPersistent> final : public TypeHandleBase {
 public:
  constexpr TypeHandle() = default;

  TypeHandle(const PersistentTypeHandle& other) { rep_ = other.Ref(); }

  TypeHandle(PersistentTypeHandle&& other) {
    rep_ = other.rep_;
    other.rep_ = kTypeHandleUnmanaged;
  }

  template <typename T, typename F>
  TypeHandle(UnmanagedResource<T>, F& from) : TypeHandleBase(kHandleInPlace) {
    uintptr_t rep = reinterpret_cast<uintptr_t>(
        static_cast<const Type*>(static_cast<const T*>(std::addressof(from))));
    ABSL_ASSERT(absl::countr_zero(rep) >=
                2);  // Verify the lower 2 bits are available.
    rep_ = rep | kTypeHandleUnmanaged;
  }

  template <typename T, typename F>
  TypeHandle(ManagedResource<T>, F& from) : TypeHandleBase(kHandleInPlace) {
    uintptr_t rep = reinterpret_cast<uintptr_t>(
        static_cast<const Type*>(static_cast<const T*>(std::addressof(from))));
    ABSL_ASSERT(absl::countr_zero(rep) >=
                2);  // Verify the lower 2 bits are available.
    rep_ = rep;
  }

  ~TypeHandle() { Unref(); }

  TypeHandle& operator=(const PersistentTypeHandle& other) {
    Unref();
    rep_ = other.Ref();
    return *this;
  }

  TypeHandle& operator=(PersistentTypeHandle&& other) {
    Unref();
    rep_ = other.rep_;
    other.rep_ = kTypeHandleUnmanaged;
    return *this;
  }
};

// Specialization for Type providing the implementation to `Persistent`.
template <>
struct HandleTraits<HandleType::kPersistent, Type> {
  using handle_type = TypeHandle<HandleType::kPersistent>;
};

// Partial specialization for `Persistent` for all classes derived from Type.
template <typename T>
struct HandleTraits<
    HandleType::kPersistent, T,
    std::enable_if_t<(std::is_base_of_v<Type, T> && !std::is_same_v<Type, T>)>>
    final : public HandleTraits<HandleType::kPersistent, Type> {};

}  // namespace base_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_INTERNAL_TYPE_POST_H_