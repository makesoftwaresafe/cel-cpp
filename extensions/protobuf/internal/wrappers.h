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

#ifndef THIRD_PARTY_CEL_CPP_EXTENSIONS_PROTOBUF_INTERNAL_WRAPPERS_H_
#define THIRD_PARTY_CEL_CPP_EXTENSIONS_PROTOBUF_INTERNAL_WRAPPERS_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "google/protobuf/message.h"

namespace cel::extensions::protobuf_internal {

absl::StatusOr<bool> UnwrapBoolValueProto(const google::protobuf::Message& message);

absl::StatusOr<absl::Cord> UnwrapBytesValueProto(
    const google::protobuf::Message& message);

absl::StatusOr<double> UnwrapFloatValueProto(const google::protobuf::Message& message);

absl::StatusOr<double> UnwrapDoubleValueProto(const google::protobuf::Message& message);

absl::StatusOr<int64_t> UnwrapIntValueProto(const google::protobuf::Message& message);

absl::StatusOr<int64_t> UnwrapInt32ValueProto(const google::protobuf::Message& message);

absl::StatusOr<int64_t> UnwrapInt64ValueProto(const google::protobuf::Message& message);

absl::StatusOr<absl::Cord> UnwrapStringValueProto(
    const google::protobuf::Message& message);

absl::StatusOr<uint64_t> UnwrapUIntValueProto(const google::protobuf::Message& message);

absl::StatusOr<uint64_t> UnwrapUInt32ValueProto(const google::protobuf::Message& message);

absl::StatusOr<uint64_t> UnwrapUInt64ValueProto(const google::protobuf::Message& message);

absl::Status WrapBoolValueProto(google::protobuf::Message& message, bool value);

absl::Status WrapBytesValueProto(google::protobuf::Message& message,
                                 const absl::Cord& value);

absl::Status WrapFloatValueProto(google::protobuf::Message& message, float value);

absl::Status WrapDoubleValueProto(google::protobuf::Message& message, double value);

absl::Status WrapIntValueProto(google::protobuf::Message& message, int64_t value);

absl::Status WrapInt32ValueProto(google::protobuf::Message& message, int32_t value);

absl::Status WrapInt64ValueProto(google::protobuf::Message& message, int64_t value);

absl::Status WrapUIntValueProto(google::protobuf::Message& message, uint64_t value);

absl::Status WrapUInt32ValueProto(google::protobuf::Message& message, uint32_t value);

absl::Status WrapUInt64ValueProto(google::protobuf::Message& message, uint64_t value);

absl::Status WrapStringValueProto(google::protobuf::Message& message,
                                  const absl::Cord& value);

}  // namespace cel::extensions::protobuf_internal

#endif  // THIRD_PARTY_CEL_CPP_EXTENSIONS_PROTOBUF_INTERNAL_WRAPPERS_H_