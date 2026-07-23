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

#ifndef THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_AST_FACTORY_H_
#define THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_AST_FACTORY_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "common/constant.h"
#include "common/expr.h"
#include "common/expr_factory.h"
#include "parser/internal/ast_factory_interface.h"

namespace cel::parser_internal {

// Explicit specialization of `AstFactoryInterface` for `cel::Expr` AST nodes.

template <>
class ListNodeBuilder<cel::Expr> {
 public:
  explicit ListNodeBuilder(int64_t id) {
    expr_.set_id(id);
    expr_.mutable_list_expr();
  }

  ListNodeBuilder& Add(cel::Expr element, bool optional = false) {
    cel::ListExpr& list_val = expr_.mutable_list_expr();
    cel::ListExprElement expr_element;
    expr_element.set_expr(std::move(element));
    expr_element.set_optional(optional);
    list_val.mutable_elements().push_back(std::move(expr_element));
    return *this;
  }

  cel::Expr Build() { return std::move(expr_); }

 private:
  cel::Expr expr_;
};

template <>
class MapNodeBuilder<cel::Expr> {
 public:
  explicit MapNodeBuilder(int64_t id) {
    expr_.set_id(id);
    expr_.mutable_map_expr();
  }

  MapNodeBuilder& Add(int64_t id, cel::Expr key, cel::Expr value,
                      bool optional = false) {
    cel::MapExpr& map_val = expr_.mutable_map_expr();
    cel::MapExprEntry entry;
    entry.set_id(id);
    entry.set_key(std::move(key));
    entry.set_value(std::move(value));
    entry.set_optional(optional);
    map_val.mutable_entries().push_back(std::move(entry));
    return *this;
  }

  cel::Expr Build() { return std::move(expr_); }

 private:
  cel::Expr expr_;
};

template <>
class StructNodeBuilder<cel::Expr> {
 public:
  explicit StructNodeBuilder(int64_t id, std::string name) {
    expr_.set_id(id);
    expr_.mutable_struct_expr().set_name(std::move(name));
  }

  StructNodeBuilder& Add(int64_t id, std::string name, cel::Expr value,
                         bool optional = false) {
    cel::StructExpr& struct_val = expr_.mutable_struct_expr();
    cel::StructExprField field;
    field.set_id(id);
    field.set_name(std::move(name));
    field.set_value(std::move(value));
    field.set_optional(optional);
    struct_val.mutable_fields().push_back(std::move(field));
    return *this;
  }

  cel::Expr Build() { return std::move(expr_); }

 private:
  cel::Expr expr_;
};

template <>
class AstFactoryInterface<cel::Expr> : public cel::ExprFactory {
 public:
  AstFactoryInterface() = default;
  AstFactoryInterface(const AstFactoryInterface&) = delete;
  AstFactoryInterface(AstFactoryInterface&&) = delete;
  AstFactoryInterface& operator=(const AstFactoryInterface&) = delete;
  AstFactoryInterface& operator=(AstFactoryInterface&&) = delete;

  ~AstFactoryInterface() override = default;

  // Node inspection and encapsulation API
  int64_t GetId(const cel::Expr& expr) const { return expr.id(); }

  bool IsEmpty(const cel::Expr& expr) const { return expr.id() == 0; }

  bool IsConst(const cel::Expr& expr) const { return expr.has_const_expr(); }

  bool IsIdent(const cel::Expr& expr) const { return expr.has_ident_expr(); }

  absl::string_view GetIdentName(const cel::Expr& expr) const {
    return expr.has_ident_expr() ? absl::string_view(expr.ident_expr().name())
                                 : absl::string_view();
  }

  bool IsSelect(const cel::Expr& expr) const { return expr.has_select_expr(); }

  bool IsPresenceTest(const cel::Expr& expr) const {
    return expr.has_select_expr() && expr.select_expr().test_only();
  }

  const cel::Expr* GetSelectOperand(const cel::Expr& expr) const {
    return expr.has_select_expr() ? &expr.select_expr().operand() : nullptr;
  }

  absl::string_view GetSelectField(const cel::Expr& expr) const {
    return expr.has_select_expr()
               ? absl::string_view(expr.select_expr().field())
               : absl::string_view();
  }

  // Node creation API
  using cel::ExprFactory::NewBoolConst;
  using cel::ExprFactory::NewBytesConst;
  using cel::ExprFactory::NewCall;
  using cel::ExprFactory::NewDoubleConst;
  using cel::ExprFactory::NewIdent;
  using cel::ExprFactory::NewIntConst;
  using cel::ExprFactory::NewMemberCall;
  using cel::ExprFactory::NewNullConst;
  using cel::ExprFactory::NewPresenceTest;
  using cel::ExprFactory::NewSelect;
  using cel::ExprFactory::NewStringConst;
  using cel::ExprFactory::NewUintConst;
  using cel::ExprFactory::NewUnspecified;

  ListNodeBuilder<cel::Expr> NewListBuilder(int64_t id) {
    return ListNodeBuilder<cel::Expr>(id);
  }

  StructNodeBuilder<cel::Expr> NewStructBuilder(int64_t id, std::string name) {
    return StructNodeBuilder<cel::Expr>(id, std::move(name));
  }

  MapNodeBuilder<cel::Expr> NewMapBuilder(int64_t id) {
    return MapNodeBuilder<cel::Expr>(id);
  }
};

using AstFactory = AstFactoryInterface<cel::Expr>;

}  // namespace cel::parser_internal

#endif  // THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_AST_FACTORY_H_
