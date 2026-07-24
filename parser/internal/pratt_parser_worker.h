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

#ifndef THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_PRATT_PARSER_WORKER_H_
#define THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_PRATT_PARSER_WORKER_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/operators.h"
#include "common/source.h"
#include "internal/lexis.h"
#include "internal/strings.h"
#include "parser/internal/ast_factory_interface.h"
#include "parser/internal/lexer.h"
#include "parser/options.h"
#include "parser/parser_interface.h"

namespace cel::parser_internal {

class ParserWorker {
 public:
  ParserWorker(const cel::Source& source, const cel::ParserOptions& options,
               std::vector<cel::ParseIssue>* absl_nullable parse_issues);

  const absl::flat_hash_map<int64_t, int32_t>& GetNodePositions() const {
    return positions_;
  }
  absl::Span<const int32_t> GetLineOffsets() const {
    return source_.line_offsets();
  }
  bool has_errors() const { return error_count_ > 0; }
  bool is_recursion_limit_exceeded() const { return recursion_limit_exceeded_; }

 protected:
  const cel::Source& source() const { return source_; }
  const cel::ParserOptions& options() const { return options_; }
  // Token stream management
  void InitTokenStream();
  Token NextSignificantToken();
  Token NextToken();
  bool Expect(TokenType type, absl::string_view msg = "");
  std::string GetTokenText(const Token& tok) const;
  void SynchronizeOnDelimiter();

  // ID and Position tracking
  int64_t NextId(int32_t position);
  int64_t NextId(const Token& token) { return NextId(token.start); }
  int64_t NextId() { return next_id_++; }
  int64_t CopyId(int64_t id);
  void EraseId(int64_t id);

  // Error reporting and recovery
  bool is_recovery_limit_exceeded() const {
    return error_count_ >= options_.error_recovery_limit;
  }
  void ReportError(int32_t position, absl::string_view msg);
  void ReportError(const SourceLocation& loc, absl::string_view msg);
  void ReportError(const Token& token, absl::string_view msg) {
    ReportError(token.start, msg);
  }

  const cel::Source& source_;
  cel::ParserOptions options_;
  Lexer lexer_;
  Token current_token_;
  Token peek_token_;
  int recursion_depth_ = 0;
  int64_t next_id_ = 1;
  absl::flat_hash_map<int64_t, int32_t> positions_;
  std::vector<cel::ParseIssue>* absl_nullable parse_issues_;
  int error_count_ = 0;
  bool lexer_error_reported_ = false;
  bool recursion_limit_exceeded_ = false;
};

// Generic Pratt parser implementation parameterized by the AST node type
// (`ExprNode`).
//
// This class implements the core recursive-descent and operator-precedence
// parsing logic for CEL without depending on a concrete expression node data
// structure. All inspection and construction of AST nodes is performed through
// `AstFactoryInterface<ExprNode>`.
//
// See `AstFactoryInterface` documentation in `ast_factory_interface.h` for
// details on how to use this generic parser with alternative AST structures.
template <typename ExprNode>
class PrattParserWorker : public ParserWorker {
 public:
  explicit PrattParserWorker(
      const cel::Source& source, const cel::ParserOptions& options,
      std::vector<cel::ParseIssue>* absl_nullable parse_issues)
      : ParserWorker(source, options, parse_issues) {
    this->InitTokenStream();
  }

  ExprNode Parse();

 private:
  using CelOperator = ::google::api::expr::common::CelOperator;

  ExprNode ParseExpr();
  ExprNode ParseConditionalOr();
  ExprNode ParseConditionalAnd();
  ExprNode ParseRelation();
  ExprNode ParseCalc(int min_prec);
  ExprNode ParseUnary();
  ExprNode ParseMember();
  ExprNode ParsePrimary();
  ExprNode ParseList();
  ExprNode ParseMap();
  ExprNode ParseStruct(int64_t obj_id, absl::string_view struct_name);
  std::vector<ExprNode> ParseArguments(TokenType close_token);
  ExprNode ParseIntLiteral();
  ExprNode ParseUintLiteral();
  ExprNode ParseDoubleLiteral();
  ExprNode ParseStringLiteral();
  ExprNode ParseBytesLiteral();
  std::string NormalizeIdent(const Token& tok, bool allow_quoted);
  std::optional<std::string> ExtractStructName(const ExprNode& expr);
  int32_t GetLeftmostPosition(const ExprNode& expr);
  ExprNode BalancedTree(absl::string_view op, std::vector<ExprNode>& terms,
                        const std::vector<int64_t>& ops, int lo, int hi);
  ExprNode BalanceLogical(absl::string_view op, std::vector<ExprNode> terms,
                          std::vector<int64_t> ops, bool enable_variadic);

  AstFactoryInterface<ExprNode> ast_factory_;
};

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::Parse() {
  ExprNode expr = ParseExpr();
  if (is_recursion_limit_exceeded()) {
    return expr;
  }
  if (peek_token_.type != TokenType::kEnd &&
      peek_token_.type != TokenType::kError) {
    ReportError(peek_token_, "unexpected token after expression");
  }
  return expr;
}

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseExpr() {
  if (recursion_limit_exceeded_) {
    return ExprNode();
  }
  if (recursion_depth_ >= options_.max_recursion_depth) {
    recursion_limit_exceeded_ = true;
    return ExprNode();
  }
  recursion_depth_++;
  ExprNode lhs = ParseConditionalOr();
  if (peek_token_.type == TokenType::kQuestion) {
    NextToken();
    int64_t op_id = NextId();
    ExprNode true_expr = ParseConditionalOr();
    if (!Expect(TokenType::kColon, "expected ':' in conditional expression")) {
      recursion_depth_--;
      return lhs;
    }
    ExprNode false_expr = ParseExpr();
    recursion_depth_--;
    return ast_factory_.NewCall(
        op_id, CelOperator::CONDITIONAL,
        std::vector<ExprNode>{std::move(lhs), std::move(true_expr),
                              std::move(false_expr)});
  }
  recursion_depth_--;
  return lhs;
}

// Parses logical OR expressions (e.g., `a || b || c`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseConditionalOr() {
  ExprNode lhs = ParseConditionalAnd();
  if (peek_token_.type != TokenType::kLogicalOr) {
    return lhs;
  }
  std::vector<ExprNode> terms;
  std::vector<int64_t> ops;
  terms.push_back(std::move(lhs));
  while (peek_token_.type == TokenType::kLogicalOr) {
    Token op_tok = NextToken();
    ExprNode rhs = ParseConditionalAnd();
    ops.push_back(NextId(op_tok));
    terms.push_back(std::move(rhs));
  }
  return BalanceLogical(CelOperator::LOGICAL_OR, std::move(terms),
                        std::move(ops),
                        options_.enable_variadic_logical_operators);
}

// Parses logical AND expressions (e.g., `a && b && c`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseConditionalAnd() {
  ExprNode lhs = ParseRelation();
  if (peek_token_.type != TokenType::kLogicalAnd) {
    return lhs;
  }
  std::vector<ExprNode> terms;
  std::vector<int64_t> ops;
  terms.push_back(std::move(lhs));
  while (peek_token_.type == TokenType::kLogicalAnd) {
    Token op_tok = NextToken();
    ExprNode rhs = ParseRelation();
    ops.push_back(NextId(op_tok));
    terms.push_back(std::move(rhs));
  }
  return BalanceLogical(CelOperator::LOGICAL_AND, std::move(terms),
                        std::move(ops),
                        options_.enable_variadic_logical_operators);
}

// Parses relational & equality ops (e.g., `a < b`, `x == y`, `a in b`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseRelation() {
  ExprNode lhs = ParseCalc(0);
  while (true) {
    TokenType tok = peek_token_.type;
    absl::string_view op_name;
    switch (tok) {
      case TokenType::kLess:
        op_name = CelOperator::LESS;
        break;
      case TokenType::kLessEqual:
        op_name = CelOperator::LESS_EQUALS;
        break;
      case TokenType::kGreater:
        op_name = CelOperator::GREATER;
        break;
      case TokenType::kGreaterEqual:
        op_name = CelOperator::GREATER_EQUALS;
        break;
      case TokenType::kEqualEqual:
        op_name = CelOperator::EQUALS;
        break;
      case TokenType::kExclamationEqual:
        op_name = CelOperator::NOT_EQUALS;
        break;
      case TokenType::kIn:
        op_name = CelOperator::IN;
        break;
      default:
        return lhs;
    }
    Token op = NextToken();
    int64_t op_id = NextId(op);
    ExprNode rhs = ParseCalc(0);
    lhs = ast_factory_.NewCall(
        op_id, std::string(op_name),
        std::vector<ExprNode>{std::move(lhs), std::move(rhs)});
  }
  return lhs;
}

// Parses arithmetic calculation expressions (e.g., `a + b * c`, `x - y % z`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseCalc(int min_prec) {
  ExprNode lhs = ParseUnary();
  while (true) {
    TokenType tok = peek_token_.type;
    int prec = 0;
    absl::string_view op_name;
    if (tok == TokenType::kAsterisk) {
      prec = 2;
      op_name = CelOperator::MULTIPLY;
    } else if (tok == TokenType::kSlash) {
      prec = 2;
      op_name = CelOperator::DIVIDE;
    } else if (tok == TokenType::kPercent) {
      prec = 2;
      op_name = CelOperator::MODULO;
    } else if (tok == TokenType::kPlus) {
      prec = 1;
      op_name = CelOperator::ADD;
    } else if (tok == TokenType::kMinus) {
      prec = 1;
      op_name = CelOperator::SUBTRACT;
    } else {
      break;
    }

    if (prec < min_prec) break;
    Token op = NextToken();
    int64_t op_id = NextId(op);
    ExprNode rhs = ParseCalc(prec + 1);
    lhs = ast_factory_.NewCall(
        op_id, std::string(op_name),
        std::vector<ExprNode>{std::move(lhs), std::move(rhs)});
  }
  return lhs;
}

// Parses unary logical NOT and negation expressions (e.g., `!a`, `-b`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseUnary() {
  TokenType tok = peek_token_.type;
  if (tok == TokenType::kExclamation) {
    Token op = NextToken();
    int64_t op_id = NextId(op);
    ExprNode operand = ParseUnary();
    return ast_factory_.NewCall(op_id, std::string(CelOperator::LOGICAL_NOT),
                                std::vector<ExprNode>{std::move(operand)});
  }
  if (tok == TokenType::kMinus) {
    Token op = NextToken();
    if (peek_token_.type == TokenType::kInt) {
      Token lit_tok = NextToken();
      std::string text = GetTokenText(lit_tok);
      int64_t int_val = 0;
      bool success = false;
      if (absl::StartsWith(text, "0x") || absl::StartsWith(text, "0X")) {
        uint64_t uint_val = 0;
        if (absl::SimpleHexAtoi(text, &uint_val)) {
          if (uint_val <= uint64_t{0x8000000000000000}) {
            if (uint_val == uint64_t{0x8000000000000000}) {
              int_val = std::numeric_limits<int64_t>::min();
            } else {
              int_val = -static_cast<int64_t>(uint_val);
            }
            success = true;
          }
        }
      } else {
        std::string val = absl::StrCat("-", text);
        success = absl::SimpleAtoi(val, &int_val);
      }
      if (success) {
        return ast_factory_.NewIntConst(NextId(op.start), int_val);
      }
      ReportError(lit_tok, "invalid int literal");
      return ast_factory_.NewUnspecified(NextId(lit_tok));
    }
    if (peek_token_.type == TokenType::kFloat) {
      Token lit_tok = NextToken();
      std::string val = absl::StrCat("-", GetTokenText(lit_tok));
      double double_val = 0.0;
      if (absl::SimpleAtod(val, &double_val)) {
        return ast_factory_.NewDoubleConst(NextId(op.start), double_val);
      }
      ReportError(lit_tok, "invalid double literal");
      return ast_factory_.NewUnspecified(NextId(lit_tok));
    }
    // Regular negate call
    int64_t op_id = NextId(op);
    ExprNode operand = ParseUnary();
    return ast_factory_.NewCall(op_id, std::string(CelOperator::NEGATE),
                                std::vector<ExprNode>{std::move(operand)});
  }
  return ParseMember();
}

// Parses member calls & indexing (e.g., `a.b`, `a.b(c)`, `a[b]`, `a.?b`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseMember() {
  ExprNode lhs = ParsePrimary();
  while (true) {
    TokenType tok = peek_token_.type;
    if (tok == TokenType::kDot) {
      Token dot_tok = NextToken();
      bool optional = false;
      if (peek_token_.type == TokenType::kQuestion) {
        NextToken();
        optional = true;
        if (!options_.enable_optional_syntax) {
          ReportError(dot_tok, "unsupported syntax '.?'");
        }
      }
      Token id_tok = NextToken();
      if (id_tok.type != TokenType::kIdent &&
          id_tok.type != TokenType::kReservedWord) {
        if (id_tok.type != TokenType::kError) {
          ReportError(id_tok, "expected identifier after '.'");
        }
        SynchronizeOnDelimiter();
        return lhs;
      }
      bool is_member_call = peek_token_.type == TokenType::kLeftParen;
      std::string id_text =
          NormalizeIdent(id_tok, /*allow_quoted=*/!is_member_call);
      if (optional) {
        int64_t op_id = NextId(dot_tok);
        lhs = ast_factory_.NewCall(
            op_id, "_?._",
            std::vector<ExprNode>{
                std::move(lhs),
                ast_factory_.NewStringConst(NextId(id_tok), id_text)});
      } else if (peek_token_.type == TokenType::kLeftParen) {
        Token lparen = NextToken();
        int64_t call_id = NextId(lparen);
        std::vector<ExprNode> args = ParseArguments(TokenType::kRightParen);
        lhs = ast_factory_.NewMemberCall(call_id, id_text, std::move(lhs),
                                         std::move(args));
      } else {
        lhs = ast_factory_.NewSelect(NextId(dot_tok), std::move(lhs), id_text);
      }
    } else if (tok == TokenType::kLeftBracket) {
      Token bracket_tok = NextToken();
      int64_t op_id = NextId(bracket_tok);
      bool optional = false;
      if (peek_token_.type == TokenType::kQuestion) {
        NextToken();
        optional = true;
        if (!options_.enable_optional_syntax) {
          ReportError(bracket_tok, "unsupported syntax '?'");
        }
      }
      ExprNode index = ParseExpr();
      Expect(TokenType::kRightBracket, "expected ']'");
      lhs = ast_factory_.NewCall(
          op_id, optional ? "_[?_]" : CelOperator::INDEX,
          std::vector<ExprNode>{std::move(lhs), std::move(index)});
    } else if (tok == TokenType::kLeftBrace) {
      int32_t struct_pos = GetLeftmostPosition(lhs);
      if (auto struct_name = ExtractStructName(lhs); struct_name.has_value()) {
        lhs = ParseStruct(NextId(struct_pos), *struct_name);
      } else {
        break;
      }
    } else {
      break;
    }
  }
  return lhs;
}

// Parses primary expressions & literals (e.g., `null`, `true`, `x`,
// `has(x.y)`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParsePrimary() {
  ExprNode expr;
  TokenType tok_type = peek_token_.type;
  if (tok_type == TokenType::kLeftParen) {
    NextToken();
    expr = ParseExpr();
    Expect(TokenType::kRightParen);
  } else if (tok_type == TokenType::kNull) {
    Token tok = NextToken();
    expr = ast_factory_.NewNullConst(NextId(tok));
  } else if (tok_type == TokenType::kTrue || tok_type == TokenType::kFalse) {
    Token tok = NextToken();
    expr = ast_factory_.NewBoolConst(NextId(tok), tok_type == TokenType::kTrue);
  } else if (tok_type == TokenType::kInt) {
    expr = ParseIntLiteral();
  } else if (tok_type == TokenType::kUint) {
    expr = ParseUintLiteral();
  } else if (tok_type == TokenType::kFloat) {
    expr = ParseDoubleLiteral();
  } else if (tok_type == TokenType::kString) {
    expr = ParseStringLiteral();
  } else if (tok_type == TokenType::kBytes) {
    expr = ParseBytesLiteral();
  } else if (tok_type == TokenType::kLeftBracket) {
    expr = ParseList();
  } else if (tok_type == TokenType::kLeftBrace) {
    expr = ParseMap();
  } else if (tok_type == TokenType::kDot || tok_type == TokenType::kIdent ||
             tok_type == TokenType::kReservedWord) {
    bool leading_dot = false;
    Token first_tok = peek_token_;
    if (tok_type == TokenType::kDot) {
      NextToken();
      leading_dot = true;
    }
    Token id_tok = NextToken();
    if (id_tok.type != TokenType::kIdent &&
        id_tok.type != TokenType::kReservedWord) {
      if (id_tok.type != TokenType::kError) {
        ReportError(id_tok, "expected identifier");
      }
      expr = ast_factory_.NewUnspecified(NextId(id_tok));
    } else {
      std::string id_text = NormalizeIdent(id_tok, /*allow_quoted=*/false);
      if (cel::internal::LexisIsReserved(id_text)) {
        ReportError(id_tok,
                    absl::StrFormat("reserved identifier: %s", id_text));
      }
      std::string name =
          leading_dot ? absl::StrCat(".", id_text) : std::string(id_text);
      int64_t id = NextId(leading_dot ? first_tok : id_tok);
      if (peek_token_.type == TokenType::kLeftParen) {
        NextToken();
        std::vector<ExprNode> args = ParseArguments(TokenType::kRightParen);
        expr = ast_factory_.NewCall(id, name, std::move(args));
      } else {
        expr = ast_factory_.NewIdent(id, std::move(name));
      }
    }
  } else {
    Token bad_tok = NextToken();
    if (bad_tok.type != TokenType::kError) {
      if (bad_tok.type == TokenType::kEnd) {
        ReportError(
            bad_tok,
            "Syntax error: mismatched input '<EOF>' expecting expression");
      } else {
        ReportError(bad_tok, "unexpected token");
      }
    }
    expr = ast_factory_.NewUnspecified(NextId(bad_tok));
  }

  return expr;
}

// Parses list creation literals (e.g., `[1, 2, ?3]`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseList() {
  Token open_tok = NextToken();
  int64_t list_id = NextId(open_tok);
  ListNodeBuilder<ExprNode> builder = ast_factory_.NewListBuilder(list_id);
  while (peek_token_.type != TokenType::kRightBracket &&
         peek_token_.type != TokenType::kEnd) {
    bool optional = false;
    if (peek_token_.type == TokenType::kQuestion) {
      Token q = NextToken();
      optional = true;
      if (!options_.enable_optional_syntax) {
        ReportError(q, "unsupported syntax '?'");
      }
    }
    ExprNode elem = ParseExpr();
    builder.Add(std::move(elem), optional);
    if (peek_token_.type == TokenType::kComma) {
      NextToken();
    } else {
      break;
    }
  }
  Expect(TokenType::kRightBracket, "expected ']'");
  return builder.Build();
}

// Parses map creation literals (e.g., `{"key": "value", ?"opt_key": 42}`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseMap() {
  Token open_tok = NextToken();
  int64_t map_id = NextId(open_tok);
  MapNodeBuilder<ExprNode> builder = ast_factory_.NewMapBuilder(map_id);
  while (peek_token_.type != TokenType::kRightBrace &&
         peek_token_.type != TokenType::kEnd) {
    bool optional = false;
    Token key_start = peek_token_;
    if (key_start.type == TokenType::kQuestion) {
      Token q = NextToken();
      optional = true;
      if (!options_.enable_optional_syntax) {
        ReportError(q, "unsupported syntax '?'");
      }
      key_start = peek_token_;
    }
    ExprNode key = ParseExpr();
    Token colon = peek_token_;
    if (!Expect(TokenType::kColon, "expected ':' in map entry")) {
      break;
    }
    int64_t entry_id = NextId(colon);
    ExprNode val = ParseExpr();
    builder.Add(entry_id, std::move(key), std::move(val), optional);
    if (peek_token_.type == TokenType::kComma) {
      NextToken();
    } else {
      break;
    }
  }
  Expect(TokenType::kRightBrace, "expected '}'");
  return builder.Build();
}

// Parses struct literas for a type name (e.g., `Msg{field: 10, ?opt: "val"}`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseStruct(
    int64_t obj_id, absl::string_view struct_name) {
  Token open_tok = NextToken();
  StructNodeBuilder<ExprNode> builder =
      ast_factory_.NewStructBuilder(obj_id, std::string(struct_name));
  while (peek_token_.type != TokenType::kRightBrace &&
         peek_token_.type != TokenType::kEnd) {
    bool optional = false;
    if (peek_token_.type == TokenType::kQuestion) {
      Token q = NextToken();
      optional = true;
      if (!options_.enable_optional_syntax) {
        ReportError(q, "unsupported syntax '?'");
      }
    }
    Token field_tok = NextToken();
    if (field_tok.type != TokenType::kIdent &&
        field_tok.type != TokenType::kReservedWord) {
      ReportError(field_tok, "expected struct field name");
      SynchronizeOnDelimiter();
      break;
    }
    std::string field_name = NormalizeIdent(field_tok, /*allow_quoted=*/true);
    Token colon = peek_token_;
    if (!Expect(TokenType::kColon, "expected ':' in struct field")) {
      break;
    }
    int64_t field_id = NextId(colon);
    ExprNode val = ParseExpr();
    builder.Add(field_id, std::move(field_name), std::move(val), optional);
    if (peek_token_.type == TokenType::kComma) {
      NextToken();
    } else {
      break;
    }
  }
  Expect(TokenType::kRightBrace, "expected '}'");
  return builder.Build();
}

// Parses comma-separated arguments (e.g., `(arg1, arg2)` in call).
template <typename ExprNode>
std::vector<ExprNode> PrattParserWorker<ExprNode>::ParseArguments(
    TokenType close_token) {
  std::vector<ExprNode> args;
  if (peek_token_.type != close_token && peek_token_.type != TokenType::kEnd) {
    while (true) {
      args.push_back(ParseExpr());
      if (peek_token_.type == TokenType::kComma) {
        NextToken();
        if (peek_token_.type == close_token) {
          break;
        }
        continue;
      }
      break;
    }
  }
  Expect(close_token);
  return args;
}

// Parses decimal & hexadecimal ints (e.g., `42`, `0x1A`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseIntLiteral() {
  Token tok = NextToken();
  std::string value(GetTokenText(tok));
  int64_t int_val = 0;
  if (absl::StartsWith(value, "0x") || absl::StartsWith(value, "0X")) {
    if (absl::SimpleHexAtoi(value, &int_val)) {
      return ast_factory_.NewIntConst(NextId(tok), int_val);
    }
  } else if (absl::SimpleAtoi(value, &int_val)) {
    return ast_factory_.NewIntConst(NextId(tok), int_val);
  }
  ReportError(tok, "invalid int literal");
  return ast_factory_.NewUnspecified(NextId(tok));
}

// Parses unsigned ints (e.g., `42u`, `0x1Au`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseUintLiteral() {
  Token tok = NextToken();
  std::string value(GetTokenText(tok));
  if (!value.empty() && (value.back() == 'u' || value.back() == 'U')) {
    value.pop_back();
  }
  uint64_t uint_val = 0;
  if (absl::StartsWith(value, "0x") || absl::StartsWith(value, "0X")) {
    if (absl::SimpleHexAtoi(value, &uint_val)) {
      return ast_factory_.NewUintConst(NextId(tok), uint_val);
    }
  } else if (absl::SimpleAtoi(value, &uint_val)) {
    return ast_factory_.NewUintConst(NextId(tok), uint_val);
  }
  ReportError(tok, "invalid uint literal");
  return ast_factory_.NewUnspecified(NextId(tok));
}

// Parses floating-point numbers (e.g., `3.14159`, `1e-10`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseDoubleLiteral() {
  Token tok = NextToken();
  std::string value(GetTokenText(tok));
  double double_val = 0.0;
  if (absl::SimpleAtod(value, &double_val)) {
    return ast_factory_.NewDoubleConst(NextId(tok), double_val);
  }
  ReportError(tok, "invalid double literal");
  return ast_factory_.NewUnspecified(NextId(tok));
}

// Parses string literals (e.g., `"hello"`, `'world'`, `"""multi"""`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseStringLiteral() {
  Token tok = NextToken();
  absl::StatusOr<std::string> status_or_val =
      cel::internal::ParseStringLiteral(GetTokenText(tok));
  if (!status_or_val.ok()) {
    ReportError(tok, status_or_val.status().message());
    return ast_factory_.NewUnspecified(NextId(tok));
  }
  return ast_factory_.NewStringConst(NextId(tok), std::move(*status_or_val));
}

// Parses byte sequence literals (e.g., `b"hello"`, `b'world'`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseBytesLiteral() {
  Token tok = NextToken();
  absl::StatusOr<std::string> status_or_val =
      cel::internal::ParseBytesLiteral(GetTokenText(tok));
  if (!status_or_val.ok()) {
    ReportError(tok, status_or_val.status().message());
    return ast_factory_.NewUnspecified(NextId(tok));
  }
  return ast_factory_.NewBytesConst(NextId(tok), std::move(*status_or_val));
}

// Normalizes regular & quoted identifiers (e.g., `foo`, `` `quoted.ident` ``).
template <typename ExprNode>
std::string PrattParserWorker<ExprNode>::NormalizeIdent(const Token& tok,
                                                        bool allow_quoted) {
  std::string text = GetTokenText(tok);
  if (text.empty()) return "";
  if (text.front() == '`') {
    if (!allow_quoted) {
      ReportError(tok, "unexpected quoted identifier");
      return "";
    }
    if (!options_.enable_quoted_identifiers) {
      ReportError(tok, "unsupported syntax '`'");
    }
    if (text.size() < 2 || text.back() != '`') {
      ReportError(tok, "unterminated quoted identifier");
      return "";
    }
    // Validate the quoted identifier syntax:
    // ESC_IDENTIFIER : '`' (LETTER | DIGIT | '_' | '.' | '-' | '/' | ' ')+ '`';
    std::string_view inner(text.data() + 1, text.size() - 2);
    if (inner.empty()) {
      ReportError(tok, "unexpected quoted identifier");
      return "";
    }
    for (char c : inner) {
      if (!absl::ascii_isalnum(static_cast<unsigned char>(c)) && c != '_' &&
          c != '.' && c != '-' && c != '/' && c != ' ') {
        ReportError(tok, "unexpected quoted identifier");
        return "";
      }
    }
    return std::string(inner);
  }
  return std::string(text);
}

// Extracts qualified type names (e.g., `a.b.Msg` from `a.b.Msg{}`).
template <typename ExprNode>
std::optional<std::string> PrattParserWorker<ExprNode>::ExtractStructName(
    const ExprNode& expr) {
  if (ast_factory_.IsConst(expr)) {
    return std::nullopt;
  }
  if (ast_factory_.IsIdent(expr)) {
    std::string name(ast_factory_.GetIdentName(expr));
    EraseId(ast_factory_.GetId(expr));
    return name;
  }
  if (ast_factory_.IsSelect(expr)) {
    if (ast_factory_.IsPresenceTest(expr)) return std::nullopt;
    const ExprNode* operand = ast_factory_.GetSelectOperand(expr);
    if (operand == nullptr) return std::nullopt;
    EraseId(ast_factory_.GetId(expr));
    absl::optional<std::string> prefix = ExtractStructName(*operand);
    if (!prefix) return std::nullopt;
    std::string name =
        absl::StrCat(*prefix, ".", ast_factory_.GetSelectField(expr));
    return name;
  }
  return std::nullopt;
}

template <typename ExprNode>
int32_t PrattParserWorker<ExprNode>::GetLeftmostPosition(const ExprNode& expr) {
  if (ast_factory_.IsIdent(expr)) {
    auto it = positions_.find(ast_factory_.GetId(expr));
    return it != positions_.end() ? it->second : 0;
  }
  if (ast_factory_.IsSelect(expr)) {
    return GetLeftmostPosition(*ast_factory_.GetSelectOperand(expr));
  }
  auto it = positions_.find(ast_factory_.GetId(expr));
  return it != positions_.end() ? it->second : 0;
}

// Recursively constructs a balanced binary AST tree for chained associative
// operators (e.g., chains of `+` or `*`). To prevent deep recursion and
// stack overflow during evaluation of expressions like `a + b + c + d`, this
// method splits the operand terms in half at midpoint `(lo + hi + 1) / 2` and
// combines the left and right subtrees with binary call nodes.
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::BalancedTree(
    absl::string_view op, std::vector<ExprNode>& terms,
    const std::vector<int64_t>& ops, int lo, int hi) {
  int mid = (lo + hi + 1) / 2;
  std::vector<ExprNode> arguments;
  arguments.reserve(2);
  if (mid == lo) {
    arguments.push_back(std::move(terms[mid]));
  } else {
    arguments.push_back(BalancedTree(op, terms, ops, lo, mid - 1));
  }
  if (mid == hi) {
    arguments.push_back(std::move(terms[mid + 1]));
  } else {
    arguments.push_back(BalancedTree(op, terms, ops, mid + 1, hi));
  }
  return ast_factory_.NewCall(ops[mid], std::string(op), std::move(arguments));
}

// Constructs the AST representation for chained logical operators (e.g.,
// `a && b && c` or `a || b || c`). When variadic logical ops are enabled
// (`enable_variadic = true`), it creates a single N-ary function call node
// containing all terms as arguments (e.g., `_&&_(a, b, c)`). Otherwise, it
// delegates to `BalancedTree` to produce a balanced binary tree of logical
// operations `(a && b) && c`.
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::BalanceLogical(
    absl::string_view op, std::vector<ExprNode> terms, std::vector<int64_t> ops,
    bool enable_variadic) {
  if (terms.size() == 1) {
    return std::move(terms[0]);
  }
  if (enable_variadic) {
    return ast_factory_.NewCall(ops[0], std::string(op), std::move(terms));
  }
  return BalancedTree(op, terms, ops, 0, ops.size() - 1);
}

}  // namespace cel::parser_internal

#endif  // THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_PRATT_PARSER_WORKER_H_
