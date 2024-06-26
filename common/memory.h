// Copyright 2023 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_MEMORY_H_
#define THIRD_PARTY_CEL_CPP_COMMON_MEMORY_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/numeric/bits.h"
#include "common/allocator.h"
#include "common/arena.h"
#include "common/data.h"
#include "common/internal/metadata.h"
#include "common/internal/reference_count.h"
#include "common/native_type.h"
#include "common/reference_count.h"
#include "internal/exceptions.h"
#include "internal/to_address.h"
#include "google/protobuf/arena.h"

namespace cel {

// Obtain the address of the underlying element from a raw pointer or "fancy"
// pointer.
using internal::to_address;

// MemoryManagement is an enumeration of supported memory management forms
// underlying `cel::MemoryManager`.
enum class MemoryManagement {
  // Region-based (a.k.a. arena). Memory is allocated in fixed size blocks and
  // deallocated all at once upon destruction of the `cel::MemoryManager`.
  kPooling = 1,
  // Reference counting. Memory is allocated with an associated reference
  // counter. When the reference counter hits 0, it is deallocated.
  kReferenceCounting,
};

std::ostream& operator<<(std::ostream& out, MemoryManagement memory_management);

class ABSL_ATTRIBUTE_TRIVIAL_ABI [[nodiscard]] Owner;
class Borrower;
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI Shared;
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI SharedView;
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI [[nodiscard]] Unique;
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI [[nodiscard]] Owned;
template <typename T>
class Borrowed;
template <typename T>
struct Ownable;
template <typename T>
struct Borrowable;
template <typename T>
struct EnableSharedFromThis;

class MemoryManager;
class ReferenceCountingMemoryManager;
class PoolingMemoryManager;

namespace common_internal {
template <typename T>
inline constexpr bool kNotMessageLiteAndNotData =
    std::conjunction_v<std::negation<std::is_base_of<google::protobuf::MessageLite, T>>,
                       std::negation<std::is_base_of<Data, T>>>;
template <typename To, typename From>
inline constexpr bool kIsPointerConvertible = std::is_convertible_v<From*, To*>;
template <typename To, typename From>
inline constexpr bool kNotSameAndIsPointerConvertible =
    std::conjunction_v<std::negation<std::is_same<To, From>>,
                       std::bool_constant<kIsPointerConvertible<To, From>>>;

// Clears the contents of `owner`, and returns the reference count if in use.
absl::Nullable<const ReferenceCount*> OwnerRelease(Owner owner) noexcept;

// Given a pointer to `T` and a reference count, returns an appropriate tagged
// pointer as used by `cel::Owner`. If `acquire` is `true`, increments the
// reference count when present.
template <typename T>
uintptr_t EncodeOwnerImpl(absl::Nullable<const T*> ptr,
                          absl::Nullable<const ReferenceCount*> refcount,
                          bool acquire = true) noexcept;
uintptr_t EncodeOwner(absl::Nullable<const google::protobuf::MessageLite*> ptr,
                      absl::Nullable<const ReferenceCount*> refcount,
                      bool acquire = true) noexcept;
uintptr_t EncodeOwner(absl::Nullable<const Data*> ptr,
                      absl::Nullable<const ReferenceCount*> refcount,
                      bool acquire = true) noexcept;
template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, uintptr_t> EncodeOwner(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount,
    bool acquire = true) noexcept;

template <typename T>
Owner MakeOwnerImpl(absl::Nullable<const T*> ptr,
                    absl::Nullable<const ReferenceCount*> refcount,
                    bool acquire = true) noexcept;
Owner MakeOwner(absl::Nullable<const google::protobuf::MessageLite*> ptr,
                absl::Nullable<const ReferenceCount*> refcount,
                bool acquire = true) noexcept;
Owner MakeOwner(absl::Nullable<const Data*> ptr,
                absl::Nullable<const ReferenceCount*> refcount,
                bool acquire = true) noexcept;
template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, Owner> MakeOwner(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount,
    bool acquire = true) noexcept;

// Clears the contents of `borrower`, and returns the reference count if in use.
absl::Nullable<const ReferenceCount*> BorrowerRelease(
    Borrower borrower) noexcept;

// Given a pointer to `T` and a reference count, returns an appropriate tagged
// pointer as used by `cel::Borrower`.
uintptr_t EncodeBorrower(
    absl::Nullable<const google::protobuf::MessageLite*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept;
uintptr_t EncodeBorrower(
    absl::Nullable<const Data*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept;
template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, uintptr_t> EncodeBorrower(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept;

template <typename T>
Borrower MakeBorrowerImpl(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept;
Borrower MakeBorrower(absl::Nullable<const google::protobuf::MessageLite*> ptr,
                      absl::Nullable<const ReferenceCount*> refcount) noexcept;
Borrower MakeBorrower(absl::Nullable<const Data*> ptr,
                      absl::Nullable<const ReferenceCount*> refcount) noexcept;
template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, Borrower> MakeBorrower(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept;

template <typename T>
T* GetPointer(const Shared<T>& shared);
template <typename T>
const ReferenceCount* GetReferenceCount(const Shared<T>& shared);
template <typename T>
Shared<T> MakeShared(AdoptRef, T* value, const ReferenceCount* refcount);
template <typename T>
Shared<T> MakeShared(T* value, const ReferenceCount* refcount);
template <typename T>
T* GetPointer(SharedView<T> shared);
template <typename T>
const ReferenceCount* GetReferenceCount(SharedView<T> shared);
template <typename T>
SharedView<T> MakeSharedView(T* value, const ReferenceCount* refcount);
}  // namespace common_internal

template <typename To, typename From>
Shared<To> StaticCast(const Shared<From>& from);
template <typename To, typename From>
Shared<To> StaticCast(Shared<From>&& from);
template <typename To, typename From>
SharedView<To> StaticCast(SharedView<From> from);

// `Owner` represents a reference to some co-owned data, of which this owner is
// one of the co-owners. When using reference counting, `Owner` performs
// increment/decrement where appropriate similar to `std::shared_ptr`.
// `Borrower` is similar to `Owner`, except that it is always trivially
// copyable/destructible. In that sense, `Borrower` is similar to
// `std::reference_wrapper<const Owner>`.
class ABSL_ATTRIBUTE_TRIVIAL_ABI [[nodiscard]] Owner final {
 private:
  static constexpr uintptr_t kNone = common_internal::kMetadataOwnerNone;
  static constexpr uintptr_t kReferenceCountBit =
      common_internal::kMetadataOwnerReferenceCountBit;
  static constexpr uintptr_t kArenaBit =
      common_internal::kMetadataOwnerArenaBit;
  static constexpr uintptr_t kBits = common_internal::kMetadataOwnerBits;
  static constexpr uintptr_t kPointerMask =
      common_internal::kMetadataOwnerPointerMask;

 public:
  static Owner None() noexcept { return Owner(); }

  static Owner Allocator(Allocator<> allocator) noexcept {
    auto* arena = allocator.arena();
    return arena != nullptr ? Arena(arena) : None();
  }

  static Owner Arena(absl::Nonnull<google::protobuf::Arena*> arena
                         ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
    ABSL_DCHECK(arena != nullptr);
    return Owner(reinterpret_cast<uintptr_t>(arena) | kArenaBit);
  }

  static Owner Arena(std::nullptr_t) = delete;

  static Owner ReferenceCount(
      absl::Nonnull<const ReferenceCount*> reference_count
          ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
    ABSL_DCHECK(reference_count != nullptr);
    common_internal::StrongRef(*reference_count);
    return Owner(reinterpret_cast<uintptr_t>(reference_count) |
                 kReferenceCountBit);
  }

  static Owner ReferenceCount(std::nullptr_t) = delete;

  Owner() = default;

  Owner(const Owner& other) noexcept : Owner(CopyFrom(other.ptr_)) {}

  Owner(Owner&& other) noexcept : Owner(MoveFrom(other.ptr_)) {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owner(const Owned<T>& owned) noexcept;

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owner(Owned<T>&& owned) noexcept;

  explicit Owner(Borrower borrower) noexcept;

  template <typename T>
  explicit Owner(Borrowed<T> borrowed) noexcept;

  ~Owner() { Destroy(ptr_); }

  Owner& operator=(const Owner& other) noexcept {
    if (ptr_ != other.ptr_) {
      Destroy(ptr_);
      ptr_ = CopyFrom(other.ptr_);
    }
    return *this;
  }

  Owner& operator=(Owner&& other) noexcept {
    if (ABSL_PREDICT_TRUE(this != &other)) {
      Destroy(ptr_);
      ptr_ = MoveFrom(other.ptr_);
    }
    return *this;
  }

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owner& operator=(const Owned<T>& owned) noexcept;

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owner& operator=(Owned<T>&& owned) noexcept;

  explicit operator bool() const noexcept { return !IsNone(ptr_); }

  absl::Nullable<google::protobuf::Arena*> arena() const noexcept {
    return (ptr_ & Owner::kBits) == Owner::kArenaBit
               ? reinterpret_cast<google::protobuf::Arena*>(ptr_ & Owner::kPointerMask)
               : nullptr;
  }

  // Tests whether two owners have ownership over the same data, that is they
  // are co-owners.
  friend bool operator==(const Owner& lhs, const Owner& rhs) noexcept {
    // A reference count and arena can never occupy the same memory address, so
    // we can compare for equality without masking off the bits.
    return lhs.ptr_ == rhs.ptr_;
  }

 private:
  friend class Borrower;
  template <typename U>
  friend struct Ownable;
  friend absl::Nullable<const common_internal::ReferenceCount*>
  common_internal::OwnerRelease(Owner owner) noexcept;
  template <typename T>
  friend Owner common_internal::MakeOwnerImpl(
      absl::Nullable<const T*> ptr,
      absl::Nullable<const common_internal::ReferenceCount*> refcount,
      bool acquire) noexcept;
  friend absl::Nullable<const common_internal::ReferenceCount*>
  common_internal::BorrowerRelease(Borrower borrower) noexcept;

  constexpr explicit Owner(uintptr_t ptr) noexcept : ptr_(ptr) {}

  static constexpr bool IsNone(uintptr_t ptr) noexcept { return ptr == kNone; }

  static constexpr bool IsArena(uintptr_t ptr) noexcept {
    return (ptr & kArenaBit) != kNone;
  }

  static constexpr bool IsReferenceCount(uintptr_t ptr) noexcept {
    return (ptr & kReferenceCountBit) != kNone;
  }

  ABSL_ATTRIBUTE_RETURNS_NONNULL
  static absl::Nonnull<google::protobuf::Arena*> AsArena(uintptr_t ptr) noexcept {
    ABSL_ASSERT(IsArena(ptr));
    return reinterpret_cast<google::protobuf::Arena*>(ptr & kPointerMask);
  }

  ABSL_ATTRIBUTE_RETURNS_NONNULL
  static absl::Nonnull<const common_internal::ReferenceCount*> AsReferenceCount(
      uintptr_t ptr) noexcept {
    ABSL_ASSERT(IsReferenceCount(ptr));
    return reinterpret_cast<const common_internal::ReferenceCount*>(
        ptr & kPointerMask);
  }

  static uintptr_t CopyFrom(uintptr_t other) noexcept { return Own(other); }

  static uintptr_t MoveFrom(uintptr_t& other) noexcept {
    return std::exchange(other, kNone);
  }

  static void Destroy(uintptr_t ptr) noexcept { Unown(ptr); }

  static uintptr_t Own(uintptr_t ptr) noexcept {
    if (IsReferenceCount(ptr)) {
      const auto* refcount = Owner::AsReferenceCount(ptr);
      ABSL_ASSUME(refcount != nullptr);
      common_internal::StrongRef(refcount);
    }
    return ptr;
  }

  static void Unown(uintptr_t ptr) noexcept {
    if (IsReferenceCount(ptr)) {
      const auto* reference_count = AsReferenceCount(ptr);
      ABSL_ASSUME(reference_count != nullptr);
      common_internal::StrongUnref(reference_count);
    }
  }

  uintptr_t ptr_ = kNone;
};

inline bool operator!=(const Owner& lhs, const Owner& rhs) noexcept {
  return !operator==(lhs, rhs);
}

namespace common_internal {

inline absl::Nullable<const ReferenceCount*> OwnerRelease(
    Owner owner) noexcept {
  uintptr_t ptr = std::exchange(owner.ptr_, kMetadataOwnerNone);
  if (Owner::IsReferenceCount(ptr)) {
    return Owner::AsReferenceCount(ptr);
  }
  return nullptr;
}

inline uintptr_t EncodeOwnerReferenceCount(
    absl::Nonnull<const ReferenceCount*> refcount, bool acquire) noexcept {
  if (acquire) {
    StrongRef(*refcount);
  }
  return reinterpret_cast<uintptr_t>(refcount) |
         kMetadataOwnerReferenceCountBit;
}

template <typename T>
uintptr_t EncodeOwnerImpl(absl::Nullable<const T*> ptr,
                          absl::Nullable<const ReferenceCount*> refcount,
                          bool acquire) noexcept {
  if (refcount != nullptr) {
    return EncodeOwnerReferenceCount(refcount, acquire);
  }
  if (ptr != nullptr) {
    if (google::protobuf::Arena* arena = ptr->GetArena(); arena != nullptr) {
      return reinterpret_cast<uintptr_t>(arena) | kMetadataOwnerArenaBit;
    }
  }
  return kMetadataOwnerNone;
}

inline uintptr_t EncodeOwner(absl::Nullable<const google::protobuf::MessageLite*> ptr,
                             absl::Nullable<const ReferenceCount*> refcount,
                             bool acquire) noexcept {
  return (EncodeOwnerImpl)(ptr, refcount, acquire);
}

inline uintptr_t EncodeOwner(absl::Nullable<const Data*> ptr,
                             absl::Nullable<const ReferenceCount*> refcount,
                             bool acquire) noexcept {
  return (EncodeOwnerImpl)(ptr, refcount, acquire);
}

template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, uintptr_t> EncodeOwner(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount, bool acquire) noexcept {
  static_assert(IsArenaConstructible<T>::value,
                "T must be arena constructable");
  return (EncodeOwnerImpl)(ptr, refcount, acquire);
}

template <typename T>
Owner MakeOwnerImpl(absl::Nullable<const T*> ptr,
                    absl::Nullable<const ReferenceCount*> refcount,
                    bool acquire) noexcept {
  return Owner((EncodeOwner)(ptr, refcount, acquire));
}

inline Owner MakeOwner(absl::Nullable<const google::protobuf::MessageLite*> ptr,
                       absl::Nullable<const ReferenceCount*> refcount,
                       bool acquire) noexcept {
  return (MakeOwnerImpl)(ptr, refcount, acquire);
}

inline Owner MakeOwner(absl::Nullable<const Data*> ptr,
                       absl::Nullable<const ReferenceCount*> refcount,
                       bool acquire) noexcept {
  return (MakeOwnerImpl)(ptr, refcount, acquire);
}

template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, Owner> MakeOwner(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount, bool acquire) noexcept {
  static_assert(IsArenaConstructible<T>::value,
                "T must be arena constructable");
  return (MakeOwnerImpl)(ptr, refcount, acquire);
}

}  // namespace common_internal

// `Borrower` represents a reference to some borrowed data, where the data has
// at least one owner. When using reference counting, `Borrower` does not
// participate in incrementing/decrementing the reference count. Thus `Borrower`
// will not keep the underlying data alive.
class Borrower final {
 public:
  static Borrower None() noexcept { return Borrower(); }

  static Borrower Allocator(Allocator<> allocator) noexcept {
    auto* arena = allocator.arena();
    return arena != nullptr ? Arena(arena) : None();
  }

  static Borrower Arena(absl::Nonnull<google::protobuf::Arena*> arena
                            ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
    ABSL_DCHECK(arena != nullptr);
    return Borrower(reinterpret_cast<uintptr_t>(arena) | Owner::kArenaBit);
  }

  static Borrower Arena(std::nullptr_t) = delete;

  static Borrower ReferenceCount(
      absl::Nonnull<const ReferenceCount*> reference_count
          ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
    ABSL_DCHECK(reference_count != nullptr);
    return Borrower(reinterpret_cast<uintptr_t>(reference_count) |
                    Owner::kReferenceCountBit);
  }

  static Borrower ReferenceCount(std::nullptr_t) = delete;

  Borrower() = default;
  Borrower(const Borrower&) = default;
  Borrower(Borrower&&) = default;
  Borrower& operator=(const Borrower&) = default;
  Borrower& operator=(Borrower&&) = default;

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrower(const Owned<T>& owned ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept;

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrower(Borrowed<T> borrowed) noexcept;

  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrower(const Owner& owner ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : ptr_(owner.ptr_) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrower& operator=(
      const Owner& owner ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
    ptr_ = owner.ptr_;
    return *this;
  }

  Borrower& operator=(Owner&&) = delete;

  template <typename T>
  Borrower& operator=(
      const Owned<T>& owned ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept;

  template <typename T>
  Borrower& operator=(Owned<T>&&) = delete;

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrower& operator=(Borrowed<T> borrowed) noexcept;

  explicit operator bool() const noexcept { return !Owner::IsNone(ptr_); }

  absl::Nullable<google::protobuf::Arena*> arena() const noexcept {
    return (ptr_ & Owner::kBits) == Owner::kArenaBit
               ? reinterpret_cast<google::protobuf::Arena*>(ptr_ & Owner::kPointerMask)
               : nullptr;
  }

  // Tests whether two borrowers are borrowing the same data.
  friend bool operator==(Borrower lhs, Borrower rhs) noexcept {
    // A reference count and arena can never occupy the same memory address, so
    // we can compare for equality without masking off the bits.
    return lhs.ptr_ == rhs.ptr_;
  }

 private:
  friend class Owner;
  template <typename U>
  friend struct Borrowable;
  template <typename T>
  friend Borrower common_internal::MakeBorrowerImpl(
      absl::Nullable<const T*> ptr,
      absl::Nullable<const common_internal::ReferenceCount*> refcount) noexcept;
  friend absl::Nullable<const common_internal::ReferenceCount*>
  common_internal::BorrowerRelease(Borrower borrower) noexcept;

  constexpr explicit Borrower(uintptr_t ptr) noexcept : ptr_(ptr) {}

  uintptr_t ptr_ = Owner::kNone;
};

inline bool operator!=(Borrower lhs, Borrower rhs) noexcept {
  return !operator==(lhs, rhs);
}

inline bool operator==(Borrower lhs, const Owner& rhs) noexcept {
  return operator==(lhs, Borrower(rhs));
}

inline bool operator==(const Owner& lhs, Borrower rhs) noexcept {
  return operator==(Borrower(lhs), rhs);
}

inline bool operator!=(Borrower lhs, const Owner& rhs) noexcept {
  return !operator==(lhs, rhs);
}

inline bool operator!=(const Owner& lhs, Borrower rhs) noexcept {
  return !operator==(lhs, rhs);
}

inline Owner::Owner(Borrower borrower) noexcept
    : ptr_(Owner::Own(borrower.ptr_)) {}

namespace common_internal {

inline absl::Nullable<const ReferenceCount*> BorrowerRelease(
    Borrower borrower) noexcept {
  if (Owner::IsReferenceCount(borrower.ptr_)) {
    return Owner::AsReferenceCount(borrower.ptr_);
  }
  return nullptr;
}

inline uintptr_t EncodeBorrower(
    absl::Nullable<const google::protobuf::MessageLite*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept {
  return (EncodeOwner)(ptr, refcount, /*acquire=*/false);
}

inline uintptr_t EncodeBorrower(
    absl::Nullable<const Data*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept {
  return (EncodeOwner)(ptr, refcount, /*acquire=*/false);
}

template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, uintptr_t> EncodeBorrower(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept {
  return (EncodeOwner)(ptr, refcount, /*acquire=*/false);
}

template <typename T>
Borrower MakeBorrowerImpl(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept {
  return Borrower((EncodeBorrower)(ptr, refcount));
}

inline Borrower MakeBorrower(
    absl::Nullable<const google::protobuf::MessageLite*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept {
  return (MakeBorrowerImpl)(ptr, refcount);
}

inline Borrower MakeBorrower(
    absl::Nullable<const Data*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept {
  return (MakeBorrowerImpl)(ptr, refcount);
}

template <typename T>
std::enable_if_t<kNotMessageLiteAndNotData<T>, Borrower> MakeBorrower(
    absl::Nullable<const T*> ptr,
    absl::Nullable<const ReferenceCount*> refcount) noexcept {
  static_assert(IsArenaConstructible<T>::value,
                "T must be arena constructable");
  return (MakeBorrowerImpl)(ptr, refcount);
}

}  // namespace common_internal

template <typename T, typename... Args>
Unique<T> AllocateUnique(Allocator<> allocator, Args&&... args);

template <typename T>
Unique<T> WrapUnique(T* object);

// `Unique<T>` points to an object which was allocated using `Allocator<>` or
// `Allocator<T>`. It has ownership over the object, and will perform any
// destruction and deallocation required. `Unique` must not outlive the
// underlying arena, if any. Unlike `Owned` and `Borrowed`, `Unique` supports
// arena incompatible objects. It is very similar to `std::unique_ptr` when
// using a custom deleter.
//
// IMPLEMENTATION NOTES:
// When utilizing arenas, we optionally perform a risky optimization via
// `AllocateUnique`. We do not use `Arena::Create`, instead we directly allocate
// the bytes and construct it in place ourselves. This avoids registering the
// destructor when required. Instead we register the destructor ourselves, if
// required, during `Unique::release`. This allows us to avoid deferring
// destruction of the object until the arena is destroyed, avoiding the cost
// involved in doing so.
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI [[nodiscard]] Unique final {
 public:
  using element_type = T;

  static_assert(!std::is_array_v<T>, "T must not be an array");
  static_assert(!std::is_reference_v<T>, "T must not be a reference");
  static_assert(!std::is_volatile_v<T>, "T must not be volatile qualified");

  Unique() = default;
  Unique(const Unique&) = delete;
  Unique& operator=(const Unique&) = delete;

  explicit Unique(T* ptr) noexcept : Unique(ptr, nullptr) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  Unique(std::nullptr_t) noexcept : Unique() {}

  Unique(Unique&& other) noexcept : Unique(other.ptr_, other.arena_) {
    other.ptr_ = nullptr;
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Unique(Unique<U>&& other) noexcept : Unique(other.ptr_, other.arena_) {
    other.ptr_ = nullptr;
  }

  ~Unique() { Delete(); }

  Unique& operator=(Unique&& other) noexcept {
    if (ABSL_PREDICT_TRUE(this != &other)) {
      Delete();
      ptr_ = other.ptr_;
      arena_ = other.arena_;
      other.ptr_ = nullptr;
    }
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Unique& operator=(U* other) noexcept {
    reset(other);
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Unique& operator=(Unique<U>&& other) noexcept {
    Delete();
    ptr_ = other.ptr_;
    arena_ = other.arena_;
    other.ptr_ = nullptr;
    return *this;
  }

  // NOLINTNEXTLINE(google-explicit-constructor)
  Unique& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  T& operator*() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(static_cast<bool>(*this));
    return *get();
  }

  absl::Nonnull<T*> operator->() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(static_cast<bool>(*this));
    return get();
  }

  // Relinquishes ownership of `T*`, returning it. If `T` was allocated and
  // constructed using an arena, no further action is required. If `T` was
  // allocated and constructed without an arena, the caller must eventually call
  // `delete`.
  ABSL_MUST_USE_RESULT T* release() noexcept {
    if constexpr (!IsArenaDestructorSkippable<T>::value) {
      if (static_cast<bool>(*this) && arena_ != nullptr) {
        // We never registered the destructor, call it if necessary.
        arena_->OwnDestructor(ptr_);
      }
    }
    return std::exchange(ptr_, nullptr);
  }

  void reset() noexcept { reset(nullptr); }

  void reset(T* ptr) noexcept {
    Delete();
    ptr_ = ptr;
    arena_ = nullptr;
  }

  void reset(std::nullptr_t) noexcept {
    Delete();
    ptr_ = nullptr;
  }

  explicit operator bool() const noexcept { return get() != nullptr; }

  friend void swap(Unique& lhs, Unique& rhs) noexcept {
    using std::swap;
    swap(lhs.ptr_, rhs.ptr_);
    swap(lhs.arena_, rhs.arena_);
  }

 private:
  template <typename U>
  friend class Unique;
  template <typename U, typename... Args>
  friend Unique<U> AllocateUnique(Allocator<> allocator, Args&&... args);
  friend class ReferenceCountingMemoryManager;
  friend class PoolingMemoryManager;
  friend struct std::pointer_traits<Unique<T>>;

  constexpr Unique(T* ptr, google::protobuf::Arena* arena) noexcept
      : ptr_(ptr), arena_(arena) {}

  T* get() const noexcept { return ptr_; }

  void Delete() const noexcept {
    if (static_cast<bool>(*this)) {
      if (arena_ != nullptr) {
        // We never registered the destructor, call it if necessary.
        if constexpr (!IsArenaDestructorSkippable<T>::value) {
          ptr_->~T();
        }
      } else {
        google::protobuf::Arena::Destroy(ptr_);
      }
    }
  }

  T* ptr_ = nullptr;
  // Nonnull when `ptr` was allocated on the arena and its destructor is not
  // skippable. In that case we must register the destructor upon release.
  google::protobuf::Arena* arena_ = nullptr;
};

template <typename T>
Unique(T*) -> Unique<T>;

template <typename T, typename... Args>
Unique<T> AllocateUnique(Allocator<> allocator, Args&&... args) {
  T* object;
  auto* arena = allocator.arena();
  if constexpr (IsArenaConstructible<T>::value) {
    object = google::protobuf::Arena::Create<T>(arena, std::forward<Args>(args)...);
    // For arena-compatible proto types, let the Arena::Create handle
    // registering the destructor call.
    // Otherwise, Unique<T> retains a pointer to the owning arena so it may
    // conditionally register T::~T depending on usage.
    arena = nullptr;
  } else {
    void* p = allocator.allocate_bytes(sizeof(T), alignof(T));
    CEL_INTERNAL_TRY { object = ::new (p) T(std::forward<Args>(args)...); }
    CEL_INTERNAL_CATCH_ANY {
      allocator.deallocate_bytes(p, sizeof(T), alignof(T));
      CEL_INTERNAL_RETHROW;
    }
  }
  return Unique<T>(object, arena);
}

template <typename T>
Unique<T> WrapUnique(T* object) {
  return Unique<T>(object);
}

}  // namespace cel

namespace std {

template <typename T>
struct pointer_traits<cel::Unique<T>> {
  using pointer = cel::Unique<T>;
  using element_type = typename cel::Unique<T>::element_type;
  using difference_type = ptrdiff_t;

  template <typename U>
  using rebind = cel::Unique<U>;

  static element_type* to_address(const pointer& p) noexcept { return p.ptr_; }
};

}  // namespace std

namespace cel {

template <typename T, typename... Args>
Owned<T> AllocateShared(Allocator<> allocator, Args&&... args);

template <typename T>
Owned<T> WrapShared(T* object);

// `Owned<T>` points to an object which was allocated using `Allocator<>` or
// `Allocator<T>`. It has co-ownership over the object. `T` must meet the named
// requirement `ArenaConstructable`.
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI [[nodiscard]] Owned final {
 public:
  using element_type = T;

  static_assert(!std::is_array_v<T>, "T must not be an array");
  static_assert(!std::is_reference_v<T>, "T must not be a reference");
  static_assert(!std::is_volatile_v<T>, "T must not be volatile qualified");
  static_assert(!std::is_void_v<T>, "T must not be void");

  Owned() = default;

  Owned(const Owned& other) noexcept : Owned(other.value_, other.refcount_) {
    common_internal::StrongRef(refcount_);
  }

  Owned(Owned&& other) noexcept : Owned(other.value_, other.refcount_) {
    other.value_ = nullptr;
    other.refcount_ = nullptr;
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned(const Owned<U>& other) noexcept : Owned(other.value_, other.refcount_) {
    common_internal::StrongRef(refcount_);
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned(Owned<U>&& other) noexcept : Owned(other.value_, other.refcount_) {
    other.value_ = nullptr;
    other.refcount_ = nullptr;
  }

  template <typename U, typename = std::enable_if_t<
                            common_internal::kIsPointerConvertible<T, U>>>
  explicit Owned(Borrowed<U> other) noexcept;

  template <typename U, typename = std::enable_if_t<
                            common_internal::kIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned(Unique<U>&& other) : Owned(cel::to_address(other), nullptr) {
    if (value_ != nullptr) {
      if (value_->GetArena() == nullptr) {
        refcount_ = common_internal::MakeDeletingReferenceCount(value_);
      }
      static_cast<void>(other.release());
    }
  }

  Owned(Owner owner, T* value ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : Owned(value, common_internal::OwnerRelease(std::move(owner))) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned(std::nullptr_t) noexcept : Owned() {}

  ~Owned() { common_internal::StrongUnref(refcount_); }

  Owned& operator=(const Owned& other) noexcept {
    if (ABSL_PREDICT_TRUE(this != &other)) {
      common_internal::StrongRef(other.refcount_);
      common_internal::StrongUnref(refcount_);
      value_ = other.value_;
      refcount_ = other.refcount_;
    }
    return *this;
  }

  Owned& operator=(Owned&& other) noexcept {
    if (ABSL_PREDICT_TRUE(this != &other)) {
      common_internal::StrongUnref(refcount_);
      value_ = other.value_;
      refcount_ = other.refcount_;
      other.value_ = nullptr;
      other.refcount_ = nullptr;
    }
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned& operator=(const Owned<U>& other) noexcept {
    common_internal::StrongRef(other.refcount_);
    common_internal::StrongUnref(refcount_);
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned& operator=(Owned<U>&& other) noexcept {
    common_internal::StrongUnref(refcount_);
    value_ = other.value_;
    refcount_ = other.refcount_;
    other.value_ = nullptr;
    other.refcount_ = nullptr;
    return *this;
  }

  template <typename U, typename = std::enable_if_t<
                            common_internal::kIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned& operator=(Borrowed<U> other) noexcept;

  template <typename U, typename = std::enable_if_t<
                            common_internal::kIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned& operator=(Unique<U>&& other) {
    common_internal::StrongUnref(refcount_);
    value_ = cel::to_address(other);
    if (value_ != nullptr) {
      if (value_->GetArena() == nullptr) {
        refcount_ = common_internal::MakeDeletingReferenceCount(value_);
      } else {
        refcount_ = nullptr;
      }
      static_cast<void>(other.release());
    } else {
      refcount_ = nullptr;
    }
    return *this;
  }

  // NOLINTNEXTLINE(google-explicit-constructor)
  Owned& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  T& operator*() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(static_cast<bool>(*this));
    return *get();
  }

  absl::Nonnull<T*> operator->() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(static_cast<bool>(*this));
    return get();
  }

  void reset() noexcept {
    common_internal::StrongUnref(refcount_);
    value_ = nullptr;
    refcount_ = nullptr;
  }

  explicit operator bool() const noexcept { return get() != nullptr; }

  friend void swap(Owned& lhs, Owned& rhs) noexcept {
    using std::swap;
    swap(lhs.value_, rhs.value_);
    swap(lhs.refcount_, rhs.refcount_);
  }

 private:
  friend class Owner;
  friend class Borrower;
  template <typename U>
  friend class Owned;
  template <typename U>
  friend class Borrowed;
  template <typename U>
  friend struct Ownable;
  template <typename U, typename... Args>
  friend Owned<U> AllocateShared(Allocator<> allocator, Args&&... args);
  template <typename U>
  friend Owned<U> WrapShared(U* object);
  friend struct std::pointer_traits<Owned<T>>;

  constexpr Owned(T* value, const ReferenceCount* refcount) noexcept
      : value_(value), refcount_(refcount) {}

  T* get() const noexcept { return value_; }

  T* value_ = nullptr;
  const ReferenceCount* refcount_ = nullptr;
};

template <typename T>
Owned(T*) -> Owned<T>;
template <typename T>
Owned(Unique<T>) -> Owned<T>;
template <typename T>
Owned(Owner, T*) -> Owned<T>;
template <typename T>
Owned(Borrowed<T>) -> Owned<T>;

}  // namespace cel

namespace std {

template <typename T>
struct pointer_traits<cel::Owned<T>> {
  using pointer = cel::Owned<T>;
  using element_type = typename cel::Owned<T>::element_type;
  using difference_type = ptrdiff_t;

  template <typename U>
  using rebind = cel::Owned<U>;

  static element_type* to_address(const pointer& p) noexcept {
    return p.value_;
  }
};

}  // namespace std

namespace cel {

template <typename T>
Owner::Owner(const Owned<T>& owned) noexcept
    : Owner(common_internal::EncodeOwner(owned.value_, owned.refcount_,
                                         /*acquire=*/true)) {}

template <typename T>
Owner::Owner(Owned<T>&& owned) noexcept
    : Owner(common_internal::EncodeOwner(owned.value_, owned.refcount_,
                                         /*acquire=*/false)) {
  owned.value_ = nullptr;
  owned.refcount_ = nullptr;
}

template <typename T>
Owner& Owner::operator=(const Owned<T>& owned) noexcept {
  Unown(ptr_);
  ptr_ = common_internal::EncodeOwner(owned.value_, owned.refcount_,
                                      /*acquire=*/true);
  return *this;
}

template <typename T>
Owner& Owner::operator=(Owned<T>&& owned) noexcept {
  Unown(ptr_);
  ptr_ = common_internal::EncodeOwner(owned.value_, owned.refcount_,
                                      /*acquire=*/false);
  owned.value_ = nullptr;
  owned.refcount_ = nullptr;
  return *this;
}

template <typename T>
bool operator==(const Owned<T>& lhs, std::nullptr_t) noexcept {
  return !static_cast<bool>(lhs);
}

template <typename T>
bool operator==(std::nullptr_t, const Owned<T>& rhs) noexcept {
  return rhs == nullptr;
}

template <typename T>
bool operator!=(const Owned<T>& lhs, std::nullptr_t) noexcept {
  return !operator==(lhs, nullptr);
}

template <typename T>
bool operator!=(std::nullptr_t, const Owned<T>& rhs) noexcept {
  return !operator==(nullptr, rhs);
}

template <typename T, typename... Args>
Owned<T> AllocateShared(Allocator<> allocator, Args&&... args) {
  static_assert(IsArenaConstructible<std::remove_const_t<T>>::value,
                "T must be arena constructable");
  T* object;
  const common_internal::ReferenceCount* refcount;
  if (allocator.arena() != nullptr) {
    object = allocator.new_object<T>(std::forward<Args>(args)...);
    refcount = nullptr;
  } else {
    std::tie(object, refcount) = common_internal::MakeEmplacedReferenceCount<T>(
        std::forward<Args>(args)...);
  }
  return Owned<T>(object, refcount);
}

template <typename T>
Owned<T> WrapShared(T* object) {
  const common_internal::ReferenceCount* refcount;
  if (object == nullptr || object->GetArena() != nullptr) {
    refcount = nullptr;
  } else {
    refcount = common_internal::MakeDeletingReferenceCount(object);
  }
  return Owned<T>(object, refcount);
}

// `Borrowed<T>` points to an object which was allocated using `Allocator<>` or
// `Allocator<T>`. It has no ownership over the object, and is only valid so
// long as one or more owners of the object exist. `T` must meet the named
// requirement `ArenaConstructable`.
template <typename T>
class Borrowed final {
 public:
  using element_type = T;

  static_assert(!std::is_array_v<T>, "T must not be an array");
  static_assert(!std::is_reference_v<T>, "T must not be a reference");
  static_assert(!std::is_volatile_v<T>, "T must not be volatile qualified");
  static_assert(!std::is_void_v<T>, "T must not be void");

  Borrowed() = default;
  Borrowed(const Borrowed&) = default;
  Borrowed(Borrowed&&) = default;
  Borrowed& operator=(const Borrowed&) = default;
  Borrowed& operator=(Borrowed&&) = default;

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed(const Borrowed<U>& other) noexcept
      : Borrowed(other.value_, other.refcount_) {}

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed(Borrowed<U>&& other) noexcept
      : Borrowed(other.value_, other.refcount_) {}

  template <typename U, typename = std::enable_if_t<
                            common_internal::kIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed(const Owned<U>& other ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : Borrowed(other.value_, other.refcount_) {}

  Borrowed(Borrower borrower, T* ptr) noexcept
      : Borrowed(ptr, common_internal::BorrowerRelease(borrower)) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed(std::nullptr_t) noexcept : Borrowed() {}

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed& operator=(const Borrowed<U>& other) noexcept {
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<
                common_internal::kNotSameAndIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed& operator=(Borrowed<U>&& other) noexcept {
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <typename U, typename = std::enable_if_t<
                            common_internal::kIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed& operator=(
      const Owned<U>& other ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <typename U, typename = std::enable_if_t<
                            common_internal::kIsPointerConvertible<T, U>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed& operator=(Owned<U>&&) = delete;

  // NOLINTNEXTLINE(google-explicit-constructor)
  Borrowed& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  T& operator*() const noexcept {
    ABSL_DCHECK(static_cast<bool>(*this));
    return *get();
  }

  absl::Nonnull<T*> operator->() const noexcept {
    ABSL_DCHECK(static_cast<bool>(*this));
    return get();
  }

  void reset() noexcept {
    value_ = nullptr;
    refcount_ = nullptr;
  }

  explicit operator bool() const noexcept { return get() != nullptr; }

 private:
  friend class Owner;
  friend class Borrower;
  template <typename U>
  friend class Owned;
  template <typename U>
  friend class Borrowed;
  template <typename U>
  friend struct Borrowable;
  friend struct std::pointer_traits<Borrowed<T>>;

  constexpr Borrowed(T* value, const ReferenceCount* refcount) noexcept
      : value_(value), refcount_(refcount) {}

  T* get() const noexcept { return value_; }

  T* value_ = nullptr;
  const ReferenceCount* refcount_ = nullptr;
};

template <typename T>
Borrowed(T*) -> Borrowed<T>;
template <typename T>
Borrowed(Borrower, T*) -> Borrowed<T>;
template <typename T>
Borrowed(Owned<T>) -> Borrowed<T>;

}  // namespace cel

namespace std {

template <typename T>
struct pointer_traits<cel::Borrowed<T>> {
  using pointer = cel::Borrowed<T>;
  using element_type = typename cel::Borrowed<T>::element_type;
  using difference_type = ptrdiff_t;

  template <typename U>
  using rebind = cel::Borrowed<U>;

  static element_type* to_address(pointer p) noexcept { return p.value_; }
};

}  // namespace std

namespace cel {

template <typename T>
Owner::Owner(Borrowed<T> borrowed) noexcept
    : Owner(common_internal::EncodeOwner(borrowed.value_, borrowed.refcount_,
                                         /*acquire=*/true)) {}

template <typename T>
Borrower::Borrower(const Owned<T>& owned ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
    : Borrower(common_internal::EncodeBorrower(owned.value_, owned.refcount_)) {
}

template <typename T>
Borrower::Borrower(Borrowed<T> borrowed) noexcept
    : Borrower(common_internal::EncodeBorrower(borrowed.value_,
                                               borrowed.refcount_)) {}

template <typename T>
Borrower& Borrower::operator=(
    const Owned<T>& owned ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
  ptr_ = common_internal::EncodeBorrower(owned.value_, owned.refcount_);
  return *this;
}

template <typename T>
Borrower& Borrower::operator=(Borrowed<T> borrowed) noexcept {
  ptr_ = common_internal::EncodeBorrower(borrowed.value_, borrowed.refcount_);
  return *this;
}

template <typename T>
bool operator==(Borrowed<T> lhs, std::nullptr_t) noexcept {
  return !static_cast<bool>(lhs);
}

template <typename T>
bool operator==(std::nullptr_t, Borrowed<T> rhs) noexcept {
  return rhs == nullptr;
}

template <typename T>
bool operator!=(Borrowed<T> lhs, std::nullptr_t) noexcept {
  return !operator==(lhs, nullptr);
}

template <typename T>
bool operator!=(std::nullptr_t, Borrowed<T> rhs) noexcept {
  return !operator==(nullptr, rhs);
}

template <typename T>
template <typename U, typename>
Owned<T>::Owned(Borrowed<U> other) noexcept
    : Owned(other.value_, other.refcount_) {
  common_internal::StrongRef(refcount_);
}

template <typename T>
template <typename U, typename>
Owned<T>& Owned<T>::operator=(Borrowed<U> other) noexcept {
  common_internal::StrongRef(other.refcount_);
  common_internal::StrongUnref(refcount_);
  value_ = other.value_;
  refcount_ = other.refcount_;
  return *this;
}

// `Ownable<T>` is a mixin for enabling the ability to get `Owned` that refer to
// this.
template <typename T>
struct Ownable {
 protected:
  Owned<const T> Own() const noexcept {
    static_assert(std::is_base_of_v<Data, T>, "T must be derived from Data");
    const T* const that = static_cast<const T*>(this);
    return Owned<const T>(
        Owner(Owner::Own(static_cast<const Data*>(that)->owner_)), that);
  }

  Owned<T> Own() noexcept {
    static_assert(std::is_base_of_v<Data, T>, "T must be derived from Data");
    T* const that = static_cast<T*>(this);
    return Owned<T>(Owner(Owner::Own(static_cast<Data*>(that)->owner_)), that);
  }

  ABSL_DEPRECATED("Use Own")
  Owned<const T> shared_from_this() const noexcept { return Own(); }

  ABSL_DEPRECATED("Use Own")
  Owned<T> shared_from_this() noexcept { return Own(); }
};

// `Borrowable<T>` is a mixin for enabling the ability to get `Borrowed` that
// refer to this.
template <typename T>
struct Borrowable {
 protected:
  Borrowed<const T> Borrow() const noexcept {
    static_assert(std::is_base_of_v<Data, T>, "T must be derived from Data");
    const T* const that = static_cast<const T*>(this);
    return Borrowed<const T>(Borrower(static_cast<const Data*>(that)->owner_),
                             that);
  }

  Borrowed<T> Borrow() noexcept {
    static_assert(std::is_base_of_v<Data, T>, "T must be derived from Data");
    T* const that = static_cast<T*>(this);
    return Borrowed<T>(Borrower(static_cast<Data*>(that)->owner_), that);
  }
};

// `Shared` points to an object allocated in memory which is managed by a
// `MemoryManager`. The pointed to object is valid so long as the managing
// `MemoryManager` is alive and one or more valid `Shared` exist pointing to the
// object.
//
// IMPLEMENTATION DETAILS:
// `Shared` is similar to `std::shared_ptr`, except that it works for
// region-based memory management as well. In that case the pointer to the
// reference count is `nullptr`.
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI Shared final {
 public:
  Shared() = default;

  Shared(const Shared& other)
      : value_(other.value_), refcount_(other.refcount_) {
    common_internal::StrongRef(refcount_);
  }

  Shared(Shared&& other) noexcept
      : value_(other.value_), refcount_(other.refcount_) {
    other.value_ = nullptr;
    other.refcount_ = nullptr;
  }

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Shared(const Shared<U>& other)
      : value_(other.value_), refcount_(other.refcount_) {
    common_internal::StrongRef(refcount_);
  }

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Shared(Shared<U>&& other) noexcept
      : value_(other.value_), refcount_(other.refcount_) {
    other.value_ = nullptr;
    other.refcount_ = nullptr;
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  explicit Shared(SharedView<U> other);

  // An aliasing constructor. The resulting `Shared` shares ownership
  // information with `alias`, but holds an unmanaged pointer to `T`.
  //
  // Usage:
  //   Shared<Object> object;
  //   Shared<Member> member = Shared<Member>(object, &object->member);
  template <typename U>
  Shared(const Shared<U>& alias, T* ptr)
      : value_(ptr), refcount_(alias.refcount_) {
    common_internal::StrongRef(refcount_);
  }

  // An aliasing constructor. The resulting `Shared` shares ownership
  // information with `alias`, but holds an unmanaged pointer to `T`.
  template <typename U>
  Shared(Shared<U>&& alias, T* ptr) noexcept
      : value_(ptr), refcount_(alias.refcount_) {
    alias.value_ = nullptr;
    alias.refcount_ = nullptr;
  }

  ~Shared() { common_internal::StrongUnref(refcount_); }

  Shared& operator=(const Shared& other) {
    common_internal::StrongRef(other.refcount_);
    common_internal::StrongUnref(refcount_);
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  Shared& operator=(Shared&& other) noexcept {
    common_internal::StrongUnref(refcount_);
    value_ = other.value_;
    refcount_ = other.refcount_;
    other.value_ = nullptr;
    other.refcount_ = nullptr;
    return *this;
  }

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Shared& operator=(const Shared<U>& other) {
    common_internal::StrongRef(other.refcount_);
    common_internal::StrongUnref(refcount_);
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Shared& operator=(Shared<U>&& other) noexcept {
    common_internal::StrongUnref(refcount_);
    value_ = other.value_;
    refcount_ = other.refcount_;
    other.value_ = nullptr;
    other.refcount_ = nullptr;
    return *this;
  }

  template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
  U& operator*() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(!IsEmpty());
    return *value_;
  }

  absl::Nonnull<T*> operator->() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(!IsEmpty());
    return value_;
  }

  explicit operator bool() const { return !IsEmpty(); }

  friend constexpr void swap(Shared& lhs, Shared& rhs) noexcept {
    using std::swap;
    swap(lhs.value_, rhs.value_);
    swap(lhs.refcount_, rhs.refcount_);
  }

 private:
  template <typename U>
  friend class Shared;
  template <typename U>
  friend class SharedView;
  template <typename To, typename From>
  friend Shared<To> StaticCast(Shared<From>&& from);
  template <typename U>
  friend U* common_internal::GetPointer(const Shared<U>& shared);
  template <typename U>
  friend const common_internal::ReferenceCount*
  common_internal::GetReferenceCount(const Shared<U>& shared);
  template <typename U>
  friend Shared<U> common_internal::MakeShared(
      common_internal::AdoptRef, U* value,
      const common_internal::ReferenceCount* refcount);

  Shared(common_internal::AdoptRef, T* value,
         const common_internal::ReferenceCount* refcount) noexcept
      : value_(value), refcount_(refcount) {}

  Shared(T* value, const common_internal::ReferenceCount* refcount) noexcept
      : value_(value), refcount_(refcount) {
    common_internal::StrongRef(refcount_);
  }

  bool IsEmpty() const noexcept { return value_ == nullptr; }

  T* value_ = nullptr;
  const common_internal::ReferenceCount* refcount_ = nullptr;
};

template <typename To, typename From>
inline Shared<To> StaticCast(const Shared<From>& from) {
  return common_internal::MakeShared(
      static_cast<To*>(common_internal::GetPointer(from)),
      common_internal::GetReferenceCount(from));
}

template <typename To, typename From>
inline Shared<To> StaticCast(Shared<From>&& from) {
  To* value = static_cast<To*>(from.value_);
  const auto* refcount = from.refcount_;
  from.value_ = nullptr;
  from.refcount_ = nullptr;
  return Shared<To>(common_internal::kAdoptRef, value, refcount);
}

template <typename T>
struct NativeTypeTraits<Shared<T>> final {
  static bool SkipDestructor(const Shared<T>& shared) {
    return common_internal::GetReferenceCount(shared) == nullptr;
  }
};

// `SharedView` is a wrapper on top of `Shared`. It is roughly equivalent to
// `const Shared<T>&` and can be used in places where it is not feasible to use
// `const Shared<T>&` directly. This is also analygous to
// `std::reference_wrapper<const Shared<T>>>` and is intended to be used under
// the same cirumstances.
template <typename T>
class ABSL_ATTRIBUTE_TRIVIAL_ABI SharedView final {
 public:
  SharedView() = default;
  SharedView(const SharedView&) = default;
  SharedView& operator=(const SharedView&) = default;

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SharedView(const SharedView<U>& other)
      : value_(other.value_), refcount_(other.refcount_) {}

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SharedView(SharedView<U>&& other) noexcept
      : value_(other.value_), refcount_(other.refcount_) {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SharedView(const Shared<U>& other ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : value_(other.value_), refcount_(other.refcount_) {}

  template <typename U>
  SharedView(SharedView<U> alias, T* ptr)
      : value_(ptr), refcount_(alias.refcount_) {}

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SharedView& operator=(const SharedView<U>& other) {
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <
      typename U,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_same<U, T>>, std::is_convertible<U*, T*>>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SharedView& operator=(SharedView<U>&& other) noexcept {
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SharedView& operator=(
      const Shared<U>& other ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept {
    value_ = other.value_;
    refcount_ = other.refcount_;
    return *this;
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  SharedView& operator=(Shared<U>&&) = delete;

  template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
  U& operator*() const noexcept ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(!IsEmpty());
    return *value_;
  }

  absl::Nonnull<T*> operator->() const noexcept {
    ABSL_DCHECK(!IsEmpty());
    return value_;
  }

  explicit operator bool() const { return !IsEmpty(); }

  friend constexpr void swap(SharedView& lhs, SharedView& rhs) noexcept {
    using std::swap;
    swap(lhs.value_, rhs.value_);
    swap(lhs.refcount_, rhs.refcount_);
  }

 private:
  template <typename U>
  friend class Shared;
  template <typename U>
  friend class SharedView;
  template <typename U>
  friend U* common_internal::GetPointer(SharedView<U> shared);
  template <typename U>
  friend const common_internal::ReferenceCount*
  common_internal::GetReferenceCount(SharedView<U> shared);
  template <typename U>
  friend SharedView<U> common_internal::MakeSharedView(
      U* value, const common_internal::ReferenceCount* refcount);

  SharedView(T* value, const common_internal::ReferenceCount* refcount)
      : value_(value), refcount_(refcount) {}

  bool IsEmpty() const noexcept { return value_ == nullptr; }

  T* value_ = nullptr;
  const common_internal::ReferenceCount* refcount_ = nullptr;
};

template <typename T>
template <typename U, typename>
Shared<T>::Shared(SharedView<U> other)
    : value_(other.value_), refcount_(other.refcount_) {
  StrongRef(refcount_);
}

template <typename To, typename From>
SharedView<To> StaticCast(SharedView<From> from) {
  return common_internal::MakeSharedView(
      static_cast<To*>(common_internal::GetPointer(from)),
      common_internal::GetReferenceCount(from));
}

template <typename T>
struct EnableSharedFromThis
    : public virtual common_internal::ReferenceCountFromThis {
 protected:
  Shared<T> shared_from_this() noexcept {
    auto* const derived = static_cast<T*>(this);
    auto* const refcount = common_internal::GetReferenceCountForThat(*this);
    return common_internal::MakeShared(derived, refcount);
  }

  Shared<const T> shared_from_this() const noexcept {
    auto* const derived = static_cast<const T*>(this);
    auto* const refcount = common_internal::GetReferenceCountForThat(*this);
    return common_internal::MakeShared(derived, refcount);
  }
};

// `ReferenceCountingMemoryManager` is a `MemoryManager` which employs automatic
// memory management through reference counting.
class ReferenceCountingMemoryManager final {
 public:
  ReferenceCountingMemoryManager(const ReferenceCountingMemoryManager&) =
      delete;
  ReferenceCountingMemoryManager(ReferenceCountingMemoryManager&&) = delete;
  ReferenceCountingMemoryManager& operator=(
      const ReferenceCountingMemoryManager&) = delete;
  ReferenceCountingMemoryManager& operator=(ReferenceCountingMemoryManager&&) =
      delete;

 private:
  template <typename T, typename... Args>
  static ABSL_MUST_USE_RESULT Shared<T> MakeShared(Args&&... args) {
    using U = std::remove_const_t<T>;
    U* ptr;
    common_internal::ReferenceCount* refcount;
    std::tie(ptr, refcount) =
        common_internal::MakeReferenceCount<U>(std::forward<Args>(args)...);
    return common_internal::MakeShared(common_internal::kAdoptRef,
                                       static_cast<T*>(ptr), refcount);
  }

  template <typename T, typename... Args>
  static ABSL_MUST_USE_RESULT Unique<T> MakeUnique(Args&&... args) {
    using U = std::remove_const_t<T>;
    return Unique<T>(static_cast<T*>(new U(std::forward<Args>(args)...)),
                     nullptr);
  }

  static void* Allocate(size_t size, size_t alignment);

  static bool Deallocate(void* ptr, size_t size, size_t alignment) noexcept;

  explicit ReferenceCountingMemoryManager() = default;

  friend class MemoryManager;
};

// `PoolingMemoryManager` is a `MemoryManager` which employs automatic
// memory management through memory pooling.
class PoolingMemoryManager final {
 public:
  PoolingMemoryManager(const PoolingMemoryManager&) = delete;
  PoolingMemoryManager(PoolingMemoryManager&&) = delete;
  PoolingMemoryManager& operator=(const PoolingMemoryManager&) = delete;
  PoolingMemoryManager& operator=(PoolingMemoryManager&&) = delete;

 private:
  template <typename T, typename... Args>
  ABSL_MUST_USE_RESULT static Shared<T> MakeShared(google::protobuf::Arena* arena,
                                                   Args&&... args) {
    using U = std::remove_const_t<T>;
    U* ptr = nullptr;
    void* addr = Allocate(arena, sizeof(U), alignof(U));
    CEL_INTERNAL_TRY {
      ptr = ::new (addr) U(std::forward<Args>(args)...);
      if constexpr (!std::is_trivially_destructible_v<U>) {
        if (!NativeType::SkipDestructor(*ptr)) {
          CEL_INTERNAL_TRY {
            OwnCustomDestructor(arena, ptr, &DefaultDestructor<U>);
          }
          CEL_INTERNAL_CATCH_ANY {
            ptr->~U();
            CEL_INTERNAL_RETHROW;
          }
        }
      }
      if constexpr (std::is_base_of_v<common_internal::ReferenceCountFromThis,
                                      U>) {
        common_internal::SetReferenceCountForThat(*ptr, nullptr);
      }
    }
    CEL_INTERNAL_CATCH_ANY {
      Deallocate(arena, addr, sizeof(U), alignof(U));
      CEL_INTERNAL_RETHROW;
    }
    return common_internal::MakeShared(common_internal::kAdoptRef,
                                       static_cast<T*>(ptr), nullptr);
  }

  template <typename T, typename... Args>
  ABSL_MUST_USE_RESULT static Unique<T> MakeUnique(google::protobuf::Arena* arena,
                                                   Args&&... args) {
    using U = std::remove_const_t<T>;
    U* ptr = nullptr;
    void* addr = Allocate(arena, sizeof(U), alignof(U));
    CEL_INTERNAL_TRY { ptr = ::new (addr) U(std::forward<Args>(args)...); }
    CEL_INTERNAL_CATCH_ANY {
      Deallocate(arena, addr, sizeof(U), alignof(U));
      CEL_INTERNAL_RETHROW;
    }
    return Unique<T>(static_cast<T*>(ptr), arena);
  }

  // Allocates memory directly from the allocator used by this memory manager.
  // If `memory_management()` returns `MemoryManagement::kReferenceCounting`,
  // this allocation *must* be explicitly deallocated at some point via
  // `Deallocate`. Otherwise deallocation is optional.
  ABSL_MUST_USE_RESULT static void* Allocate(
      absl::Nonnull<google::protobuf::Arena*> arena, size_t size, size_t alignment) {
    ABSL_DCHECK(absl::has_single_bit(alignment))
        << "alignment must be a power of 2";
    if (size == 0) {
      return nullptr;
    }
    return arena->AllocateAligned(size, alignment);
  }

  // Attempts to deallocate memory previously allocated via `Allocate`, `size`
  // and `alignment` must match the values from the previous call to `Allocate`.
  // Returns `true` if the deallocation was successful and additional calls to
  // `Allocate` may re-use the memory, `false` otherwise. Returns `false` if
  // given `nullptr`.
  static bool Deallocate(absl::Nonnull<google::protobuf::Arena*>, void*, size_t,
                         size_t alignment) noexcept {
    ABSL_DCHECK(absl::has_single_bit(alignment))
        << "alignment must be a power of 2";
    return false;
  }

  // Registers a custom destructor to be run upon destruction of the memory
  // management implementation. Return value is always `true`, indicating that
  // the destructor may be called at some point in the future.
  static bool OwnCustomDestructor(absl::Nonnull<google::protobuf::Arena*> arena,
                                  void* object,
                                  absl::Nonnull<void (*)(void*)> destruct) {
    ABSL_DCHECK(destruct != nullptr);
    arena->OwnCustomDestructor(object, destruct);
    return true;
  }

  template <typename T>
  static void DefaultDestructor(void* ptr) {
    static_assert(!std::is_trivially_destructible_v<T>);
    static_cast<T*>(ptr)->~T();
  }

  explicit PoolingMemoryManager() = default;

  friend class MemoryManager;
};

// `MemoryManager` is an abstraction for supporting automatic memory management.
// All objects created by the `MemoryManager` have a lifetime governed by the
// underlying memory management strategy. Currently `MemoryManager` is a
// composed type that holds either a reference to
// `ReferenceCountingMemoryManager` or owns a `PoolingMemoryManager`.
//
// ============================ Reference Counting ============================
// `Unique`: The object is valid until destruction of the `Unique`.
//
// `Shared`: The object is valid so long as one or more `Shared` managing the
// object exist.
//
// ================================= Pooling ==================================
// `Unique`: The object is valid until destruction of the underlying memory
// resources or of the `Unique`.
//
// `Shared`: The object is valid until destruction of the underlying memory
// resources.
class MemoryManager final {
 public:
  // Returns a `MemoryManager` which utilizes an arena but never frees its
  // memory. It is effectively a memory leak and should only be used for limited
  // use cases, such as initializing singletons which live for the life of the
  // program.
  static MemoryManager Unmanaged();

  // Returns a `MemoryManager` which utilizes reference counting.
  ABSL_MUST_USE_RESULT static MemoryManager ReferenceCounting() {
    return MemoryManager(nullptr);
  }

  // Returns a `MemoryManager` which utilizes an arena.
  ABSL_MUST_USE_RESULT static MemoryManager Pooling(
      absl::Nonnull<google::protobuf::Arena*> arena) {
    return MemoryManager(arena);
  }

  MemoryManager() = delete;
  MemoryManager(const MemoryManager&) = default;
  MemoryManager& operator=(const MemoryManager&) = default;

  MemoryManagement memory_management() const noexcept {
    return arena_ == nullptr ? MemoryManagement::kReferenceCounting
                             : MemoryManagement::kPooling;
  }

  template <typename T, typename... Args>
  ABSL_MUST_USE_RESULT Shared<T> MakeShared(Args&&... args) {
    if (arena_ == nullptr) {
      return ReferenceCountingMemoryManager::MakeShared<T>(
          std::forward<Args>(args)...);
    } else {
      return PoolingMemoryManager::MakeShared<T>(arena_,
                                                 std::forward<Args>(args)...);
    }
  }

  template <typename T, typename... Args>
  ABSL_MUST_USE_RESULT Unique<T> MakeUnique(Args&&... args) {
    if (arena_ == nullptr) {
      return ReferenceCountingMemoryManager::MakeUnique<T>(
          std::forward<Args>(args)...);
    } else {
      return PoolingMemoryManager::MakeUnique<T>(arena_,
                                                 std::forward<Args>(args)...);
    }
  }

  // Allocates memory directly from the allocator used by this memory manager.
  // If `memory_management()` returns `MemoryManagement::kReferenceCounting`,
  // this allocation *must* be explicitly deallocated at some point via
  // `Deallocate`. Otherwise deallocation is optional.
  ABSL_MUST_USE_RESULT void* Allocate(size_t size, size_t alignment) {
    if (arena_ == nullptr) {
      return ReferenceCountingMemoryManager::Allocate(size, alignment);
    } else {
      return PoolingMemoryManager::Allocate(arena_, size, alignment);
    }
  }

  // Attempts to deallocate memory previously allocated via `Allocate`, `size`
  // and `alignment` must match the values from the previous call to `Allocate`.
  // Returns `true` if the deallocation was successful and additional calls to
  // `Allocate` may re-use the memory, `false` otherwise. Returns `false` if
  // given `nullptr`.
  bool Deallocate(void* ptr, size_t size, size_t alignment) noexcept {
    if (arena_ == nullptr) {
      return ReferenceCountingMemoryManager::Deallocate(ptr, size, alignment);
    } else {
      return PoolingMemoryManager::Deallocate(arena_, ptr, size, alignment);
    }
  }

  // Registers a custom destructor to be run upon destruction of the memory
  // management implementation. A return of `true` indicates the destructor may
  // be called at some point in the future, `false` if will definitely not be
  // called. All pooling memory managers return `true` while the reference
  // counting memory manager returns `false`.
  bool OwnCustomDestructor(void* object,
                           absl::Nonnull<void (*)(void*)> destruct) {
    ABSL_DCHECK(destruct != nullptr);
    if (arena_ == nullptr) {
      return false;
    } else {
      return PoolingMemoryManager::OwnCustomDestructor(arena_, object,
                                                       destruct);
    }
  }

  absl::Nullable<google::protobuf::Arena*> arena() const noexcept { return arena_; }

  friend void swap(MemoryManager& lhs, MemoryManager& rhs) noexcept {
    using std::swap;
    swap(lhs.arena_, rhs.arena_);
  }

 private:
  friend class PoolingMemoryManager;

  explicit MemoryManager(std::nullptr_t) : arena_(nullptr) {}

  explicit MemoryManager(absl::Nonnull<google::protobuf::Arena*> arena) : arena_(arena) {}

  // If `nullptr`, we are using reference counting. Otherwise we are using
  // Pooling. We use `UnreachablePooling()` as a sentinel to detect use after
  // move otherwise the moved-from `MemoryManager` would be in a valid state and
  // utilize reference counting.
  absl::Nullable<google::protobuf::Arena*> arena_;
};

using MemoryManagerRef = MemoryManager;

namespace common_internal {

template <typename T>
inline T* GetPointer(const Shared<T>& shared) {
  return shared.value_;
}

template <typename T>
inline const ReferenceCount* GetReferenceCount(const Shared<T>& shared) {
  return shared.refcount_;
}

template <typename T>
inline Shared<T> MakeShared(T* value, const ReferenceCount* refcount) {
  StrongRef(refcount);
  return MakeShared(kAdoptRef, value, refcount);
}

template <typename T>
inline Shared<T> MakeShared(AdoptRef, T* value,
                            const ReferenceCount* refcount) {
  return Shared<T>(kAdoptRef, value, refcount);
}

template <typename T>
inline T* GetPointer(SharedView<T> shared) {
  return shared.value_;
}

template <typename T>
inline const ReferenceCount* GetReferenceCount(SharedView<T> shared) {
  return shared.refcount_;
}

template <typename T>
inline SharedView<T> MakeSharedView(T* value, const ReferenceCount* refcount) {
  return SharedView<T>(value, refcount);
}

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_MEMORY_H_
