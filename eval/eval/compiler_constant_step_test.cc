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
#include "eval/eval/compiler_constant_step.h"

#include <memory>

#include "base/type_factory.h"
#include "base/type_manager.h"
#include "base/value_factory.h"
#include "base/values/int_value.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/test_type_registry.h"
#include "eval/public/activation.h"
#include "eval/public/cel_expression.h"
#include "extensions/protobuf/memory_manager.h"
#include "internal/rtti.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "runtime/runtime_options.h"

namespace google::api::expr::runtime {

namespace {

class CompilerConstantStepTest : public testing::Test {
 public:
  CompilerConstantStepTest()
      : memory_manager_(&arena_),
        type_factory_(memory_manager_),
        type_manager_(type_factory_, cel::TypeProvider::Builtin()),
        value_factory_(type_manager_),
        state_(2, &arena_) {}

 protected:
  google::protobuf::Arena arena_;
  cel::extensions::ProtoMemoryManager memory_manager_;
  cel::TypeFactory type_factory_;
  cel::TypeManager type_manager_;
  cel::ValueFactory value_factory_;

  CelExpressionFlatEvaluationState state_;
  Activation empty_activation_;
  cel::RuntimeOptions options_;
};

TEST_F(CompilerConstantStepTest, Evaluate) {
  ExecutionPath path;
  path.push_back(std::make_unique<CompilerConstantStep>(
      value_factory_.CreateIntValue(42), -1, false));

  ExecutionFrame frame(path, empty_activation_, options_, &state_);

  ASSERT_OK_AND_ASSIGN(cel::Handle<cel::Value> result,
                       frame.Evaluate(CelEvaluationListener()));

  EXPECT_EQ(result->As<cel::IntValue>().value(), 42);
}

TEST_F(CompilerConstantStepTest, TypeId) {
  CompilerConstantStep step(value_factory_.CreateIntValue(42), -1, false);

  ExpressionStep& abstract_step = step;
  EXPECT_EQ(abstract_step.TypeId(),
            cel::internal::TypeId<CompilerConstantStep>());
}

TEST_F(CompilerConstantStepTest, Value) {
  CompilerConstantStep step(value_factory_.CreateIntValue(42), -1, false);

  EXPECT_EQ(step.value()->As<cel::IntValue>().value(), 42);
}

}  // namespace
}  // namespace google::api::expr::runtime