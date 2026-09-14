#pragma once
// JSONPath (RFC 9535) implementation for asvJSON++
// Supports:
//   $ (root), .member, ['member'], [index], [start:end:step] (slice),
//   ..member / ..* / ..[selectors] (recursive descent), [*] (wildcard), .*,
//   [?(filter)] with operators: < > <= >= == != && || !
//   functions: length(), count(), min(), max(), sum(), match(), search(), value()
//
// RFC compliance notes:
//   - Numeric selectors/bounds/indices are validated per the RFC int grammar
//     (no leading zeros) and must lie in the I-JSON exact range
//     [-(2^53)+1, (2^53)-1]; out-of-range or malformed numbers select nothing
//     at query time and reject the path in isValidPath.
//   - Filter comparisons are exact for int64 operands; an empty/nothing operand
//     yields false for every operator except == where both sides must be empty.
//   - A bare query in a filter is an existence test: true iff it selects at
//     least one node; the selected node's value is irrelevant (null/false/{}).
//   - String literals follow the RFC escape rules, including \uXXXX escapes
//     and surrogate pairs.
//   - Slice semantics follow the RFC Normalize()/Bounds() algorithm exactly.

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
  if (v > 9007199254740991LL || v < -9007199254740991LL) return false;
  out = static_cast<int64_t>(v);
  return true;
}

// Lenient int64 parse for filter numeric literals: RFC int grammar but only
// the int64 range is enforced (no I-JSON clamp), so exact int64 comparisons in
// filters keep their precision. Never throws.
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

// Lenient decimal literal parse for filter operands: optional '-', digits, at
// most one '.'. Never throws.
inline bool jpParseNumber(const std::string& s, double& out) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '-') {
    if (s.size() == 1) return false;
    i = 1;
  }
  bool digit = false, dot = false;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c >= '0' && c <= '9') { digit = true; continue; }
    if (c == '.') {
      if (dot) return false;
      dot = true;
      continue;
    }
    return false;
  }
  if (!digit) return false;
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
    pos_++;
    return {TokType::END, ""};
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
  void skipToEndBracket() {
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
        if (depth == 0) { pos_++; return; }
        depth--; pos_++; continue;
      }
      pos_++;
    }
  }

  // Like skipToEndBracket but additionally enforces balanced parentheses in
  // the skipped region (used by the strict path validator for filter
  // selectors).
  bool skipBracketBalanced() {
    int depth = 0, paren = 0;
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
      if (c == '(') { paren++; pos_++; continue; }
      if (c == ')') {
        if (paren == 0) return false;
        paren--; pos_++; continue;
      }
      if (c == ']') {
        if (depth == 0) {
          if (paren != 0) return false;
          pos_++;
          return true;
        }
        depth--; pos_++; continue;
      }
      pos_++;
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

inline int jpGetPropCount(const asvJSONValue* n) {
  if (!n) return 0;
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
// are handled as existence tests, not via this function.
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

inline std::unique_ptr<asvJSONValue> jpApplyFunc(const std::string& fn, const asvJSONValue* arg);

class JPFilterEval {
  PathLexer& lex_;
  const asvJSONValue* cur_;
  const asvJSONValue* root_;
  std::vector<std::unique_ptr<asvJSONValue>> temp_vals_;
  bool lastOperandFromQuery_ = false;
  int filterDepth_ = 0;

  const asvJSONValue* hold(std::unique_ptr<asvJSONValue> val) {
    temp_vals_.push_back(std::move(val));
    return temp_vals_.back().get();
  }

  struct BoolVal { bool val; };

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
    // Iterative: a chain of NOT prefixes just toggles the negation of the
    // following comparison. No recursion, so `!!!!...` cannot overflow the
    // stack regardless of length.
    bool negate = false;
    while (lex_.peek().type == TokType::NOT) {
      lex_.next();
      negate = !negate;
    }
    bool r = parseComparison();
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
            break;
          }
        } else if (jpIsZeroArgFunc(p.val) && lex_.peek().type == TokType::LPAREN) {
          lex_.next();
          if (lex_.peek().type == TokType::RPAREN) lex_.next();
          auto r = jpApplyFunc(p.val, node);
          if (r) { temp_vals_.push_back(std::move(r)); node = temp_vals_.back().get(); }
          else node = nullptr;
        } else {
          node = jpGetProp(node, p.val);
          if (!node) node = nullptr;
        }
      } else if (lex_.peek().type == TokType::LBRACKET) {
        lex_.next();
        Tok idx = lex_.next();
        if (idx.type == TokType::NUMBER && node &&
            node->type == asvJSONValue::ARRAY && node->arr) {
          int64_t i;
          if (!idx.ok || !jpParseIntStrict(idx.val, i)) {
            if (lex_.peek().type == TokType::RBRACKET) lex_.next();
            node = nullptr;
          } else {
            size_t ai;
            if (i < 0) {
              size_t mag = static_cast<size_t>(-i);
              if (mag > node->arr->size()) {
                if (lex_.peek().type == TokType::RBRACKET) lex_.next();
                node = nullptr;
              } else {
                ai = node->arr->size() - mag;
                node = (ai < node->arr->size()) ? (*node->arr)[ai].get() : nullptr;
              }
            } else {
              ai = static_cast<size_t>(i);
              node = (ai < node->arr->size()) ? (*node->arr)[ai].get() : nullptr;
            }
          }
        } else if (idx.type == TokType::STRING && node &&
                   node->type == asvJSONValue::OBJECT && node->obj) {
          if (!idx.ok) {
            if (lex_.peek().type == TokType::RBRACKET) lex_.next();
            node = nullptr;
          } else {
            auto it = node->obj->find(idx.val);
            node = (it != node->obj->end()) ? it->second.get() : nullptr;
          }
        } else if (idx.type == TokType::STAR && node) {
          // wildcard: the node is selected iff it has at least one child
          if (jpAllChildren(node).empty()) node = nullptr;
        }
        if (lex_.peek().type == TokType::RBRACKET) lex_.next();
      } else {
        break;
      }
      if (!node) return nullptr;
    }
    return node;
  }

  const asvJSONValue* parseFuncValue() {
    Tok t = lex_.peek();
    lastOperandFromQuery_ = false;

    if (t.type == TokType::NUMBER) {
      lex_.next();
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
      lex_.next();
      if (!t.ok) return nullptr;
      return hold(asvJSONValue::makeString(t.val.data(), t.val.size()));
    }

    if (t.type == TokType::TRUE_LIT) {
      lex_.next();
      return hold(asvJSONValue::makeBool(true));
    }

    if (t.type == TokType::FALSE_LIT) {
      lex_.next();
      return hold(asvJSONValue::makeBool(false));
    }

    if (t.type == TokType::NULL_LIT) {
      lex_.next();
      return hold(asvJSONValue::makeNull());
    }

    if (t.type != TokType::IDENT) return nullptr;

    std::string fn = t.val;

    if (jpIsFuncName(fn)) {
      lex_.next();
      if (lex_.peek().type == TokType::LPAREN) lex_.next();

      if (fn == "match" || fn == "search") {
        const asvJSONValue* strNode = resolvePath();
        if (lex_.peek().type == TokType::COMMA) lex_.next();
        Tok patTok = lex_.next();
        std::string pat = patTok.val;
        if (lex_.peek().type == TokType::RPAREN) lex_.next();
        if (!strNode || strNode->type != asvJSONValue::STRING) {
          return hold(asvJSONValue::makeBool(false));
        }
        try {
          std::regex re(pat);
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

  bool parseComparison() {
    Tok t = lex_.peek();
    if (t.type == TokType::LPAREN) {
      lex_.next();
      // Depth-cap the '('...')' recursion so nested paren filters cannot
      // overflow the stack on hand-crafted inputs.
      if (filterDepth_ >= 64) return false;
      ++filterDepth_;
      bool r = parseOr();
      --filterDepth_;
      if (lex_.peek().type == TokType::RPAREN) lex_.next();
      return r;
    }

    const asvJSONValue* lhs = parseFuncValue();
    Tok op = lex_.peek();
    if (op.type == TokType::LT || op.type == TokType::GT ||
        op.type == TokType::LE || op.type == TokType::GE ||
        op.type == TokType::EQ || op.type == TokType::NE) {
      lex_.next();
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
  while (true) {
    Tok inner = lex.peek();

    if (inner.type == TokType::STAR) {
      lex.next();
      auto children = jpAllChildren(node);
      out.insert(out.end(), children.begin(), children.end());
    }

    else if (inner.type == TokType::NUMBER) {
      lex.next();
      if (!inner.ok) return;
      if (lex.peek().type == TokType::COLON) {
        // Slice with an explicit start.
        int64_t start, end, step;
        if (!jpParseIntStrict(inner.val, start)) return;
        lex.next(); // consume ':'
        end = INT64_MAX;
        step = 1;
        bool hasEnd = false;
        Tok endTok = lex.peek();
        if (endTok.type == TokType::NUMBER) {
          if (!endTok.ok || !jpParseIntStrict(endTok.val, end)) return;
          hasEnd = true;
          lex.next();
        }
        if (lex.peek().type == TokType::COLON) {
          lex.next();
          Tok stepTok = lex.peek();
          if (stepTok.type == TokType::NUMBER) {
            if (!stepTok.ok || !jpParseIntStrict(stepTok.val, step) || step == 0) return;
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
        if (!jpParseIntStrict(inner.val, idx)) return;
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
      if (!inner.ok) return;
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
        if (!endTok.ok || !jpParseIntStrict(endTok.val, end)) return;
        hasEnd = true;
        lex.next();
      }
      if (lex.peek().type == TokType::COLON) {
        lex.next();
        Tok stepTok = lex.peek();
        if (stepTok.type == TokType::NUMBER) {
          if (!stepTok.ok || !jpParseIntStrict(stepTok.val, step) || step == 0) return;
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
      lex.next();
      if (lex.peek().type == TokType::LPAREN) lex.next();

      auto children = jpAllChildren(node);
      for (const asvJSONValue* child : children) {
        PathLexer filterLex(lex);
        JPFilterEval filterEval(filterLex, child, res.root);
        if (filterEval.evaluate()) out.push_back(child);
      }

      lex.skipToEndBracket();
    }

    else {
      return;
    }

    if (lex.peek().type == TokType::COMMA) { lex.next(); continue; }
    break;
  }

  if (lex.peek().type == TokType::RBRACKET) lex.next();
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
  return nullptr;
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
        if (lex.peek().type == TokType::RPAREN) lex.next();
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
    res.values.push_back(root);
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
        lex.next();
        if (lex.peek().type == TokType::RPAREN) lex.next();
        else if (!jpValidateQueryPath(lex)) return false;
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
  return true;
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
    if (!jpValidateQueryPath(lex)) return false;
    if (lex.peek().type != TokType::COMMA) return false;
    lex.next();
    Tok s = lex.peek();
    if (s.type != TokType::STRING || !s.ok) return false;
    lex.next();
  } else {
    if (jpIsZeroArgFunc(t.val) && lex.peek().type == TokType::RPAREN) {
      // zero-arg function (length(), count(), ...): no argument
    } else {
      if (!jpValidateQueryPath(lex)) return false;
    }
  }
  if (lex.peek().type != TokType::RPAREN) return false;
  lex.next();
  return true;
}

inline bool jpValidateNotExpr(PathLexer& lex) {
  // Consume a run of NOT prefixes iteratively so `!!!!...` cannot overflow the stack.
  while (lex.peek().type == TokType::NOT) lex.next();
  if (lex.peek().type == TokType::LPAREN) {
    lex.next();
    if (!jpValidateExpr(lex)) return false;
    if (lex.peek().type != TokType::RPAREN) return false;
    lex.next();
    return true;
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
      lex.next();
      if (lex.peek().type != TokType::LPAREN) return false;
      lex.next();
      if (!jpValidateExpr(lex)) return false;
      if (lex.peek().type != TokType::RPAREN) return false;
      lex.next();
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
      if (seg.type == TokType::IDENT && jpIsFuncName(seg.val) &&
          lex.peek().type == TokType::LPAREN) {
        lex.next(); // consume '('
        if (lex.peek().type == TokType::RPAREN) lex.next();
        else if (!jpValidateQueryPath(lex)) return false;
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
