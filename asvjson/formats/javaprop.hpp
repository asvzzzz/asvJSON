#pragma once
// Java .properties serialization/parsing for asvJSON++.
//
// The classic Java properties format -- flat key/value pairs:
//
//   # comment
//   ! also-a-comment
//   name = My App
//   port: 8080
//   fruits apple, banana
//
// Separators: '=', ':', or the first whitespace. '#'/'!' comment lines.
// '\' line continuation joins a physical line with the next (the backslash
// plus the next line's leading whitespace are dropped). Escapes: \\ \t \n \r
// \f and \uXXXX (surrogate pairs combined into 4-byte UTF-8). There is no
// quoting and no inline comment stripping -- a '#' mid-value is plain data.
// An unescaped trailing whitespace run of a value is stripped on load (the
// JDK behavior); leading whitespace of a value is skipped.
//
// Bare values are type-inferred (null/bool/int/double/datetime/string) like
// dotenv/INI. Duplicate keys follow last-wins. Empty keys, which the format
// formally allows (`=value`), are rejected on BOTH sides: fromJavaProp() and
// toJavaProp() each throw "javaprop: empty key", because the object store
// cannot hold a nameless member and a parsed one could not be re-emitted.
// Bare values accept a leading `+` as well (e.g. `+5` -> INT 5, `+3.14` -> DOUBLE);
// the plain JSON grammar rejects a leading sign, and the JDK does not type at all,
// but this library's bare-value inference follows the same lenient convention as
// INI/dotenv/CSV -- the sign is part of the number token for strtod/from_chars.
// Comments are preserved via the shared `Comments` metadata (leading, trailing,
// footing) and re-emitted by toJavaProp(). The MARKUP normalizes, though: the
// loader strips exactly one space after the marker, and the encoder always
// writes `# `, so a `!`-style comment or a space-less `#text` loads as text and
// comes back as `# text`. And a comment block the encoder emits under a value's
// `trailing` is read back as the NEXT key's `leading` (or the root's `footing`
// after the last value), because the parser never produces trailing comments
// itself (there is no inline-comment syntax).
//
// Lossy on encode, because .properties has no syntax for these:
//   - nested objects and arrays are written as their compact JSON text, escaped
//     as a bare value (the text survives, but decodes back as a STRING);
//   - the remaining special types (TIMESTAMP, OBJECTID, REGEX, CUSTOM_TAG)
//     ride serialize()'s extended-JSON text the same way -- `{"$timestamp":
//     {"t":...}}`, `{"$oid":"..."}`, `{"$regex":...,"$options":...}`,
//     `{"$customTag":{"name":...,"value":...}}` -- which reads back as a plain
//     STRING holding that JSON text (type lost, payload kept);
//   - BINARY and EXTENSION are written as base64 of their bytes, which decodes
//     back as a plain STRING holding that base64 -- both the type and the bytes
//     are lost, since nothing tries to reverse the encoding on load;
//   - a STRING whose text would type-infer as one of the bare scalar forms ("5",
//     "true", "null", "3.14", an ISO-8601 timestamp) is emitted bare and reads
//     back as that typed value, not as a STRING. The format has no quoting, and
//     escaping cannot help because the escape is decoded before inference
//     (`\u0035` decodes to `5` and is then typed INT), so the STRING-ness is
//     unrecoverable. INI/dotenv avoid this with a quoted form; here it is a
//     documented loss, like CSV/TOML.
//   - the encoder writes non-Latin characters as raw UTF-8 bytes (only C0/DEL are
//     escaped via `\uXXXX`); the loader reads those same bytes back as UTF-8 so a
//     self round-trip is fine, but a JDK `store()` file expects ISO-8859-1 and will
//     misread bytes >= 0x80 (the JDK writer escapes non-Latin characters as `\uXXXX`).
//   - a comment whose text contains `\n` is emitted as one `#` line per physical
//     line, so it re-reads as several separate Comment entries rather than one
//     multi-line comment -- the single-comment-to-multiple-comments loss mirrors
//     what INI/dotenv do with trailing comment text.
// DATETIME is not lossy: it is written in ISO form and tryParseDateTime() gives
// the type back on load. INT/DOUBLE/BOOL/NULL keep their inferred types (a
// finite double whose fmtDoubleVal literal carries no '.', 'e' or 'E' gets a
// ".0" suffix so it re-loads as DOUBLE, not INT; NaN is written as the bare token
// `nan` with no sign bit preserved -- `-NaN` decodes back as positive NaN, while
// only `-Infinity` keeps its negative sign. This spelling (`nan`/`inf`/`-inf`)
// matches the INI/dotenv/plist convention; the guard against stamping ".0" on top
// (`nan.0`) is needed for both the strtod fallback and the `std::from_chars` fast
// path, since from_chars stops at the first non-digit after `nan`). One exception:
// a timestamp gmtimeSafe cannot convert to UTC (e.g. before 1970 or beyond the
// platform's range) renders as the bare text `null` via fmtDateTimeVal and
// loads back as NULL -- the type is lost there.
//
// A malformed \u escape is kept literally rather than rejected (the JDK throws
// IllegalArgumentException): "\uZZZZ" loads as the text `uZZZZ`, and a truncated
// escape such as "\u00" as `u00`.
//
// Locale-sensitivity: double parsing uses std::from_chars when available
// (locale-independent, spec-compliant); falls back to std::strtod which
// honors LC_NUMERIC. A comma-decimal locale (de_DE, fr_FR) would read "1.5"
// as STRING("1.5") instead of DOUBLE(1.5). The from_chars path is available
// on C++17 toolchains with libc++ or libstdc++ >= 11; the strtod fallback
// is used on older libstdc++ and some compilers lacking full to_chars support.
// Callers in comma-decimal locales should either setlocale(LC_ALL, "C") before
// parsing or use the core parser via fromJSON() with explicit quoting.
//
// fromAny() detection is a heuristic, not a parser: it needs a line with an '='
// or an unspaced ':' before any whitespace on that line, an '!' comment or a \u
// escape. So "name=My App" and "key:value" signal, while "name = My App" (the '='
// only after whitespace), a whitespace-only separator ("key value") or "key:
// value" is deliberately left to the YAML/GOON/TOON cascade, since those are far
// more common. Call fromJavaProp() directly for such files.

#include "../core.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
#define ASVJSON_JAVAPROP_USE_FROM_CHARS 1
#endif

namespace asvJSONInternal {

// Infer a typed value from a bare .properties string. Mirrors the INI/dotenv
// convention: JSON-shaped numbers only, leading-zero/hex literals kept as
// text, out-of-range numbers preserved as raw_number strings.
inline std::unique_ptr<asvJSONValue> javapropParseVal(const std::string& s) {
  if (s == "null") return asvJSONValue::makeNull();
  if (s == "true" || s == "True" || s == "TRUE") return asvJSONValue::makeBool(true);
  if (s == "false" || s == "False" || s == "FALSE") return asvJSONValue::makeBool(false);
  {
    time_t ts = 0; int ms = 0; int tz = 0;
    if (tryParseDateTime(s, ts, &ms, &tz))
      return asvJSONValue::makeDateTime(ts, ms, tz);
  }
  if (!s.empty()) {
    size_t i0 = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (s.size() > i0 + 1 && s[i0] == '0' && s[i0 + 1] >= '0' && s[i0 + 1] <= '9')
      return asvJSONValue::makeString(s.data(), s.size());
    if (s.size() > i0 + 1 && s[i0] == '0' && (s[i0 + 1] == 'x' || s[i0 + 1] == 'X'))
      return asvJSONValue::makeString(s.data(), s.size());
    {
      errno = 0;
      char* end = nullptr;
      long long ll = std::strtoll(s.c_str(), &end, 10);
      if (end && *end == 0 && end == s.c_str() + s.size()) {
        if (errno == ERANGE) {
          auto v = asvJSONValue::makeStringView(std::string_view(s));
          if (v) v->raw_number = true;
          return v;
        }
        return asvJSONValue::makeInt(static_cast<int64_t>(ll));
      }
    }
#if defined(ASVJSON_JAVAPROP_USE_FROM_CHARS)
    {
      double d2 = 0;
      auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), d2);
      if (ec == std::errc() && ptr == s.data() + s.size()) return asvJSONValue::makeDouble(d2);
    }
#endif
    {
      errno = 0;
      char* end2 = nullptr;
      double d = std::strtod(s.c_str(), &end2);
      if (end2 && *end2 == 0 && end2 == s.c_str() + s.size()) {
        if (errno == ERANGE) {
          auto v = asvJSONValue::makeStringView(std::string_view(s));
          if (v) v->raw_number = true;
          return v;
        }
        return asvJSONValue::makeDouble(d);
      }
    }
  }
  return asvJSONValue::makeString(s.data(), s.size());
}

inline void javapropAppendCp(std::string& out, uint32_t cp) {
  if (cp < 0x80) { out += static_cast<char>(cp); return; }
  if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
    return;
  }
  if (cp >= 0xD800 && cp <= 0xDFFF) { out += "\xEF\xBF\xBD"; return; } // lone surrogate
  if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
    return;
  }
  out += static_cast<char>(0xF0 | (cp >> 18));
  out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out += static_cast<char>(0x80 | (cp & 0x3F));
}

inline int javapropHexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Process Java escape sequences: `\t \n \r \f \\` and `\uXXXX` (a high
// surrogate immediately followed by a low `\uYYYY` is combined into a 4-byte
// codepoint). Any other `\x` collapses to the bare character `x`.
inline std::string javapropUnescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; i++; continue; }
    char e = s[i + 1];
    switch (e) {
      case 't': out += '\t'; i += 2; break;
      case 'n': out += '\n'; i += 2; break;
      case 'r': out += '\r'; i += 2; break;
      case 'f': out += '\f'; i += 2; break;
      case '\\': out += '\\'; i += 2; break;
      case 'u': {
        if (i + 6 <= s.size()) {
          uint32_t cp = 0;
          bool ok = true;
          for (int k = 0; k < 4; k++) {
            int h = javapropHexDigit(s[i + 2 + k]);
            if (h < 0) { ok = false; break; }
            cp = (cp << 4) | static_cast<uint32_t>(h);
          }
          if (ok) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 12 <= s.size() &&
                s[i + 6] == '\\' && s[i + 7] == 'u') {
              uint32_t lo = 0;
              bool ok2 = true;
              for (int k = 0; k < 4; k++) {
                int h = javapropHexDigit(s[i + 8 + k]);
                if (h < 0) { ok2 = false; break; }
                lo = (lo << 4) | static_cast<uint32_t>(h);
              }
              if (ok2 && lo >= 0xDC00 && lo <= 0xDFFF) {
                uint32_t cp2 = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                javapropAppendCp(out, cp2);
                i += 12;
                break;
              }
            }
            javapropAppendCp(out, cp);
            i += 6;
            break;
          }
        }
        out += 'u';
        i += 2;
        break;
      }
      default: out += e; i += 2; break;
    }
  }
  return out;
}

// Join physical lines into logical lines: a physical line ending in an odd
// number of backslashes is continued on the next line (the continuation
// backslash and the next line's leading whitespace are dropped).
inline std::vector<std::string> javapropLogicalLines(std::string_view input) {
  std::vector<std::string> lines = splitLines(input);
  std::vector<std::string> logical;
  logical.reserve(lines.size());
  size_t li = 0;
  while (li < lines.size()) {
    std::string cur = lines[li];
    // Defense in depth: splitLines() already drops a trailing CR on every line,
    // so one here only means a caller fed us raw input through an exotic path.
    // Keeping it would fold the CR into the key or value. Mirrors INI/dotenv.
    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
    for (;;) {
      size_t bs = 0;
      while (bs < cur.size() && cur[cur.size() - 1 - bs] == '\\') bs++;
      if (bs % 2 == 0) break;            // no (unescaped) continuation
      cur.pop_back();                    // drop the continuation backslash
      if (li + 1 >= lines.size()) break; // trailing backslash at EOF
      li++;
      std::string next = lines[li];
      if (!next.empty() && next.back() == '\r') next.pop_back(); // as above
      size_t k = 0;
      while (k < next.size() && (next[k] == ' ' || next[k] == '\t' || next[k] == '\r')) k++;
      cur += next.substr(k);
    }
    logical.push_back(std::move(cur));
    li++;
  }
  return logical;
}

inline void javapropWriteCommentLines(std::string& out, const std::vector<Comment>& list) {
  for (const auto& c : list) {
    std::string_view text(c.text);
    size_t start = 0;
    for (;;) {
      size_t nl = text.find('\n', start);
      std::string_view seg = text.substr(start, nl == std::string_view::npos ? text.size() - start : nl - start);
      out += "# ";
      out += seg;
      out += '\n';
      if (nl == std::string_view::npos) break;
      start = nl + 1;
    }
  }
}

inline std::string javapropEscapeVal(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  // Leading whitespace of a value is skipped on load, so it must be escaped.
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    out += (s[i] == ' ') ? "\\ " : "\\t";
    i++;
  }
  for (; i < s.size(); i++) {
    char c = s[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\f': out += "\\f"; break;
      // An unescaped trailing whitespace run is stripped on load, so a literal
      // space in final position is data that has to be escaped -- otherwise
      // "a " round-trips as "a". Only the last character needs it: the loader
      // stops stripping at the first escaped space, which protects the whole
      // run behind it ("a  " -> "a \ " -> "a  "). Interior spaces stay literal,
      // matching what the JDK's own writer produces. The other whitespace
      // characters need no special case because they are always escaped above.
      case ' ':  out += (i + 1 == s.size()) ? "\\ " : " "; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20 || c == '\x7f') {
          char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c)); out += buf;
        } else out += c;
        break;
    }
  }
  return out;
}

inline std::string javapropEscapeKey(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\f': out += "\\f"; break;
      case ' ':  out += "\\ "; break;
      case '=':  out += "\\="; break;
      case ':':  out += "\\:"; break;
      case '#':  out += "\\#"; break;
      case '!':  out += "\\!"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20 || c == '\x7f') {
          char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c)); out += buf;
        } else out += c;
        break;
    }
  }
  return out;
}

inline void javapropFormatVal(const asvJSONValue* v, std::string& out) {
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::STRING:
      out += javapropEscapeVal(std::string_view(v->str_data));
      break;
    case T::INT:
      out += std::to_string(v->num);
      break;
    case T::DOUBLE: {
      // NaN/Infinity must be written bare: fmtDoubleVal renders them as
      // "nan"/"inf"/"-inf" (no '.', 'e' or 'E'), and stamping the ".0" suffix
      // on top would produce "nan.0". strtod stops at the first non-digit after
      // "nan", and std::from_chars (the fast path on C++17 toolchains with
      // libc++/libstdc++ >= 11) also rejects it -- both paths leave the full
      // string check failing, so the value would degrade to STRING. Note: NaN's
      // sign bit is lost here (always `nan`, never `-nan`), matching the INI /
      // dotenv / plist convention; only -Infinity preserves its negative sign.
      if (std::isnan(v->dbl)) { out += "nan"; break; }
      if (std::isinf(v->dbl)) { out += (v->dbl < 0 ? "-inf" : "inf"); break; }
      std::string tmp;
      fmtDoubleVal(v->dbl, tmp);
      if (tmp.find('.') == std::string::npos && tmp.find('e') == std::string::npos &&
          tmp.find('E') == std::string::npos)
        tmp += ".0";
      out += tmp;
      break;
    }
    case T::BOOL_VAL: out += v->flag ? "true" : "false"; break;
    case T::NULL_VAL: out += "null"; break;
    case T::DATETIME: {
      std::string s;
      fmtDateTimeVal(v->timestamp, v->datetime_ms, s, v->tz_offset_min);
      out += javapropEscapeVal(s);
      break;
    }
    case T::BINARY:
    case T::EXTENSION:
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      break;
    default: {
      // Objects, arrays and the remaining special types (TIMESTAMP, OBJECTID,
      // REGEX, CUSTOM_TAG) have no .properties syntax: emit their compact JSON
      // text as an escaped bare value.
      std::string s;
      v->serialize(s, false);
      out += javapropEscapeVal(s);
      break;
    }
  }
}

inline void asvJSONValue::toJavaProp(std::string& out) const {
  if (type != OBJECT || !obj)
    throw asvJSONError("javaprop: root must be an object");
  // A document that reached us from another format can carry root-level leading
  // comments (our own parser attaches a comment block before the first key to
  // that key's `leading`, so this is a no-op for a parse -> emit cycle). Emitted
  // for parity with dotenv and INI, which both write the root's leading and
  // footing; only footing used to be written here, silently dropping them.
  if (comments && !comments->leading.empty())
    javapropWriteCommentLines(out, comments->leading);
  for (const auto& [key, val] : *obj) {
    if (key.empty())
      throw asvJSONError("javaprop: empty key");
    if (val->comments && !val->comments->leading.empty())
      javapropWriteCommentLines(out, val->comments->leading);
    out += javapropEscapeKey(key);
    out += '=';
    javapropFormatVal(val.get(), out);
    if (val->comments && !val->comments->trailing.empty()) {
      out += '\n';
      javapropWriteCommentLines(out, val->comments->trailing);
    } else {
      out += '\n';
    }
  }
  if (comments && !comments->footing.empty())
    javapropWriteCommentLines(out, comments->footing);
}

inline std::string asvJSON::toJavaProp() const {
  std::string out;
  if (root) root->toJavaProp(out);
  return out;
}

// fromAny() heuristic: an '!' comment line, a '\u' escape, or a line with an
// '=' or unspaced ':' before any whitespace. Whitespace-separated pairs, an '='
// that only appears after whitespace, and ':' followed by a space are not
// distinctive enough (YAML/TOON/... style) and are left for the later
// YAML/GOON/TOON cascade. '#' comment lines are SKIPPED entirely -- unlike '!',
// a '#' is not a JavaProp signal (too many formats use it), and its text must
// not misread `=`/`:`/`\u` inside the comment as data.
inline bool javapropLooksLike(std::string_view s) {
  size_t p = 0;
  int sig = 0;
  int lines = 0;
  while (p < s.size() && lines < 16) {
    size_t nl = s.find('\n', p);
    if (nl == std::string_view::npos) nl = s.size();
    std::string_view line = s.substr(p, nl - p);
    size_t a = 0, b = line.size();
    while (a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) a++;
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) b--;
    line = line.substr(a, b - a);
    if (!line.empty()) {
      if (line[0] == '!') { sig |= 4; break; }
      if (line[0] == '#') {
        // Comment lines are skipped entirely -- no signal, and their text must
        // not be scanned for '='/':'/'\u' -- and do NOT count against the
        // 16-line budget. Advance p BEFORE continuing: a bare `continue` jumps
        // to the loop condition and skips the `p = nl + 1` at the bottom, so the
        // same comment line was rescanned 16 times and a document whose first
        // non-empty line is a '#' comment never reached its data lines (false
        // negative for the extremely common `# comment\nkey=value`).
        if (nl == s.size()) break;
        p = nl + 1;
        continue;
      }
      lines++;
      if (line.find("\\u") != std::string_view::npos) { sig |= 8; break; }
      for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '\\') { if (i + 1 < line.size()) i++; continue; }
        if (c == '=') { sig |= 1; break; }
        if (c == ':' && i + 1 < line.size() && line[i + 1] != ' ' && line[i + 1] != '\t') {
          sig |= 2;
          break;
        }
        if (c == ' ' || c == '\t') break; // whitespace-separated: not distinctive
      }
      if (sig) break;
    }
    if (nl == s.size()) break;
    p = nl + 1;
  }
  return sig != 0;
}

inline bool asvJSON::fromJavaProp(std::string_view input) {
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    if (input.size() >= 3 && (unsigned char)input[0] == 0xEF &&
        (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
      input = input.substr(3);

    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("out of memory");

    std::vector<std::string> logical = javapropLogicalLines(input);
    std::vector<Comment> pending;

    for (const std::string& line : logical) {
      size_t p = 0;
      while (p < line.size() && (line[p] == ' ' || line[p] == '\t' || line[p] == '\f')) p++;
      if (p >= line.size()) continue;
      char c0 = line[p];
      if (c0 == '#' || c0 == '!') {
        Comment c;
        std::string_view ct(line.data() + p + 1, line.size() - p - 1);
        if (!ct.empty() && ct[0] == ' ') ct.remove_prefix(1);
        c.text.assign(ct.data(), ct.size());
        pending.push_back(std::move(c));
        continue;
      }

      // Parse the key up to the first unescaped '=' / ':' / whitespace.
      std::string key;
      size_t q = p;
      int sep = 0; // 0 none, 1 '=', 2 ':', 3 whitespace
      std::string rawVal;
      while (q < line.size()) {
        char c = line[q];
        if (c == '\\' && q + 1 < line.size()) {
          // Collect a run of consecutive escape sequences into one contiguous
          // span so a single javapropUnescape() call decodes the whole backslash
          // batch -- the decoder has to see complete sequences to size its
          // `\uXXXX` (and surrogate-pair) reads, which span several chars each.
          const char* segStart = line.data() + q;
          size_t segLen = 0;
          while (q < line.size()) {
            if (line[q] != '\\') break;
            if (q + 1 >= line.size()) break;
            if (line[q + 1] == 'u' && q + 5 < line.size()) {
              segLen += 6;
              q += 6;
            } else {
              segLen += 2;
              q += 2;
            }
          }
          key += javapropUnescape(std::string_view(segStart, segLen));
          continue;
        }
        if (c == '=' || c == ':') { sep = (c == '=') ? 1 : 2; break; }
        if (c == ' ' || c == '\t' || c == '\f') {
          size_t q2 = q;
          while (q2 < line.size() && (line[q2] == ' ' || line[q2] == '\t' || line[q2] == '\f')) q2++;
          if (q2 < line.size() && (line[q2] == '=' || line[q2] == ':')) {
            sep = (line[q2] == '=') ? 1 : 2;
            q = q2;
            break;
          }
          sep = 3;
          rawVal.assign(line.data() + q2, line.size() - q2);
          q = q2;
          break;
        }
        key += c;
        q++;
      }
      // Empty keys are rejected on load too, doubling the encoder's check: a
      // `=value` row would otherwise create key "" (which a .properties file
      // formally allows), and that document could never be re-emitted without
      // throwing. Mirrors dotenv.
      if (key.empty())
        throw asvJSONError("javaprop: empty key");
      if (!asvJSONValue::checkStringLen(key.size()))
        throw asvJSONError("javaprop: key too long");

      if (sep == 0) {
        // No separator at all: the whole trimmed line is the key, value empty.
        rawVal.clear();
      } else if (sep == 1 || sep == 2) {
        rawVal.assign(line.data() + q + 1, line.size() - q - 1);
        size_t k = 0;
        while (k < rawVal.size() && (rawVal[k] == ' ' || rawVal[k] == '\t' || rawVal[k] == '\f')) k++;
        rawVal.erase(0, k);
      }

      // Strip unescaped trailing whitespace of the raw value (JDK behavior).
      while (!rawVal.empty()) {
        char c = rawVal.back();
        if (c != ' ' && c != '\t' && c != '\f') break;
        size_t e = rawVal.size() - 1;
        size_t bs = 0;
        while (bs < e && rawVal[e - 1 - bs] == '\\') bs++;
        if (bs % 2 == 1) break; // escaped whitespace is data
        rawVal.pop_back();
      }

      std::string value = javapropUnescape(rawVal);
      auto val = javapropParseVal(value);
      if (!val)
        throw asvJSONError("javaprop: value too long");

      if (!pending.empty()) {
        val->comments.reset(new Comments());
        val->comments->leading.swap(pending); // swap leaves `pending` empty
      }

      auto it = obj->obj->find(key);
      if (it == obj->obj->end()) obj->obj->emplace(key, std::move(val));
      else it->second = std::move(val);
    }

    if (!pending.empty()) {
      obj->comments.reset(new Comments());
      obj->comments->footing.swap(pending);
    }

    root = std::move(obj);
    formatType = FormatType::JavaProp;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromMessagePack).
    // Not theoretical here: javapropLogicalLines copies and grows the line
    // list, `key += javapropUnescape(...)`, rawVal.assign/erase, Comments is
    // new'd and obj->obj->emplace grows the map -- only factories new(nothrow).
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal