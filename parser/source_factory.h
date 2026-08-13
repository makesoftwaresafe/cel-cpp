// Copyright 2021 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_PARSER_SOURCE_FACTORY_H_
#define THIRD_PARTY_CEL_CPP_PARSER_SOURCE_FACTORY_H_

#include <cstdint>
#include <utility>

#include "absl/container/flat_hash_map.h"

namespace cel {

class EnrichedSourceInfo {
 public:
  explicit EnrichedSourceInfo(
      absl::flat_hash_map<int64_t, std::pair<int32_t, int32_t>> offsets)
      : offsets_(std::move(offsets)) {}

  EnrichedSourceInfo() = default;
  EnrichedSourceInfo(const EnrichedSourceInfo& other) = default;
  EnrichedSourceInfo& operator=(const EnrichedSourceInfo& other) = default;
  EnrichedSourceInfo(EnrichedSourceInfo&& other) = default;
  EnrichedSourceInfo& operator=(EnrichedSourceInfo&& other) = default;

  const absl::flat_hash_map<int64_t, std::pair<int32_t, int32_t>>& offsets()
      const {
    return offsets_;
  }

 private:
  // A map between node_id and pair of start position and end position
  absl::flat_hash_map<int64_t, std::pair<int32_t, int32_t>> offsets_;
};

}  // namespace cel

namespace google::api::expr::parser {

using EnrichedSourceInfo = ::cel::EnrichedSourceInfo;

}  // namespace google::api::expr::parser

#endif  // THIRD_PARTY_CEL_CPP_PARSER_SOURCE_FACTORY_H_
