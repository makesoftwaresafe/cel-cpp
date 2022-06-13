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

#ifndef THIRD_PARTY_CEL_CPP_BASE_VALUES_UINT_VALUE_H_
#define THIRD_PARTY_CEL_CPP_BASE_VALUES_UINT_VALUE_H_

#include <cstdint>
#include <string>

#include "absl/hash/hash.h"
#include "base/kind.h"
#include "base/type.h"
#include "base/value.h"

namespace cel {

class UintValue final : public Value, public base_internal::ResourceInlined {
 public:
  Persistent<const Type> type() const override;

  Kind kind() const override { return Kind::kUint; }

  std::string DebugString() const override;

  constexpr uint64_t value() const { return value_; }

 private:
  template <base_internal::HandleType H>
  friend class base_internal::ValueHandle;
  friend class base_internal::ValueHandleBase;

  // Called by base_internal::ValueHandleBase to implement Is for Transient and
  // Persistent.
  static bool Is(const Value& value) { return value.kind() == Kind::kUint; }

  // Called by `base_internal::ValueHandle` to construct value inline.
  explicit UintValue(uint64_t value) : value_(value) {}

  UintValue() = delete;

  UintValue(const UintValue&) = default;
  UintValue(UintValue&&) = default;

  // See comments for respective member functions on `Value`.
  void CopyTo(Value& address) const override;
  void MoveTo(Value& address) override;
  bool Equals(const Value& other) const override;
  void HashValue(absl::HashState state) const override;

  uint64_t value_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_VALUES_UINT_VALUE_H_