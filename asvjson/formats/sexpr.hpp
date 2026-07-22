#pragma once
// S-Expression serialization/parsing for asvJSON++
//
// S-Expression format:
//   (key "value" (nested 1 2 3))
//   Objects: alternating key-value pairs in a list (heuristic: even length, keys are strings/symbols)
//   Arrays: plain list of values
//   Strings: "quoted" with standard C escapes
//   Numbers: as-is (must be valid JSON-like numbers)
//   Booleans: #t / #f
//   Null: nil
//   Comments: ; to end of line

#include "../core.hpp"

namespace asvJSONInternal {

enum SexprTokenType { SEXPR_LPAREN, SEXPR_RPAREN, SEXPR_STRING, SEXPR_NUMBER, SEXPR_SYMBOL, SEXPR_TRUE, SEXPR_FALSE, SEXPR_NIL };

struct SexprToken {
  SexprTokenType type;
  std::string value;
};

static std::vector<SexprToken> sexprTokenize(std::string_view input) {
  std::vector<SexprToken> tokens;
  size_t i = 0;
  while (i < input.size()) {
    char c = input[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
    if (c == ';') { while (i < input.size() && input[i] != '\n') i++; continue; }
    if (c == '(') { tokens.push_back({SEXPR_LPAREN, ""}); i++; continue; }
    if (c == ')') { tokens.push_back({SEXPR_RPAREN, ""}); i++; continue; }

    // String with proper unescaping
    if (c == '"') {
      std::string s;
      i++;
      bool closed = false;
      while (i < input.size()) {
        if (input[i] == '\\' && i + 1 < input.size()) {
          i++;
          switch (input[i]) {
            case 'n': s += '\n'; break;
            case 't': s += '\t'; break;
            case 'r': s += '\r'; break;
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            default: s += input[i]; break;
          }
          i++;
          continue;
        }
        if (input[i] == '"') { closed = true; i++; break; }
        s += input[i]; i++;
      }
      if (!closed) throw asvJSONError("unclosed string in S-Expression");
      tokens.push_back({SEXPR_STRING, s});
      continue;
    }

    if (c == '#' && i + 1 < input.size()) {
      if (input[i + 1] == 't') { tokens.push_back({SEXPR_TRUE, ""}); i += 2; continue; }
      if (input[i + 1] == 'f') { tokens.push_back({SEXPR_FALSE, ""}); i += 2; continue; }
    }

    std::string val;
    while (i < input.size() && input[i] != ' ' && input[i] != '\t' && input[i] != '\n' && input[i] != '\r' && input[i] != '(' && input[i] != ')' && input[i] != '"' && input[i] != ';') {
      val += input[i]; i++;
    }

    if (val == "nil") tokens.push_back({SEXPR_NIL, ""});
    else if (val == "#t" || val == "true") tokens.push_back({SEXPR_TRUE, ""});
    else if (val == "#f" || val == "false") tokens.push_back({SEXPR_FALSE, ""});
    else {
      // Strict number validation
      bool isNum = false;
      if (!val.empty()) {
        size_t start = 0;
        if (val[0] == '-' || val[0] == '+') start = 1;
        if (start < val.size() && std::isdigit(static_cast<unsigned char>(val[start]))) {
          isNum = true;
          bool hasDot = false;
          bool hasExp = false;
          for (size_t j = start + 1; j < val.size(); j++) {
            char cc = val[j];
            if (cc == '.') {
              if (hasDot || hasExp) { isNum = false; break; }
              hasDot = true;
            } else if (cc == 'e' || cc == 'E') {
              if (hasExp) { isNum = false; break; }
              hasExp = true;
              if (j + 1 >= val.size()) { isNum = false; break; }
              if (val[j + 1] == '+' || val[j + 1] == '-') {
                j++;
                if (j + 1 >= val.size() || !std::isdigit(static_cast<unsigned char>(val[j + 1]))) {
                  isNum = false; break;
                }
              }
            } else if (!std::isdigit(static_cast<unsigned char>(cc))) {
              isNum = false; break;
            }
          }
        }
      }
      if (isNum) tokens.push_back({SEXPR_NUMBER, val});
      else tokens.push_back({SEXPR_SYMBOL, val});
    }
  }
  return tokens;
}

// S-Expression internal marker byte for quoted object keys (stripped before JSON escaping)
static const char SEXPR_KEY_MARK = '\x01';

static std::string sexprTokenToJson(const SexprToken& token) {
  switch (token.type) {
    case SEXPR_STRING: {
      std::string esc;
      esc.reserve(token.value.size() + 2);
      esc += '"';
      appendJsonEscaped(esc, token.value);
      esc += '"';
      return esc;
    }
    case SEXPR_NUMBER: return token.value;
    case SEXPR_TRUE:   return "true";
    case SEXPR_FALSE:  return "false";
    case SEXPR_NIL:    return "null";
    case SEXPR_SYMBOL: {
      std::string esc;
      esc.reserve(token.value.size() + 2);
      esc += '"';
      appendJsonEscaped(esc, token.value);
      esc += '"';
      return esc;
    }
    default: return "null";
  }
}

static bool sexprTypeIsKey(int t) {
  return t == SEXPR_SYMBOL || t == SEXPR_NUMBER ||
         t == SEXPR_TRUE || t == SEXPR_FALSE || t == SEXPR_NIL;
}

static std::string sexprKeyJson(const std::string& item, int type) {
  if (type == SEXPR_NUMBER || type == SEXPR_TRUE ||
      type == SEXPR_FALSE || type == SEXPR_NIL) {
    // Bare JSON value — wrap in quotes for JSON object key
    return '"' + item + '"';
  }
  return item;
}

static std::string sexprTokensToJson(const std::vector<SexprToken>& tokens, size_t& pos, int depth = 0) {
  if (depth > 256) throw asvJSONError("S-Expression nesting too deep");

  if (pos >= tokens.size()) throw asvJSONError("unexpected end of S-Expression");
  if (tokens[pos].type == SEXPR_RPAREN) throw asvJSONError("unexpected closing parenthesis");

  // Top-level atom (not a list)
  if (tokens[pos].type != SEXPR_LPAREN) {
    return sexprTokenToJson(tokens[pos++]);
  }

  pos++; // skip '('
  std::vector<std::string> items;
  std::vector<int> itemTypes;
  while (pos < tokens.size() && tokens[pos].type != SEXPR_RPAREN) {
    if (tokens[pos].type == SEXPR_LPAREN) {
      std::string sub = sexprTokensToJson(tokens, pos, depth + 1);
      itemTypes.push_back(sub.empty() ? SEXPR_NIL : (sub[0] == '{' ? -2 : -1));
      items.push_back(std::move(sub));
    } else if (tokens[pos].type == SEXPR_STRING && !tokens[pos].value.empty() && tokens[pos].value[0] == SEXPR_KEY_MARK) {
      // Marked string key from encoder — strip marker and treat as symbol
      SexprToken stripped = tokens[pos];
      stripped.value = stripped.value.substr(1);
      items.push_back(sexprTokenToJson(stripped));
      itemTypes.push_back(SEXPR_SYMBOL);
      pos++;
    } else {
      items.push_back(sexprTokenToJson(tokens[pos]));
      itemTypes.push_back(tokens[pos].type);
      pos++;
    }
  }
  if (pos >= tokens.size()) throw asvJSONError("unclosed parenthesis in S-Expression");
  pos++; // skip ')'

  bool isObj = false;
  if (items.size() >= 2 && items.size() % 2 == 0) {
    // For 2-item lists, only treat as object if first item is a symbol
    if (items.size() == 2) {
      isObj = (itemTypes[0] == SEXPR_SYMBOL);
    } else {
      isObj = true;
      for (size_t j = 0; j < items.size(); j += 2) {
        if (!sexprTypeIsKey(itemTypes[j])) { isObj = false; break; }
      }
    }
  }

  // Fallback: if first item is a symbol-key, emit {key: values}
  if (!isObj && items.size() >= 2 && itemTypes[0] == SEXPR_SYMBOL) {
    std::string key = items[0];
    if (items.size() == 2) {
      return '{' + key + ':' + items[1] + '}';
    }
    std::string r = '{' + key + ":[";
    for (size_t j = 1; j < items.size(); j++) {
      if (j > 1) r += ',';
      r += items[j];
    }
    r += "]}";
    return r;
  }

  std::string out;
  if (isObj) {
    out = '{';
    for (size_t j = 0; j < items.size(); j += 2) {
      if (j > 0) out += ',';
      out += sexprKeyJson(items[j], itemTypes[j]) + ':' + items[j + 1];
    }
    out += '}';
  } else {
    out = '[';
    for (size_t j = 0; j < items.size(); j++) {
      if (j > 0) out += ',';
      out += items[j];
    }
    out += ']';
  }
  return out;
}

static void sexprSerializeVal(const asvJSONValue* v, std::string& out, int depth = 0) {
  if (!v) { out += "nil"; return; }
  if (!asvJSONValue::checkNestingDepth(depth)) { out += "nil"; return; }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += "nil"; break;
    case asvJSONValue::BOOL_VAL: out += v->flag ? "#t" : "#f"; break;
    case asvJSONValue::INT: out += std::to_string(v->num); break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d) || std::isinf(d)) { out += "nil"; break; }
      std::string r; fmtDoubleVal(d, r); out += r;
      break;
    }
    case asvJSONValue::STRING: {
      out += '"';
      appendJsonEscaped(out, v->str_data);
      out += '"';
      break;
    }
    case asvJSONValue::OBJECT: {
      out += '(';
      bool first = true;
      for (const auto& [k, child] : *(v->obj)) {
        if (!first) out += ' ';
        first = false;
        bool needsQuote = false;
        if (k.empty()) { needsQuote = true; }
        else {
          for (char c : k) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '+' && c != '*' && c != '/' && c != '?' && c != '!') {
              needsQuote = true; break;
            }
          }
          if (!needsQuote) {
            // Quote keys that would be misinterpreted as non-string tokens
            if (k == "nil" || k == "true" || k == "false" || k == "#t" || k == "#f") {
              needsQuote = true;
            } else if (std::isdigit(static_cast<unsigned char>(k[0]))) {
              needsQuote = true;
            } else if ((k[0] == '-' || k[0] == '+') && k.size() > 1 && std::isdigit(static_cast<unsigned char>(k[1]))) {
              needsQuote = true;
            }
          }
        }
        if (needsQuote) { out += '"'; out += SEXPR_KEY_MARK; appendJsonEscaped(out, k); out += '"'; }
        else out += k;
        out += ' ';
        sexprSerializeVal(child.get(), out, depth + 1);
      }
      out += ')';
      break;
    }
    case asvJSONValue::ARRAY: {
      out += '(';
      for (size_t i = 0; i < v->size(); i++) {
        if (i > 0) out += ' ';
        sexprSerializeVal(v->get(i), out, depth + 1);
      }
      out += ')';
      break;
    }
    default: out += "nil"; break;
  }
}

inline std::string asvJSON::toSexpr() const {
  if (!root) return "nil";
  std::string out;
  sexprSerializeVal(root.get(), out);
  return out;
}

inline bool asvJSON::fromSexpr(std::string_view input) {
  try {
    if (input.empty()) throw asvJSONError("empty input");
    auto tokens = sexprTokenize(input);
    if (tokens.empty()) throw asvJSONError("no tokens");
    size_t pos = 0;
    std::string json = sexprTokensToJson(tokens, pos);
    if (pos != tokens.size()) throw asvJSONError("unexpected tokens after S-Expression");
    return parse(std::string_view(json));
  } catch (const asvJSONError& e) {
    lastError = e.what();
    return false;
  }
}

} // namespace asvJSONInternal
