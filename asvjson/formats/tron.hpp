#pragma once
// TRON serialization/parsing for asvJSON++

#include "../core.hpp"

// ── TRON Encoder ──

namespace asvJSONInternal {

static std::string tronSchemaSignature(const asvJSONValue* v) {
  if (!v || v->type != asvJSONValue::OBJECT || v->obj->empty()) return {};
  std::vector<std::string> keys;
  keys.reserve(v->obj->size());
  for (const auto& [k, _] : *v->obj) keys.push_back(k);
  std::sort(keys.begin(), keys.end());
  std::string r;
  for (size_t i = 0; i < keys.size(); i++) {
    if (i > 0) r += ',';
    r += keys[i];
  }
  return r;
}

static void tronDiscoverSchemas(const asvJSONValue* v,
    std::unordered_map<std::string, std::vector<std::string>>& firstKeys,
    std::unordered_map<std::string, size_t>& counts,
    std::unordered_set<const asvJSONValue*>& visited) {
  if (!v) return;
  if (v->type == asvJSONValue::OBJECT && !v->obj->empty()) {
    if (visited.count(v)) return;
    visited.insert(v);
    auto sig = tronSchemaSignature(v);
    counts[sig]++;
    if (!firstKeys.count(sig)) {
      std::vector<std::string> orig;
      orig.reserve(v->obj->size());
      for (const auto& [k, _] : *v->obj) orig.push_back(k);
      std::sort(orig.begin(), orig.end());
      firstKeys[sig] = std::move(orig);
    }
    for (const auto& [_, child] : *v->obj)
      tronDiscoverSchemas(child.get(), firstKeys, counts, visited);
  } else if (v->type == asvJSONValue::ARRAY) {
    for (size_t i = 0; i < v->size(); i++)
      tronDiscoverSchemas(v->get(i), firstKeys, counts, visited);
  }
}

static std::string tronClassName(int idx) {
  std::string r(1, static_cast<char>('A' + (idx % 26)));
  int n = idx / 26;
  if (n > 0) r += std::to_string(n);
  return r;
}

static void tronSerializeVal(const asvJSONValue* v,
    const std::unordered_map<std::string, std::string>& sigToClass,
    const std::unordered_map<std::string, std::vector<std::string>>& classKeys,
    std::string& out, bool allowNaNInfinity = false) {
  if (!v) { out += "null"; return; }
  if (v->type >= asvJSONValue::DATETIME && v->type <= asvJSONValue::EXTENSION) {
    // Special types serialized as TRON values
    switch (v->type) {
      case asvJSONValue::DATETIME:
        out.push_back('"');
        fmtDateTimeVal(v->timestamp, v->datetime_ms, out);
        out.push_back('"');
        return;
      case asvJSONValue::BINARY:
        out += "\"__BASE64__";
        out += encodeBase64(v->bin_data.data(), v->bin_data.size());
        out += '"';
        return;
      case asvJSONValue::OBJECTID:
        out += '"';
        fmtObjectIdHexVal(v->str_data, out);
        out += '"';
        return;
      case asvJSONValue::REGEX:
        out += '"';
        {
          size_t sep = v->str_data.find('|');
          if (sep != std::string_view::npos) {
            appendJsonEscaped(out, v->str_data.substr(0, sep));
            out += '|';
            appendJsonEscaped(out, v->str_data.substr(sep + 1));
          } else {
            appendJsonEscaped(out, v->str_data);
          }
        }
        out += '"';
        return;
      case asvJSONValue::TIMESTAMP: {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
        if (ec == std::errc()) out.append(buf, ptr - buf);
        else out += '0';
        return;
      }
      case asvJSONValue::EXTENSION:
        out += '"';
        fmtExtVal(v->ext_type, v->bin_data.data(), v->bin_data.size(), out);
        out += '"';
        return;
    }
  }
  // Standard JSON-serializable types
  switch (v->type) {
    case asvJSONValue::NULL_VAL:
    case asvJSONValue::BOOL_VAL:
    case asvJSONValue::INT:
    case asvJSONValue::DOUBLE:
    case asvJSONValue::STRING:
      appendJsonToken(out, v, allowNaNInfinity);
      break;
    case asvJSONValue::ARRAY: {
      out += '[';
      for (size_t i = 0; i < v->size(); i++) {
        if (i > 0) out += ',';
        tronSerializeVal(v->get(i), sigToClass, classKeys, out, allowNaNInfinity);
      }
      out += ']';
      break;
    }
    case asvJSONValue::OBJECT: {
      if (v->obj->empty()) { out += "{}"; break; }
      auto sig = tronSchemaSignature(v);
      auto it = sigToClass.find(sig);
      if (it != sigToClass.end()) {
        auto kit = classKeys.find(it->second);
        if (kit != classKeys.end()) {
          out += it->second + '(';
          for (size_t i = 0; i < kit->second.size(); i++) {
            if (i > 0) out += ',';
            auto* child = v->getConst(kit->second[i]);
            if (child) tronSerializeVal(child, sigToClass, classKeys, out, allowNaNInfinity);
            else out += "null";
          }
          out += ')';
          break;
        }
      }
      out += '{';
      bool first = true;
      for (const auto& [k, child] : *v->obj) {
        if (!first) out += ',';
        first = false;
        out += '"';
        appendJsonEscaped(out, k);
        out += "\":";
        tronSerializeVal(child.get(), sigToClass, classKeys, out, allowNaNInfinity);
      }
      out += '}';
      break;
    }
    default: out += "null"; break;
  }
}

} // namespace asvJSONInternal
using namespace asvJSONInternal;

inline std::string asvJSON::toTRON() const {
  if (!root) return "null";
  std::unordered_map<std::string, std::vector<std::string>> firstKeys;
  std::unordered_map<std::string, size_t> counts;
  std::unordered_set<const asvJSONValue*> visited;
  tronDiscoverSchemas(root.get(), firstKeys, counts, visited);
  std::vector<std::pair<std::string, std::vector<std::string>>> qualified;
  for (const auto& [sig, keys] : firstKeys) {
    auto cit = counts.find(sig);
    if (cit != counts.end() && keys.size() > 1 && cit->second > 1)
      qualified.push_back({sig, keys});
  }
  std::unordered_map<std::string, std::string> sigToClass;
  std::unordered_map<std::string, std::vector<std::string>> classKeys;
  int idx = 0;
  for (auto& [sig, keys] : qualified) {
    std::string name = tronClassName(idx++);
    sigToClass[sig] = name;
    classKeys[name] = std::move(keys);
  }
  // Build header: collect class defs sorted by size then name
  std::vector<std::string> classNames;
  for (const auto& [name, _] : classKeys) classNames.push_back(name);
  std::sort(classNames.begin(), classNames.end(),
    [&](const std::string& a, const std::string& b) {
      size_t sa = classKeys.at(a).size();
      size_t sb = classKeys.at(b).size();
      if (sa != sb) return sa < sb;
      return a < b;
    });
  std::string out;
  for (const auto& name : classNames) {
    out += "class " + name + ":";
    for (size_t i = 0; i < classKeys.at(name).size(); i++) {
      if (i > 0) out += ',';
      out += '"';
      appendJsonEscaped(out, classKeys.at(name)[i]);
      out += '"';
    }
    out += '\n';
  }
  if (!classNames.empty()) out += '\n';
  tronSerializeVal(root.get(), sigToClass, classKeys, out, allowNaNInfinity);
  out += '\n';
  return out;
}

// ── TRON Decoder ──

enum class TronTokType {
  CLASS, IDENT, STRING, NUMBER, TRUE, FALSE, NUL, NAN_VAL, INF_VAL,
  LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE,
  COMMA, COLON, SEMICOLON, EQUALS, NEWLINE, END
};

struct TronTok {
  TronTokType type;
  std::string text;
};

namespace asvJSONInternal {

static std::string tronUnescape(const std::string& s) {
  return unescapeJsonString(s, false);
}

static std::vector<TronTok> tronTokenize(std::string_view in, bool allowNaNInfinity = false) {
  std::vector<TronTok> toks;
  size_t i = 0;
  auto add = [&](TronTokType t, std::string s = {}) { toks.push_back({t, std::move(s)}); };
  while (i < in.size()) {
    char c = in[i];
    if (c == '\r') { i++; continue; }
    if (c == '#') { while (i < in.size() && in[i] != '\n') i++; continue; }
    if (c == '\n') { i++; add(TronTokType::NEWLINE); continue; }
    if (c == ' ' || c == '\t') { i++; continue; }
    if (c == '"') {
      i++;
      std::string s;
      while (i < in.size() && in[i] != '"') {
        if (in[i] == '\\' && i + 1 < in.size()) { s += in[i++]; s += in[i++]; }
        else s += in[i++];
      }
      if (i < in.size()) i++;
      add(TronTokType::STRING, s);
      continue;
    }
    if (allowNaNInfinity && c == '-' && i + 8 < in.size() && in.substr(i + 1, 8) == "Infinity") {
      add(TronTokType::INF_VAL, "-Infinity");
      i += 9; continue;
    }
    if (allowNaNInfinity && c == '+' && i + 8 < in.size() && in.substr(i + 1, 8) == "Infinity") {
      add(TronTokType::INF_VAL, "+Infinity");
      i += 9; continue;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      size_t start = i;
      if (c == '-') i++;
      while (i < in.size() && in[i] >= '0' && in[i] <= '9') i++;
      if (i < in.size() && in[i] == '.') { i++; while (i < in.size() && in[i] >= '0' && in[i] <= '9') i++; }
      if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
        i++; if (i < in.size() && (in[i] == '+' || in[i] == '-')) i++;
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') i++;
      }
      add(TronTokType::NUMBER, std::string(in.substr(start, i - start)));
      continue;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      size_t start = i;
      while (i < in.size() && ((in[i] >= 'a' && in[i] <= 'z') || (in[i] >= 'A' && in[i] <= 'Z') || (in[i] >= '0' && in[i] <= '9') || in[i] == '_')) i++;
      std::string word(in.substr(start, i - start));
      if (word == "class") add(TronTokType::CLASS);
      else if (word == "true") add(TronTokType::TRUE);
      else if (word == "false") add(TronTokType::FALSE);
      else if (word == "null") add(TronTokType::NUL);
      else if (allowNaNInfinity && word == "NaN") add(TronTokType::NAN_VAL);
      else if (allowNaNInfinity && word == "Infinity") add(TronTokType::INF_VAL, word);
      else add(TronTokType::IDENT, word);
      continue;
    }
    switch (c) {
      case '(': add(TronTokType::LPAREN); break;
      case ')': add(TronTokType::RPAREN); break;
      case '[': add(TronTokType::LBRACKET); break;
      case ']': add(TronTokType::RBRACKET); break;
      case '{': add(TronTokType::LBRACE); break;
      case '}': add(TronTokType::RBRACE); break;
      case ',': add(TronTokType::COMMA); break;
      case ':': add(TronTokType::COLON); break;
      case ';': add(TronTokType::SEMICOLON); break;
      case '=': add(TronTokType::EQUALS); break;
      default: i++; continue;
    }
    i++;
  }
  add(TronTokType::END);
  return toks;
}

struct TronClassInfo {
  std::vector<std::string> props;
};

struct TronParseState {
  const std::vector<TronTok>& toks;
  size_t pos = 0;
  std::unordered_map<std::string, TronClassInfo> classes;
  asvJSONValue* root = nullptr;

  TronParseState(const std::vector<TronTok>& t) : toks(t) {}

  const TronTok& peek() { return toks[pos]; }
  const TronTok& advance() { return toks[pos++]; }
  bool match(TronTokType t) { if (toks[pos].type == t) { pos++; return true; } return false; }
  void skipNewlines() { while (toks[pos].type == TronTokType::NEWLINE || toks[pos].type == TronTokType::SEMICOLON) pos++; }

  asvJSONValue* parseValue();

  asvJSONValue* parseObject() {
    advance(); // '{'
    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("out of memory");
    skipNewlines();
    if (peek().type == TronTokType::RBRACE) { advance(); return obj.release(); }
    bool first = true;
    while (true) {
      if (!first) {
        if (peek().type == TronTokType::COMMA) { advance(); skipNewlines(); }
        else break;
      }
      first = false;
      if (peek().type == TronTokType::RBRACE) break;
      if (peek().type != TronTokType::STRING) throw asvJSONError("expected string key");
      std::string key = tronUnescape(advance().text);
      if (!match(TronTokType::COLON)) throw asvJSONError("expected ':'");
      skipNewlines();
      asvJSONValue* val = parseValue();
      if (val) obj->obj->emplace(key, std::unique_ptr<asvJSONValue>(val));
      skipNewlines();
    }
    if (!match(TronTokType::RBRACE)) throw asvJSONError("expected '}' or ',' in object");
    return obj.release();
  }

  asvJSONValue* parseArray() {
    advance(); // '['
    auto arr = asvJSONValue::makeArray();
    if (!arr) throw asvJSONError("out of memory");
    skipNewlines();
    if (peek().type == TronTokType::RBRACKET) { advance(); return arr.release(); }
    bool first = true;
    while (true) {
      if (!first) {
        if (peek().type == TronTokType::COMMA) { advance(); skipNewlines(); }
        else break;
      }
      first = false;
      if (peek().type == TronTokType::RBRACKET) break;
      asvJSONValue* val = parseValue();
      if (val) arr->arr->push_back(std::unique_ptr<asvJSONValue>(val));
      skipNewlines();
    }
    if (!match(TronTokType::RBRACKET)) throw asvJSONError("expected ']' or ',' in array");
    return arr.release();
  }

  asvJSONValue* parseInstance() {
    std::string className = advance().text;
    auto it = classes.find(className);
    if (it == classes.end()) throw asvJSONError("undefined class: " + className);
    const auto& props = it->second.props;
    if (!match(TronTokType::LPAREN)) throw asvJSONError("expected '('");
    skipNewlines();
    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("out of memory");
    std::vector<std::unique_ptr<asvJSONValue>> posArgs;
    std::unordered_map<std::string, std::unique_ptr<asvJSONValue>> namedArgs;
    bool namedMode = false;
    while (peek().type != TronTokType::RPAREN && peek().type != TronTokType::END) {
      if (!posArgs.empty() || !namedArgs.empty()) {
        if (peek().type == TronTokType::COMMA) { advance(); skipNewlines(); continue; }
      }
      skipNewlines();
      if ((peek().type == TronTokType::IDENT || peek().type == TronTokType::STRING) && pos + 1 < toks.size() && toks[pos + 1].type == TronTokType::EQUALS) {
        namedMode = true;
        std::string propName;
        if (peek().type == TronTokType::STRING) propName = tronUnescape(advance().text);
        else propName = advance().text;
        advance(); // '='
        skipNewlines();
        asvJSONValue* val = parseValue();
        namedArgs[propName] = std::unique_ptr<asvJSONValue>(val);
      } else {
        if (namedMode) throw asvJSONError("positional arg after named");
        asvJSONValue* val = parseValue();
        posArgs.push_back(std::unique_ptr<asvJSONValue>(val));
      }
      skipNewlines();
    }
    if (!match(TronTokType::RPAREN)) throw asvJSONError("expected ')'");
    for (size_t i = 0; i < props.size(); i++) {
      std::unique_ptr<asvJSONValue> valPtr;
      if (i < posArgs.size()) {
        valPtr = std::move(posArgs[i]);
      } else {
        auto nit = namedArgs.find(props[i]);
        if (nit != namedArgs.end()) valPtr = std::move(nit->second);
      }
      if (!valPtr) {
        obj->obj->emplace(props[i], asvJSONValue::makeNull());
      } else {
        obj->obj->emplace(props[i], std::move(valPtr));
      }
    }
    return obj.release();
  }
};

asvJSONValue* TronParseState::parseValue() {
  skipNewlines();
  auto& tok = peek();
  static const double tronNaN = std::numeric_limits<double>::quiet_NaN();
  static const double tronInf = std::numeric_limits<double>::infinity();
  switch (tok.type) {
    case TronTokType::LBRACE: return parseObject();
    case TronTokType::LBRACKET: return parseArray();
    case TronTokType::STRING: {
      std::string raw = advance().text;
      std::string s = tronUnescape(raw);
      // Check __BASE64__ prefix
      if (s.size() > 10 && s.compare(0, 10, "__BASE64__") == 0) {
        auto data = decodeBase64Fast(s.data() + 10, s.size() - 10);
        auto v = asvJSONValue::makeBinary(data.data(), data.size());
        if (!v) throw asvJSONError("out of memory");
        return v.release();
      }
      // Check __EXT__ prefix: "__EXT__<type>__<base64>"
      if (s.size() > 7 && s.compare(0, 7, "__EXT__") == 0) {
        size_t sep = s.find("__", 7);
        if (sep != std::string::npos && sep > 7) {
          int extType;
          auto [ptr, ec] = std::from_chars(s.data() + 7, s.data() + sep, extType);
          if (ec != std::errc() || ptr != s.data() + sep)
            throw asvJSONError("invalid extension type");
          auto data = decodeBase64Fast(s.data() + sep + 2, s.size() - sep - 2);
          auto v = asvJSONValue::makeExtension(static_cast<int8_t>(extType), data.data(), data.size());
          if (!v) throw asvJSONError("out of memory");
          return v.release();
        }
      }
      // Check ISO 8601 date
      if (s.size() >= 20 && s[4] == '-' && s[7] == '-' && s[10] == 'T') {
        time_t ts;
        int ms = 0;
        if (tryParseDateTime(s, ts, &ms)) {
          auto v = asvJSONValue::makeDateTime(ts, ms);
          if (!v) throw asvJSONError("out of memory");
          return v.release();
        }
      }
      auto v = asvJSONValue::makeStringView(s);
      if (!v) throw asvJSONError("out of memory");
      return v.release();
    }
    case TronTokType::NUMBER: {
      std::string n = advance().text;
      bool isDbl = n.find('.') != std::string::npos || n.find('e') != std::string::npos || n.find('E') != std::string::npos;
      if (isDbl) {
        double d;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
        auto [ptr, ec] = std::from_chars(n.data(), n.data() + n.size(), d);
        if (ec != std::errc() || ptr != n.data() + n.size()) throw asvJSONError("invalid number: " + n);
#else
        char* end;
        errno = 0;
        d = std::strtod(n.c_str(), &end);
        if (errno == ERANGE || end != n.c_str() + n.size()) throw asvJSONError("invalid number: " + n);
#endif
        auto v = asvJSONValue::makeDouble(d);
        if (!v) throw asvJSONError("out of memory");
        return v.release();
      } else {
        long long l;
        auto [ptr, ec] = std::from_chars(n.data(), n.data() + n.size(), l);
        if (ec != std::errc() || ptr != n.data() + n.size()) throw asvJSONError("invalid number: " + n);
        auto v = asvJSONValue::makeInt(l);
        if (!v) throw asvJSONError("out of memory");
        return v.release();
      }
    }
    case TronTokType::TRUE: advance(); { auto v = asvJSONValue::makeBool(true); return v.release(); }
    case TronTokType::FALSE: advance(); { auto v = asvJSONValue::makeBool(false); return v.release(); }
    case TronTokType::NUL: advance(); { auto v = asvJSONValue::makeNull(); return v.release(); }
    case TronTokType::NAN_VAL: advance(); { auto v = asvJSONValue::makeDouble(tronNaN); return v.release(); }
    case TronTokType::INF_VAL: {
      bool isNeg = tok.text.size() > 0 && tok.text[0] == '-';
      advance();
      auto v = asvJSONValue::makeDouble(isNeg ? -tronInf : tronInf);
      return v.release();
    }
    case TronTokType::IDENT: {
      if (pos + 1 < toks.size() && toks[pos + 1].type == TronTokType::LPAREN)
        return parseInstance();
      throw asvJSONError("unexpected identifier: " + tok.text);
    }
    default:
      throw asvJSONError("unexpected token");
  }
}

} // namespace asvJSONInternal
using namespace asvJSONInternal;

inline bool asvJSON::fromTRON(std::string_view input) {
  try {
    auto toks = tronTokenize(input, allowNaNInfinity);
    TronParseState state(toks);
    state.skipNewlines();
    // Parse class definitions
    while (state.peek().type == TronTokType::CLASS) {
      state.advance(); // 'class'
      if (state.peek().type != TronTokType::IDENT)
        throw asvJSONError("expected class name");
      std::string name = state.advance().text;
      // Check for inheritance: Name(Parent)
      std::vector<std::string> parentProps;
      if (state.peek().type == TronTokType::LPAREN) {
        state.advance(); // '('
        if (state.peek().type != TronTokType::IDENT)
          throw asvJSONError("expected parent class name");
        std::string pname = state.advance().text;
        auto pit = state.classes.find(pname);
        if (pit == state.classes.end())
          throw asvJSONError("parent class not found: " + pname);
        parentProps = pit->second.props;
        if (!state.match(TronTokType::RPAREN))
          throw asvJSONError("expected ')' after parent name");
      }
      if (!state.match(TronTokType::COLON))
        throw asvJSONError("expected ':' in class definition");
      state.skipNewlines();
      // Parse property list
      std::vector<std::string> props;
      while (state.peek().type == TronTokType::IDENT || state.peek().type == TronTokType::STRING) {
        if (state.peek().type == TronTokType::STRING) {
          props.push_back(tronUnescape(state.advance().text));
        } else {
          props.push_back(state.advance().text);
        }
        state.skipNewlines();
        if (state.peek().type == TronTokType::COMMA) { state.advance(); state.skipNewlines(); }
        else if (state.peek().type == TronTokType::NEWLINE) { state.advance(); state.skipNewlines(); if (state.peek().type != TronTokType::IDENT && state.peek().type != TronTokType::STRING) break; }
        else break;
      }
      if (props.empty())
        throw asvJSONError("class requires at least one property");
      // Merge parent props (child overrides parent)
      std::vector<std::string> allProps = std::move(parentProps);
      for (const auto& p : props) {
        if (std::find(allProps.begin(), allProps.end(), p) == allProps.end())
          allProps.push_back(p);
      }
      state.classes[name] = {std::move(allProps)};
    }
    // Parse root value
    state.root = state.parseValue();
    if (!state.root) {
      root = nullptr;
      return false;
    }
    state.skipNewlines();
    if (state.peek().type != TronTokType::END)
      throw asvJSONError("trailing tokens after root value");
    root.reset(state.root);
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}
