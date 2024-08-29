// Copyright 2024 Google LLC
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

// IWYU pragma: private, include "common/value.h"
// IWYU pragma: friend "common/value.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_MESSAGE_VALUE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_MESSAGE_VALUE_H_

#include <cstddef>
#include <memory>
#include <utility>

#include "google/protobuf/any.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/log/die_if_null.h"
#include "absl/strings/string_view.h"
#include "common/memory.h"
#include "common/type.h"
#include "common/value_kind.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {

class MessageValue;
class Value;

class ParsedMessageValue final {
 public:
  static constexpr ValueKind kKind = ValueKind::kStruct;

  using element_type = const google::protobuf::Message;

  explicit ParsedMessageValue(Owned<const google::protobuf::Message> value)
      : value_(std::move(value)) {
    ABSL_DCHECK(!value_ || !IsWellKnownMessageType(value_->GetDescriptor()))
        << value_->GetTypeName() << " is a well known type";
  }

  // Places the `ParsedMessageValue` into an invalid state. Anything except
  // assigning to `MessageValue` is undefined behavior.
  ParsedMessageValue() = default;

  ParsedMessageValue(const ParsedMessageValue&) = default;
  ParsedMessageValue(ParsedMessageValue&&) = default;
  ParsedMessageValue& operator=(const ParsedMessageValue&) = default;
  ParsedMessageValue& operator=(ParsedMessageValue&&) = default;

  static ValueKind kind() { return kKind; }

  absl::string_view GetTypeName() const {
    return (*this)->GetDescriptor()->full_name();
  }

  MessageType GetRuntimeType() const {
    return MessageType((*this)->GetDescriptor());
  }

  absl::Nonnull<const google::protobuf::Descriptor*> GetDescriptor() const {
    return (*this)->GetDescriptor();
  }

  absl::Nullable<const google::protobuf::Reflection*> GetReflection() const {
    return (*this)->GetReflection();
  }

  const google::protobuf::Message& operator*() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(*this);
    return *value_;
  }

  absl::Nonnull<const google::protobuf::Message*> operator->() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(*this);
    return value_.operator->();
  }

  // Returns `true` if `ParsedMessageValue` is in a valid state.
  explicit operator bool() const { return static_cast<bool>(value_); }

  friend void swap(ParsedMessageValue& lhs, ParsedMessageValue& rhs) noexcept {
    using std::swap;
    swap(lhs.value_, rhs.value_);
  }

 private:
  friend std::pointer_traits<ParsedMessageValue>;

  absl::Nonnull<const google::protobuf::Reflection*> GetReflectionOrDie() const {
    return ABSL_DIE_IF_NULL(GetReflection());  // Crash OK
  }

  Owned<const google::protobuf::Message> value_;
};

}  // namespace cel

namespace std {

template <>
struct pointer_traits<cel::ParsedMessageValue> {
  using pointer = cel::ParsedMessageValue;
  using element_type = typename cel::ParsedMessageValue::element_type;
  using difference_type = ptrdiff_t;

  static element_type* to_address(const pointer& p) noexcept {
    return cel::to_address(p.value_);
  }
};

}  // namespace std

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_MESSAGE_VALUE_H_