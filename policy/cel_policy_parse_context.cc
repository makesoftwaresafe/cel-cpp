// Copyright 2026 Google LLC
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

#include "policy/cel_policy_parse_context.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/log/absl_check.h"
#include "common/source.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_result.h"

namespace cel {

CelPolicy& CelPolicyParseContext::policy() const {
  ABSL_CHECK(policy_ != nullptr)
      << "CelPolicyParseContext::policy() called after GetResult()";
  return *policy_;
}

CelPolicyParseResult CelPolicyParseContext::GetResult() {
  if (policy_ != nullptr && issues_.empty()) {
    return CelPolicyParseResult(std::move(policy_source_), std::move(policy_),
                                std::move(issues_));
  }
  policy_.reset();
  return CelPolicyParseResult(std::move(policy_source_), nullptr,
                              std::move(issues_));
}

void CelPolicyParseContext::ReportError(CelPolicyElementId element_id,
                                        std::string_view message) {
  issues_.push_back(CelPolicyIssue(element_id, message));
}

SourcePosition CelPolicyParseContext::GetCodepointPosition(
    SourcePosition byte_offset) const {
  if (byte_offset < 0) {
    return -1;
  }
  if (byte_to_codepoint_mapping_.empty()) {
    return byte_offset;
  }
  if (static_cast<size_t>(byte_offset) >= byte_to_codepoint_mapping_.size()) {
    return byte_to_codepoint_mapping_.back();
  }
  return byte_to_codepoint_mapping_[byte_offset];
}

}  // namespace cel
