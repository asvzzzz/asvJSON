#pragma once
// TOML serialization/parsing for asvJSON++
//
// Known limitations (documented, not bugs):
// - TOML has no null type: a NULL value is silently dropped by tomlVal(), both
//   in objects ({"a": null, "b": 1} -> b = 1) and in arrays ([1, null, 2] ->
//   [1, 2]). An object whose members are all NULL is therefore emitted as an
//   empty table ([a] in pretty mode, a = {} in compact mode), and a NULL
//   scalar ROOT emits an empty document instead of the invalid line `value = `.
// - Multi-line basic/literal strings and dotted keys are decoded but never
//   emitted: toTOML() writes every string as a single-line basic string with
//   escapes, and nests objects as [table] headers in pretty mode or as inline
//   tables in compact mode (toTOML(false)). A TOML -> JSON -> TOML round-trip
//   is one-way with respect to those three forms.
// - BINARY and DATETIME do not round-trip: toTOML() emits them as a base64
//   string / formatted date-time string, and fromTOML() decodes both back as
//   STRING (they cannot be retagged without an external schema). This includes
//   the RFC 3339 form `t = 2024-01-01T00:00:00Z`, which decodes to STRING. A
//   DATETIME whose instant gmtimeSafe cannot convert renders as the QUOTED
//   string "null" (a bare `null` is not valid TOML), so it re-parses as STRING.
// - Mixed-type arrays are accepted, a lenient extension: TOML 1.0 requires an
//   array to be homogeneous ("data types may not be mixed"), but fromTOML()
//   parses `[1, "two"]` without complaint. It has to: toTOML() writes whatever
//   JSON array it is given, so a mixed array must survive the round-trip.
// - Multi-line inline tables are accepted, a lenient extension: TOML 1.0
//   allows no newline inside `{...}`, but continuation lines are gathered until
//   the braces balance and a '#' comment on a continuation line is dropped, so
//   `a = { b = 1, # c` + `d = 2 }` parses. The one-line form stays strict: a
//   '#' before the closing brace is a comment that swallows it, which leaves
//   the table unterminated, so `a = {b = 1 # c}` is an error.
// - `\` + newline basic-string continuation is accepted, a TOML 0.5 rule that
//   TOML 1.0 dropped for single-line strings: the backslash, the line break and
//   the next line's indentation are discarded, so `a = "foo \` + `bar"` folds
//   to "foobar". Only an ACTIVE trailing backslash (an odd run, so the last
//   one is not itself escaped) lets a single-line string span lines; a string
//   with no continuation still fails as unterminated.
// - Date/time validation is deliberately weak: tomlDateTimeEnd() checks only
//   the date prefix month (1-12) and day (1-31) ranges, so the time fields go
//   unvalidated -- `2024-01-01T99:99:99Z` decodes to the STRING
//   "2024-01-01T99:99:99Z" rather than being rejected. A malformed date
//   prefix (month/day out of range, e.g. `2024-13-01`) is NOT a date at all and
//   fails the numeric parse ("invalid number").
// - Table redefinition is scoped to the resolved tree: a repeated `[a.b]`
//   header is an error, but `[items.s]` after a new `[[items]]` element is
//   accepted, because every reference to an array of tables resolves to its
//   most recently defined element.

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
      case '\x7F': out += "\\u007F"; break;
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
        appendUtf8Codepoint(out, static_cast<unsigned int>(combined));
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
  appendUtf8Codepoint(out, static_cast<unsigned int>(cp));
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
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c == '\r') out += "\\r";
    else if (c == '\b') out += "\\b";
    else if (c == '\f') out += "\\f";
    else if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7F) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
      out += buf;
    } else {
      out += c;
    }
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
      // TOML separates int from float lexically, so an integral double must
      // keep a '.' or an exponent: fmtDoubleVal renders 1.0 as "1", which
      // would re-read as INT and lose the DOUBLE type. Mirrors the .0 suffix
      // the INI/dotenv/ADE encoders append.
      if (s.find_first_of(".eE") == std::string::npos) s += ".0";
      return s;
    }
    case T::STRING: return tomlEscapeStr(v->str_data);
    case T::DATETIME: {
      std::string s;
      fmtDateTimeVal(v->timestamp, v->datetime_ms, s, v->tz_offset_min);
      // A timestamp gmtimeSafe cannot convert renders as "null"; emit it as
      // the QUOTED string "null" so the line stays valid TOML and re-parses as
      // a STRING (the same convention the INI encoder uses; a bare `null` is
      // not valid TOML and would fail the decode).
      if (s == "null") return "\"null\"";
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

// Parse a single TOML key (bare or quoted), advances s past it
inline std::string parseTomlKey(std::string_view& s) {
  s = trimSV(s);
  if (s.empty()) return "";
  std::string key;
  if (s[0] == '"') {
    s.remove_prefix(1);
    bool escape = false;
    for (;;) {
      if (s.empty()) throw asvJSONError("unterminated quoted key");
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
      if (s.empty()) throw asvJSONError("unterminated quoted key");
      char c = s[0]; s.remove_prefix(1);
      if (c == '\'') break;
      key += c;
    }
  } else {
    // Bare key: ASCII alnum, '_', '-' (a leading/trailing high-bit byte is
    // tolerated so non-ASCII UTF-8 keys (e.g. Cyrillic) parse leniently)
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
    // Key may be followed only by a separating '=', a table/array closer, or
    // a comma/brace in an inline table -- anything else is junk like `ae` or
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
  auto v = cloneValue(tmp.getRoot()->get("_"));
  // cloneValue() returns null on OOM / node-budget exhaustion (nothrow). Throw
  // instead of returning it: every caller guards with `if (val)`, so a null
  // would silently drop the key=value pair rather than failing the parse
  // (CBOR/MessagePack/UBJSON diagnose the same condition explicitly).
  if (!v) throw asvJSONError("out of memory");
  return v;
}

// Quote-aware scanner: skip a string literal (single or triple-quoted)
// starting at position `i`. Returns the position after the closing delimiter.
static inline size_t tomlSkipString(std::string_view s, size_t i) {
  if (i >= s.size()) return i;
  char q = s[i];
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
        if (run >= 3) return i + run;
        i++;
        continue;
      }
      return i + 1;
    }
    i++;
  }
  return i;
}

// Quote-aware scan that reports whether a container opened by `open` is
// already balanced on this string. Used to gather multiline array/table value
// lines until the delimiters close.
template<char OPEN, char CLOSE>
inline bool tomlBalanced(std::string_view s) {
  int depth = 0;
  size_t i = 0;
  while (i < s.size()) {
    char c = s[i];
    if (c == '"' || c == '\'') {
      i = tomlSkipString(s, i);
      continue;
    }
    if (c == '#') {
      while (i < s.size() && s[i] != '\n' && s[i] != '\r') i++;
      continue;
    }
    if (c == OPEN) depth++;
    else if (c == CLOSE) {
      depth--;
      if (depth <= 0) return true;
    }
    i++;
  }
  return false;
}

// Quote-aware scan that reports whether the array opened by the first '['
// is already balanced on this string. Used to gather multiline array value
// lines (`arr = [\n1,\n2]`) until the brackets close.
inline bool tomlArrayBalanced(std::string_view s) { return tomlBalanced<'[', ']'>(s); }

// Quote-aware scan that reports whether the inline table opened by the first '{'
// is already balanced on this string. Used to gather multiline inline table
// value lines (`a = {\n  b = 1,\n}`) until the braces close.
inline bool tomlTableBalanced(std::string_view s) { return tomlBalanced<'{', '}'>(s); }

// Finds the closing `delim` at or after `from` and returns the offset of its
// first character, or npos when the string is still open. Mirrors the
// quote-run rule of parseTomlValue(): a run of 3+ delimiters closes the string
// with its FINAL three, earlier ones in the run stay as content (`""""` is one
// content quote plus the closer). In a multi-line basic string a backslash
// escapes the next character, so `\"""` is the escape `\"` followed by two
// literal quotes and does not close the string. A multi-line literal string has
// no escapes, so the first `'''` run always closes it.
inline size_t tomlFindUnescapedDelim(std::string_view s, size_t from,
                                     std::string_view delim, bool basic) {
  const char q = delim[0];
  const size_t dl = delim.size();
  for (size_t i = from; i < s.size();) {
    if (basic && s[i] == '\\') { i += (i + 1 < s.size()) ? 2 : 1; continue; }
    if (s[i] != q) { i++; continue; }
    size_t run = 1;
    while (i + run < s.size() && s[i + run] == q) run++;
    if (run >= dl) return i + (run - dl);
    i += run;
  }
  return std::string_view::npos;
}

// Single-line basic-string state scanner for the fromTOML gather. Returns true
// once a `"..."` string opened in `s` is closed on this text, and when the
// string is STILL open, sets `*continues` when the text ends in an ACTIVE
// line-continuation backslash (an odd trailing run -- an even run means the
// backslash itself is escaped and the string simply has no closer). The `\` +
// newline fold discards the backslash, the line break and the next line's
// indentation, exactly like parseTomlValue's basic-string branch.
inline bool tomlBasicStrClosed(std::string_view s, bool* continues) {
  if (continues) *continues = false;
  if (s.size() < 2 || s[0] != '"') return false;
  size_t i = 1;
  bool escape = false;
  while (i < s.size()) {
    char c = s[i];
    if (escape) { escape = false; i++; continue; }
    if (c == '\\') {
      size_t skip = i + 1;
      while (skip < s.size() && (s[skip] == ' ' || s[skip] == '\t')) skip++;
      if (skip < s.size() && (s[skip] == '\n' || s[skip] == '\r')) {
        if (s[skip] == '\r' && skip + 1 < s.size() && s[skip + 1] == '\n') skip++;
        skip++;
        while (skip < s.size() && (s[skip] == ' ' || s[skip] == '\t' || s[skip] == '\n' || s[skip] == '\r')) skip++;
        i = skip;
        continue;
      }
      escape = true;
      i++;
      continue;
    }
    if (c == '"') return true;
    i++;
  }
  if (continues) *continues = escape;
  return false;
}

// Strip a comment (# to end of line) from one line, respecting quoted strings.
// A `"""`/`'''` multi-line string is consumed as ONE delimiter and its body is
// skipped in a single step: a '#' inside a multi-line string is content, not a
// start of a comment, so the quote run must not be counted as individual
// single-line toggles. Counting them truncated the text after an even number of
// quotes on the opening line (`key = """a" # kept` lost ` # kept`, and a
// multi-line string nested in a one-line array `[ """a" # b" ]` no longer
// balanced). A body with no closer on this line runs to the end of the line:
// the caller gathers the continuation lines unstripped, so nothing inside a
// multi-line string can be eaten as a comment.
inline std::string_view stripComment(std::string_view line, bool& inBasic, bool& inLiteral) {
  bool escape = false;
  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    if (escape) { escape = false; continue; }
    if (c == '\\' && inBasic) { escape = true; continue; }
    if (!inBasic && !inLiteral && (c == '"' || c == '\'')) {
      if (i + 2 < line.size() && line[i + 1] == c && line[i + 2] == c) {
        size_t after = tomlSkipString(line, i);
        if (after >= line.size()) return line; // unterminated here: rest is content
        i = after - 1;
        continue;
      }
    }
    if (c == '"' && !inLiteral) { inBasic = !inBasic; continue; }
    if (c == '\'' && !inBasic) { inLiteral = !inLiteral; continue; }
    if (!inBasic && !inLiteral && c == '#') return line.substr(0, i);
  }
  return line;
}

// Length of the leading date/date-time token in `s` (offset-date-time, local
// date-time, optionally space-separated from the time part), or npos when `s`
// does not start with one. Month/day ranges are validated; the bare local-time
// form (HH:MM:SS) is recognised by its own branch in parseTomlValue.
inline size_t tomlDateTimeEnd(std::string_view s) {
  if (s.size() < 10 || !std::isdigit(static_cast<unsigned char>(s[0])) ||
      s[4] != '-' || s[7] != '-')
    return std::string_view::npos;
  int month = (s[5] - '0') * 10 + (s[6] - '0');
  int day = (s[8] - '0') * 10 + (s[9] - '0');
  if (month < 1 || month > 12 || day < 1 || day > 31) return std::string_view::npos;
  auto isDt = [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) || ch == 'T' || ch == ':' ||
           ch == '.' || ch == 'Z' || ch == '+' || ch == '-';
  };
  size_t end = 10;
  while (end < s.size() && isDt(s[end])) end++;
  // A space may separate the date and the time (local date-time)
  if (end < s.size() && s[end] == ' ' && end + 1 < s.size() &&
      std::isdigit(static_cast<unsigned char>(s[end + 1]))) {
    end++;
    while (end < s.size() && isDt(s[end])) end++;
  }
  return end;
}

// Parse a complete TOML value into JSON string
constexpr int MAX_TOML_DEPTH = 64;

inline std::string parseTomlValue(std::string_view s, int depth = 0, bool strictTail = false, std::unordered_set<std::string>* inlineTablePaths = nullptr, const std::string& prefix = "") {
  s = trimSV(s);
  if (depth > MAX_TOML_DEPTH) throw asvJSONError("TOML value nesting too deep");
  if (s.empty()) return "null";
  auto stripCommentTail = [&]() {
    // A '#' after a closed value (ml strings, inline tables, arrays) is a
    // comment; everything to end of line is dropped. Only applied to trailing
    // junk, never inside a value or string.
    s = trimSV(s);
    if (!s.empty() && s[0] == '#') {
      size_t e = s.find_first_of("\r\n");
      s = (e == std::string_view::npos) ? std::string_view{} : s.substr(e);
    }
  };
  auto rejectTrailing = [&]() {
    if (strictTail) {
      stripCommentTail();
      if (!trimSV(s).empty())
        throw asvJSONError("trailing content after value");
    }
  };
  auto checkTailLen = [&](size_t n) {
    if (strictTail) {
      std::string_view t = trimSV(s.substr(n));
      if (!t.empty() && t[0] == '#') {
        size_t e = t.find_first_of("\r\n");
        t = (e == std::string_view::npos) ? std::string_view{} : t.substr(e);
      }
      if (!trimSV(t).empty())
        throw asvJSONError("trailing content after value");
    }
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
      bool inMultiBasic = false, inMultiLiteral = false;
      while (!s.empty()) {
        // A depth-0 '}' right at the current position (also immediately after
        // a closing quote -- the quote toggles below continue past it) is the
        // table closer: leave it for the outer loop, never swallow it into
        // the raw value text that parseTomlValue() will re-parse.
        if (!inBasic && !inLiteral && !escape && !inMultiBasic && !inMultiLiteral && !valStr.empty() && s[0] == '}' && bracketDepth == 0)
          break;
        char c = s[0]; s.remove_prefix(1);
        if (escape) { escape = false; valStr += c; continue; }
        if (c == '\\' && (inBasic || inMultiBasic)) { escape = true; valStr += c; continue; }
        // Triple-quote detection for multiline strings
        if (!inMultiBasic && !inMultiLiteral && c == '"' && !inLiteral) {
          if (s.size() >= 2 && s[0] == '"' && s[1] == '"') {
            inMultiBasic = true; valStr += "\"\"\""; s.remove_prefix(2); continue;
          }
          inBasic = !inBasic; valStr += c; continue;
        }
        if (!inMultiBasic && !inMultiLiteral && c == '\'' && !inBasic) {
          if (s.size() >= 2 && s[0] == '\'' && s[1] == '\'') {
            inMultiLiteral = true; valStr += "'''"; s.remove_prefix(2); continue;
          }
          inLiteral = !inLiteral; valStr += c; continue;
        }
        // Close multiline string delimiters
        if (inMultiBasic && c == '"' && s.size() >= 2 && s[0] == '"' && s[1] == '"') {
          inMultiBasic = false; valStr += "\"\"\""; s.remove_prefix(2); continue;
        }
        if (inMultiLiteral && c == '\'' && s.size() >= 2 && s[0] == '\'' && s[1] == '\'') {
          inMultiLiteral = false; valStr += "'''"; s.remove_prefix(2); continue;
        }
        // Newlines are allowed inside multiline strings
        if (c == '\n' || c == '\r') {
          if (inMultiBasic || inMultiLiteral || bracketDepth > 0) { valStr += c; continue; }
          throw asvJSONError("newline not allowed in inline table");
        }
        if (!inBasic && !inLiteral && !inMultiBasic && !inMultiLiteral) {
          if (c == '{' || c == '[') bracketDepth++;
          if (c == '}' || c == ']') bracketDepth--;
          if (c == ',' && bracketDepth == 0) { commaBreak = true; break; }
        }
        valStr += c;
      }
      if (trimSV(std::string_view(valStr)).empty())
        throw asvJSONError("missing value for key in inline table");
      std::string childPrefix = prefix;
      for (size_t ki = 0; ki < keys.size(); ki++) {
        if (!childPrefix.empty()) childPrefix += '.';
        childPrefix += keys[ki];
      }
      std::string valJson = parseTomlValue(trimSV(std::string_view(valStr)), depth + 1, true, inlineTablePaths, childPrefix);
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
    // Mark all paths created by this inline table as immutable (closed for extension)
    if (inlineTablePaths) {
      std::function<void(const asvJSONValue*, const std::string&)> markPaths;
      markPaths = [&](const asvJSONValue* objVal, const std::string& p) {
        if (!objVal || objVal->type != asvJSONValue::OBJECT || !objVal->obj) return;
        for (auto& kv : *objVal->obj) {
          std::string fullPath = p.empty() ? kv.first : p + "." + kv.first;
          inlineTablePaths->insert(fullPath);
          markPaths(kv.second.get(), fullPath);
        }
      };
      if (!prefix.empty()) inlineTablePaths->insert(prefix);
      markPaths(obj.get(), prefix);
    }
    std::string json;
    obj->serialize(json, false);
    return json;
  }

  // Multi-line basic string """..."""
  if (s.size() >= 3 && s[0] == '"' && s[1] == '"' && s[2] == '"') {
    s.remove_prefix(3);
    std::string val;
    // Trim the single line break immediately after the opening delimiter
    // (LF or CRLF); a second \n keeps as content ("\n\n" -> "\n").
    if (!s.empty() && s[0] == '\r') s.remove_prefix(1);
    if (!s.empty() && s[0] == '\n') s.remove_prefix(1);
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
      if (c == '\\') {
        // A backslash escapes the next character, so `\"""` is the escape `\"`
        // (one quote) plus the two literal quotes that follow it -- the generic
        // escape path below already yields `"""` here, and consuming the three
        // quotes as one unit would swallow a real closer when they start a run
        // of four (`x = """\"""""`).
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
      if (c == '"') {
        // A run of 1-2 unescaped quotes is content; a run of 3+ closes the
        // string with its final three quotes, any earlier quotes in the run
        // keep as content (e.g. `"""""` = two content quotes + the closer).
        size_t r = 0;
        while (r < s.size() && s[r] == '"') r++;
        size_t total = 1 + r; // including the already-consumed c
        if (total >= 3) {
          val.append(total - 3, '"');
          s.remove_prefix(r);
          break;
        }
        val.append(total, '"');
        s.remove_prefix(r);
        continue;
      }
      val += c;
    }
    rejectTrailing();
    return "\"" + tomlJsonEscape(val) + "\"";
  }

  // Multi-line literal string '''...'''
  if (s.size() >= 3 && s[0] == '\'' && s[1] == '\'' && s[2] == '\'') {
    s.remove_prefix(3);
    // Trim the single line break immediately after the opening delimiter
    // (LF or CRLF); a second \n keeps as content ("\n\n" -> "\n").
    if (!s.empty() && s[0] == '\r') s.remove_prefix(1);
    if (!s.empty() && s[0] == '\n') s.remove_prefix(1);
    std::string val;
    for (;;) {
      if (s.empty()) throw asvJSONError("unterminated string");
      char c = s[0]; s.remove_prefix(1);
      if (c == '\'') {
        // A run of 1-2 unescaped quotes is content; a run of 3+ closes the
        // string with its final three quotes, any earlier quotes in the run
        // keep as content (e.g. `'''''` = two content quotes + the closer).
        size_t r = 0;
        while (r < s.size() && s[r] == '\'') r++;
        size_t total = 1 + r; // including the already-consumed c
        if (total >= 3) {
          val.append(total - 3, '\'');
          s.remove_prefix(r);
          break;
        }
        val.append(total, '\'');
        s.remove_prefix(r);
        continue;
      }
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
    bool inMultiBasic = false, inMultiLiteral = false;
    bool closed = false;
    auto flushVal = [&]() {
      std::string trimmed = std::string(trimSV(std::string_view(cur)));
      if (!trimmed.empty()) {
        if (!first) json += ',';
        first = false;
        json += parseTomlValue(std::string_view(trimmed), depth + 1, true, inlineTablePaths, prefix);
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
      if (c == '\\' && (inBasic || inMultiBasic)) { escape = true; cur += c; continue; }
      // Triple-quote detection for multiline strings
      if (!inMultiBasic && !inMultiLiteral && c == '"' && !inLiteral) {
        if (s.size() >= 2 && s[0] == '"' && s[1] == '"') {
          inMultiBasic = true; cur += "\"\"\""; s.remove_prefix(2); continue;
        }
        inBasic = !inBasic; cur += c; continue;
      }
      if (!inMultiBasic && !inMultiLiteral && c == '\'' && !inBasic) {
        if (s.size() >= 2 && s[0] == '\'' && s[1] == '\'') {
          inMultiLiteral = true; cur += "'''"; s.remove_prefix(2); continue;
        }
        inLiteral = !inLiteral; cur += c; continue;
      }
      // Close multiline string delimiters
      if (inMultiBasic && c == '"' && s.size() >= 2 && s[0] == '"' && s[1] == '"') {
        inMultiBasic = false; cur += "\"\"\""; s.remove_prefix(2); continue;
      }
      if (inMultiLiteral && c == '\'' && s.size() >= 2 && s[0] == '\'' && s[1] == '\'') {
        inMultiLiteral = false; cur += "'''"; s.remove_prefix(2); continue;
      }
      if (!inBasic && !inLiteral && !inMultiBasic && !inMultiLiteral) {
        if (c == '#') { skipToEol(); continue; }
        // Newlines separate elements in a multiline array (comma optional)
        if (c == '\n' || c == '\r') { flushVal(); continue; }
        if (c == '[') { bracketDepth++; cur += c; continue; }
        if (c == ']') {
          if (bracketDepth == 0) {
            // A trailing comma is legal in both single-line and multiline arrays (TOML 1.0)
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
  if (size_t dtEnd = tomlDateTimeEnd(s); dtEnd != std::string_view::npos) {
    checkTailLen(dtEnd);
    return "\"" + std::string(s.substr(0, dtEnd)) + "\"";
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
      if (c == '\\') {
        // Line continuation: a backslash directly before a line break discards
        // itself, the break and the next line's indentation (a TOML 0.5 rule
        // kept for single-line strings as a lenient extension -- TOML 1.0 moved
        // it to multi-line strings only). fromTOML gathers the continuation
        // lines before this parser sees the value.
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
          uint64_t digit = (c <= '9') ? static_cast<uint64_t>(c - '0')
                                      : static_cast<uint64_t>(std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
          if (hv > (UINT64_MAX - digit) / 16) throw asvJSONError("hex integer out of range");
          hv = hv * 16 + digit; pos++;
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
          uint64_t digit = static_cast<uint64_t>(c - '0');
          if (ov > (UINT64_MAX - digit) / 8) throw asvJSONError("octal integer out of range");
          ov = ov * 8 + digit; pos++;
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
          uint64_t digit = static_cast<uint64_t>(c - '0');
          if (bv > (UINT64_MAX - digit) / 2) throw asvJSONError("binary integer out of range");
          bv = bv * 2 + digit; pos++;
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
        std::string vs = tomlVal(val.get());
        if (!vs.empty()) out += qk + " = " + vs + "\n";
      }
    } else {
      // Expanded: use [section] headers for nested objects.
      // TOML scopes bare "key = value" lines to the current table (the most
      // recent [header]). A table's own scalar members must therefore be
      // emitted BEFORE any child subtable header: else a sibling emitted after
      // "[child]" would leak into the child table ({"o":{"a":1},"y":1} would
      // move y inside o). Scalar members first, subtable headers after.
      auto isSubtable = [](const asvJSONValue* val) {
        if (val->type == T::OBJECT) return true;
        if (val->type == T::ARRAY && val->arr && !val->arr->empty()) {
          for (const auto& e : *val->arr)
            if (e->type != T::OBJECT) return false;
          return true;
        }
        return false;
      };
      if (needHeader && !prefix.empty()) {
        emitHeader(prefix, false);
      }
      // First pass: this table's scalar members (including primitive arrays).
      for (const auto& [k, val] : *v->obj) {
        if (isSubtable(val.get())) continue;
        std::string qk = tomlQuoteKey(k);
        std::string vs = tomlVal(val.get());
        if (!vs.empty()) {
          out += qk + " = " + vs + "\n";
        }
      }
      // Second pass: child subtable headers.
      for (const auto& [k, val] : *v->obj) {
        if (!isSubtable(val.get())) continue;
        std::string qk = tomlQuoteKey(k);
        std::string fullKey = prefix.empty() ? qk : prefix + "." + qk;
        if (val->type == T::OBJECT) {
          tomlEmitVal(val.get(), fullKey, true, out, true);
        } else {
          // Array of tables: one `[[path]]` header per element. Elements may
          // carry subtables with the same name -- TOML resolves every reference
          // to an array of tables to the most recently defined element, so
          // `[[items]] [items.s] [[items]] [items.s]` is legal and each
          // `[items.s]` lands in the element above it. No inline-table
          // fallback is needed for that case.
          for (const auto& elem : *val->arr) {
            emitHeader(fullKey, true);
            tomlEmitVal(elem.get(), fullKey, false, out, true);
          }
        }
      }
    }
  } else if (v->type == T::ARRAY && v->arr) {
    // Only tables carry `[key]` headers, never arrays. This branch is reached
    // (when at all) with `needHeader` already false -- the callers hand in an
    // OBJECT or OBJECT-array elements -- so forward it explicitly as false
    // rather than re-forwarding the parameter.
    for (const auto& elem : *v->arr)
      tomlEmitVal(elem.get(), prefix, false, out, pretty);
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
    std::string valueOut = tomlVal(this);
    // TOML has no null, so a NULL scalar ROOT (tomlVal returns "") would emit
    // the invalid line `value = `. Emit an empty document instead -- the same
    // silent-drop rule that governs NULL inside objects and arrays.
    if (!valueOut.empty()) out += "value = " + valueOut + "\n";
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

// Key identifying where a table header / dotted key actually lands in the tree.
// Identical to the plain dotted path unless the path crosses an array of
// tables: that segment is annotated with the index of its current last element,
// so `[items.s]` after a second `[[items]]` is a DIFFERENT table than the first
// one. TOML resolves every reference to an array of tables to the most recently
// defined element, so re-opening `[items.s]` under a new element is legal and
// only an unresolved repeat is a redefinition. Segments the tree does not have
// (yet) keep their plain name, so a dotted key registers exactly the paths a
// later header would compute.
inline std::string tomlCanonicalPath(const asvJSONValue* root,
                                    const std::vector<std::string>& path) {
  std::string key;
  const asvJSONValue* cur = root;
  for (const std::string& seg : path) {
    if (!key.empty()) key += '.';
    key += seg;
    if (!cur || cur->type != asvJSONValue::OBJECT || !cur->obj) { cur = nullptr; continue; }
    auto it = cur->obj->find(seg);
    if (it == cur->obj->end()) { cur = nullptr; continue; }
    cur = it->second.get();
    if (cur->type == asvJSONValue::ARRAY && cur->arr && !cur->arr->empty()) {
      char idx[24];
      snprintf(idx, sizeof(idx), "[%zu]", cur->arr->size() - 1);
      key += idx;
      cur = cur->arr->back().get();
    }
  }
  return key;
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
    // Dotted keys implicitly define every super-table they create; the [table]
    // header form may not later redefine those (unlike plain header paths,
    // whose implicit super-tables stay reopenable).
    auto markDottedSupers = [&](const std::vector<std::string>& pathParts) {
      for (size_t i = 0; i + 1 < pathParts.size(); i++) {
        std::vector<std::string> prefix(pathParts.begin(), pathParts.begin() + i + 1);
        tableDefs.insert(tomlCanonicalPath(root.get(), prefix));
      }
    };
    bool stripInBasic = false, stripInLiteral = false; // persistent state for stripComment
    // Paths that were opened by an array-of-tables header (`[[a.b]]`). The
    // array lives at the parent key 'b'; the FULL dotted path is tracked so
    // two different parents with the same leaf name (`[[a.x]]`, `[[b.x]]`)
    // cannot collide and silently defeat the static-array guard below.
    std::unordered_set<std::string> arrayTablePaths;
    // Paths created by inline tables are immutable -- headers and key=value
    // that would extend them are rejected at parse time.
    std::unordered_set<std::string> inlineTablePaths;
    for (size_t lineNum = 0; lineNum < lines.size(); lineNum++) {
      std::string_view line = trimSV(lines[lineNum]);
      if (line.empty()) continue;

      line = stripComment(line, stripInBasic, stripInLiteral);
      line = trimSV(line);
      if (line.empty()) continue;

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
        if (trimSV(headerPath).empty())
          throw asvJSONError("empty table header");
        tablePath = parseTomlKeyPath(headerPath);
        isArrayTable = isArr;

        // Reject headers that extend an inline-table path (inline tables are
        // immutable). inlineTablePaths is keyed by CANONICAL path (the
        // [N]-marked form tomlCanonicalPath builds), so the comparison must use
        // canonical paths too: a plain "items.foo" collides with the foo of a
        // DIFFERENT [[items]] element and would wrongly reject that element's
        // legal [items.foo] header. Same reasoning as defKey below.
        if (!tablePath.empty()) {
          for (size_t i = 1; i <= tablePath.size(); i++) {
            std::vector<std::string> sub(tablePath.begin(), tablePath.begin() + i);
            std::string canon = tomlCanonicalPath(root.get(), sub);
            if (inlineTablePaths.count(canon))
              throw asvJSONError("cannot extend inline table '" + canon + "'");
          }
        }

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
          // Definition key of the table being (re)opened: the canonical path
          // through the resolved tree, so a subtable header under a new array
          // element does not collide with the same header under the previous one.
          std::string defKey = tomlCanonicalPath(root.get(), parentPath);
          if (!defKey.empty()) defKey += '.';
          defKey += last;
          if (isArrayTable) {
            if (it != parent->obj->end() && it->second->type != asvJSONValue::ARRAY)
              throw asvJSONError("cannot redefine key '" + last + "' as array table");
            // Appending to a statically-defined array (created by `arr = [...]`)
            // is a parse error (even if empty), but re-appending through a
            // `[[...]]` header or a sub-array-of-tables header on the last
            // element is allowed.
            if (it != parent->obj->end() && it->second->type == asvJSONValue::ARRAY &&
                !arrayTablePaths.count(fullPath))
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
            // Each element is its own table, so the key carries the new index
            tableDefs.insert(tomlCanonicalPath(root.get(), tablePath));
            arrayTablePaths.insert(fullPath);
          } else {
            if (tableDefs.count(defKey))
              throw asvJSONError("table '" + fullPath + "' already defined");
            if (it != parent->obj->end() && it->second->type != asvJSONValue::OBJECT)
              throw asvJSONError("cannot redefine key '" + last + "' as table");
            if (it == parent->obj->end()) {
              auto newObj = asvJSONValue::makeObject();
              parent->obj->emplace(last, std::move(newObj));
            }
            tableDefs.insert(defKey);
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
        // A close that lands right at the opening position is the empty-string
        // form (`""""""`); a close preceded by an escape is content, so the scan
        // has to skip escapes to find the real terminator.
        size_t closePos = tomlFindUnescapedDelim(vtmp, 3, delim, isBasic);
        bool hasTrailingContent = false;
        if (closePos != std::string_view::npos && closePos > 2) {
          // Check that everything after the closing delimiter is whitespace/comment
          // (vtmp.substr() would return a temporary std::string, leaving `after`
          // dangling -- the view has to be taken over the member string)
          std::string_view after(vtmp.data() + closePos + 3, vtmp.size() - closePos - 3);
          after = trimSV(after);
          if (!after.empty() && after[0] != '#')
            hasTrailingContent = true;
        }
        if (closePos != std::string_view::npos && closePos > 2 && !hasTrailingContent) {
          // The string closes on this line: hand the whole value to
          // parseTomlValue, which enforces the closing delimiter and any
          // trailing content (previously trailing text was silently dropped).
          valueStr = vtmp;
          // Reset persistent stripComment state -- the multiline string
          // was fully consumed on this line.
          stripInBasic = false;
          stripInLiteral = false;
        } else if (hasTrailingContent) {
          // Appears closed but has trailing content: pass to parseTomlValue
          // which will reject it with a proper error instead of gathering
          // continuation lines and eventually throwing "unclosed multi-line string".
          valueStr = vtmp;
          stripInBasic = false;
          stripInLiteral = false;
        } else {
          std::string accum = vtmp;
          bool found = false;
          for (size_t ml = lineNum + 1; ml < lines.size(); ml++) {
            std::string mlLine = std::string(lines[ml]);
            if (tomlFindUnescapedDelim(mlLine, 0, delim, isBasic) != std::string::npos) {
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
          // Reset persistent stripComment state -- multiline string closed (or will throw)
          stripInBasic = false;
          stripInLiteral = false;
        }
      } else if (vtmp.size() >= 2 && vtmp[0] == '"' &&
                 !(vtmp.size() >= 3 && vtmp.substr(0, 3) == "\"\"\"")) {
        // Single-line basic string: TOML 0.5 let a string span lines via a
        // trailing `\` (the backslash, line break and the next line's
        // indentation are folded away). Gather the continuation lines only when
        // the opening line ends in an ACTIVE backslash (odd trailing run) -- a
        // plain unterminated `a = "abc` has no continuation and still fails
        // immediately instead of eating later lines. parseTomlValue performs
        // the actual fold in its basic-string branch.
        bool cont = false;
        if (!tomlBasicStrClosed(std::string_view(vtmp), &cont) && cont) {
          std::string accum = vtmp;
          for (size_t ml = lineNum + 1; ml < lines.size(); ml++) {
            accum += '\n' + std::string(lines[ml]);
            lineNum = ml;
            if (tomlBasicStrClosed(std::string_view(accum), nullptr)) break;
          }
          // Reset persistent stripComment state -- string closed (or will throw)
          stripInBasic = false;
          stripInLiteral = false;
          valueStr = accum;
        } else {
          valueStr = vtmp;
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
      } else if (!vtmp.empty() && vtmp[0] == '{' &&
                 !tomlTableBalanced(std::string_view(vtmp))) {
        // Multiline inline table: gather continuation lines until the braces balance
        std::string accum = vtmp;
        bool found = false;
        for (size_t ml = lineNum + 1; ml < lines.size(); ml++) {
          accum += '\n' + std::string(lines[ml]);
          lineNum = ml;
          if (tomlTableBalanced(std::string_view(accum))) {
            found = true;
            break;
          }
        }
        if (!found) throw asvJSONError("unclosed inline table");
        stripInBasic = false;
        stripInLiteral = false;
        valueStr = accum;
      } else {
        valueStr = vtmp;
      }

      // A key must have an explicit value (`key =` is invalid per the spec)
      if (trimSV(std::string_view(valueStr)).empty())
        throw asvJSONError("missing value for key");

      // Canonical prefix: an array-of-tables segment carries its current [N]
      // index, so an inline table registered under one [[items]] element is not
      // confused with the same-named key under another element (the plain path
      // collides across elements).
      std::string prefix = tomlCanonicalPath(root.get(), tablePath);
      for (size_t ki = 0; ki < keys.size(); ki++) {
        if (!prefix.empty()) prefix += '.';
        prefix += keys[ki];
      }
      // Snapshot the set of inline-table paths that exist *before* this line's
      // value is parsed. parseTomlValue() registers every inline table it
      // builds (including the current key via `prefix`), so the regular
      // key=value branch below must only reject extensions of inline tables
      // that already existed -- never the very line that just defined one
      // (otherwise `point = {x = 1, y = 2}` self-rejects with
      // "cannot extend inline table 'point'").
      std::unordered_set<std::string> preInlinePaths = inlineTablePaths;

      std::string jsonVal = parseTomlValue(std::string_view(valueStr), 0, true, &inlineTablePaths, prefix);

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
        // Reject extending an inline table that already existed in THIS
        // element (inline tables are immutable), the same rule the regular
        // branch applies. Without it `[[items]]\nfoo = {x=1}\nfoo.y = 2`
        // silently added to foo. Canonical paths so a same-named inline table
        // under a different [[items]] element is not confused with this one.
        {
          std::vector<std::string> fullPath = tablePath;
          fullPath.insert(fullPath.end(), keys.begin(), keys.end());
          for (size_t i = 1; i <= fullPath.size(); i++) {
            std::vector<std::string> sub(fullPath.begin(), fullPath.begin() + i);
            std::string canon = tomlCanonicalPath(root.get(), sub);
            if (preInlinePaths.count(canon))
              throw asvJSONError("cannot extend inline table '" + canon + "'");
          }
        }
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
        // Reject key=value that extends an inline-table path. Only paths that
        // already existed *before this line* count: parseTomlValue() registers
        // the current key itself (via `prefix`) when it builds an inline table,
        // so checking the live set here would self-reject the very line that
        // defines the inline table (`point = {x = 1, y = 2}` would fail with
        // "cannot extend inline table 'point'").
        for (size_t i = 1; i <= fullPath.size(); i++) {
          std::vector<std::string> sub(fullPath.begin(), fullPath.begin() + i);
          std::string canon = tomlCanonicalPath(root.get(), sub);
          if (preInlinePaths.count(canon))
            throw asvJSONError("cannot extend inline table '" + canon + "'");
        }
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
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromMessagePack).
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
