// Copyright 2026 Google LLC
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

#include "parser/internal/ast_factory.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/expr.h"
#include "internal/status_macros.h"

namespace cel::parser_internal {

ListNodeBuilder<cel::Expr>::ListNodeBuilder(int64_t id) {
  expr_.set_id(id);
  expr_.mutable_list_expr();
}

ListNodeBuilder<cel::Expr>& ListNodeBuilder<cel::Expr>::Add(cel::Expr element,
                                                            bool optional) {
  cel::ListExpr& list_val = expr_.mutable_list_expr();
  cel::ListExprElement expr_element;
  expr_element.set_expr(std::move(element));
  expr_element.set_optional(optional);
  list_val.mutable_elements().push_back(std::move(expr_element));
  return *this;
}

cel::Expr ListNodeBuilder<cel::Expr>::Build() { return std::move(expr_); }

MapNodeBuilder<cel::Expr>::MapNodeBuilder(int64_t id) {
  expr_.set_id(id);
  expr_.mutable_map_expr();
}

MapNodeBuilder<cel::Expr>& MapNodeBuilder<cel::Expr>::Add(int64_t id,
                                                          cel::Expr key,
                                                          cel::Expr value,
                                                          bool optional) {
  cel::MapExpr& map_val = expr_.mutable_map_expr();
  cel::MapExprEntry entry;
  entry.set_id(id);
  entry.set_key(std::move(key));
  entry.set_value(std::move(value));
  entry.set_optional(optional);
  map_val.mutable_entries().push_back(std::move(entry));
  return *this;
}

cel::Expr MapNodeBuilder<cel::Expr>::Build() { return std::move(expr_); }

StructNodeBuilder<cel::Expr>::StructNodeBuilder(int64_t id, std::string name) {
  expr_.set_id(id);
  expr_.mutable_struct_expr().set_name(std::move(name));
}

StructNodeBuilder<cel::Expr>& StructNodeBuilder<cel::Expr>::Add(
    int64_t id, std::string name, cel::Expr value, bool optional) {
  cel::StructExpr& struct_val = expr_.mutable_struct_expr();
  cel::StructExprField field;
  field.set_id(id);
  field.set_name(std::move(name));
  field.set_value(std::move(value));
  field.set_optional(optional);
  struct_val.mutable_fields().push_back(std::move(field));
  return *this;
}

cel::Expr StructNodeBuilder<cel::Expr>::Build() { return std::move(expr_); }

int64_t AstFactoryInterface<cel::Expr>::GetId(const cel::Expr& expr) const {
  return expr.id();
}

bool AstFactoryInterface<cel::Expr>::IsEmpty(const cel::Expr& expr) const {
  return expr.id() == 0;
}

bool AstFactoryInterface<cel::Expr>::IsConst(const cel::Expr& expr) const {
  return expr.has_const_expr();
}

bool AstFactoryInterface<cel::Expr>::IsIdent(const cel::Expr& expr) const {
  return expr.has_ident_expr();
}

absl::string_view AstFactoryInterface<cel::Expr>::GetIdentName(
    const cel::Expr& expr) const {
  return expr.has_ident_expr() ? absl::string_view(expr.ident_expr().name())
                               : absl::string_view();
}

bool AstFactoryInterface<cel::Expr>::IsSelect(const cel::Expr& expr) const {
  return expr.has_select_expr();
}

bool AstFactoryInterface<cel::Expr>::IsPresenceTest(
    const cel::Expr& expr) const {
  return expr.has_select_expr() && expr.select_expr().test_only();
}

const cel::Expr* AstFactoryInterface<cel::Expr>::GetSelectOperand(
    const cel::Expr& expr) const {
  return expr.has_select_expr() ? &expr.select_expr().operand() : nullptr;
}

absl::string_view AstFactoryInterface<cel::Expr>::GetSelectField(
    const cel::Expr& expr) const {
  return expr.has_select_expr() ? absl::string_view(expr.select_expr().field())
                                : absl::string_view();
}

absl::StatusOr<cel::Expr> AstFactoryInterface<cel::Expr>::CopyAndReplace(
    const cel::Expr& expr,
    absl::FunctionRef<std::optional<cel::Expr>(const cel::Expr&)> replacer,
    int max_recursion_depth) const {
  if (max_recursion_depth <= 0) {
    return absl::InvalidArgumentError("recursion limit exceeded");
  }
  std::optional<cel::Expr> replaced = replacer(expr);
  if (replaced.has_value()) {
    return *replaced;
  }

  cel::Expr new_expr = expr;
  switch (new_expr.kind_case()) {
    case cel::ExprKindCase::kUnspecifiedExpr:
    case cel::ExprKindCase::kConstant:
    case cel::ExprKindCase::kIdentExpr:
      break;
    case cel::ExprKindCase::kSelectExpr: {
      cel::SelectExpr& select = new_expr.mutable_select_expr();
      if (select.has_operand()) {
        CEL_ASSIGN_OR_RETURN(cel::Expr operand,
                             CopyAndReplace(select.operand(), replacer,
                                            max_recursion_depth - 1));
        select.set_operand(std::move(operand));
      }
      break;
    }
    case cel::ExprKindCase::kCallExpr: {
      cel::CallExpr& call = new_expr.mutable_call_expr();
      if (call.has_target()) {
        CEL_ASSIGN_OR_RETURN(
            cel::Expr target,
            CopyAndReplace(call.target(), replacer, max_recursion_depth - 1));
        call.set_target(std::move(target));
      }
      for (auto& arg : call.mutable_args()) {
        CEL_ASSIGN_OR_RETURN(
            cel::Expr new_arg,
            CopyAndReplace(arg, replacer, max_recursion_depth - 1));
        arg = std::move(new_arg);
      }
      break;
    }
    case cel::ExprKindCase::kListExpr: {
      cel::ListExpr& list = new_expr.mutable_list_expr();
      for (auto& elem : list.mutable_elements()) {
        if (elem.has_expr()) {
          CEL_ASSIGN_OR_RETURN(
              cel::Expr new_elem,
              CopyAndReplace(elem.expr(), replacer, max_recursion_depth - 1));
          elem.set_expr(std::move(new_elem));
        }
      }
      break;
    }
    case cel::ExprKindCase::kStructExpr: {
      cel::StructExpr& str = new_expr.mutable_struct_expr();
      for (auto& field : str.mutable_fields()) {
        if (field.has_value()) {
          CEL_ASSIGN_OR_RETURN(
              cel::Expr new_val,
              CopyAndReplace(field.value(), replacer, max_recursion_depth - 1));
          field.set_value(std::move(new_val));
        }
      }
      break;
    }
    case cel::ExprKindCase::kMapExpr: {
      cel::MapExpr& map = new_expr.mutable_map_expr();
      for (auto& entry : map.mutable_entries()) {
        if (entry.has_key()) {
          CEL_ASSIGN_OR_RETURN(
              cel::Expr new_key,
              CopyAndReplace(entry.key(), replacer, max_recursion_depth - 1));
          entry.set_key(std::move(new_key));
        }
        if (entry.has_value()) {
          CEL_ASSIGN_OR_RETURN(
              cel::Expr new_val,
              CopyAndReplace(entry.value(), replacer, max_recursion_depth - 1));
          entry.set_value(std::move(new_val));
        }
      }
      break;
    }
    case cel::ExprKindCase::kComprehensionExpr: {
      cel::ComprehensionExpr& comp = new_expr.mutable_comprehension_expr();
      if (comp.has_accu_init()) {
        CEL_ASSIGN_OR_RETURN(cel::Expr new_accu_init,
                             CopyAndReplace(comp.accu_init(), replacer,
                                            max_recursion_depth - 1));
        comp.set_accu_init(std::move(new_accu_init));
      }
      if (comp.has_iter_range()) {
        CEL_ASSIGN_OR_RETURN(cel::Expr new_iter_range,
                             CopyAndReplace(comp.iter_range(), replacer,
                                            max_recursion_depth - 1));
        comp.set_iter_range(std::move(new_iter_range));
      }
      if (comp.has_loop_condition()) {
        CEL_ASSIGN_OR_RETURN(cel::Expr new_loop_condition,
                             CopyAndReplace(comp.loop_condition(), replacer,
                                            max_recursion_depth - 1));
        comp.set_loop_condition(std::move(new_loop_condition));
      }
      if (comp.has_loop_step()) {
        CEL_ASSIGN_OR_RETURN(cel::Expr new_loop_step,
                             CopyAndReplace(comp.loop_step(), replacer,
                                            max_recursion_depth - 1));
        comp.set_loop_step(std::move(new_loop_step));
      }
      if (comp.has_result()) {
        CEL_ASSIGN_OR_RETURN(
            cel::Expr new_result,
            CopyAndReplace(comp.result(), replacer, max_recursion_depth - 1));
        comp.set_result(std::move(new_result));
      }
      break;
    }
  }
  return new_expr;
}

ListNodeBuilder<cel::Expr> AstFactoryInterface<cel::Expr>::NewListBuilder(
    int64_t id) {
  return ListNodeBuilder<cel::Expr>(id);
}

StructNodeBuilder<cel::Expr> AstFactoryInterface<cel::Expr>::NewStructBuilder(
    int64_t id, std::string name) {
  return StructNodeBuilder<cel::Expr>(id, std::move(name));
}

MapNodeBuilder<cel::Expr> AstFactoryInterface<cel::Expr>::NewMapBuilder(
    int64_t id) {
  return MapNodeBuilder<cel::Expr>(id);
}

}  // namespace cel::parser_internal
