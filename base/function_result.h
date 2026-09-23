// Copyright 2022 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_BASE_FUNCTION_RESULT_H_
#define THIRD_PARTY_CEL_CPP_BASE_FUNCTION_RESULT_H_

#include <cstdint>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "base/function_descriptor.h"

namespace cel {

// Represents a function result that is unknown at the time of execution. This
// allows for lazy evaluation of expensive functions.
class FunctionResult final {
 public:
  FunctionResult() = delete;
  FunctionResult(const FunctionResult&) = default;
  FunctionResult(FunctionResult&&) = default;
  FunctionResult& operator=(const FunctionResult&) = default;
  FunctionResult& operator=(FunctionResult&&) = default;

  explicit FunctionResult(std::string_view name) : name_(name) {}

  absl::string_view name() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return name_; }

  // Equality operator provided for testing. Compatible with set less-than
  // comparator.
  // Compares descriptor then arguments elementwise.
  bool IsEqualTo(const FunctionResult& other) const {
    return name() == other.name();
  }

  // TODO(uncreated-issue/5): re-implement argument capture

 private:
  std::string name_;
};

inline bool operator==(const FunctionResult& lhs, const FunctionResult& rhs) {
  return lhs.IsEqualTo(rhs);
}

inline bool operator<(const FunctionResult& lhs, const FunctionResult& rhs) {
  return lhs.name() < rhs.name();
}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_FUNCTION_RESULT_H_
