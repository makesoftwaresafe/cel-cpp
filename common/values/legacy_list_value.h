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

// IWYU pragma: private, include "common/values/list_value.h"
// IWYU pragma: friend "common/values/list_value.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_LEGACY_LIST_VALUE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_LEGACY_LIST_VALUE_H_

#include <cstddef>
#include <ostream>
#include <string>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/value_kind.h"
#include "common/values/custom_list_value.h"
#include "common/values/values.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime {
class CelList;
}

namespace cel {

class TypeManager;
class Value;

namespace common_internal {

class LegacyListValue;

class LegacyListValue final
    : private common_internal::ListValueMixin<LegacyListValue> {
 public:
  static constexpr ValueKind kKind = ValueKind::kList;

  explicit LegacyListValue(
      absl::NullabilityUnknown<const google::api::expr::runtime::CelList*> impl)
      : impl_(impl) {}

  // By default, this creates an empty list whose type is `list(dyn)`. Unless
  // you can help it, you should use a more specific typed list value.
  LegacyListValue() = default;
  LegacyListValue(const LegacyListValue&) = default;
  LegacyListValue(LegacyListValue&&) = default;
  LegacyListValue& operator=(const LegacyListValue&) = default;
  LegacyListValue& operator=(LegacyListValue&&) = default;

  constexpr ValueKind kind() const { return kKind; }

  absl::string_view GetTypeName() const { return "list"; }

  std::string DebugString() const;

  // See Value::SerializeTo().
  absl::Status SerializeTo(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::io::ZeroCopyOutputStream*> output) const;

  // See Value::ConvertToJson().
  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const;

  // See Value::ConvertToJsonArray().
  absl::Status ConvertToJsonArray(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const;

  absl::Status Equal(
      const Value& other,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const;
  using ListValueMixin::Equal;

  bool IsZeroValue() const { return IsEmpty(); }

  bool IsEmpty() const;

  size_t Size() const;

  // See ListValueInterface::Get for documentation.
  absl::Status Get(size_t index,
                   absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
                   absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
                   absl::Nonnull<google::protobuf::Arena*> arena,
                   absl::Nonnull<Value*> result) const;
  using ListValueMixin::Get;

  using ForEachCallback = typename CustomListValueInterface::ForEachCallback;

  using ForEachWithIndexCallback =
      typename CustomListValueInterface::ForEachWithIndexCallback;

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const;
  using ListValueMixin::ForEach;

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const;

  absl::Status Contains(
      const Value& other,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const;
  using ListValueMixin::Contains;

  absl::NullabilityUnknown<const google::api::expr::runtime::CelList*>
  cel_list() const {
    return impl_;
  }

  friend void swap(LegacyListValue& lhs, LegacyListValue& rhs) noexcept {
    using std::swap;
    swap(lhs.impl_, rhs.impl_);
  }

 private:
  friend class common_internal::ValueMixin<LegacyListValue>;
  friend class common_internal::ListValueMixin<LegacyListValue>;

  absl::NullabilityUnknown<const google::api::expr::runtime::CelList*> impl_ =
      nullptr;
};

inline std::ostream& operator<<(std::ostream& out,
                                const LegacyListValue& type) {
  return out << type.DebugString();
}

bool IsLegacyListValue(const Value& value);

LegacyListValue GetLegacyListValue(const Value& value);

absl::optional<LegacyListValue> AsLegacyListValue(const Value& value);

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_LEGACY_LIST_VALUE_H_
