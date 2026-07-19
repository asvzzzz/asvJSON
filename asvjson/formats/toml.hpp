#pragma once
// TOML serialization/parsing for asvJSON++

#include "../core.hpp"

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
      fmtDateTimeVal(v->timestamp, v->datetime_ms, s);
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

// Trim whitespace (spaces, tabs, \r)
inline std::string_view trimSV(std::string_view s) {
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// Strip comment (#) from line, respecting quoted strings
inline std::string_view stripComment(std::string_view line) {
  bool inBasic = false, inLiteral = false, escape = false;
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
        else if (c == 'u') {
          int count = 4; unsigned long cp = 0; bool ok = true;
          for (int i = 0; i < count && ok; i++) {
            if (s.empty()) { ok = false; break; }
            char hc = s[0]; s.remove_prefix(1);
            if (hc >= '0' && hc <= '9') cp = cp * 16 + (hc - '0');
            else if (hc >= 'a' && hc <= 'f') cp = cp * 16 + (hc - 'a' + 10);
            else if (hc >= 'A' && hc <= 'F') cp = cp * 16 + (hc - 'A' + 10);
            else ok = false;
          }
          if (ok) appendCodepoint(cp, key); else key += c;
        }
        else if (c == 'U') {
          int count = 8; unsigned long cp = 0; bool ok = true;
          for (int i = 0; i < count && ok; i++) {
            if (s.empty()) { ok = false; break; }
            char hc = s[0]; s.remove_prefix(1);
            if (hc >= '0' && hc <= '9') cp = cp * 16 + (hc - '0');
            else if (hc >= 'a' && hc <= 'f') cp = cp * 16 + (hc - 'a' + 10);
            else if (hc >= 'A' && hc <= 'F') cp = cp * 16 + (hc - 'A' + 10);
            else ok = false;
          }
          if (ok && cp <= 0x10FFFF) appendCodepoint(cp, key); else key += c;
        }
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
    while (!s.empty() && (std::isalnum(static_cast<unsigned char>(s[0])) || s[0] == '_' || s[0] == '-')) {
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
    std::string key = parseTomlKey(s);
    if (!key.empty()) keys.push_back(std::move(key));
    s = trimSV(s);
    if (!s.empty() && s[0] == '.') { s.remove_prefix(1); continue; }
    break;
  }
  return keys;
}

// Helper: parse a single JSON value string into an asvJSONValue
inline std::unique_ptr<asvJSONValue> tomlParseJsonValue(const std::string& jsonVal) {
  asvJSON tmp;
  tmp.allowNaNInfinity = true;
  if (!tmp.parse(std::string_view("{\"_\":" + jsonVal + "}"))) {
    return asvJSONValue::makeNull();
  }
  return cloneValue(tmp.getRoot()->get("_"));
}

// Parse a complete TOML value into JSON string
constexpr int MAX_TOML_DEPTH = 64;

inline std::string parseTomlValue(std::string_view s, int depth = 0) {
  s = trimSV(s);
  if (depth > MAX_TOML_DEPTH) throw asvJSONError("TOML value nesting too deep");
  if (s.empty()) return "null";

  // Inline table
  if (s[0] == '{') {
    s.remove_prefix(1);
    auto obj = asvJSONValue::makeObject();
    std::unordered_set<std::string> seenKeys;
    while (!s.empty()) {
      s = trimSV(s);
      if (s.empty() || s[0] == '}') break;
      auto keys = parseTomlKeyPath(s);
      s = trimSV(s);
      if (!s.empty() && s[0] == '=') { s.remove_prefix(1); s = trimSV(s); }
      int bracketDepth = 0;
      std::string valStr;
      bool inBasic = false, inLiteral = false, escape = false;
      while (!s.empty()) {
        char c = s[0]; s.remove_prefix(1);
        if (escape) { escape = false; valStr += c; continue; }
        if (c == '\\' && inBasic) { escape = true; valStr += c; continue; }
        if (c == '"' && !inLiteral) { inBasic = !inBasic; valStr += c; continue; }
        if (c == '\'' && !inBasic) { inLiteral = !inLiteral; valStr += c; continue; }
        if (!inBasic && !inLiteral) {
          if (c == '{' || c == '[') bracketDepth++;
          if (c == '}' || c == ']') bracketDepth--;
          if (bracketDepth < 0 && c == '}') break;
          if (c == ',' && bracketDepth == 0) break;
        }
        valStr += c;
      }
      std::string valJson = parseTomlValue(trimSV(std::string_view(valStr)), depth + 1);
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
      if (!s.empty() && s[0] == ',') s.remove_prefix(1);
    }
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
      if (s.empty()) { qc = 3; break; }
      char c = s[0]; s.remove_prefix(1);
      if (escape) { escape = false;
        if (c == 'n') val += '\n'; else if (c == 't') val += '\t';
        else if (c == 'r') val += '\r'; else if (c == '"') val += '"';
        else if (c == '\\') val += '\\'; else if (c == 'b') val += '\b';
        else if (c == 'f') val += '\f';
        else if (c == 'u') {
          int count = 4; unsigned long cp = 0; bool ok = true;
          for (int i = 0; i < count && ok; i++) {
            if (s.empty()) { ok = false; break; }
            char hc = s[0]; s.remove_prefix(1);
            if (hc >= '0' && hc <= '9') cp = cp * 16 + (hc - '0');
            else if (hc >= 'a' && hc <= 'f') cp = cp * 16 + (hc - 'a' + 10);
            else if (hc >= 'A' && hc <= 'F') cp = cp * 16 + (hc - 'A' + 10);
            else ok = false;
          }
          if (ok) appendCodepoint(cp, val); else val += c;
        }
        else if (c == 'U') {
          int count = 8; unsigned long cp = 0; bool ok = true;
          for (int i = 0; i < count && ok; i++) {
            if (s.empty()) { ok = false; break; }
            char hc = s[0]; s.remove_prefix(1);
            if (hc >= '0' && hc <= '9') cp = cp * 16 + (hc - '0');
            else if (hc >= 'a' && hc <= 'f') cp = cp * 16 + (hc - 'a' + 10);
            else if (hc >= 'A' && hc <= 'F') cp = cp * 16 + (hc - 'A' + 10);
            else ok = false;
          }
          if (ok && cp <= 0x10FFFF) appendCodepoint(cp, val); else val += c;
        }
        else val += c;
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
      if (s.empty()) { qc = 3; break; }
      char c = s[0]; s.remove_prefix(1);
      if (c == '\'') { qc++; if (qc >= 3) break; continue; }
      qc = 0;
      val += c;
    }
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
    auto flushVal = [&]() {
      std::string trimmed = std::string(trimSV(std::string_view(cur)));
      if (!trimmed.empty()) {
        if (!first) json += ',';
        first = false;
        json += parseTomlValue(std::string_view(trimmed), depth + 1);
      }
      cur.clear();
    };
    while (!s.empty()) {
      char c = s[0]; s.remove_prefix(1);
      if (escape) { escape = false; cur += c; continue; }
      if (c == '\\' && inBasic) { escape = true; cur += c; continue; }
      if (c == '"' && !inLiteral) { inBasic = !inBasic; cur += c; continue; }
      if (c == '\'' && !inBasic) { inLiteral = !inLiteral; cur += c; continue; }
      if (!inBasic && !inLiteral) {
        if (c == '[') { bracketDepth++; cur += c; continue; }
        if (c == ']') {
          if (bracketDepth == 0) { flushVal(); break; }
          bracketDepth--; cur += c; continue;
        }
        if (c == ',' && bracketDepth == 0) { flushVal(); continue; }
        if (c == '{' || c == '}') { bracketDepth = (c == '{') ? bracketDepth + 1 : bracketDepth - 1; cur += c; continue; }
      }
      cur += c;
    }
    json += "]";
    return json;
  }

  // Boolean
  if (s.substr(0, 4) == "true" && (s.size() == 4 || s[4] == ',' || s[4] == ']' || s[4] == '}' || s[4] == '#' || s[4] == ' ' || s[4] == '\t'))
    return "true";
  if (s.substr(0, 5) == "false" && (s.size() == 5 || s[5] == ',' || s[5] == ']' || s[5] == '}' || s[5] == '#' || s[5] == ' ' || s[5] == '\t'))
    return "false";

  // Date/time
  if (s.size() >= 10 && std::isdigit(static_cast<unsigned char>(s[0])) &&
      s[4] == '-' && s[7] == '-') {
    size_t end = 10;
    while (end < s.size() && (std::isdigit(static_cast<unsigned char>(s[end])) ||
           s[end] == 'T' || s[end] == ':' || s[end] == '.' || s[end] == 'Z' ||
           s[end] == '+' || s[end] == '-')) end++;
    return "\"" + std::string(s.substr(0, end)) + "\"";
  }

  // Basic string "..."
  if (s[0] == '"') {
    s.remove_prefix(1);
    std::string val;
    bool escape = false;
    for (;;) {
      if (s.empty()) break;
      char c = s[0]; s.remove_prefix(1);
      if (escape) { escape = false;
        if (c == 'n') val += '\n'; else if (c == 't') val += '\t';
        else if (c == 'r') val += '\r'; else if (c == '"') val += '"';
        else if (c == '\\') val += '\\'; else if (c == 'b') val += '\b';
        else if (c == 'f') val += '\f';
        else if (c == 'u') {
          int count = 4; unsigned long cp = 0; bool ok = true;
          for (int i = 0; i < count && ok; i++) {
            if (s.empty()) { ok = false; break; }
            char hc = s[0]; s.remove_prefix(1);
            if (hc >= '0' && hc <= '9') cp = cp * 16 + (hc - '0');
            else if (hc >= 'a' && hc <= 'f') cp = cp * 16 + (hc - 'a' + 10);
            else if (hc >= 'A' && hc <= 'F') cp = cp * 16 + (hc - 'A' + 10);
            else ok = false;
          }
          if (ok) appendCodepoint(cp, val); else val += c;
        }
        else if (c == 'U') {
          int count = 8; unsigned long cp = 0; bool ok = true;
          for (int i = 0; i < count && ok; i++) {
            if (s.empty()) { ok = false; break; }
            char hc = s[0]; s.remove_prefix(1);
            if (hc >= '0' && hc <= '9') cp = cp * 16 + (hc - '0');
            else if (hc >= 'a' && hc <= 'f') cp = cp * 16 + (hc - 'a' + 10);
            else if (hc >= 'A' && hc <= 'F') cp = cp * 16 + (hc - 'A' + 10);
            else ok = false;
          }
          if (ok && cp <= 0x10FFFF) appendCodepoint(cp, val); else val += c;
        }
        else val += c;
        continue;
      }
      if (c == '\\') { escape = true; continue; }
      if (c == '"') break;
      val += c;
    }
    return "\"" + tomlJsonEscape(val) + "\"";
  }

  // Literal string '...'
  if (s[0] == '\'') {
    s.remove_prefix(1);
    std::string val;
    for (;;) {
      if (s.empty()) break;
      char c = s[0]; s.remove_prefix(1);
      if (c == '\'') break;
      val += c;
    }
    return "\"" + tomlJsonEscape(val) + "\"";
  }

  // Hex, octal, binary
  if (s.size() >= 2 && s[0] == '0') {
    if (s[1] == 'x' || s[1] == 'X') {
      uint64_t hv = 0; size_t pos = 0;
      for (auto c : s.substr(2)) {
        if (c == '_') continue;
        if (c >= '0' && c <= '9') { hv = hv * 16 + (c - '0'); pos++; }
        else if (c >= 'a' && c <= 'f') { hv = hv * 16 + (c - 'a' + 10); pos++; }
        else if (c >= 'A' && c <= 'F') { hv = hv * 16 + (c - 'A' + 10); pos++; }
        else break;
      }
      if (pos > 0) return std::to_string(static_cast<long long>(hv));
    }
    if (s[1] == 'o' || s[1] == 'O') {
      uint64_t ov = 0; size_t pos = 0;
      for (auto c : s.substr(2)) {
        if (c == '_') continue;
        if (c >= '0' && c <= '7') { ov = ov * 8 + (c - '0'); pos++; }
        else break;
      }
      if (pos > 0) return std::to_string(static_cast<long long>(ov));
    }
    if (s[1] == 'b' || s[1] == 'B') {
      uint64_t bv = 0; size_t pos = 0;
      for (auto c : s.substr(2)) {
        if (c == '_') continue;
        if (c == '0' || c == '1') { bv = bv * 2 + (c - '0'); pos++; }
        else break;
      }
      if (pos > 0) return std::to_string(static_cast<long long>(bv));
    }
  }

  // Number
  bool isFloat = false;
  std::string numStr;
  size_t pos = 0;
  if (pos < s.size() && s[pos] == '-') { numStr += '-'; pos++; }
  if (pos < s.size() && s[pos] == '+') { pos++; }
  auto isDelim = [&](size_t next) -> bool {
    return next >= s.size() || s[next] == ' ' || s[next] == '\t' || s[next] == ',' || s[next] == ']' || s[next] == '}' || s[next] == '#' || s[next] == '\r' || s[next] == '\n';
  };
  if (pos < s.size() && std::tolower(static_cast<unsigned char>(s[pos])) == 'i') {
    if (pos + 2 < s.size() &&
        std::tolower(static_cast<unsigned char>(s[pos+1])) == 'n' &&
        std::tolower(static_cast<unsigned char>(s[pos+2])) == 'f' && isDelim(pos + 3))
      return numStr + "Infinity";
  }
  if (pos < s.size() && std::tolower(static_cast<unsigned char>(s[pos])) == 'n') {
    if (pos + 2 < s.size() &&
        std::tolower(static_cast<unsigned char>(s[pos+1])) == 'a' &&
        std::tolower(static_cast<unsigned char>(s[pos+2])) == 'n' && isDelim(pos + 3))
      return "NaN";
  }
  bool afterE = false;
  for (; pos < s.size(); pos++) {
    char c = s[pos];
    if (c == '_') continue;
    if (c == '.' || c == 'e' || c == 'E') isFloat = true;
    if (c == 'e' || c == 'E') { afterE = true; numStr += c; continue; }
    if (afterE && c == '-') { numStr += c; afterE = false; continue; }
    if (afterE && c == '+') { afterE = false; continue; }
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-')
      numStr += c;
    else break;
    afterE = false;
  }
  if (numStr.empty() || numStr == "-" || numStr == "+") return "null";
  // Check for trailing content after number
  if (pos < s.size() && !isDelim(pos))
    throw asvJSONError("invalid number: unexpected character after number");
  return numStr;
}

} // namespace asvJSONInternal
using namespace asvJSONInternal;

// --- Encoder ---
static void tomlEmitVal(const asvJSONValue* v, const std::string& prefix, bool needHeader, std::string& out) {
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
    if (needHeader && !prefix.empty()) {
      emitHeader(prefix, false);
    }
    for (const auto& [k, val] : *v->obj) {
      std::string qk = tomlQuoteKey(k);
      std::string fullKey = prefix.empty() ? qk : prefix + "." + qk;
      if (val->type == T::OBJECT) {
        tomlEmitVal(val.get(), fullKey, true, out);
      } else if (val->type == T::ARRAY && val->arr) {
        bool allObjects = true;
        for (const auto& elem : *val->arr)
          if (elem->type != T::OBJECT) { allObjects = false; break; }
        if (allObjects && !val->arr->empty()) {
          for (const auto& elem : *val->arr) {
            emitHeader(fullKey, true);
            tomlEmitVal(elem.get(), fullKey, false, out);
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
  } else if (v->type == T::ARRAY && v->arr) {
    for (const auto& elem : *v->arr)
      tomlEmitVal(elem.get(), prefix, needHeader, out);
  }
}

inline void asvJSONValue::toTOML(std::string& out) const {
  using T = asvJSONValue::Type;
  if (type == T::OBJECT) {
    tomlEmitVal(this, "", false, out);
  } else if (type == T::ARRAY && arr) {
    bool allObjects = true;
    for (const auto& elem : *arr)
      if (elem->type != T::OBJECT) { allObjects = false; break; }
    if (allObjects && !arr->empty()) {
      for (const auto& elem : *arr) {
        out += "[[item]]\n";
        tomlEmitVal(elem.get(), "item", false, out);
      }
    } else {
      out += "items = " + tomlVal(this) + "\n";
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
    }
  }
  return cur;
}

// Helper: set a value on an object by key (insert or assign)
inline void tomlObjSet(asvJSONValue* obj, const std::string& key, std::unique_ptr<asvJSONValue> val) {
  auto it = obj->obj->find(key);
  if (it != obj->obj->end()) throw asvJSONError("duplicate key: " + key);
  obj->obj->emplace(key, std::move(val));
}

inline bool asvJSON::fromTOML(std::string_view input) {
  try {
    if (input.empty()) {
      root = asvJSONValue::makeObject();
      return true;
    }

    root = asvJSONValue::makeObject();
    std::vector<std::string> tablePath;
    bool isArrayTable = false;

    auto lines = splitLines(input);
    for (size_t lineNum = 0; lineNum < lines.size(); lineNum++) {
      std::string_view line = trimSV(lines[lineNum]);
      if (line.empty()) continue;

      line = stripComment(line);
      line = trimSV(line);
      if (line.empty()) continue;

      // Table header [table] or [[array]]
      if (line[0] == '[') {
        size_t end = line.rfind(']');
        if (end == std::string_view::npos) throw asvJSONError("unclosed table header");
        bool isArr = false;
        size_t start = 1;
        if (line.size() > 2 && line[1] == '[') {
          if (end <= 2 || line[end-1] != ']') throw asvJSONError("malformed array table");
          isArr = true;
          start = 2;
          end--;
        }
        std::string_view headerPath = trimSV(line.substr(start, end - start));
        tablePath = parseTomlKeyPath(headerPath);
        isArrayTable = isArr;

        // For [[array]], ensure the array exists and add a new element
        if (isArrayTable && !tablePath.empty()) {
          std::string arrKey = tablePath.back();
          std::vector<std::string> parentPath(tablePath.begin(), tablePath.end() - 1);
          asvJSONValue* parent = tomlNavigate(root.get(), parentPath, true);
          auto it = parent->obj->find(arrKey);
          asvJSONValue* arrVal;
          if (it == parent->obj->end() || it->second->type != asvJSONValue::ARRAY) {
            auto newArr = asvJSONValue::makeArray();
            arrVal = newArr.get();
            parent->obj->insert_or_assign(arrKey, std::move(newArr));
          } else {
            arrVal = it->second.get();
          }
          arrVal->arr->push_back(asvJSONValue::makeObject());
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
        if (c == '\'' && !inLiteral) { inLiteral = !inLiteral; continue; }
        if (!inBasic && !inLiteral && c == '=') { eqPos = i; break; }
      }
      if (eqPos == std::string_view::npos) continue;

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
        size_t closePos = vtmp.find(delim, 3);
        if (closePos != std::string::npos && closePos > 2) {
          vtmp = vtmp.substr(0, closePos + 3);
          valueStr = vtmp;
        } else {
          std::string accum = vtmp;
          bool found = false;
          for (size_t ml = lineNum + 1; ml < lines.size(); ml++) {
            std::string mlLine = std::string(lines[ml]);
            size_t cp = mlLine.find(delim);
            if (cp != std::string::npos) {
              accum += '\n' + mlLine.substr(0, cp + 3);
              lineNum = ml;
              found = true;
              break;
            }
            accum += '\n' + mlLine;
            lineNum = ml;
          }
          if (!found) throw asvJSONError("unclosed multi-line string");
          valueStr = accum;
        }
      } else {
        valueStr = vtmp;
      }

      std::string jsonVal = parseTomlValue(std::string_view(valueStr));

      if (isArrayTable) {
        // Inside [[array]]: set on current (last) array element
        if (tablePath.empty()) continue;
        std::string arrKey = tablePath.back();
        std::vector<std::string> parentPath(tablePath.begin(), tablePath.end() - 1);
        asvJSONValue* parent = tomlNavigate(root.get(), parentPath, false);
        if (!parent) continue;
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
            if (val) tomlObjSet(target, lastKey, std::move(val));
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
          if (val) tomlObjSet(parent, lastKey, std::move(val));
        }
      }
    }

    return true;

  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}
