#pragma once
// ADE (Advanced Data Exchange) format support for asvJSON++
// See ade.md for the full specification.
//
// Features:
//   - Optional "# ADE vX.Y" header
//   - Multiple documents separated by "// --- End of Document ---" lines
//   - Comments: #, //, /* ... */ (outside strings)
//   - Keys: identifiers, quoted strings, dotted keys (a.b.c => nested objects)
//   - Values: quoted/unquoted strings, decimal/hex/octal/binary numbers,
//     booleans (case-insensitive), null, inline or multi-line arrays/objects,
//     block scalars (| and > with indent + chomp indicators)
//   - Strings: double-quoted with JSON escapes + \xHH/\uXXXX/\UXXXXXXXX/\u{...}
//     and line continuation; single-quoted (literal, '' escape); multiline
//   - Anchors (&name) and aliases (*name); aliases deep-copy the anchored
//     value and forward/self references are rejected (undefined alias)
//   - Tags: !base64, !bin, !datetime, !ext, !regex, !objectid, !timestamp;
//     unknown tags preserve value
//   - Strict mode rejects duplicate keys, unquoted keys, and special
//     characters in bare tokens

#include "../core.hpp"

#include <cstdlib>
#include <cstring>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace asvJSONInternal {

// ====================== Shared helpers ======================

inline std::string_view adeTrim(std::string_view s) {
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r' || s[0] == '\n')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
  return s;
}

inline std::string adeToLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  return out;
}

// Null-node guard: every make* factory is a nothrow `new` that returns nullptr
// on OOM. The ADE decoder builds the tree directly, so a null unique_ptr must
// become a reported error instead of entering a container (a serializer would
// dereference it). Mirrors makeAdeString's explicit check.
inline std::unique_ptr<asvJSONValue> adeNonNull(std::unique_ptr<asvJSONValue> v) {
  if (!v) throw asvJSONError("ADE: out of memory");
  return v;
}

inline bool adeIsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool adeIsIdentCont(char c) {
  return adeIsIdentStart(c) || (c >= '0' && c <= '9') || c == '-';
}

// Marks the lossless !regex body form: "__REGEX__<pattern>|<opts>|". The
// marker is what keeps the canonical encoding distinguishable from the
// historical "decoded text, split at the last '|'" form that the decoder
// still accepts, so an old `!regex "a|b|c"` is not read as canonical and
// silently truncated. Declared before ADEParser and the encoder, which are
// separate classes that both need it.
constexpr std::string_view kAdeRegexMarker = "__REGEX__";

// Tag names may include a namespace separator ':' (e.g. !ns:tag) or a dotted
// prefix, so that registered tag namespaces are directly representable.
// Appendix A: TAG ::= '!' ( IDENTIFIER (':' IDENTIFIER | '.' IDENTIFIER)* | ...)
// so the FIRST character must be an identifier start; a digit or '-' may not
// begin the name. Each '.'/':' separated component is checked separately so
// that "!1a:b" is rejected too, not just "!1".
inline bool adeIsTagChar(char c) {
  return adeIsIdentCont(c) || c == ':' || c == '.';
}

// True if `name` is one of the built-in tags that applyTag dispatches on.
// These must never be emitted in the simple !name form when the value was a
// CUSTOM_TAG, because the parser dispatches on the name alone and would
// re-interpret it as that built-in instead of preserving the custom tag. Must
// list exactly the names applyTag handles. Encoder-side only.
inline bool adeIsBuiltinTagName(std::string_view name) {
  return name == "int" || name == "float" || name == "string" || name == "bool" ||
         name == "null" || name == "schema" || name == "base64" || name == "bin" ||
         name == "datetime" || name == "regex" || name == "objectid" ||
         name == "timestamp" || name == "ext" || name == "extension";
}

// True when `name` matches TAG's non-verbatim form: identifier components
// joined by ':' or '.'. Used both to validate a parsed tag and to decide
// whether the encoder may emit !name or must fall back to !<name>.
inline bool adeIsSimpleTagName(std::string_view name) {
  if (name.empty()) return false;
  size_t i = 0;
  while (true) {
    if (i >= name.size() || !adeIsIdentStart(name[i])) return false;
    i++;
    while (i < name.size() && adeIsIdentCont(name[i])) i++;
    if (i == name.size()) return true;
    if (name[i] != ':' && name[i] != '.') return false;
    i++;  // consume the separator; a component MUST follow it
  }
}

// ====================== Encoding / BOM ======================

inline void adeAppendUtf8(std::string& out, uint32_t cp) {
  // A surrogate code point is never a valid Unicode scalar value, and encoding
  // one directly would emit an ill-formed UTF-8 sequence (ED A0 80..ED BF BF
  // for U+D800..U+DFFF). Substitute U+FFFD so the result is always well-formed.
  if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
  if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
  else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Decode UTF-16 (with BOM already stripped) to UTF-8, combining surrogate pairs.
// An unpaired surrogate is not a valid Unicode scalar value; adeAppendUtf8
// substitutes U+FFFD for it, so a malformed input still yields well-formed
// UTF-8 (replacing the ill-formed ED A0 80..ED BF BF that a direct encoding
// would produce).
inline void adeDecodeUtf16(std::string& out, const uint8_t* p, size_t n, bool littleEndian) {
  size_t i = 0;
  while (i + 1 < n) {
    uint16_t u = littleEndian ? static_cast<uint16_t>(p[i] | (static_cast<uint16_t>(p[i + 1]) << 8))
                              : static_cast<uint16_t>((static_cast<uint16_t>(p[i]) << 8) | p[i + 1]);
    i += 2;
    uint32_t cp = u;
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n) {
      uint16_t u2 = littleEndian ? static_cast<uint16_t>(p[i] | (static_cast<uint16_t>(p[i + 1]) << 8))
                                 : static_cast<uint16_t>((static_cast<uint16_t>(p[i]) << 8) | p[i + 1]);
      if (u2 >= 0xDC00 && u2 <= 0xDFFF) {
        cp = 0x10000 + ((static_cast<uint32_t>(u - 0xD800)) << 10) + (u2 - 0xDC00);
        i += 2;
      }
    }
    adeAppendUtf8(out, cp);
  }
}

// Decode UTF-32 (with BOM already stripped) to UTF-8.
inline void adeDecodeUtf32(std::string& out, const uint8_t* p, size_t n, bool littleEndian) {
  for (size_t i = 0; i + 3 < n; i += 4) {
    uint32_t cp = littleEndian ? (static_cast<uint32_t>(p[i]) | (static_cast<uint32_t>(p[i + 1]) << 8) |
                                  (static_cast<uint32_t>(p[i + 2]) << 16) | (static_cast<uint32_t>(p[i + 3]) << 24))
                               : ((static_cast<uint32_t>(p[i]) << 24) | (static_cast<uint32_t>(p[i + 1]) << 16) |
                                  (static_cast<uint32_t>(p[i + 2]) << 8) | static_cast<uint32_t>(p[i + 3]));
    if (cp > 0x10FFFF) cp = 0xFFFD;
    // U+D800..U+DFFF is inside the 0x10FFFF range but is still not a valid
    // scalar value; adeAppendUtf8 maps it to U+FFFD.
    adeAppendUtf8(out, cp);
  }
}

// Normalize the byte order mark: a UTF-8 BOM is stripped, UTF-16/UTF-32 input
// (detected by its BOM) is transcoded to UTF-8 so that Windows-produced ADE
// files parse correctly. The BOM itself is a byte-order signature, not part of
// the document text, and is consumed without altering the remaining content.
// Returns true when a BOM was present.
inline bool adeNormalizeEncoding(const std::string& in, std::string& out) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(in.data());
  size_t n = in.size();
  if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
    out.assign(in.data() + 3, n - 3);
    return true;
  }
  if (n >= 4 && p[0] == 0xFF && p[1] == 0xFE && p[2] == 0x00 && p[3] == 0x00) {
    adeDecodeUtf32(out, p + 4, n - 4, true);
    return true;
  }
  if (n >= 4 && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0xFE && p[3] == 0xFF) {
    adeDecodeUtf32(out, p + 4, n - 4, false);
    return true;
  }
  if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
    adeDecodeUtf16(out, p + 2, n - 2, true);
    return true;
  }
  if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
    adeDecodeUtf16(out, p + 2, n - 2, false);
    return true;
  }
  out = in;
  return false;
}

inline bool adeIsUnquotedChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '.' || c == '_' || c == '~' || c == '+' || c == '/' || c == '=' || c == '-';
}

inline int adeHexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Recognized document separator lines (exact match only, so that a comment
// merely containing the phrase "End of Document" does not split the stream).
// "---" is the YAML-style document start marker; the legacy comment-style
// separators remain accepted for backward compatibility.
inline bool adeIsDocSeparator(std::string_view line) {
  return line == "---" ||
         line == "// --- End of Document ---" ||
         line == "// ADE document separator";
}

// A "..." line is a YAML-style document end marker and is skipped entirely.
inline bool adeIsDocEndMarker(std::string_view line) {
  return line == "...";
}

// Split a ADE stream into documents on document separator lines.
inline std::vector<std::string> splitAdeDocuments(const std::string& text) {
  auto lines = splitLines(text);
  std::vector<std::string> docs;
  std::string cur;
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    std::string_view t = adeTrim(line);
    // Document separators/end-markers must start at column 0.
    // An indented line like "  ---" inside a block scalar is not a separator.
    bool col0 = !line.empty() && line[0] != ' ' && line[0] != '\t';
    if (col0 && adeIsDocSeparator(t)) {
      if (!cur.empty()) docs.push_back(cur);
      cur.clear();
      continue;
    }
    if (col0 && adeIsDocEndMarker(t)) {
      // End of the current document: close it, a following "---" opens the next.
      if (!cur.empty()) docs.push_back(cur);
      cur.clear();
      continue;
    }
    // splitLines() does NOT emit a phantom trailing empty line at EOF, so the
    // last line here is always real content (a genuine trailing blank line is
    // re-appended with its '\n', preserving block-scalar clip behavior).
    cur += line;
    cur += '\n';
  }
  if (!cur.empty()) docs.push_back(cur);
  if (docs.empty()) docs.push_back("");
  return docs;
}

// Try to interpret a bare token as a number (decimal / hex / octal / binary).
// Returns nullptr when the token is not a valid ADE number literal.
// Supports digit separators ('_' between digits) and, on int64 overflow,
// stores the exact literal as a STRING with raw_number set (so it round-trips).
inline std::unique_ptr<asvJSONValue> adeTryParseNumber(const std::string& tok) {
  if (tok.empty()) return nullptr;
  // Validate digit separators: '_' must sit between two digits, where "digit"
  // means a digit of the token's own base (Appendix A: INT ::= DIGITS |
  // DIGITS '_' DIGITS, and the same rule applies inside HEX/OCTAL/BINARY - so
  // 0xFF_FF is fine, 0b1_2 and 1__000 are not). The bare-hexdigit test is not
  // enough: it would accept "1_A" (an A is a hex digit but not a decimal one),
  // so the base is determined from the sign and prefix first.
  int seppBase = 10;
  {
    size_t bi = (!tok.empty() && tok[0] == '-') ? 1 : 0;
    if (bi + 1 < tok.size() && tok[bi] == '0') {
      char n = tok[bi + 1];
      if (n == 'x' || n == 'X') seppBase = 16;
      else if (n == 'o' || n == 'O') seppBase = 8;
      else if (n == 'b' || n == 'B') seppBase = 2;
    }
  }
  for (size_t i = 0; i < tok.size(); i++) {
    if (tok[i] != '_') continue;
    if (i == 0 || i + 1 >= tok.size()) return nullptr;
    int a = adeHexDigit(tok[i - 1]);
    int b = adeHexDigit(tok[i + 1]);
    if (a < 0 || a >= seppBase || b < 0 || b >= seppBase) return nullptr;
  }
  std::string t;
  t.reserve(tok.size());
  for (char c : tok) if (c != '_') t += c;

  size_t i = 0;
  bool neg = false;
  // Per the ADE grammar SIGNED_INT ::= '-'? INT, a leading '+' is not a
  // valid numeric sign; such tokens are treated as unquoted strings.
  if (t[0] == '-') { neg = true; i = 1; }

  // hex / octal / binary (the optional '-' belongs to the token itself)
  if (i + 1 < t.size() && t[i] == '0') {
    int base = 0;
    char n = t[i + 1];
    if (n == 'x' || n == 'X') base = 16;
    else if (n == 'o' || n == 'O') base = 8;
    else if (n == 'b' || n == 'B') base = 2;
    if (base != 0) {
      size_t j = i + 2;
      if (j >= t.size()) return nullptr;
      unsigned long long v = 0;
      bool overflow = false;
      for (; j < t.size(); j++) {
        int d = adeHexDigit(t[j]);
        if (d < 0 || d >= base) return nullptr;
        if (v > (std::numeric_limits<unsigned long long>::max() - static_cast<unsigned long long>(d)) / static_cast<unsigned long long>(base)) { overflow = true; break; }
        v = v * static_cast<unsigned long long>(base) + static_cast<unsigned long long>(d);
      }
      // '-' can represent exactly one extra magnitude (e.g. -0x8000000000000000 == INT64_MIN)
      unsigned long long maxV = neg ? (static_cast<unsigned long long>(INT64_MAX) + 1ULL)
                                    : static_cast<unsigned long long>(INT64_MAX);
      if (overflow || v > maxV) {
        auto s = adeNonNull(asvJSONValue::makeStringView(tok));
        s->raw_number = true;
        return s;
      }
      if (neg && v == static_cast<unsigned long long>(INT64_MAX) + 1ULL)
        return adeNonNull(asvJSONValue::makeInt(std::numeric_limits<int64_t>::min()));
      int64_t iv = neg ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
      return adeNonNull(asvJSONValue::makeInt(iv));
    }
  }

  // decimal int / float
  size_t j = i;
  while (j < t.size() && t[j] >= '0' && t[j] <= '9') j++;
  if (j == i) return nullptr;                       // no digits
  if (t[i] == '0' && j > i + 1) return nullptr;     // leading zeros are invalid

  bool isFloat = false;
  if (j < t.size() && t[j] == '.') {
    isFloat = true;
    j++;
    while (j < t.size() && t[j] >= '0' && t[j] <= '9') j++;
  }
  if (j < t.size() && (t[j] == 'e' || t[j] == 'E')) {
    isFloat = true;
    j++;
    if (j < t.size() && (t[j] == '+' || t[j] == '-')) j++;
    size_t expStart = j;
    while (j < t.size() && t[j] >= '0' && t[j] <= '9') j++;
    if (j == expStart) return nullptr;              // "1e" is invalid
  }
  if (j != t.size()) return nullptr;                // trailing junk

  if (isFloat) {
    char* end = nullptr;
    errno = 0;
    double d = std::strtod(t.c_str(), &end);
    if (!end || *end != 0 || errno == ERANGE || !std::isfinite(d)) return nullptr;
    // Mirror the JSON parser: a literal with more significant digits than a
    // double can hold exactly is stored as a raw_number STRING so the value
    // round-trips losslessly instead of silently losing precision.
    size_t sigDigits = 0;
    for (size_t q = neg ? 1 : 0; q < t.size(); q++) {
      char c = t[q];
      if (c != '.' && c != 'e' && c != 'E' && c != '+' && c != '-') sigDigits++;
    }
    if (sigDigits > static_cast<size_t>(asvJSONValue::MAX_DOUBLE_SIG_DIGITS)) {
      auto s = adeNonNull(asvJSONValue::makeStringView(tok));
      s->raw_number = true;
      return s;
    }
    return adeNonNull(asvJSONValue::makeDouble(d));
  }
  errno = 0;
  char* end = nullptr;
  long long ll = std::strtoll(t.c_str(), &end, 10);
  if (!end || *end != 0) return nullptr;
  if (errno == ERANGE) {
    // Big integer: keep the exact literal so round-trip is lossless.
    auto s = adeNonNull(asvJSONValue::makeStringView(tok));
    s->raw_number = true;
    return s;
  }
  return adeNonNull(asvJSONValue::makeInt(static_cast<int64_t>(ll)));
}

// Parse a ADE datetime value. Accepts the full ISO-8601 form handled by
// tryParseDateTime (with optional fractional seconds and timezone offset) as
// well as date-only "YYYY-MM-DD" (midnight UTC).
inline bool adeTryParseDateTime(std::string_view sv, time_t& out, int* ms_out, int* tz_out = nullptr) {
  if (ms_out) *ms_out = 0;
  if (tz_out) *tz_out = 0;
  // Date-only form: YYYY-MM-DD exactly.
  if (sv.size() == 10 && sv[4] == '-' && sv[7] == '-') {
    auto parse2 = [](const char* p, int& v) -> bool {
      if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
      v = (p[0] - '0') * 10 + (p[1] - '0');
      return true;
    };
    int year, month, day;
    if (!parse2(sv.data(), year)) return false;
    year = year * 100 + (sv[2] - '0') * 10 + (sv[3] - '0');
    if (!parse2(sv.data() + 5, month) || month < 1 || month > 12) return false;
    if (!parse2(sv.data() + 8, day) || day < 1 || day > 31) return false;
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = 0;
    out = asvjson_timegm(&tm);
    return true;
  }
  return tryParseDateTime(sv, out, ms_out, tz_out);
}

// ====================== Parser ======================

class ADEParser {
public:
  explicit ADEParser(std::string_view text, bool strict) : text_(text), strict_(strict) {}

  std::unique_ptr<asvJSONValue> parseDocument();
  std::unique_ptr<asvJSONValue> parseScalarDocValue() {
    skipWsAndComments();
    auto v = parseValue(1);
    skipWsAndComments();
    // A scalar document must consume the whole input: "hello" extra is a parse
    // error, not a silently dropped suffix (matching how a mapping document
    // rejects trailing content after its last value).
    if (!eof()) throw asvJSONError("ADE: trailing content after scalar at line " + std::to_string(lineNum_));
    return v;
  }

private:
  // Documented resource limits (Section 12 of the ADE spec): the maximum
  // number of anchors that may be registered while parsing a document.
  static constexpr size_t kMaxAnchors = 1000000;

  // A parsed key plus whether it was quoted. Quoted keys are always literal;
  // unquoted dotted keys are expanded into nested objects. For dot-separated
  // keys with more than one segment, `segs` holds each segment together with
  // its quoted flag, so that literal segments can contain dots ("a.b".c).
  struct ParsedKey {
    std::string name;
    bool quoted = false;
    std::vector<std::pair<std::string, bool>> segs;
  };

  std::string_view text_;
  bool strict_ = false;
  size_t pos_ = 0;
  int lineNum_ = 1;
  // Anchors store a deep copy of the anchored value so that aliases never
  // reference values that may be moved or destroyed by later key insertions.
  std::unordered_map<std::string, std::unique_ptr<asvJSONValue>> anchors_;
  // Raw (still-escaped) token body of the value of the !regex tag currently
  // being parsed, so applyTag can split the pattern from the options before
  // adeUnescape collapses an escaped pipe into a bare one. Empty for any other
  // tag, which then falls back to splitting the decoded text.
  std::string regexRaw_;
  // Whether regexRaw_ came from a single-quoted body, so applyTag decodes the
  // split fields with the matching escape rules.
  bool regexRawSingle_ = false;

  // "pat|opts|" -- the first raw '|' separates the fields and the last one is a
  // mandatory terminator, so a field may itself end in a pipe and an empty
  // options field stays distinguishable. Only raw text is inspected, so a
  // "\u007C" inside a field is never read as a separator. `singleQuoted` picks
  // the field unescaper: a single-quoted body knows only the '' doubling, so
  // decoding it with the double-quoted rules would leave '' in the pattern.
  static void adeSplitCanonicalRegex(std::string_view raw, bool singleQuoted, std::string& pattern, std::string& opts) {
    auto dec = [singleQuoted](std::string_view field) {
      return singleQuoted ? adeUnescapeSingle(field) : adeUnescape(field);
    };
    // Canonical form: __REGEX__<pattern>|<opts>| - the body has exactly two
    // pipes (separator + trailing terminator). Three or more means a bare '|'
    // leaked into pattern or opts, which the old rfind/find logic would silently
    // truncate; reject so no data is lost (the encoder escapes all interior '|'
    // as \u007C). A single pipe with no terminator is treated as a no-options
    // pattern (backward-compat with documents that omitted the trailing '|').
    size_t first = raw.find('|');
    if (first == std::string_view::npos) { pattern = dec(raw); return; }
    size_t second = raw.find('|', first + 1);
    if (second == std::string_view::npos) { pattern = dec(raw.substr(0, first)); opts = dec(std::string_view()); return; }
    size_t third = raw.find('|', second + 1);
    if (third != std::string_view::npos) {
      // More than two pipes: fall back to legacy split (last pipe = separator).
      // This preserves data for foreign input that the encoder would never produce.
      adeSplitLegacyRegex(std::string(raw), pattern, opts);
      return;
    }
    pattern = dec(raw.substr(0, first));
    opts = dec(raw.substr(first + 1, second - first - 1));
  }

  // Historical body form: the already-decoded text split at the LAST '|', so a
  // pattern that itself contains a pipe keeps its old meaning.
  static void adeSplitLegacyRegex(const std::string& s, std::string& pattern, std::string& opts) {
    size_t sep = s.rfind('|');
    pattern = (sep == std::string::npos) ? s : s.substr(0, sep);
    opts = (sep == std::string::npos) ? std::string() : s.substr(sep + 1);
  }

  bool eof() const { return pos_ >= text_.size(); }
  char peek() const { return eof() ? '\0' : text_[pos_]; }

  void skipWs() {
    while (!eof()) {
      char c = text_[pos_];
      if (c == ' ' || c == '\t') pos_++;
      else if (c == '\n') { pos_++; lineNum_++; }
      else if (c == '\r') pos_++;
      else break;
    }
  }

  // Skip a single comment if one is present; leaves position otherwise.
  void skipOneComment() {
    if (eof()) return;
    char c = text_[pos_];
    if (c == '#') {
      while (!eof() && text_[pos_] != '\n') pos_++;
      return;
    }
    if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
      // Only treat as comment at line start or after whitespace so that
      // unquoted strings containing '/' (e.g. URLs) are not broken.
      if (pos_ == 0 || text_[pos_ - 1] == ' ' || text_[pos_ - 1] == '\t' || text_[pos_ - 1] == '\n') {
        while (!eof() && text_[pos_] != '\n') pos_++;
      }
      return;
    }
    if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
      pos_ += 2;
      while (pos_ + 1 < text_.size()) {
        if (text_[pos_] == '\n') lineNum_++;
        if (text_[pos_] == '*' && text_[pos_ + 1] == '/') { pos_ += 2; return; }
        pos_++;
      }
      throw asvJSONError("ADE: unterminated block comment at line " + std::to_string(lineNum_));
    }
  }

  void skipWsAndComments() {
    while (true) {
      skipWs();
      size_t saved = pos_;
      skipOneComment();
      if (pos_ == saved) break;
    }
  }

  // True when the rest of the current line is whitespace/comments only.
  bool atLineEndSkipping() {
    size_t i = pos_;
    while (true) {
      while (i < text_.size() && (text_[i] == ' ' || text_[i] == '\t')) i++;
      if (i >= text_.size() || text_[i] == '\n' || text_[i] == '\r') return true;
      if (text_[i] == '#') { while (i < text_.size() && text_[i] != '\n') i++; continue; }
      if (text_[i] == '/' && i + 1 < text_.size() && text_[i + 1] == '/') {
        if (i == 0 || text_[i - 1] == ' ' || text_[i - 1] == '\t' || text_[i - 1] == '\n') {
          while (i < text_.size() && text_[i] != '\n') i++;
          continue;
        }
        return false;
      }
      if (text_[i] == '/' && i + 1 < text_.size() && text_[i + 1] == '*') {
        // An unclosed block comment is a syntax error, not the end of the
        // line: skipOneComment() throws for the same input, and answering
        // "yes, the line ends here" would report a missing tag value (or a
        // plain null) for a document whose real fault is the comment.
        size_t startLine = lineNum_;
        size_t j = i + 2;
        bool closed = false;
        while (j + 1 < text_.size()) {
          if (text_[j] == '*' && text_[j + 1] == '/') { closed = true; break; }
          j++;
        }
        if (!closed)
          throw asvJSONError("ADE: unterminated block comment at line " + std::to_string(startLine));
        i = j + 2;
        continue;
      }
      return false;
    }
  }

  // True when a tag is immediately followed by something that cannot start a
  // value: the end of the line, the end of the input, or a flow delimiter.
  // ADE has no block-sequence production (Appendix A: DOC_LINE is only
  // WS? (LINE | BLOCK_COMMENT | COMMENT)), so a value-less tag is only legal
  // at end of line or right before a ',' / '}' / ']' in a flow collection,
  // which is what makes "[!null, 1]" and "{y: !null, z: 1}" work.
  bool atTagValueEnd() {
    if (atLineEndSkipping()) return true;
    return !eof() && (peek() == ',' || peek() == '}' || peek() == ']');
  }

  void expect(char c, const char* what) {
    if (eof() || peek() != c) throw asvJSONError(std::string("ADE: expected '") + what + "' at line " + std::to_string(lineNum_));
    pos_++;
  }

  ParsedKey parseKey() {
    skipWs();
    if (eof()) throw asvJSONError("ADE: expected key at line " + std::to_string(lineNum_));
    ParsedKey pk;
    // YAML-style merge key (Section: Merge Keys). '<' is not an unquoted
    // character, so the bare "<<" form is recognized explicitly here.
    if (peek() == '<' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '<') {
      pos_ += 2;
      pk.name = "<<";
      return pk;
    }
    // A key may be dotted: each dot-separated segment is either a quoted
    // string (literal) or a plain identifier. "a.b.c" nests; "a.b".c nests
    // with a literal first segment "a.b"; a fully quoted "a.b.c" is a single
    // literal key (Section 3 of the ADE spec).
    std::vector<std::pair<std::string, bool>> segs;
    while (true) {
      std::pair<std::string, bool> seg;
      if (peek() == '"') {
        auto v = parseQuotedString();
        seg = { std::move(v->str_data), true };
      } else if (peek() == '\'') {
        auto v = parseSingleQuotedString();
        seg = { std::move(v->str_data), true };
      } else {
        size_t start = pos_;
        while (!eof() && adeIsUnquotedChar(text_[pos_]) && text_[pos_] != '.') pos_++;
        if (pos_ == start) throw asvJSONError("ADE: invalid key at line " + std::to_string(lineNum_));
        seg = { std::string(text_.data() + start, pos_ - start), false };
      }
      segs.push_back(std::move(seg));
      if (!eof() && peek() == '.') { pos_++; continue; }
      break;
    }
    if (strict_) {
      // The spec (Section 3) requires all keys to be quoted in strict mode.
      for (const auto& s : segs) {
        if (!s.second) throw asvJSONError("ADE: strict mode requires all keys to be quoted (key: " + s.first + ")");
      }
    }
    for (const auto& s : segs) {
      if (!pk.name.empty()) pk.name += '.';
      pk.name += s.first;
    }
    if (segs.size() == 1) {
      pk.quoted = segs[0].second;
    } else {
      pk.segs = std::move(segs);
    }
    return pk;
  }

  static inline bool adeKeyIsIdentLike(const std::string& key) {
    if (key.empty()) return false;
    size_t segStart = 0;
    int segs = 0;
    for (size_t i = 0; i <= key.size(); i++) {
      if (i == key.size() || key[i] == '.') {
        std::string_view seg(key.data() + segStart, i - segStart);
        if (seg.empty() || !adeIsIdentStart(seg[0])) return false;
        for (char c : seg) if (!adeIsIdentCont(c)) return false;
        segs++;
        segStart = i + 1;
      }
    }
    return segs >= 1;
  }

  std::unique_ptr<asvJSONValue> parseValue(int depth) {
    skipWsAndComments();
    if (eof()) throw asvJSONError("ADE: expected value at line " + std::to_string(lineNum_));
    if (!asvJSONValue::checkNestingDepth(depth))
      throw asvJSONError("ADE: nesting too deep at line " + std::to_string(lineNum_));
    char c = peek();
    if (c == '"') return parseQuotedString();
    if (c == '\'') return parseSingleQuotedString();
    if (c == '[') return parseArray(depth + 1);
    if (c == '{') return parseObject(depth + 1);
    if (c == '!') return parseTagged(depth + 1);
    if (c == '&') return parseAnchored(depth + 1);
    if (c == '*') return parseAlias();
    if (c == '|' || c == '>') return parseBlockScalar();
    return parseScalar();
  }

  // Unescape a double-quoted ADE string. Supports JSON escapes (\n, \t, \r,
  // \b, \f, \\, \", \/), byte escapes \xHH, Unicode escapes \uXXXX (including
  // surrogate pairs), \UXXXXXXXX and \u{...}, plus INI-style line continuation
  // (backslash immediately before a line break joins the next line, stripping
  // leading whitespace). Literal \r\n line breaks inside multiline strings are
  // normalized to a single \n.
  static std::string adeUnescape(const std::string_view& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
      char c = raw[i];
      if (c == '\\') {
        if (i + 1 >= raw.size()) throw asvJSONError("ADE: invalid escape at end of string");
        char e = raw[++i];
        // Line continuation: '\' immediately before a line break joins the next
        // line and strips its leading whitespace (INI-style).
        if (e == '\n' || e == '\r') {
          if (e == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') i++;
          i++;
          while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) i++;
          i--;
          continue;
        }
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case '/': out.push_back('/'); break;
          case '\\': out.push_back('\\'); break;
          case '"': out.push_back('"'); break;
          case 'x': {
            // Byte escape \xHH -- exactly two hex digits (Section 3.1).
            if (i + 2 >= raw.size()) throw asvJSONError("ADE: invalid \\x escape in string");
            int hi = adeHexDigit(raw[i + 1]);
            int lo = adeHexDigit(raw[i + 2]);
            if (hi < 0 || lo < 0) throw asvJSONError("ADE: invalid \\x escape in string");
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
            break;
          }
          case 'u':
          case 'U': {
            // Unicode escape: \uXXXX, \UXXXXXXXX or \u{...}.
            if (e == 'u' && i + 1 < raw.size() && raw[i + 1] == '{') {
              // \u{...} variable-length form.
              size_t j = i + 2;
              unsigned int cp = 0;
              bool any = false;
              while (j < raw.size() && raw[j] != '}') {
                int d = adeHexDigit(raw[j]);
                if (d < 0) throw asvJSONError("ADE: invalid \\u{...} escape in string");
                cp = cp * 16u + static_cast<unsigned int>(d);
                if (cp > 0x10FFFFu) throw asvJSONError("ADE: code point out of range in string");
                any = true;
                j++;
              }
              if (!any || j >= raw.size()) throw asvJSONError("ADE: unterminated \\u{...} escape in string");
              i = j; // consume '}'
              appendUtf8Codepoint(out, cp);
              break;
            }
            size_t need = (e == 'u') ? 4 : 8;
            if (i + need >= raw.size()) throw asvJSONError("ADE: invalid \\u/\\U escape in string");
            unsigned int cp = 0;
            for (size_t k = 1; k <= need; k++) {
              int d = adeHexDigit(raw[i + k]);
              if (d < 0) throw asvJSONError("ADE: invalid \\u/\\U escape in string");
              cp = (cp << 4) | static_cast<unsigned int>(d);
            }
            i += need;
            // Combine surrogate pairs (\uD800\uDC00 form).
            if (cp >= 0xD800u && cp <= 0xDBFFu) {
              if (i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
                unsigned int lo = 0;
                bool ok = true;
                for (size_t k = 1; k <= 4; k++) {
                  int d = adeHexDigit(raw[i + 2 + k]);
                  if (d < 0) { ok = false; break; }
                  lo = (lo << 4) | static_cast<unsigned int>(d);
                }
                if (ok && lo >= 0xDC00u && lo <= 0xDFFFu) {
                  cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                  i += 6;
                }
              }
            }
            if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
              throw asvJSONError("ADE: invalid code point in string");
            appendUtf8Codepoint(out, cp);
            break;
          }
          default: throw asvJSONError(std::string("ADE: invalid escape sequence \\" ) + e + " in string");
        }
      } else if (c == '\r') {
        // Normalize literal \r\n and lone \r to \n (YAML line-break semantics).
        if (i + 1 < raw.size() && raw[i + 1] == '\n') i++;
        out.push_back('\n');
      } else {
        out.push_back(c);
      }
    }
    return out;
  }

  // Single-quoted ADE string: everything is literal, '' is the only escape
  // (YAML-style). No backslash processing. Literal \r\n is normalized to \n.
  static std::string adeUnescapeSingle(const std::string_view& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
      char c = raw[i];
      if (c == '\'' && i + 1 < raw.size() && raw[i + 1] == '\'') { out.push_back('\''); i++; }
      else if (c == '\r') {
        if (i + 1 < raw.size() && raw[i + 1] == '\n') i++;
        out.push_back('\n');
      } else out.push_back(c);
    }
    return out;
  }

  // Wrap an unescaped string in a STRING node, enforcing the core string-length
  // limit (Section 12). The make*String factories return nullptr past
  // MAX_STRING_LEN, and every caller here dereferences its result, so the check
  // must be explicit: an over-long literal would otherwise be a null-pointer
  // dereference rather than a parse error.
  std::unique_ptr<asvJSONValue> makeAdeString(std::string&& s) {
    if (!asvJSONValue::checkStringLen(s.size()))
      throw asvJSONError("ADE: string too long");
    auto v = asvJSONValue::makeStringView(std::move(s));
    if (!v) throw asvJSONError("ADE: string too long");
    return v;
  }

  std::unique_ptr<asvJSONValue> parseQuotedString() {
    pos_++; // consume '"'
    size_t start = pos_;
    bool escaped = false;
    while (!eof()) {
      char c = text_[pos_];
      if (escaped) { escaped = false; pos_++; continue; }
      if (c == '\\') { escaped = true; pos_++; continue; }
      if (c == '"') break;
      if (c == '\n') lineNum_++;
      pos_++;
    }
    if (eof()) throw asvJSONError("ADE: unterminated string at line " + std::to_string(lineNum_));
    std::string_view raw(text_.data() + start, pos_ - start);
    pos_++; // consume '"'
    std::string decoded;
    try {
      decoded = adeUnescape(raw);
    } catch (const std::exception& e) {
      // adeUnescape already names the offending escape ("ADE: invalid \x
      // escape in string", ...). Keep that text and only add the position:
      // collapsing every failure into one generic message would make the
      // detailed list in Section 13 unreachable and leave the user guessing
      // which escape was wrong.
      throw asvJSONError(std::string(e.what()) + " at line " + std::to_string(lineNum_));
    }
    // Outside the try: a length failure must not be reported as a bad escape.
    return makeAdeString(std::move(decoded));
  }

  // Scan a quoted string WITHOUT decoding it, returning the raw token body
  // (between the quotes, escapes intact) and leaving pos_ after the closing
  // quote. Used by !regex, which must find the pattern/options separator before
  // escape processing turns a \u007C inside the pattern into a raw '|'
  // indistinguishable from the separator. `q` is the opening quote: a
  // double-quoted body carries backslash escapes, a single-quoted one only the
  // '' doubling.
  std::string_view scanRawQuotedString(char q) {
    pos_++; // consume the opening quote
    size_t start = pos_;
    bool escaped = false;
    while (!eof()) {
      char c = text_[pos_];
      if (q == '"') {
        if (escaped) { escaped = false; pos_++; continue; }
        if (c == '\\') { escaped = true; pos_++; continue; }
      } else if (c == '\'') {
        // '' is an escaped single quote, otherwise this closes the string.
        if (pos_ + 1 < text_.size() && text_[pos_ + 1] == '\'') { pos_ += 2; continue; }
      }
      if (c == q) break;
      if (c == '\n') lineNum_++;
      pos_++;
    }
    if (eof()) throw asvJSONError(std::string("ADE: unterminated ") + (q == '"' ? "" : "single-quoted ") + "string at line " + std::to_string(lineNum_));
    std::string_view raw(text_.data() + start, pos_ - start);
    pos_++; // consume the closing quote
    return raw;
  }

  std::unique_ptr<asvJSONValue> parseSingleQuotedString() {
    pos_++; // consume '\''
    size_t start = pos_;
    while (!eof()) {
      char c = text_[pos_];
      if (c == '\'') {
        // '' is an escaped single quote, otherwise this closes the string.
        if (pos_ + 1 < text_.size() && text_[pos_ + 1] == '\'') { pos_ += 2; continue; }
        break;
      }
      if (c == '\n') lineNum_++;
      pos_++;
    }
    if (eof()) throw asvJSONError("ADE: unterminated single-quoted string at line " + std::to_string(lineNum_));
    std::string_view raw(text_.data() + start, pos_ - start);
    pos_++; // consume '\''
    return makeAdeString(adeUnescapeSingle(raw));
  }

  std::unique_ptr<asvJSONValue> parseArray(int depth) {
    pos_++; // consume '['
    auto arr = asvJSONValue::makeArray();
    if (!arr || !arr->arr) throw asvJSONError("ADE: out of memory");
    while (true) {
      skipWsAndComments();
      if (eof()) throw asvJSONError("ADE: unterminated array at line " + std::to_string(lineNum_));
      if (peek() == ']') { pos_++; break; }
      if (peek() == ',') { pos_++; continue; } // leading/trailing comma tolerated
      if (!asvJSONValue::checkArraySize(arr->arr->size() + 1))
        throw asvJSONError("ADE: array too large");
      auto elem = parseValue(depth);
      if (!elem) throw asvJSONError("ADE: out of memory");
      arr->arr->push_back(std::move(elem));
      skipWsAndComments();
      if (eof()) throw asvJSONError("ADE: unterminated array at line " + std::to_string(lineNum_));
      if (peek() == ',') { pos_++; continue; }
      if (peek() == ']') { pos_++; break; }
      // A single space between elements on the same line ("[1 2]") is
      // tolerated as an extension: the grammar asks for ',' or a newline
      // (SEP ::= WS? ',' WS? | NEWLINE), but skipping whitespace here lets the
      // next element start right after it without consuming a separator.
    }
    return arr;
  }

  std::unique_ptr<asvJSONValue> parseObject(int depth) {
    pos_++; // consume '{'
    auto obj = asvJSONValue::makeObject();
    if (!obj || !obj->obj) throw asvJSONError("ADE: out of memory");
    while (true) {
      skipWsAndComments();
      if (eof()) throw asvJSONError("ADE: unterminated object at line " + std::to_string(lineNum_));
      if (peek() == '}') { pos_++; break; }
      if (peek() == ',') { pos_++; continue; } // leading/trailing comma tolerated
      ParsedKey pk = parseKey();
      skipWs();
      expect(':', ":");
      std::unique_ptr<asvJSONValue> val;
      if (atLineEndSkipping()) {
        if (strict_) throw asvJSONError("ADE: missing value after ':' at line " + std::to_string(lineNum_));
        val = asvJSONValue::makeNull();
      } else {
        val = parseValue(depth);
      }
      if (!val) throw asvJSONError("ADE: out of memory");
      // YAML-style merge key inside inline objects: "{<<: *default}".
      if (pk.name == "<<" && !pk.quoted) {
        applyMergeKey(obj.get(), std::move(val));
      } else {
        insertKey(obj.get(), std::move(pk), std::move(val), depth);
      }
      skipWsAndComments();
      if (eof()) throw asvJSONError("ADE: unterminated object at line " + std::to_string(lineNum_));
      if (peek() == ',') { pos_++; continue; }
      if (peek() == '}') { pos_++; break; }
      // A single space between "key: value" pairs on the same line
      // ("{a: 1 b: 2}") is tolerated as an extension, mirroring parseArray.
    }
    return obj;
  }

  // Leading-space count of the line the cursor is on, i.e. the indentation of
  // the line that introduced a block scalar header.
  size_t currentLineIndent() const {
    size_t ls = pos_;
    while (ls > 0 && text_[ls - 1] != '\n') ls--;
    size_t n = 0;
    while (ls + n < pos_ && text_[ls + n] == ' ') n++;
    return n;
  }

  std::unique_ptr<asvJSONValue> parseBlockScalar() {
    // Content must be indented deeper than the line carrying the header, so
    // "key: |\nnext: 1" yields an empty scalar and a separate "next" key. Without
    // this floor the first non-blank line sets baseIndent to 0, the following
    // top-level entry is swallowed as content, and the document silently loses
    // a key. Blank lines may still precede the content (they are content).
    const size_t parentIndent = currentLineIndent();
    char indicator = text_[pos_++]; // '|' or '>'
    int explicitIndent = -1;
    char chomp = 0;
    // An explicit indent of 0 is degenerate: baseIndent becomes 0, so the
    // content is never stripped of indentation (|2 strips 2 columns, |0 strips
    // none) and a body line at or above the header's indentation terminates
    // the scalar and is re-parsed as the next entry. That behaviour is
    // documented (and tested: "key: |0\nnext: 1" is an empty scalar "key"
    // followed by "next"), so |0 is legal, not an error.
    if (!eof() && text_[pos_] >= '0' && text_[pos_] <= '9') { explicitIndent = text_[pos_] - '0'; pos_++; }
    if (!eof() && (text_[pos_] == '+' || text_[pos_] == '-')) { chomp = text_[pos_]; pos_++; }
    // Only whitespace, a comment, or end-of-line may follow the indicators. All
    // three comment forms the rest of the parser accepts are honoured here too
    // (skipWsAndComments, atLineEndSkipping): '#', '//' and '/* */'. Accepting
    // only two of them made "key: | /* c */" the one place in the grammar where
    // a block comment was a syntax error, which reads as an oversight rather than
    // a rule. The block form is scanned to its '*/' WITHOUT crossing a line
    // break: a comment opened here but closed on a later line must not swallow
    // the scalar's body.
    while (!eof() && (text_[pos_] == ' ' || text_[pos_] == '\t')) pos_++;
    if (!eof() && text_[pos_] == '#') {
      while (!eof() && text_[pos_] != '\n') pos_++;
    } else if (!eof() && text_[pos_] == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
      while (!eof() && text_[pos_] != '\n') pos_++;
    } else if (!eof() && text_[pos_] == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
      size_t cend = text_.find("*/", pos_ + 2);
      size_t nl = text_.find('\n', pos_);
      if (cend == std::string::npos || (nl != std::string::npos && nl < cend))
        throw asvJSONError("ADE: unterminated comment after block scalar header at line " + std::to_string(lineNum_));
      pos_ = cend + 2;
    } else if (!eof() && text_[pos_] != '\n' && text_[pos_] != '\r') {
      throw asvJSONError("ADE: unexpected content after block scalar header at line " + std::to_string(lineNum_));
    }
    // consume the rest of the header line (newline)
    while (!eof() && text_[pos_] != '\n') pos_++;
    if (!eof()) { pos_++; lineNum_++; }

    std::vector<std::string> content;
    size_t baseIndent = (explicitIndent >= 0) ? static_cast<size_t>(explicitIndent) : static_cast<size_t>(-1);

    while (!eof()) {
      size_t nl = text_.find('\n', pos_);
      std::string_view line = (nl == std::string_view::npos)
        ? text_.substr(pos_) : text_.substr(pos_, nl - pos_);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

      size_t indent = 0;
      while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) indent++;
      bool blank = (indent == line.size());
      // Section 6 forbids tabs in the indentation margin. The check is skipped
      // for a whitespace-only line because indent counts spaces AND tabs there,
      // so " \t" would otherwise be rejected for a tab that is not acting as
      // indentation -- it is a blank line, and a blank line is content (a
      // paragraph break) whose whitespace is discarded anyway. There is nothing
      // for the margin rule to protect on such a line.
      // YAML rule (Section 6): tabs may not be used for indentation. The
      // indentation margin of a block scalar must consist of spaces only;
      // tabs after the margin are part of the content.
      if (!blank) {
        for (size_t i = 0; i < indent; i++) {
          if (line[i] == '\t')
            throw asvJSONError("ADE: tab used as indentation in block scalar at line " + std::to_string(lineNum_));
        }
      }

      if (baseIndent == static_cast<size_t>(-1)) {
        if (!blank) {
          if (indent <= parentIndent) break; // not part of this scalar
          baseIndent = indent;
          content.emplace_back(line.substr(indent));
        } else {
          content.emplace_back(""); // leading blank line is preserved until the base indent is known
        }
      } else if (!blank) {
        if (indent < baseIndent || indent <= parentIndent) break; // dedent terminates the scalar
        content.emplace_back(std::string(line.substr(baseIndent < line.size() ? baseIndent : line.size())));
      } else {
        // Empty lines are part of the block scalar content (paragraph breaks).
        // Termination occurs only when indentation decreases, which is handled
        // above. Preserve the empty line.
        content.emplace_back("");
      }

      if (nl == std::string_view::npos) { pos_ = text_.size(); break; }
      pos_ = nl + 1;
      lineNum_++;
    }

    // Join content lines with single newlines, then apply chomping below. A
    // blank line in the source becomes an empty element (a paragraph break),
    // and a trailing blank line leaves a phantom empty element recording how
    // many trailing newlines the source carried; '-' keeps none, clip one, '+' all.
    std::string out;
    if (indicator == '>') {
      // Folded scalar (Section 6): a single line break between two content
      // lines becomes a space, a run of blank lines becomes that many line
      // breaks, and the last content line's own break is part of the content
      // so that '+' chomping can preserve it. Working from `content` rather
      // than from a pre-joined string is what keeps the break counts exact --
      // joining first loses the final line's terminator, and collapsing a
      // blank-line run to one break would lose the others. Empty lines are
      // part of the content and never terminate the scalar; only a dedent in
      // the loop above does that.
      bool seen = false;
      size_t brk = 0; // blank lines seen since the last content line
      for (const std::string& e : content) {
        if (e.empty()) { brk++; continue; }
        if (!seen) out.append(brk, '\n'); // leading blanks are never folded away
        else if (brk == 0) out += ' ';
        else out.append(brk, '\n');
        out += e;
        brk = 0;
        seen = true;
      }
      if (seen) out.append(brk + 1, '\n');
    } else {
      for (size_t i = 0; i < content.size(); i++) {
        if (i > 0) out += '\n';
        out += content[i];
      }
      // Every collected line was terminated by a line break in the source,
      // including the last one; the join only produced n-1 of them.
      if (!content.empty()) out += '\n';
    }

    // Apply chomping ('-' = none, clip = one, '+' = all) per Section 6. The
    // trailing blank lines of the source are already joined into `out`, and
    // the last content line's own break is present too, so '+' needs no fixup.
    if (chomp == '-') {
      while (!out.empty() && out.back() == '\n') out.pop_back();
    } else if (chomp == '+') {
      // keep as is
    } else {
      // Clip is a guarantee, not a maximum: trailing newlines are stripped and
      // exactly one is put back, so "x\ny" gains the break and the same document
      // means the same thing here and in a YAML parser. The guarantee is keyed
      // on the source having had a body at all, not on `out` being non-empty:
      // a block of nothing but blank lines still has a final line break to keep
      // ("key: |\n\n" is "\n", not ""). Only a scalar with no lines at all -- the
      // header at end of file, or a first content line that dedents -- has none.
      while (!out.empty() && out.back() == '\n') out.pop_back();
      if (!content.empty()) out += '\n';
    }

    return makeAdeString(std::move(out));
  }

  std::unique_ptr<asvJSONValue> parseScalar() {
    size_t start = pos_;
    while (!eof() && adeIsUnquotedChar(text_[pos_])) pos_++;
    if (pos_ == start) throw asvJSONError("ADE: expected value at line " + std::to_string(lineNum_));
    std::string tok(text_.substr(start, pos_ - start));

    std::string lower = adeToLower(tok);
    if (lower == "true") return adeNonNull(asvJSONValue::makeBool(true));
    if (lower == "false") return adeNonNull(asvJSONValue::makeBool(false));
    if (lower == "null") return adeNonNull(asvJSONValue::makeNull());

    // Non-finite floats: .nan, -.nan, .inf, -.inf (YAML-style).
    if (lower == ".nan") return adeNonNull(asvJSONValue::makeDouble(std::numeric_limits<double>::quiet_NaN()));
    if (lower == "-.nan") return adeNonNull(asvJSONValue::makeDouble(-std::numeric_limits<double>::quiet_NaN()));
    if (lower == ".inf" || lower == ".infinity") return adeNonNull(asvJSONValue::makeDouble(std::numeric_limits<double>::infinity()));
    if (lower == "-.inf" || lower == "-.infinity") return adeNonNull(asvJSONValue::makeDouble(-std::numeric_limits<double>::infinity()));

    auto num = adeTryParseNumber(tok);
    if (num) return num;

    if (strict_) {
      for (char c : tok) {
        unsigned char u = static_cast<unsigned char>(c);
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')))
          throw asvJSONError("ADE: strict mode requires quoting value: " + tok);
      }
    }
    return makeAdeString(std::move(tok));
  }

  std::string parseAnchorName() {
    pos_++; // consume '&' or '*'
    size_t start = pos_;
    while (!eof() && adeIsIdentCont(text_[pos_])) pos_++;
    if (pos_ == start) throw asvJSONError("ADE: expected anchor name at line " + std::to_string(lineNum_));
    // ANCHOR/ALIAS ::= '&'/'*' IDENTIFIER, and IDENTIFIER is
    // [A-Za-z_][A-Za-z0-9_-]* -- the first character must be a letter or '_'.
    // adeIsIdentCont above is permissive so the scan can find the name's end;
    // the shape is validated here, so "&1abc" is rejected instead of silently
    // accepted as an anchor named "1abc".
    if (!adeIsIdentStart(text_[start]))
      throw asvJSONError("ADE: anchor name must start with a letter or '_' at line " + std::to_string(lineNum_));
    return std::string(text_.substr(start, pos_ - start));
  }

  void registerAnchor(const std::string& name, const asvJSONValue* v) {
    if (anchors_.size() >= kMaxAnchors) throw asvJSONError("ADE: too many anchors");
    auto c = cloneValue(v);
    if (!c) throw asvJSONError("ADE: failed to store anchor &" + name);
    anchors_[name] = std::move(c);
  }

  std::unique_ptr<asvJSONValue> parseAnchored(int depth) {
    std::string name = parseAnchorName();
    skipWsAndComments();
    auto val = parseValue(depth);
    registerAnchor(name, val.get());
    return val;
  }

  std::unique_ptr<asvJSONValue> parseAlias() {
    std::string name = parseAnchorName();
    auto it = anchors_.find(name);
    if (it == anchors_.end()) throw asvJSONError("ADE: undefined alias *" + name);
    // An anchor is registered only after its value is fully parsed, so a value
    // can never refer to itself or to an anchor defined later in the document:
    // both are caught above as an undefined alias. Resolving an alias is a
    // single deep copy of the anchored value (cloneValue, bounded by
    // MAX_CLONE_NODES) and never recurses into further anchor resolution, so
    // there is no cycle to detect and no resolution-depth chain to bound.
    auto res = cloneValue(it->second.get());
    if (!res) throw asvJSONError("ADE: failed to resolve alias *" + name);
    return res;
  }

  std::unique_ptr<asvJSONValue> applyTag(const std::string& tag, std::unique_ptr<asvJSONValue> val) {
    // Explicit type tags: coerce the value to the requested scalar type.
    if (tag == "int") {
      if (val->type == asvJSONValue::INT) return val;
      if (val->type == asvJSONValue::DOUBLE) {
        if (val->dbl < static_cast<double>(INT64_MIN) || val->dbl > static_cast<double>(INT64_MAX) ||
            val->dbl != std::floor(val->dbl))
          throw asvJSONError("ADE: !int requires an integral value");
        return adeNonNull(asvJSONValue::makeInt(static_cast<int64_t>(val->dbl)));
      }
      if (val->type == asvJSONValue::STRING) {
        auto n = adeTryParseNumber(val->str_data);
        if (!n || n->type != asvJSONValue::INT) throw asvJSONError("ADE: !int requires an integer literal");
        return n;
      }
      throw asvJSONError("ADE: !int requires a numeric value");
    }
    if (tag == "float") {
      if (val->type == asvJSONValue::DOUBLE) return val;
      if (val->type == asvJSONValue::INT) return adeNonNull(asvJSONValue::makeDouble(static_cast<double>(val->num)));
      if (val->type == asvJSONValue::STRING) {
        auto n = adeTryParseNumber(val->str_data);
        if (!n) throw asvJSONError("ADE: !float requires a numeric literal");
        if (n->type == asvJSONValue::INT) return adeNonNull(asvJSONValue::makeDouble(static_cast<double>(n->num)));
        // adeTryParseNumber returns a raw_number STRING for a literal no double
        // holds exactly (int64 overflow, or more significant digits than
        // MAX_DOUBLE_SIG_DIGITS). Returning it produced a node carrying the
        // !float tag but of type STRING -- a !float that is not a float, and one
        // that re-encodes with no tag at all. !int rejects the same case outright
        // ("!int requires an integer literal"); !float must not be laxer than the
        // integer path, so an unrepresentable literal is an error. The exact
        // text is still preserved by writing it unquoted, where the same
        // raw_number STRING is the documented representation.
        if (n->type != asvJSONValue::DOUBLE)
          throw asvJSONError("ADE: !float requires a literal representable as a double");
        return n;
      }
      throw asvJSONError("ADE: !float requires a numeric value");
    }
    if (tag == "string") {
      if (val->type == asvJSONValue::STRING) return val;
      // The conversions below go through the checked factory rather than
      // makeStringView() directly: a null result has to become a reported
      // error, not a null unique_ptr handed back to the caller. None of these
      // buffers can reach the limit today (an int64 is at most 20 digits, a
      // double at most ~24), so the check is a guard against a future change
      // rather than a reachable limit -- but it keeps the Section 12 promise
      // local instead of resting on "the value was already checked upstream".
      if (val->type == asvJSONValue::INT) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val->num);
        if (ec != std::errc()) throw asvJSONError("ADE: !string conversion failed");
        return makeAdeString(std::string(buf, ptr - buf));
      }
      if (val->type == asvJSONValue::DOUBLE) {
        std::string tmp;
        fmtDoubleVal(val->dbl, tmp);
        return makeAdeString(std::move(tmp));
      }
      if (val->type == asvJSONValue::BOOL_VAL) {
        return makeAdeString(std::string(val->flag ? "true" : "false"));
      }
      throw asvJSONError("ADE: !string cannot convert this value");
    }
    if (tag == "bool") {
      if (val->type == asvJSONValue::BOOL_VAL) return val;
      if (val->type == asvJSONValue::INT) return adeNonNull(asvJSONValue::makeBool(val->num != 0));
      if (val->type == asvJSONValue::DOUBLE) return adeNonNull(asvJSONValue::makeBool(val->dbl != 0.0));
      if (val->type == asvJSONValue::STRING) {
        std::string lower = adeToLower(val->str_data);
        if (lower == "true" || lower == "yes" || lower == "on" || lower == "1") return adeNonNull(asvJSONValue::makeBool(true));
        if (lower == "false" || lower == "no" || lower == "off" || lower == "0") return adeNonNull(asvJSONValue::makeBool(false));
        throw asvJSONError("ADE: !bool requires true/false");
      }
      throw asvJSONError("ADE: !bool requires a boolean value");
    }
    if (tag == "null") {
      // Explicit null (useful for merge patch semantics): value is discarded.
      return adeNonNull(asvJSONValue::makeNull());
    }
    if (tag == "schema") {
      // Schema annotation: value is a schema reference (string) or an inline
      // JSON Schema object. Preserved as a CUSTOM_TAG for user validation,
      // with the tag name ("schema") stored separately from the value.
      if (val->type == asvJSONValue::STRING || val->type == asvJSONValue::OBJECT) {
        auto custom = std::make_unique<asvJSONValue>();
        custom->type = asvJSONValue::CUSTOM_TAG;
        custom->str_data = "schema";
        custom->custom_value = std::move(val);
        return custom;
      }
      throw asvJSONError("ADE: !schema requires a string or object");
    }
    if (tag == "base64") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("ADE: !base64 requires a string value");
      bool err = false;
      auto bytes = decodeBase64Fast(val->str_data.data(), val->str_data.size(), &err);
      if (err) throw asvJSONError("ADE: invalid base64 data");
      auto b = asvJSONValue::makeBinary(bytes.data(), bytes.size());
      if (!b) throw asvJSONError("ADE: binary data too large");
      return b;
    }
    if (tag == "bin") {
      std::vector<uint8_t> bytes;
      if (val->type == asvJSONValue::STRING) {
        std::string hex = val->str_data;
        if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
        if (hex.empty() || hex.size() % 2 != 0) throw asvJSONError("ADE: !bin requires even-length hex string");
        for (size_t i = 0; i < hex.size(); i += 2) {
          int h1 = adeHexDigit(hex[i]), h2 = adeHexDigit(hex[i + 1]);
          if (h1 < 0 || h2 < 0) throw asvJSONError("ADE: invalid hex in !bin");
          bytes.push_back(static_cast<uint8_t>((h1 << 4) | h2));
        }
      } else if (val->type == asvJSONValue::INT) {
        if (val->num < 0) throw asvJSONError("ADE: !bin requires a non-negative integer");
        uint64_t n = static_cast<uint64_t>(val->num);
        if (n == 0) {
          bytes.push_back(0);
        } else {
          uint8_t tmp[8];
          int cnt = 0;
          while (n > 0) { tmp[cnt++] = static_cast<uint8_t>(n & 0xFF); n >>= 8; }
          for (int i = cnt - 1; i >= 0; i--) bytes.push_back(tmp[i]);
        }
      } else {
        throw asvJSONError("ADE: !bin requires a hex literal or string");
      }
      auto b = asvJSONValue::makeBinary(bytes.data(), bytes.size());
      if (!b) throw asvJSONError("ADE: binary data too large");
      return b;
    }
    if (tag == "datetime") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("ADE: !datetime requires a string value");
      time_t ts = 0;
      int ms = 0;
      int tz = 0;
      if (!adeTryParseDateTime(val->str_data, ts, &ms, &tz))
        throw asvJSONError("ADE: invalid datetime: " + val->str_data);
      return adeNonNull(asvJSONValue::makeDateTime(ts, ms, tz));
    }
    if (tag == "regex") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("ADE: !regex requires a string value");
      // Two wire forms are accepted.
      //
      // Canonical: !regex "__REGEX__<pattern>|<opts>|" (marker + trailing
      // terminator, literal '|' inside a field written as \u007C). The split is
      // done on the RAW token text, before unescaping, so an escaped pipe in the
      // pattern is not mistaken for the separator. The marker is what makes it
      // unambiguous: without it "a|b|c" is indistinguishable from a canonical
      // body whose options are "b" and whose trailing "c" would be dropped.
      //
      // Legacy (no marker): the DECODED text split at the LAST '|', the
      // historical behaviour. Old documents keep loading with the meaning they
      // had, including a pattern that contains a pipe.
      std::string pattern, opts;
      if (regexRaw_.empty()) {
        adeSplitLegacyRegex(val->str_data, pattern, opts);
      } else {
        std::string_view raw(regexRaw_);
        if (raw.compare(0, kAdeRegexMarker.size(), kAdeRegexMarker) == 0)
          adeSplitCanonicalRegex(raw.substr(kAdeRegexMarker.size()), regexRawSingle_, pattern, opts);
        else
          adeSplitLegacyRegex(val->str_data, pattern, opts);
      }
      auto r = asvJSONValue::makeRegex(pattern.c_str(), opts.empty() ? nullptr : opts.c_str());
      if (!r) throw asvJSONError("ADE: invalid regex");
      return r;
    }
    if (tag == "objectid") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("ADE: !objectid requires a string value");
      // The encoder emits a 24-char hex string (fmtObjectIdHexVal). Restore it
      // to the 12 raw bytes that makeObjectId requires.
      if (val->str_data.empty() || val->str_data.size() != 24) throw asvJSONError("ADE: invalid !objectid value");
      uint8_t bytes[12];
      for (int i = 0; i < 12; i++) {
        int hi = adeHexDigit(val->str_data[i * 2]);
        int lo = adeHexDigit(val->str_data[i * 2 + 1]);
        if (hi < 0 || lo < 0) throw asvJSONError("ADE: invalid !objectid value");
        bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
      }
      return adeNonNull(asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(bytes), 12)));
    }
    if (tag == "timestamp") {
      // The encoder emits "!timestamp <int64>". Accept a bare integer (from
      // the encoder) or an ISO-8601 string (text form); on int64 overflow the
      // value arrives as a raw_number STRING, which is also accepted.
      if (val->type == asvJSONValue::INT) return adeNonNull(asvJSONValue::makeTimestamp(val->num));
      if (val->type == asvJSONValue::STRING) {
        errno = 0;
        char* end = nullptr;
        long long ts = std::strtoll(val->str_data.c_str(), &end, 10);
        if (!errno && end && *end == 0 && end != val->str_data.c_str())
          return adeNonNull(asvJSONValue::makeTimestamp(ts));
        time_t t = 0;
        int ms = 0;
        if (!adeTryParseDateTime(val->str_data, t, &ms))
          throw asvJSONError("ADE: invalid timestamp: " + val->str_data);
        return adeNonNull(asvJSONValue::makeTimestamp(static_cast<int64_t>(t)));
      }
      throw asvJSONError("ADE: !timestamp requires an integer or string value");
    }
    if (tag == "ext" || tag == "extension") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("ADE: !ext requires a string value");
      const std::string& s = val->str_data;
      size_t colon = s.find(':');
      if (colon == std::string::npos) throw asvJSONError("ADE: !ext requires \"type:data\"");
      errno = 0;
      char* end = nullptr;
      long type = std::strtol(s.c_str(), &end, 10);
      if (errno == ERANGE || !end || end != s.c_str() + static_cast<long>(colon))
        throw asvJSONError("ADE: invalid extension type");
      // strtol reports only long-range overflow. The stored type is int8_t
      // (Section 8), so a value outside [-128, 127] must be rejected here:
      // static_cast<int8_t>(300) silently wrapped to 44.
      if (type < -128 || type > 127)
        throw asvJSONError("ADE: extension type out of range [-128, 127]");
      bool err = false;
      auto bytes = decodeBase64Fast(s.data() + colon + 1, s.size() - colon - 1, &err);
      if (err) throw asvJSONError("ADE: invalid base64 in !ext");
      auto e = asvJSONValue::makeExtension(static_cast<int8_t>(type), bytes.data(), bytes.size());
      if (!e) throw asvJSONError("ADE: extension data too large");
      return e;
    }
    // Preserve unknown tags using CUSTOM_TAG type. The tag name is stored
    // separately (str_data) from its value (custom_value), which is the
    // canonical !<name> <value> form and round-trips without ambiguity.
    auto custom = std::make_unique<asvJSONValue>();
    custom->type = asvJSONValue::CUSTOM_TAG;
    custom->str_data = tag;
    custom->custom_value = std::move(val);
    return custom;
  }

  std::unique_ptr<asvJSONValue> parseTagged(int depth) {
    pos_++; // consume '!'
    std::string tag;
    bool verbatim = false;
    if (!eof() && peek() == '<') {
      verbatim = true;
      // Verbatim URI tag: !<tag:example.com,2026:type> -- any content up to '>'.
      pos_++;
      size_t start = pos_;
      while (!eof() && text_[pos_] != '>') pos_++;
      if (eof()) throw asvJSONError("ADE: unterminated tag URI at line " + std::to_string(lineNum_));
      // An empty body is accepted on purpose: a CUSTOM_TAG may carry an empty
      // name, and the encoder writes such a name as "!<>" so the round-trip
      // stays total. "!<" is unambiguous -- the '<' cannot open a simple name.
      //
      // The brackets mean "this name is a literal CUSTOM_TAG name", NOT "a
      // synonym for the simple form". That is what lets the form express a
      // custom tag whose name collides with a built-in: !<regex> "a|b" is a
      // CUSTOM_TAG named "regex", while !regex "a|b" is a REGEX. Matching on the
      // name after stripping the brackets made the two spellings identical, so a
      // custom tag named after any built-in was unreachable -- and the encoder
      // hit exactly that, writing "!regex abc" for a CUSTOM_TAG named "regex"
      // and reading it back as a REGEX (see CUSTOM_TAG in adeWriteValue).
      // verbatim suppresses built-in dispatch below.
      tag.assign(text_.substr(start, pos_ - start));
      pos_++; // consume '>'
    } else {
      size_t start = pos_;
      while (!eof() && adeIsTagChar(text_[pos_])) pos_++;
      if (pos_ == start) throw asvJSONError("ADE: expected tag name at line " + std::to_string(lineNum_));
      tag = std::string(text_.substr(start, pos_ - start));
      // adeIsTagChar is deliberately permissive about the FIRST character so
      // the scan above can find where the name ends; validate the shape now.
      // A name that is not identifier-like has no !name spelling and must be
      // written as the verbatim !<name> form, so it is rejected here.
      if (!adeIsSimpleTagName(tag))
        throw asvJSONError("ADE: invalid tag name '!" + tag + "' at line " + std::to_string(lineNum_));
    }
    // A bare tag with no value. The end-of-line test MUST run before
    // skipWsAndComments(), which also crosses line breaks: with it first, the
    // check below could never see '\n' (skipWs had already eaten it) and the
    // NEXT line's first token was consumed as the tag's value. "a: !null\nb: 1"
    // parsed 'b' as the value of !null, then failed on the leftover ':' with
    // "invalid key at line 2". Appendix A agrees: WS is [ \t]* and does not
    // include a line break, so a tag's value may not live on a later line.
    if (atTagValueEnd()) {
      // Only !null is meaningful without a value, and only in the SIMPLE form:
      // the verbatim !<null> is a custom tag, so it keeps the standard
      // "requires a value" rule.
      if (!verbatim && tag == "null") return adeNonNull(asvJSONValue::makeNull());
      throw asvJSONError("ADE: !" + tag + " requires a value");
    }
    skipWsAndComments();
    std::string anchor;
    if (!eof() && peek() == '&') {
      anchor = parseAnchorName();
      // Same rule after the anchor: "!null &a" on its own line is a bare null.
      // A deliberate extension beyond Appendix A, whose VALUE is mandatory
      // ("TAGGED_VALUE ::= TAG WS? ANCHOR? WS? VALUE"). A bare "!null" already
      // carries no VALUE, so accepting the anchored spelling is the consistent
      // reading rather than a new capability -- and an anchor with nothing to
      // point at would otherwise be unrepresentable. Every other tag still
      // requires its value. Documented in ade.md beside the !null rule.
      if (atTagValueEnd()) {
        if (!verbatim && tag == "null") {
          auto n = adeNonNull(asvJSONValue::makeNull());
          registerAnchor(anchor, n.get());
          return n;
        }
        throw asvJSONError("ADE: !" + tag + " requires a value");
      }
      skipWsAndComments();
    }
    // !regex reads the RAW token body: the pattern/options separator has to be
    // found before escape processing, otherwise an escaped pipe inside the
    // pattern is indistinguishable from the separator. Both quoting styles are
    // scanned so that the canonical __REGEX__ marker is honoured in either one;
    // any other value form uses the decoded-text fallback in applyTag.
    regexRaw_.clear();
    if (!verbatim && tag == "regex" && !eof() && (peek() == '"' || peek() == '\'')) {
      char q = peek();
      std::string_view raw = scanRawQuotedString(q);
      regexRaw_.assign(raw.data(), raw.size());
      regexRawSingle_ = (q == '\'');
      std::string decoded;
      try {
        decoded = (q == '"') ? adeUnescape(raw) : adeUnescapeSingle(raw);
      } catch (const std::exception& e) {
        regexRaw_.clear();
        throw asvJSONError(std::string(e.what()) + " at line " + std::to_string(lineNum_));
      }
      auto val = makeAdeString(std::move(decoded));
      val = applyTag(tag, std::move(val));
      regexRaw_.clear();
      if (!anchor.empty()) registerAnchor(anchor, val.get());
      return val;
    }
    auto val = parseValue(depth);
    // Apply the tag first so the anchor references the final tagged value. A
    // verbatim name skips applyTag and builds the CUSTOM_TAG directly: the
    // brackets already said the name is literal, so no built-in may claim it.
    if (verbatim) {
      auto custom = std::make_unique<asvJSONValue>();
      custom->type = asvJSONValue::CUSTOM_TAG;
      custom->str_data = tag;
      custom->custom_value = std::move(val);
      regexRaw_.clear();
      if (!anchor.empty()) registerAnchor(anchor, custom.get());
      return custom;
    }
    val = applyTag(tag, std::move(val));
    regexRaw_.clear();
    if (!anchor.empty()) registerAnchor(anchor, val.get());
    return val;
  }

  // Dotted keys expand into nested objects when every unquoted segment is a
  // plain identifier; quoted segments are always literal and permitted, which
  // is what makes "a.b".c (literal first segment containing a dot) work.
  static bool adeDottedKeyExpandable(const std::vector<std::pair<std::string, bool>>& segs) {
    for (const auto& s : segs) {
      if (!s.second && !adeKeyIsIdentLike(s.first)) return false;
    }
    return true;
  }

  // Merge keys (YAML-compatible): "<<: *default" copies the entries of the
  // referenced object into the current mapping. Keys already present in the
  // target win over merged ones; for a sequence of objects, later objects win.
  static void adeMergeInto(asvJSONValue* obj, const asvJSONValue* src) {
    if (!obj || !obj->obj || !src || src->type != asvJSONValue::OBJECT || !src->obj) return;
    for (const auto& [k, v] : *src->obj) {
      if (obj->obj->find(k) == obj->obj->end()) {
        if (!asvJSONValue::checkObjectSize(obj->obj->size() + 1))
          throw asvJSONError("ADE: object too large");
        auto copy = cloneValue(v.get());
        // A failed clone (clone budget exhausted) must not be swallowed: that
        // would drop a key from the merge with no error, unlike registerAnchor/
        // parseAlias, which both turn the same failure into a throw.
        if (!copy) throw asvJSONError("ADE: failed to clone merged key " + k);
        obj->obj->emplace(k, std::move(copy));
      }
    }
  }

  void applyMergeKey(asvJSONValue* obj, std::unique_ptr<asvJSONValue> val) {
    if (!val) throw asvJSONError("ADE: merge key requires an object or sequence of objects");
    if (val->type == asvJSONValue::OBJECT) {
      adeMergeInto(obj, val.get());
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      // Later objects in the sequence take precedence (YAML semantics), so
      // iterate in reverse: the first-processed source keeps its keys and
      // earlier sources only fill in the gaps.
      for (auto it = val->arr->rbegin(); it != val->arr->rend(); ++it) {
        if ((*it)->type != asvJSONValue::OBJECT)
          throw asvJSONError("ADE: merge key sequence must contain only objects");
        adeMergeInto(obj, it->get());
      }
    } else {
      throw asvJSONError("ADE: merge key requires an object or sequence of objects");
    }
  }

  void insertKey(asvJSONValue* obj, ParsedKey pk, std::unique_ptr<asvJSONValue> val, int depth = 1) {
    if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) {
      throw asvJSONError("ADE: cannot insert key into non-object");
    }
    // Multi-segment (dotted) keys expand into nested objects; a fully quoted
    // key (single segment) is always a literal key (Section 3).
    if (!pk.segs.empty() && adeDottedKeyExpandable(pk.segs)) {
      asvJSONValue* cur = obj;
      // `depth` is the object's nesting depth in the tree; each new dotted
      // segment adds one object level, bounded by the nesting limit (Section 12).
      int prefixDepth = depth;
      for (size_t i = 0; i + 1 < pk.segs.size(); i++) {
        const std::string& seg = pk.segs[i].first;
        auto it = cur->obj->find(seg);
        if (it == cur->obj->end()) {
          if (!asvJSONValue::checkObjectSize(cur->obj->size() + 1))
            throw asvJSONError("ADE: object too large");
          if (prefixDepth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
            throw asvJSONError("ADE: nesting too deep");
          prefixDepth++;
          auto no = asvJSONValue::makeObject();
          if (!no || !no->obj) throw asvJSONError("ADE: out of memory");
          auto* raw = no.get();
          cur->obj->emplace(seg, std::move(no));
          cur = raw;
        } else if (it->second->type == asvJSONValue::OBJECT) {
          cur = it->second.get();
        } else {
          // Existing key is not an object - cannot expand dotted key further.
          throw asvJSONError("ADE: key conflict - intermediate key '" + seg + "' is not an object");
        }
      }
      const std::string& leaf = pk.segs.back().first;
      auto it = cur->obj->find(leaf);
      if (it != cur->obj->end()) {
        if (strict_) throw asvJSONError("ADE: duplicate key '" + leaf + "' in strict mode");
        cur->obj->insert_or_assign(leaf, std::move(val));
      } else {
        if (!asvJSONValue::checkObjectSize(cur->obj->size() + 1))
          throw asvJSONError("ADE: object too large");
        cur->obj->emplace(leaf, std::move(val));
      }
    } else {
      auto it = obj->obj->find(pk.name);
      if (it != obj->obj->end()) {
        if (strict_) throw asvJSONError("ADE: duplicate key '" + pk.name + "' in strict mode");
        obj->obj->insert_or_assign(std::move(pk.name), std::move(val));
      } else {
        if (!asvJSONValue::checkObjectSize(obj->obj->size() + 1))
          throw asvJSONError("ADE: object too large");
        obj->obj->emplace(std::move(pk.name), std::move(val));
      }
    }
  }

};

std::unique_ptr<asvJSONValue> ADEParser::parseDocument() {
  skipWsAndComments();
  if (eof()) return adeNonNull(asvJSONValue::makeObject()); // empty document

  std::unique_ptr<asvJSONValue> root = asvJSONValue::makeObject();
  if (!root || !root->obj) throw asvJSONError("ADE: out of memory");
    bool any = false;
    while (true) {
      skipWsAndComments();
      if (eof()) break;
      char c = peek();
      if (c == '[' || c == '{') {
        if (any) throw asvJSONError("ADE: unexpected top-level collection at line " + std::to_string(lineNum_));
        auto coll = (c == '[') ? parseArray(2) : parseObject(2);
        skipWsAndComments();
        if (!eof()) throw asvJSONError("ADE: trailing content after top-level collection");
        return coll;
      }
      if (c == '|' || c == '>') {
        auto val = parseBlockScalar();
        skipWsAndComments();
        if (!eof()) throw asvJSONError("ADE: trailing content after block scalar");
        return val;
      }
      ParsedKey key = parseKey();
      if (key.name.empty()) throw asvJSONError("ADE: expected key at line " + std::to_string(lineNum_));
      skipWs();
      if (eof() || peek() != ':') throw asvJSONError("ADE: expected ':' after key '" + key.name + "' at line " + std::to_string(lineNum_));
      pos_++;
      std::unique_ptr<asvJSONValue> val;
      if (atLineEndSkipping()) {
        if (strict_) throw asvJSONError("ADE: missing value after ':' at line " + std::to_string(lineNum_));
        val = asvJSONValue::makeNull();
      } else {
        val = parseValue(1);
      }
      if (!val) throw asvJSONError("ADE: out of memory");
      // YAML-style merge key: "<<: *default" (only the unquoted form merges).
      if (key.name == "<<" && !key.quoted) {
        applyMergeKey(root.get(), std::move(val));
      } else {
        insertKey(root.get(), std::move(key), std::move(val));
      }
      any = true;
    }
    return root;
  }

// Fall back to a plain string for a single-line document that failed to parse.
// Only applied when the line cannot be a structured ADE document at all, so
// that malformed documents and strict-mode rejections report their error
// instead of silently degrading to a plain string.
inline std::unique_ptr<asvJSONValue> adePlainTextFallback(const std::string& text, bool strict) {
  std::string_view t = adeTrim(text);
  if (t.empty()) return nullptr;
  if (t.find('\n') != std::string_view::npos) return nullptr;
  char c = t[0];
  if (c == '[' || c == '{' || c == '|' || c == '>') return nullptr;
  // A ':' only disambiguates a key if the text is not a quoted scalar root or
  // a tagged value; inside quotes it is part of the string, and inside a tag
  // name ('!ns:tag') it is a namespace separator, so both must be preserved.
  if (c != '"' && c != '\'' && c != '!' && t.find(':') != std::string_view::npos) return nullptr;
  // Unquoted multi-word plain text is a verbatim string (a scalar token never
  // contains a space), so keep the whole trimmed line as-is. A leading '!',
  // '&' or '*' is a structured start (tag / anchor / alias) and is handed to
  // the scalar-document parse below, which yields a real value for valid
  // documents (e.g. a root "!null", "&x 1" or "*x" after "&x 1") and rethrows
  // the original error for malformed ones.
  if (c != '"' && c != '\'' && c != '!' && c != '&' && c != '*' && t.find(' ') != std::string_view::npos) {
    if (!asvJSONValue::checkStringLen(t.size())) throw asvJSONError("ADE: string too long");
    return adeNonNull(asvJSONValue::makeStringView(t));
  }
  // Keep the temporary buffer alive for the whole parse: ADEParser stores a
  // string_view, so the std::string must outlive the parser object.
  std::string buffer(t);
  try {
    ADEParser p(buffer, strict);
    return p.parseScalarDocValue();
  } catch (...) {
    return nullptr;
  }
}

// Versioning of the "# ADE vX.Y" header (Section 6 of the spec): the major
// digit X is a breaking change, the minor digit Y is backward compatible.
// The current implementation supports major version 1. Documents declaring a
// newer major version are rejected; older minor versions are accepted. The
// value below is the version this build emits; the parser accepts any 1.x
// (see adeStripAndCheckHeader).
// Kept as a public accessor so external code can compose headers; the encoder
// itself does not re-emit the header on round-trip (documented above).
inline std::string adeHeaderVersion() {
  return "1.1";
}

// Detect and validate an optional "# ADE vX.Y [flags]" header on the first
// non-empty line. The header is stripped from the returned document text and
// recognized flags (currently "strict") update `strict`. Unknown flags are
// ignored so that forward-compatible minor versions keep working.
inline std::string adeStripAndCheckHeader(const std::string& text, bool& strict) {
  std::string_view sv(text);
  // Sec. 2.1: the first non-empty line may be an optional "# ADE vX.Y" header.
  size_t scan = 0;
  size_t headEnd = std::string_view::npos;
  std::string_view t;
  while (scan < sv.size()) {
    size_t eol = sv.find('\n', scan);
    std::string_view line = (eol == std::string_view::npos)
      ? sv.substr(scan) : sv.substr(scan, eol - scan);
    std::string_view lt = adeTrim(line);
    if (!lt.empty()) {
      if (lt.size() < 6 || lt.substr(0, 6) != "# ADE ") return text;
      t = lt;
      headEnd = (eol == std::string_view::npos) ? sv.size() : eol + 1;
      break;
    }
    if (eol == std::string_view::npos) break;
    scan = eol + 1;
  }
  if (headEnd == std::string_view::npos) return text;
  // t = "# ADE v1.1 [flags]"
  std::string_view rest = t.substr(6); // "v1.1 [flags]"
  if (rest.empty() || rest[0] != 'v') return text;
  rest.remove_prefix(1);
  size_t i = 0;
  while (i < rest.size() && rest[i] >= '0' && rest[i] <= '9') i++;
  if (i == 0) throw asvJSONError("ADE: malformed version header");
  int major = 0;
  for (size_t k = 0; k < i; k++) major = major * 10 + (rest[k] - '0');
  if (i >= rest.size() || rest[i] != '.') throw asvJSONError("ADE: malformed version header");
  i++;
  size_t j = i;
  while (j < rest.size() && rest[j] >= '0' && rest[j] <= '9') j++;
  if (j == i) throw asvJSONError("ADE: malformed version header");
  int minor = 0;
  for (size_t k = i; k < j; k++) minor = minor * 10 + (rest[k] - '0');
  if (major != 1) throw asvJSONError("ADE: unsupported ADE version v" + std::to_string(major) + "." + std::to_string(minor));
  (void)minor;
  // Optional flags after the version number.
  size_t k = j;
  while (k < rest.size()) {
    while (k < rest.size() && (rest[k] == ' ' || rest[k] == '\t')) k++;
    size_t start = k;
    while (k < rest.size() && rest[k] != ' ' && rest[k] != '\t' && rest[k] != '\r') k++;
    if (k == start) break;
    std::string_view flag = rest.substr(start, k - start);
    if (flag == "strict") strict = true;
    // Unknown flags are ignored for forward compatibility.
  }
  // Strip the header line (and any leading blank lines) from the document text.
  if (headEnd >= sv.size()) return "";
  return std::string(text.substr(headEnd));
}

inline std::unique_ptr<asvJSONValue> parseADE(const std::string& text, bool strict) {
  std::string normalized;
  adeNormalizeEncoding(text, normalized);
  std::string body = adeStripAndCheckHeader(normalized, strict);
  std::vector<std::string> docs = splitAdeDocuments(body);
  if (docs.size() > 1) {
    auto arr = asvJSONValue::makeArray();
    if (!arr || !arr->arr) throw asvJSONError("ADE: out of memory");
    for (const auto& doc : docs) {
      std::unique_ptr<asvJSONValue> v;
      try {
        ADEParser p(doc, strict);
        v = p.parseDocument();
      } catch (const asvJSONError&) {
        v = adePlainTextFallback(doc, strict);
        if (!v) throw;
      }
      if (!v) throw asvJSONError("ADE: out of memory");
      if (!asvJSONValue::checkArraySize(arr->arr->size() + 1))
        throw asvJSONError("ADE: too many documents");
      arr->arr->push_back(std::move(v));
    }
    return arr;
  }
  try {
    ADEParser p(docs[0], strict);
    return p.parseDocument();
  } catch (const asvJSONError&) {
    auto v = adePlainTextFallback(docs[0], strict);
    if (!v) throw;
    return v;
  }
}

// ====================== Serializer ======================

// A key can be written bare only if it is a plain identifier. Keys containing
// dots must be quoted so that they round-trip as literal keys rather than
// being re-expanded as dotted (nested) keys by the parser.
inline bool adeKeySafe(const std::string& key) {
  if (key.empty() || key.find('.') != std::string::npos) return false;
  if (!adeIsIdentStart(key[0])) return false;
  for (char c : key) if (!adeIsIdentCont(c)) return false;
  return true;
}

inline void adeWriteQuoted(std::ostream& os, std::string_view s) {
  // UPPERCASE hex digits, matching adeEscPipe and the canonical bodies in
  // ade.md, so one character is never spelled two ways across the two writers.
  os << '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': os << "\\\""; break;
      case '\\': os << "\\\\"; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      case '\b': os << "\\b"; break;
      case '\f': os << "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
          os << buf;
        } else {
          os << static_cast<char>(c);
        }
    }
  }
  os << '"';
}

// Write `s` as ADE string-body content, additionally turning any literal '|'
// into \u007C. The escape is UPPERCASE to match this file's own adeWriteQuoted
// and the bodies spelled out in ade.md ("__REGEX__a\u007C||"); one spelling per
// character keeps a diff between two documents that differ only in that escape
// meaningful. The decoder accepts either case. Used by the !regex encoder so that raw '|' characters appear
// ONLY as the pattern/options separator and the trailing terminator, never as
// pattern or option content. The decoder splits the raw token text before
// unescaping, so the escape is what keeps the two apart. A plain backslash
// escape is not usable: adeUnescape would collapse "\|" into "|" (and "\|" is
// not itself a valid ADE escape).
inline void adeEscPipe(std::ostream& os, std::string_view s) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  for (unsigned char c : s) {
    if (c == '|') { os << "\\u007C"; continue; }
    switch (c) {
      case '"': os << "\\\""; break;
      case '\\': os << "\\\\"; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      case '\b': os << "\\b"; break;
      case '\f': os << "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
          os << buf;
        } else {
          os << static_cast<char>(c);
        }
    }
  }
}

inline void adeWriteKey(std::ostream& os, const std::string& k, bool strict) {
  // Strict mode requires all keys to be quoted (Section 3).
  if (!strict && adeKeySafe(k)) os << k;
  else adeWriteQuoted(os, k);
}

inline bool adeStringNeedsQuote(std::string_view s, bool strict) {
  if (s.empty()) return true;
  std::string lower = adeToLower(s);
  if (lower == "true" || lower == "false" || lower == "null" ||
      lower == ".nan" || lower == "-.nan" ||
      lower == ".inf" || lower == "-.inf" || lower == ".infinity" || lower == "-.infinity") return true;
  if (adeTryParseNumber(std::string(s))) return true;
  // Strings starting with comment prefixes would be misinterpreted as comments
  // by the parser (skipWsAndComments runs before parseValue).
  if (s.size() >= 2 && s[0] == '/' && (s[1] == '/' || s[1] == '*')) return true;
  for (unsigned char c : s) {
    if (!adeIsUnquotedChar(static_cast<char>(c))) return true;
    if (strict && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return true;
  }
  return false;
}

inline void adeWriteIndent(std::ostream& os, int indent) {
  for (int i = 0; i < indent; i++) os << "  ";
}

// A string can be written as a block scalar only when it round-trips through
// the parser. The decoder treats any all-whitespace line (including one made
// of spaces/tabs only) as a blank line, and leading whitespace on a content
// line is absorbed into the base indent (or rejected outright for tabs), so
// the encoder keeps a conservative subset here: at least one content line,
// every line is either truly empty or begins with a non-whitespace character,
// and any number of trailing blank lines (kept exactly by a '|+' header, with
// blanks becoming paragraph breaks). A '\r' anywhere -- whether a CRLF line
// ending or an embedded carriage return -- cannot round-trip here, because the
// decoder strips a line-ending '\r' when reading content, so such strings are
// rejected outright and written as quoted strings instead, where '\r' is
// escaped as \\r and preserved exactly. Other strings (whose value cannot be
// preserved exactly) are likewise written as quoted strings by the caller.
inline bool adeCanBlockEncode(std::string_view s) {
  if (s.find('\n') == std::string_view::npos) return false;
  if (s.find('\r') != std::string_view::npos) return false;
  // Any number of trailing newlines round-trips via a '|+' header that keeps
  // all of them, and blank lines (including repeats) become paragraph breaks.
  bool seenContent = false;
  size_t i = 0;
  while (i < s.size()) {
    size_t nl = s.find('\n', i);
    std::string_view line = (nl == std::string_view::npos) ? s.substr(i) : s.substr(i, nl - i);
    bool allWhite = true;
    for (char c : line) if (c != ' ' && c != '\t') { allWhite = false; break; }
    bool emptyLine = line.empty();
    // A non-empty whitespace-only line (e.g. " " or "\t") is content that the
    // decoder folds into a blank line, so it cannot round-trip here. Only a
    // truly empty line can represent a blank line.
    if (allWhite && !emptyLine) return false;
    if (!emptyLine) {
      // Leading whitespace on a content line is unsafe: it is absorbed into
      // the base indent (a leading space) or rejected by the parser (a tab in
      // the indentation margin). Fall back to a quoted string.
      if (line[0] == ' ' || line[0] == '\t' || line[0] == '\r')
        return false;
      seenContent = true;
    }
    if (nl == std::string_view::npos) break;
    i = nl + 1;
  }
  return seenContent;
}

// A string can be written as a folded block scalar (>) only when folding is
// lossless: folding turns internal newlines into spaces and collapses blank
// lines into paragraph breaks, so the only string that round-trips is a single
// content line with exactly one trailing newline. Anything else would change
// the value, so those strings fall back to '|' or a quoted string.
inline bool adeCanFoldEncode(std::string_view s) {
  if (s.size() < 2 || s.back() != '\n') return false;
  std::string_view body = s.substr(0, s.size() - 1);
  if (body.find('\n') != std::string_view::npos) return false;
  if (body.find('\r') != std::string_view::npos) return false;
  char c0 = body[0];
  if (c0 == ' ' || c0 == '\t') return false; // absorbed into the base indent
  return true;
}

// Write a string as a block scalar. '|' keeps no trailing newline; '|+' keeps
// every one, so any number of trailing newlines round-trips exactly. Strings
// that cannot round-trip as a block scalar are written as quoted strings by
// the caller instead.
inline void adeWriteBlockScalar(std::ostream& os, std::string_view s) {
  size_t k = 0;
  while (k < s.size() && s[s.size() - 1 - k] == '\n') k++;
  // The indicator follows the trailing newline count so every count
  // round-trips: none -> '|-', exactly one -> '|' (clip), several -> '|+'.
  os << (k == 0 ? "|-\n" : (k == 1 ? "|\n" : "|+\n"));
  std::string_view body = s.substr(0, s.size() - k);
  size_t i = 0;
  while (i < body.size()) {
    size_t eol = body.find('\n', i);
    bool lastLine = (eol == std::string_view::npos);
    if (lastLine) eol = body.size();
    os << "  " << body.substr(i, eol - i);
    // The final content line keeps its own line break whenever the value is
    // meant to end in a newline; under '|-' omitting it is what tells the
    // decoder the value ends without one.
    if (!lastLine || k > 0) os << '\n';
    i = eol + 1;
  }
  // The decoder keeps one break for the final content line itself, so only the
  // remaining k-1 newlines need a blank line each. Clip normalises extras away
  // and '|-' strips them, so a single newline needs none.
  for (size_t j = 1; j < k; j++) os << '\n';
}

// Write a string as a folded block scalar. Default (clip) chomping keeps
// exactly one trailing newline and the decoder adds that break back after
// folding, so the extra blank line YAML would need here is not necessary.
inline void adeWriteFoldedScalar(std::ostream& os, std::string_view s) {
  os << ">\n  " << s.substr(0, s.size() - 1) << "\n";
}

// Forward declaration
inline void adeWriteValue(std::ostream& os, const asvJSONValue* v, int indent, bool lineContext, bool strict);

// True when a node carries comments (or any descendant does): such nodes
// need multi-line output so the comments can be preserved verbatim.
inline bool adeHasComments(const asvJSONValue* v) {
  if (!v) return false;
  if (v->comments &&
      (!v->comments->leading.empty() || !v->comments->trailing.empty() ||
       !v->comments->closing.empty() || !v->comments->footing.empty()))
    return true;
  if (v->type == asvJSONValue::OBJECT && v->obj) {
    for (const auto& [k, c] : *v->obj) if (adeHasComments(c.get())) return true;
  } else if (v->type == asvJSONValue::ARRAY && v->arr) {
    for (const auto& c : *v->arr) if (adeHasComments(c.get())) return true;
  }
  return false;
}

inline void adeWriteCommentLines(std::ostream& os, const std::vector<Comment>& list, int indent) {
  for (const auto& c : list) {
    if (c.block) {
      auto nl = c.text.find('\n');
      if (nl != std::string_view::npos) {
        adeWriteIndent(os, indent);
        os << "/*\n";
        std::string_view remaining(c.text);
        while (!remaining.empty()) {
          auto lineEnd = remaining.find('\n');
          adeWriteIndent(os, indent);
          if (lineEnd != std::string_view::npos) {
            os << remaining.substr(0, lineEnd + 1);
            remaining.remove_prefix(lineEnd + 1);
          } else {
            os << remaining << '\n';
            remaining = {};
          }
        }
        adeWriteIndent(os, indent);
        os << "*/\n";
      } else {
        adeWriteIndent(os, indent);
        os << "/* " << c.text << " */\n";
      }
    } else {
      adeWriteIndent(os, indent);
      os << "//" << c.text << '\n';
    }
  }
}

// Appends a trailing comment after a value; returns true when the comment
// was terminated with a newline (a `#`-style comment swallows anything on the
// same line, so ADE separators/closers must start on a fresh line).
inline bool adeWriteTrailing(std::ostream& os, const asvJSONValue* v) {
  if (!v->comments || v->comments->trailing.empty()) return false;
  for (const auto& c : v->comments->trailing) {
    if (c.block) {
      os << " /* " << c.text << " */";
    } else {
      os << " //" << c.text;
    }
  }
  const auto& last = v->comments->trailing.back();
  bool needNl = !last.block;
  if (needNl) os << '\n';
  return needNl;
}

// Multi-line inline object ("key: { ... }") that preserves comments on
// fields, closing comments before '}', and the object's own trailing.
inline void adeWriteMultilineObject(std::ostream& os, const asvJSONValue* v, int indent, bool strict) {
  os << "{\n";
  for (const auto& [k, sub] : *v->obj) {
    if (sub->comments) adeWriteCommentLines(os, sub->comments->leading, indent);
    adeWriteIndent(os, indent);
    adeWriteKey(os, k, strict);
    os << ": ";
    std::ostringstream vbuf;
    adeWriteValue(vbuf, sub.get(), indent, true, strict);
    os << vbuf.str();
    os << ',';
    bool nl = adeWriteTrailing(os, sub.get());
    if (!nl) os << '\n';
  }
  if (v->comments && !v->comments->closing.empty()) {
    adeWriteCommentLines(os, v->comments->closing, indent);
  }
  adeWriteIndent(os, indent > 0 ? indent - 1 : 0);
  os << '}';
}

// Multi-line array ("[\n ... \n]") that preserves comments on elements
// and closing comments before ']'.
inline void adeWriteMultilineArray(std::ostream& os, const asvJSONValue* v, int indent, bool strict) {
  os << "[\n";
  for (size_t i = 0; i < v->arr->size(); i++) {
    const auto& el = v->arr->at(i);
    if (el->comments) adeWriteCommentLines(os, el->comments->leading, indent);
    adeWriteIndent(os, indent);
    std::ostringstream vbuf;
    adeWriteValue(vbuf, el.get(), indent, true, strict);
    os << vbuf.str();
    os << ',';
    bool nl = adeWriteTrailing(os, el.get());
    if (!nl) os << '\n';
  }
  if (v->comments && !v->comments->closing.empty()) {
    adeWriteCommentLines(os, v->comments->closing, indent);
  }
  adeWriteIndent(os, indent > 0 ? indent - 1 : 0);
  os << ']';
}

inline void adeWriteObject(std::ostream& os, const asvJSONValue* v, int indent, bool strict) {
  if (!asvJSONValue::checkNestingDepth(indent))
    throw asvJSONError("ADE: maximum nesting depth exceeded");
  if (indent == 0) {
    bool first = true;
    for (const auto& [k, sub] : *v->obj) {
      if (!first) os << '\n';
      first = false;
      adeWriteKey(os, k, strict);
      os << ": ";
      adeWriteValue(os, sub.get(), 1, true, strict);
    }
  } else if (adeHasComments(v)) {
    adeWriteMultilineObject(os, v, indent, strict);
  } else {
    os << '{';
    bool first = true;
    for (const auto& [k, sub] : *v->obj) {
      if (!first) os << ", ";
      first = false;
      adeWriteKey(os, k, strict);
      os << ": ";
      adeWriteValue(os, sub.get(), indent + 1, false, strict);
    }
    os << '}';
  }
}

inline void adeWriteArray(std::ostream& os, const asvJSONValue* v, int indent, bool multiline, bool strict) {
  if (!asvJSONValue::checkNestingDepth(indent))
    throw asvJSONError("ADE: maximum nesting depth exceeded");
  if (!v->arr || v->arr->empty()) { os << "[]"; return; }
  bool allObjs = true;
  for (const auto& e : *v->arr) if (e->type != asvJSONValue::OBJECT) { allObjs = false; break; }
  if ((allObjs && multiline) || adeHasComments(v)) {
    if (adeHasComments(v)) { adeWriteMultilineArray(os, v, indent, strict); return; }
    os << "[\n";
    for (size_t i = 0; i < v->arr->size(); i++) {
      adeWriteIndent(os, indent);
      adeWriteObject(os, v->arr->at(i).get(), indent + 1, strict);
      if (i + 1 < v->arr->size()) os << ',';
      os << '\n';
    }
    adeWriteIndent(os, indent > 0 ? indent - 1 : 0);
    os << ']';
  } else {
    os << '[';
    for (size_t i = 0; i < v->arr->size(); i++) {
      if (i) os << ", ";
      adeWriteValue(os, v->arr->at(i).get(), indent + 1, false, strict);
    }
    os << ']';
  }
}

inline void adeWriteValue(std::ostream& os, const asvJSONValue* v, int indent, bool lineContext, bool strict) {
  using T = asvJSONValue::Type;
  if (!v) { os << "null"; return; }
  switch (v->type) {
    case T::OBJECT:
      if (v->obj) adeWriteObject(os, v, indent, strict);
      else os << "{}";
      break;
    case T::ARRAY:
      adeWriteArray(os, v, indent, lineContext, strict);
      break;
    case T::STRING: {
      if (v->raw_number) {
        // Exact numeric literal.  Emit bare only when the decoder re-parses it
        // as a raw_number (e.g. big integers that overflow int64, or hex/oct/binary
        // overflow).  High-precision decimals (>MAX_DOUBLE_SIG_DIGITS sig digits)
        // would be re-parsed as plain doubles, losing the STRING type, so quote
        // them to preserve fidelity.
        auto probe = adeTryParseNumber(v->str_data);
        if (probe && probe->raw_number) {
          os << v->str_data;
        } else {
          adeWriteQuoted(os, v->str_data);
        }
        break;
      }
      // Block/folded scalars (|, >) carry unquoted content, so strict mode
      // never emits them: a strict document must be fully quotable, and a
      // multiline string is written as a quoted (JSON-escaped) string instead.
      if (lineContext && !strict && adeCanFoldEncode(v->str_data)) {
        adeWriteFoldedScalar(os, v->str_data);
      } else if (lineContext && !strict && adeCanBlockEncode(v->str_data)) {
        adeWriteBlockScalar(os, v->str_data);
      } else if (adeStringNeedsQuote(v->str_data, strict)) {
        adeWriteQuoted(os, v->str_data);
      } else {
        os << v->str_data;
      }
      break;
    }
    case T::INT: {
      char buf[32];
      auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
      if (ec == std::errc()) os.write(buf, ptr - buf);
      else os << '0';
      break;
    }
    case T::DOUBLE: {
      std::string tmp;
      if (std::isnan(v->dbl)) { os << (std::signbit(v->dbl) ? "-.nan" : ".nan"); break; }
      if (std::isinf(v->dbl)) { os << (v->dbl > 0 ? ".inf" : "-.inf"); break; }
      fmtDoubleVal(v->dbl, tmp);
      // A double with an integral value must round-trip as a float, so append
      // ".0" when fmtDoubleVal produced a plain integer literal.
      if (!tmp.empty() && tmp.find('.') == std::string::npos && tmp.find('e') == std::string::npos && tmp.find('E') == std::string::npos)
        tmp += ".0";
      os << tmp;
      break;
    }
    case T::BOOL_VAL: os << (v->flag ? "true" : "false"); break;
    case T::NULL_VAL: os << "null"; break;
    case T::DATETIME: {
      std::string dt;
      fmtDateTimeVal(v->timestamp, v->datetime_ms, dt, v->tz_offset_min);
      os << "!datetime \"" << dt << '"';
      break;
    }
    case T::BINARY:
      os << "!base64 \"" << encodeBase64(v->bin_data.data(), v->bin_data.size()) << '"';
      break;
    case T::OBJECTID: {
      std::string hex;
      fmtObjectIdHexVal(v->str_data, hex);
      os << "!objectid " << hex;
      break;
    }
    case T::REGEX: {
      // Emit !regex "__REGEX__<pattern>|<opts>|" with any literal '|' inside the
      // pattern or the options written as \u007C, so raw '|' characters appear
      // ONLY as the pattern/options boundary and the mandatory trailing
      // terminator. The terminator is what disambiguates an options-free
      // pattern that itself ends in '|', and the \u007C escapes keep a pipe
      // inside a field from being read as a separator. The __REGEX__ marker
      // keeps this canonical form distinct from the historical "decoded text,
      // split at the last '|'" form, which the decoder still accepts.
      // The decoder splits the RAW token text (before unescaping), mirroring
      // the TRON/GOON convention.
      size_t sep = v->str_data.rfind('\0');
      std::string_view pat = (sep != std::string_view::npos)
          ? std::string_view(v->str_data.data(), sep) : std::string_view(v->str_data);
      std::string_view opts = (sep != std::string_view::npos && sep + 1 < v->str_data.size())
          ? std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1)
          : std::string_view();
      os << "!regex \"" << kAdeRegexMarker;
      adeEscPipe(os, pat);
      os << '|';
      adeEscPipe(os, opts);
      os << '|';
      os << '"';
      break;
    }
    case T::CUSTOM_TAG: {
      // str_data holds the tag name; custom_value holds the value. Unknown
      // tags are emitted in the canonical "!<name> <value>" form so that a
      // name the parser would not accept back still round-trips exactly.
      // adeIsSimpleTagName mirrors the parser's TAG validation exactly, so a
      // name accepted here is guaranteed to be accepted on the way back. That is
      // necessary but not sufficient: a name that IS a simple tag name can still
      // collide with a built-in, and the parser dispatches on the name alone, so
      // "!regex abc" came back as a REGEX and the custom tag was lost. The
      // verbatim !<name> form suppresses that dispatch, so a built-in name is
      // written verbatim too.
      bool verbatim = !adeIsSimpleTagName(v->str_data) || adeIsBuiltinTagName(v->str_data);
      // The verbatim !<name> form scans up to the FIRST '>', so a name that
      // contains one cannot be spelled back: the parser would stop early and
      // read the remainder as the value (a name "a>b" emitted as "!<a>b>" came
      // back as name "a"). Neither spelling can represent it, so reject it
      // rather than emit a document we cannot read back. A name built through
      // the public API can hold a '>'; the parser never accepts one.
      if (v->str_data.find('>') != std::string::npos)
        throw asvJSONError("ADE: CUSTOM_TAG name must not contain '>'");
      if (verbatim) os << "!<" << v->str_data << "> ";
      else os << '!' << v->str_data << ' ';
      if (v->custom_value) adeWriteValue(os, v->custom_value.get(), indent + 1, true, strict);
      else os << "null";
      break;
    }
    case T::TIMESTAMP: {
      char buf[32];
      auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
      os << "!timestamp ";
      if (ec == std::errc()) os.write(buf, ptr - buf);
      else os << '0';
      break;
    }
    case T::EXTENSION: {
      os << "!ext \"" << static_cast<int>(v->ext_type) << ':' << encodeBase64(v->bin_data.data(), v->bin_data.size()) << '"';
      break;
    }
    default:
      os << "null";
  }
}

inline void serializeADE(std::ostream& os, const asvJSONValue& val, int indent, bool strict) {
  if (val.type == asvJSONValue::OBJECT && val.obj && indent == 0) {
    if (val.comments) adeWriteCommentLines(os, val.comments->leading, 0);
    bool first = true;
    bool prevEndsNl = false;
    for (const auto& [k, sub] : *val.obj) {
      // Separate top-level entries with exactly one newline. A block scalar
      // (| / >) already ends with a newline, so emitting an extra separator
      // would create a blank line that the scalar swallows as content, adding
      // a spurious trailing newline on round-trip.
      if (!first && !prevEndsNl) os << '\n';
      first = false;
      if (sub->comments) adeWriteCommentLines(os, sub->comments->leading, 0);
      adeWriteKey(os, k, strict);
      os << ": ";
      std::ostringstream vbuf;
      adeWriteValue(vbuf, sub.get(), 1, true, strict);
      std::string vs = vbuf.str();
      os << vs;
      // Ask the writer whether the stream now ends on a fresh line, rather than
      // deriving it from the value text alone. A BLOCK trailing comment is
      // deliberately left unterminated (it must be, or a following '//' comment
      // would be swallowed), so the line is still open even when the value was a
      // block scalar that ended with a newline. The old `if (tNl) prevEndsNl =
      // true` could only raise the flag, never lower it, which produced
      // " /* note */b: 2" -- the next key glued onto the comment. hasTrailing
      // separates "no trailing comment" (fall back to the value text) from
      // "block trailing" (mid-line), which the return value alone cannot.
      bool tNl = adeWriteTrailing(os, sub.get());
      bool hasTrailing = sub->comments && !sub->comments->trailing.empty();
      prevEndsNl = hasTrailing ? tNl : (!vs.empty() && vs.back() == '\n');
    }
    if (val.comments) {
      if (!val.comments->closing.empty()) {
        if (!prevEndsNl) os << '\n';
        adeWriteCommentLines(os, val.comments->closing, 0);
        prevEndsNl = true;
      }
      if (!val.comments->footing.empty()) {
        if (!prevEndsNl) os << '\n';
        adeWriteCommentLines(os, val.comments->footing, 0);
      }
    }
  } else {
    if (val.comments) adeWriteCommentLines(os, val.comments->leading, 0);
    adeWriteValue(os, &val, indent, false, strict);
    os << '\n';
    if (val.comments && !val.comments->footing.empty())
      adeWriteCommentLines(os, val.comments->footing, 0);
  }
}

// ====================== asvJSON member methods ======================

inline std::string asvJSON::toADE(bool strict) const {
  std::ostringstream os;
  if (root) asvJSONInternal::serializeADE(os, *root, 0, strict);
  return os.str();
}

inline bool asvJSON::fromADE(std::string_view input, bool strict) {
  dirty_ = false;
  formatType = FormatType::Unknown;
  root = nullptr;
  try {
    auto parsed = asvJSONInternal::parseADE(std::string(input), strict);
    if (!parsed) {
      lastError = "ADE: failed to parse input";
      return false;
    }
    root = std::move(parsed);
    formatType = FormatType::ADE;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  } catch (const std::exception& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
