#pragma once
// INI serialization/parsing for asvJSON++
// Conforms to the standard INI format with dot-notation section nesting.
//
// Key escaping. Only [A-Za-z0-9_-] are emitted bare. '.' and '\' use a
// backslash form; every other character (the '=' and ':' delimiters, ';', '#',
// spaces, tabs, CR, LF and any other control byte) is emitted as a \xNN hex
// escape. See iniEscapeKey for why a backslash is insufficient for those.
// The decoder still accepts the historical unescaped and "\[" / "\]" forms,
// so previously written documents keep parsing.
//
// KNOWN LIMITATIONS (inherent to the format, not encoder defects):
//   - A non-OBJECT root cannot survive as a root, because fromINI always
//     builds an object. A scalar root is wrapped under the key "value"; a
//     non-empty array root is written as repeated "value = ..." lines
//     (primitives) or [[value]] tables (objects), so no element is lost, but
//     the document gains that one key on the way back in.
//   - A comment whose text contains a newline is written as one ';'-prefixed
//     line per text line, so it re-parses as several Comment objects rather
//     than one. INI has no block comment and no way to mark the lines as one
//     logical comment without inventing an escape that would collide with
//     comment text legitimately containing a backslash.
//   - Only a SINGLE-LINE trailing comment can stay on the value's line. A
//     block or multiline one is written as ';' lines BELOW it, and ';' lines
//     carry no marker saying which node they belong to: on re-parse they
//     become the LEADING comments of the next node written (the next key or
//     section header), or the root's footing if the value ends the document.
//     There is no INI syntax for a comment that both starts on a value's line
//     and continues past it, nor for one that ends that value's node. The text
//     is never dropped, only its position and node change, so this applies to
//     every value, not just roots.
//   - Several single-line trailing comments on ONE value are written as
//     " ;c1 ;c2 ;c3", but the decoder reads everything after the first ';' as
//     a single comment, so they re-parse as ONE Comment whose text is
//     "c1 ;c2 ;c3". Re-emitting that is byte-identical, so it is stable from
//     then on, but the separate-comment structure does not survive. One line
//     holds at most one inline comment.
//   - Leading comments on the root object are written above the first key and
//     re-parse as leading comments on THAT key. INI has no notion of a comment
//     that precedes the document itself. A non-object root (scalar or array,
//     which the decoder always rebuilds as an object) keeps its own comments:
//     leading above the wrapped block, trailing after the wrapped value,
//     footing after it -- except for an object-array root, which has no line
//     of its own (only its [[value]] elements do), so its trailing comment is
//     API-only state, mirroring a nested object that emits no header.
//   - footing is only emitted for the root, matching the decoder: trailing
//     comments at EOF are always attributed to the root, so a nested object's
//     footing is an API-only state that does not survive a round trip.
//   - A repeated key inside one section decodes to an array, so writing
//     {k:1, k:2} and reading it back yields {k:[1,2]}. Comments collected for
//     both occurrences then attach to that array node: the leading ones are
//     emitted above its first element, the trailing ones after its LAST. A
//     trailing comment therefore cannot sit on an element line of its own --
//     every occurrence of the repeated key would re-collect it -- which is why
//     it goes after the final line, where the collector still resolves it to
//     the array node.
//   - An object holding only sub-objects emits no [prefix] header of its own,
//     so its leading comments are written above the first section this level
//     produces (the text is never dropped). On re-parse they bind to that
//     section, not to the object they came from: INI cannot express a comment
//     on a node that has no line of its own. The same applies to a TRAILING
//     comment on such an object: there is no line of its own to hang it on.
//   - \xNN is understood on decode, so a value whose TEXT starts with "\x" +
//     two hex digits is rewritten as "\\xNN" to survive a round trip. Escapes
//     and unescapes are otherwise exact inverses (\n \t \r \0 \a \b \' \\ \; \#
//     \= \: plus \xNN for every other control byte), and a value is quoted
//     whenever it holds a character with an escape, so every escape the encoder
//     writes is reachable. No control byte (C0 range or DEL) is ever written
//     raw: it would round-trip, but it truncates the line in any external INI
//     reader or editor.
//   - Keys may hold ANY byte: iniEscapeKey hex-escapes everything outside
//     [A-Za-z0-9_.-] and iniUnescapeSectionKey restores it, so a key can
//     contain NUL, 0x1F or a high byte. The comment collector's path segments
//     are length-prefixed for exactly that reason (see iniPathSeg).
//   - A key that is EMPTY, or whose section path would be empty, has no INI
//     spelling: "" is skipped by the decoder on a " = v" line and "[]" is
//     rejected as a section header. The encoder therefore throws for both, so
//     {"": {...}} (whose section header would be "[]") is an error, never a
//     silently collapsed {"a":1} in the output. A key STARTING with '[' needs
//     no rejection: iniEscapeKey hex-escapes the '[' to "\x5B", so the emitted
//     line never begins with '[' at all (a bare "[k] = v" line would be read as
//     a section header -- the encoder never writes one) and the decoder restores
//     the byte. Section-header, dotted and flat keys all round-trip bracketed
//     keys today.
//   - Special value types have no native INI value syntax, so they degrade to
//     text like the other text formats (CSV, dotenv, Java Properties):
//       DATETIME    - written as bare ISO-8601 text (fmtDateTimeVal's 'T'
//                     separator needs no quotes). This is the ONLY special type
//                     with a native INI form: iniParseVal detects the exact ISO
//                     text back as a DATETIME, so the instant, milliseconds and
//                     tz offset round-trip. A bare HAND-WRITTEN ISO value is
//                     typed as a DATETIME too; a plain STRING that merely looks
//                     like one is QUOTED by the encoder (the STRING case in
//                     iniFormatVal re-parse the text, sees the DATETIME, and
//                     quotes), so it stays a STRING. A quoted "..." value is
//                     never re-typed. A timestamp gmtimeSafe cannot convert
//                     renders the literal "null", quoted, so it re-parses as the
//                     STRING "null", not the null value.
//       TIMESTAMP   - bare epoch seconds; re-parses as an INT of the same
//                     number (type identity lost).
//       BINARY / EXTENSION - the payload as a QUOTED BASE64 string; type lost,
//                     the value survives as text.
//       OBJECTID    - the 12 bytes as a QUOTED 24-hex string.
//       REGEX       - the pattern text only (the options after the NUL are
//                     dropped, like CSV/bencode/plist), QUOTED.
//       CUSTOM_TAG  - the {"$customTag":{"name":...,"value":...}} Extended-JSON
//                     convention text (serialize()), QUOTED, so both the tag
//                     name and its value survive as text.

#include "../core.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace asvJSONInternal {

// -- Helpers ----------------------------------------------------------------

inline std::string_view iniTrim(std::string_view s) {
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// Find first unescaped ; or # in a bare value (pos of inline comment, or npos)
inline size_t iniFindComment(std::string_view s) {
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\') { i++; continue; }
    if (s[i] == ';' || s[i] == '#') return i;
  }
  return std::string_view::npos;
}

inline int iniHexVal(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Unescape INI value
inline std::string iniUnescape(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      // \xNN first, like iniUnescapeSectionKey: the generic rule below would
      // otherwise decode "\x41" to 'x' and leave "41" behind. This is the
      // inverse of the hex escape emitted for keys and of a hand-written value.
      if (s[i + 1] == 'x' && i + 3 < s.size() &&
          iniHexVal(s[i + 2]) >= 0 && iniHexVal(s[i + 3]) >= 0) {
        out += (char)((iniHexVal(s[i + 2]) << 4) | iniHexVal(s[i + 3]));
        i += 3;
        continue;
      }
      switch (s[i + 1]) {
        case 'n': out += '\n'; i++; break;
        case 't': out += '\t'; i++; break;
        case 'r': out += '\r'; i++; break;
        case '0': out += '\0'; i++; break;
        case 'a': out += '\a'; i++; break;
        case 'b': out += '\b'; i++; break;
        case '\\': out += '\\'; i++; break;
        case '\"': out += '\"'; i++; break;
        case '\'': out += '\''; i++; break;
        case ';': out += ';'; i++; break;
        case '#': out += '#'; i++; break;
        case '=': out += '='; i++; break;
        case ':': out += ':'; i++; break;
        default: out += s[i]; break;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

// Escape an INI value for output. The table is the exact inverse of
// iniUnescape, and every remaining control byte (C0 and DEL) is written as a
// \xNN hex escape, which iniUnescape decodes back -- so no control byte is ever
// written raw. That matters for portability, not round-tripping: an escaped NUL
// or 0x01 survives this library's string_view-based parsing either way, but any
// external INI reader or editor truncates the line at a raw control byte.
// needsQuotes() forces the value through this function, so a bare value can
// never carry one. The quote forms matter for symmetry with needsQuotes(): a
// value holding a lone apostrophe is wrapped in double quotes, and a value
// holding a double quote is wrapped or escaped accordingly, so an unescaped \'
// / \" in the output would be a hole in the table.
inline std::string iniEscape(std::string_view s) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (auto c : s) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case '\0': out += "\\0"; break;
      case '\a': out += "\\a"; break;
      case '\b': out += "\\b"; break;
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\'': out += "\\'"; break;
      case ';': out += "\\;"; break;
      case '#': out += "\\#"; break;
      case '=': out += "\\="; break;
      case ':': out += "\\:"; break;
      default:
        if ((unsigned char)c < 0x20 || (unsigned char)c == 0x7F) {
          out += "\\x";
          out += kHex[(unsigned char)c >> 4];
          out += kHex[(unsigned char)c & 0x0F];
        } else {
          out += c;
        }
    }
  }
  return out;
}

// Characters that force a value to be quoted (and therefore run through
// iniEscape). Kept in one place so the encoder and its comment stay in step:
// needsQuotes() decides this, iniEscape() implements the escapes. A control
// byte in the C0 range or DEL counts, because a bare one reaches the file
// verbatim (this is the whole point of quoting it) and external readers truncate
// the line there.
inline bool iniNeedsQuotes(std::string_view sv) {
  for (auto c : sv) {
    if (c == ' ' || c == '\t' || c == ';' || c == '#' || c == '=' || c == ':' ||
        c == '"' || c == '\'' || c == '\\' || c == '\n' || c == '\r' ||
        c == '\0' || c == '\a' || c == '\b' ||
        (unsigned char)c < 0x20 || (unsigned char)c == 0x7F)
      return true;
  }
  return false;
}

// Escape one key for INI output. Only [A-Za-z0-9_-] pass through untouched.
//
// '.' and '\' keep a backslash form because iniSplitDottedPath uses a
// backslash to suppress the dot split, and iniUnescapeSectionKey consumes a
// backslash generically. Everything else is emitted as a \xNN hex escape,
// because a backslash is NOT enough for those three separate reasons:
//
//   - '=' and ':' are located by the delimiter scan BEFORE any unescaping, so
//     a leading '\' cannot hide them ("\= = 1" still splits at the '='). A
//     hex escape is not '=' text, so the scan skips over it.
//   - iniTrim runs BEFORE unescaping, so a trailing backslash-escaped space
//     is still trimmed away ("a\ " -> "a\"). A hex escape ends in a hex digit,
//     so trim cannot reach the encoded character.
//   - iniUnescapeSectionKey maps '\' + ANY character to that character, so a
//     backslash-escaped '\t' would decode to 't' and silently drop the tab.
//
// Hex-escaping ';' and '#' as well keeps one uniform rule; only a comment
// whose marker is the FIRST character is recognized, so even the bare
// backslash form would have worked, but uniformity avoids special cases.
// Reading a hex escape takes precedence over the generic rule, so a key that
// literally contains the text "\x41" stays distinguishable: its backslash is
// doubled by the encoder and decodes to a single literal backslash.
inline std::string iniEscapeKey(std::string_view s) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out += (char)c;
    } else if (c == '.') {
      out += "\\.";
    } else if (c == '\\') {
      out += "\\\\";
    } else {
      out += "\\x";
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

// Escape a key for use inside a section header [...] or a dotted path. An EMPTY
// key is rejected: a "" segment in a section path or a dotted key would have no
// spelling any decoder can read (iniSplitDottedPath("") is not a valid header,
// and a [[]] table name is rejected as an empty section too). The decoder
// already rejects every "[]" form, so rejecting here keeps the encoder from
// ever emitting a headless section header and silently collapsing {"": {...}}
// into the merged content of whatever section follows.
inline std::string iniEscapeSectionKey(std::string_view s) {
  if (s.empty())
    throw asvJSONError("cannot represent empty key in INI"); // "[]" would be rejected by the decoder
  return iniEscapeKey(s);
}

// Unescape a key path segment read from a header or a dotted key. Handles the
// \xNN form emitted by iniEscapeKey plus the historical "a backslash escapes
// the next character" rule for '.', '\', '[', ']' and anything else.
inline std::string iniUnescapeSectionKey(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      // \xNN must be tested first: the generic rule below would otherwise
      // decode "\x41" to 'x' and leave "41" behind.
      if (s[i + 1] == 'x' && i + 3 < s.size() &&
          iniHexVal(s[i + 2]) >= 0 && iniHexVal(s[i + 3]) >= 0) {
        out += (char)((iniHexVal(s[i + 2]) << 4) | iniHexVal(s[i + 3]));
        i += 3;
        continue;
      }
      out += s[i + 1];
      i++;
    } else {
      out += s[i];
    }
  }
  return out;
}

// Escape a flat (key = value side) key. The one key with no INI spelling as a
// flat key is the EMPTY one (" = v" is skipped by the decoder), rejected here
// rather than at each of the call sites. A key STARTING with '[' used to be
// rejected too ("the decoder would read it as a section header"), but that was
// a false restriction: iniEscapeKey hex-escapes the '[' into "\x5B", so the
// emitted line never begins with '[' at all and iniUnescapeSectionKey restores
// the byte on decode. Every encode path (root flat key, dotted key, section
// header, [[...]] table) therefore round-trips a bracketed key today, exactly
// like any other non-alphanumeric byte.
inline std::string iniEscapeFlatKey(std::string_view s) {
  if (s.empty())
    throw asvJSONError("cannot represent empty key in INI"); // {"": ...} has no INI spelling
  return iniEscapeKey(s);
}

// Split a dotted path (section name or dotted key) on unescaped '.', trimming
// and unescaping each segment. A backslash escapes the following character, so
// "[a\.b]" and "a\.b = 1" both yield a single literal segment "a.b".
inline std::vector<std::string> iniSplitDottedPath(std::string_view s) {
  std::vector<std::string> out;
  size_t dotStart = 0;
  bool escaped = false;
  for (size_t i = 0; i <= s.size(); i++) {
    if (i == s.size() || (s[i] == '.' && !escaped)) {
      out.push_back(iniUnescapeSectionKey(iniTrim(s.substr(dotStart, i - dotStart))));
      dotStart = i + 1;
      escaped = false;
    } else {
      bool wasEscaped = escaped;
      escaped = (s[i] == '\\' && !wasEscaped);
    }
  }
  return out;
}

// Navigate or create nested path in the object tree
inline asvJSONValue* iniNavigate(asvJSONValue* root, const std::vector<std::string>& pathParts, bool create) {
  asvJSONValue* cur = root;
  for (const auto& seg : pathParts) {
    if (!cur || cur->type != asvJSONValue::OBJECT || !cur->obj) {
      if (!create) return nullptr;
      throw asvJSONError("cannot navigate through non-object");
    }
    auto it = cur->obj->find(seg);
    if (it == cur->obj->end()) {
      if (!create) return nullptr;
      auto newObj = asvJSONValue::makeObject();
      if (!newObj) throw asvJSONError("INI: out of memory");
      auto* raw = newObj.get();
      cur->obj->emplace(seg, std::move(newObj));
      cur = raw;
    } else {
      cur = it->second.get();
    }
  }
  // The final segment must also be an object: a section header (or dotted key)
  // targeting an existing scalar or array would otherwise silently drop every
  // following key-value pair.
  if (!cur || cur->type != asvJSONValue::OBJECT)
    throw asvJSONError("cannot navigate through non-object");
  return cur;
}

// Try to infer a typed value from a parsed INI string.
// Quoted values always remain strings; bare values may be null/bool/int/double.
inline std::unique_ptr<asvJSONValue> iniParseVal(const std::string& s, bool quoted);

// Format a value as INI text
inline void iniFormatVal(const asvJSONValue* v, std::string& out) {
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::STRING:
      {
        std::string_view sv(v->str_data);
        // Every character in the iniNeedsQuotes() set forces the quoted form:
        // iniEscape only runs on a QUOTED value, so a bare value is copied
        // verbatim and any escape sequence in it would survive as literal text.
        // That covers the delimiters, the comment markers and quotes, and
        // every control byte -- a raw NUL/0x01 truncates the line in any
        // external INI reader or editor.
        bool needsQuotes = iniNeedsQuotes(sv);
        // A bare text that would decode as a different type (bool/int/double/null)
        // must be quoted so the round-trip preserves the STRING type. raw_number
        // literals (e.g. overflowed integers) stay bare so they re-decode via the
        // overflow path. An empty string is quoted as "" (never a bare empty
        // fragment followed by a dangling space on the line).
        if (!needsQuotes && !v->raw_number) {
          if (sv.empty()) needsQuotes = true;
          else {
            auto bare = iniParseVal(std::string(sv), false);
            if (bare && bare->type != T::STRING) needsQuotes = true;
          }
        }
        if (needsQuotes) {
          out += '"';
          out += iniEscape(sv);
          out += '"';
        } else {
          out += sv;
        }
      }
      break;
    case T::INT:
      out += std::to_string(v->num);
      break;
    case T::DOUBLE:
      {
        if (std::isnan(v->dbl)) { out += "nan"; break; }
        if (std::isinf(v->dbl)) { out += (v->dbl < 0 ? "-inf" : "inf"); break; }
        // A double with an integral value must round-trip as a float
        // (otherwise "100000" would re-decode as an INT on compilers whose
        // fmtDoubleVal falls back to %g; to_chars already emits "1e+05" there).
        // Mirror the ADE convention of appending ".0" to a plain integer literal.
        std::string tmp;
        fmtDoubleVal(v->dbl, tmp);
        if (tmp.find('.') == std::string::npos && tmp.find('e') == std::string::npos &&
            tmp.find('E') == std::string::npos)
          tmp += ".0";
        out += tmp;
      }
      break;
    case T::BOOL_VAL:
      out += v->flag ? "true" : "false";
      break;
    case T::NULL_VAL:
      out += "null";
      break;
    case T::OBJECT:
      out += "{}";
      break;
    case T::ARRAY:
      out += "[]";
      break;
    case T::DATETIME:
      {
        // Bare ISO-8601 text: fmtDateTimeVal's 'T' separator means the value
        // contains no space, so it stays BARE (no quotes needed), and iniParseVal
        // detects the exact ISO text back as a DATETIME -- the instant,
        // milliseconds and tz offset all round-trip. A timestamp gmtimeSafe
        // cannot convert renders the literal "null" (same convention as the JSON
        // encoder); it is quoted so it re-parses as the STRING "null", not as the
        // null value.
        std::string dt;
        fmtDateTimeVal(v->timestamp, v->datetime_ms, dt, v->tz_offset_min);
        if (dt == "null") { out += '"'; out += "null"; out += '"'; }
        else out += dt;
      }
      break;
    case T::TIMESTAMP:
      // The epoch seconds survive as the same INT number on a re-parse; the type
      // identity is lost (mirrors CSV/Java Properties/UBJSON).
      out += std::to_string(v->num);
      break;
    case T::BINARY:
    case T::EXTENSION:
      {
        // The payload as quoted BASE64 text, like the other text formats; on a
        // re-parse it is a plain STRING (no "base64:" prefix convention exists
        // in INI).
        std::string b = encodeBase64(v->bin_data.data(), v->bin_data.size());
        out += '"';
        out += iniEscape(b);
        out += '"';
      }
      break;
    case T::OBJECTID:
      {
        // The 12 bytes as a quoted 24-hex string.
        std::string hex;
        fmtObjectIdHexVal(v->str_data, hex);
        out += '"';
        out += iniEscape(hex);
        out += '"';
      }
      break;
    case T::REGEX:
      {
        // Only the pattern text survives; the options after the NUL are dropped,
        // mirroring the CSV/bencode/plist encoders.
        size_t sep = v->str_data.rfind('\0');
        std::string_view pat = (sep == std::string_view::npos)
                                 ? std::string_view(v->str_data)
                                 : std::string_view(v->str_data.data(), sep);
        out += '"';
        out += iniEscape(pat);
        out += '"';
      }
      break;
    case T::CUSTOM_TAG:
      {
        // No native INI form: keep the tag name AND value as the same
        // Extended-JSON convention text serialize()/appendJsonToken emit, quoted
        // so it re-parses as a STRING holding that JSON.
        std::string payload;
        v->serialize(payload, false);
        out += '"';
        out += iniEscape(payload);
        out += '"';
      }
      break;
    default:
      // Defensive: every current core type is handled above, but a future type
      // must not silently produce `key = ` (an invalid INI line). Degrade to the
      // null literal like the other encoders' default branches.
      out += "null";
      break;
  }
}

// -- Null-node guards -------------------------------------------------------
// Every factory is a nothrow `new`: it returns nullptr both on OOM and (for
// makeString/makeStringView) when the payload exceeds MAX_STRING_LEN. The INI
// decoder builds the tree directly (it never round-trips through parse()), and
// its input length is not bounded anywhere else, so a bare/quoted value larger
// than MAX_STRING_LEN used to plant a null member: fromINI() returned true and
// the next serialize() dereferenced it (0xC0000005). These helpers enforce the
// module's "non-null or throw" contract, matching plist's checkStringLen and
// XML's xmlMakeString.
inline std::unique_ptr<asvJSONValue> iniNonNull(std::unique_ptr<asvJSONValue> v) {
  if (!v) throw asvJSONError("INI: out of memory");
  return v;
}

inline std::unique_ptr<asvJSONValue> iniMakeString(const std::string& s) {
  auto v = asvJSONValue::makeString(s.data(), s.size());
  if (!v) {
    if (s.size() > asvJSONValue::MAX_STRING_LEN)
      throw asvJSONError("INI: string too long");
    throw asvJSONError("INI: out of memory");
  }
  return v;
}

// Try to infer a typed value from a parsed INI string.
// Quoted values always remain strings; bare values may be null/bool/int/double.
inline std::unique_ptr<asvJSONValue> iniParseVal(const std::string& s, bool quoted) {
  if (quoted) {
    return iniMakeString(s);
  }
  // A BARE value in the exact ISO-8601 form fmtDateTimeVal() writes is a
  // DATETIME (the same convention as CSV/dotenv/Java Properties). The encoder's
  // STRING case in iniFormatVal QUOTES any plain STRING that merely looks like
  // one, and a quoted "..." value is never re-typed here, so only a bare
  // hand-written ISO value -- or a DATETIME node's own text -- becomes a
  // DATETIME on decode. tryParseDateTime is strict (whole string, seconds and
  // timezone mandatory), so it cannot misfire on a number or an ordinary word.
  {
    time_t ts = 0;
    int ms = 0, tz = 0;
    if (tryParseDateTime(s, ts, &ms, &tz))
      return iniNonNull(asvJSONValue::makeDateTime(ts, ms, tz));
  }
  // Bare "null" decodes to the null type (encoder emits the literal, and a
  // STRING "null" is quoted so it stays text -- see iniFormatVal).
  if (s == "null") return iniNonNull(asvJSONValue::makeNull());
  // Bare "{}" / "[]" represent empty containers (the encoder emits them for
  // object/array values that have no children).
  if (s == "{}") return iniNonNull(asvJSONValue::makeObject());
  if (s == "[]") return iniNonNull(asvJSONValue::makeArray());
  // Case-insensitive bool
  if (s == "true" || s == "True" || s == "TRUE") return iniNonNull(asvJSONValue::makeBool(true));
  if (s == "false" || s == "False" || s == "FALSE") return iniNonNull(asvJSONValue::makeBool(false));
  // Try int (base 10 -- no octal surprises)
  if (!s.empty()) {
    // JSON numbers never carry leading zeros ("042", "-042", "+042"), so those
    // literals stay STRINGs (otherwise "zip = 042" silently becomes 42 on a
    // re-parse). "0", "-0", "0.5", "-0.5" still parse as numbers.
    size_t i0 = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (s.size() > i0 + 1 && s[i0] == '0' && s[i0 + 1] >= '0' && s[i0 + 1] <= '9')
      return iniMakeString(s);
    // "0x"/"0X" literals are hex floats to strtod, not INI numbers. INI has no
    // hex syntax, so keep the literal as text (else "k = 0x10" silently
    // becomes 16 on a re-parse).
    if (s.size() > i0 + 1 && s[i0] == '0' && (s[i0 + 1] == 'x' || s[i0 + 1] == 'X'))
      return iniMakeString(s);
    errno = 0;
    char* end = nullptr;
    long long ll = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == 0 && end == s.c_str() + s.size()) {
      if (errno == ERANGE) {
        // Out-of-range integer: preserve the exact literal so the round-trip is
        // lossless (same convention as core JSON and ADE) instead of silently
        // clamping to LLONG_MAX.
        auto v = iniNonNull(asvJSONValue::makeStringView(std::string_view(s)));
        v->raw_number = true;
        return v;
      }
      return iniNonNull(asvJSONValue::makeInt(static_cast<int64_t>(ll)));
    }
    // Try double
    errno = 0;
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == 0 && end == s.c_str() + s.size()) {
      if (errno == ERANGE) {
        // Out-of-range double (e.g. "1e999"): preserve the literal as text
        // instead of silently degrading it to +inf/-inf/0 on the round-trip.
        return iniNonNull(asvJSONValue::makeStringView(std::string_view(s)));
      }
      return iniNonNull(asvJSONValue::makeDouble(d));
    }
  }
  return iniMakeString(s);
}

// Insert a value into an object by key. Repeated keys convert to arrays: the
// primitive-array encoder emits duplicates of the same key (one per element),
// so the decoder must merge them back into an array instead of overriding.
inline void iniSetValue(asvJSONValue* target, const std::string& key, std::unique_ptr<asvJSONValue> val) {
  if (!target || target->type != asvJSONValue::OBJECT || !target->obj) return;
  // A null value must never enter the tree: every serializer dereferences
  // members/elements without a null check.
  if (!val) throw asvJSONError("INI: out of memory");
  auto it = target->obj->find(key);
  if (it == target->obj->end()) {
    target->obj->emplace(key, std::move(val));
  } else {
    // Convert to array if not already
    if (it->second && it->second->type == asvJSONValue::ARRAY && it->second->arr) {
      it->second->arr->push_back(std::move(val));
    } else {
      // A null EXISTING node means an earlier insert already broke the
      // invariant (defensive: unreachable now that values are checked).
      if (!it->second) throw asvJSONError("INI: out of memory");
      auto arr = asvJSONValue::makeArray();
      if (!arr || !arr->arr) throw asvJSONError("INI: out of memory");
      arr->arr->push_back(std::move(it->second));
      arr->arr->push_back(std::move(val));
      it->second = std::move(arr);
    }
  }
}

// ----------------------------------------------------------------------------
// Comment collection (INI -> tree): comments are stored on the value nodes they
// annotate, so a JSON5/YAML/... tree carrying comments round-trips through INI
// and back. A comment-only line leads the following key / section; an inline
// `;`/`#` comment trails its value; comments at EOF become the root footing.
// ----------------------------------------------------------------------------

struct IniCommentEntry {
  std::string path;               // length-prefixed segment path ("" = root)
  std::vector<Comment> leading;   // comments before the annotated node
  std::vector<Comment> trailing;  // inline comment after its value
  bool footing = false;           // comments at EOF -> root footing
};

// Comment paths are built from DECODED key segments, and a decoded key may hold
// ANY byte: iniEscapeKey hex-escapes everything outside [A-Za-z0-9_.-] and
// iniUnescapeSectionKey restores it, so a key can contain NUL, 0x1F or a high
// byte verbatim. Segments are therefore LENGTH-PREFIXED ("<len>:<bytes>") rather
// than joined by a separator byte: with a raw \x1F separator a key like "a\x1Fb"
// was split into the three segments "base", "a", "b", iniFindPath() then walked
// root[base][a][b] and the comment was dropped silently. Length prefixes need no
// reserved byte at all, so every key survives byte for byte.
inline std::string iniPathSeg(std::string_view s) {
  return std::to_string(s.size()) + ":" + std::string(s);
}
inline std::string iniPathJoin(const std::string& base, std::string_view seg) {
  return base + iniPathSeg(seg);
}

inline asvJSONValue* iniFindPath(asvJSONValue* root, std::string_view path) {
  if (path.empty()) return root;
  asvJSONValue* cur = root;
  size_t pos = 0;
  while (pos < path.size()) {
    size_t colon = path.find(':', pos);
    if (colon == std::string_view::npos) return nullptr;
    size_t len = 0;
    for (size_t k = pos; k < colon; k++) {
      char d = path[k];
      if (d < '0' || d > '9' || len > (SIZE_MAX - 9) / 10) return nullptr;
      len = len * 10 + static_cast<size_t>(d - '0');
    }
    if (colon + 1 + len > path.size()) return nullptr;
    std::string_view seg = path.substr(colon + 1, len);
    // Dispatch on the node's TYPE, not on the segment's first byte: keys are
    // arbitrary text, so a key like "[abc]", "[0]" or "" under an OBJECT is a
    // plain map lookup. Sniffing seg[0] == '[' routed such keys into the array
    // branch, failed the index parse, returned nullptr, and silently dropped
    // the comment. An empty segment is reachable (iniSplitDottedPath("a.")
    // yields ["a", ""], which the decoder turns into {"a": {"": ...}}); here
    // it is just the empty OBJECT key, and the size check in the ARRAY branch
    // keeps it from ever reaching front(). The collector writes "[N]" only for
    // ARRAY children, so brackets are a reliable signal there.
    if (cur->type == asvJSONValue::ARRAY) {
      if (!cur->arr || seg.size() < 3 || seg.front() != '[' || seg.back() != ']') return nullptr;
      size_t idx = 0;
      for (size_t k = 1; k + 1 < seg.size(); k++) {
        char d = seg[k];
        if (d < '0' || d > '9') return nullptr;
        idx = idx * 10 + static_cast<size_t>(d - '0');
      }
      if (idx >= cur->arr->size()) return nullptr;
      cur = (*cur->arr)[idx].get();
    } else if (cur->type == asvJSONValue::OBJECT) {
      if (!cur->obj) return nullptr;
      auto it = (*cur->obj).find(seg);
      if (it == cur->obj->end()) return nullptr;
      cur = it->second.get();
    } else {
      return nullptr;
    }
    pos = colon + 1 + len;
  }
  return cur;
}

inline void iniAttachComments(asvJSONValue* root, const std::vector<IniCommentEntry>& entries) {
  if (!root || entries.empty()) return;
  auto appendAll = [](std::vector<Comment>& dst, const std::vector<Comment>& src) {
    for (const auto& c : src) dst.push_back(c);
  };
  for (const auto& e : entries) {
    asvJSONValue* node = iniFindPath(root, e.path);
    if (!node) continue;
    if (!node->comments) node->comments = std::make_unique<Comments>();
    if (e.footing) {
      if (!e.leading.empty()) appendAll(node->comments->footing, e.leading);
      if (!e.trailing.empty()) appendAll(node->comments->footing, e.trailing);
    } else {
      if (!e.leading.empty()) appendAll(node->comments->leading, e.leading);
      if (!e.trailing.empty()) appendAll(node->comments->trailing, e.trailing);
    }
  }
}

// Walk the INI document and collect comment entries. Mirrors fromINI's own
// line classification (continuation lines, quoted multi-line values, dotted
// keys, [[...]] elements) so comment paths resolve against the parsed tree.
inline std::vector<IniCommentEntry> iniCollectComments(std::string_view input) {
  std::vector<IniCommentEntry> entries;
  std::vector<Comment> pending;
  std::vector<std::string> sectionPath;  // decoded segments of the current section
  std::string curArrayElemPath;          // path of the current [[...]] element (e.g. items -> "[0]")
  std::string mlValuePath;               // path of the multi-line quoted value being scanned
  bool inSection = false, inArrayElement = false, inMlValue = false;
  std::vector<std::pair<std::string, size_t>> arrayCounts;  // array key path -> next element index

  auto joinSegs = [&](const std::vector<std::string>& segs) {
    std::string p;
    for (const auto& s : segs) p = iniPathJoin(p, s);
    return p;
  };
  auto consumeLeading = [&](std::string path) {
    if (pending.empty()) return;
    IniCommentEntry e;
    e.path = std::move(path);
    e.leading.swap(pending);
    entries.push_back(std::move(e));
  };

  auto lines = splitLines(input);
  size_t lineCount = lines.size();
  for (size_t li = 0; li < lineCount; li++) {
    std::string_view line = iniTrim(lines[li]);
    if (inMlValue) {
      // Continue scanning a multi-line quoted value for its closing quote. The
      // scan must be escape-aware like the decoder's (fromINI tracks backslash
      // escapes on these continuation lines, see its gather loop): a naive
      // line.find('"') cleared inMlValue on an ESCAPED '\"' inside the value, so
      // the rest of the value (including any ';'/ '#' line) was then classified
      // on its own, stolen into a pending comment, and re-attached to the wrong
      // node on the round-trip.
      bool escaped = false, closed = false;
      size_t qi = 0;
      for (size_t i = 0; i < line.size(); i++) {
        if (escaped) { escaped = false; continue; }
        if (line[i] == '\\') { escaped = true; continue; }
        if (line[i] == '"') { inMlValue = false; closed = true; qi = i; break; }
      }
      // A multi-line quoted value may carry an inline comment after its closing
      // quote on that same line (`k = "line1\nline2" ; comment`). The decoder's
      // gather loop keeps only the text before the quote and drops the rest, so
      // the collector must record the comment here as well -- skipping it left
      // the encoder's own trailing comment on any multi-line value silently
      // dropped on a toINI() -> fromINI() round-trip (the single-line case is
      // handled below). Bind it to the value's path, saved when the scan opened.
      if (closed) {
        size_t t = qi + 1;
        while (t < line.size() && (line[t] == ' ' || line[t] == '\t')) t++;
        if (t < line.size() && (line[t] == ';' || line[t] == '#')) {
          std::string_view rest = iniTrim(line.substr(t + 1));
          IniCommentEntry e;
          e.path = mlValuePath;
          Comment c;
          c.text.assign(rest.data(), rest.size());
          e.trailing.push_back(std::move(c));
          entries.push_back(std::move(e));
        }
      }
      continue;
    }
    if (line.empty()) continue;
    // Continuation: a line ending with '\' joins the next. Mirror fromINI and
    // classify the JOINED text as one logical line, exactly like the decoder
    // does. Skipping the classification of the first line only (the previous
    // behavior) left the continuation lines to be classified on their own, so a
    // ';' or '#' line inside a continuation became a pending comment and was
    // then attached as a leading comment to whatever key came next:
    //
    //   a = 1 (continued)
    //   ; part of the value, not a comment
    //   b = 2
    //
    // fromINI keeps "; part of the value..." in a's value, while the collector
    // handed it to b. Joining also lets a comment above the run bind to the key
    // the run defines, and keeps the inline-comment scan on the whole value.
    std::string joined;
    if (line.back() == '\\') {
      joined.assign(line.substr(0, line.size() - 1));      size_t j = li + 1;
      while (j < lineCount) {
        std::string_view nx = iniTrim(lines[j]);
        if (nx.empty()) { j++; continue; }
        if (nx.back() == '\\') {
          joined.append(nx.data(), nx.size() - 1);
          j++;
        } else {
          joined.append(nx.data(), nx.size());
          j++;
          break;
        }
      }
      // Every line up to and including j-1 is part of the logical line, so the
      // loop's own ++ must resume at j.
      li = j - 1;
      line = joined;
    }
    // Comment-only line.
    if (line[0] == ';' || line[0] == '#') {
      size_t start = 1;
      while (start < line.size() && (line[start] == ' ' || line[start] == '\t' || line[start] == '\r')) start++;
      Comment c;
      c.text.assign(line.data() + start, line.size() - start);
      pending.push_back(std::move(c));
      continue;
    }
    // Section header.
    if (line[0] == '[') {
      auto findCloseBracket = [&](size_t from) -> size_t {
        size_t i = from;
        while (i < line.size()) {
          if (line[i] == '\\' && i + 1 < line.size()) { i += 2; continue; }
          if (line[i] == ']') return i;
          i++;
        }
        return std::string_view::npos;
      };
      bool isArray = line.size() > 1 && line[1] == '[';
      size_t start = isArray ? 2 : 1;
      size_t end = findCloseBracket(start);
      if (end == std::string_view::npos || (isArray && (end + 1 >= line.size() || line[end + 1] != ']'))) {
        // Malformed header: the parser will reject it; nothing to collect.
        pending.clear();
        inSection = false; inArrayElement = false;
        continue;
      }
      std::string_view name = iniTrim(line.substr(start, end - start));
      if (name.empty()) { pending.clear(); continue; }
      std::vector<std::string> newPath = iniSplitDottedPath(name);
      if (isArray) {
        // "[[a.b]]" -> array key "b" under path "a"; element index increments
        // per repeated header.
        //
        // The collector must mirror the decoder's descent (see the array branch
        // of iniParseDocImpl, which calls descendArrays() after navigating into
        // each enclosing segment): when an enclosing path node is itself a
        // tracked array, a "[[a.b]]" header binds INSIDE that array's CURRENT
        // (last) element. E.g. after `[[items]]`, a following `[[items.sub]]`
        // belongs at items[0].sub[0] -- physical path items -> "[0]" -> sub -> "[0]",
        // NOT the flat items -> sub -> "[0]" the old code produced (which made
        // the collector's paths diverge from the decoder's tree). We therefore
        // build the physical base by walking the enclosing segments, appending a
        // "[<last element index>]" marker after every enclosing level that is
        // itself a tracked array.
        //
        // arrayCounts is keyed by the PHYSICAL path (markers included). Repeated
        // headers then correctly restart the element counter per enclosing
        // element: after `[[items]]` re-opens items as items[1], a fresh
        // items -> "[1]" -> sub counter is created, matching the decoder's
        // fresh b array. With no enclosing array the markers never appear, so
        // this is byte-identical to the prior flat behavior.
        std::string arrKey = newPath.back();
        newPath.pop_back();
        std::string physBase;   // physical base built so far (with [N] markers)
        for (const auto& seg : newPath) {
          std::string segPath = iniPathJoin(physBase, seg);
          // Is this enclosing level a tracked array? Then descend into its
          // current (last) element, mirroring the decoder's descendArrays().
          bool segIsArray = false;
          for (const auto& ac : arrayCounts) {
            if (ac.first == segPath) { segIsArray = true; break; }
          }
          if (segIsArray) {
            size_t lastIdx = 0;
            for (auto& ac : arrayCounts) {
              if (ac.first == segPath) { lastIdx = ac.second > 0 ? ac.second - 1 : 0; break; }
            }
            physBase = iniPathJoin(segPath, "[" + std::to_string(lastIdx) + "]");
          } else {
            physBase = segPath;
          }
        }
        std::string arrP = iniPathJoin(physBase, arrKey);
        size_t idx = 0;
        bool found = false;
        for (auto& ac : arrayCounts) {
          if (ac.first == arrP) { idx = ac.second++; found = true; break; }
        }
        if (!found) arrayCounts.push_back(std::make_pair(arrP, size_t(1)));
        curArrayElemPath = iniPathJoin(arrP, "[" + std::to_string(idx) + "]");
        consumeLeading(curArrayElemPath);
        inArrayElement = true;
        inSection = false;
        sectionPath = std::move(newPath);
      } else {
        std::string path = joinSegs(newPath);
        consumeLeading(path);
        sectionPath = std::move(newPath);
        inSection = true;
        inArrayElement = false;
        curArrayElemPath.clear();
      }
      inMlValue = false;
      continue;
    }
    // Key = value (or key: value, or key followed by whitespace).
    size_t delim = std::string_view::npos;
    for (size_t i = 0; i < line.size(); i++) {
      if (line[i] == '=' || line[i] == ':') { delim = i; break; }
    }
    if (delim == std::string_view::npos) {
      for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == ' ' || line[i] == '\t') { delim = i; break; }
      }
      if (delim == std::string_view::npos) continue;
    }
    std::string_view keyView = iniTrim(line.substr(0, delim));
    std::string_view valView = iniTrim(line.substr(delim + 1));
    if (keyView.empty()) continue;

    // Quoted values may span lines (fromINI gathers them); suppress the inline
    // comment scan while inside one.
    std::vector<Comment> trailing;
    if (!valView.empty() && valView.front() == '"') {
      // Find closing quote on this line.
      bool escaped = false, closed = false;
      size_t qi = 1;
      for (; qi < valView.size(); qi++) {
        if (escaped) { escaped = false; continue; }
        if (valView[qi] == '\\') { escaped = true; continue; }
        if (valView[qi] == '"') { closed = true; break; }
      }
      if (!closed) inMlValue = true;
      else {
        // A quoted value may carry an inline comment after the closing quote --
        // the decoder accepts exactly that (whitespace, then ';' or '#') and
        // strips it from the value, so the collector must record it as well.
        // Scanning only the bare-value case (as before) meant the encoder's own
        // trailing comment on any value that needs quotes was silently dropped
        // on a toINI() -> fromINI() round-trip.
        size_t t = qi + 1;
        while (t < valView.size() && (valView[t] == ' ' || valView[t] == '\t')) t++;
        if (t < valView.size() && (valView[t] == ';' || valView[t] == '#')) {
          std::string_view rest = iniTrim(valView.substr(t + 1));
          Comment c;
          c.text.assign(rest.data(), rest.size());
          trailing.push_back(std::move(c));
        }
      }
    } else {
      size_t cpos = iniFindComment(valView);
      if (cpos != std::string_view::npos) {
        std::string_view rest = valView.substr(cpos + 1);
        rest = iniTrim(rest);
        Comment c;
        c.text.assign(rest.data(), rest.size());
        trailing.push_back(std::move(c));
      }
    }

    // Resolve the value node path (section + dotted key / array element + key).
    std::string base = inArrayElement ? curArrayElemPath
                                      : (inSection ? joinSegs(sectionPath) : std::string());
    std::vector<std::string> keyPath = iniSplitDottedPath(keyView);
    std::string path = base;
    for (const auto& s : keyPath) path = iniPathJoin(path, s);

    // Remember the value's path for the closing-quote line: `path` is moved
    // into the entry below, and the multi-line scan (which resumes on the next
    // iterations) needs it to attach a comment written after the closing quote.
    if (inMlValue) mlValuePath = path;

    if (!pending.empty() || !trailing.empty()) {
      IniCommentEntry e;
      e.path = std::move(path);
      e.leading.swap(pending);
      e.trailing.swap(trailing);
      entries.push_back(std::move(e));
    }
    // NOTE: inMlValue is deliberately NOT cleared here. It was set above when
    // this line opened a quoted value that has no closing quote yet, and the
    // continuation branch at the top of the loop clears it on the line that
    // carries the closing quote. Clearing it unconditionally at the end of
    // this iteration cancelled the flag in the same breath it was raised, so
    // the continuation branch was dead code and every line of a multi-line
    // quoted value was classified as ordinary content -- a ';' or '#' line
    // inside the value landed in `pending` and was then attached to the NEXT
    // key as a leading comment, moving it out of the value it belonged to.
  }

  // Unconsumed comments at EOF become the root footing.
  if (!pending.empty()) {
    IniCommentEntry e;  // path "" -> root footing
    e.footing = true;
    e.leading.swap(pending);
    entries.push_back(std::move(e));
  }
  return entries;
}

// Encoder classification of one child value: returns true when the child is
// emitted as bare `key = value` lines (scalar, primitive array, empty
// container), false when it becomes a [[...]] table (array of objects) or a
// dotted section (non-empty object). Arrays that cannot be represented at all
// (mixed object/scalar contents, or nested array elements) throw.
inline bool iniIsBareChild(const asvJSONValue* v) {
  if (v->type == asvJSONValue::ARRAY) {
    // A null container (only reachable from a hand-built tree) is treated as
    // empty: falling through to `return true` used to hand it to
    // iniWriteRepeatedLines(), which dereferenced v->arr unconditionally.
    if (!v->arr || v->arr->empty()) return true;
    bool hasObj = false, hasPrim = false;
    for (const auto& e : *v->arr) {
      if (e->type == asvJSONValue::ARRAY)
        throw asvJSONError("nested array cannot be represented in INI");
      if (e->type == asvJSONValue::OBJECT) hasObj = true;
      else hasPrim = true;
    }
    if (hasObj && hasPrim)
      throw asvJSONError("mixed array cannot be represented in INI");
    return !hasObj; // all primitives -> repeated key = value lines
  }
  if (v->type == asvJSONValue::OBJECT)
    return !v->obj || v->obj->empty();
  return true;
}

// Recursively emit object contents for a given section path (prefix).
// prefix == current section path; emitHeader controls whether to emit a section header.

// Write comment groups as `; `-prefixed lines (INI has no block comments, so
// multi-line text is line-ified).
inline void iniWriteCommentLines(std::string& out, const std::vector<Comment>& list) {
  for (const auto& c : list) {
    std::string_view text(c.text);
    size_t start = 0;
    for (;;) {
      size_t nl = text.find('\n', start);
      std::string_view seg = text.substr(start, nl == std::string_view::npos ? text.size() - start : nl - start);
      out += "; ";
      out += seg;
      out += '\n';
      if (nl == std::string_view::npos) break;
      start = nl + 1;
    }
  }
}

// Emit a trailing comment group for a single value: a run of single-line
// comments is inlined as " ;..." suffixes, anything block/multiline opens a
// comment block below the line so no text is dropped.
//
// Neither shape round-trips to this node, and the header says why; the two
// failure modes, so the behaviour is not rediscovered by surprise:
//   - The ';' lines below the value carry no node marker, so they become the
//     leading comments of the next node written (root footing at EOF).
//   - Several " ;c1 ;c2" suffixes re-parse as ONE comment with text
//     "c1 ;c2", since everything after the first ';' is one comment. Stable
//     afterwards (re-emitting is byte-identical) but the split is lost.
inline void iniWriteTrailingComments(std::string& out, const std::vector<Comment>& trailing) {
  if (trailing.empty()) return;
  bool allSingle = true;
  for (const auto& c : trailing)
    if (c.block || c.text.find('\n') != std::string::npos) { allSingle = false; break; }
  if (allSingle) {
    for (const auto& c : trailing) {
      out += " ;";
      out += c.text;
    }
    out += '\n';
  } else {
    out += '\n';
    iniWriteCommentLines(out, trailing);
  }
}

// Emit a `key = value` line with the value's leading comments above it and a
// single-line trailing comment inlined after the value.
inline void iniWriteKeyValue(std::string& out, const std::string& keq, const asvJSONValue* v) {
  if (v->comments && !v->comments->leading.empty())
    iniWriteCommentLines(out, v->comments->leading);
  out += keq;
  out += " = ";
  iniFormatVal(v, out);
  if (v->comments && !v->comments->trailing.empty()) {
    // Emit EVERY trailing comment, not just the last one: multiple single-line
    // trailing comments each get an inline " ;" suffix (they merge into ONE
    // comment again on re-parse -- see iniWriteTrailingComments), and any
    // block/multiline trailing comment opens a comment-block that carries the
    // whole list (so nothing is silently dropped).
    iniWriteTrailingComments(out, v->comments->trailing);
  } else {
    out += '\n';
  }
}

// Emit a primitive array as repeated `key = value` lines. The array node's own
// leading comments go above the first line and its trailing comments after the
// LAST one: iniCollectComments records the inline comment of every occurrence
// against the single merged array node, so a trailing comment written after the
// final line comes back on that node, exactly as it does for a scalar. Leading
// goes above too (INI has no syntax to attach a comment to the array as a
// whole). Emitting BOTH here is what keeps every call site from having to
// remember the leading half -- one of them did not, and dropped the comment.
inline void iniWriteRepeatedLines(std::string& out, const std::string& keyEq, const asvJSONValue* v) {
  const std::vector<std::unique_ptr<asvJSONValue>>* arr = v->arr.get();
  if (!arr || arr->empty()) return;
  if (v->comments) iniWriteCommentLines(out, v->comments->leading);
  for (size_t i = 0; i < arr->size(); i++) {
    out += keyEq;
    out += " = ";
    iniFormatVal((*arr)[i].get(), out);
    if (i + 1 == arr->size() && v->comments && !v->comments->trailing.empty())
      iniWriteTrailingComments(out, v->comments->trailing);
    else
      out += '\n';
  }
}

inline void iniEmitElement(const asvJSONValue* elem, std::string& out, const std::string& prefix);
inline void iniEmitObject(const asvJSONValue* obj, std::string& out, const std::string& prefix,
                           bool emitHeader, bool pretty = true) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) return;

  // Check if this level has direct (bare-line-producing) keys
  bool hasDirectKeys = false;
  for (const auto& [key, val] : *obj->obj) {
    if (iniIsBareChild(val.get())) {
      hasDirectKeys = true; break;
    }
  }

  // Whether a `[prefix]` section header is emitted for THIS level. When it is,
  // the bare keys below it must be emitted FLAT (the header already pins the
  // dotted path), never as their own dotted prefix -- otherwise a compact
  // encode of {"a":{"b":{"c":1}}} produced `[a.b]` followed by `a.b.c = 1`,
  // which re-parses into a nested {"a":{"b":{"a":{"b":{"c":1}}}}} (the dotted
  // key re-applies the whole section path). See "compact dotted 2+" in the
  // module audit.
  // Compact mode never emits section headers -- only dotted flat keys (and
  // [[...]] table headers for object-arrays, produced by the toINI caller, not
  // here). Without the `pretty` guard, the Pass-2 recursion below would pass
  // `subHasDirect` as emitHeader=true and a depth>=2 compact encode of
  // {"a":{"b":{"c":42}}} would emit `[a.b]` + flat `c = 42` instead of the
  // required dotted `a.b.c = 42` (the "compact dotted 2+" audit case).
  bool headerEmitted = pretty && emitHeader && hasDirectKeys && !prefix.empty();

  // Leading comments must not depend on a section header being written for THIS
  // level. An intermediate object -- one holding only sub-objects -- has no
  // direct keys, so headerEmitted is false and the comments were dropped
  // entirely: {"a": {comment "note-a", {"b": {"c": 1}}}} encoded as
  //
  //   [a.b]
  //   c = 1
  //
  // with "note-a" gone, and on re-parse there is nowhere left to recover it from
  // (a comment above [a.b] binds to the path a.b, not to a). Write the level's
  // comments once, immediately before whatever this level emits first: the
  // header, the first bare key line, or the first nested section/table.
  bool wroteLeading = false;
  auto writeLeading = [&]() {
    if (wroteLeading) return;
    wroteLeading = true;
    if (obj->comments && !obj->comments->leading.empty())
      iniWriteCommentLines(out, obj->comments->leading);
  };

  if (headerEmitted) {
    if (!out.empty() && out.back() != '\n') out += '\n';
    writeLeading();
    out += '[' + prefix + "]\n";
  }

  // Pass 1: bare-line producers (scalars, primitive arrays, empty containers).
  // These MUST precede [[...]] table headers so the decoder does not assign
  // them to a currentArrayElement opened by a preceding table.
  for (const auto& [key, val] : *obj->obj) {
    std::string flatKey = iniEscapeFlatKey(key);
    std::string dottedKey = prefix.empty() ? flatKey : prefix + "." + flatKey;
    if (val->type == asvJSONValue::OBJECT) {
      if (val->obj && val->obj->empty()) {
        writeLeading();
        iniWriteKeyValue(out, (headerEmitted ? flatKey : dottedKey), val.get());
      }
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      if (val->arr->empty()) {
        writeLeading();
        iniWriteKeyValue(out, (headerEmitted ? flatKey : dottedKey), val.get());
      } else if (iniIsBareChild(val.get())) {
        writeLeading();
        iniWriteRepeatedLines(out, (headerEmitted ? flatKey : dottedKey), val.get());
      }
    } else {
      writeLeading();
      iniWriteKeyValue(out, (headerEmitted ? flatKey : dottedKey), val.get());
    }
  }

  // Pass 2: objects and object-arrays (produce section headers / [[...]] tables).
  for (const auto& [key, val] : *obj->obj) {
    if (val->type == asvJSONValue::OBJECT) {
      std::string escKey = iniEscapeSectionKey(key);
      std::string newPrefix = prefix.empty() ? escKey : prefix + "." + escKey;
      bool subHasDirect = false;
      if (val->obj) {
        for (const auto& [sk, sv] : *val->obj) {
          if (iniIsBareChild(sv.get())) { subHasDirect = true; break; }
        }
      }
      writeLeading();
      iniEmitObject(val.get(), out, newPrefix, subHasDirect || !val->obj || val->obj->empty(), pretty);
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      bool isObj = true;
      for (const auto& elem : *val->arr) {
        if (elem->type != asvJSONValue::OBJECT) { isObj = false; break; }
      }
      if (isObj && !val->arr->empty()) {
        std::string escKey = iniEscapeSectionKey(key);
        for (const auto& elem : *val->arr) {
          std::string newPrefix = prefix.empty() ? escKey : prefix + "." + escKey;
          if (!out.empty() && out.back() != '\n') out += '\n';
          writeLeading();
          if (elem->comments) iniWriteCommentLines(out, elem->comments->leading);
          out += "[[" + newPrefix + "]]\n";
          if (elem->type == asvJSONValue::OBJECT) iniEmitElement(elem.get(), out, newPrefix);
        }
      }
    }
  }
}

// Emit the contents of one array-of-objects element (under a [[...]] header).
// Direct scalar / primitive-array keys go bare within the current element;
// nested objects are flattened with dotted keys and nested arrays-of-objects
// reuse [[full.path]] headers. Section headers must NOT be used here -- a
// "[x]" header after "[[items]]" would resolve at document scope, not inside
// the current element (the old encoder emitted e.g. "[items.host]" which
// failed to re-parse). prefix is the escaped full path of the array itself.

// Flatten object descendants of an array element into dotted leaf keys; nested
// arrays-of-objects become [[prefix.dotted]] headers resolved back into the
// element by the array-aware decoder.
inline void iniEmitElementFlat(const asvJSONValue* obj, const std::string& dk, std::string& out, const std::string& prefix) {
  // Pass 1: bare-line producers
  for (const auto& [key, val] : *obj->obj) {
    std::string flatKey = iniEscapeFlatKey(key);
    std::string full = dk.empty() ? flatKey : dk + "." + flatKey;
    if (val->type == asvJSONValue::OBJECT) {
      if (val->obj && val->obj->empty()) {
        iniWriteKeyValue(out, full, val.get());
      }
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      if (val->arr->empty()) {
        iniWriteKeyValue(out, full, val.get());
      } else if (iniIsBareChild(val.get())) {
        // The array's own leading comments are emitted by the helper.
        iniWriteRepeatedLines(out, full, val.get());
      }
    } else {
      iniWriteKeyValue(out, full, val.get());
    }
  }
  // Pass 2: objects and object-arrays
  for (const auto& [key, val] : *obj->obj) {
    std::string flatKey = iniEscapeFlatKey(key);
    std::string full = dk.empty() ? flatKey : dk + "." + flatKey;
    if (val->type == asvJSONValue::OBJECT && val->obj) {
      iniEmitElementFlat(val.get(), full, out, prefix);
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      bool allObjects = true;
      for (const auto& e : *val->arr) if (e->type != asvJSONValue::OBJECT) { allObjects = false; break; }
      if (allObjects && !val->arr->empty()) {
        for (const auto& e : *val->arr) {
          std::string np = prefix.empty() ? full : prefix + "." + full;
          if (!out.empty() && out.back() != '\n') out += '\n';
          if (e->comments) iniWriteCommentLines(out, e->comments->leading);
          out += "[[" + np + "]]\n";
          iniEmitElement(e.get(), out, np);
        }
      }
    }
  }
}

inline void iniEmitElement(const asvJSONValue* elem, std::string& out, const std::string& prefix) {
  if (!elem || elem->type != asvJSONValue::OBJECT || !elem->obj) return;
  // Pass 1: bare-line producers
  for (const auto& [key, val] : *elem->obj) {
    std::string flatKey = iniEscapeFlatKey(key);
    if (val->type == asvJSONValue::OBJECT) {
      if (val->obj && val->obj->empty()) {
        iniWriteKeyValue(out, flatKey, val.get());
      }
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      if (val->arr->empty()) {
        iniWriteKeyValue(out, flatKey, val.get());
      } else if (iniIsBareChild(val.get())) {
        // The array's own leading comments are emitted by the helper.
        iniWriteRepeatedLines(out, flatKey, val.get());
      }
    } else {
      iniWriteKeyValue(out, flatKey, val.get());
    }
  }
  // Pass 2: objects and object-arrays
  for (const auto& [key, val] : *elem->obj) {
    std::string flatKey = iniEscapeFlatKey(key);
    if (val->type == asvJSONValue::OBJECT && val->obj) {
      iniEmitElementFlat(val.get(), flatKey, out, prefix);
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      bool allObjects = true;
      for (const auto& e : *val->arr) if (e->type != asvJSONValue::OBJECT) { allObjects = false; break; }
      if (allObjects && !val->arr->empty()) {
        for (const auto& e : *val->arr) {
          std::string np = prefix.empty() ? flatKey : prefix + "." + flatKey;
          if (!out.empty() && out.back() != '\n') out += '\n';
          if (e->comments) iniWriteCommentLines(out, e->comments->leading);
          out += "[[" + np + "]]\n";
          iniEmitElement(e.get(), out, np);
        }
      }
    }
  }
}

// -- Encoder ----------------------------------------------------------------

inline void asvJSONValue::toINI(std::string& out, bool pretty) const {
  using T = asvJSONValue::Type;
  if (type == T::OBJECT) {
    if (obj) {
      if (comments) iniWriteCommentLines(out, comments->leading);
      if (!pretty) {
        // Compact: dotted keys for scalars/objects, [[...]] headers for
        // arrays-of-objects.  Bare lines must precede tables.
        for (const auto& [key, val] : *obj) {
          std::string flatKey = iniEscapeFlatKey(key);
          if (val->type == T::OBJECT) {
            if (val->obj && val->obj->empty()) {
              iniWriteKeyValue(out, flatKey, val.get());
            }
          } else if (val->type == T::ARRAY && val->arr) {
            if (val->arr->empty()) {
              iniWriteKeyValue(out, flatKey, val.get());
            } else if (iniIsBareChild(val.get())) {
              // The array's own leading comments are emitted by the helper.
              iniWriteRepeatedLines(out, flatKey, val.get());
            }
          } else {
            iniWriteKeyValue(out, flatKey, val.get());
          }
        }
        // Pass 2: objects and object-arrays
        for (const auto& [key, val] : *obj) {
          if (val->type == T::OBJECT) {
            iniEmitObject(val.get(), out, iniEscapeSectionKey(key), false, false);
          } else if (val->type == T::ARRAY && val->arr) {
            bool allObjects = true;
            for (const auto& elem : *val->arr)
              if (elem->type != T::OBJECT) { allObjects = false; break; }
            if (allObjects && !val->arr->empty()) {
              std::string escKey = iniEscapeSectionKey(key);
              for (const auto& elem : *val->arr) {
                if (!out.empty() && out.back() != '\n') out += '\n';
                if (elem->comments) iniWriteCommentLines(out, elem->comments->leading);
                out += "[[" + escKey + "]]\n";
                if (elem->type == T::OBJECT)
                  iniEmitElement(elem.get(), out, escKey);
              }
            }
          }
        }
      } else {
        // Pretty: first pass -- bare-line producers
        for (const auto& [key, val] : *obj) {
          std::string flatKey = iniEscapeFlatKey(key);
          if (val->type == T::OBJECT) {
            if (val->obj && val->obj->empty()) {
              iniWriteKeyValue(out, flatKey, val.get());
            }
          } else if (val->type == T::ARRAY && val->arr) {
            if (val->arr->empty()) {
              iniWriteKeyValue(out, flatKey, val.get());
            } else if (iniIsBareChild(val.get())) {
              // The array's own leading comments are emitted by the helper.
              iniWriteRepeatedLines(out, flatKey, val.get());
            }
          } else {
            iniWriteKeyValue(out, flatKey, val.get());
          }
        }
        // Second pass: objects and object-arrays
        for (const auto& [key, val] : *obj) {
          if (val->type == T::OBJECT) {
            bool hasDirect = false;
            if (val->obj) {
              for (const auto& [sk, sv] : *val->obj) {
                if (iniIsBareChild(sv.get())) { hasDirect = true; break; }
              }
            }
            iniEmitObject(val.get(), out, iniEscapeSectionKey(key), hasDirect || !val->obj || val->obj->empty(), true);
          } else if (val->type == T::ARRAY && val->arr) {
            bool allObjects = true;
            for (const auto& elem : *val->arr) {
              if (elem->type != T::OBJECT) { allObjects = false; break; }
            }
            if (allObjects && !val->arr->empty()) {
              std::string escKey = iniEscapeSectionKey(key);
              for (const auto& elem : *val->arr) {
                if (!out.empty() && out.back() != '\n') out += '\n';
                if (elem->comments) iniWriteCommentLines(out, elem->comments->leading);
                out += "[[" + escKey + "]]\n";
                if (elem->type == T::OBJECT) {
                  iniEmitElement(elem.get(), out, escKey);
                }
              }
            }
          }
        }
      }
      if (comments) iniWriteCommentLines(out, comments->footing);
    }
  } else if (type == T::ARRAY && arr) {
    // INI always decodes to an OBJECT root, so a bare array cannot survive as
    // a root and has to be wrapped under a key. Emitting a single
    // "value = " line lost every element (iniFormatVal renders ANY array as
    // "[]", so [1,2,3] became "value = []" and re-parsed as {"value": []}).
    // Reuse the shapes the decoder already understands for a named array:
    // repeated keys build an array of primitives, [[...]] tables build an
    // array of objects. Both preserve the elements. iniIsBareChild throws for
    // nested and mixed arrays, which have no INI spelling at all.
    //
    // The root node's own comments are honoured like an OBJECT root's: leading
    // above the block, trailing after the wrapped value, footing after it. The
    // trailing comment survives wherever a line of the root's own exists to hang
    // it on: the bare path (after the last repeated line, inside
    // iniWriteRepeatedLines) and the empty path (`value = [] ;...`, which the
    // decoder reads as an inline comment on the "value" key). Both of those are
    // the single-line case only -- iniWriteTrailingComments pushes a block or
    // multiline trailing into ';' lines BELOW the value line, which re-parse as
    // the leading comments of whatever node comes next (see KNOWN LIMITATIONS).
    // The object-array path has NO line for the array itself -- only its
    // elements do -- so its trailing comment is API-only state, exactly like a
    // nested object that emits no header; writing
    // it after the last element line would move it to that ELEMENT on re-parse,
    // which is worse than dropping it.
    //
    // The leading half is written once per branch, NOT above the whole if/else:
    // iniWriteRepeatedLines() emits the leading comments of the array it writes,
    // so the bare path would repeat them.
    if (arr->empty()) {
      if (comments) iniWriteCommentLines(out, comments->leading);
      out += "value = []";
      if (comments && !comments->trailing.empty())
        iniWriteTrailingComments(out, comments->trailing);
      else
        out += '\n';
    } else if (iniIsBareChild(this)) {
      iniWriteRepeatedLines(out, "value", this);
    } else {
      if (comments) iniWriteCommentLines(out, comments->leading);
      for (const auto& elem : *arr) {
        if (!out.empty() && out.back() != '\n') out += '\n';
        if (elem->comments) iniWriteCommentLines(out, elem->comments->leading);
        out += "[[value]]\n";
        if (elem->type == T::OBJECT) iniEmitElement(elem.get(), out, "value");
      }
    }
    if (comments) iniWriteCommentLines(out, comments->footing);
  } else {
    // A scalar root likewise cannot be represented (the decoder always builds
    // an object), so it is wrapped under "value". The data is preserved; only
    // the document shape changes, which is inherent to INI.
    if (comments) iniWriteCommentLines(out, comments->leading);
    out += "value = ";
    iniFormatVal(this, out);
    if (comments && !comments->trailing.empty())
      iniWriteTrailingComments(out, comments->trailing);
    else
      out += '\n';
    if (comments) iniWriteCommentLines(out, comments->footing);
  }
}

// -- Decoder ----------------------------------------------------------------

inline bool asvJSON::fromINI(std::string_view input) {
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    if (input.empty()) {
      root = asvJSONValue::makeObject();
      if (!root) throw asvJSONError("INI: out of memory");
      return true;
    }

    // Strip UTF-8 BOM (EF BB BF) if present (mirrors the JSON/CSV parsers).
    if (input.size() >= 3 && (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
      input = input.substr(3);

    root = asvJSONValue::makeObject();
    if (!root) throw asvJSONError("INI: out of memory");

    auto lines = splitLines(input);
    std::vector<std::string> sectionPath;
    asvJSONValue* currentArrayElement = nullptr;
    bool inSection = false;

    for (size_t lineNum = 0; lineNum < lines.size(); lineNum++) {
      std::string rawLine = lines[lineNum];
      std::string_view line = iniTrim(rawLine);
      if (line.empty()) continue;

      // Line continuation: if line ends with \, join with next line
      if (line.back() == '\\') {
        std::string continued(line.substr(0, line.size() - 1));
        lineNum++;
        while (lineNum < lines.size()) {
          std::string_view nextLine = iniTrim(lines[lineNum]);
          if (nextLine.empty()) { lineNum++; continue; }
          if (nextLine.back() == '\\') {
            continued += std::string(nextLine.substr(0, nextLine.size() - 1));
            lineNum++;
          } else {
            continued += std::string(nextLine);
            break;
          }
        }
        rawLine = std::move(continued);
        line = rawLine;
      }

      // Comments: ; or # at line start (after trim)
      if (line[0] == ';' || line[0] == '#') continue;

      // Section header [section] or [[section]]
      if (line[0] == '[') {
        // Find the first unescaped closing bracket (keys may contain \] and \[)
        auto findCloseBracket = [&](size_t from) -> size_t {
          size_t i = from;
          while (i < line.size()) {
            if (line[i] == '\\' && i + 1 < line.size()) { i += 2; continue; }
            if (line[i] == ']') return i;
            i++;
          }
          return std::string_view::npos;
        };

        bool isArraySection = (line.size() > 1 && line[1] == '[');
        size_t start = isArraySection ? 2 : 1;
        size_t end = findCloseBracket(start);
        if (end == std::string_view::npos) throw asvJSONError("unclosed section bracket");
        if (isArraySection) {
          if (end + 1 >= line.size() || line[end + 1] != ']') throw asvJSONError("malformed array section header");
        }
        // Only whitespace and an optional ;/# comment may follow the header;
        // anything else is dropped silently today and would corrupt the layout.
        {
          size_t t = end + (isArraySection ? 2 : 1);
          while (t < line.size() && (line[t] == ' ' || line[t] == '\t')) t++;
          if (t < line.size() && line[t] != ';' && line[t] != '#')
            throw asvJSONError("unexpected content after section header");
        }
        size_t nameLen = end - start;

        std::string_view sectionName = iniTrim(line.substr(start, nameLen));
        if (sectionName.empty()) throw asvJSONError("empty section name");

        // Split section name on dots for nesting, trimming each segment.
        // Escaped dots ("\.") are literal characters of a single segment.
        std::vector<std::string> newPath = iniSplitDottedPath(sectionName);

        if (isArraySection) {
          std::string arrKey = newPath.back();
          newPath.pop_back();
          asvJSONValue* parent = root.get();
          // Walk the enclosing path. When a path node is an array (e.g. "items"
          // in "[[items.host]]"), descend into its last element: the new table
          // array belongs inside that element, not at document scope (the old
          // code silently dropped such headers entirely).
          auto descendArrays = [&]() -> void {
            while (parent && parent->type == asvJSONValue::ARRAY && parent->arr && !parent->arr->empty())
              parent = parent->arr->back().get();
          };
          descendArrays();
          for (const auto& seg : newPath) {
            if (!parent || parent->type != asvJSONValue::OBJECT)
              throw asvJSONError("cannot navigate through non-object");
            auto it = parent->obj->find(seg);
            if (it == parent->obj->end()) {
              auto newObj = asvJSONValue::makeObject();
              if (!newObj) throw asvJSONError("INI: out of memory");
              auto* raw = newObj.get();
              parent->obj->emplace(seg, std::move(newObj));
              parent = raw;
            } else {
              parent = it->second.get();
            }
            descendArrays();
          }
          if (!parent || parent->type != asvJSONValue::OBJECT)
            throw asvJSONError("cannot navigate through non-object");
          auto it = parent->obj->find(arrKey);
          if (it == parent->obj->end()) {
            auto newArr = asvJSONValue::makeArray();
            if (!newArr || !newArr->arr) throw asvJSONError("INI: out of memory");
            auto* arrPtr = newArr.get();
            parent->obj->emplace(arrKey, std::move(newArr));
            auto newObj = asvJSONValue::makeObject();
            if (!newObj) throw asvJSONError("INI: out of memory");
            currentArrayElement = newObj.get();
            arrPtr->arr->push_back(std::move(newObj));
          } else if (it->second && it->second->type == asvJSONValue::ARRAY && it->second->arr) {
            auto newObj = asvJSONValue::makeObject();
            if (!newObj) throw asvJSONError("INI: out of memory");
            currentArrayElement = newObj.get();
            it->second->arr->push_back(std::move(newObj));
          } else {
            // Reject instead of silently overwriting the existing value.
            throw asvJSONError("array section conflicts with existing value for \"" + arrKey + "\"");
          }
          inSection = false;
        } else {
          sectionPath = std::move(newPath);
          currentArrayElement = nullptr;
          inSection = true;
        }
        continue;
      }

      // Key-value pair
      size_t eqPos = std::string_view::npos;
      size_t colPos = std::string_view::npos;

      for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '=') { eqPos = i; break; }
        if (line[i] == ':') { colPos = i; break; }
      }

      size_t delim = eqPos != std::string_view::npos ? eqPos :
                     colPos != std::string_view::npos ? colPos : std::string_view::npos;

      if (delim == std::string_view::npos) {
        for (size_t i = 0; i < line.size(); i++) {
          if (line[i] == ' ' || line[i] == '\t') {
            delim = i;
            break;
          }
        }
        if (delim == std::string_view::npos) continue;
      }

      std::string_view keyView = iniTrim(line.substr(0, delim));
      std::string_view valView = iniTrim(line.substr(delim + 1));
      if (keyView.empty()) continue;
      std::string key(keyView);

      std::string valueStr;
      bool quoted = false;

      if (!valView.empty() && valView.front() == '"') {
        quoted = true;
        // Find closing quote
        size_t closeQuote = std::string_view::npos;
        bool escape = false;
        for (size_t i = 1; i < valView.size(); i++) {
          if (escape) { escape = false; continue; }
          if (valView[i] == '\\') { escape = true; continue; }
          if (valView[i] == '"') { closeQuote = i; break; }
        }
        if (closeQuote != std::string_view::npos) {
          // Only whitespace and an optional ;/# comment may follow the closing
          // quote; anything else is rejected instead of being silently dropped
          // (a "k = \"a\"b" line would otherwise lose the "b").
          size_t t = closeQuote + 1;
          while (t < valView.size() && (valView[t] == ' ' || valView[t] == '\t')) t++;
          if (t < valView.size() && valView[t] != ';' && valView[t] != '#')
            throw asvJSONError("unexpected content after quoted value");
          valueStr = iniUnescape(valView.substr(1, closeQuote - 1));
        } else {
          // Multi-line quoted value: gather lines until closing quote
          std::string accum(valView.substr(1));
          lineNum++;
          bool found = false;
          while (lineNum < lines.size()) {
            std::string_view nextLine = lines[lineNum];
            // Strip trailing \r from CRLF line endings
            if (!nextLine.empty() && nextLine.back() == '\r') {
              nextLine.remove_suffix(1);
            }
            accum += '\n';
            escape = false;
            closeQuote = std::string_view::npos;
            for (size_t i = 0; i < nextLine.size(); i++) {
              if (escape) { escape = false; continue; }
              if (nextLine[i] == '\\') { escape = true; continue; }
              if (nextLine[i] == '"') { closeQuote = i; found = true; break; }
            }
            if (found) {
              accum += nextLine.substr(0, closeQuote);
              break;
            }
            accum += nextLine;
            lineNum++;
          }
          if (!found) throw asvJSONError("unclosed quoted value");
          valueStr = iniUnescape(accum);
        }
      } else {
        // Bare value: strip inline comments
        size_t commentPos = iniFindComment(valView);
        if (commentPos != std::string_view::npos) {
          valView = valView.substr(0, commentPos);
        }
        valView = iniTrim(valView);
        valueStr = iniUnescape(valView);
      }

      // Determine target
      asvJSONValue* target = nullptr;
      if (currentArrayElement) {
        target = currentArrayElement;
      } else if (inSection && !sectionPath.empty()) {
        target = iniNavigate(root.get(), sectionPath, true);
      } else {
        target = root.get();
      }

      if (target) {
        // Split dotted keys into nested objects (e.g. "general.name" -> {"general":{"name":...}}).
        // Escaped dots ("a\.b") are literal single segments, so {"a.b":1} round-trips.
        if (key.find('.') != std::string::npos) {
          std::vector<std::string> keyPath = iniSplitDottedPath(key);
          std::string leafKey = keyPath.back();
          keyPath.pop_back();
          asvJSONValue* parent = iniNavigate(target, keyPath, true);
          if (parent) {
            iniSetValue(parent, leafKey, iniParseVal(valueStr, quoted));
          }
        } else {
          iniSetValue(target, iniUnescapeSectionKey(key), iniParseVal(valueStr, quoted));
        }
      }
    }

    formatType = FormatType::INI;
    auto entries = iniCollectComments(input);
    iniAttachComments(root.get(), entries);
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
