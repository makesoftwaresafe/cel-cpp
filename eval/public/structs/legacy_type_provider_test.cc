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

#include "eval/public/structs/legacy_type_provider.h"

#include <optional>

#include "absl/strings/string_view.h"
#include "common/type.h"
#include "eval/public/structs/legacy_type_adapter.h"
#include "eval/public/structs/legacy_type_info_apis.h"
#include "eval/public/structs/proto_message_type_adapter.h"
#include "eval/public/structs/trivial_legacy_type_info.h"
#include "eval/testutil/test_message.pb.h"
#include "internal/testing.h"

namespace google::api::expr::runtime {
namespace {

class LegacyTypeProviderTestEmpty : public LegacyTypeProvider {
 public:
  absl::optional<LegacyTypeAdapter> ProvideLegacyType(
      absl::string_view name) const override {
    return std::nullopt;
  }
};

class LegacyTypeProviderTestImpl : public LegacyTypeProvider {
 public:
  explicit LegacyTypeProviderTestImpl(const LegacyTypeInfoApis* test_type_info)
      : test_type_info_(test_type_info) {}
  absl::optional<LegacyTypeAdapter> ProvideLegacyType(
      absl::string_view name) const override {
    if (name == "test") {
      return LegacyTypeAdapter(nullptr, nullptr);
    }
    return std::nullopt;
  }
  absl::optional<const LegacyTypeInfoApis*> ProvideLegacyTypeInfo(
      absl::string_view name) const override {
    if (name == "test") {
      return test_type_info_;
    }
    return std::nullopt;
  }

 private:
  const LegacyTypeInfoApis* test_type_info_ = nullptr;
};

TEST(LegacyTypeProviderTest, EmptyTypeProviderHasProvideTypeInfo) {
  LegacyTypeProviderTestEmpty provider;
  EXPECT_EQ(provider.ProvideLegacyType("test"), std::nullopt);
  EXPECT_EQ(provider.ProvideLegacyTypeInfo("test"), std::nullopt);
}

TEST(LegacyTypeProviderTest, NonEmptyTypeProviderProvidesSomeTypes) {
  LegacyTypeProviderTestImpl provider(TrivialTypeInfo::GetInstance());
  EXPECT_TRUE(provider.ProvideLegacyType("test").has_value());
  EXPECT_TRUE(provider.ProvideLegacyTypeInfo("test").has_value());
  EXPECT_EQ(provider.ProvideLegacyType("other"), std::nullopt);
  EXPECT_EQ(provider.ProvideLegacyTypeInfo("other"), std::nullopt);
}

TEST(LegacyTypeProviderTest, FindStructTypeFieldByName) {
  ProtoMessageTypeAdapter adapter(TestMessage::descriptor(), nullptr);
  LegacyTypeProviderTestImpl provider(&adapter);

  ASSERT_OK_AND_ASSIGN(
      absl::optional<cel::StructTypeField> field,
      provider.FindStructTypeFieldByName("test", "int32_value"));
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->name(), "int32_value");
  EXPECT_EQ(field->number(), 1);
  EXPECT_EQ(field->GetType(), cel::IntType());

  ASSERT_OK_AND_ASSIGN(
      absl::optional<cel::StructTypeField> not_found_field,
      provider.FindStructTypeFieldByName("test", "unknown_field"));
  EXPECT_FALSE(not_found_field.has_value());
}

}  // namespace
}  // namespace google::api::expr::runtime
