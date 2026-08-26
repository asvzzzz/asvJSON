#pragma once
// JSONPath (RFC 9535) implementation for asvJSON++
// Supports:
//   $ (root), .member, ['member'], [index], [start:end:step] (slice),
//   ..member (recursive descent), [*] (wildcard), .*, [?(filter)],
//   filter operators: < > <= >= == != && || !
//   functions: length(), count(), min(), max(), sum(), match(), search(), value()

#include "../core.hpp"
#include <regex>
#include <algorithm>
#include <functional>

namespace asvJSONInternal {

enum class TokType {
  END, DOT, DOTDOT, LBRACKET, RBRACKET, STAR, COLON, QUESTION,
  LPAREN, RPAREN, COMMA, LT, GT, LE, GE, EQ, NE, AND, OR, NOT,
  IDENT, NUMBER, STRING, TRUE_LIT, FALSE_LIT, NULL_LIT
};

struct Tok {
  TokType type;
  std::string val;
};

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
      char q = c; pos_++;
      std::string s;
      while (pos_ < src.size() && src[pos_] != q) {
        if (src[pos_] == '\\' && pos_ + 1 < src.size()) { pos_++; s += src[pos_]; }
        else { s += src[pos_]; }
        pos_++;
      }
      if (pos_ < src.size()) pos_++;
      return {TokType::STRING, s};
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

private:
  void skipWS() { while (pos_ < src.size() && src[pos_] == ' ') pos_++; }
};

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

inline void jpCollectDesc(const asvJSONValue* n, const std::string& name,
                           std::vector<const asvJSONValue*>& r) {
  if (!n) return;
  if (n->type == asvJSONValue::OBJECT && n->obj) {
    auto it = n->obj->find(name);
    if (it != n->obj->end()) r.push_back(it->second.get());
    for (auto& kv : *n->obj) jpCollectDesc(kv.second.get(), name, r);
  } else if (n->type == asvJSONValue::ARRAY && n->arr) {
    for (auto& e : *n->arr) jpCollectDesc(e.get(), name, r);
  }
}

inline void jpCollectAllDesc(const asvJSONValue* n, std::vector<const asvJSONValue*>& r) {
  if (!n) return;
  r.push_back(n);
  if (n->type == asvJSONValue::OBJECT && n->obj) {
    for (auto& kv : *n->obj) jpCollectAllDesc(kv.second.get(), r);
  } else if (n->type == asvJSONValue::ARRAY && n->arr) {
    for (auto& e : *n->arr) jpCollectAllDesc(e.get(), r);
  }
}

inline std::vector<const asvJSONValue*> jpSlice(const asvJSONValue* n,
                                                   int64_t start, int64_t end, int64_t step) {
  std::vector<const asvJSONValue*> r;
  if (!n || n->type != asvJSONValue::ARRAY || !n->arr || step == 0) return r;
  int64_t sz = static_cast<int64_t>(n->arr->size());
  if (sz == 0) return r;

  if (step > 0) {
    for (int64_t i = start; i < end; i += step)
      if (i >= 0 && i < sz) r.push_back((*n->arr)[static_cast<size_t>(i)].get());
  } else {
    for (int64_t i = start; i > end; i += step)
      if (i >= 0 && i < sz) r.push_back((*n->arr)[static_cast<size_t>(i)].get());
  }
  return r;
}

inline int jpCompare(const asvJSONValue* a, const asvJSONValue* b) {
  if (!a && !b) return 0;
  if (!a) return -1;
  if (!b) return 1;

  bool aNum = (a->type == asvJSONValue::INT || a->type == asvJSONValue::DOUBLE);
  bool bNum = (b->type == asvJSONValue::INT || b->type == asvJSONValue::DOUBLE);
  if (aNum && bNum) {
    double da = (a->type == asvJSONValue::INT) ? static_cast<double>(a->num) : a->dbl;
    double db = (b->type == asvJSONValue::INT) ? static_cast<double>(b->num) : b->dbl;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
  }
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

inline bool jpIsTruthy(const asvJSONValue* v) {
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
  std::vector<std::unique_ptr<asvJSONValue>> temp_vals_;

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
    if (lex_.peek().type == TokType::NOT) {
      lex_.next();
      return !parseNot();
    }
    return parseComparison();
  }

  const asvJSONValue* resolvePath() {
    Tok t = lex_.next();
    if (t.type != TokType::IDENT || t.val != "@") return nullptr;

    const asvJSONValue* node = cur_;
    while (lex_.peek().type == TokType::DOT) {
      lex_.next();
      Tok p = lex_.next();
      if (p.type != TokType::IDENT) break;
      if (jpIsZeroArgFunc(p.val) && lex_.peek().type == TokType::LPAREN) {
        lex_.next();
        if (lex_.peek().type == TokType::RPAREN) lex_.next();
        auto r = jpApplyFunc(p.val, node);
        if (r) { temp_vals_.push_back(std::move(r)); node = temp_vals_.back().get(); }
        else return nullptr;
      } else {
        node = jpGetProp(node, p.val);
        if (!node) return nullptr;
      }
    }
    while (lex_.peek().type == TokType::LBRACKET) {
      lex_.next();
      Tok idx = lex_.next();
      if (idx.type == TokType::NUMBER && node &&
          node->type == asvJSONValue::ARRAY && node->arr) {
        int64_t i = std::stoll(idx.val);
        size_t ai;
        if (i < 0) {
          if (static_cast<size_t>(-i) > node->arr->size()) { if (lex_.peek().type == TokType::RBRACKET) lex_.next(); continue; }
          ai = node->arr->size() - static_cast<size_t>(-i);
        } else {
          ai = static_cast<size_t>(i);
        }
        node = (ai < node->arr->size()) ? (*node->arr)[ai].get() : nullptr;
      } else if (idx.type == TokType::STRING && node &&
                 node->type == asvJSONValue::OBJECT && node->obj) {
        auto it = node->obj->find(idx.val);
        node = (it != node->obj->end()) ? it->second.get() : nullptr;
      }
      if (lex_.peek().type == TokType::RBRACKET) lex_.next();
    }
    return node;
  }

  const asvJSONValue* parseFuncValue() {
    Tok t = lex_.peek();

    if (t.type == TokType::NUMBER) {
      lex_.next();
      if (t.val.find('.') != std::string::npos) {
        return hold(asvJSONValue::makeDouble(std::stod(t.val)));
      }
      return hold(asvJSONValue::makeInt(std::stoll(t.val)));
    }

    if (t.type == TokType::STRING) {
      lex_.next();
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

    if (fn == "length" || fn == "count" || fn == "min" || fn == "max" ||
        fn == "sum" || fn == "avg" || fn == "match" || fn == "search" || fn == "value") {
      lex_.next();
      if (lex_.peek().type == TokType::LPAREN) lex_.next();

      if (fn == "match" || fn == "search") {
        const asvJSONValue* strNode = resolvePath();
        if (lex_.peek().type == TokType::COMMA) lex_.next();
        Tok patTok = lex_.next();
        std::string pat = (patTok.type == TokType::STRING) ? patTok.val : patTok.val;
        if (lex_.peek().type == TokType::RPAREN) lex_.next();
        if (!strNode || strNode->type != asvJSONValue::STRING) return nullptr;
        try {
          std::regex re(pat);
          bool res = std::regex_search(strNode->str_data, re);
          return hold(asvJSONValue::makeBool(res));
        } catch (...) { return nullptr; }
      }

      if (fn == "value") {
        const asvJSONValue* v = resolvePath();
        if (lex_.peek().type == TokType::RPAREN) lex_.next();
        return v;
      }

      const asvJSONValue* arg = resolvePath();
      if (lex_.peek().type == TokType::RPAREN) lex_.next();

      if (fn == "length") {
        if (!arg) return nullptr;
        int64_t len = 0;
        if (arg->type == asvJSONValue::STRING) {
          for (size_t i = 0; i < arg->str_data.size(); ) {
            auto c = static_cast<unsigned char>(arg->str_data[i]);
            if (c < 0x80) { len++; i++; }
            else if (c < 0xC0) { i++; }
            else if (c < 0xE0) { len++; i += 2; }
            else if (c < 0xF0) { len++; i += 3; }
            else { len++; i += 4; }
          }
        } else if (arg->type == asvJSONValue::ARRAY && arg->arr) {
          len = static_cast<int64_t>(arg->arr->size());
        } else if (arg->type == asvJSONValue::OBJECT && arg->obj) {
          len = static_cast<int64_t>(arg->obj->size());
        }
        return hold(asvJSONValue::makeInt(len));
      }

      if (fn == "count") {
        return hold(asvJSONValue::makeInt(arg ? 1 : 0));
      }

      if (fn == "min" || fn == "max" || fn == "sum" || fn == "avg") {
        if (!arg || arg->type != asvJSONValue::ARRAY || !arg->arr) return nullptr;
        if (arg->arr->empty()) return nullptr;
        double acc = 0;
        const asvJSONValue* best = (*arg->arr)[0].get();
        size_t count = 0;
        for (auto& e : *arg->arr) {
          if (e->type == asvJSONValue::INT || e->type == asvJSONValue::DOUBLE) {
            double v = (e->type == asvJSONValue::INT) ? static_cast<double>(e->num) : e->dbl;
            acc += v;
            count++;
            if (fn == "min" && jpCompare(e.get(), best) < 0) best = e.get();
            if (fn == "max" && jpCompare(e.get(), best) > 0) best = e.get();
          }
        }
        if (fn == "min" || fn == "max") return best;
        if (fn == "avg" && count > 0) return hold(asvJSONValue::makeDouble(acc / count));
        return hold(asvJSONValue::makeDouble(acc));
      }
    }

    return resolvePath();
  }

  bool parseComparison() {
    Tok t = lex_.peek();
    if (t.type == TokType::LPAREN) {
      lex_.next();
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
      const asvJSONValue* rhs = parseFuncValue();
      int c = jpCompare(lhs, rhs);
      switch (op.type) {
        case TokType::LT: return c < 0;
        case TokType::GT: return c > 0;
        case TokType::LE: return c <= 0;
        case TokType::GE: return c >= 0;
        case TokType::EQ: return c == 0;
        case TokType::NE: return c != 0;
        default: return false;
      }
    }
    return jpIsTruthy(lhs);
  }

public:
  JPFilterEval(PathLexer& lex, const asvJSONValue* cur) : lex_(lex), cur_(cur) {}
  bool evaluate() { return parseOr(); }
};

struct PathResult {
  std::vector<const asvJSONValue*> values;
  std::vector<std::unique_ptr<asvJSONValue>> owned;
  std::string error;
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

inline void jpExpandDotDot(Tok seg, PathLexer& lex, const asvJSONValue* node,
                            std::vector<const asvJSONValue*>& out) {
  if (seg.type == TokType::STAR) {
    jpCollectAllDesc(node, out);
  } else if (seg.type == TokType::IDENT) {
    jpCollectDesc(node, seg.val, out);
  }
}

inline void jpExpandBracket(PathLexer& lex, const asvJSONValue* node,
                             std::vector<const asvJSONValue*>& out, PathResult& res) {
  Tok inner = lex.peek();

  if (inner.type == TokType::STAR) {
    lex.next();
    if (lex.peek().type == TokType::RBRACKET) lex.next();
    auto children = jpAllChildren(node);
    out.insert(out.end(), children.begin(), children.end());
    return;
  }

  if (inner.type == TokType::NUMBER) {
    lex.next();

    if (lex.peek().type == TokType::COLON || lex.peek().type == TokType::DOT) {
      int64_t start = std::stoll(inner.val);
      int64_t end = INT64_MAX, step = 1;

      lex.next();
      Tok endTok = lex.peek();
      bool hasEnd = false;
      if (endTok.type == TokType::NUMBER) {
        end = std::stoll(endTok.val);
        hasEnd = true;
        lex.next();
      }

      if (lex.peek().type == TokType::COLON) {
        lex.next();
        Tok stepTok = lex.peek();
        if (stepTok.type == TokType::NUMBER) {
          step = std::stoll(stepTok.val);
          lex.next();
        }
      }

      if (lex.peek().type == TokType::RBRACKET) lex.next();

      int64_t sliceSz = (node && node->type == asvJSONValue::ARRAY && node->arr)
                         ? static_cast<int64_t>(node->arr->size()) : 0;
      if (!hasEnd) {
        end = (step > 0) ? sliceSz : -1;
      } else if (end < 0) {
        end = sliceSz + end;
      }
      if (end < 0) end = -1;

      auto sliced = jpSlice(node, start, end, step);
      out.insert(out.end(), sliced.begin(), sliced.end());
      return;
    }

    if (lex.peek().type == TokType::RBRACKET) lex.next();

    if (node && node->type == asvJSONValue::ARRAY && node->arr) {
      int64_t idx = std::stoll(inner.val);
      size_t ai;
      if (idx < 0) {
        if (static_cast<size_t>(-idx) > node->arr->size()) return;
        ai = node->arr->size() - static_cast<size_t>(-idx);
      } else {
        ai = static_cast<size_t>(idx);
      }
      if (ai < node->arr->size()) out.push_back((*node->arr)[ai].get());
    }
    return;
  }

  if (inner.type == TokType::STRING) {
    lex.next();
    if (lex.peek().type == TokType::RBRACKET) lex.next();
    const asvJSONValue* child = jpGetProp(node, inner.val);
    if (child) out.push_back(child);
    return;
  }

  if (inner.type == TokType::COLON || inner.type == TokType::DOT) {
    int64_t start = 0, end = INT64_MAX, step = 1;
    bool hasStart = false, hasEnd = false, hasStep = false;

    if (lex.peek().type == TokType::COLON) {
      lex.next();
      Tok endTok = lex.peek();
      if (endTok.type == TokType::NUMBER) {
        end = std::stoll(endTok.val);
        hasEnd = true;
        lex.next();
      } else if (endTok.type == TokType::COLON) {
      } else {
        end = INT64_MAX;
        hasEnd = true;
      }

      if (lex.peek().type == TokType::COLON) {
        lex.next();
        Tok stepTok = lex.peek();
        if (stepTok.type == TokType::NUMBER) {
          step = std::stoll(stepTok.val);
          hasStep = true;
          lex.next();
        }
      }
    }

    if (lex.peek().type == TokType::RBRACKET) lex.next();

    int64_t sliceSz = (node && node->type == asvJSONValue::ARRAY && node->arr)
                       ? static_cast<int64_t>(node->arr->size()) : 0;
    if (!hasStart) {
      start = (step > 0) ? 0 : sliceSz - 1;
    } else if (start < 0) {
      start = sliceSz + start;
    }
    if (!hasEnd) {
      end = (step > 0) ? sliceSz : -1;
    } else if (end < 0) {
      end = sliceSz + end;
    }
    if (start < 0) start = 0;
    if (end < 0) end = -1;

    auto sliced = jpSlice(node, start, end, step);
    out.insert(out.end(), sliced.begin(), sliced.end());
    return;
  }

  if (inner.type == TokType::QUESTION) {
    lex.next();
    if (lex.peek().type == TokType::LPAREN) lex.next();

    auto children = jpAllChildren(node);
    for (const asvJSONValue* child : children) {
      PathLexer filterLex(lex);  
      JPFilterEval filterEval(filterLex, child);
      if (filterEval.evaluate()) out.push_back(child);
    }

    while (lex.hasMore() && lex.peek().type != TokType::RBRACKET) lex.next();
    if (lex.peek().type == TokType::RBRACKET) lex.next();
    return;
  }

  if (lex.peek().type == TokType::RBRACKET) lex.next();
}

inline std::unique_ptr<asvJSONValue> jpApplyFunc(const std::string& fn, const asvJSONValue* arg) {
  if (fn == "length") {
    if (!arg) return nullptr;
    int64_t len = 0;
    if (arg->type == asvJSONValue::STRING) {
      for (size_t i = 0; i < arg->str_data.size(); ) {
        auto c = static_cast<unsigned char>(arg->str_data[i]);
        if (c < 0x80) { len++; i++; }
        else if (c < 0xC0) { i++; }
        else if (c < 0xE0) { len++; i += 2; }
        else if (c < 0xF0) { len++; i += 3; }
        else { len++; i += 4; }
      }
    } else if (arg->type == asvJSONValue::ARRAY && arg->arr) {
      len = static_cast<int64_t>(arg->arr->size());
    } else if (arg->type == asvJSONValue::OBJECT && arg->obj) {
      len = static_cast<int64_t>(arg->obj->size());
    }
    return asvJSONValue::makeInt(len);
  }
  if (fn == "count") {
    return asvJSONValue::makeInt(arg ? 1 : 0);
  }
  if (fn == "min" || fn == "max" || fn == "sum" || fn == "avg") {
    if (!arg || arg->type != asvJSONValue::ARRAY || !arg->arr) return nullptr;
    if (arg->arr->empty()) return nullptr;
    double acc = 0;
    const asvJSONValue* best = (*arg->arr)[0].get();
    size_t cnt = 0;
    for (auto& e : *arg->arr) {
      if (e->type == asvJSONValue::INT || e->type == asvJSONValue::DOUBLE) {
        double v = (e->type == asvJSONValue::INT) ? static_cast<double>(e->num) : e->dbl;
        acc += v;
        cnt++;
        if (fn == "min" && jpCompare(e.get(), best) < 0) best = e.get();
        if (fn == "max" && jpCompare(e.get(), best) > 0) best = e.get();
      }
    }
    if (fn == "min" || fn == "max") return cloneValue(best);
    if (fn == "avg" && cnt > 0) return asvJSONValue::makeDouble(acc / cnt);
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
      Tok seg = lex.next();
      std::vector<const asvJSONValue*> next;
      for (auto* n : current) jpExpandDotDot(seg, lex, n, next);
      current = std::move(next);
    }
    else if (t.type == TokType::LBRACKET) {
      lex.next();
      std::vector<const asvJSONValue*> next;
      for (auto* n : current) jpExpandBracket(lex, n, next, res);
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

  jpExpand(root, lex, res);
  return res;
}

inline bool jpIsValid(std::string_view path) {
  PathLexer lex(path);
  Tok first = lex.next();
  if (first.type != TokType::IDENT || first.val != "$") return false;
  PathResult dummy;
  jpExpand(nullptr, lex, dummy);
  return true;
}

inline std::vector<const asvJSONValue*> asvJSON::query(std::string_view path) const {
  auto r = jpQuery(root.get(), path);
  pathOwned = std::move(r.owned);
  return std::move(r.values);
}

inline const asvJSONValue* asvJSON::queryFirst(std::string_view path) const {
  auto r = jpQuery(root.get(), path);
  pathOwned = std::move(r.owned);
  return r.values.empty() ? nullptr : r.values.front();
}

inline bool asvJSON::isValidPath(std::string_view path) {
  return jpIsValid(path);
}

} // namespace asvJSONInternal
