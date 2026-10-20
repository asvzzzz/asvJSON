#pragma once
// TRON (Token Reduced Object Notation) serialization/parsing for asvJSON++
//
// JSON superset that hoists repeated property sets into class definitions:
//   class A: name,age            -- schema for objects with keys {name,age}
//   A(Alice,30)                  -- positional instantiation
//   A(name=Alice,age=30)         -- named instantiation
//   class B(A): z                -- inheritance (B = A's props + z)
// A class is hoisted only for a key set with TWO OR MORE properties that occurs
// at least twice; a single-property object (e.g. [{"id":1},{"id":2}]) is always
// serialized inline, since hoisting one property saves nothing.
// Comments use `#` to end of line; trailing commas allowed. Full round-trip
// via toTRON()/fromTRON(). A literal STRING whose text starts with a special-type
// marker prefix (__TS__, __OID__, __REGEX__, __BASE64__, __EXT__) or has the
// ISO-8601 shape the decoder auto-detects as a date has its leading character
// escaped as \uXXXX, so it stays a STRING; DATETIME itself is written unescaped
// in plain ISO form and decodes back as a DATETIME. The one DATETIME that cannot
// round-trip as DATETIME is a timestamp outside gmtimeSafe's range, which
// fmtDateTimeVal renders as the literal "null"; it is quoted so it decodes back
// as the STRING "null" (matching INI/TOML/CSV).
//
// The encoder emits FLAT class definitions: inheritance (`class B(A):`) is fully
// supported by the decoder, but re-serialized as an equivalent flat class (the
// inherited properties are merged into the emitted class's property list).
//
// Known limitation: toTRON() returns std::string with no error channel, so a tree
// nested deeper than MAX_NESTING_DEPTH (reachable via setByPointer, which does not
// enforce the limit) is emitted with "null" past that depth instead of reporting an
// error. This matches the other recursive encoders (JSON5/RON/sexpr/GOON).
// CUSTOM_TAG values are not preserved: they serialize as "null" (the encoder's
// default case), as TRON has no marker form for them.
// `;` is accepted only as a class-definition terminator (`class A: x,y;z`). In
// every other context it emits a syntax error (the tokenizer still produces a
// SEMICOLON token, but parseValue/parseInstance reject it). This is intentional:
// the spec does not document `,` or `;` as general-purpose separators.
// Decoder diagnostics are prefixed "TRON: " and carry no line number (unlike the
// JSON/INI/TOML parsers): the lexer tokenizes the whole document before parsing,
// so positions are not tracked through to the error sites.
//
// Known limitation: bare-root REGEX markers (`__REGEX__pattern|opts`) are NOT
// accepted at the document root - `|` is a TRON syntax character there and the
// lexer rejects it before the marker handler runs. The encoder never emits such
// a form (it wraps REGEX values in a JSON object key), so this is not a round-trip
// gap. Hand-written documents that use this spelling will fail with
// "TRON: unexpected character: '|''; the supported external-marker form is the
// JSON-embedded one: `{"re":"__REGEX__pattern|opts|"}`.

#include "../core.hpp"

#include <algorithm>

// -- TRON Encoder --

namespace asvJSONInternal {

inline std::string tronSchemaSignature(const asvJSONValue* v) {
  if (!v || v->type != asvJSONValue::OBJECT || v->obj->empty()) return {};
  std::vector<std::string> keys;
  keys.reserve(v->obj->size());
  for (const auto& [k, _] : *v->obj) keys.push_back(k);
  std::sort(keys.begin(), keys.end());
  // Each key is JSON-quoted so a key containing ',' (e.g. ["a,b"] as a single
  // key) can never collide with a multi-key shape (["a","b"]): the plain
  // comma-joined form merged both into the same signature, corrupting values.
  std::string r;
  for (size_t i = 0; i < keys.size(); i++) {
    if (i > 0) r += ',';
    r += '"';
    appendJsonEscaped(r, keys[i]);
    r += '"';
  }
  return r;
}

inline void tronDiscoverSchemas(const asvJSONValue* v,
    std::unordered_map<std::string, std::vector<std::string>>& firstKeys,
    std::unordered_map<std::string, size_t>& counts,
    std::unordered_set<const asvJSONValue*>& visited,
    int depth = 0) {
  if (!v) return;
  if (!asvJSONValue::checkNestingDepth(depth)) return;
  if (v->type == asvJSONValue::OBJECT && !v->obj->empty()) {
    // A yaml_set object is emitted as the {"$set":[...]} marker form, never as a
    // class instance: hoisting it would render the members as a positional
    // instance and drop the set flag.
    if (v->yaml_set) return;
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
      tronDiscoverSchemas(child.get(), firstKeys, counts, visited, depth + 1);
  } else if (v->type == asvJSONValue::ARRAY) {
    for (size_t i = 0; i < v->size(); i++)
      tronDiscoverSchemas(v->get(i), firstKeys, counts, visited, depth + 1);
  }
}

inline std::string tronClassName(int idx) {
  std::string r(1, static_cast<char>('A' + (idx % 26)));
  int n = idx / 26;
  if (n > 0) r += std::to_string(n);
  return r;
}

inline bool tronLooksLikeMarker(std::string_view s) {
  static constexpr std::string_view pre[] = {
    "__TS__", "__OID__", "__REGEX__", "__BASE64__", "__EXT__"
  };
  for (auto p : pre) {
    if (s.size() >= p.size() && s.compare(0, p.size(), p) == 0) return true;
  }
  return false;
}

// True when the decoder would turn this string token into a DATETIME (the
// ISO-8601 auto-detection in parseValue). Kept as a named predicate so the
// encoder and the decoder gate on the exact same shape test.
inline bool tronLooksLikeDateTime(std::string_view s) {
  return s.size() >= 20 && s[4] == '-' && s[7] == '-' && s[10] == 'T';
}

// Write `s` as a TRON quoted token whose FIRST character is emitted as a \uXXXX
// escape, so the raw token text no longer matches tronLooksLikeMarker() or
// tronLooksLikeDateTime() and the decoder returns a plain STRING. Used for
// literal strings the decoder would otherwise reinterpret as a special type.
inline void tronEmitEscapedLeading(std::string& out, std::string_view s) {
  static constexpr char HEX[] = "0123456789abcdef";
  unsigned char c = static_cast<unsigned char>(s[0]);
  out += '"';
  char buf[6] = {'\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 0xF]};
  out.append(buf, 6);
  appendJsonEscaped(out, s.substr(1));
  out += '"';
}

// JSON-escape a string for embedding inside a TRON quoted value, additionally
// turning any literal '|' into \u007C so raw '|' characters appear ONLY as the
// outer __REGEX__ field separators (never in the pattern/options content). TRON
// strings use strict JSON escaping, so \| is not valid there.
inline void tronEscPipe(std::string& out, std::string_view s) {
  const char* p = s.data();
  const char* const end = p + s.size();
  const char* run = p;
  static constexpr char HEX[] = "0123456789abcdef";
  while (p < end) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (c == '|') {
      if (p > run) out.append(run, static_cast<size_t>(p - run));
      out += "\\u007C";
      p++;
      run = p;
      continue;
    }
    if (c >= 0x20 && c != '"' && c != '\\') { p++; continue; }
    if (p > run) out.append(run, static_cast<size_t>(p - run));
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: {
        char buf[6] = {'\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 0xF]};
        out.append(buf, 6);
        break;
      }
    }
    p++;
    run = p;
  }
  if (run < end) out.append(run, static_cast<size_t>(end - run));
}

inline void tronSerializeVal(const asvJSONValue* v,
    const std::unordered_map<std::string, std::string>& sigToClass,
    const std::unordered_map<std::string, std::vector<std::string>>& classKeys,
    std::string& out, bool allowNaNInfinity = false, int depth = 0) {
  if (!v) { out += "null"; return; }
  if (!asvJSONValue::checkNestingDepth(depth)) { out += "null"; return; }
  if (v->type >= asvJSONValue::DATETIME && v->type <= asvJSONValue::EXTENSION) {
    // Special types serialized as TRON values
    switch (v->type) {
      case asvJSONValue::DATETIME:
        out.push_back('"');
        fmtDateTimeVal(v->timestamp, v->datetime_ms, out, v->tz_offset_min);
        out.push_back('"');
        return;
      case asvJSONValue::BINARY:
        out += "\"__BASE64__";
        out += encodeBase64(v->bin_data.data(), v->bin_data.size());
        out += '"';
        return;
      case asvJSONValue::OBJECTID:
        out += "\"__OID__";
        fmtObjectIdHexVal(v->str_data, out);
        out += '"';
        return;
      case asvJSONValue::REGEX: {
        // Emit __REGEX__<P>|<O>| -- the pattern/options are JSON-escaped with
        // any literal '|' as \u007C, so raw '|' chars only ever appear as field
        // separators: the first is the pattern/options boundary and the last a
        // mandatory terminator that disambiguates an options-free pattern that
        // itself ends in '|'.
        out += "\"__REGEX__";
        size_t sep = v->str_data.rfind('\0');
        std::string_view pat = (sep != std::string_view::npos)
            ? std::string_view(v->str_data.data(), sep) : std::string_view(v->str_data);
        std::string_view opts = (sep != std::string_view::npos && sep + 1 < v->str_data.size())
            ? std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1)
            : std::string_view();
        tronEscPipe(out, pat);
        out += '|';
        if (!opts.empty()) { tronEscPipe(out, opts); out += '|'; }
        out += '"';
        return;
      }
      case asvJSONValue::TIMESTAMP: {
        out += "\"__TS__";
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
        if (ec == std::errc()) out.append(buf, ptr - buf);
        else out += '0';
        out += '"';
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
    case asvJSONValue::DOUBLE: {
      // fmtDoubleVal emits "1" for 1.0 and "-0" for -0.0, and the TRON decoder
      // classifies a token as DOUBLE only when it contains '.', 'e' or 'E'.
      // A bare "1" would therefore re-decode as INT, and "-0" as INT 0 (losing
      // the sign), so append ".0" whenever the literal is a plain integer.
      // This mirrors the CSV/TOON/ADE convention.
      if (std::isnan(v->dbl) || std::isinf(v->dbl)) {
        appendJsonToken(out, v, allowNaNInfinity);
        break;
      }
      std::string s;
      fmtDoubleVal(v->dbl, s);
      if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
          s.find('E') == std::string::npos)
        s += ".0";
      out += s;
      break;
    }
    case asvJSONValue::NULL_VAL:
    case asvJSONValue::BOOL_VAL:
    case asvJSONValue::INT:
      appendJsonToken(out, v, allowNaNInfinity);
      break;
    case asvJSONValue::STRING:
      // A literal string that the decoder would re-interpret as a special type
      // must keep its STRING type on round-trip. The decoder only recognizes
      // markers (and the ISO-8601 date shape) in RAW token text, so escaping the
      // first character as \uXXXX keeps the value a string.
      if (tronLooksLikeMarker(v->str_data) || tronLooksLikeDateTime(v->str_data)) {
        tronEmitEscapedLeading(out, v->str_data);
        break;
      }
      appendJsonToken(out, v, allowNaNInfinity);
      break;
    case asvJSONValue::ARRAY: {
      out += '[';
      for (size_t i = 0; i < v->size(); i++) {
        if (i > 0) out += ',';
        tronSerializeVal(v->get(i), sigToClass, classKeys, out, allowNaNInfinity, depth + 1);
      }
      out += ']';
      break;
    }
    case asvJSONValue::OBJECT: {
      if (v->obj->empty()) { out += "{}"; break; }
      if (v->yaml_set) {
        // YAML !!set marker form: {"$set":["a","b"]}; the member keys become
        // the array strings in insertion order. fromTRON() recognizes this
        // shape and restores the yaml_set flag.
        out += "{\"$set\":[";
        bool sfirst = true;
        for (const auto& [k, _] : *v->obj) {
          if (!sfirst) out.push_back(',');
          out.push_back('"');
          appendJsonEscaped(out, k);
          out.push_back('"');
          sfirst = false;
        }
        out += "]}";
        break;
      }
      auto sig = tronSchemaSignature(v);
      auto it = sigToClass.find(sig);
      if (it != sigToClass.end()) {
        auto kit = classKeys.find(it->second);
        if (kit != classKeys.end()) {
          out += it->second + '(';
          for (size_t i = 0; i < kit->second.size(); i++) {
            if (i > 0) out += ',';
            auto* child = v->getConst(kit->second[i]);
            if (child) tronSerializeVal(child, sigToClass, classKeys, out, allowNaNInfinity, depth + 1);
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
        tronSerializeVal(child.get(), sigToClass, classKeys, out, allowNaNInfinity, depth + 1);
      }
      out += '}';
      break;
    }
    default: out += "null"; break;
  }
}

inline std::string asvJSON::toTRON() const {
  if (!root) return "null\n";
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
  // Sort before assigning class names: firstKeys iterates an unordered_map, so
  // without this the A..Z name assignment (and thus the emitted document) would
  // be hash-order dependent instead of deterministic.
  std::sort(qualified.begin(), qualified.end(),
    [](const std::pair<std::string, std::vector<std::string>>& a,
       const std::pair<std::string, std::vector<std::string>>& b) {
      if (a.second.size() != b.second.size()) return a.second.size() < b.second.size();
      return a.first < b.first;
    });
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

// -- TRON Decoder --

enum class TronTokType {
  CLASS, IDENT, STRING, NUMBER, TRUE, FALSE, NUL, NAN_VAL, INF_VAL,
  LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE,
  COMMA, COLON, SEMICOLON, EQUALS, NEWLINE, END
};

struct TronTok {
  TronTokType type;
  std::string text;
};

inline std::string tronUnescape(const std::string& s) {
  try { return unescapeJsonString(s, true); }
  catch (const std::runtime_error&) { throw asvJSONError("TRON: invalid escape in string"); }
}

inline std::vector<TronTok> tronTokenize(std::string_view in, bool allowNaNInfinity = false) {
  std::vector<TronTok> toks;
  size_t i = 0;
  auto add = [&](TronTokType t, std::string s = {}) { toks.push_back({t, std::move(s)}); };
  while (i < in.size()) {
    char c = in[i];
    if (c == '\r') { i++; if (i < in.size() && in[i] == '\n') i++; add(TronTokType::NEWLINE); continue; }
    // Note: \r\n is consumed as a single NEWLINE above. A bare \r also becomes
    // NEWLINE (the fix above). The original code silently skipped \r which meant
    // old Mac-style line endings were invisible to the parser.
    if (c == '#') { while (i < in.size() && in[i] != '\n' && in[i] != '\r') i++; continue; }
    if (c == '\n') { i++; add(TronTokType::NEWLINE); continue; }
    if (c == ' ' || c == '\t') { i++; continue; }
    if (c == '"') {
      i++;
      std::string s;
      while (i < in.size() && in[i] != '"') {
        if (in[i] == '\\' && i + 1 < in.size()) { s += in[i++]; s += in[i++]; }
        else s += in[i++];
      }
      if (i >= in.size()) throw asvJSONError("TRON: unterminated string");
      i++;
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
    if (allowNaNInfinity && c == '-' && i + 3 < in.size() && in.substr(i + 1, 3) == "NaN") {
      add(TronTokType::NAN_VAL, "-NaN");
      i += 4; continue;
    }
    if (allowNaNInfinity && c == '+' && i + 3 < in.size() && in.substr(i + 1, 3) == "NaN") {
      add(TronTokType::NAN_VAL, "+NaN");
      i += 4; continue;
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
      default:
        throw asvJSONError("TRON: " + std::string("unexpected character: '") + c + "'");
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
  void skipNewlines() { while (toks[pos].type == TronTokType::NEWLINE) pos++; }

  // A property name is a bare identifier or a quoted string. Per the spec, an
  // unquoted identifier immediately followed by '(' is a class *instantiation*
  // (the start of the data section), not a property  -  even when it follows a
  // newline separator. Without this guard the data identifier would be
  // swallowed into the preceding class's property list.
  bool propAhead() const {
    auto t = toks[pos].type;
    return (t == TronTokType::IDENT || t == TronTokType::STRING) &&
           !(pos + 1 < toks.size() && toks[pos + 1].type == TronTokType::LPAREN);
  }

  asvJSONValue* parseValue(int depth = 0);

  asvJSONValue* parseObject(int depth) {
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
      if (obj->obj->size() >= asvJSONValue::MAX_OBJECT_SIZE)
        throw asvJSONError("TRON object too large");
      if (peek().type != TronTokType::STRING) throw asvJSONError("TRON: expected string key");
      std::string key = tronUnescape(advance().text);
      if (!match(TronTokType::COLON)) throw asvJSONError("TRON: expected ':'");
      skipNewlines();
      asvJSONValue* val = parseValue(depth + 1);
      if (val) obj->obj->insert_or_assign(key, std::unique_ptr<asvJSONValue>(val));
      skipNewlines();
    }
    if (!match(TronTokType::RBRACE)) throw asvJSONError("TRON: expected '}' or ',' in object");
    // YAML set marker: {"$set":["a","b"]} -> object whose members are the array
    // strings (each mapped to null), flagged yaml_set. Recognized only when the
    // key is the sole member and every array element is a string, so a plain
    // object with a "$set" key holding non-strings (e.g. MongoDB's update
    // operator {"$set":[1,2]}) is left untouched. Mirrors the JSON parser.
    if (obj->obj->size() == 1) {
      auto stIt = obj->obj->find("$set");
      if (stIt != obj->obj->end() && stIt->second && stIt->second->type == asvJSONValue::ARRAY &&
          stIt->second->arr) {
        bool allStrings = true;
        for (const auto& m : *stIt->second->arr) {
          if (!m || m->type != asvJSONValue::STRING) { allStrings = false; break; }
        }
        if (allStrings) {
          // Reuse the parsed object in place: the sole "$set" member is an ARRAY
          // that gets replaced by one null per array element, so the object needs
          // no further allocation beyond the null values themselves.
          auto setArr = std::move(stIt->second);
          obj->obj->clear();
          for (const auto& m : *setArr->arr) {
            if (!m) continue;
            if (!asvJSONValue::checkObjectSize(obj->obj->size() + 1))
              throw asvJSONError("TRON: set too large");
            auto nullv = asvJSONValue::makeNull();
            if (!nullv) throw asvJSONError("out of memory");
            obj->obj->emplace(m->str_data, std::move(nullv));
          }
          obj->yaml_set = true;
          return obj.release();
        }
      }
    }
    return obj.release();
  }

  asvJSONValue* parseArray(int depth) {
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
      if (arr->size() >= asvJSONValue::MAX_ARRAY_SIZE)
        throw asvJSONError("TRON: array too large");
      asvJSONValue* val = parseValue(depth + 1);
      if (val) arr->arr->push_back(std::unique_ptr<asvJSONValue>(val));
      skipNewlines();
    }
    if (!match(TronTokType::RBRACKET)) throw asvJSONError("TRON: expected ']' or ',' in array");
    return arr.release();
  }

  asvJSONValue* parseInstance(int depth) {
    std::string className = advance().text;
    auto it = classes.find(className);
    if (it == classes.end()) throw asvJSONError("TRON: undefined class: " + className);
    const auto& props = it->second.props;
    if (!match(TronTokType::LPAREN)) throw asvJSONError("TRON: expected '('");
    skipNewlines();
    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("out of memory");
    std::vector<std::unique_ptr<asvJSONValue>> posArgs;
    std::unordered_map<std::string, std::unique_ptr<asvJSONValue>> namedArgs;
    bool namedMode = false;
    bool needsSeparator = false;
    while (peek().type != TronTokType::RPAREN && peek().type != TronTokType::END) {
      if (needsSeparator) {
        if (peek().type == TronTokType::COMMA) { advance(); skipNewlines(); needsSeparator = false; continue; }
        if (peek().type == TronTokType::RPAREN) break;
        throw asvJSONError("TRON: expected ',' or ')'");
      }
      skipNewlines();
      if ((peek().type == TronTokType::IDENT || peek().type == TronTokType::STRING) && pos + 1 < toks.size() && toks[pos + 1].type == TronTokType::EQUALS) {
        namedMode = true;
        std::string propName;
        if (peek().type == TronTokType::STRING) propName = tronUnescape(advance().text);
        else propName = advance().text;
        advance(); // '='
        skipNewlines();
        asvJSONValue* val = parseValue(depth + 1);
        if (namedArgs.find(propName) != namedArgs.end())
          throw asvJSONError("TRON: duplicate argument: " + propName);
        namedArgs[propName] = std::unique_ptr<asvJSONValue>(val);
      } else {
        if (namedMode) throw asvJSONError("TRON: positional arg after named");
        asvJSONValue* val = parseValue(depth + 1);
        posArgs.push_back(std::unique_ptr<asvJSONValue>(val));
      }
      needsSeparator = true;
      skipNewlines();
    }
    if (!match(TronTokType::RPAREN)) throw asvJSONError("TRON: expected ')'");
    // Per spec, argument counts/names must match the class definition.
    // Positional args fill the first posArgs.size() properties in order.
    if (posArgs.size() > props.size())
      throw asvJSONError("TRON: too many arguments for class " + className);
    // Spec: a call with fewer values than the class has properties is a
    // SyntaxError (missing values are not silently filled with null).
    if (posArgs.size() + namedArgs.size() < props.size())
      throw asvJSONError("TRON: not enough arguments for class " + className);
    if (!namedArgs.empty()) {
      std::unordered_set<std::string> propSet;
      propSet.reserve(props.size());
      for (const auto& p : props) propSet.insert(p);
      for (auto& kv : namedArgs) {
        if (propSet.find(kv.first) == propSet.end())
          throw asvJSONError("TRON: unknown argument: " + kv.first);
      }
    }
    for (size_t i = 0; i < posArgs.size(); i++) {
      if (namedArgs.find(props[i]) != namedArgs.end())
        throw asvJSONError("TRON: argument specified twice: " + props[i]);
    }
    for (size_t i = 0; i < props.size(); i++) {
      std::unique_ptr<asvJSONValue> valPtr;
      if (i < posArgs.size()) {
        valPtr = std::move(posArgs[i]);
      } else {
        auto nit = namedArgs.find(props[i]);
        if (nit != namedArgs.end()) valPtr = std::move(nit->second);
      }
      if (!valPtr) {
        // Defensive: unreachable given the checks above. posArgs.size() +
        // namedArgs.size() >= props.size() with every named arg validated as a
        // known property and never colliding with a positional one, so each
        // props[i] is covered by posArgs or namedArgs.
        auto nv = asvJSONValue::makeNull();
        if (!nv) throw asvJSONError("out of memory");
        obj->obj->emplace(props[i], std::move(nv));
      } else {
        obj->obj->emplace(props[i], std::move(valPtr));
      }
    }
    return obj.release();
  }
};

asvJSONValue* TronParseState::parseValue(int depth) {
  skipNewlines();
  if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
    throw asvJSONError("TRON: nesting too deep");
  auto& tok = peek();
  static const double tronNaN = std::numeric_limits<double>::quiet_NaN();
  static const double tronInf = std::numeric_limits<double>::infinity();
  switch (tok.type) {
    case TronTokType::LBRACE: return parseObject(depth);
    case TronTokType::LBRACKET: return parseArray(depth);
    case TronTokType::STRING: {
      std::string raw = advance().text;
      std::string s = tronUnescape(raw);
      // Marker recognition runs on the RAW token text (not the unescaped
      // value): the encoder escapes literal strings that start with a marker
      // prefix as \u005F..., so they stay strings instead of being converted
      // into special types here.
      // Check __TS__ prefix (timestamp stored as a quoted marker)
      if (raw.size() > 6 && raw.compare(0, 6, "__TS__") == 0) {
        long long ts;
        auto [tptr, tec] = std::from_chars(s.data() + 6, s.data() + s.size(), ts);
        if (tec == std::errc() && tptr == s.data() + s.size()) {
          auto v = asvJSONValue::makeTimestamp(static_cast<int64_t>(ts));
          if (!v) throw asvJSONError("out of memory");
          return v.release();
        }
      }
      // Check __OID__ prefix (ObjectId stored as a 24-char hex string)
      if (raw.size() == 31 && raw.compare(0, 7, "__OID__") == 0) {
        // Index the RAW token text (length-31) for the hex pairs: the
        // unescaped `s` may be shorter than raw when escapes were present,
        // which would read past its end. Hex digits never need escaping.
        uint8_t bytes[12];
        bool hexOk = true;
        for (int i = 0; i < 12; i++) {
          char buf[3] = {raw[i * 2 + 7], raw[i * 2 + 8], 0};
          char* end = nullptr;
          long byte = std::strtol(buf, &end, 16);
          if (end != buf + 2) { hexOk = false; break; }
          bytes[i] = static_cast<uint8_t>(byte);
        }
        if (hexOk) {
          auto v = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<char*>(bytes), 12));
          if (!v) throw asvJSONError("out of memory");
          return v.release();
        }
      }
      // Check __REGEX__ prefix: pattern|options|. The encoder emits the pattern and
      // options as strict-JSON escapes with any literal '|' as \u007C, so raw
      // '|' chars only ever separate fields: the first is the pattern/options
      // boundary, the last a mandatory terminator. Splitting on the RAW token
      // text (before tronUnescape) keeps those separators distinguishable from
      // \u007C-escaped pipes; each side is then unescaped independently.
      if (raw.size() >= 9 && raw.compare(0, 9, "__REGEX__") == 0) {
        std::string content(raw.data() + 9, raw.size() - 9);
        if (!content.empty()) {
          size_t term = content.rfind('|');
          if (term != 0) {
            size_t sep = content.find('|');
            std::string pat = tronUnescape(content.substr(0, sep));
            std::string opts = (sep != term) ? tronUnescape(content.substr(sep + 1, term - sep - 1)) : std::string();
            auto v = asvJSONValue::makeRegex(pat.c_str(), opts.empty() ? nullptr : opts.c_str());
            if (!v) throw asvJSONError("out of memory");
            return v.release();
          }
          // No terminator: legacy format -- the whole content is the pattern
          // (the old encoder emitted no separator when options were empty).
          std::string pat = tronUnescape(content);
          auto v = asvJSONValue::makeRegex(pat.c_str(), nullptr);
          if (!v) throw asvJSONError("out of memory");
          return v.release();
        }
        // An empty pattern cannot be a REGEX (makeRegex rejects it): fall
        // through so a literal "__REGEX__" string stays a plain STRING.
      }
      // Check __BASE64__ prefix (payload may be empty)
      if (raw.size() >= 10 && raw.compare(0, 10, "__BASE64__") == 0) {
        auto data = decodeBase64Fast(s.data() + 10, s.size() - 10);
        auto v = asvJSONValue::makeBinary(data.data(), data.size());
        if (!v) throw asvJSONError("out of memory");
        return v.release();
      }
      // Check __EXT__ prefix: "__EXT__<type>__<base64>". An unparsable type
      // falls through to a plain string instead of rejecting the whole doc.
      if (raw.size() > 7 && raw.compare(0, 7, "__EXT__") == 0) {
        size_t sep = s.find("__", 7);
        if (sep != std::string::npos && sep > 7) {
          int extType;
          auto [ptr, ec] = std::from_chars(s.data() + 7, s.data() + sep, extType);
          if (ec == std::errc() && ptr == s.data() + sep) {
            // ext_type is a signed byte; from_chars parses into an int, so an
            // out-of-range value (e.g. __EXT__300__...) would silently wrap via
            // static_cast<int8_t>. Reject it instead (same guard as ADE).
            if (extType < -128 || extType > 127)
              throw asvJSONError("TRON: extension type out of range [-128, 127]");
            auto data = decodeBase64Fast(s.data() + sep + 2, s.size() - sep - 2);
            auto v = asvJSONValue::makeExtension(static_cast<int8_t>(extType), data.data(), data.size());
            if (!v) throw asvJSONError("out of memory");
            return v.release();
          }
        }
      }
      // Check ISO 8601 date. Skipped when the raw token starts with a backslash:
      // the encoder escapes the leading character of an ISO-shaped literal
      // STRING (\u0032...), which is how such a string keeps its type; an
      // unescaped DATETIME is emitted in plain ISO form and still detects here.
      if (!raw.empty() && raw[0] != '\\' && tronLooksLikeDateTime(s)) {
        time_t ts;
        int ms = 0;
        int tz = 0;
        if (tryParseDateTime(s, ts, &ms, &tz)) {
          auto v = asvJSONValue::makeDateTime(ts, ms, tz);
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
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
        auto [ptr, ec] = std::from_chars(n.data(), n.data() + n.size(), d);
        if (ec != std::errc() || ptr != n.data() + n.size()) throw asvJSONError("TRON: invalid number: " + n);
#else
        char* end;
        errno = 0;
        d = std::strtod(n.c_str(), &end);
        if (errno == ERANGE || end != n.c_str() + n.size()) throw asvJSONError("TRON: invalid number: " + n);
#endif
        auto v = asvJSONValue::makeDouble(d);
        if (!v) throw asvJSONError("out of memory");
        return v.release();
      } else {
        long long l;
        auto [ptr, ec] = std::from_chars(n.data(), n.data() + n.size(), l);
        if (ec != std::errc() || ptr != n.data() + n.size()) throw asvJSONError("TRON: invalid number: " + n);
        auto v = asvJSONValue::makeInt(l);
        if (!v) throw asvJSONError("out of memory");
        return v.release();
      }
    }
    case TronTokType::TRUE: advance(); { auto v = asvJSONValue::makeBool(true); if (!v) throw asvJSONError("out of memory"); return v.release(); }
    case TronTokType::FALSE: advance(); { auto v = asvJSONValue::makeBool(false); if (!v) throw asvJSONError("out of memory"); return v.release(); }
    case TronTokType::NUL: advance(); { auto v = asvJSONValue::makeNull(); if (!v) throw asvJSONError("out of memory"); return v.release(); }
    case TronTokType::NAN_VAL: advance(); { auto v = asvJSONValue::makeDouble(tronNaN); if (!v) throw asvJSONError("out of memory"); return v.release(); }
    case TronTokType::INF_VAL: {
      bool isNeg = tok.text.size() > 0 && tok.text[0] == '-';
      advance();
      auto v = asvJSONValue::makeDouble(isNeg ? -tronInf : tronInf);
      if (!v) throw asvJSONError("out of memory");
      return v.release();
    }
    case TronTokType::IDENT: {
      if (pos + 1 < toks.size() && toks[pos + 1].type == TronTokType::LPAREN)
        return parseInstance(depth);
      throw asvJSONError("TRON: unexpected identifier: " + tok.text);
    }
    default:
      throw asvJSONError("TRON: unexpected token");
  }
}

inline bool asvJSON::fromTRON(std::string_view input) {
  dirty_ = false;
  formatType = FormatType::Unknown;
  if (input.empty()) { lastError = "empty input"; root = nullptr; return false; }
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
      // Parse property list. Separators are commas or newlines. A newline may
      // either separate two properties or mark the end of the class definition
      // (the data section follows); propAhead() disambiguates, so an identifier
      // that is really a class instantiation is never treated as a property.
      std::vector<std::string> props;
      while (state.propAhead()) {
        if (state.peek().type == TronTokType::STRING) {
          props.push_back(tronUnescape(state.advance().text));
        } else {
          props.push_back(state.advance().text);
        }
        if (state.peek().type == TronTokType::COMMA) { state.advance(); state.skipNewlines(); }
        else if (state.peek().type == TronTokType::NEWLINE) { state.advance(); state.skipNewlines(); }
        else if (state.peek().type == TronTokType::SEMICOLON) { state.advance(); state.skipNewlines(); break; }
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
    state.skipNewlines();
    if (state.peek().type != TronTokType::END)
      throw asvJSONError("trailing tokens after root value");
    root.reset(state.root);
    formatType = FormatType::TRON;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromMessagePack).
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
