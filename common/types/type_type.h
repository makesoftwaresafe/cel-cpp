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

// IWYU pragma: private, include "common/type.h"
// IWYU pragma: friend "common/type.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPES_TYPE_TYPE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPES_TYPE_TYPE_H_

#include <ostream>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/memory.h"
#include "common/type_kind.h"

namespace cel {

class Type;
class TypeType;
class TypeTypeView;

namespace common_internal {
struct TypeTypeData;
}  // namespace common_internal

// `TypeType` is a special type which represents the type of a type.
class TypeType final {
 public:
  using view_alternative_type = TypeTypeView;

  static constexpr TypeKind kKind = TypeKind::kType;
  static constexpr absl::string_view kName = "type";

  explicit TypeType(TypeTypeView type);

  TypeType(MemoryManagerRef memory_manager, Type parameter);

  TypeType() = default;
  TypeType(const TypeType&) = default;
  TypeType(TypeType&&) = default;
  TypeType& operator=(const TypeType&) = default;
  TypeType& operator=(TypeType&&) = default;

  constexpr TypeKind kind() const { return kKind; }

  constexpr absl::string_view name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return kName;
  }

  absl::Span<const Type> parameters() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  std::string DebugString() const { return std::string(name()); }

  constexpr void swap(TypeType& other) noexcept {
    using std::swap;
    swap(data_, other.data_);
  }

  Shared<const common_internal::TypeTypeData> data_;
};

inline constexpr void swap(TypeType& lhs, TypeType& rhs) noexcept {
  lhs.swap(rhs);
}

inline constexpr bool operator==(const TypeType&, const TypeType&) {
  return true;
}

inline constexpr bool operator!=(const TypeType& lhs, const TypeType& rhs) {
  return !operator==(lhs, rhs);
}

template <typename H>
H AbslHashValue(H state, TypeType) {
  // TypeType is really a singleton and all instances are equal. Nothing to
  // hash.
  return std::move(state);
}

inline std::ostream& operator<<(std::ostream& out, const TypeType& type) {
  return out << type.DebugString();
}

class TypeTypeView final {
 public:
  using alternative_type = TypeType;

  static constexpr TypeKind kKind = TypeType::kKind;
  static constexpr absl::string_view kName = TypeType::kName;

  // NOLINTNEXTLINE(google-explicit-constructor)
  TypeTypeView(const TypeType& type ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : data_(type.data_) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  TypeTypeView& operator=(const TypeType& type ABSL_ATTRIBUTE_LIFETIME_BOUND
                              ABSL_ATTRIBUTE_UNUSED) {
    data_ = type.data_;
    return *this;
  }

  TypeTypeView& operator=(TypeType&&) = delete;

  TypeTypeView() = default;
  TypeTypeView(const TypeTypeView&) = default;
  TypeTypeView(TypeTypeView&&) = default;
  TypeTypeView& operator=(const TypeTypeView&) = default;
  TypeTypeView& operator=(TypeTypeView&&) = default;

  constexpr TypeKind kind() const { return kKind; }

  constexpr absl::string_view name() const { return kName; }

  absl::Span<const Type> parameters() const;

  std::string DebugString() const { return std::string(name()); }

  constexpr void swap(TypeTypeView& other) noexcept {
    using std::swap;
    swap(data_, other.data_);
  }

  SharedView<const common_internal::TypeTypeData> data_;
};

inline constexpr void swap(TypeTypeView& lhs, TypeTypeView& rhs) noexcept {
  lhs.swap(rhs);
}

inline constexpr bool operator==(TypeTypeView, TypeTypeView) { return true; }

inline constexpr bool operator!=(TypeTypeView lhs, TypeTypeView rhs) {
  return !operator==(lhs, rhs);
}

template <typename H>
H AbslHashValue(H state, TypeTypeView) {
  // TypeType is really a singleton and all instances are equal. Nothing to
  // hash.
  return std::move(state);
}

inline std::ostream& operator<<(std::ostream& out, TypeTypeView type) {
  return out << type.DebugString();
}

inline TypeType::TypeType(TypeTypeView type) : data_(type.data_) {}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPES_TYPE_TYPE_H_
