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

#include "parser/internal/pratt_parser.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/expr.h"
#include "common/source.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/internal/lexer.h"
#include "parser/internal/pratt_parser_worker.h"
#include "parser/options.h"
#include "parser/parser_interface.h"
#include "testutil/expr_printer.h"

// Change to 0 to test with the ANTLR parser to check for differences.
#define USE_PRATT_PARSER 1

namespace cel::parser_internal {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::NotNull;

absl::StatusOr<std::unique_ptr<cel::Ast>> Parse(
    std::string_view expression,
    const cel::ParserOptions& options = cel::ParserOptions(),
    std::vector<cel::ParseIssue>* issues = nullptr) {
#if USE_PRATT_PARSER
  std::unique_ptr<cel::ParserBuilder> builder = NewPrattParserBuilder(options);
#else
  std::unique_ptr<cel::ParserBuilder> builder = cel::NewParserBuilder(options);
#endif
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Parser> parser, builder->Build());
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Source> source,
                       cel::NewSource(expression));
  return parser->Parse(*source, issues);
}

struct TestCase {
  std::string_view source;
  std::string_view expected_ast;
  bool enable_optional_syntax = false;
  bool enable_variadic_logical_operators = false;
};

class PrattParserTest : public testing::TestWithParam<TestCase> {};

std::string Unindent(std::string_view multiline) {
  std::vector<std::string> unindented_lines;
  int indent = -1;
  for (std::string_view line : absl::StrSplit(multiline, '\n')) {
    std::size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string_view::npos) continue;
    if (indent == -1) indent = pos;
    unindented_lines.push_back(std::string(line.substr(indent)));
  }
  return absl::StrJoin(unindented_lines, "\n");
}

std::string_view ConstantKind(const cel::Constant& c) {
  switch (c.kind_case()) {
    case ConstantKindCase::kBool:
      return "bool";
    case ConstantKindCase::kInt:
      return "int64";
    case ConstantKindCase::kUint:
      return "uint64";
    case ConstantKindCase::kDouble:
      return "double";
    case ConstantKindCase::kString:
      return "string";
    case ConstantKindCase::kBytes:
      return "bytes";
    case ConstantKindCase::kNull:
      return "NullValue";
    default:
      return "unspecified_constant";
  }
}

std::string_view ExprKind(const cel::Expr& e) {
  switch (e.kind_case()) {
    case ExprKindCase::kConstant:
      // special cased, this doesn't appear.
      return "Expr.Constant";
    case ExprKindCase::kIdentExpr:
      return "Expr.Ident";
    case ExprKindCase::kSelectExpr:
      return "Expr.Select";
    case ExprKindCase::kCallExpr:
      return "Expr.Call";
    case ExprKindCase::kListExpr:
      return "Expr.CreateList";
    case ExprKindCase::kMapExpr:
      return "Expr.CreateMap";
    case ExprKindCase::kStructExpr:
      return "Expr.CreateStruct";
    case ExprKindCase::kComprehensionExpr:
      return "Expr.Comprehension";
    default:
      return "unspecified_expr";
  }
}

class KindAndIdAdorner : public cel::test::ExpressionAdorner {
 public:
  std::string Adorn(const cel::Expr& e) const override {
    if (e.has_const_expr()) {
      const cel::Constant& const_expr = e.const_expr();
      return absl::StrCat("^#", e.id(), ":", ConstantKind(const_expr), "#");
    } else {
      return absl::StrCat("^#", e.id(), ":", ExprKind(e), "#");
    }
  }

  std::string AdornStructField(const cel::StructExprField& e) const override {
    return absl::StrFormat("^#%d:Expr.CreateStruct.Entry#", e.id());
  }

  std::string AdornMapEntry(const cel::MapExprEntry& e) const override {
    return absl::StrFormat("^#%d:Expr.CreateStruct.Entry#", e.id());
  }
};

TEST_P(PrattParserTest, Parse) {
  const TestCase& test_case = GetParam();
  cel::ParserOptions options;
  options.enable_optional_syntax = test_case.enable_optional_syntax;
  options.enable_variadic_logical_operators =
      test_case.enable_variadic_logical_operators;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       Parse(test_case.source, options));
  const Expr& root = ast->root_expr();
  KindAndIdAdorner kind_and_id_adorner;
  test::ExprPrinter printer(kind_and_id_adorner);
  EXPECT_EQ(Unindent(printer.Print(root)), Unindent(test_case.expected_ast));
}

std::vector<TestCase> GetParserTestCases() {
  return {
      TestCase{
          .source = "null",
          .expected_ast = R"(
              null^#1:NullValue#
            )",
      },
      TestCase{
          .source = "true",
          .expected_ast = R"(
              true^#1:bool#
            )",
      },
      TestCase{
          .source = "false",
          .expected_ast = R"(
              false^#1:bool#
            )",
      },
      TestCase{
          .source = "123",
          .expected_ast = R"(
              123^#1:int64#
            )",
      },
      TestCase{
          .source = "-123",
          .expected_ast = R"(
              -123^#1:int64#
            )",
      },
      TestCase{
          .source = "-9223372036854775808",
          .expected_ast = R"(
              -9223372036854775808^#1:int64#
            )",
      },
      TestCase{
          .source = "0xA",
          .expected_ast = R"(
              10^#1:int64#
            )",
      },
      TestCase{
          .source = "-0x1A",
          .expected_ast = R"(
              -26^#1:int64#
            )",
      },
      TestCase{
          .source = "-0X1a",
          .expected_ast = R"(
              -26^#1:int64#
            )",
      },
      TestCase{
          .source = "-0x8000000000000000",
          .expected_ast = R"(
              -9223372036854775808^#1:int64#
            )",
      },
      TestCase{
          .source = "-0X8000000000000000",
          .expected_ast = R"(
              -9223372036854775808^#1:int64#
            )",
      },
      TestCase{
          .source = "42u",
          .expected_ast = R"(
              42u^#1:uint64#
            )",
      },
      TestCase{
          .source = "0u",
          .expected_ast = R"(
              0u^#1:uint64#
            )",
      },
      TestCase{
          .source = "0xAu",
          .expected_ast = R"(
              10u^#1:uint64#
            )",
      },
      TestCase{
          .source = "3.14159",
          .expected_ast = R"(
              3.14159^#1:double#
            )",
      },
      TestCase{
          .source = "-3.14159",
          .expected_ast = R"(
              -3.14159^#1:double#
            )",
      },
      TestCase{
          .source = "-5.5e-3",
          .expected_ast = R"(
              -0.0055^#1:double#
            )",
      },
      TestCase{
          .source = "b'hello'",
          .expected_ast = R"(
              b"hello"^#1:bytes#
            )",
      },
      TestCase{
          .source = "b\"hello\"",
          .expected_ast = R"(
              b"hello"^#1:bytes#
            )",
      },
      TestCase{
          .source = "'hello world'",
          .expected_ast = R"(
              "hello world"^#1:string#
            )",
      },
      TestCase{
          .source = "\"hello world\"",
          .expected_ast = R"(
              "hello world"^#1:string#
            )",
      },
      TestCase{
          .source = "\"\u2764\"",
          .expected_ast = "\"\u2764\"^#1:string#",
      },
      TestCase{
          .source = "\"\\a\\b\\f\\n\\r\\t\\v'\\\"\\\\\\? Legal escapes\"",
          .expected_ast = R"(
              "\x07\x08\x0c\n\r\t\x0b'\"\\? Legal escapes"^#1:string#
            )",
      },
      TestCase{
          .source = "a",
          .expected_ast = R"(
              a^#1:Expr.Ident#
            )",
      },
      TestCase{
          .source = "(a)",
          .expected_ast = R"(
              a^#1:Expr.Ident#
            )",
      },
      TestCase{
          .source = "((a))",
          .expected_ast = R"(
              a^#1:Expr.Ident#
            )",
      },
      TestCase{
          .source = "a.b",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b^#2:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.?b",
          .expected_ast = R"(
              _?._(
                a^#1:Expr.Ident#,
                "b"^#3:string#
              )^#2:Expr.Call#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "a.b.c",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b^#2:Expr.Select#.c^#3:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.`b`",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b^#2:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.`b-c`",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b-c^#2:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.?`b`",
          .expected_ast = R"(
              _?._(
                a^#1:Expr.Ident#,
                "b"^#3:string#
              )^#2:Expr.Call#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "-x",
          .expected_ast = R"(
              -_(
                x^#2:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "- -1",
          .expected_ast = R"(
              -_(
                -1^#2:int64#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "-(1 + 2)",
          .expected_ast = R"(
              -_(
                _+_(
                  1^#2:int64#,
                  2^#4:int64#
                )^#3:Expr.Call#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "---a",
          .expected_ast = R"(
              -_(
                -_(
                  -_(
                    a^#4:Expr.Ident#
                  )^#3:Expr.Call#
                )^#2:Expr.Call#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "!false",
          .expected_ast = R"(
              !_(
                false^#2:bool#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "!a",
          .expected_ast = R"(
              !_(
                a^#2:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "-!true",
          .expected_ast = R"(
              -_(
                !_(
                  true^#3:bool#
                )^#2:Expr.Call#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a + b",
          .expected_ast = R"(
              _+_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a - b",
          .expected_ast = R"(
              _-_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "4--4",
          .expected_ast = R"(
              _-_(
                4^#1:int64#,
                -4^#3:int64#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a * b",
          .expected_ast = R"(
              _*_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a / b",
          .expected_ast = R"(
              _/_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a % b",
          .expected_ast = R"(
              _%_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a in b",
          .expected_ast = R"(
              @in(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a == b",
          .expected_ast = R"(
              _==_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a != b",
          .expected_ast = R"(
              _!=_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a > b",
          .expected_ast = R"(
              _>_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a >= b",
          .expected_ast = R"(
              _>=_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a < b",
          .expected_ast = R"(
              _<_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a <= b",
          .expected_ast = R"(
              _<=_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b",
          .expected_ast = R"(
              _&&_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#
              )^#3:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c",
          .expected_ast = R"(
              _&&_(
                _&&_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                c^#4:Expr.Ident#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c && d",
          .expected_ast = R"(
              _&&_(
                _&&_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                _&&_(
                  c^#4:Expr.Ident#,
                  d^#6:Expr.Ident#
                )^#7:Expr.Call#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c && d && e",
          .expected_ast = R"(
              _&&_(
                _&&_(
                  _&&_(
                    a^#1:Expr.Ident#,
                    b^#2:Expr.Ident#
                  )^#3:Expr.Call#,
                  c^#4:Expr.Ident#
                )^#5:Expr.Call#,
                _&&_(
                  d^#6:Expr.Ident#,
                  e^#8:Expr.Ident#
                )^#9:Expr.Call#
              )^#7:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c && d",
          .expected_ast = R"(
              _&&_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#,
                c^#4:Expr.Ident#,
                d^#6:Expr.Ident#
              )^#3:Expr.Call#
            )",
          .enable_variadic_logical_operators = true,
      },
      TestCase{
          .source = "a || b",
          .expected_ast = R"(
              _||_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#
              )^#3:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c",
          .expected_ast = R"(
              _||_(
                _||_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                c^#4:Expr.Ident#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c || d",
          .expected_ast = R"(
              _||_(
                _||_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                _||_(
                  c^#4:Expr.Ident#,
                  d^#6:Expr.Ident#
                )^#7:Expr.Call#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c || d || e",
          .expected_ast = R"(
              _||_(
                _||_(
                  _||_(
                    a^#1:Expr.Ident#,
                    b^#2:Expr.Ident#
                  )^#3:Expr.Call#,
                  c^#4:Expr.Ident#
                )^#5:Expr.Call#,
                _||_(
                  d^#6:Expr.Ident#,
                  e^#8:Expr.Ident#
                )^#9:Expr.Call#
              )^#7:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c || d",
          .expected_ast = R"(
              _||_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#,
                c^#4:Expr.Ident#,
                d^#6:Expr.Ident#
              )^#3:Expr.Call#
            )",
          .enable_variadic_logical_operators = true,
      },
      TestCase{
          .source = "10 - 3 - 2",
          .expected_ast = R"(
              _-_(
                _-_(
                  10^#1:int64#,
                  3^#3:int64#
                )^#2:Expr.Call#,
                2^#5:int64#
              )^#4:Expr.Call#
            )",
      },
      TestCase{
          .source = "1 + 2 * 3 - 1 / 2 == 6 % 1",
          .expected_ast = R"(
              _==_(
                _-_(
                  _+_(
                    1^#1:int64#,
                    _*_(
                      2^#3:int64#,
                      3^#5:int64#
                    )^#4:Expr.Call#
                  )^#2:Expr.Call#,
                  _/_(
                    1^#7:int64#,
                    2^#9:int64#
                  )^#8:Expr.Call#
                )^#6:Expr.Call#,
                _%_(
                  6^#11:int64#,
                  1^#13:int64#
                )^#12:Expr.Call#
              )^#10:Expr.Call#
            )",
      },
      TestCase{
          .source = "1 + 2 * 3 == 7 && true || false",
          .expected_ast = R"(
              _||_(
                _&&_(
                  _==_(
                    _+_(
                      1^#1:int64#,
                      _*_(
                        2^#3:int64#,
                        3^#5:int64#
                      )^#4:Expr.Call#
                    )^#2:Expr.Call#,
                    7^#7:int64#
                  )^#6:Expr.Call#,
                  true^#8:bool#
                )^#9:Expr.Call#,
                false^#10:bool#
              )^#11:Expr.Call#
            )",
      },
      TestCase{
          .source = "x > 0 ? 'pos' : 'neg'",
          .expected_ast = R"(
              _?_:_(
                _>_(
                  x^#1:Expr.Ident#,
                  0^#3:int64#
                )^#2:Expr.Call#,
                "pos"^#5:string#,
                "neg"^#6:string#
              )^#4:Expr.Call#
            )",
      },
      TestCase{
          .source = "a?b:c",
          .expected_ast = R"(
              _?_:_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#,
                c^#4:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a[b]",
          .expected_ast = R"(
              _[_](
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a[?b]",
          .expected_ast = R"(
              _[?_](
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "a[3]",
          .expected_ast = R"(
              _[_](
                a^#1:Expr.Ident#,
                3^#3:int64#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "[1,3,4][0]",
          .expected_ast = R"(
              _[_](
                [
                  1^#2:int64#,
                  3^#3:int64#,
                  4^#4:int64#
                ]^#1:Expr.CreateList#,
                0^#6:int64#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a()",
          .expected_ast = R"(
              a()^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a(b)",
          .expected_ast = R"(
              a(
                b^#2:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a(b, c)",
          .expected_ast = R"(
              a(
                b^#2:Expr.Ident#,
                c^#3:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a.b()",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b()^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a.b(1)",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b(
                1^#3:int64#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a.b(c)",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b(
                c^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "size(x) == x.size()",
          .expected_ast = R"(
              _==_(
                size(
                  x^#2:Expr.Ident#
                )^#1:Expr.Call#,
                x^#4:Expr.Ident#.size()^#5:Expr.Call#
              )^#3:Expr.Call#
            )",
      },
      TestCase{
          .source = "\"foo\".size()",
          .expected_ast = R"(
              "foo"^#1:string#.size()^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "[true, true].size() == 2",
          .expected_ast = R"(
              _==_(
                [
                  true^#2:bool#,
                  true^#3:bool#
                ]^#1:Expr.CreateList#.size()^#4:Expr.Call#,
                2^#6:int64#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "[]",
          .expected_ast = R"(
              []^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[a]",
          .expected_ast = R"(
              [
                a^#2:Expr.Ident#
              ]^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[1, 2, 3]",
          .expected_ast = R"(
              [
                1^#2:int64#,
                2^#3:int64#,
                3^#4:int64#
              ]^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[1, 2, 3,]",
          .expected_ast = R"(
              [
                1^#2:int64#,
                2^#3:int64#,
                3^#4:int64#
              ]^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[?1, 2]",
          .expected_ast = R"(
              [
                ?1^#2:int64#,
                2^#3:int64#
              ]^#1:Expr.CreateList#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "{}",
          .expected_ast = R"(
              {}^#1:Expr.CreateMap#
            )",
      },
      TestCase{
          .source = "{'key': 'value', 'num': 42}",
          .expected_ast = R"(
              {
                "key"^#2:string#:"value"^#4:string#^#3:Expr.CreateStruct.Entry#,
                "num"^#5:string#:42^#7:int64#^#6:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateMap#
            )",
      },
      TestCase{
          .source = "{?'key': 'value', 'num': 42}",
          .expected_ast = R"(
              {
                ?"key"^#2:string#:"value"^#4:string#^#3:Expr.CreateStruct.Entry#,
                "num"^#5:string#:42^#7:int64#^#6:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateMap#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "{foo: 5, bar: \"xyz\"}",
          .expected_ast = R"(
              {
                foo^#2:Expr.Ident#:5^#4:int64#^#3:Expr.CreateStruct.Entry#,
                bar^#5:Expr.Ident#:"xyz"^#7:string#^#6:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateMap#
            )",
      },
      TestCase{
          .source = "google.protobuf.Empty{}",
          .expected_ast = R"(
              google.protobuf.Empty{}^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "foo{ }",
          .expected_ast = R"(
              foo{}^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "foo{ a:b }",
          .expected_ast = R"(
              foo{
                a:b^#3:Expr.Ident#^#2:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "A{`b`: 1}",
          .expected_ast = R"(
              A{
                b:1^#3:int64#^#2:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "A{`b-c`: 1}",
          .expected_ast = R"(
              A{
                b-c:1^#3:int64#^#2:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "Msg{field: 10, other: 'val'}",
          .expected_ast = R"(
              Msg{
                field:10^#3:int64#^#2:Expr.CreateStruct.Entry#,
                other:"val"^#5:string#^#4:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "Msg{?field: 10, other: 'val'}",
          .expected_ast = R"(
              Msg{
                ?field:10^#3:int64#^#2:Expr.CreateStruct.Entry#,
                other:"val"^#5:string#^#4:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
          .enable_optional_syntax = true,
      },
  };
}

std::string TestName(const testing::TestParamInfo<TestCase>& test_info) {
  std::string name = absl::StrCat(test_info.index, "-", test_info.param.source);
  absl::c_replace_if(name, [](char c) { return !absl::ascii_isalnum(c); }, '_');
  return name;
}

INSTANTIATE_TEST_SUITE_P(PrattParserTest, PrattParserTest,
                         testing::ValuesIn(GetParserTestCases()), TestName);

struct ErrorTestCase {
  std::string_view source;
  std::string_view expected_error;
  bool enable_optional_syntax = false;
  bool enable_quoted_identifiers = false;
};

class PrattParserErrorTest : public testing::TestWithParam<ErrorTestCase> {};

std::string FormatIssues(const cel::Source& source,
                         const std::vector<cel::ParseIssue>& issues) {
  return absl::StrJoin(
      issues, "\n", [&source](std::string* out, const cel::ParseIssue& issue) {
        absl::StrAppend(
            out,
            absl::StrFormat("ERROR: %s:%zu:%zu: %s", source.description(),
                            issue.location().line, issue.location().column + 1,
                            issue.message()),
            source.DisplayErrorLocation(issue.location()));
      });
}

TEST_P(PrattParserErrorTest, ParseSyntaxError) {
  const ErrorTestCase& test_case = GetParam();
  cel::ParserOptions options;
  options.enable_optional_syntax = test_case.enable_optional_syntax;
  options.enable_quoted_identifiers = test_case.enable_quoted_identifiers;
  std::vector<cel::ParseIssue> issues;
  absl::StatusOr<std::unique_ptr<cel::Ast>> result =
      Parse(test_case.source, options, &issues);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument,
                               Eq(test_case.expected_error)));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Source> source,
                       cel::NewSource(test_case.source));
  EXPECT_EQ(FormatIssues(*source, issues), test_case.expected_error);
}

std::vector<ErrorTestCase> GetErrorTestCases() {
  return {
      ErrorTestCase{
          .source = "1 + 2 * 3 4",
          .expected_error =
              "ERROR: <input>:1:11: unexpected token after expression\n"
              " | 1 + 2 * 3 4\n"
              " | ..........^",
      },
      ErrorTestCase{
          .source = "1{}",
          .expected_error =
              "ERROR: <input>:1:2: unexpected token after expression\n"
              " | 1{}\n"
              " | .^",
      },
      ErrorTestCase{
          .source = "true ? 1",
          .expected_error =
              "ERROR: <input>:1:9: expected ':' in conditional expression\n"
              " | true ? 1\n"
              " | ........^",
      },
      ErrorTestCase{
          .source = "a.?b",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '.?'\n"
                            " | a.?b\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "a.",
          .expected_error =
              "ERROR: <input>:1:3: expected identifier after '.'\n"
              " | a.\n"
              " | ..^",
      },
      ErrorTestCase{
          .source = "a[?0]",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '?'\n"
                            " | a[?0]\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = ". *",
          .expected_error = "ERROR: <input>:1:3: expected identifier\n"
                            " | . *\n"
                            " | ..^",
      },
      ErrorTestCase{
          .source = ".as",
          .expected_error = "ERROR: <input>:1:2: reserved identifier: as\n"
                            " | .as\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "* 2",
          .expected_error =
              "ERROR: <input>:1:1: unexpected token\n"
              " | * 2\n"
              " | ^\n"
              "ERROR: <input>:1:3: unexpected token after expression\n"
              " | * 2\n"
              " | ..^",
      },
      ErrorTestCase{
          .source = "(1 + 2",
          .expected_error =
              "ERROR: <input>:1:7: mismatched input <EOF> expecting ')'\n"
              " | (1 + 2\n"
              " | ......^",
      },
      ErrorTestCase{
          .source = "[?1]",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '?'\n"
                            " | [?1]\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "[1, 2",
          .expected_error = "ERROR: <input>:1:6: expected ']'\n"
                            " | [1, 2\n"
                            " | .....^",
      },
      ErrorTestCase{
          .source = "{?'k': 'v'}",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '?'\n"
                            " | {?'k': 'v'}\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "{'k' 'v'}",
          .expected_error = "ERROR: <input>:1:6: expected ':' in map entry\n"
                            " | {'k' 'v'}\n"
                            " | .....^",
      },
      ErrorTestCase{
          .source = "{'k': 'v'",
          .expected_error = "ERROR: <input>:1:10: expected '}'\n"
                            " | {'k': 'v'\n"
                            " | .........^",
      },
      ErrorTestCase{
          .source = "Msg{?f: 1}",
          .expected_error = "ERROR: <input>:1:5: unsupported syntax '?'\n"
                            " | Msg{?f: 1}\n"
                            " | ....^",
      },
      ErrorTestCase{
          .source = "Msg{1: 2}",
          .expected_error = "ERROR: <input>:1:5: expected struct field name\n"
                            " | Msg{1: 2}\n"
                            " | ....^",
      },
      ErrorTestCase{
          .source = "Msg{f 10}",
          .expected_error = "ERROR: <input>:1:7: expected ':' in struct field\n"
                            " | Msg{f 10}\n"
                            " | ......^",
      },
      ErrorTestCase{
          .source = "Msg{f: 10",
          .expected_error = "ERROR: <input>:1:10: expected '}'\n"
                            " | Msg{f: 10\n"
                            " | .........^",
      },
      ErrorTestCase{
          .source = "f(1, 2",
          .expected_error =
              "ERROR: <input>:1:7: mismatched input <EOF> expecting ')'\n"
              " | f(1, 2\n"
              " | ......^",
      },
      ErrorTestCase{
          .source = "999999999999999999999999999999999999999",
          .expected_error = "ERROR: <input>:1:1: invalid int literal\n"
                            " | 999999999999999999999999999999999999999\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "999999999999999999999999999999999999999u",
          .expected_error = "ERROR: <input>:1:1: invalid uint literal\n"
                            " | 999999999999999999999999999999999999999u\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "1e",
          .expected_error =
              "ERROR: <input>:1:1: floating point literal missing digits after "
              "exponent separator\n"
              " | 1e\n"
              " | ^",
      },
      ErrorTestCase{
          .source = "\"unterminated",
          .expected_error = "ERROR: <input>:1:1: unterminated string literal\n"
                            " | \"unterminated\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "b\"unterminated",
          .expected_error = "ERROR: <input>:1:1: unterminated bytes literal\n"
                            " | b\"unterminated\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "a.?`foo`",
          .expected_error = "ERROR: <input>:1:4: unsupported syntax '`'\n"
                            " | a.?`foo`\n"
                            " | ...^",
          .enable_optional_syntax = true,
          .enable_quoted_identifiers = false,
      },
      ErrorTestCase{
          .source = "a.`foo`()",
          .expected_error = "ERROR: <input>:1:3: unexpected quoted identifier\n"
                            " | a.`foo`()\n"
                            " | ..^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "`foo`",
          .expected_error = "ERROR: <input>:1:1: unexpected quoted identifier\n"
                            " | `foo`\n"
                            " | ^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "`foo`()",
          .expected_error = "ERROR: <input>:1:1: unexpected quoted identifier\n"
                            " | `foo`()\n"
                            " | ^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "a.`b@c`",
          .expected_error = "ERROR: <input>:1:3: unexpected quoted identifier\n"
                            " | a.`b@c`\n"
                            " | ..^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "a.``",
          .expected_error = "ERROR: <input>:1:3: unexpected quoted identifier\n"
                            " | a.``\n"
                            " | ..^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "a.`foo`",
          .expected_error = "ERROR: <input>:1:3: unsupported syntax '`'\n"
                            " | a.`foo`\n"
                            " | ..^",
          .enable_quoted_identifiers = false,
      },
      ErrorTestCase{
          .source = "`foo",
          .expected_error =
              "ERROR: <input>:1:1: unterminated quoted identifier\n"
              " | `foo\n"
              " | ^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "f(*, 1e, {2 3})",
          .expected_error =
              "ERROR: <input>:1:3: unexpected token\n"
              " | f(*, 1e, {2 3})\n"
              " | ..^\n"
              "ERROR: <input>:1:6: floating point literal missing digits after "
              "exponent separator\n"
              " | f(*, 1e, {2 3})\n"
              " | .....^\n"
              "ERROR: <input>:1:13: expected ':' in map entry\n"
              " | f(*, 1e, {2 3})\n"
              " | ............^",
      },
      ErrorTestCase{
          .source = "(1 + *) + 2",
          .expected_error = "ERROR: <input>:1:6: unexpected token\n"
                            " | (1 + *) + 2\n"
                            " | .....^",
      },
      ErrorTestCase{
          .source = "f(1 + *, 2)",
          .expected_error = "ERROR: <input>:1:7: unexpected token\n"
                            " | f(1 + *, 2)\n"
                            " | ......^",
      },
      ErrorTestCase{
          .source = "(a. + 1)",
          .expected_error =
              "ERROR: <input>:1:5: expected identifier after '.'\n"
              " | (a. + 1)\n"
              " | ....^",
      },
      ErrorTestCase{
          .source = "f(a., 1)",
          .expected_error =
              "ERROR: <input>:1:5: expected identifier after '.'\n"
              " | f(a., 1)\n"
              " | ....^",
      },
      ErrorTestCase{
          .source = "[a., 1]",
          .expected_error =
              "ERROR: <input>:1:4: expected identifier after '.'\n"
              " | [a., 1]\n"
              " | ...^",
      },
      ErrorTestCase{
          .source = "-0x8000000000000001",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -0x8000000000000001\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-0x10000000000000000",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -0x10000000000000000\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-9223372036854775809",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -9223372036854775809\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-999999999999999999999999999999999999999",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -999999999999999999999999999999999999999\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-",
          .expected_error =
              "ERROR: <input>:1:2: Syntax error: mismatched input '<EOF>' "
              "expecting expression\n"
              " | -\n"
              " | .^",
      },
      ErrorTestCase{
          .source = "- *",
          .expected_error = "ERROR: <input>:1:3: unexpected token\n"
                            " | - *\n"
                            " | ..^",
      },
      ErrorTestCase{
          .source = "\"😀😀😀😀😀\" ~error",
          .expected_error = "ERROR: <input>:1:9: unexpected character\n"
                            " | \"😀😀😀😀😀\" ~error\n"
                            " | .．．．．．..^",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(PrattParserErrorTest, PrattParserErrorTest,
                         testing::ValuesIn(GetErrorTestCases()));

TEST(PrattParserTest, SourceInfoPositionsPopulated) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast, Parse("a + b"));
  const cel::SourceInfo& source_info = ast->source_info();
  EXPECT_FALSE(source_info.positions().empty());

  const cel::Expr& root = ast->root_expr();
  EXPECT_EQ(source_info.positions().at(root.id()), 2);
  ASSERT_TRUE(root.has_call_expr());
  ASSERT_EQ(root.call_expr().args().size(), 2);
  EXPECT_EQ(source_info.positions().at(root.call_expr().args()[0].id()), 0);
  EXPECT_EQ(source_info.positions().at(root.call_expr().args()[1].id()), 4);
}

TEST(PrattParserRecursionDepthTest, ParseRecursionDepth) {
  cel::ParserOptions options;
  options.max_recursion_depth = 5;
  EXPECT_THAT(Parse("((((1))))", options), IsOkAndHolds(NotNull()));
  EXPECT_THAT(Parse("[[[[[[1]]]]]]", options),
              StatusIs(absl::StatusCode::kCancelled));
}

TEST(PrattParserRecursionDepthTest, SequentialScopesDoNotAccumulateDepth) {
  cel::ParserOptions options;
  options.max_recursion_depth = 2;
  EXPECT_THAT(Parse("[1] + [2] + [3]", options), IsOkAndHolds(NotNull()));
}

class TestParserWorker : public ParserWorker {
  // Expose the protected constructor and methods for testing.
 public:
  using ParserWorker::GetTokenText;
  using ParserWorker::ParserWorker;
};

TEST(ParserWorkerTest, GetTokenTextBoundsChecking) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Source> source,
                       cel::NewSource("hello world"));
  cel::ParserOptions options;
  TestParserWorker worker(*source, options, nullptr);
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = 0, .end = 5}),
            "hello");
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = -1, .end = 5}),
            "");
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = 5, .end = 2}),
            "");
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = 0, .end = 100}),
            "");
}

}  // namespace
}  // namespace cel::parser_internal
