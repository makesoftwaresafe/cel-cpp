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

#include <cstddef>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/memory.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/list_value_builder.h"
#include "common/values/map_value_builder.h"
#include "common/values/values.h"
#include "eval/public/cel_value.h"
#include "internal/status_macros.h"
#include "internal/well_known_types.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {

namespace {

using ::cel::well_known_types::StructReflection;
using ::cel::well_known_types::ValueReflection;
using ::google::api::expr::runtime::CelList;
using ::google::api::expr::runtime::CelValue;

absl::Status NoSuchKeyError(const Value& key) {
  return absl::NotFoundError(
      absl::StrCat("Key not found in map : ", key.DebugString()));
}

absl::Status InvalidMapKeyTypeError(ValueKind kind) {
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid map key type: '", ValueKindToString(kind), "'"));
}

class EmptyMapValue final : public common_internal::CompatMapValue {
 public:
  static const EmptyMapValue& Get() {
    static const absl::NoDestructor<EmptyMapValue> empty;
    return *empty;
  }

  EmptyMapValue() = default;

  std::string DebugString() const override { return "{}"; }

  bool IsEmpty() const override { return true; }

  size_t Size() const override { return 0; }

  absl::Status ListKeys(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<ListValue*> result) const override {
    *result = ListValue();
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator() const override {
    return NewEmptyValueIterator();
  }

  absl::Status ConvertToJson(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(json != nullptr);
    ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                   google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

    ValueReflection value_reflection;
    CEL_RETURN_IF_ERROR(value_reflection.Initialize(json->GetDescriptor()));
    value_reflection.MutableListValue(json)->Clear();
    return absl::OkStatus();
  }

  absl::Status ConvertToJsonObject(
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Message*> json) const override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(json != nullptr);
    ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                   google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT);

    json->Clear();
    return absl::OkStatus();
  }

  CustomMapValue Clone(absl::Nonnull<google::protobuf::Arena*>) const override {
    return CustomMapValue();
  }

  absl::optional<CelValue> operator[](CelValue key) const override {
    return absl::nullopt;
  }

  using CompatMapValue::Get;
  absl::optional<CelValue> Get(google::protobuf::Arena* arena,
                               CelValue key) const override {
    return absl::nullopt;
  }

  using CompatMapValue::Has;
  absl::StatusOr<bool> Has(const CelValue& key) const override { return false; }

  int size() const override { return static_cast<int>(Size()); }

  absl::StatusOr<const CelList*> ListKeys() const override {
    return common_internal::EmptyCompatListValue();
  }

  absl::StatusOr<const CelList*> ListKeys(google::protobuf::Arena*) const override {
    return ListKeys();
  }

 private:
  absl::StatusOr<bool> FindImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena,
      absl::Nonnull<Value*> result) const override {
    return false;
  }

  absl::StatusOr<bool> HasImpl(
      const Value& key,
      absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
      absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
      absl::Nonnull<google::protobuf::Arena*> arena) const override {
    return false;
  }
};

}  // namespace

namespace common_internal {

absl::Nonnull<const CompatMapValue*> EmptyCompatMapValue() {
  return &EmptyMapValue::Get();
}

}  // namespace common_internal

absl::Status CustomMapValueInterface::SerializeTo(
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<absl::Cord*> value) const {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(value != nullptr);

  StructReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(descriptor_pool));
  const google::protobuf::Message* prototype =
      message_factory->GetPrototype(reflection.GetDescriptor());
  if (prototype == nullptr) {
    return absl::UnknownError(
        absl::StrCat("failed to get message prototype: ",
                     reflection.GetDescriptor()->full_name()));
  }
  google::protobuf::Arena arena;
  google::protobuf::Message* message = prototype->New(&arena);
  CEL_RETURN_IF_ERROR(
      ConvertToJsonObject(descriptor_pool, message_factory, message));
  if (!message->SerializePartialToCord(value)) {
    return absl::UnknownError(
        "failed to serialize message: google.protobuf.Struct");
  }
  return absl::OkStatus();
}

absl::Status CustomMapValueInterface::Get(
    const Value& key,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const {
  CEL_ASSIGN_OR_RETURN(
      bool ok, Find(key, descriptor_pool, message_factory, arena, result));
  if (ABSL_PREDICT_FALSE(!ok)) {
    switch (result->kind()) {
      case ValueKind::kError:
        ABSL_FALLTHROUGH_INTENDED;
      case ValueKind::kUnknown:
        break;
      default:
        *result = ErrorValue(NoSuchKeyError(key));
        break;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<bool> CustomMapValueInterface::Find(
    const Value& key,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const {
  switch (key.kind()) {
    case ValueKind::kError:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUnknown:
      *result = Value(key);
      return false;
    case ValueKind::kBool:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kInt:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUint:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kString:
      break;
    default:
      *result = ErrorValue(InvalidMapKeyTypeError(key.kind()));
      return false;
  }
  CEL_ASSIGN_OR_RETURN(
      auto ok, FindImpl(key, descriptor_pool, message_factory, arena, result));
  if (ok) {
    return true;
  }
  *result = NullValue{};
  return false;
}

absl::Status CustomMapValueInterface::Has(
    const Value& key,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const {
  switch (key.kind()) {
    case ValueKind::kError:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUnknown:
      *result = Value{key};
      return absl::OkStatus();
    case ValueKind::kBool:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kInt:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUint:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kString:
      break;
    default:
      return InvalidMapKeyTypeError(key.kind());
  }
  CEL_ASSIGN_OR_RETURN(auto has,
                       HasImpl(key, descriptor_pool, message_factory, arena));
  *result = BoolValue(has);
  return absl::OkStatus();
}

absl::Status CustomMapValueInterface::ForEach(
    ForEachCallback callback,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena) const {
  CEL_ASSIGN_OR_RETURN(auto iterator, NewIterator());
  while (iterator->HasNext()) {
    Value key;
    Value value;
    CEL_RETURN_IF_ERROR(
        iterator->Next(descriptor_pool, message_factory, arena, &key));
    CEL_RETURN_IF_ERROR(
        Get(key, descriptor_pool, message_factory, arena, &value));
    CEL_ASSIGN_OR_RETURN(auto ok, callback(key, value));
    if (!ok) {
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status CustomMapValueInterface::Equal(
    const Value& other,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) const {
  if (auto list_value = other.As<MapValue>(); list_value.has_value()) {
    return MapValueEqual(*this, *list_value, descriptor_pool, message_factory,
                         arena, result);
  }
  *result = FalseValue();
  return absl::OkStatus();
}

CustomMapValue::CustomMapValue()
    : CustomMapValue(Owned(Owner::None(), &EmptyMapValue::Get())) {}

CustomMapValue CustomMapValue::Clone(
    absl::Nonnull<google::protobuf::Arena*> arena) const {
  ABSL_DCHECK(arena != nullptr);
  ABSL_DCHECK(*this);

  if (ABSL_PREDICT_FALSE(!interface_)) {
    return CustomMapValue();
  }
  if (interface_.arena() != arena) {
    return interface_->Clone(arena);
  }
  return *this;
}

}  // namespace cel
