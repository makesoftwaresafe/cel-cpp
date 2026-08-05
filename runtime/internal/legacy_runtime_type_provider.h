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

#ifndef THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_LEGACY_RUNTIME_TYPE_PROVIDER_H_
#define THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_LEGACY_RUNTIME_TYPE_PROVIDER_H_

#include <optional>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/type.h"
#include "common/type_reflector.h"
#include "common/value.h"
#include "eval/public/structs/protobuf_descriptor_type_provider.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {

class LegacyRuntimeTypeProvider final
    : public google::api::expr::runtime::ProtobufDescriptorProvider,
      public TypeReflector {
 public:
  LegacyRuntimeTypeProvider(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nullable message_factory)
      : google::api::expr::runtime::ProtobufDescriptorProvider(
            descriptor_pool, message_factory) {}

  absl::StatusOr<absl_nullable ValueBuilderPtr> NewValueBuilder(
      absl::string_view name,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override;

 protected:
  absl::StatusOr<std::optional<Type>> FindTypeImpl(
      absl::string_view name) const override;

  absl::StatusOr<std::optional<StructTypeField>> FindStructTypeFieldByNameImpl(
      absl::string_view type, absl::string_view name) const override;
};

}  // namespace cel::runtime_internal

#endif  // THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_LEGACY_RUNTIME_TYPE_PROVIDER_H_
