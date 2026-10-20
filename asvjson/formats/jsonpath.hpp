#pragma once
// JSONPath (RFC 9535) implementation for asvJSON++
// Supports:
//   $ (root), .member, ['member'], [index], [start:end:step] (slice),
//   ..member / ..* / ..[selectors] (recursive descent), [*] (wildcard), .*,
//   [?filter] / [?(filter)] with operators: < > <= >= == != && || !
//   functions: length(), count(), min(), max(), sum(), avg(), match(), search(),
//   value()
//
// RFC compliance notes:
//   - Numeric selectors/bounds/indices are validated per the RFC int grammar
//     (no leading zeros) and must lie in the I-JSON exact range
//     [-(2^53)+1, (2^53)-1]; out-of-range or malformed numbers select nothing
//     at query time and reject the path in isValidPath.
//   - Filter *literals* follow the RFC 9535 2.3.5.1 number grammar
//     `number = (int / "-0") [ frac ] [ exp ]`: `-0` and `-0.5` are valid
//     filter numbers -- the explicit `"-0"` alternative exists precisely
//     because `int = "0" / (["-"] DIGIT1 *DIGIT)` on its own rejects `-0`.
//     A leading zero is still rejected everywhere (`-01`, `01`), matching
//     both grammars. Index and slice selectors keep the strict 2.3.3 int
//     grammar, where `-0` is NOT an int: `$[-0]` selects nothing and is
//     reported invalid by isValidPath, while `$[?@.a == -0]` is a valid
//     filter that compares as integer 0.
//   - Recursive descent is supported at the top level ($..name, $..*, $..[...])
//     but NOT inside a filter. The RFC grammar allows a descendant-segment in a
//     filter-query, here `$[?(@..a)]` and `$[?@.a..b]` are rejected by
//     isValidPath (the filter's query-path grammar has no descendant-segment)
//     and select nothing through query(). Not supporting it is a deliberate
//     simplification of the single-value filter evaluator described below.
//   - Filter comparisons are exact for int64 operands; an empty/nothing operand
//     yields false for every operator except == where both sides must be empty.
//   - A bare query in a filter is an existence test: true iff it selects at
//     least one node; the selected node's value is irrelevant (null/false/{}).
//     `!` may precede any basic-expr, i.e. both a paren-expr and a test-expr
//     ($[?(!(@.a))], $[?!@.a], $[?!length(@.items)]); a comparison-expr may not
//     follow a negated test-expr, so $[?(!@.a == 5)] is rejected. RFC 9535
//     allows a single NOT per basic-expr and has no recursive not-production, so
//     `!!@.a` is rejected too - write $[?(!(@.a))].
//   - Bare literals and ValueType functions in a test position ($[?0],
//     $[?length(@.s)]) are *not well-typed* per 9535 2.4.3 - a test-expr is a
//     filter-query or a LogicalType/NodesType function-expr - so their meaning is
//     an extension: they evaluate by JSON-ish truthiness, where false, null,
//     0/0.0 and the empty string are false and anything else is true. An empty
//     array/object is *not* false (jpValueTruthy falls through to `true` for
//     them), and no function can produce one here anyway: length/count/sum/avg
//     yield numbers, min/max only compare numbers, and value() takes the
//     existence path below. Existence tests (@.a) bypass truthiness entirely:
//     they are true whenever the node exists, INT 0 and "" included.
//   - A function used as a path segment must be zero-arg: $.items.length() and
//     $[?(@.items.length() > 2)] are supported, a segment call with an argument
//     ($.length(@.a), $[?(@.length(@.a))]) is not a path and is reported
//     invalid; query() yields no result for it. The same holds for
//     match()/search(): they take two arguments (RFC 9535 2.4.6/2.4.7), so
//     `$.match()` is not a path either -- isValidPath rejects it and query()
//     yields no result for it.
//   - value() as a filter operand formally requires exactly one NodesType
//     argument (RFC 9535 2.4.8): `value()` with no argument is rejected by
//     isValidPath() and yields nothing via query(); `value(@.a)` is the correct
//     spelling. Only the path-segment form ($.x.value(), the local extension
//     described in the previous bullet) is a valid zero-arg identity call.
//   - String literals follow the RFC escape rules, including \uXXXX escapes
//     and surrogate pairs.
//   - Slice semantics follow the RFC Normalize()/Bounds() algorithm exactly.
//   - Filters are evaluated with single-value (not nodelist) semantics: a path
//     fragment inside a filter resolves to at most one node, so count(x) means
//     "x selected something" (1/0), not "number of nodes in x's result set";
//     likewise `@.a[*] == 5` tests membership loosely rather than comparing the
//     full nodelist per RFC 2.3.5.2.2. This is a deliberate design choice to
//     keep the filter evaluator simple and allocation-light, not an oversight;
//     paths that genuinely need multi-node comparison should be split into
//     separate query() calls combined by caller code.
//   - Function arguments follow the RFC 9535 2.4.3 well-typedness rule per
//     the declared parameter type: a ValueType parameter (match/search input
//     and pattern, length) accepts any scalar value literal or a singular
//     query -- there is no static type check to perform, because a non-string
//     input or pattern simply evaluates to LogicalFalse, which is what the
//     evaluator does (match(5, "pat") and match("abc", 5) are both
//     well-typed and both false; length("abc") evaluates to 3). A NodesType
//     parameter (count, value) accepts only a query path, and min/max/sum/avg
//     (library extensions not in the RFC) keep the query form their array
//     semantics need. Two narrowing deviations, kept identical in isValidPath
//     and query(): a query as the match/search *pattern* (legal per the RFC)
//     is rejected because the evaluator would read it as a non-string anyway,
//     and a nested function argument (length(count(@.a)), also legal per the
//     RFC) is rejected because neither side implements it. Patterns go
//     through std::regex (ECMAScript), which is a superset of the RFC 9485
//     I-Regexp subset; patterns relying on lookahead/lookbehind or
//     backreferences will compile here even though they are not valid
//     I-Regexps. Untrusted, hand-crafted patterns carry ReDoS risk since
//     std::regex has no timeout - validate/reject suspiciously nested
//     quantifiers at your boundary before passing them in when the pattern
//     source is untrusted. Invalid or otherwise rejected patterns simply
//     yield false (no exception escapes).
//   - query()/queryFirst() are lenient: malformed, unterminated, or otherwise
//     invalid path segments select nothing rather than throwing; however they do
//     not guard against out-of-memory conditions - std::bad_alloc (or similar)
//     from internal allocations while evaluating deep/wide filters on very large
//     trees can still propagate to the caller. Catch at your call site if that is
//     a concern for untrusted inputs.

#include "../core.hpp"
#include <regex>
#include <algorithm>
#include <functional>
#include <cerrno>
#include <cstdlib>
#include <cmath>

namespace asvJSONInternal {

enum class TokType {
  END, DOT, DOTDOT, LBRACKET, RBRACKET, STAR, COLON, QUESTION,
  LPAREN, RPAREN, COMMA, LT, GT, LE, GE, EQ, NE, AND, OR, NOT,
  IDENT, NUMBER, STRING, TRUE_LIT, FALSE_LIT, NULL_LIT
};

struct Tok {
  TokType type;
  std::string val;
  bool ok = true;
};

inline int jpHexDig(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline void jpAppendUtf8(uint32_t cp, std::string& s) {
  if (cp < 0x80) {
    s.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Strict RFC `int` parse: "0" or (["-"] DIGIT1 *DIGIT). The value must lie in
// the I-JSON exact range [-(2^53)+1, (2^53)-1]. Throws nothing; returns false
// on malformed or out-of-range input.
inline bool jpParseIntStrict(const std::string& s, int64_t& out) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '-') {
    if (s.size() == 1) return false;
    i = 1;
  }
  if (i == 0 && s[i] == '0') {
    if (i + 1 != s.size()) return false;
    out = 0;
    return true;
  }
  if (s[i] < '1' || s[i] > '9') return false;
  for (size_t k = i; k < s.size(); ++k) {
    if (s[k] < '0' || s[k] > '9') return false;
  }
  errno = 0;
  char* endp = nullptr;
  long long v = std::strtoll(s.c_str(), &endp, 10);
  if (errno == ERANGE || endp != s.c_str() + s.size()) return false;
  if (v > 9007199254740991LL || v < -9007199254740991LL) return false;
  out = static_cast<int64_t>(v);
  return true;
}

// Lenient int64 parse for filter numeric literals without frac/exp: follows
// the RFC 9535 2.3.5.1 number grammar `(int / "-0")`, so `-0` is accepted as
// integer 0 while `-01`/`01` stay rejected; only the int64 range is enforced
// (no I-JSON clamp), so exact int64 comparisons in filters keep their
// precision. Never throws.
inline bool jpParseInt64(const std::string& s, int64_t& out) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '-') {
    if (s.size() == 1) return false;
    i = 1;
  }
  if (s[i] == '0') {
    if (i + 1 != s.size()) return false;
    out = 0;
    return true;
  }
  if (s[i] < '1' || s[i] > '9') return false;
  for (size_t k = i; k < s.size(); ++k) {
    if (s[k] < '0' || s[k] > '9') return false;
  }
  errno = 0;
  char* endp = nullptr;
  long long v = std::strtoll(s.c_str(), &endp, 10);
  if (errno == ERANGE || endp != s.c_str() + s.size()) return false;
  out = static_cast<int64_t>(v);
  return true;
}

// Lenient decimal literal parse for filter operands: RFC number grammar with
// exponent support. Never throws.
inline bool jpParseNumber(const std::string& s, double& out) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '-') {
    if (s.size() == 1) return false;
    i = 1;
  }
  // int part ("0" / DIGIT1 *DIGIT); the optional '-' was consumed above, so
  // "-0" is accepted per the 2.3.5.1 alternative `(int / "-0")`.
  if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
  if (s[i] == '0') {
    i++;
    if (i < s.size() && s[i] >= '0' && s[i] <= '9') return false;
  } else {
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
  }
  // frac
  if (i < s.size() && s[i] == '.') {
    i++;
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
  }
  // exp
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
  }
  if (i != s.size()) return false;
  errno = 0;
  char* endp = nullptr;
  double v = std::strtod(s.c_str(), &endp);
  if (endp != s.c_str() + s.size()) return false;
  if (std::isinf(v) || std::isnan(v)) return false;
  out = v;
  return true;
}

class PathLexer {
  std::string_view src;
  size_t pos_ = 0;
public:
  explicit PathLexer(std::string_view s) : src(s) {}

  Tok next() {
    skipWS();
    if (pos_ >= src.size()) return {TokType::END, ""};
    char c = src[pos_];
    if (c == '.') {
      if (pos_ + 1 < src.size() && src[pos_ + 1] == '.') {
        pos_ += 2;
        return {TokType::DOTDOT, ".."};
      }
      pos_++;
      return {TokType::DOT, "."};
    }
    if (c == '[') { pos_++; return {TokType::LBRACKET, "["}; }
    if (c == ']') { pos_++; return {TokType::RBRACKET, "]"}; }
    if (c == '*') { pos_++; return {TokType::STAR, "*"}; }
    if (c == ':') { pos_++; return {TokType::COLON, ":"}; }
    if (c == '?') { pos_++; return {TokType::QUESTION, "?"}; }
    if (c == '(') { pos_++; return {TokType::LPAREN, "("}; }
    if (c == ')') { pos_++; return {TokType::RPAREN, ")"}; }
    if (c == ',') { pos_++; return {TokType::COMMA, ","}; }
    if (c == '<') {
      if (pos_ + 1 < src.size() && src[pos_ + 1] == '=') { pos_ += 2; return {TokType::LE, "<="}; }
      pos_++; return {TokType::LT, "<"};
    }
    if (c == '>') {
      if (pos_ + 1 < src.size() && src[pos_ + 1] == '=') { pos_ += 2; return {TokType::GE, ">="}; }
      pos_++; return {TokType::GT, ">"};
    }
    if (c == '=' && pos_ + 1 < src.size() && src[pos_ + 1] == '=') {
      pos_ += 2; return {TokType::EQ, "=="};
    }
    if (c == '!' && pos_ + 1 < src.size() && src[pos_ + 1] == '=') {
      pos_ += 2; return {TokType::NE, "!="};
    }
    if (c == '&' && pos_ + 1 < src.size() && src[pos_ + 1] == '&') {
      pos_ += 2; return {TokType::AND, "&&"};
    }
    if (c == '|' && pos_ + 1 < src.size() && src[pos_ + 1] == '|') {
      pos_ += 2; return {TokType::OR, "||"};
    }
    if (c == '!') { pos_++; return {TokType::NOT, "!"}; }
    if (c == '\'' || c == '"') {
      char q = c;
      pos_++;
      std::string s;
      bool ok = true;
      while (pos_ < src.size() && src[pos_] != q) {
        char ch = src[pos_];
        if (ch == '\\') {
          pos_++;
          if (pos_ >= src.size()) { ok = false; break; }
          char e = src[pos_++];
          switch (e) {
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case '/': s += '/'; break;
            case '\\': s += '\\'; break;
            case '\'': s += '\''; break;
            case '"': s += '"'; break;
            case 'u': {
              auto hex4 = [&](uint32_t& cp) -> bool {
                uint32_t v = 0;
                for (int k = 0; k < 4; ++k) {
                  if (pos_ >= src.size()) return false;
                  int d = jpHexDig(src[pos_++]);
                  if (d < 0) return false;
                  v = (v << 4) | static_cast<uint32_t>(d);
                }
                cp = v;
                return true;
              };
              uint32_t cp = 0;
              if (!hex4(cp)) { ok = false; break; }
              if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (pos_ + 1 < src.size() && src[pos_] == '\\' && src[pos_ + 1] == 'u') {
                  pos_ += 2;
                  uint32_t lo = 0;
                  if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) { ok = false; break; }
                  cp = 0x10000 + static_cast<uint32_t>((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                  ok = false; break;
                }
              } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                ok = false;
                break;
              }
              jpAppendUtf8(cp, s);
              break;
            }
            default: ok = false; break;
          }
          if (!ok) break;
        } else {
          s += ch;
          pos_++;
        }
      }
      if (pos_ < src.size() && src[pos_] == q) pos_++;
      else ok = false;
      return {TokType::STRING, s, ok};
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      std::string num;
      num += c; pos_++;
      while (pos_ < src.size() && ((src[pos_] >= '0' && src[pos_] <= '9') || src[pos_] == '.')) {
        num += src[pos_]; pos_++;
      }
      if (pos_ < src.size() && (src[pos_] == 'e' || src[pos_] == 'E')) {
        size_t save = pos_;
        size_t k = pos_ + 1;
        if (k < src.size() && (src[k] == '+' || src[k] == '-')) k++;
        if (k < src.size() && src[k] >= '0' && src[k] <= '9') {
          num += src[pos_++];
          if (pos_ < src.size() && (src[pos_] == '+' || src[pos_] == '-')) num += src[pos_++];
          while (pos_ < src.size() && src[pos_] >= '0' && src[pos_] <= '9') num += src[pos_++];
        } else {
          pos_ = save;
        }
      }
      return {TokType::NUMBER, num};
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '@' || c == '$') {
      std::string id;
      id += c;
      pos_++;
      while (pos_ < src.size() && ((src[pos_] >= 'a' && src[pos_] <= 'z') ||
             (src[pos_] >= 'A' && src[pos_] <= 'Z') || (src[pos_] >= '0' && src[pos_] <= '9') ||
             src[pos_] == '_' || src[pos_] == '@')) {
        id += src[pos_]; pos_++;
      }
      if (id == "true") return {TokType::TRUE_LIT, id};
      if (id == "false") return {TokType::FALSE_LIT, id};
      if (id == "null") return {TokType::NULL_LIT, id};
      return {TokType::IDENT, id};
    }
    // Fall-through: a character the lexer does not recognize. Emit END with
    // ok=false so callers can distinguish a real end of input (ok=true, the
    // buffer ran out) from a stray byte (e.g. '+', '{') that was skipped. The
    // path grammar checks rely on this to reject garbage instead of treating
    // it as the end of a valid query path.
    pos_++;
    return {TokType::END, "", false};
  }

  Tok peek() {
    size_t saved = pos_;
    Tok t = next();
    pos_ = saved;
    return t;
  }

  bool hasMore() { skipWS(); return pos_ < src.size(); }

  size_t pos() const { return pos_; }
  void setPos(size_t p) { pos_ = p; }

  // Advance raw position past the ']' that closes the current bracket segment
  // (depth-aware over nested '[', skipped quoted strings). Caller has consumed
  // the opening '['.
  bool skipToEndBracket() {
    int depth = 0;
    char q = 0;
    while (pos_ < src.size()) {
      char c = src[pos_];
      if (q) {
        if (c == '\\') { pos_ += 2; continue; }
        if (c == q) q = 0;
        pos_++; continue;
      }
      if (c == '\'' || c == '"') { q = c; pos_++; continue; }
      if (c == '[') { depth++; pos_++; continue; }
      if (c == ']') {
        if (depth == 0) { pos_++; return true; }
        depth--; pos_++; continue;
      }
      pos_++;
    }
    return false;
  }

  // Advance the raw position from just past the '?' of a filter-selector to
  // the split point at the end of the filter expression: the ']' that closes
  // the bracket segment (returns true) or the ',' that introduces the next
  // selector (returns false; the caller's COMMA branch continues the list).
  // Token-aware, so a comma inside a quoted string is a STRING token and
  // cannot end the scan, and depth-aware over the '('...')' and '['...']'
  // pairs a filter can contain (`max(@.a, @.b) > 2`, `@.x[1] == 3`). Garbage
  // and end-of-input both lex to END; the byte is left in place for the
  // caller's RBRACKET check to reject. This must not depend on the filter
  // evaluator having consumed the whole expression: parseAnd/parseOr
  // short-circuit, so an unevaluated trailing `&& term` cannot leave the
  // lexer stranded mid-filter.
  bool skipToFilterEnd() {
    int paren = 0, brack = 0;
    while (pos_ < src.size()) {
      size_t save = pos_;
      Tok t = next();
      if (t.type == TokType::LPAREN) { paren++; continue; }
      if (t.type == TokType::RPAREN) { if (paren > 0) paren--; continue; }
      if (t.type == TokType::LBRACKET) { brack++; continue; }
      if (t.type == TokType::RBRACKET) {
        if (brack == 0) { pos_ = save; return true; }
        brack--; continue;
      }
      if (t.type == TokType::COMMA && paren == 0 && brack == 0) {
        pos_ = save; return false;
      }
      if (t.type == TokType::END) { pos_ = save; return false; }
    }
    return false;
  }

private:
  void skipWS() {
    while (pos_ < src.size()) {
      char c = src[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos_++;
      else break;
    }
  }
};

// Returns the RFC "length" of a node, or -1 (Nothing) when it has no length:
// null/missing nodes, booleans, numbers. Callers treat <0 as Nothing and must not emit it.
inline int jpGetPropCount(const asvJSONValue* n) {
  if (!n) return -1;
  if (n->type == asvJSONValue::STRING) {
    int count = 0;
    const std::string& s = n->str_data;
    for (size_t i = 0; i < s.size(); ) {
      auto c = static_cast<unsigned char>(s[i]);
      if (c < 0x80) { count++; i++; }
      else if (c < 0xC0) { i++; }
      else if (c < 0xE0) { count++; i += 2; }
      else if (c < 0xF0) { count++; i += 3; }
      else { count++; i += 4; }
    }
    return count;
  }
  if (n->type == asvJSONValue::ARRAY && n->arr) return static_cast<int>(n->arr->size());
  if (n->type == asvJSONValue::OBJECT && n->obj) return static_cast<int>(n->obj->size());
  return -1;
}

inline const asvJSONValue* jpGetProp(const asvJSONValue* n, const std::string& name) {
  if (!n || n->type != asvJSONValue::OBJECT || !n->obj) return nullptr;
  auto it = n->obj->find(name);
  return it != n->obj->end() ? it->second.get() : nullptr;
}

inline std::vector<const asvJSONValue*> jpAllChildren(const asvJSONValue* n) {
  std::vector<const asvJSONValue*> r;
  if (!n) return r;
  if (n->type == asvJSONValue::OBJECT && n->obj) {
    for (auto& kv : *n->obj) r.push_back(kv.second.get());
  } else if (n->type == asvJSONValue::ARRAY && n->arr) {
    for (auto& e : *n->arr) r.push_back(e.get());
  }
  return r;
}

// The descendant visit set: the input node followed by every descendant,
// parents before children (RFC 9535 2.5.2.2: D1 is the input value).
inline void jpVisitAll(const asvJSONValue* n, std::vector<const asvJSONValue*>& r) {
  if (!n) return;
  r.push_back(n);
  auto children = jpAllChildren(n);
  for (auto c : children) jpVisitAll(c, r);
}

// Descendant child-segment results. The child selector is applied to each
// visited node, so "..*" collects the children of every visited node (never
// the nodes themselves) and "..name" collects each visited node's child with
// that name.
inline void jpDescChildrenAll(const asvJSONValue* n, std::vector<const asvJSONValue*>& r) {
  if (!n) return;
  auto children = jpAllChildren(n);
  for (auto c : children) r.push_back(c);
  for (auto c : children) jpDescChildrenAll(c, r);
}

inline void jpDescChildrenName(const asvJSONValue* n, const std::string& name,
                               std::vector<const asvJSONValue*>& r) {
  if (!n) return;
  const asvJSONValue* child = jpGetProp(n, name);
  if (child) r.push_back(child);
  for (auto c : jpAllChildren(n)) jpDescChildrenName(c, name, r);
}

// RFC 9535 array slice with the Normalize()/Bounds() algorithm (2.3.4.2.2).
// start/end/step must already be defaulted per Table 8 (step default 1;
// step >= 0: start = 0, end = len; step < 0: start = len - 1, end = -len - 1).
// A step of 0 selects nothing.
inline std::vector<const asvJSONValue*> jpSliceAdd(const asvJSONValue* n,
                                                   int64_t start, int64_t end,
                                                   int64_t step) {
  std::vector<const asvJSONValue*> r;
  if (!n || n->type != asvJSONValue::ARRAY || !n->arr || step == 0) return r;
  int64_t len = static_cast<int64_t>(n->arr->size());
  if (len == 0) return r;

  auto norm = [len](int64_t i) -> int64_t { return i >= 0 ? i : len + i; };
  auto clamp = [](int64_t v, int64_t lo, int64_t hi) -> int64_t {
    return v < lo ? lo : (v > hi ? hi : v);
  };
  int64_t lower, upper;
  if (step > 0) {
    lower = clamp(norm(start), 0, len);
    upper = clamp(norm(end), 0, len);
    for (int64_t i = lower; i < upper; i += step)
      r.push_back((*n->arr)[static_cast<size_t>(i)].get());
  } else {
    upper = clamp(norm(start), -1, len - 1);
    lower = clamp(norm(end), -1, len - 1);
    for (int64_t i = upper; i > lower; i += step)
      r.push_back((*n->arr)[static_cast<size_t>(i)].get());
  }
  return r;
}

// Exact ordering for two numbers: int64 against int64 compares exactly
// (preserving full precision); anything involving a double compares as double.
inline int jpNumCmp(const asvJSONValue* a, const asvJSONValue* b) {
  bool aInt = a->type == asvJSONValue::INT;
  bool bInt = b->type == asvJSONValue::INT;
  if (aInt && bInt) {
    if (a->num < b->num) return -1;
    if (a->num > b->num) return 1;
    return 0;
  }
  double da = aInt ? static_cast<double>(a->num) : a->dbl;
  double db = bInt ? static_cast<double>(b->num) : b->dbl;
  if (da < db) return -1;
  if (da > db) return 1;
  return 0;
}

// Type-aware comparison for min/max extension functions and heterogeneous
// ordering; uses exact numeric comparison for INT/INT pairs.
inline int jpCompare(const asvJSONValue* a, const asvJSONValue* b) {
  if (!a && !b) return 0;
  if (!a) return -1;
  if (!b) return 1;

  bool aNum = (a->type == asvJSONValue::INT || a->type == asvJSONValue::DOUBLE);
  bool bNum = (b->type == asvJSONValue::INT || b->type == asvJSONValue::DOUBLE);
  if (aNum && bNum) return jpNumCmp(a, b);
  if (a->type == asvJSONValue::STRING && b->type == asvJSONValue::STRING) {
    if (a->str_data < b->str_data) return -1;
    if (a->str_data > b->str_data) return 1;
    return 0;
  }
  if (a->type == asvJSONValue::BOOL_VAL && b->type == asvJSONValue::BOOL_VAL) {
    return (a->flag == b->flag) ? 0 : (a->flag ? 1 : -1);
  }
  if (a->type == asvJSONValue::NULL_VAL && b->type == asvJSONValue::NULL_VAL) return 0;

  auto order = [](const asvJSONValue* v) -> int {
    if (!v || v->type == asvJSONValue::NULL_VAL) return 0;
    if (v->type == asvJSONValue::BOOL_VAL) return 1;
    if (v->type == asvJSONValue::INT || v->type == asvJSONValue::DOUBLE) return 2;
    if (v->type == asvJSONValue::STRING) return 3;
    if (v->type == asvJSONValue::ARRAY) return 4;
    if (v->type == asvJSONValue::OBJECT) return 5;
    return 6;
  };
  int oa = order(a), ob = order(b);
  return (oa < ob) ? -1 : (oa > ob ? 1 : 0);
}

inline bool jpDeepEqImpl(const asvJSONValue* x, const asvJSONValue* y);

// RFC 9535 deep equality: numbers compare by value (1 == 1.0), structurally
// equal arrays and objects compare equal.
inline bool jpDeepEq(const asvJSONValue* a, const asvJSONValue* b) {
  if (!a || !b) return a == b;
  return jpDeepEqImpl(a, b);
}

inline bool jpDeepEqImpl(const asvJSONValue* x, const asvJSONValue* y) {
  if (x->type == asvJSONValue::NULL_VAL && y->type == asvJSONValue::NULL_VAL) return true;
  if (x->type == asvJSONValue::BOOL_VAL || y->type == asvJSONValue::BOOL_VAL)
    return x->type == asvJSONValue::BOOL_VAL && y->type == asvJSONValue::BOOL_VAL && x->flag == y->flag;
  bool xNum = (x->type == asvJSONValue::INT || x->type == asvJSONValue::DOUBLE);
  bool yNum = (y->type == asvJSONValue::INT || y->type == asvJSONValue::DOUBLE);
  if (xNum && yNum) return jpNumCmp(x, y) == 0;
  if (x->type == asvJSONValue::STRING && y->type == asvJSONValue::STRING)
    return x->str_data == y->str_data;
  if (x->type == asvJSONValue::ARRAY && y->type == asvJSONValue::ARRAY && x->arr && y->arr) {
    if (x->arr->size() != y->arr->size()) return false;
    for (size_t i = 0; i < x->arr->size(); ++i) {
      if (!jpDeepEqImpl((*x->arr)[i].get(), (*y->arr)[i].get())) return false;
    }
    return true;
  }
  if (x->type == asvJSONValue::OBJECT && y->type == asvJSONValue::OBJECT && x->obj && y->obj) {
    if (x->obj->size() != y->obj->size()) return false;
    for (auto& kv : *x->obj) {
      auto it = y->obj->find(kv.first);
      if (it == y->obj->end()) return false;
      if (!jpDeepEqImpl(kv.second.get(), it->second.get())) return false;
    }
    return true;
  }
  return false;
}

// RFC 9535 filter comparison semantics (2.3.5.2.2). A null pointer operand
// denotes an empty nodelist or the special result Nothing.
inline bool jpCmpForFilter(const asvJSONValue* lhs, const asvJSONValue* rhs,
                           TokType op) {
  bool lEmpty = lhs == nullptr;
  bool rEmpty = rhs == nullptr;
  if (lEmpty || rEmpty) {
    switch (op) {
      case TokType::EQ: return lEmpty && rEmpty;
      case TokType::NE: return !(lEmpty && rEmpty);
      case TokType::LT:
      case TokType::LE:
      case TokType::GT:
      case TokType::GE: return false;
      default: return false;
    }
  }
  switch (op) {
    case TokType::EQ: return jpDeepEq(lhs, rhs);
    case TokType::NE: return !jpDeepEq(lhs, rhs);
    default: break;
  }
  bool lNum = (lhs->type == asvJSONValue::INT || lhs->type == asvJSONValue::DOUBLE);
  bool rNum = (rhs->type == asvJSONValue::INT || rhs->type == asvJSONValue::DOUBLE);
  bool lStr = lhs->type == asvJSONValue::STRING;
  bool rStr = rhs->type == asvJSONValue::STRING;
  if (!((lNum && rNum) || (lStr && rStr))) return false;
  int c = (lNum && rNum) ? jpNumCmp(lhs, rhs)
                         : (lhs->str_data < rhs->str_data ? -1 : (lhs->str_data > rhs->str_data ? 1 : 0));
  switch (op) {
    case TokType::LT: return c < 0;
    case TokType::GT: return c > 0;
    case TokType::LE: return c <= 0;
    case TokType::GE: return c >= 0;
    default: return false;
  }
}

// Truthiness of a value-result (filter literals and function results). Queries
// are handled as existence tests, not via this function. See the RFC compliance
// notes in the file header for why the ValueType/array cases below are
// extensions, and why the ARRAY/OBJECT fall-through is unreachable today.
inline bool jpValueTruthy(const asvJSONValue* v) {
  if (!v) return false;
  if (v->type == asvJSONValue::NULL_VAL) return false;
  if (v->type == asvJSONValue::BOOL_VAL) return v->flag;
  if (v->type == asvJSONValue::INT) return v->num != 0;
  if (v->type == asvJSONValue::DOUBLE) return v->dbl != 0.0;
  if (v->type == asvJSONValue::STRING) return !v->str_data.empty();
  return true;
}

inline bool jpIsFuncName(const std::string& s) {
  return s == "length" || s == "count" || s == "min" || s == "max" ||
         s == "sum" || s == "avg" || s == "match" || s == "search" || s == "value";
}

inline bool jpIsZeroArgFunc(const std::string& s) {
  return s == "length" || s == "count" || s == "min" || s == "max" ||
         s == "sum" || s == "avg" || s == "value";
}

// The five scalar-literal token types of the RFC 9535 `literal` production
// (number / string / true / false / null).
inline bool jpIsScalarLiteralTok(TokType ty) {
  return ty == TokType::NUMBER || ty == TokType::STRING ||
         ty == TokType::TRUE_LIT || ty == TokType::FALSE_LIT ||
         ty == TokType::NULL_LIT;
}

inline std::unique_ptr<asvJSONValue> jpApplyFunc(const std::string& fn, const asvJSONValue* arg);

class JPFilterEval {
  PathLexer& lex_;
  const asvJSONValue* cur_;
  const asvJSONValue* root_;
  std::vector<std::unique_ptr<asvJSONValue>> temp_vals_;
  bool lastOperandFromQuery_ = false;
  int filterDepth_ = 0;

  const asvJSONValue* hold(std::unique_ptr<asvJSONValue> val) {
    // A literal node is owned for the duration of the evaluation; a null
    // (factory OOM / oversize string) must never be dereferenced later by the
    // comparison operators or jpDeepEqual. Nothing is represented by returning
    // nullptr directly from resolvePath, never through hold(), so throwing here
    // does not change the "missing value" semantics.
    if (!val) throw asvJSONError("JSONPath: out of memory");
    temp_vals_.push_back(std::move(val));
    return temp_vals_.back().get();
  }

  struct BoolVal { bool val; };

  // RAII guard: decrements filterDepth_ exactly once on scope exit, even if the
  // guarded call (parseOr) throws or otherwise exits non-locally.
  struct FilterDepthGuard {
    int& depth_;
    explicit FilterDepthGuard(int& d) : depth_(d) {}
    ~FilterDepthGuard() { --depth_; }
  };

  bool parseOr() {
    bool l = parseAnd();
    while (lex_.peek().type == TokType::OR) {
      lex_.next();
      bool r = parseAnd();
      l = l || r;
    }
    return l;
  }

  bool parseAnd() {
    bool l = parseNot();
    while (lex_.peek().type == TokType::AND) {
      lex_.next();
      bool r = parseNot();
      l = l && r;
    }
    return l;
  }

  bool parseNot() {
    // RFC 9535 spells NOT as a single optional prefix on a paren-expr or a
    // test-expr (`paren-expr = [logical-not-op S] "(" ... ")"`,
    // `test-expr  = [logical-not-op S] (...)`) and defines no recursive
    // not-production, so `!!X` is not well-formed - it has to be written
    // `!(!X)`. The prefix is still consumed in a loop and negation toggled, so a
    // long run of '!' cannot be used to burn stack, but the second '!' in the
    // same position is rejected instead of being silently accepted.
    bool negate = false;
    while (lex_.peek().type == TokType::NOT) {
      lex_.next();
      if (negate) return false;
      negate = true;
    }
    bool sawCmp = false;
    bool r = parseComparison(&sawCmp);
    // logical-not-op binds a test-expr (or a paren-expr), and neither may be
    // followed by a comparison-op: `!@.a == 5` is not a basic-expr, so it must
    // not be evaluated as `!(@.a == 5)`. Kept in sync with the validator.
    if (negate && sawCmp) return false;
    return negate ? !r : r;
  }

  const asvJSONValue* resolvePath() {
    Tok t = lex_.next();
    const asvJSONValue* node = nullptr;
    if (t.type == TokType::IDENT && t.val == "@") node = cur_;
    else if (t.type == TokType::IDENT && t.val == "$") node = root_;
    else return nullptr;
    if (lex_.peek().type == TokType::DOTDOT) return nullptr; // recursive descent not supported in filters

    while (true) {
      if (lex_.peek().type == TokType::DOT) {
        lex_.next();
        Tok p = lex_.next();
        if (p.type != TokType::IDENT) {
          if (p.type == TokType::STAR) {
            // wildcard: the node is selected iff it has at least one child
            if (jpAllChildren(node).empty()) node = nullptr;
          } else {
            node = nullptr; break;
          }
        } else if (jpIsZeroArgFunc(p.val) && lex_.peek().type == TokType::LPAREN) {
          lex_.next();
          // Segment functions take no argument. An argument used to be left in
          // the stream while the function was still applied to the current node,
          // so `@.s.length(@.a)` silently resolved to the length of @.s and the
          // filter then matched on that; isValidPath rejects such a path, so
          // drop the whole path here too and let the filter not match.
          if (lex_.peek().type != TokType::RPAREN) { node = nullptr; break; }
          lex_.next();
          // Apply the function only while the node exists: a dead path must
          // stay Nothing, otherwise count() would turn it into INT 0.
          if (node) {
            auto r = jpApplyFunc(p.val, node);
            if (r) { temp_vals_.push_back(std::move(r)); node = temp_vals_.back().get(); }
            else node = nullptr;
          }
        } else {
          node = jpGetProp(node, p.val);
          if (!node) node = nullptr;
        }
      } else if (lex_.peek().type == TokType::LBRACKET) {
        lex_.next();
        Tok idx = lex_.next();
        const asvJSONValue* nextNode = nullptr;
        if (idx.type == TokType::NUMBER && node &&
            node->type == asvJSONValue::ARRAY && node->arr) {
          int64_t i;
          if (!idx.ok || !jpParseIntStrict(idx.val, i)) {
            nextNode = nullptr;
          } else {
            size_t ai;
            if (i < 0) {
              size_t mag = static_cast<size_t>(-i);
              if (mag > node->arr->size()) {
                nextNode = nullptr;
              } else {
                ai = node->arr->size() - mag;
                nextNode = (ai < node->arr->size()) ? (*node->arr)[ai].get() : nullptr;
              }
            } else {
              ai = static_cast<size_t>(i);
              nextNode = (ai < node->arr->size()) ? (*node->arr)[ai].get() : nullptr;
            }
          }
        } else if (idx.type == TokType::STRING && node &&
                   node->type == asvJSONValue::OBJECT && node->obj) {
          if (!idx.ok) {
            nextNode = nullptr;
          } else {
            auto it = node->obj->find(idx.val);
            nextNode = (it != node->obj->end()) ? it->second.get() : nullptr;
          }
        } else if (idx.type == TokType::STAR && node) {
          // wildcard: the node is selected iff it has at least one child
          if (!jpAllChildren(node).empty()) nextNode = node;
        }
        node = nextNode;
        // The single ']' consume point, error paths included: the error
        // branches above must not eat it themselves or a following ']'
        // (e.g. in `@[01]]`) would be swallowed twice.
        if (lex_.peek().type == TokType::RBRACKET) lex_.next();
      } else {
        break;
      }
      // NOTE: no early `if (!node) return nullptr` here. A dead path must
      // keep consuming its remaining segments (.length(), [0], [*]) so the
      // lexer ends exactly at the filter's ']' -- the filterEnd check of the
      // LAST child discards the results of ALL children when the evaluator
      // stops mid-expression (e.g. `[{"s":"ab"},{"t":1}]` with
      // `$[?(@.s.length() > 1)]` used to yield 0). jpGetProp/jpAllChildren/
      // the index branch above are all null-safe, so evaluation is a no-op
      // on a dead node and Nothing propagates to the end of the path.
    }
    // Trailing recursive-descent after a segment (e.g. @.a..b) must not be
    // silently ignored - the filter grammar has no descendant-segment, so
    // treat it as "selects nothing" to stay consistent with isValidPath.
    if (lex_.peek().type == TokType::DOTDOT) return nullptr;
    // Trailing garbage (e.g. @.a{) must also not be silently ignored - the
    // lexer falls through to !ok, which isValidPath catches but query() did
    // not. Treat as "selects nothing" for consistency.
    if (!lex_.peek().ok) return nullptr;
    return node;
  }

  // Consumes and materializes one scalar-literal token (number/string/true/
  // false/null); the caller must gate on jpIsScalarLiteralTok(peek) first.
  // A malformed number/string returns nullptr after the token was consumed.
  // Never sets lastOperandFromQuery_ (a literal is never a query).
  const asvJSONValue* parseScalarLiteral() {
    Tok t = lex_.next();
    if (t.type == TokType::NUMBER) {
      if (!t.ok) return nullptr;
      if (t.val.find('.') != std::string::npos) {
        double d;
        if (!jpParseNumber(t.val, d)) return nullptr;
        return hold(asvJSONValue::makeDouble(d));
      }
      int64_t iv;
      if (jpParseInt64(t.val, iv)) return hold(asvJSONValue::makeInt(iv));
      double d;
      if (jpParseNumber(t.val, d)) return hold(asvJSONValue::makeDouble(d));
      return nullptr;
    }
    if (t.type == TokType::STRING) {
      if (!t.ok) return nullptr;
      if (!asvJSONValue::checkStringLen(t.val.size()))
        throw asvJSONError("JSONPath: string literal too long");
      return hold(asvJSONValue::makeString(t.val.data(), t.val.size()));
    }
    if (t.type == TokType::TRUE_LIT) return hold(asvJSONValue::makeBool(true));
    if (t.type == TokType::FALSE_LIT) return hold(asvJSONValue::makeBool(false));
    if (t.type == TokType::NULL_LIT) return hold(asvJSONValue::makeNull());
    return nullptr;
  }

  const asvJSONValue* parseFuncValue() {
    Tok t = lex_.peek();
    lastOperandFromQuery_ = false;

    if (jpIsScalarLiteralTok(t.type)) return parseScalarLiteral();

    if (t.type != TokType::IDENT) return nullptr;

    std::string fn = t.val;

    if (jpIsFuncName(fn)) {
      lex_.next();
      if (lex_.peek().type == TokType::LPAREN) lex_.next();

      if (fn == "match" || fn == "search") {
        // RFC 9535 2.4.6/2.4.7: both arguments are ValueType. The input may
        // be a string literal (match("literal", "pattern")) or a query path;
        // per the RFC a non-string first argument yields LogicalFalse.
        const asvJSONValue* strNode = nullptr;
        Tok ft = lex_.peek();
        if (ft.type == TokType::STRING) {
          if (!ft.ok) return hold(asvJSONValue::makeBool(false));
          lex_.next();
          if (!asvJSONValue::checkStringLen(ft.val.size()))
            throw asvJSONError("JSONPath: string literal too long");
          strNode = hold(asvJSONValue::makeString(ft.val.data(), ft.val.size()));
        } else {
          strNode = resolvePath();
        }
        if (lex_.peek().type == TokType::COMMA) lex_.next();
        Tok patTok = lex_.next();
        if (patTok.type != TokType::STRING || !patTok.ok) {
          return hold(asvJSONValue::makeBool(false));
        }
        if (lex_.peek().type == TokType::RPAREN) lex_.next();
        if (!strNode || strNode->type != asvJSONValue::STRING) {
          return hold(asvJSONValue::makeBool(false));
        }
        try {
          std::regex re(patTok.val);
          bool res = (fn == "search")
                       ? std::regex_search(strNode->str_data, re)
                       : std::regex_match(strNode->str_data, re);
          return hold(asvJSONValue::makeBool(res));
        } catch (...) {
          return hold(asvJSONValue::makeBool(false));
        }
      }

      if (fn == "value") {
        const asvJSONValue* v = resolvePath();
        if (lex_.peek().type == TokType::RPAREN) lex_.next();
        lastOperandFromQuery_ = true;
        return v;
      }

      const asvJSONValue* arg = nullptr;
      if (lex_.peek().type == TokType::RPAREN && jpIsZeroArgFunc(fn)) {
        // zero-arg form: the function applies to the current node
        arg = cur_;
      } else if (fn == "length" && jpIsScalarLiteralTok(lex_.peek().type)) {
        // RFC 9535 2.4.4: length's parameter is ValueType, so a scalar
        // literal is a well-typed argument (string -> code-point count,
        // any other scalar -> Nothing, i.e. nullptr below).
        arg = parseScalarLiteral();
      } else {
        arg = resolvePath();
      }
      if (lex_.peek().type == TokType::RPAREN) lex_.next();

      if (fn == "length") {
        int len = jpGetPropCount(arg);
        if (len < 0) return nullptr;
        return hold(asvJSONValue::makeInt(len));
      }

      if (fn == "count") {
        return hold(asvJSONValue::makeInt(arg ? 1 : 0));
      }

      if (fn == "min" || fn == "max" || fn == "sum" || fn == "avg") {
        if (!arg || arg->type != asvJSONValue::ARRAY || !arg->arr) return nullptr;
        if (arg->arr->empty()) return nullptr;
        const asvJSONValue* best = nullptr;
        bool allInt = true, haveNum = false;
        int64_t isum = 0;
        double acc = 0.0;
        size_t cnt = 0;
        for (auto& e : *arg->arr) {
          if (e->type != asvJSONValue::INT && e->type != asvJSONValue::DOUBLE) continue;
          haveNum = true;
          cnt++;
          if (fn == "min" || fn == "max") {
            if (!best) { best = e.get(); continue; }
            bool take = (fn == "min") ? (jpCompare(e.get(), best) < 0)
                                      : (jpCompare(e.get(), best) > 0);
            if (take) best = e.get();
            continue;
          }
          if (allInt && e->type == asvJSONValue::INT) {
            if ((e->num > 0 && isum > INT64_MAX - e->num) ||
                (e->num < 0 && isum < INT64_MIN - e->num)) {
              acc = static_cast<double>(isum) + static_cast<double>(e->num);
              allInt = false;
            } else {
              isum += e->num;
            }
          } else {
            if (allInt) { acc = static_cast<double>(isum); allInt = false; }
            acc += (e->type == asvJSONValue::INT) ? static_cast<double>(e->num) : e->dbl;
          }
        }
        if (!haveNum) return nullptr;
        if (fn == "min" || fn == "max") return best;
        if (fn == "avg") {
          if (allInt) return hold(asvJSONValue::makeDouble(static_cast<double>(isum) / static_cast<double>(cnt)));
          return hold(asvJSONValue::makeDouble(acc / static_cast<double>(cnt)));
        }
        if (allInt) return hold(asvJSONValue::makeInt(isum));
        return hold(asvJSONValue::makeDouble(acc));
      }
    }

    lastOperandFromQuery_ = true;
    return resolvePath();
  }

  bool parseComparison(bool* sawCmp = nullptr) {
    Tok t = lex_.peek();
    if (t.type == TokType::LPAREN) {
      lex_.next();
      // Depth-cap the '('...')' recursion so nested paren filters cannot
      // overflow the stack on hand-crafted inputs.
      if (filterDepth_ >= 64) return false;
      ++filterDepth_;
      FilterDepthGuard guard(filterDepth_);
      bool r = parseOr();
      if (lex_.peek().type == TokType::RPAREN) lex_.next();
      return r;
    }

    const asvJSONValue* lhs = parseFuncValue();
    Tok op = lex_.peek();
    if (op.type == TokType::LT || op.type == TokType::GT ||
        op.type == TokType::LE || op.type == TokType::GE ||
        op.type == TokType::EQ || op.type == TokType::NE) {
      lex_.next();
      if (sawCmp) *sawCmp = true;
      const asvJSONValue* rhs = parseFuncValue();
      return jpCmpForFilter(lhs, rhs, op.type);
    }
    // Bare operand: a query is an RFC existence test (true iff it selected a
    // node, regardless of the node's value); literals and function results
    // evaluate by their value.
    if (lhs == nullptr) return false;
    if (lastOperandFromQuery_) return true;
    return jpValueTruthy(lhs);
  }

public:
  JPFilterEval(PathLexer& lex, const asvJSONValue* cur, const asvJSONValue* root = nullptr)
    : lex_(lex), cur_(cur), root_(root) {}
  bool evaluate() { return parseOr(); }
};

struct PathResult {
  std::vector<const asvJSONValue*> values;
  std::vector<std::unique_ptr<asvJSONValue>> owned;
  std::string error;
  const asvJSONValue* root = nullptr;
};

inline void jpExpand(const asvJSONValue* node, PathLexer& lex, PathResult& res);

inline void jpExpandDot(Tok seg, PathLexer& lex, const asvJSONValue* node,
                         std::vector<const asvJSONValue*>& out) {
  if (seg.type == TokType::STAR) {
    auto children = jpAllChildren(node);
    out.insert(out.end(), children.begin(), children.end());
  } else if (seg.type == TokType::IDENT) {
    const asvJSONValue* child = jpGetProp(node, seg.val);
    if (child) out.push_back(child);
  }
}

inline void jpExpandBracket(PathLexer& lex, const asvJSONValue* node,
                             std::vector<const asvJSONValue*>& out, PathResult& res);

inline void jpExpandDotDot(Tok seg, PathLexer& lex, const asvJSONValue* node,
                            std::vector<const asvJSONValue*>& out, PathResult& res) {
  if (seg.type == TokType::STAR) {
    jpDescChildrenAll(node, out);
  } else if (seg.type == TokType::IDENT) {
    jpDescChildrenName(node, seg.val, out);
  } else if (seg.type == TokType::LBRACKET) {
    // lex is positioned right after '['; the child segment is applied to each
    // visited node (input included, per RFC 2.5.2.2).
    std::vector<const asvJSONValue*> visited;
    jpVisitAll(node, visited);
    for (auto* vn : visited) {
      PathLexer blex(lex);
      jpExpandBracket(blex, vn, out, res);
    }
  }
}

inline void jpExpandBracket(PathLexer& lex, const asvJSONValue* node,
                             std::vector<const asvJSONValue*>& out, PathResult& res) {
  bool closed = false;
  size_t before = out.size();
  while (true) {
    Tok inner = lex.peek();

    if (inner.type == TokType::STAR) {
      lex.next();
      auto children = jpAllChildren(node);
      out.insert(out.end(), children.begin(), children.end());
    }

    else if (inner.type == TokType::NUMBER) {
      lex.next();
      if (!inner.ok) { out.resize(before); return; }
      if (lex.peek().type == TokType::COLON) {
        // Slice with an explicit start.
        int64_t start, end, step;
        if (!jpParseIntStrict(inner.val, start)) { out.resize(before); return; }
        lex.next(); // consume ':'
        end = INT64_MAX;
        step = 1;
        bool hasEnd = false;
        Tok endTok = lex.peek();
        if (endTok.type == TokType::NUMBER) {
          if (!endTok.ok || !jpParseIntStrict(endTok.val, end)) { out.resize(before); return; }
          hasEnd = true;
          lex.next();
        }
        if (lex.peek().type == TokType::COLON) {
          lex.next();
          Tok stepTok = lex.peek();
          if (stepTok.type == TokType::NUMBER) {
            if (!stepTok.ok || !jpParseIntStrict(stepTok.val, step) || step == 0) { out.resize(before); return; }
            lex.next();
          }
        }
        int64_t len = (node && node->type == asvJSONValue::ARRAY && node->arr)
                        ? static_cast<int64_t>(node->arr->size()) : 0;
        if (!hasEnd) end = (step > 0) ? len : -len - 1;
        auto sliced = jpSliceAdd(node, start, end, step);
        out.insert(out.end(), sliced.begin(), sliced.end());
      }
      else if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        int64_t idx;
        if (!jpParseIntStrict(inner.val, idx)) { out.resize(before); return; }
        size_t ai;
        if (idx < 0) {
          size_t mag = static_cast<size_t>(-idx);
          if (mag <= node->arr->size()) {
            ai = node->arr->size() - mag;
            if (ai < node->arr->size()) out.push_back((*node->arr)[ai].get());
          }
        } else {
          ai = static_cast<size_t>(idx);
          if (ai < node->arr->size()) out.push_back((*node->arr)[ai].get());
        }
      }
    }

    else if (inner.type == TokType::STRING) {
      lex.next();
      if (!inner.ok) { out.resize(before); return; }
      const asvJSONValue* child = jpGetProp(node, inner.val);
      if (child) out.push_back(child);
    }

    else if (inner.type == TokType::COLON) {
      // Slice with no explicit start.
      lex.next();
      int64_t start = 0, end = INT64_MAX, step = 1;
      bool hasStart = false, hasEnd = false;
      Tok endTok = lex.peek();
      if (endTok.type == TokType::NUMBER) {
        if (!endTok.ok || !jpParseIntStrict(endTok.val, end)) { out.resize(before); return; }
        hasEnd = true;
        lex.next();
      }
      if (lex.peek().type == TokType::COLON) {
        lex.next();
        Tok stepTok = lex.peek();
        if (stepTok.type == TokType::NUMBER) {
          if (!stepTok.ok || !jpParseIntStrict(stepTok.val, step) || step == 0) { out.resize(before); return; }
          lex.next();
        }
      }
      int64_t len = (node && node->type == asvJSONValue::ARRAY && node->arr)
                      ? static_cast<int64_t>(node->arr->size()) : 0;
      if (!hasStart) start = (step > 0) ? 0 : len - 1;
      if (!hasEnd) end = (step > 0) ? len : -len - 1;
      auto sliced = jpSliceAdd(node, start, end, step);
      out.insert(out.end(), sliced.begin(), sliced.end());
    }

    else if (inner.type == TokType::QUESTION) {
      // The filter is one item of the comma-separated selector list (RFC 9535
      // bracketed-selection), so the lexer must STOP at the closing ']' or at
      // the ',' introducing the next selector -- not run past it (the old
      // skipToEndBracket() consumed both, silently dropping every selector
      // after a filter). The whole expression is then skipped with
       // skipToFilterEnd(), which is a standalone token-depth scan: it must not
       // rely on the filter evaluator's final position, because parseAnd/parseOr
       // evaluate ALL terms (no short-circuit) and the evaluator may leave a
       // trailing `&& term` unconsumed if it errors out partway through.
      // Do not skip an immediately following '(' either: that opening paren is
      // the start of a paren-expr and JPFilterEval::parseComparison's '('
      // branch consumes the whole balanced group, so `?(@.a) && (@.b)`
      // evaluates both terms. A malformed filter (garbage byte, unterminated
      // input) leaves the tail unskipped; the `!closed` rollback below then
      // empties this selector, and jpExpand's unclean-up still runs.
      lex.next();
      auto children = jpAllChildren(node);
      // Capture the filter end token from the copy lexer (not the outer one -
      // the outer lex is not advanced by the evaluator). Stray tokens after
      // the evaluated expression (e.g. `$[?@.a)]` with a trailing `)`) are
      // invalid; reject the whole selector to match isValidPath's stricter check.
      Tok filterEnd;
      bool filterEndSet = false;
      for (const asvJSONValue* child : children) {
        PathLexer filterLex(lex);
        JPFilterEval filterEval(filterLex, child, res.root);
        if (filterEval.evaluate()) out.push_back(child);
        filterEnd = filterLex.peek();
        filterEndSet = true;
      }
      if (filterEndSet && filterEnd.type != TokType::RBRACKET && filterEnd.type != TokType::COMMA) {
        closed = false; out.resize(before); return;
      }
      closed = lex.skipToFilterEnd();
    }

    else {
      out.resize(before); return; // invalid token mid-selector: discard partial results from earlier items
    }

    if (lex.peek().type == TokType::COMMA) { lex.next(); continue; }
    break;
  }

  if (lex.peek().type == TokType::RBRACKET) {
    lex.next();
    closed = true;
  }
  if (!closed) out.resize(before);
}

inline std::unique_ptr<asvJSONValue> jpApplyFunc(const std::string& fn, const asvJSONValue* arg) {
  if (fn == "length") {
    int len = jpGetPropCount(arg);
    if (len < 0) return nullptr;
    return asvJSONValue::makeInt(len);
  }
  if (fn == "count") {
    return asvJSONValue::makeInt(arg ? 1 : 0);
  }
  if (fn == "min" || fn == "max" || fn == "sum" || fn == "avg") {
    if (!arg || arg->type != asvJSONValue::ARRAY || !arg->arr) return nullptr;
    if (arg->arr->empty()) return nullptr;
    const asvJSONValue* best = nullptr;
    bool allInt = true, haveNum = false;
    int64_t isum = 0;
    double acc = 0.0;
    size_t cnt = 0;
    for (auto& e : *arg->arr) {
      if (e->type != asvJSONValue::INT && e->type != asvJSONValue::DOUBLE) continue;
      haveNum = true;
      cnt++;
      if (fn == "min" || fn == "max") {
        if (!best) { best = e.get(); continue; }
        bool take = (fn == "min") ? (jpCompare(e.get(), best) < 0)
                                  : (jpCompare(e.get(), best) > 0);
        if (take) best = e.get();
        continue;
      }
      if (allInt && e->type == asvJSONValue::INT) {
        if ((e->num > 0 && isum > INT64_MAX - e->num) ||
            (e->num < 0 && isum < INT64_MIN - e->num)) {
          acc = static_cast<double>(isum) + static_cast<double>(e->num);
          allInt = false;
        } else {
          isum += e->num;
        }
      } else {
        if (allInt) { acc = static_cast<double>(isum); allInt = false; }
        acc += (e->type == asvJSONValue::INT) ? static_cast<double>(e->num) : e->dbl;
      }
    }
    if (!haveNum) return nullptr;
    if (fn == "min" || fn == "max") return cloneValue(best);
    if (fn == "avg") {
      if (allInt) return asvJSONValue::makeDouble(static_cast<double>(isum) / static_cast<double>(cnt));
      return asvJSONValue::makeDouble(acc / static_cast<double>(cnt));
    }
    if (allInt) return asvJSONValue::makeInt(isum);
    return asvJSONValue::makeDouble(acc);
  }
  if (fn == "value") {
    return arg ? cloneValue(arg) : nullptr;
  }
  if (fn == "match" || fn == "search") {
    // Registered names (jpIsFuncName) but never segment functions: RFC 9535
    // 2.4.6/2.4.7 give them two arguments, so a zero-arg segment call
    // ($.match()) has no meaning. jpExpand reaches this branch with the
    // parentheses already consumed; nullptr makes it an empty result while
    // isValidPath rejects the path outright.
    return nullptr;
  }
  return nullptr; // unknown function name (defensive; none registered here)
}

inline void jpExpand(const asvJSONValue* node, PathLexer& lex, PathResult& res) {
  std::vector<const asvJSONValue*> current;
  current.push_back(node);

  while (lex.hasMore()) {
    Tok t = lex.peek();

    if (t.type == TokType::DOT) {
      lex.next();
      Tok seg = lex.next();
      if (seg.type == TokType::IDENT && jpIsFuncName(seg.val) &&
          lex.peek().type == TokType::LPAREN) {
        lex.next();
        // Segment-level functions take no arguments (length(), count(), ...,
        // value()). A call with arguments was previously evaluated with the
        // arguments silently dropped; query() is lenient so reject it as an
        // empty result instead. match()/search() are registered names but are
        // not segment functions at all (they take two arguments, RFC 9535
        // 2.4.6/2.4.7): jpApplyFunc returns nullptr for them, so $.match()
        // is an empty result here while isValidPath rejects the path.
        if (lex.peek().type != TokType::RPAREN) {
          current.clear();
          break;
        }
        lex.next();
        std::vector<const asvJSONValue*> next;
        for (auto* n : current) {
          auto r = jpApplyFunc(seg.val, n);
          if (r) { res.owned.push_back(std::move(r)); next.push_back(res.owned.back().get()); }
        }
        current = std::move(next);
      } else {
        std::vector<const asvJSONValue*> next;
        for (auto* n : current) jpExpandDot(seg, lex, n, next);
        current = std::move(next);
      }
    }
    else if (t.type == TokType::DOTDOT) {
      lex.next();
      Tok seg = lex.peek();
      std::vector<const asvJSONValue*> next;
      if (seg.type == TokType::LBRACKET) {
        lex.next(); // consume '['
        for (auto* n : current) jpExpandDotDot(seg, lex, n, next, res);
        lex.skipToEndBracket();
      } else {
        lex.next();
        for (auto* n : current) jpExpandDotDot(seg, lex, n, next, res);
      }
      current = std::move(next);
    }
    else if (t.type == TokType::LBRACKET) {
      lex.next();
      std::vector<const asvJSONValue*> next;
      for (auto* n : current) {
        PathLexer blex(lex);
        jpExpandBracket(blex, n, next, res);
      }
      lex.skipToEndBracket();
      current = std::move(next);
    }
    else {
      // The only token that legally ends a path is a real END (ok=true: the
      // input ran out). A character the lexer could not turn into a token
      // lexes to END with ok=false; treat that as a malformed path that
      // selects nothing instead of silently stopping here.
      if (!t.ok) current.clear();
      break;
    }
  }

  res.values = std::move(current);
}

inline PathResult jpQuery(const asvJSONValue* root, std::string_view path) {
  PathResult res;
  if (!root) { res.error = "null root"; return res; }

  PathLexer lex(path);
  Tok first = lex.next();

  if (first.type == TokType::END) {
    // A real end of input ("" or whitespace-only) is the empty query, which
    // selects the root. A lexer fall-through also reports END but with
    // ok=false; a path whose first character is not lexable ("+x") is an
    // error here, not a request for the root.
    if (first.ok) res.values.push_back(root);
    else res.error = "path must start with $";
    return res;
  }

  if (first.type != TokType::IDENT || first.val != "$") {
    res.error = "path must start with $";
    return res;
  }

  res.root = root;
  jpExpand(root, lex, res);
  return res;
}

// ---------------------------------------------------------------------------
// Strict path validation (isValidPath). Fully consumes the token stream and
// enforces well-formedness (grammar, balanced parens/brackets) plus the semantic
// requirements of RFC 9535 (I-JSON range for indices/steps, step != 0, valid
// filter/function-argument syntax).
// ---------------------------------------------------------------------------

inline bool jpValidateQueryPath(PathLexer& lex) {
  Tok t = lex.next();
  if (t.type != TokType::IDENT || (t.val != "@" && t.val != "$")) return false;
  while (true) {
    Tok p = lex.peek();
    if (p.type == TokType::DOT) {
      lex.next();
      Tok seg = lex.peek();
      if (seg.type == TokType::STAR) { lex.next(); continue; }
      if (seg.type != TokType::IDENT) return false;
      lex.next();
      if (jpIsZeroArgFunc(seg.val) && lex.peek().type == TokType::LPAREN) {
        // A function used as a path segment is zero-arg: jpExpand applies it to
        // the current node and so does resolvePath inside a filter. A call with
        // an argument is not a path; previously the '(' was consumed, an
        // argument was validated and the closing ')' was left in the stream for
        // the caller to trip over. Reject it outright instead, matching query(),
        // which yields no result for such a path.
        lex.next();
        if (lex.peek().type != TokType::RPAREN) return false;
        lex.next();
      }
    } else if (p.type == TokType::LBRACKET) {
      lex.next();
      Tok idx = lex.peek();
      if (idx.type == TokType::STRING) {
        if (!idx.ok) return false;
        lex.next();
      } else if (idx.type == TokType::NUMBER) {
        int64_t v;
        if (!idx.ok || !jpParseIntStrict(idx.val, v)) return false;
        lex.next();
      } else {
        return false;
      }
      if (lex.peek().type != TokType::RBRACKET) return false;
      lex.next();
    } else {
      break;
    }
  }
  // The token that ended the path loop must be a real boundary, not a
  // lexer fall-through: a garbage byte next to a query path (`@.a{`) would
  // otherwise be silently treated as end-of-path. Recursive descent (DOTDOT)
  // is also rejected here - it is valid at the top level but not inside a
  // filter query-path, and self-consistency prevents future refactors from
  // accidentally relying on callers to catch it.
  if (lex.peek().type == TokType::DOTDOT) return false;
  if (!lex.peek().ok) return false;
  return true;
}

// Consumes one scalar-literal token for a ValueType function argument; false
// when the token is not a literal or is malformed (a NUMBER like "-01" fails
// the 2.3.5.1 number grammar).
inline bool jpValidateValueLiteral(PathLexer& lex) {
  Tok t = lex.peek();
  if (!jpIsScalarLiteralTok(t.type)) return false;
  if (!t.ok) return false;
  if (t.type == TokType::NUMBER) {
    double d;
    if (!jpParseNumber(t.val, d)) return false;
  }
  lex.next();
  return true;
}

// A ValueType function argument per RFC 9535 2.4.3: any value literal or a
// singular query path -- no static type check is required beyond the syntax.
inline bool jpValidateValueTypeArg(PathLexer& lex) {
  if (jpIsScalarLiteralTok(lex.peek().type)) return jpValidateValueLiteral(lex);
  return jpValidateQueryPath(lex);
}

inline bool jpValidateOperand(PathLexer& lex);
inline bool jpValidateNotExpr(PathLexer& lex);
inline bool jpValidateAndExpr(PathLexer& lex);
inline bool jpValidateExpr(PathLexer& lex);

// A filter operand: a query path, a literal, or a well-typed function call.
inline bool jpValidateOperand(PathLexer& lex) {
  Tok t = lex.peek();
  if (t.type == TokType::STRING) {
    if (!t.ok) return false;
    lex.next();
    return true;
  }
  if (t.type == TokType::TRUE_LIT || t.type == TokType::FALSE_LIT ||
      t.type == TokType::NULL_LIT) {
    lex.next();
    return true;
  }
  if (t.type == TokType::NUMBER) {
    double d;
    if (!jpParseNumber(t.val, d)) return false;
    lex.next();
    return true;
  }
  if (t.type != TokType::IDENT) return false;
  if (t.val == "@" || t.val == "$") return jpValidateQueryPath(lex);

  // Function call: name '(' [args] ')'.
  lex.next();
  if (lex.peek().type != TokType::LPAREN) return false;
  lex.next();
  if (!jpIsFuncName(t.val)) return false;
  if (t.val == "match" || t.val == "search") {
    // RFC 9535 2.4.6/2.4.7 give both parameters the declared type ValueType,
    // and 2.4.3 makes a ValueType parameter well-typed with any value literal
    // or a singular query: there is no type check to perform (a non-string
    // input or pattern evaluates to LogicalFalse, which is what the evaluator
    // does, so match(5, "pat") and match("abc", 5) are well-typed and both
    // false). The pattern stays a literal only -- a query pattern is not
    // implemented by the evaluator, which would read it as a non-string, so it
    // remains rejected here to keep validator and evaluator in agreement.
    if (!jpValidateValueTypeArg(lex)) return false;
    if (lex.peek().type != TokType::COMMA) return false;
    lex.next();
    if (!jpValidateValueLiteral(lex)) return false;
  } else if (t.val == "length" && lex.peek().type != TokType::RPAREN) {
    // RFC 9535 2.4.4: length's parameter is ValueType, so the argument may be
    // any scalar literal (length("abc") -> 3) or a query. The zero-arg
    // current-node form falls through to the shared branch below.
    if (!jpValidateValueTypeArg(lex)) return false;
  } else {
    if (jpIsZeroArgFunc(t.val) && lex.peek().type == TokType::RPAREN) {
      // RFC 9535 2.4.8 gives value() exactly one NodesType argument; its
      // zero-arg identity form is a path-segment extension only ($.x.value()),
      // never a filter operand, so value() here must keep its query argument.
      if (t.val == "value") return false;
      // other zero-arg functions (length(), count(), ...): no argument
    } else {
      if (!jpValidateQueryPath(lex)) return false;
    }
  }
  if (lex.peek().type != TokType::RPAREN) return false;
  lex.next();
  return true;
}

inline bool jpValidateNotExpr(PathLexer& lex) {
  // Consume the NOT prefix iteratively so `!!!!...` cannot overflow the stack.
  // RFC 9535 allows exactly one optional logical-not-op per basic-expr and has
  // no recursive not-production, so a second '!' is malformed: `!!@.a` is
  // invalid, `!(!@.a)` is not.
  bool hasNot = false;
  while (lex.peek().type == TokType::NOT) {
    lex.next();
    if (hasNot) return false;
    hasNot = true;
  }
  if (lex.peek().type == TokType::LPAREN) {
    // paren-expr = [logical-not-op S] "(" S logical-expr S ")"
    lex.next();
    if (!jpValidateExpr(lex)) return false;
    if (lex.peek().type != TokType::RPAREN) return false;
    lex.next();
    return true;
  }
  if (hasNot) {
    // test-expr = [logical-not-op S] (filter-query / function-expr). RFC 9535
    // allows logical-not-op before *both* basic-expr forms, so `!@.a`,
    // `!length(@.a)` and `!@.a.b` are all valid -- the earlier version required
    // a '(' after every `!` and wrongly rejected all of them. No comparison-op
    // may follow, because a test-expr is not a comparable.
    return jpValidateOperand(lex);
  }
  if (!jpValidateOperand(lex)) return false;
  Tok op = lex.peek();
  if (op.type == TokType::LT || op.type == TokType::GT ||
      op.type == TokType::LE || op.type == TokType::GE ||
      op.type == TokType::EQ || op.type == TokType::NE) {
    lex.next();
    return jpValidateOperand(lex);
  }
  return true;
}

inline bool jpValidateAndExpr(PathLexer& lex) {
  if (!jpValidateNotExpr(lex)) return false;
  while (lex.peek().type == TokType::AND) {
    lex.next();
    if (!jpValidateNotExpr(lex)) return false;
  }
  return true;
}

inline bool jpValidateExpr(PathLexer& lex) {
  // Paren nesting revisits this function recursively; cap the depth so deeply
  // nested '('...')' filters cannot overflow the stack.
  static thread_local int jpValidateExprDepth = 0;
  if (jpValidateExprDepth >= 64) return false;
  struct JpValidateGuard {
    int& d;
    explicit JpValidateGuard(int& depth) : d(depth) { ++d; }
    ~JpValidateGuard() { --d; }
  } jpValidateGuard(jpValidateExprDepth);
  if (!jpValidateAndExpr(lex)) return false;
  while (lex.peek().type == TokType::OR) {
    lex.next();
    if (!jpValidateAndExpr(lex)) return false;
  }
  return true;
}

// Validates the content of a bracketed-selection: one or more comma-separated
// selectors (name/wildcard/index/slice/filter). Caller has consumed '['; the
// closing ']' is not consumed.
inline bool jpValidateBracketContent(PathLexer& lex) {
  while (true) {
    Tok inner = lex.peek();
    if (inner.type == TokType::STAR) {
      lex.next();
    } else if (inner.type == TokType::STRING) {
      if (!inner.ok) return false;
      lex.next();
    } else if (inner.type == TokType::QUESTION) {
      // filter-selector = "?" S logical-expr -- RFC 9535 makes the parentheses
      // optional; they are merely the paren-expr form of logical-expr, so
      // `$[?@.a == 5]`, `$[?(@.a == 5)]` and `$[?(@.a) && (@.b)]` are the same
      // grammar. jpValidateNotExpr consumes the matching ')' of a paren-expr,
      // so there is no wrapper to check for here; the query evaluator parses
      // the optional paren-expr through JPFilterEval::parseComparison's '('
      // branch, keeping both paths in agreement.
      lex.next();
      if (!jpValidateExpr(lex)) return false;
    } else if (inner.type == TokType::NUMBER) {
      lex.next();
      if (!inner.ok) return false;
      if (lex.peek().type == TokType::COLON) {
        // Slice with explicit start: start ':' [end] [':' [step]]
        int64_t v;
        if (!jpParseIntStrict(inner.val, v)) return false;
        lex.next(); // ':'
        if (lex.peek().type == TokType::NUMBER) {
          if (!lex.peek().ok || !jpParseIntStrict(lex.peek().val, v)) return false;
          lex.next();
        } else if (lex.peek().type != TokType::COLON &&
                   lex.peek().type != TokType::RBRACKET &&
                   lex.peek().type != TokType::COMMA) {
          return false;
        }
        if (lex.peek().type == TokType::COLON) {
          lex.next();
          if (lex.peek().type == TokType::NUMBER) {
            if (!lex.peek().ok || !jpParseIntStrict(lex.peek().val, v) || v == 0) return false;
            lex.next();
          } else if (lex.peek().type != TokType::RBRACKET &&
                     lex.peek().type != TokType::COMMA) {
            return false;
          }
        }
      } else {
        // Index selector.
        int64_t v;
        if (!jpParseIntStrict(inner.val, v)) return false;
      }
    } else if (inner.type == TokType::COLON) {
      // Slice without start: ':' [end] [':' [step]]
      lex.next();
      if (lex.peek().type == TokType::NUMBER) {
        int64_t v;
        if (!lex.peek().ok || !jpParseIntStrict(lex.peek().val, v)) return false;
        lex.next();
      } else if (lex.peek().type != TokType::COLON &&
                 lex.peek().type != TokType::RBRACKET &&
                 lex.peek().type != TokType::COMMA) {
        return false;
      }
      if (lex.peek().type == TokType::COLON) {
        lex.next();
        if (lex.peek().type == TokType::NUMBER) {
          int64_t v;
          if (!lex.peek().ok || !jpParseIntStrict(lex.peek().val, v) || v == 0) return false;
          lex.next();
        } else if (lex.peek().type != TokType::RBRACKET &&
                   lex.peek().type != TokType::COMMA) {
          return false;
        }
      }
    } else {
      return false;
    }
    if (lex.peek().type == TokType::COMMA) { lex.next(); continue; }
    break;
  }
  return lex.peek().type == TokType::RBRACKET;
}

inline bool jpIsValid(std::string_view path) {
  PathLexer lex(path);
  Tok first = lex.next();
  if (!first.ok || first.type != TokType::IDENT || first.val != "$") return false;
  while (lex.hasMore()) {
    Tok t = lex.peek();
    if (t.type == TokType::DOT) {
      lex.next();
      Tok seg = lex.next();
      if (!seg.ok) return false;
      if (seg.type != TokType::IDENT && seg.type != TokType::STAR) return false;
      if (seg.type == TokType::IDENT && jpIsZeroArgFunc(seg.val) &&
          lex.peek().type == TokType::LPAREN) {
        // Zero-arg only, as above: the closing ')' has to be consumed here, and
        // an argument makes the whole path invalid (jpExpand returns nothing).
        // match()/search() are deliberately absent from this check -- they take
        // two arguments (RFC 9535 2.4.6/2.4.7) and are never segments, so the
        // '(' below falls into the generic path and rejects `$.match()`.
        lex.next(); // consume '('
        if (lex.peek().type != TokType::RPAREN) return false;
        lex.next();
      }
    } else if (t.type == TokType::DOTDOT) {
      lex.next();
      Tok seg = lex.peek();
      if (seg.type == TokType::LBRACKET) {
        lex.next(); // consume '['
        if (!jpValidateBracketContent(lex)) return false;
        if (lex.peek().type != TokType::RBRACKET) return false;
        lex.next();
      } else {
        lex.next();
        if (!seg.ok) return false;
        if (seg.type != TokType::IDENT && seg.type != TokType::STAR) return false;
      }
    } else if (t.type == TokType::LBRACKET) {
      lex.next(); // consume '['
      if (!jpValidateBracketContent(lex)) return false;
      if (lex.peek().type != TokType::RBRACKET) return false;
      lex.next();
    } else {
      return false;
    }
  }
  return true;
}

inline std::vector<const asvJSONValue*> asvJSON::query(std::string_view path) {
  auto r = jpQuery(root.get(), path);
  pathOwned = std::move(r.owned);
  return std::move(r.values);
}

inline const asvJSONValue* asvJSON::queryFirst(std::string_view path) {
  auto r = jpQuery(root.get(), path);
  pathOwned = std::move(r.owned);
  return r.values.empty() ? nullptr : r.values.front();
}

inline bool asvJSON::isValidPath(std::string_view path) {
  return jpIsValid(path);
}

} // namespace asvJSONInternal
