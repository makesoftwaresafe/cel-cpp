// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "eval/public/structs/proto_message_type_adapter.h"

#include <optional>
#include <vector>

#include "google/protobuf/wrappers.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "absl/status/status.h"
#include "base/attribute.h"
#include "common/legacy_value.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "eval/public/cel_value.h"
#include "eval/public/message_wrapper.h"
#include "eval/public/structs/legacy_type_adapter.h"
#include "eval/public/structs/legacy_type_info_apis.h"
#include "eval/public/structs/trivial_legacy_type_info.h"
#include "eval/public/testing/matchers.h"
#include "eval/testutil/test_message.pb.h"
#include "extensions/protobuf/memory_manager.h"
#include "internal/testing.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/message_lite.h"

namespace google::api::expr::runtime {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::cel::ProtoWrapperTypeOptions;
using ::cel::extensions::ProtoMemoryManagerRef;
using ::google::protobuf::Int64Value;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::Optional;
using ::testing::Truly;

using LegacyQualifyResult = LegacyTypeAccessApis::LegacyQualifyResult;

class ProtoMessageTypeAccessorTest : public testing::TestWithParam<bool> {
 public:
  ProtoMessageTypeAccessorTest()
      : type_specific_instance_(
            google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
                "google.api.expr.runtime.TestMessage"),
            google::protobuf::MessageFactory::generated_factory()) {}

  const LegacyTypeAccessApis& GetAccessApis() {
    bool use_generic_instance = GetParam();
    if (use_generic_instance) {
      // implementation detail: in general, type info implementations may
      // return a different accessor object based on the message instance, but
      // this implementation returns the same one no matter the message.
      return *GetGenericProtoTypeInfoInstance().GetAccessApis(dummy_);

    } else {
      return type_specific_instance_;
    }
  }

 private:
  ProtoMessageTypeAdapter type_specific_instance_;
  CelValue::MessageWrapper dummy_;
};

TEST_P(ProtoMessageTypeAccessorTest, HasFieldSingular) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();
  TestMessage example;

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.HasField("int64_value", value), IsOkAndHolds(false));
  example.set_int64_value(10);
  EXPECT_THAT(accessor.HasField("int64_value", value), IsOkAndHolds(true));
}

TEST_P(ProtoMessageTypeAccessorTest, HasFieldRepeated) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  TestMessage example;

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.HasField("int64_list", value), IsOkAndHolds(false));
  example.add_int64_list(10);
  EXPECT_THAT(accessor.HasField("int64_list", value), IsOkAndHolds(true));
}

TEST_P(ProtoMessageTypeAccessorTest, HasFieldMap) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  TestMessage example;
  example.set_int64_value(10);

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.HasField("int64_int32_map", value), IsOkAndHolds(false));
  (*example.mutable_int64_int32_map())[2] = 3;
  EXPECT_THAT(accessor.HasField("int64_int32_map", value), IsOkAndHolds(true));
}

TEST_P(ProtoMessageTypeAccessorTest, HasFieldUnknownField) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  TestMessage example;
  example.set_int64_value(10);

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.HasField("unknown_field", value),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_P(ProtoMessageTypeAccessorTest, HasFieldNonMessageType) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  MessageWrapper value(static_cast<const google::protobuf::MessageLite*>(nullptr),
                       nullptr);

  EXPECT_THAT(accessor.HasField("unknown_field", value),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_P(ProtoMessageTypeAccessorTest, GetFieldSingular) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage example;
  example.set_int64_value(10);

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.GetField("int64_value", value,
                                ProtoWrapperTypeOptions::kUnsetNull, manager),
              IsOkAndHolds(test::IsCelInt64(10)));
}

TEST_P(ProtoMessageTypeAccessorTest, GetFieldNoSuchField) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage example;
  example.set_int64_value(10);

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.GetField("unknown_field", value,
                                ProtoWrapperTypeOptions::kUnsetNull, manager),
              IsOkAndHolds(test::IsCelError(StatusIs(
                  absl::StatusCode::kNotFound, HasSubstr("unknown_field")))));
}

TEST_P(ProtoMessageTypeAccessorTest, GetFieldNotAMessage) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  MessageWrapper value(static_cast<const google::protobuf::MessageLite*>(nullptr),
                       nullptr);

  EXPECT_THAT(accessor.GetField("int64_value", value,
                                ProtoWrapperTypeOptions::kUnsetNull, manager),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_P(ProtoMessageTypeAccessorTest, GetFieldRepeated) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage example;
  example.add_int64_list(10);
  example.add_int64_list(20);

  MessageWrapper value(&example, nullptr);

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      accessor.GetField("int64_list", value,
                        ProtoWrapperTypeOptions::kUnsetNull, manager));

  const CelList* held_value;
  ASSERT_TRUE(result.GetValue(&held_value)) << result.DebugString();

  EXPECT_EQ(held_value->size(), 2);
  EXPECT_THAT((*held_value)[0], test::IsCelInt64(10));
  EXPECT_THAT((*held_value)[1], test::IsCelInt64(20));
}

TEST_P(ProtoMessageTypeAccessorTest, GetFieldMap) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage example;
  (*example.mutable_int64_int32_map())[10] = 20;

  MessageWrapper value(&example, nullptr);

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      accessor.GetField("int64_int32_map", value,
                        ProtoWrapperTypeOptions::kUnsetNull, manager));

  const CelMap* held_value;
  ASSERT_TRUE(result.GetValue(&held_value)) << result.DebugString();

  EXPECT_EQ(held_value->size(), 1);
  EXPECT_THAT((*held_value)[CelValue::CreateInt64(10)],
              Optional(test::IsCelInt64(20)));
}

TEST_P(ProtoMessageTypeAccessorTest, GetFieldWrapperType) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage example;
  example.mutable_int64_wrapper_value()->set_value(10);

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.GetField("int64_wrapper_value", value,
                                ProtoWrapperTypeOptions::kUnsetNull, manager),
              IsOkAndHolds(test::IsCelInt64(10)));
}

TEST_P(ProtoMessageTypeAccessorTest, GetFieldWrapperTypeUnsetNullUnbox) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage example;

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(accessor.GetField("int64_wrapper_value", value,
                                ProtoWrapperTypeOptions::kUnsetNull, manager),
              IsOkAndHolds(test::IsCelNull()));

  // Wrapper field present, but default value.
  example.mutable_int64_wrapper_value()->clear_value();
  EXPECT_THAT(accessor.GetField("int64_wrapper_value", value,
                                ProtoWrapperTypeOptions::kUnsetNull, manager),
              IsOkAndHolds(test::IsCelInt64(_)));
}

TEST_P(ProtoMessageTypeAccessorTest,
       GetFieldWrapperTypeUnsetDefaultValueUnbox) {
  google::protobuf::Arena arena;
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage example;

  MessageWrapper value(&example, nullptr);

  EXPECT_THAT(
      accessor.GetField("int64_wrapper_value", value,
                        ProtoWrapperTypeOptions::kUnsetProtoDefault, manager),
      IsOkAndHolds(test::IsCelInt64(_)));

  // Wrapper field present with unset value is used to signal Null, but legacy
  // behavior just returns the proto default value.
  example.mutable_int64_wrapper_value()->clear_value();
  // Same behavior for this option.
  EXPECT_THAT(
      accessor.GetField("int64_wrapper_value", value,
                        ProtoWrapperTypeOptions::kUnsetProtoDefault, manager),
      IsOkAndHolds(test::IsCelInt64(_)));
}

TEST_P(ProtoMessageTypeAccessorTest, IsEqualTo) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  TestMessage example;
  example.mutable_int64_wrapper_value()->set_value(10);
  TestMessage example2;
  example2.mutable_int64_wrapper_value()->set_value(10);

  MessageWrapper value(&example, nullptr);
  MessageWrapper value2(&example2, nullptr);

  EXPECT_TRUE(accessor.IsEqualTo(value, value2));
  EXPECT_TRUE(accessor.IsEqualTo(value2, value));
}

TEST_P(ProtoMessageTypeAccessorTest, IsEqualToSameTypeInequal) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  TestMessage example;
  example.mutable_int64_wrapper_value()->set_value(10);
  TestMessage example2;
  example2.mutable_int64_wrapper_value()->set_value(12);

  MessageWrapper value(&example, nullptr);
  MessageWrapper value2(&example2, nullptr);

  EXPECT_FALSE(accessor.IsEqualTo(value, value2));
  EXPECT_FALSE(accessor.IsEqualTo(value2, value));
}

TEST_P(ProtoMessageTypeAccessorTest, IsEqualToDifferentTypeInequal) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  TestMessage example;
  example.mutable_int64_wrapper_value()->set_value(10);
  Int64Value example2;
  example2.set_value(10);

  MessageWrapper value(&example, nullptr);
  MessageWrapper value2(&example2, nullptr);

  EXPECT_FALSE(accessor.IsEqualTo(value, value2));
  EXPECT_FALSE(accessor.IsEqualTo(value2, value));
}

TEST_P(ProtoMessageTypeAccessorTest, IsEqualToNonMessageInequal) {
  const LegacyTypeAccessApis& accessor = GetAccessApis();

  TestMessage example;
  example.mutable_int64_wrapper_value()->set_value(10);
  TestMessage example2;
  example2.mutable_int64_wrapper_value()->set_value(10);

  MessageWrapper value(&example, nullptr);
  // Upcast to message lite to prevent unwrapping to message.
  MessageWrapper value2(static_cast<const google::protobuf::MessageLite*>(&example2),
                        nullptr);

  EXPECT_FALSE(accessor.IsEqualTo(value, value2));
  EXPECT_FALSE(accessor.IsEqualTo(value2, value));
}

INSTANTIATE_TEST_SUITE_P(GenericAndSpecific, ProtoMessageTypeAccessorTest,
                         testing::Bool());

TEST(GetGenericProtoTypeInfoInstance, GetTypeName) {
  const LegacyTypeInfoApis& info_api = GetGenericProtoTypeInfoInstance();

  TestMessage test_message;
  CelValue::MessageWrapper wrapped_message(&test_message, nullptr);

  EXPECT_EQ(info_api.GetTypename(wrapped_message), test_message.GetTypeName());
}

TEST(GetGenericProtoTypeInfoInstance, DebugString) {
  const LegacyTypeInfoApis& info_api = GetGenericProtoTypeInfoInstance();

  TestMessage test_message;
  test_message.set_string_value("abcd");
  CelValue::MessageWrapper wrapped_message(&test_message, nullptr);

  EXPECT_EQ(info_api.DebugString(wrapped_message),
            test_message.ShortDebugString());
}

TEST(GetGenericProtoTypeInfoInstance, GetAccessApis) {
  const LegacyTypeInfoApis& info_api = GetGenericProtoTypeInfoInstance();

  TestMessage test_message;
  test_message.set_string_value("abcd");
  CelValue::MessageWrapper wrapped_message(&test_message, nullptr);

  auto* accessor = info_api.GetAccessApis(wrapped_message);
  google::protobuf::Arena arena;
  auto manager = ProtoMemoryManagerRef(&arena);

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      accessor->GetField("string_value", wrapped_message,
                         ProtoWrapperTypeOptions::kUnsetNull, manager));
  EXPECT_THAT(result, test::IsCelString("abcd"));
}

TEST(GetGenericProtoTypeInfoInstance, FallbackForNonMessage) {
  const LegacyTypeInfoApis& info_api = GetGenericProtoTypeInfoInstance();

  TestMessage test_message;
  test_message.set_string_value("abcd");
  // Upcast to signal no google::protobuf::Message / reflection support.
  CelValue::MessageWrapper wrapped_message(
      static_cast<const google::protobuf::MessageLite*>(&test_message), nullptr);

  EXPECT_EQ(info_api.GetTypename(wrapped_message), "<unknown message>");
  EXPECT_EQ(info_api.DebugString(wrapped_message), "<unknown message>");

  // Check for not-null.
  CelValue::MessageWrapper null_message(
      static_cast<const google::protobuf::Message*>(nullptr), nullptr);

  EXPECT_EQ(info_api.GetTypename(null_message), "<unknown message>");
  EXPECT_EQ(info_api.DebugString(null_message), "<unknown message>");
}

TEST(ProtoMesssageTypeAdapter, TypeInfoDebug) {
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());

  TestMessage message;
  message.set_int64_value(42);
  EXPECT_THAT(adapter.DebugString(MessageWrapper(&message, &adapter)),
              HasSubstr(message.ShortDebugString()));

  EXPECT_THAT(adapter.DebugString(MessageWrapper()),
              HasSubstr("<unknown message>"));
}

TEST(ProtoMesssageTypeAdapter, TypeInfoName) {
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());

  EXPECT_EQ(adapter.GetTypename(MessageWrapper()),
            "google.api.expr.runtime.TestMessage");
}

TEST(ProtoMesssageTypeAdapter, FindFieldFound) {
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());

  EXPECT_THAT(
      adapter.FindFieldByName("int64_value"),
      Optional(Truly([](const LegacyTypeInfoApis::FieldDescription& desc) {
        return desc.name == "int64_value" && desc.number == 2;
      })))
      << "expected field int64_value: 2";
}

TEST(ProtoMesssageTypeAdapter, FindFieldNotFound) {
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());

  EXPECT_EQ(adapter.FindFieldByName("foo_not_a_field"), std::nullopt);
}

TEST(ProtoMesssageTypeAdapter, TypeInfoAccesor) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  EXPECT_THAT(api->GetField("int64_value", wrapped,
                            ProtoWrapperTypeOptions::kUnsetNull, manager),
              IsOkAndHolds(test::IsCelInt64(42)));
}

TEST(ProtoMesssageTypeAdapter, Qualify) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.mutable_message_value()->set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{2, "int64_value"}};
  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value, test::IsCelInt64(42))));
}

TEST(ProtoMesssageTypeAdapter, QualifyDynamicFieldAccessUnsupported) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.mutable_message_value()->set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::AttributeQualifier::OfString("int64_value")};
  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(ProtoMesssageTypeAdapter, QualifyNoSuchField) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.mutable_message_value()->set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{99, "not_a_field"},
      cel::FieldSpecifier{2, "int64_value"}};
  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(
                  &LegacyQualifyResult::value,
                  test::IsCelError(StatusIs(absl::StatusCode::kNotFound,
                                            HasSubstr("no_such_field"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyHasNoSuchField) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.mutable_message_value()->set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{99, "not_a_field"}};
  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/true, manager),
              IsOkAndHolds(Field(
                  &LegacyQualifyResult::value,
                  test::IsCelError(StatusIs(absl::StatusCode::kNotFound,
                                            HasSubstr("no_such_field"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyNoSuchFieldLeaf) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.mutable_message_value()->set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{99, "not_a_field"}};
  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(
                  &LegacyQualifyResult::value,
                  test::IsCelError(StatusIs(absl::StatusCode::kNotFound,
                                            HasSubstr("no_such_field"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalSupport) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_string_message_map())["@key"].set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{210, "string_message_map"},
      cel::AttributeQualifier::OfString("@key"),
      cel::FieldSpecifier{2, "int64_value"}};

  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value, test::IsCelInt64(42))));
}

TEST(ProtoMesssageTypeAdapter, TypedFieldAccessOnMapUnsupported) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_string_message_map())["@key"].set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{210, "string_message_map"},
      // This is probably a bug, but defer to evaluator for consistent handling.
      cel::FieldSpecifier{2, "value"}, cel::FieldSpecifier{2, "int64_value"}};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalWrongKeyType) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_string_message_map())["@key"].set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{210, "string_message_map"},
      cel::AttributeQualifier::OfInt(0), cel::FieldSpecifier{2, "int64_value"}};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(&LegacyQualifyResult::value,
                                 test::IsCelError(StatusIs(
                                     absl::StatusCode::kInvalidArgument,
                                     HasSubstr("Invalid map key type"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalHasWrongKeyType) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_string_message_map())["@key"].set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{210, "string_message_map"},
      cel::AttributeQualifier::OfInt(0)};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/true, manager),
              IsOkAndHolds(Field(&LegacyQualifyResult::value,
                                 test::IsCelError(StatusIs(
                                     absl::StatusCode::kUnknown,
                                     HasSubstr("No matching overloads"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalSupportNoSuchKey) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_string_message_map())["@key"].set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{210, "string_message_map"},
      cel::AttributeQualifier::OfString("bad_key"),
      cel::FieldSpecifier{2, "int64_value"}};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(
                  &LegacyQualifyResult::value,
                  test::IsCelError(StatusIs(absl::StatusCode::kNotFound,
                                            HasSubstr("Key not found"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalInt32Key) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_int32_int32_map())[0] = 42;
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{205, "int32_int32_map"},
      cel::AttributeQualifier::OfInt(0)};

  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value, test::IsCelInt64(42))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalIntOutOfRange) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_int32_int32_map())[0] = 42;
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{205, "int32_int32_map"},
      cel::AttributeQualifier::OfInt(1LL << 32)};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(
                  &LegacyQualifyResult::value,
                  test::IsCelError(StatusIs(absl::StatusCode::kOutOfRange,
                                            HasSubstr("integer overflow"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalUint32Key) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_uint32_uint32_map())[0] = 42;
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{206, "uint32_uint32_map"},
      cel::AttributeQualifier::OfUint(0)};

  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value, test::IsCelUint64(42))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalUintOutOfRange) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_uint32_uint32_map())[0] = 42;
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{206, "uint32_uint32_map"},
      cel::AttributeQualifier::OfUint(1LL << 32)};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(
                  &LegacyQualifyResult::value,
                  test::IsCelError(StatusIs(absl::StatusCode::kOutOfRange,
                                            HasSubstr("integer overflow"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapTraversalUnexpectedFieldAccess) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_string_message_map())["@key"].set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{210, "string_message_map"},
      // For coverage check that qualify gives up if there's a strong field
      // access requested for a map.
      cel::FieldSpecifier{0, "field_like_key"}};

  auto result = api->Qualify(qualfiers, wrapped,
                             /*presence_test=*/false, manager);

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              StatusIs(absl::StatusCode::kUnimplemented, _));
}

TEST(ProtoMesssageTypeAdapter, UntypedQualifiersNotYetSupported) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  (*message.mutable_string_message_map())["@key"].set_int64_value(42);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::AttributeQualifier::OfString("string_message_map"),
      cel::AttributeQualifier::OfString("@key"),
      cel::AttributeQualifier::OfString("int64_value")};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              StatusIs(absl::StatusCode::kUnimplemented, _));
}

TEST(ProtoMesssageTypeAdapter, QualifyRepeatedIndexWrongType) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.add_message_list()->add_int64_list(1);
  message.add_message_list()->add_int64_list(2);

  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{112, "message_list"},
      cel::AttributeQualifier::OfBool(false),
      cel::FieldSpecifier{102, "int64_list"},
      cel::AttributeQualifier::OfInt(0)};

  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value,
                         test::IsCelError(StatusIs(
                             absl::StatusCode::kUnknown,
                             HasSubstr("No matching overloads found"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyRepeatedTypeCheckError) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  message.add_int64_list(1);
  message.add_int64_list(2);

  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{102, "int64_list"}, cel::AttributeQualifier::OfInt(0),
      // index on an int.
      cel::AttributeQualifier::OfInt(1)};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),

              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unexpected qualify intermediate type")));
}

TEST(ProtoMesssageTypeAdapter, QualifyRepeatedLeaf) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  auto* nested = message.mutable_message_value();
  nested->add_int64_list(1);
  nested->add_int64_list(2);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{102, "int64_list"},
  };

  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value,
                         test::IsCelList(ElementsAre(test::IsCelInt64(1),
                                                     test::IsCelInt64(2))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyRepeatedIndexLeaf) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  auto* nested = message.mutable_message_value();
  nested->add_int64_list(1);
  nested->add_int64_list(2);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{102, "int64_list"},
      cel::AttributeQualifier::OfInt(1)};

  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value, test::IsCelInt64(2))));
}

TEST(ProtoMesssageTypeAdapter, QualifyRepeatedIndexLeafOutOfBounds) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  auto* nested = message.mutable_message_value();
  nested->add_int64_list(1);
  nested->add_int64_list(2);
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{102, "int64_list"},
      cel::AttributeQualifier::OfInt(2)};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(&LegacyQualifyResult::value,
                                 test::IsCelError(StatusIs(
                                     absl::StatusCode::kInvalidArgument,
                                     HasSubstr("index out of bounds"))))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapLeaf) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  auto* nested_map =
      message.mutable_message_value()->mutable_string_int32_map();
  (*nested_map)["@key"] = 42;
  (*nested_map)["@key2"] = -42;
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{203, "string_int32_map"},
  };

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(
                  &LegacyQualifyResult::value, Truly([](const CelValue& v) {
                    return v.IsMap() && v.MapOrDie()->size() == 2;
                  }))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapIndexLeaf) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  auto* nested_map =
      message.mutable_message_value()->mutable_string_int32_map();
  (*nested_map)["@key"] = 42;
  (*nested_map)["@key2"] = -42;
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{203, "string_int32_map"},
      cel::AttributeQualifier::OfString("@key")};

  EXPECT_THAT(
      api->Qualify(qualfiers, wrapped,
                   /*presence_test=*/false, manager),
      IsOkAndHolds(Field(&LegacyQualifyResult::value, test::IsCelInt64(42))));
}

TEST(ProtoMesssageTypeAdapter, QualifyMapIndexLeafWrongType) {
  google::protobuf::Arena arena;
  ProtoMessageTypeAdapter adapter(
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "google.api.expr.runtime.TestMessage"),
      google::protobuf::MessageFactory::generated_factory());
  auto manager = ProtoMemoryManagerRef(&arena);

  TestMessage message;
  auto* nested_map =
      message.mutable_message_value()->mutable_string_int32_map();
  (*nested_map)["@key"] = 42;
  (*nested_map)["@key2"] = -42;
  CelValue::MessageWrapper wrapped(&message, &adapter);

  const LegacyTypeAccessApis* api = adapter.GetAccessApis(MessageWrapper());
  ASSERT_NE(api, nullptr);

  std::vector<cel::SelectQualifier> qualfiers{
      cel::FieldSpecifier{12, "message_value"},
      cel::FieldSpecifier{203, "string_int32_map"},
      cel::AttributeQualifier::OfInt(0)};

  EXPECT_THAT(api->Qualify(qualfiers, wrapped,
                           /*presence_test=*/false, manager),
              IsOkAndHolds(Field(&LegacyQualifyResult::value,
                                 test::IsCelError(StatusIs(
                                     absl::StatusCode::kInvalidArgument,
                                     HasSubstr("Invalid map key type"))))));
}

TEST(ProtoMesssageTypeAdapter, InteropUnwrappingNotTestInstance) {
  google::protobuf::Arena arena;

  TestMessage message;
  message.set_string_value("hello");
  auto legacy_value = CelValue::CreateMessageWrapper(
      CelValue::MessageWrapper(&message, TrivialTypeInfo::GetInstance()));
  cel::Value modern_value;
  ASSERT_THAT(cel::ModernValue(&arena, legacy_value, modern_value), IsOk());
  auto unwrapped = cel::interop_internal::GetLegacyMessage(modern_value);

  // Can't unwrap a non-generic MessageWrapper -- we test by identity to
  // be sure we're not dropping a custom adapter.
  ASSERT_EQ(unwrapped, nullptr);
}

TEST(ProtoMesssageTypeAdapter, InteropUnwrappingGeneric) {
  google::protobuf::Arena arena;

  TestMessage message;
  message.set_string_value("hello");
  auto legacy_value = CelValue::CreateMessageWrapper(
      CelValue::MessageWrapper(&message, &GetGenericProtoTypeInfoInstance()));
  cel::Value modern_value;
  ASSERT_THAT(cel::ModernValue(&arena, legacy_value, modern_value), IsOk());
  auto unwrapped = cel::interop_internal::GetLegacyMessage(modern_value);

  ASSERT_EQ(unwrapped, &message);
}

TEST(ProtoMesssageTypeAdapter, InteropFieldAccess) {
  google::protobuf::Arena arena;

  TestMessage message;
  message.set_string_value("hello");

  const google::protobuf::FieldDescriptor* field =
      message.GetDescriptor()->FindFieldByName("string_value");
  ASSERT_NE(field, nullptr);
  cel::Value field_value;
  ASSERT_THAT(
      cel::interop_internal::WrapLegacyMessageField(
          &message, field, ProtoWrapperTypeOptions::kUnsetNull,
          google::protobuf::DescriptorPool::generated_pool(),
          google::protobuf::MessageFactory::generated_factory(), &arena, &field_value),
      IsOk());

  EXPECT_THAT(field_value, cel::test::StringValueIs("hello"));
}

}  // namespace
}  // namespace google::api::expr::runtime
