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

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "common/expr.h"
#include "internal/testing.h"

namespace cel::parser_internal {
namespace {

TEST(AstFactoryInterfaceTest, AstFactoryUnspecified) {
  AstFactory factory;
  cel::Expr expr = factory.NewUnspecified(1);
  EXPECT_EQ(factory.GetId(expr), 1);
  EXPECT_FALSE(factory.IsEmpty(expr));
  EXPECT_FALSE(factory.IsConst(expr));
  EXPECT_FALSE(factory.IsIdent(expr));
  EXPECT_FALSE(factory.IsSelect(expr));

  cel::Expr empty_expr = factory.NewUnspecified(0);
  EXPECT_TRUE(factory.IsEmpty(empty_expr));
}

TEST(AstFactoryInterfaceTest, AstFactoryConstNodes) {
  AstFactory factory;

  cel::Expr null_expr = factory.NewNullConst(10);
  EXPECT_EQ(factory.GetId(null_expr), 10);
  EXPECT_TRUE(factory.IsConst(null_expr));
  ASSERT_TRUE(null_expr.has_const_expr());
  EXPECT_TRUE(null_expr.const_expr().has_null_value());

  cel::Expr bool_expr = factory.NewBoolConst(11, true);
  EXPECT_EQ(factory.GetId(bool_expr), 11);
  EXPECT_TRUE(factory.IsConst(bool_expr));
  ASSERT_TRUE(bool_expr.has_const_expr());
  EXPECT_TRUE(bool_expr.const_expr().bool_value());

  cel::Expr int_expr = factory.NewIntConst(12, -42);
  EXPECT_EQ(factory.GetId(int_expr), 12);
  EXPECT_TRUE(factory.IsConst(int_expr));
  ASSERT_TRUE(int_expr.has_const_expr());
  EXPECT_EQ(int_expr.const_expr().int_value(), -42);

  cel::Expr uint_expr = factory.NewUintConst(13, 100u);
  EXPECT_EQ(factory.GetId(uint_expr), 13);
  EXPECT_TRUE(factory.IsConst(uint_expr));
  ASSERT_TRUE(uint_expr.has_const_expr());
  EXPECT_EQ(uint_expr.const_expr().uint_value(), 100u);

  cel::Expr double_expr = factory.NewDoubleConst(14, 3.14159);
  EXPECT_EQ(factory.GetId(double_expr), 14);
  EXPECT_TRUE(factory.IsConst(double_expr));
  ASSERT_TRUE(double_expr.has_const_expr());
  EXPECT_DOUBLE_EQ(double_expr.const_expr().double_value(), 3.14159);

  cel::Expr bytes_expr = factory.NewBytesConst(15, "bytes_val");
  EXPECT_EQ(factory.GetId(bytes_expr), 15);
  EXPECT_TRUE(factory.IsConst(bytes_expr));
  ASSERT_TRUE(bytes_expr.has_const_expr());
  EXPECT_EQ(bytes_expr.const_expr().bytes_value(), "bytes_val");

  cel::Expr string_expr = factory.NewStringConst(16, "string_val");
  EXPECT_EQ(factory.GetId(string_expr), 16);
  EXPECT_TRUE(factory.IsConst(string_expr));
  ASSERT_TRUE(string_expr.has_const_expr());
  EXPECT_EQ(string_expr.const_expr().string_value(), "string_val");
}

TEST(AstFactoryInterfaceTest, AstFactoryIdentAndSelect) {
  AstFactory factory;

  cel::Expr ident_expr = factory.NewIdent(20, "foo");
  EXPECT_EQ(factory.GetId(ident_expr), 20);
  EXPECT_TRUE(factory.IsIdent(ident_expr));
  EXPECT_EQ(factory.GetIdentName(ident_expr), "foo");

  cel::Expr select_expr =
      factory.NewSelect(21, factory.NewIdent(20, "foo"), "bar");
  EXPECT_EQ(factory.GetId(select_expr), 21);
  EXPECT_TRUE(factory.IsSelect(select_expr));
  EXPECT_FALSE(factory.IsPresenceTest(select_expr));
  EXPECT_EQ(factory.GetSelectField(select_expr), "bar");
  ASSERT_NE(factory.GetSelectOperand(select_expr), nullptr);
  EXPECT_EQ(factory.GetIdentName(*factory.GetSelectOperand(select_expr)),
            "foo");

  cel::Expr presence_expr =
      factory.NewPresenceTest(22, factory.NewIdent(20, "foo"), "bar");
  EXPECT_EQ(factory.GetId(presence_expr), 22);
  EXPECT_TRUE(factory.IsSelect(presence_expr));
  EXPECT_TRUE(factory.IsPresenceTest(presence_expr));
  EXPECT_EQ(factory.GetSelectField(presence_expr), "bar");
}

TEST(AstFactoryInterfaceTest, AstFactoryCalls) {
  AstFactory factory;

  std::vector<cel::Expr> call_args;
  call_args.push_back(factory.NewIntConst(30, 1));
  call_args.push_back(factory.NewIntConst(31, 2));
  cel::Expr call_expr = factory.NewCall(32, "_+_", std::move(call_args));
  EXPECT_EQ(factory.GetId(call_expr), 32);
  ASSERT_TRUE(call_expr.has_call_expr());
  EXPECT_EQ(call_expr.call_expr().function(), "_+_");
  EXPECT_FALSE(call_expr.call_expr().has_target());
  EXPECT_EQ(call_expr.call_expr().args().size(), 2);

  std::vector<cel::Expr> member_args;
  member_args.push_back(factory.NewStringConst(33, "suffix"));
  cel::Expr member_call_expr = factory.NewMemberCall(
      34, "endsWith", factory.NewIdent(35, "str_var"), std::move(member_args));
  EXPECT_EQ(factory.GetId(member_call_expr), 34);
  ASSERT_TRUE(member_call_expr.has_call_expr());
  EXPECT_EQ(member_call_expr.call_expr().function(), "endsWith");
  EXPECT_TRUE(member_call_expr.call_expr().has_target());
  EXPECT_EQ(member_call_expr.call_expr().target().ident_expr().name(),
            "str_var");
  EXPECT_EQ(member_call_expr.call_expr().args().size(), 1);
}

TEST(AstFactoryInterfaceTest, AstFactoryList) {
  AstFactory factory;

  cel::Expr list_expr = factory.NewListBuilder(42)
                            .Add(factory.NewIntConst(40, 1), false)
                            .Add(factory.NewIntConst(41, 2), true)
                            .Build();
  EXPECT_EQ(factory.GetId(list_expr), 42);
  ASSERT_TRUE(list_expr.has_list_expr());
  ASSERT_EQ(list_expr.list_expr().elements().size(), 2);
  EXPECT_FALSE(list_expr.list_expr().elements()[0].optional());
  EXPECT_EQ(list_expr.list_expr().elements()[0].expr().const_expr().int_value(),
            1);
  EXPECT_TRUE(list_expr.list_expr().elements()[1].optional());
  EXPECT_EQ(list_expr.list_expr().elements()[1].expr().const_expr().int_value(),
            2);
}

TEST(AstFactoryInterfaceTest, AstFactoryStruct) {
  AstFactory factory;

  cel::Expr struct_expr =
      factory.NewStructBuilder(54, "MyMessage")
          .Add(50, "field1", factory.NewIntConst(51, 100), false)
          .Add(52, "field2", factory.NewIntConst(53, 200), true)
          .Build();
  EXPECT_EQ(factory.GetId(struct_expr), 54);
  ASSERT_TRUE(struct_expr.has_struct_expr());
  EXPECT_EQ(struct_expr.struct_expr().name(), "MyMessage");
  ASSERT_EQ(struct_expr.struct_expr().fields().size(), 2);
  EXPECT_EQ(struct_expr.struct_expr().fields()[0].id(), 50);
  EXPECT_EQ(struct_expr.struct_expr().fields()[0].name(), "field1");
  EXPECT_FALSE(struct_expr.struct_expr().fields()[0].optional());
  EXPECT_EQ(struct_expr.struct_expr().fields()[1].id(), 52);
  EXPECT_EQ(struct_expr.struct_expr().fields()[1].name(), "field2");
  EXPECT_TRUE(struct_expr.struct_expr().fields()[1].optional());
}

TEST(AstFactoryInterfaceTest, AstFactoryMap) {
  AstFactory factory;

  cel::Expr map_expr = factory.NewMapBuilder(66)
                           .Add(60, factory.NewStringConst(61, "key1"),
                                factory.NewIntConst(62, 10), false)
                           .Add(63, factory.NewStringConst(64, "key2"),
                                factory.NewIntConst(65, 20), true)
                           .Build();
  EXPECT_EQ(factory.GetId(map_expr), 66);
  ASSERT_TRUE(map_expr.has_map_expr());
  ASSERT_EQ(map_expr.map_expr().entries().size(), 2);
  EXPECT_EQ(map_expr.map_expr().entries()[0].id(), 60);
  EXPECT_EQ(map_expr.map_expr().entries()[0].key().const_expr().string_value(),
            "key1");
  EXPECT_EQ(map_expr.map_expr().entries()[0].value().const_expr().int_value(),
            10);
  EXPECT_FALSE(map_expr.map_expr().entries()[0].optional());
  EXPECT_EQ(map_expr.map_expr().entries()[1].id(), 63);
  EXPECT_TRUE(map_expr.map_expr().entries()[1].optional());
}

}  // namespace
}  // namespace cel::parser_internal
