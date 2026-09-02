#pragma once
// TOML serialization/parsing for asvJSON++

#include "../core.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace asvJSONInternal {

inline std::string tomlEscapeStr(std::string_view s) {
  std::string out;
  out += '"';
  for (auto c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
  return out;
}

// JSON-escape a string for use in intermediate JSON output
inline std::string tomlJsonEscape(std::string_view s) {
  std::string out;
  for (auto c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// Convert Unicode codepoint to UTF-8
inline void appendCodepoint(unsigned long cp, std::string& out) {
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
  else if (cp < 0x110000) { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

// Parse a Unicode escape: \uXXXX (hexDigits=4) or \UXXXXXXXX (hexDigits=8).
// `s` is positioned just after the 'u'/'U'. Throws asvJSONError on malformed
// input (bad/truncated hex digit, out-of-range, or an unpaired surrogate).
inline void consumeUnicodeEscape(std::string_view& s, int hexDigits, std::string& out) {
  auto readHex = [&]() -> int {
    if (s.empty()) throw asvJSONError("truncated unicode escape in string");
    char hc = s[0]; s.remove_prefix(1);
    if (hc >= '0' && hc <= '9') return hc - '0';
    if (hc >= 'a' && hc <= 'f') return hc - 'a' + 10;
    if (hc >= 'A' && hc <= 'F') return hc - 'A' + 10;
    throw asvJSONError("invalid hex digit in unicode escape");
  };
  unsigned long cp = 0;
  for (int i = 0; i < hexDigits; i++)
    cp = cp * 16 + static_cast<unsigned long>(readHex());
  if (hexDigits == 4 && cp >= 0xD800 && cp <= 0xDBFF) {
    // High surrogate: require a following \uDC00-\uDFFF low surrogate
    if (s.size() >= 2 && s[0] == '\\' && s[1] == 'u') {
      std::string_view ls = s; ls.remove_prefix(2);
      unsigned long low = 0; bool lok = true;
      for (int i = 0; i < 4 && lok; i++) {
        if (ls.empty()) { lok = false; break; }
        char hc = ls[0]; ls.remove_prefix(1);
        if (hc >= '0' && hc <= '9') low = low * 16 + (hc - '0');
        else if (hc >= 'a' && hc <= 'f') low = low * 16 + (hc - 'a' + 10);
        else if (hc >= 'A' && hc <= 'F') low = low * 16 + (hc - 'A' + 10);
        else lok = false;
      }
      if (lok && low >= 0xDC00 && low <= 0xDFFF) {
        unsigned long combined = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        appendCodepoint(combined, out);
        s.remove_prefix(6); // \u + 4 hex digits of the low surrogate
        return;
      }
    }
    throw asvJSONError("unpaired high surrogate in unicode escape");
  }
  if (cp >= 0xD800 && cp <= 0xDFFF)
    throw asvJSONError("unpaired surrogate in unicode escape");
  if (cp > 0x10FFFF)
    throw asvJSONError("unicode escape out of range");
  appendCodepoint(cp, out);
}

// Quote a TOML key if not a valid bare key
inline std::string tomlQuoteKey(const std::string& key) {
  if (key.empty()) return "\"\"";
  bool bare = true;
  for (char c : key) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
      bare = false;
      break;
    }
  }
  if (bare) return key;
  std::string out = "\"";
  for (char c : key) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out += c;
  }
  out += '"';
  return out;
}

inline std::string tomlVal(const asvJSONValue* v) {
  if (!v) return "";
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::NULL_VAL: return "";
    case T::BOOL_VAL: return v->flag ? "true" : "false";
    case T::INT: return std::to_string(v->num);
    case T::DOUBLE: {
      if (std::isnan(v->dbl)) return "nan";
      if (std::isinf(v->dbl)) return v->dbl > 0 ? "inf" : "-inf";
      std::string s;
      fmtDoubleVal(v->dbl, s);
      return s;
    }
    case T::STRING: return tomlEscapeStr(v->str_data);
    case T::DATETIME: {
      std::string s;
      fmtDateTimeVal(v->timestamp, v->datetime_ms, s, v->tz_offset_min);
      return s;
    }
    case T::BINARY: return "\"" + encodeBase64(v->bin_data.data(), v->bin_data.size()) + "\"";
    case T::ARRAY: {
      if (!v->arr) return "[]";
      std::string out = "[";
      bool first = true;
      for (size_t i = 0; i < v->arr->size(); i++) {
        std::string elem = tomlVal((*v->arr)[i].get());
        if (elem.empty()) continue;
        if (!first) out += ", ";
        first = false;
        out += elem;
      }
      out += "]";
      return out;
    }
    case T::OBJECT: {
      if (!v->obj || v->obj->empty()) return "{}";
      std::string out = "{";
      bool first = true;
      for (const auto& [k, val] : *v->obj) {
        std::string vs = tomlVal(val.get());
        if (vs.empty()) continue;
        if (!first) out += ", ";
        first = false;
        out += tomlQuoteKey(k) + " = " + vs;
      }
      out += "}";
      return out;
    }
    default: return "";
  }
}

// Trim whitespace (spaces, tabs, \r, \n)
inline std::string_view trimSV(std::string_view s) {
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r' || s[0] == '\n')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
  return s;
}

// Strip comment (#) from line, respecting quoted strings
inline std::string_view stripComment(std::string_view line, bool& inBasic, bool& inLiteral) {
  bool escape = false;
  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    if (escape) { escape = false; continue; }
    if (c == '\\' && inBasic) { escape = true; continue; }
if (c == '"' && !inLiteral) { inBasic = !inBasic; continue; }
    if (c == '\'' && !inBasic) { inLiteral = !inLiteral; continue; }
    if (!inBasic && !inLiteral && c == '#') return line.substr(0, i);
  }
  return line;
}

// Parse a single TOML key (bare or quoted), advances s past it
inline std::string parseTomlKey(std::string_view& s) {
  s = trimSV(s);
  if (s.empty()) return "";
  std::string key;
  if (s[0] == '"') {
    s.remove_prefix(1);
    bool escape = false;
    for (;;) {
      if (s.empty()) return key;
      char c = s[0]; s.remove_prefix(1);
      if (escape) { escape = false;
        if (c == 'n') key += '\n';
        else if (c == 't') key += '\t';
        else if (c == 'r') key += '\r';
        else if (c == '"') key += '"';
        else if (c == '\\') key += '\\';
        else if (c == 'b') key += '\b';
        else if (c == 'f') key += '\f';
        else if (c == 'u') { consumeUnicodeEscape(s, 4, key); }
        else if (c == 'U') { consumeUnicodeEscape(s, 8, key); }
        else key += c;
        continue;
      }
      if (c == '\\') { escape = true; continue; }
      if (c == '"') break;
      key += c;
    }
  } else if (s[0] == '\'') {
    s.remove_prefix(1);
    for (;;) {
      if (s.empty()) return key;
      char c = s[0]; s.remove_prefix(1);
      if (c == '\'') break;
      key += c;
    }
  } else {
    // Bare key: ASCII alnum, '_', '-' (a leading/trailing high-bit byte is
    // tolerated so non-ASCII UTF-8 keys like `ключ` parse leniently)
    while (!s.empty() &&
           (std::isalnum(static_cast<unsigned char>(s[0])) || s[0] == '_' || s[0] == '-' ||
            (static_cast<unsigned char>(s[0]) & 0x80))) {
      key += s[0]; s.remove_prefix(1);
    }
  }
  return key;
}

// Parse dotted key path into segments
inline std::vector<std::string> parseTomlKeyPath(std::string_view& s) {
  std::vector<std::string> keys;
  while (!s.empty()) {
    s = trimSV(s);
    if (s.empty()) break;
    if (s[0] == '.') throw asvJSONError("empty key segment in dotted key path");
    bool quotedStart = (!s.empty() && (s[0] == '"' || s[0] == '\''));
    std::string key = parseTomlKey(s);
    if (!quotedStart && key.empty())
      throw asvJSONError("invalid key: unexpected character");
    if (quotedStart || !key.empty()) keys.push_back(std::move(key));
    s = trimSV(s);
    if (!s.empty() && s[0] == '.') {
      s.remove_prefix(1);
      s = trimSV(s);
      // A dot must be followed by a non-empty key segment
      if (s.empty() || s[0] == '.' || s[0] == ']' || s[0] == '=' || s[0] == ',')
        throw asvJSONError("empty key segment in dotted key path");
      continue;
    }
    // Keys may be followed only by a separating '=', a table/array closer, or
    // a comma/brace in an inline table -- anything else is junk like `aé` or
    // `"a"x` (previously such lines were silently dropped).
    if (!s.empty() && s[0] != '=' && s[0] != ']' && s[0] != ',' && s[0] != '}')
      throw asvJSONError("invalid character after key");
    break;
  }
  return keys;
}

// Helper: parse a single JSON value string into an asvJSONValue
inline std::unique_ptr<asvJSONValue> tomlParseJsonValue(const std::string& jsonVal) {
  asvJSON tmp;
  tmp.allowNaNInfinity = true;
  if (!tmp.parse(std::string_view("{\"_\":" + jsonVal + "}"))) {
    throw asvJSONError("invalid TOML value");
  }
  return cloneValue(tmp.getRoot()->get("_"));
}

// Quote-aware scan that reports whether the array opened by the first '['
// is already balanced on this string. Used to gather multiline array value
// lines (`arr = [\n1,\n2]`) until the brackets close.
inline bool tomlArrayBalanced(std::string_view s) {
  int depth = 0;
  size_t i = 0;
  while (i < s.size()) {
    char c = s[i];
    if (c == '"' || c == '\'') {
      char q = c;
      bool ml = (i + 2 < s.size() && s[i + 1] == q && s[i + 2] == q);
      i += ml ? 3 : 1;
      bool escaped = false;
      while (i < s.size()) {
        char d = s[i];
        if (escaped) { escaped = false; i++; continue; }
        if (q == '"' && d == '\\') { escaped = true; i++; continue; }
        if (d == q) {
          if (ml) {
            size_t run = 0;
            while (i + run < s.size() && s[i + run] == q) run++;
            if (run >= 3) { i += run; break; }
            i++;
            continue;
          }
          i++;
          break;
        }
        i++;
      }
      continue;
    }
    if (c == '#') {
      while (i < s.size() && s[i] != '\n' && s[i] != '\r') i++;
      continue;
    }
    if (c == '[') depth++;
    else if (c == ']') {
      depth--;
      if (depth <= 0) return true;
    }
    i++;
  }
  return false;
}

// Parse a complete TOML value into JSON string
constexpr int MAX_TOML_DEPTH = 64;

inline std::string parseTomlValue(std::string_view s, int depth = 0, bool strictTail = false) {
  s = trimSV(s);
  if (depth > MAX_TOML_DEPTH) throw asvJSONError("TOML value nesting too deep");
  if (s.empty()) return "null";
  auto rejectTrailing = [&]() {
    if (strictTail && !trimSV(s).empty())
      throw asvJSONError("trailing content after value");
  };
  auto checkTailLen = [&](size_t n) {
    if (strictTail && !trimSV(s.substr(n)).empty())
      throw asvJSONError("trailing content after value");
  };

  // Inline table
  if (s[0] == '{') {
    s.remove_prefix(1);
    auto obj = asvJSONValue::makeObject();
    std::unordered_set<std::string> seenKeys;
    bool closed = false;
    while (!s.empty()) {
      s = trimSV(s);
      if (s.empty()) break;
      if (s[0] == '}') { closed = true; s.remove_prefix(1); break; }
      if (s[0] == ',') {
        // Separator between entries
        s.remove_prefix(1);
        s = trimSV(s);
        if (s.empty() || s[0] == '}')
          throw asvJSONError("trailing comma in inline table");
      }
      auto keys = parseTomlKeyPath(s);
      s = trimSV(s);
      if (!s.empty() && s[0] == '=') { s.remove_prefix(1); s = trimSV(s); }
      int bracketDepth = 0;
      std::string valStr;
      bool commaBreak = false;
      bool inBasic = false, inLiteral = false, escape = false;
      while (!s.empty()) {
        char c = s[0]; s.remove_prefix(1);
        if (escape) { escape = false; valStr += c; continue; }
        if (c == '\\' && inBasic) { escape = true; valStr += c; continue; }
        if (c == '"' && !inLiteral) { inBasic = !inBasic; valStr += c; continue; }
        if (c == '\'' && !inBasic) { inLiteral = !inLiteral; valStr += c; continue; }
        if (c == '\n' || c == '\r')
          throw asvJSONError("newline not allowed in inline table");
        if (!inBasic && !inLiteral) {
          if (c == '{' || c == '[') bracketDepth++;
          if (c == '}' || c == ']') bracketDepth--;
          if (c == ',' && bracketDepth == 0) { commaBreak = true; break; }
        }
        valStr += c;
        // The value ends when an unquoted '}' at depth 0 follows; leave that
        // table closer for the outer loop (it must not be swallowed here).
        if (!inBasic && !inLiteral && !escape && !s.empty() && s[0] == '}' && bracketDepth == 0)
          break;
      }
      std::string valJson = parseTomlValue(trimSV(std::string_view(valStr)), depth + 1, true);
      if (!keys.empty()) {
        std::string fullKey;
        for (size_t ki = 0; ki < keys.size(); ki++) {
          if (ki > 0) fullKey += '.';
          fullKey += keys[ki];
        }
        if (!seenKeys.insert(fullKey).second)
          throw asvJSONError("duplicate key '" + fullKey + "' in inline table");

        // Build nested object tree to avoid duplicate-key JSON output for dotted keys
        auto val = tomlParseJsonValue(valJson);
        if (val) {
          asvJSONValue* cur = obj.get();
          for (size_t i = 0; i < keys.size() - 1; i++) {
            auto it = cur->obj->find(keys[i]);
            if (it == cur->obj->end()) {
              auto newObj = asvJSONValue::makeObject();
              auto* raw = newObj.get();
              cur->obj->emplace(keys[i], std::move(newObj));
              cur = raw;
            } else {
              if (it->second->type != asvJSONValue::OBJECT)
                throw asvJSONError("key '" + keys[i] + "' is not a table in inline table");
              cur = it->second.get();
            }
          }
          if (cur->obj->find(keys.back()) != cur->obj->end())
            throw asvJSONError("duplicate key '" + keys.back() + "' in inline table");
          cur->obj->emplace(keys.back(), std::move(val));
        }
      }
      s = trimSV(s);
      if (commaBreak) {
        if (s.empty() || s[0] == '}')
          throw asvJSONError("trailing comma in inline table");
      } else if (!s.empty() && s[0] != '}') {
        throw asvJSONError("expected ',' or '}' in inline table");
      }
    }
    if (!closed) throw asvJSONError("unterminated inline table");
    rejectTrailing();
    std::string json;
    obj->serialize(json, true);
    return json;
  }

  // Multi-line basic string """..."""
  if (s.size() >= 3 && s[0] == '"' && s[1] == '"' && s[2] == '"') {
    s.remove_prefix(3);
    std::string val;
    // Trim leading newline(s) per TOML spec
    if (!s.empty() && (s[0] == '\n' || s[0] == '\r')) {
      s.remove_prefix(1);
      if (!s.empty() && s[0] == '\n') s.remove_prefix(1);
    }
    bool escape = false;
    int qc = 0;
    for (;;) {
      if (s.empty()) throw asvJSONError("unterminated string");
      char c = s[0]; s.remove_prefix(1);
      if (escape) { escape = false;
        if (c == 'n') val += '\n'; else if (c == 't') val += '\t';
        else if (c == 'r') val += '\r'; else if (c == '"') val += '"';
        else if (c == '\\') val += '\\'; else if (c == 'b') val += '\b';
        else if (c == 'f') val += '\f';
        else if (c == 'u') { consumeUnicodeEscape(s, 4, val); }
        else if (c == 'U') { consumeUnicodeEscape(s, 8, val); }
        else throw asvJSONError("unknown escape sequence in string");
        continue;
      }
      if (c == '\\') {
        // Check for line continuation (backslash at end of line)
        size_t skip = 0;
        while (skip < s.size() && (s[skip] == ' ' || s[skip] == '\t')) skip++;
        if (skip < s.size() && (s[skip] == '\n' || s[skip] == '\r')) {
          if (s[skip] == '\r' && skip + 1 < s.size() && s[skip + 1] == '\n') skip++;
          skip++;
          while (skip < s.size() && (s[skip] == ' ' || s[skip] == '\t' || s[skip] == '\n' || s[skip] == '\r')) skip++;
          s.remove_prefix(skip);
          continue;
        }
        escape = true; continue;
      }
      if (c == '"') { qc++; if (qc >= 3) break; continue; }
      qc = 0;
      val += c;
    }
    rejectTrailing();
    return "\"" + tomlJsonEscape(val) + "\"";
  }

  // Multi-line literal string '''...'''
  if (s.size() >= 3 && s[0] == '\'' && s[1] == '\'' && s[2] == '\'') {
    s.remove_prefix(3);
    // Trim leading newline(s) per TOML spec
    if (!s.empty() && (s[0] == '\n' || s[0] == '\r')) {
      s.remove_prefix(1);
      if (!s.empty() && s[0] == '\n') s.remove_prefix(1);
    }
    std::string val;
    int qc = 0;
    for (;;) {
      if (s.empty()) throw asvJSONError("unterminated string");
      char c = s[0]; s.remove_prefix(1);
      if (c == '\'') { qc++; if (qc >= 3) break; continue; }
      qc = 0;
      val += c;
    }
    rejectTrailing();
    return "\"" + tomlJsonEscape(val) + "\"";
  }

  // Inline array
  if (s[0] == '[') {
    s.remove_prefix(1);
    std::string json = "[";
    bool first = true;
    int bracketDepth = 0;
    std::string cur;
    bool inBasic = false, inLiteral = false, escape = false;
    bool multi = s.find('\n') != std::string_view::npos || s.find('\r') != std::string_view::npos;
    bool closed = false;
    auto flushVal = [&]() {
      std::string trimmed = std::string(trimSV(std::string_view(cur)));
      if (!trimmed.empty()) {
        if (!first) json += ',';
        first = false;
        json += parseTomlValue(std::string_view(trimmed), depth + 1);
      }
      cur.clear();
    };
    auto skipToEol = [&]() {
      const char* p = s.data();
      const char* e = s.data() + s.size();
      while (p < e && *p != '\n' && *p != '\r') p++;
      s = std::string_view(p, static_cast<size_t>(e - p));
    };
    while (!s.empty()) {
      char c = s[0]; s.remove_prefix(1);
      if (escape) { escape = false; cur += c; continue; }
      if (c == '\\' && inBasic) { escape = true; cur += c; continue; }
      if (c == '"' && !inLiteral) { inBasic = !inBasic; cur += c; continue; }
      if (c == '\'' && !inBasic) { inLiteral = !inLiteral; cur += c; continue; }
      if (!inBasic && !inLiteral) {
        if (c == '#') { skipToEol(); continue; }
        // Newlines separate elements in a multiline array (comma optional)
        if (c == '\n' || c == '\r') { flushVal(); continue; }
        if (c == '[') { bracketDepth++; cur += c; continue; }
        if (c == ']') {
          if (bracketDepth == 0) {
            // A trailing comma is only legal in a multiline array
            if (trimSV(std::string_view(cur)).empty() && !first && !multi)
              throw asvJSONError("trailing comma in inline array");
            flushVal();
            closed = true;
            break;
          }
          bracketDepth--; cur += c; continue;
        }
        if (c == ',' && bracketDepth == 0) {
          // Consecutive / leading comma with no element is invalid
          if (trimSV(std::string_view(cur)).empty())
            throw asvJSONError("missing element in inline array");
          flushVal();
          continue;
        }
        if (c == '{' || c == '}') { bracketDepth = (c == '{') ? bracketDepth + 1 : bracketDepth - 1; cur += c; continue; }
      }
      cur += c;
    }
    if (!closed) throw asvJSONError("unterminated array");
    // Only whitespace or a trailing comment may follow the closing bracket
    // (a '#' comment can only reach here via gathered continuation lines)
    for (;;) {
      s = trimSV(s);
      if (s.empty()) break;
      if (s[0] == '#') {
        while (!s.empty() && s[0] != '\n' && s[0] != '\r') s.remove_prefix(1);
        continue;
      }
      throw asvJSONError("trailing content after value");
    }
    json += "]";
    return json;
  }

  // Boolean
  if (s.substr(0, 4) == "true" && (s.size() == 4 || s[4] == ',' || s[4] == ']' || s[4] == '}' || s[4] == '#' || s[4] == ' ' || s[4] == '\t')) {
    checkTailLen(4);
    return "true";
  }
  if (s.substr(0, 5) == "false" && (s.size() == 5 || s[5] == ',' || s[5] == ']' || s[5] == '}' || s[5] == '#' || s[5] == ' ' || s[5] == '\t')) {
    checkTailLen(5);
    return "false";
  }

  // Date/time (optionally space-separated local datetime)
  if (s.size() >= 10 && std::isdigit(static_cast<unsigned char>(s[0])) &&
      s[4] == '-' && s[7] == '-') {
    auto isDt = [&](char ch) {
      return std::isdigit(static_cast<unsigned char>(ch)) || ch == 'T' || ch == ':' ||
             ch == '.' || ch == 'Z' || ch == '+' || ch == '-';
    };
    size_t end = 10;
    while (end < s.size() && isDt(s[end])) end++;
    if (end < s.size() && s[end] == ' ' && end + 1 < s.size() &&
        std::isdigit(static_cast<unsigned char>(s[end + 1]))) {
      end++;
      while (end < s.size() && isDt(s[end])) end++;
    }
    checkTailLen(end);
    return "\"" + std::string(s.substr(0, end)) + "\"";
  }

  // Local time: HH:MM:SS[.ffffff]
  if (s.size() >= 8 && std::isdigit(static_cast<unsigned char>(s[0])) &&
      std::isdigit(static_cast<unsigned char>(s[1])) && s[2] == ':' &&
      std::isdigit(static_cast<unsigned char>(s[3])) && std::isdigit(static_cast<unsigned char>(s[4])) && s[5] == ':' &&
      std::isdigit(static_cast<unsigned char>(s[6])) && std::isdigit(static_cast<unsigned char>(s[7]))) {
    size_t end = 8;
    while (end < s.size() && s[end] == '.') {
      end++;
      while (end < s.size() && std::isdigit(static_cast<unsigned char>(s[end]))) end++;
    }
    checkTailLen(end);
    return "\"" + std::string(s.substr(0, end)) + "\"";
  }

  // Basic string "..."
  if (s[0] == '"') {
    s.remove_prefix(1);
    std::string val;
    bool escape = false;
    for (;;) {
      if (s.empty()) throw asvJSONError("unterminated string");
      char c = s[0]; s.remove_prefix(1);
      if (escape) { escape = false;
        if (c == 'n') val += '\n'; else if (c == 't') val += '\t';
        else if (c == 'r') val += '\r'; else if (c == '"') val += '"';
        else if (c == '\\') val += '\\'; else if (c == 'b') val += '\b';
        else if (c == 'f') val += '\f';
        else if (c == 'u') { consumeUnicodeEscape(s, 4, val); }
        else if (c == 'U') { consumeUnicodeEscape(s, 8, val); }
        else throw asvJSONError("unknown escape sequence in string");
        continue;
      }
      if (c == '\\') { escape = true; continue; }
      if (c == '"') break;
      if ((static_cast<unsigned char>(c) < 0x20 && c != '\t') || static_cast<unsigned char>(c) == 0x7F)
        throw asvJSONError("raw control character in basic string");
      val += c;
    }
    rejectTrailing();
    return "\"" + tomlJsonEscape(val) + "\"";
  }

  // Literal string '...'
  if (s[0] == '\'') {
    s.remove_prefix(1);
    std::string val;
    for (;;) {
      if (s.empty()) throw asvJSONError("unterminated string");
      char c = s[0]; s.remove_prefix(1);
      if (c == '\'') break;
      if ((static_cast<unsigned char>(c) < 0x20 && c != '\t') || static_cast<unsigned char>(c) == 0x7F)
        throw asvJSONError("raw control character in literal string");
      val += c;
    }
    rejectTrailing();
    return "\"" + tomlJsonEscape(val) + "\"";
  }

  // Hex, octal, binary
  if (s.size() >= 2 && s[0] == '0') {
    auto validBaseTail = [&](size_t idx) {
      if (idx < s.size()) {
        char d = s[idx];
        if (d != ' ' && d != '\t' && d != ',' && d != ']' && d != '}' && d != '#' && d != '\r' && d != '\n')
          throw asvJSONError("invalid character after integer");
      }
      checkTailLen(idx);
    };
    if (s[1] == 'x' || s[1] == 'X') {
      uint64_t hv = 0; size_t pos = 0; size_t idx = 2;
      bool prevDigit = false;
      while (idx < s.size() && (std::isxdigit(static_cast<unsigned char>(s[idx])) || s[idx] == '_')) {
        char c = s[idx];
        if (c == '_') {
          // Underscores must be flanked by digits
          if (!prevDigit || idx + 1 >= s.size() || !std::isxdigit(static_cast<unsigned char>(s[idx + 1])))
            throw asvJSONError("invalid underscore in hex integer");
        } else {
          prevDigit = true;
          hv = hv * 16 + ((c <= '9') ? (c - '0') : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10)); pos++;
        }
        idx++;
      }
      if (pos == 0) throw asvJSONError("invalid hex integer");
      validBaseTail(idx);
      if (hv > static_cast<uint64_t>(INT64_MAX))
        throw asvJSONError("hex integer out of range");
      return std::to_string(static_cast<long long>(hv));
    }
    if (s[1] == 'o' || s[1] == 'O') {
      uint64_t ov = 0; size_t pos = 0; size_t idx = 2;
      bool prevDigit = false;
      while (idx < s.size() && ((s[idx] >= '0' && s[idx] <= '7') || s[idx] == '_')) {
        char c = s[idx];
        if (c == '_') {
          if (!prevDigit || idx + 1 >= s.size() || s[idx + 1] < '0' || s[idx + 1] > '7')
            throw asvJSONError("invalid underscore in octal integer");
        } else {
          prevDigit = true;
          ov = ov * 8 + (c - '0'); pos++;
        }
        idx++;
      }
      if (pos == 0) throw asvJSONError("invalid octal integer");
      validBaseTail(idx);
      if (ov > static_cast<uint64_t>(INT64_MAX))
        throw asvJSONError("octal integer out of range");
      return std::to_string(static_cast<long long>(ov));
    }
    if (s[1] == 'b' || s[1] == 'B') {
      uint64_t bv = 0; size_t pos = 0; size_t idx = 2;
      bool prevDigit = false;
      while (idx < s.size() && (s[idx] == '0' || s[idx] == '1' || s[idx] == '_')) {
        char c = s[idx];
        if (c == '_') {
          if (!prevDigit || idx + 1 >= s.size() || (s[idx + 1] != '0' && s[idx + 1] != '1'))
            throw asvJSONError("invalid underscore in binary integer");
        } else {
          prevDigit = true;
          bv = bv * 2 + (c - '0'); pos++;
        }
        idx++;
      }
      if (pos == 0) throw asvJSONError("invalid binary integer");
      validBaseTail(idx);
      if (bv > static_cast<uint64_t>(INT64_MAX))
        throw asvJSONError("binary integer out of range");
      return std::to_string(static_cast<long long>(bv));
    }
  }

  // Number / float / inf / nan
  std::string numStr;
  size_t pos = 0;
  if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) { numStr += s[pos]; pos++; }
  auto isDelim = [&](size_t next) -> bool {
    return next >= s.size() || s[next] == ' ' || s[next] == '\t' || s[next] == ',' || s[next] == ']' || s[next] == '}' || s[next] == '#' || s[next] == '\r' || s[next] == '\n';
  };
  // inf / nan are case-sensitive lowercase literals per the TOML spec
  if (s.substr(pos, 3) == "inf" && isDelim(pos + 3)) {
    checkTailLen(pos + 3);
    return (numStr == "-") ? "-Infinity" : "Infinity";
  }
  if (s.substr(pos, 3) == "nan" && isDelim(pos + 3)) {
    checkTailLen(pos + 3);
    return (numStr == "-") ? "-NaN" : "NaN";
  }
  if (pos >= s.size()) {
    if (numStr.empty()) return "null";
    throw asvJSONError("invalid number: missing digits after sign");
  }
  bool isFloat = false;
  bool inExp = false;
  std::string raw;
  while (pos < s.size()) {
    char c = s[pos];
    bool isDigitC = (c >= '0' && c <= '9');
    bool isE = (c == 'e' || c == 'E');
    if (isDigitC || c == '.' || c == '_' || isE ||
        ((c == '+' || c == '-') && !raw.empty() && (raw.back() == 'e' || raw.back() == 'E'))) {
      // Underscores are only permitted within the integer/fractional parts,
      // never in the exponent or right after a sign/separator.
      if (c == '_' && (inExp || raw.empty() || raw.back() == 'e' || raw.back() == 'E' ||
                       raw.back() == '+' || raw.back() == '-' || raw.back() == '.'))
        throw asvJSONError("invalid underscore in number");
      if (isE) { isFloat = true; inExp = true; }
      else if (c == '.') isFloat = true;
      raw += c;
      pos++;
    } else {
      break;
    }
  }
  if (raw.empty()) throw asvJSONError("invalid number");
  std::string tok = numStr + raw;
  // Underscores must be flanked by digits
  for (size_t i = 0; i < tok.size(); i++) {
    if (tok[i] == '_' && (i == 0 || i + 1 >= tok.size() ||
                          !std::isdigit(static_cast<unsigned char>(tok[i - 1])) ||
                          !std::isdigit(static_cast<unsigned char>(tok[i + 1]))))
      throw asvJSONError("invalid underscore in number");
  }
  std::string clean;
  clean.reserve(tok.size());
  for (char c : tok) if (c != '_') clean += c;
  size_t b = 0;
  if (!clean.empty() && (clean[0] == '-' || clean[0] == '+')) b = 1;
  std::string body = clean.substr(b);
  if (body.empty()) throw asvJSONError("invalid number");
  bool isFloatNum = isFloat || body.find_first_of(".eE") != std::string::npos;
  if (isFloatNum) {
    size_t p = 0;
    size_t ipStart = p;
    while (p < body.size() && std::isdigit(static_cast<unsigned char>(body[p]))) p++;
    if (p == ipStart) throw asvJSONError("invalid float: missing integer part");
    if (body[ipStart] == '0' && p - ipStart > 1)
      throw asvJSONError("leading zeros are not allowed in numbers");
    if (p < body.size() && body[p] == '.') {
      p++;
      size_t fpStart = p;
      while (p < body.size() && std::isdigit(static_cast<unsigned char>(body[p]))) p++;
      if (p == fpStart) throw asvJSONError("invalid float: missing fractional part");
    }
    if (p < body.size() && (body[p] == 'e' || body[p] == 'E')) {
      p++;
      if (p < body.size() && (body[p] == '+' || body[p] == '-')) p++;
      size_t epStart = p;
      while (p < body.size() && std::isdigit(static_cast<unsigned char>(body[p]))) p++;
      if (p == epStart) throw asvJSONError("invalid float: missing exponent digits");
    }
    if (p != body.size()) throw asvJSONError("invalid float");
  } else {
    size_t p = 0;
    while (p < body.size() && std::isdigit(static_cast<unsigned char>(body[p]))) p++;
    if (p != body.size()) throw asvJSONError("invalid number");
    if (body[0] == '0' && p > 1) throw asvJSONError("leading zeros are not allowed in numbers");
  }
  // Reject numeric overflow (exponent / out-of-range integer)
  errno = 0;
  {
    char* endp = nullptr;
    if (isFloatNum) {
      double dv = std::strtod(clean.c_str(), &endp);
      if (errno == ERANGE || std::isinf(dv))
        throw asvJSONError("number out of range");
    } else {
      std::strtoll(clean.c_str(), &endp, 10);
      if (errno == ERANGE)
        throw asvJSONError("integer out of range");
    }
  }
  // Check for trailing content after number
  if (pos < s.size() && !isDelim(pos))
    throw asvJSONError("invalid number: unexpected character after number");
  checkTailLen(pos);
  if (!clean.empty() && clean[0] == '+') clean.erase(0, 1);
  return clean;
}

// --- Encoder ---
inline void tomlEmitVal(const asvJSONValue* v, const std::string& prefix, bool needHeader, std::string& out, bool pretty = true) {
  using T = asvJSONValue::Type;
  if (!v) return;

  auto emitHeader = [&](const std::string& path, bool isArray) {
    out += '[';
    if (isArray) out += '[';
    out += path;
    if (isArray) out += ']';
    out += "]\n";
  };

  if (v->type == T::OBJECT && v->obj) {
    if (!pretty) {
      // Compact: inline all nested objects as {key = val} tables
      for (const auto& [k, val] : *v->obj) {
        std::string qk = tomlQuoteKey(k);
        if (val->type == T::OBJECT || (val->type == T::ARRAY && val->arr)) {
          std::string vs = tomlVal(val.get());
          if (!vs.empty()) out += qk + " = " + vs + "\n";
        } else {
          std::string vs = tomlVal(val.get());
          if (!vs.empty()) out += qk + " = " + vs + "\n";
        }
      }
    } else {
      // Expanded: use [section] headers for nested objects
      if (needHeader && !prefix.empty()) {
        emitHeader(prefix, false);
      }
      for (const auto& [k, val] : *v->obj) {
        std::string qk = tomlQuoteKey(k);
        std::string fullKey = prefix.empty() ? qk : prefix + "." + qk;
        if (val->type == T::OBJECT) {
          tomlEmitVal(val.get(), fullKey, true, out, true);
        } else if (val->type == T::ARRAY && val->arr) {
          bool allObjects = true;
          for (const auto& elem : *val->arr)
            if (elem->type != T::OBJECT) { allObjects = false; break; }
          if (allObjects && !val->arr->empty()) {
            for (const auto& elem : *val->arr) {
              emitHeader(fullKey, true);
              tomlEmitVal(elem.get(), fullKey, false, out, true);
            }
          } else {
            std::string vs = tomlVal(val.get());
            out += qk + " = " + vs + "\n";
          }
        } else {
          std::string vs = tomlVal(val.get());
          if (!vs.empty()) {
            out += qk + " = " + vs + "\n";
          }
        }
      }
    }
  } else if (v->type == T::ARRAY && v->arr) {
    for (const auto& elem : *v->arr)
      tomlEmitVal(elem.get(), prefix, needHeader, out, pretty);
  }
}

inline void asvJSONValue::toTOML(std::string& out, bool pretty) const {
  using T = asvJSONValue::Type;
  if (type == T::OBJECT) {
    tomlEmitVal(this, "", false, out, pretty);
  } else if (type == T::ARRAY && arr) {
    if (!pretty) {
      out += "value = " + tomlVal(this) + "\n";
    } else {
      bool allObjects = true;
      for (const auto& elem : *arr)
        if (elem->type != T::OBJECT) { allObjects = false; break; }
      if (allObjects && !arr->empty()) {
        for (const auto& elem : *arr) {
          out += "[[item]]\n";
          tomlEmitVal(elem.get(), "item", false, out, true);
        }
      } else {
        out += "items = " + tomlVal(this) + "\n";
      }
    }
  } else {
    out += "value = " + tomlVal(this) + "\n";
  }
}

// --- Decoder ---
// Helper: navigate/create path in a tree, return the final object at the path
inline asvJSONValue* tomlNavigate(asvJSONValue* cur, const std::vector<std::string>& path,
                                  bool create = true) {
  for (const auto& seg : path) {
    if (!cur || cur->type != asvJSONValue::OBJECT || !cur->obj) {
      if (!create) return nullptr;
      throw asvJSONError("cannot navigate through non-object node");
    }
    auto it = cur->obj->find(seg);
    if (it == cur->obj->end()) {
      if (!create) return nullptr;
      auto newObj = asvJSONValue::makeObject();
      auto* raw = newObj.get();
      cur->obj->emplace(seg, std::move(newObj));
      cur = raw;
    } else {
      cur = it->second.get();
      // A header or dotted key may legally pass through an array of tables;
      // it binds to the most recently defined element (TOML: any reference to
      // an array of tables points to its last element).
      if (cur->type == asvJSONValue::ARRAY && cur->arr && !cur->arr->empty() &&
          cur->arr->back()->type == asvJSONValue::OBJECT)
        cur = cur->arr->back().get();
    }
  }
  return cur;
}

// Helper: set a value on an object by key (insert or assign)
inline void tomlObjSet(asvJSONValue* obj, const std::string& key, std::unique_ptr<asvJSONValue> val) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj)
    throw asvJSONError("duplicate key: " + key);
  auto it = obj->obj->find(key);
  if (it != obj->obj->end()) throw asvJSONError("duplicate key: " + key);
  obj->obj->emplace(key, std::move(val));
}

inline bool asvJSON::fromTOML(std::string_view input) {
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    if (input.empty()) {
      root = asvJSONValue::makeObject();
      return true;
    }

    root = asvJSONValue::makeObject();
    std::vector<std::string> tablePath;
    bool isArrayTable = false;
    std::unordered_set<std::string> tableDefs; // header-defined tables (redefinition guard)

    auto lines = splitLines(input);
    bool inMlString = false; // inside a multiline string continuation
    // Dotted keys implicitly define every super-table they create; the [table]
    // header form may not later redefine those (unlike plain header paths,
    // whose implicit super-tables stay reopenable).
    auto markDottedSupers = [&](const std::vector<std::string>& pathParts) {
      for (size_t i = 0; i + 1 < pathParts.size(); i++) {
        std::string acc;
        for (size_t j = 0; j <= i; j++) {
          if (j > 0) acc += '.';
          acc += pathParts[j];
        }
        tableDefs.insert(acc);
      }
    };
    bool stripInBasic = false, stripInLiteral = false; // persistent state for stripComment
    // Paths that were opened by an array-of-tables header (`[[a.b]]`) -- the
    // array lives at the parent key 'b'; sub-arrays nested per element bind
    // through the last element, so only the deepest parent key is tracked.
    std::unordered_set<std::string> arrayTablePaths;
    for (size_t lineNum = 0; lineNum < lines.size(); lineNum++) {
      std::string_view line = trimSV(lines[lineNum]);
      if (line.empty()) continue;

      if (!inMlString) {
        line = stripComment(line, stripInBasic, stripInLiteral);
        line = trimSV(line);
        if (line.empty()) continue;
      }

      // Table header [table] or [[array]]
      if (line[0] == '[') {
        bool isArr = false;
        size_t start = 1;
        if (line.size() > 2 && line[1] == '[') {
          isArr = true;
          start = 2;
        }
        // Find the ']' that actually closes the header: scan quote-aware so a
        // ']' inside a quoted key is not mistaken for the closer (rfind would
        // also swallow trailing content like `[a.b] z [c]`).
        bool inBasic = false, inLiteral = false, escape = false;
        size_t end = std::string_view::npos;
        for (size_t i = start; i < line.size(); i++) {
          char c = line[i];
          if (escape) { escape = false; continue; }
          if (c == '\\' && inBasic) { escape = true; continue; }
if (c == '"' && !inLiteral) { inBasic = !inBasic; continue; }
          if (c == '\'' && !inBasic) { inLiteral = !inLiteral; continue; }
          if (!inBasic && !inLiteral && c == ']') { end = i; break; }
        }
        if (end == std::string_view::npos) throw asvJSONError("unclosed table header");
        if (isArr) {
          if (end + 1 >= line.size() || line[end + 1] != ']')
            throw asvJSONError("malformed array table");
        }
        std::string_view headerPath = trimSV(line.substr(start, end - start));
        tablePath = parseTomlKeyPath(headerPath);
        isArrayTable = isArr;

        // Nothing but whitespace may follow the closing bracket(s)
        size_t afterClose = end + (isArr ? 2 : 1);
        if (!trimSV(line.substr(afterClose)).empty())
          throw asvJSONError("trailing content after table header");

        // Materialize tables eagerly and reject redefinition. A table may be
        // reopened only if it was implicitly created as an intermediate segment
        // of a deeper header (e.g. `[A.x]` then `[A]`), never if it already
        // holds a value/array or was defined by an earlier header/array header.
        if (!tablePath.empty()) {
          std::string fullPath;
          for (size_t i = 0; i < tablePath.size(); i++) {
            if (i > 0) fullPath += '.';
            fullPath += tablePath[i];
          }
          std::string last = tablePath.back();
          std::vector<std::string> parentPath(tablePath.begin(), tablePath.end() - 1);
          asvJSONValue* parent = tomlNavigate(root.get(), parentPath, true);
          if (!parent || parent->type != asvJSONValue::OBJECT || !parent->obj)
            throw asvJSONError("cannot define table on non-object");
          auto it = parent->obj->find(last);
          if (isArrayTable) {
            if (it != parent->obj->end() && it->second->type != asvJSONValue::ARRAY)
              throw asvJSONError("cannot redefine key '" + last + "' as array table");
            // Appending to a statically-defined array (created by `arr = [...]`)
            // is a parse error (even if empty), but re-appending through a
            // `[[...]]` header or a sub-array-of-tables header on the last
            // element is allowed.
            if (it != parent->obj->end() && it->second->type == asvJSONValue::ARRAY &&
                !arrayTablePaths.count(last))
              throw asvJSONError("cannot append to statically defined array '" + fullPath + "'");
            asvJSONValue* arrVal;
            if (it == parent->obj->end()) {
              auto newArr = asvJSONValue::makeArray();
              arrVal = newArr.get();
              parent->obj->emplace(last, std::move(newArr));
            } else {
              arrVal = it->second.get();
            }
            arrVal->arr->push_back(asvJSONValue::makeObject());
            tableDefs.insert(fullPath);
            arrayTablePaths.insert(last);
          } else {
            if (tableDefs.count(fullPath))
              throw asvJSONError("table '" + fullPath + "' already defined");
            if (it != parent->obj->end() && it->second->type != asvJSONValue::OBJECT)
              throw asvJSONError("cannot redefine key '" + last + "' as table");
            if (it == parent->obj->end()) {
              auto newObj = asvJSONValue::makeObject();
              parent->obj->emplace(last, std::move(newObj));
            }
            tableDefs.insert(fullPath);
          }
        }
        continue;
      }

      // Key = value
      bool inBasic = false, inLiteral = false, escape = false;
      size_t eqPos = std::string_view::npos;
      for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (escape) { escape = false; continue; }
        if (c == '\\' && inBasic) { escape = true; continue; }
        if (c == '"' && !inLiteral) { inBasic = !inBasic; continue; }
        if (c == '\'' && !inBasic) { inLiteral = !inLiteral; continue; }
        if (!inBasic && !inLiteral && c == '=') { eqPos = i; break; }
      }
      if (eqPos == std::string_view::npos)
        throw asvJSONError("expected '=' in key-value pair");

      std::string_view keyPart = line.substr(0, eqPos);
      std::string_view valPart = line.substr(eqPos + 1);

      auto keys = parseTomlKeyPath(keyPart);

      // Parse value - handle multi-line strings
      std::string valueStr;
      std::string vtmp = std::string(trimSV(valPart));

      if ((vtmp.size() >= 3 && (vtmp.substr(0, 3) == "\"\"\"")) ||
          (vtmp.size() >= 3 && (vtmp.substr(0, 3) == "'''"))) {
        bool isBasic = vtmp.substr(0, 3) == "\"\"\"";
        std::string delim = isBasic ? "\"\"\"" : "'''";
        // A delimiter that closes at (or right after) the opening position is
        // not a real close -- e.g. `""""""` is an empty string, and
        // `s="""\n"""""` continues to the next line. Only close at a position
        // that leaves real content (or a genuine ``"""`` terminator) before it.
        size_t closePos = vtmp.find(delim, 3);
        if (closePos == 3) closePos = vtmp.find("\\" + delim, 3);
        size_t fieldClose = std::string_view::npos;
        if (closePos != std::string_view::npos) {
          size_t endPos = vtmp.find_first_of(" \t\r\n", closePos + 3);
          fieldClose = (endPos == std::string_view::npos) ? vtmp.size()
                                                          : vtmp.find_first_of("\"", closePos + 3);
          if (fieldClose == std::string_view::npos ||
              vtmp.find_first_not_of(" \t\r\n" + delim, fieldClose) != std::string_view::npos)
            closePos = std::string_view::npos;
        }
        if (closePos != std::string_view::npos && closePos > 2) {
          // The string closes on this line: hand the whole value to
          // parseTomlValue, which enforces the closing delimiter and any
          // trailing content (previously trailing text was silently dropped).
          valueStr = vtmp;
          // Reset persistent stripComment state -- the multiline string
          // was fully consumed on this line.
          stripInBasic = false;
          stripInLiteral = false;
        } else {
          std::string accum = vtmp;
          bool found = false;
          for (size_t ml = lineNum + 1; ml < lines.size(); ml++) {
            std::string mlLine = std::string(lines[ml]);
            size_t cp = mlLine.find(delim);
            if (cp != std::string::npos) {
              // Keep the full line: parseTomlValue enforces the closing
              // delimiter boundary and rejects any trailing content
              // (previously everything after the first `"""` was dropped).
              accum += '\n' + mlLine;
              lineNum = ml;
              found = true;
              // Reset persistent stripComment state -- multiline string closed
              stripInBasic = false;
              stripInLiteral = false;
              break;
            }
            accum += '\n' + mlLine;
            lineNum = ml;
          }
          if (!found) throw asvJSONError("unclosed multi-line string");
          valueStr = accum;
        }
      } else if (!vtmp.empty() && vtmp[0] == '[' &&
                 !tomlArrayBalanced(std::string_view(vtmp))) {
        // Multiline array: gather continuation lines until the brackets balance
        std::string accum = vtmp;
        bool found = false;
        for (size_t ml = lineNum + 1; ml < lines.size(); ml++) {
          accum += '\n' + std::string(lines[ml]);
          lineNum = ml;
          if (tomlArrayBalanced(std::string_view(accum))) {
            found = true;
            break;
          }
        }
        if (!found) throw asvJSONError("unclosed array");
        stripInBasic = false;
        stripInLiteral = false;
        valueStr = accum;
      } else {
        valueStr = vtmp;
      }

      // A key must have an explicit value (`key =` is invalid per the spec)
      if (trimSV(std::string_view(valueStr)).empty())
        throw asvJSONError("missing value for key");

      std::string jsonVal = parseTomlValue(std::string_view(valueStr), 0, true);

      if (isArrayTable) {
        // Inside [[array]]: set on current (last) array element
        if (tablePath.empty()) continue;
        std::string arrKey = tablePath.back();
        std::vector<std::string> parentPath(tablePath.begin(), tablePath.end() - 1);
        asvJSONValue* parent = tomlNavigate(root.get(), parentPath, false);
        if (!parent || parent->type != asvJSONValue::OBJECT || !parent->obj) continue;
        auto it = parent->obj->find(arrKey);
        if (it == parent->obj->end() || it->second->arr->empty()) continue;
        asvJSONValue* elem = it->second->arr->back().get();
        // Set key=value on the element (handle dotted keys)
        if (keys.size() == 1) {
          auto val = tomlParseJsonValue(jsonVal);
          if (val) tomlObjSet(elem, keys[0], std::move(val));
        } else {
          std::string lastKey = keys.back();
          std::vector<std::string> nestedPath(keys.begin(), keys.end() - 1);
          asvJSONValue* target = tomlNavigate(elem, nestedPath, true);
          if (target) {
            auto val = tomlParseJsonValue(jsonVal);
            if (val) {
              std::vector<std::string> p = tablePath;
              p.insert(p.end(), keys.begin(), keys.end());
              markDottedSupers(p);
              tomlObjSet(target, lastKey, std::move(val));
            }
          }
        }
      } else {
        // Regular: full path = table path + key path
        std::vector<std::string> fullPath = tablePath;
        fullPath.insert(fullPath.end(), keys.begin(), keys.end());

        if (fullPath.empty()) continue;
        std::string lastKey = fullPath.back();
        fullPath.pop_back();
        asvJSONValue* parent = tomlNavigate(root.get(), fullPath, true);
        if (parent) {
          auto val = tomlParseJsonValue(jsonVal);
          if (val) {
            if (keys.size() > 1) {
              std::vector<std::string> p = fullPath;
              p.push_back(lastKey);
              markDottedSupers(p);
            }
            tomlObjSet(parent, lastKey, std::move(val));
          }
        }
      }
    }

    formatType = FormatType::TOML;
    return true;

  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
