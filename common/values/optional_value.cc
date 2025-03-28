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

#include <cstdint>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/arena.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {

namespace {

struct OptionalValueDispatcher : public OpaqueValueDispatcher {
  using HasValue =
      bool (*)(absl::Nonnull<const OptionalValueDispatcher*> dispatcher,
               CustomValueContent content);
  using Value =
      void (*)(absl::Nonnull<const OptionalValueDispatcher*> dispatcher,
               CustomValueContent content, absl::Nonnull<cel::Value*> result);

  absl::Nonnull<HasValue> has_value;

  absl::Nonnull<Value> value;
};

NativeTypeId OptionalValueGetTypeId(absl::Nonnull<const OpaqueValueDispatcher*>,
                                    OpaqueValueContent) {
  return NativeTypeId::For<OptionalValue>();
}

absl::string_view OptionalValueGetTypeName(
    absl::Nonnull<const OpaqueValueDispatcher*>, OpaqueValueContent) {
  return "optional_type";
}

OpaqueType OptionalValueGetRuntimeType(
    absl::Nonnull<const OpaqueValueDispatcher*>, OpaqueValueContent) {
  return OptionalType();
}

std::string OptionalValueDebugString(
    absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
    OpaqueValueContent content) {
  if (!static_cast<const OptionalValueDispatcher*>(dispatcher)
           ->has_value(static_cast<const OptionalValueDispatcher*>(dispatcher),
                       content)) {
    return "optional.none()";
  }
  Value value;
  static_cast<const OptionalValueDispatcher*>(dispatcher)
      ->value(static_cast<const OptionalValueDispatcher*>(dispatcher), content,
              &value);
  return absl::StrCat("optional.of(", value.DebugString(), ")");
}

bool OptionalValueHasValue(absl::Nonnull<const OptionalValueDispatcher*>,
                           OpaqueValueContent) {
  return true;
}

absl::Status OptionalValueEqual(
    absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
    OpaqueValueContent content, const OpaqueValue& other,
    absl::Nonnull<const google::protobuf::DescriptorPool*> descriptor_pool,
    absl::Nonnull<google::protobuf::MessageFactory*> message_factory,
    absl::Nonnull<google::protobuf::Arena*> arena, absl::Nonnull<Value*> result) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(arena != nullptr);
  ABSL_DCHECK(result != nullptr);
  if (auto other_optional = other.AsOptional(); other_optional) {
    const bool lhs_has_value =
        static_cast<const OptionalValueDispatcher*>(dispatcher)
            ->has_value(static_cast<const OptionalValueDispatcher*>(dispatcher),
                        content);
    const bool rhs_has_value = other_optional->HasValue();
    if (lhs_has_value != rhs_has_value) {
      *result = FalseValue();
      return absl::OkStatus();
    }
    if (!lhs_has_value) {
      *result = TrueValue();
      return absl::OkStatus();
    }
    Value lhs_value;
    Value rhs_value;
    static_cast<const OptionalValueDispatcher*>(dispatcher)
        ->value(static_cast<const OptionalValueDispatcher*>(dispatcher),
                content, &lhs_value);
    other_optional->Value(&rhs_value);
    return lhs_value.Equal(rhs_value, descriptor_pool, message_factory, arena,
                           result);
  }
  *result = FalseValue();
  return absl::OkStatus();
}

ABSL_CONST_INIT const OptionalValueDispatcher
    empty_optional_value_dispatcher = {
        {
            .get_type_id = &OptionalValueGetTypeId,
            .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                            OpaqueValueContent)
                -> absl::Nullable<google::protobuf::Arena*> { return nullptr; },
            .get_type_name = &OptionalValueGetTypeName,
            .debug_string = &OptionalValueDebugString,
            .get_runtime_type = &OptionalValueGetRuntimeType,
            .equal = &OptionalValueEqual,
            .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                        OpaqueValueContent content,
                        absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
              return common_internal::MakeOptionalValue(dispatcher, content);
            },
        },
        [](absl::Nonnull<const OptionalValueDispatcher*> dispatcher,
           CustomValueContent content) -> bool { return false; },
        [](absl::Nonnull<const OptionalValueDispatcher*> dispatcher,
           CustomValueContent content,
           absl::Nonnull<cel::Value*> result) -> void {
          *result = ErrorValue(
              absl::FailedPreconditionError("optional.none() dereference"));
        },
};

ABSL_CONST_INIT const OptionalValueDispatcher null_optional_value_dispatcher = {
    {
        .get_type_id = &OptionalValueGetTypeId,
        .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                        OpaqueValueContent) -> absl::Nullable<google::protobuf::Arena*> {
          return nullptr;
        },
        .get_type_name = &OptionalValueGetTypeName,
        .debug_string = &OptionalValueDebugString,
        .get_runtime_type = &OptionalValueGetRuntimeType,
        .equal = &OptionalValueEqual,
        .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                    OpaqueValueContent content,
                    absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
          return common_internal::MakeOptionalValue(dispatcher, content);
        },
    },
    &OptionalValueHasValue,
    [](absl::Nonnull<const OptionalValueDispatcher*>, CustomValueContent,
       absl::Nonnull<cel::Value*> result) -> void { *result = NullValue(); },
};

ABSL_CONST_INIT const OptionalValueDispatcher bool_optional_value_dispatcher = {
    {
        .get_type_id = &OptionalValueGetTypeId,
        .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                        OpaqueValueContent) -> absl::Nullable<google::protobuf::Arena*> {
          return nullptr;
        },
        .get_type_name = &OptionalValueGetTypeName,
        .debug_string = &OptionalValueDebugString,
        .get_runtime_type = &OptionalValueGetRuntimeType,
        .equal = &OptionalValueEqual,
        .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                    OpaqueValueContent content,
                    absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
          return common_internal::MakeOptionalValue(dispatcher, content);
        },
    },
    &OptionalValueHasValue,
    [](absl::Nonnull<const OptionalValueDispatcher*>,
       CustomValueContent content, absl::Nonnull<cel::Value*> result) -> void {
      *result = BoolValue(content.To<bool>());
    },
};

ABSL_CONST_INIT const OptionalValueDispatcher int_optional_value_dispatcher = {
    {
        .get_type_id = &OptionalValueGetTypeId,
        .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                        OpaqueValueContent) -> absl::Nullable<google::protobuf::Arena*> {
          return nullptr;
        },
        .get_type_name = &OptionalValueGetTypeName,
        .debug_string = &OptionalValueDebugString,
        .get_runtime_type = &OptionalValueGetRuntimeType,
        .equal = &OptionalValueEqual,
        .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                    OpaqueValueContent content,
                    absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
          return common_internal::MakeOptionalValue(dispatcher, content);
        },
    },
    &OptionalValueHasValue,
    [](absl::Nonnull<const OptionalValueDispatcher*>,
       CustomValueContent content, absl::Nonnull<cel::Value*> result) -> void {
      *result = IntValue(content.To<int64_t>());
    },
};

ABSL_CONST_INIT const OptionalValueDispatcher uint_optional_value_dispatcher = {
    {
        .get_type_id = &OptionalValueGetTypeId,
        .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                        OpaqueValueContent) -> absl::Nullable<google::protobuf::Arena*> {
          return nullptr;
        },
        .get_type_name = &OptionalValueGetTypeName,
        .debug_string = &OptionalValueDebugString,
        .get_runtime_type = &OptionalValueGetRuntimeType,
        .equal = &OptionalValueEqual,
        .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                    OpaqueValueContent content,
                    absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
          return common_internal::MakeOptionalValue(dispatcher, content);
        },
    },
    &OptionalValueHasValue,
    [](absl::Nonnull<const OptionalValueDispatcher*>,
       CustomValueContent content, absl::Nonnull<cel::Value*> result) -> void {
      *result = UintValue(content.To<uint64_t>());
    },
};

ABSL_CONST_INIT const OptionalValueDispatcher
    double_optional_value_dispatcher = {
        {
            .get_type_id = &OptionalValueGetTypeId,
            .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                            OpaqueValueContent)
                -> absl::Nullable<google::protobuf::Arena*> { return nullptr; },
            .get_type_name = &OptionalValueGetTypeName,
            .debug_string = &OptionalValueDebugString,
            .get_runtime_type = &OptionalValueGetRuntimeType,
            .equal = &OptionalValueEqual,
            .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                        OpaqueValueContent content,
                        absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
              return common_internal::MakeOptionalValue(dispatcher, content);
            },
        },
        &OptionalValueHasValue,
        [](absl::Nonnull<const OptionalValueDispatcher*>,
           CustomValueContent content,
           absl::Nonnull<cel::Value*> result) -> void {
          *result = DoubleValue(content.To<double>());
        },
};

ABSL_CONST_INIT const OptionalValueDispatcher
    duration_optional_value_dispatcher = {
        {
            .get_type_id = &OptionalValueGetTypeId,
            .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                            OpaqueValueContent)
                -> absl::Nullable<google::protobuf::Arena*> { return nullptr; },
            .get_type_name = &OptionalValueGetTypeName,
            .debug_string = &OptionalValueDebugString,
            .get_runtime_type = &OptionalValueGetRuntimeType,
            .equal = &OptionalValueEqual,
            .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                        OpaqueValueContent content,
                        absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
              return common_internal::MakeOptionalValue(dispatcher, content);
            },
        },
        &OptionalValueHasValue,
        [](absl::Nonnull<const OptionalValueDispatcher*>,
           CustomValueContent content,
           absl::Nonnull<cel::Value*> result) -> void {
          *result = UnsafeDurationValue(content.To<absl::Duration>());
        },
};

ABSL_CONST_INIT const OptionalValueDispatcher
    timestamp_optional_value_dispatcher = {
        {
            .get_type_id = &OptionalValueGetTypeId,
            .get_arena = [](absl::Nonnull<const OpaqueValueDispatcher*>,
                            OpaqueValueContent)
                -> absl::Nullable<google::protobuf::Arena*> { return nullptr; },
            .get_type_name = &OptionalValueGetTypeName,
            .debug_string = &OptionalValueDebugString,
            .get_runtime_type = &OptionalValueGetRuntimeType,
            .equal = &OptionalValueEqual,
            .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                        OpaqueValueContent content,
                        absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
              return common_internal::MakeOptionalValue(dispatcher, content);
            },
        },
        &OptionalValueHasValue,
        [](absl::Nonnull<const OptionalValueDispatcher*>,
           CustomValueContent content,
           absl::Nonnull<cel::Value*> result) -> void {
          *result = UnsafeTimestampValue(content.To<absl::Time>());
        },
};

struct OptionalValueContent {
  absl::Nonnull<const Value*> value;
  absl::Nonnull<google::protobuf::Arena*> arena;
};

ABSL_CONST_INIT const OptionalValueDispatcher optional_value_dispatcher = {
    {
        .get_type_id = &OptionalValueGetTypeId,
        .get_arena =
            [](absl::Nonnull<const OpaqueValueDispatcher*>,
               OpaqueValueContent content) -> absl::Nullable<google::protobuf::Arena*> {
          return content.To<OptionalValueContent>().arena;
        },
        .get_type_name = &OptionalValueGetTypeName,
        .debug_string = &OptionalValueDebugString,
        .get_runtime_type = &OptionalValueGetRuntimeType,
        .equal = &OptionalValueEqual,
        .clone = [](absl::Nonnull<const OpaqueValueDispatcher*> dispatcher,
                    OpaqueValueContent content,
                    absl::Nonnull<google::protobuf::Arena*> arena) -> OpaqueValue {
          ABSL_DCHECK(arena != nullptr);

          absl::Nonnull<cel::Value*> result = ::new (
              arena->AllocateAligned(sizeof(cel::Value), alignof(cel::Value)))
              cel::Value(
                  content.To<OptionalValueContent>().value->Clone(arena));
          if (!ArenaTraits<>::trivially_destructible(result)) {
            arena->OwnDestructor(result);
          }
          return common_internal::MakeOptionalValue(
              &optional_value_dispatcher,
              OpaqueValueContent::From(
                  OptionalValueContent{.value = result, .arena = arena}));
        },
    },
    &OptionalValueHasValue,
    [](absl::Nonnull<const OptionalValueDispatcher*>,
       CustomValueContent content, absl::Nonnull<cel::Value*> result) -> void {
      *result = *content.To<OptionalValueContent>().value;
    },
};

}  // namespace

OptionalValue OptionalValue::Of(cel::Value value,
                                absl::Nonnull<google::protobuf::Arena*> arena) {
  ABSL_DCHECK(value.kind() != ValueKind::kError &&
              value.kind() != ValueKind::kUnknown);
  ABSL_DCHECK(arena != nullptr);

  // We can actually fit a lot more of the underlying values, avoiding arena
  // allocations and destructors. For now, we just do scalars.
  switch (value.kind()) {
    case ValueKind::kNull:
      return OptionalValue(&null_optional_value_dispatcher,
                           OpaqueValueContent::Zero());
    case ValueKind::kBool:
      return OptionalValue(
          &bool_optional_value_dispatcher,
          OpaqueValueContent::From(absl::implicit_cast<bool>(value.GetBool())));
    case ValueKind::kInt:
      return OptionalValue(&int_optional_value_dispatcher,
                           OpaqueValueContent::From(
                               absl::implicit_cast<int64_t>(value.GetInt())));
    case ValueKind::kUint:
      return OptionalValue(&uint_optional_value_dispatcher,
                           OpaqueValueContent::From(
                               absl::implicit_cast<uint64_t>(value.GetUint())));
    case ValueKind::kDouble:
      return OptionalValue(&double_optional_value_dispatcher,
                           OpaqueValueContent::From(
                               absl::implicit_cast<double>(value.GetDouble())));
    case ValueKind::kDuration:
      return OptionalValue(
          &duration_optional_value_dispatcher,
          OpaqueValueContent::From(value.GetDuration().ToDuration()));
    case ValueKind::kTimestamp:
      return OptionalValue(
          &timestamp_optional_value_dispatcher,
          OpaqueValueContent::From(value.GetTimestamp().ToTime()));
    default: {
      absl::Nonnull<cel::Value*> result = ::new (
          arena->AllocateAligned(sizeof(cel::Value), alignof(cel::Value)))
          cel::Value(std::move(value));
      if (!ArenaTraits<>::trivially_destructible(result)) {
        arena->OwnDestructor(result);
      }
      return OptionalValue(&optional_value_dispatcher,
                           OpaqueValueContent::From(OptionalValueContent{
                               .value = result, .arena = arena}));
    }
  }
}

OptionalValue OptionalValue::None() {
  return OptionalValue(&empty_optional_value_dispatcher,
                       OpaqueValueContent::Zero());
}

bool OptionalValue::HasValue() const {
  return static_cast<const OptionalValueDispatcher*>(OpaqueValue::dispatcher())
      ->has_value(static_cast<const OptionalValueDispatcher*>(
                      OpaqueValue::dispatcher()),
                  OpaqueValue::content());
}

void OptionalValue::Value(absl::Nonnull<cel::Value*> result) const {
  ABSL_DCHECK(result != nullptr);

  static_cast<const OptionalValueDispatcher*>(OpaqueValue::dispatcher())
      ->value(static_cast<const OptionalValueDispatcher*>(
                  OpaqueValue::dispatcher()),
              OpaqueValue::content(), result);
}

cel::Value OptionalValue::Value() const {
  cel::Value result;
  Value(&result);
  return result;
}

}  // namespace cel
