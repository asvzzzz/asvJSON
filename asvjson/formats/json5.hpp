#pragma once
// JSON5 serialization/parsing for asvJSON++
//
// JSON5 is a superset of JSON that adds:
//   - Unquoted object keys: {key: "value"}
//   - Single-quoted strings: 'value'
//   - Trailing commas: [1,2,3,]
//   - Hex numbers: 0xFF
//   - Octal numbers: 0o77
//   - Binary numbers: 0b1010
//   - Leading decimal: .5
//   - Plus sign: +42
//   - Comments // /* */ # (already handled by JSON parser)
// NOTE: unescaped U+000A/U+000D inside strings is accepted (lenient, per json5ToJson)
// and escaped to \n / \r via escapeControl; spec sec 5.2 forbids them -- use line-continuation
// with backslash for actual multi-line strings.
//
// Comments (`//`, `/* */`, `#`) round-trip: json5CollectComments walks the
// ORIGINAL input text, binds each comment to a node by path, and toJSON5()
// re-emits it in place (leading above a key/item, same-line trailing after a
// value, closings before the `}`/`]`). The paths are length-prefixed
// ("<len>:<bytes>" segments, json5PathSeg) so a key containing ANY byte -- 0x1F
// included -- is addressed exactly; the old 0x1F separator collided with such
// keys and dropped their comments.
//
// Known limitations:
// - A second `//`-trailing comment on one value cannot share the value's line
//   (a `//` ends its line), so it is emitted on its own line below and re-reads
//   as a LEADING comment of the next member, or as the container's closing
//   comment when the value was last. Only reachable from a programmatically
//   built tree: the collector never binds two `//`-comments to one value. The
//   text of every comment survives; only its placement is normalized.
// - Special types are lossless in both directions. DATETIME is written as an
//   ISO-8601 string and restored by the core parser's date detection, including
//   milliseconds and the UTC offset; BINARY and EXTENSION use MongoDB Extended
//   JSON (`{"$binary":{...,"subType":...}}`, subType 00 vs. the extension type),
//   OBJECTID `{"$oid":"<24 hex>"}` over the 12 raw bytes, REGEX
//   `{"$regex":...,"$options":...}`, TIMESTAMP `{"$timestamp":{"t":...}}`.
//   CUSTOM_TAG rides the same Extended-JSON convention as the core JSON
//   serializer (`{"$customTag":{"name":...,"value":...}}`), which the core
//   object parser restores; the encoder's default branch used to write a bare
//   "null" for it, so the tag and its payload were lost.

#include "../core.hpp"

namespace asvJSONInternal {

inline std::string json5ToJson(std::string_view input) {
  std::string out;
  out.reserve(input.size() + input.size() / 8);

  enum StrState { NONE, SINGLE, DOUBLE };
  StrState str = NONE;

  // Identifier characters. Deliberately byte-based rather than a UTF-8 decoder:
  // 0xC0 is the first lead byte of every 2-byte sequence and 0x80 the first
  // continuation byte, so all valid UTF-8 in the identifier range is accepted
  // (Unicode letters are a subset of the spec's ranges, which is lenient but
  // safe). Overlong or truncated sequences are accepted here as well; they can
  // only reach this point in an unquoted key, and the strict JSON parser
  // downstream rejects them when it decodes the key as UTF-8.
  auto isIdStart = [](char ch) noexcept { auto u = static_cast<unsigned char>(ch); return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || ch == '_' || ch == '$' || u >= 0xC0; };
  auto isIdCont = [](char ch) noexcept { auto u = static_cast<unsigned char>(ch); return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || ch == '_' || ch == '$' || u >= 0x80; };

  // Hex digit value (0-15) or -1 for invalid
  auto hexVal = [](char c) -> int { auto u = static_cast<unsigned char>(c); if (u >= '0' && u <= '9') return u - '0'; if (u >= 'a' && u <= 'f') return u - 'a' + 10; if (u >= 'A' && u <= 'F') return u - 'A' + 10; return -1; };

  // Escape control characters (< 0x20) for strict JSON
  auto escapeControl = [&out](char c) {
    if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c == '\b') out += "\\b";
    else if (c == '\f') out += "\\f";
    else { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c)); out += buf; }
  };

  bool radixJustEnded = false;

  for (size_t i = 0; i < input.size(); i++) {
    char c = input[i];

    if (str == DOUBLE) {
      if (c == '\\' && i + 1 < input.size()) {
        char n = input[i + 1];
        if (n == '\n') { i++; }
        else if (n == '\r') { i++; if (i + 1 < input.size() && input[i + 1] == '\n') i++; }
        else if (static_cast<unsigned char>(n) == 0xE2 && i + 3 < input.size() &&
                 static_cast<unsigned char>(input[i + 2]) == 0x80 &&
                 (static_cast<unsigned char>(input[i + 3]) == 0xA8 || static_cast<unsigned char>(input[i + 3]) == 0xA9)) { i += 3; }
        else if (n == 'v') { out += "\\u000B"; i++; }
        else if (n == '0') {
          if (i + 2 < input.size() && std::isdigit(static_cast<unsigned char>(input[i + 2])))
            throw asvJSONError("octal escape not allowed in JSON5");
          out += "\\u0000"; i++;
        }
        else if (n == 'x') {
          if (i + 3 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 2])) || !std::isxdigit(static_cast<unsigned char>(input[i + 3])))
            throw asvJSONError("invalid \\x escape in JSON5");
          out += "\\u00"; out += input[i + 2]; out += input[i + 3]; i += 3;
        }
        else if (n == 'u') {
          if (i + 2 < input.size() && input[i + 2] == '{') {
            size_t hexStart = i + 3;
            size_t hexEnd = hexStart;
            while (hexEnd < input.size() && std::isxdigit(static_cast<unsigned char>(input[hexEnd]))) hexEnd++;
            if (hexEnd == hexStart || hexEnd >= input.size() || input[hexEnd] != '}')
              throw asvJSONError("invalid \\u{...} escape in JSON5");
            if (hexEnd - hexStart > 6) throw asvJSONError("codepoint too large in \\u{...} escape");
            unsigned long long cp = 0;
            for (size_t k = hexStart; k < hexEnd; k++) cp = (cp << 4) | static_cast<unsigned>(hexVal(input[k]));
            if (cp > 0x10FFFF) throw asvJSONError("codepoint too large in \\u{...} escape");
            if (cp < 0x10000) {
              char b[8]; snprintf(b, sizeof(b), "\\u%04X", static_cast<unsigned>(cp)); out += b;
            } else {
              unsigned hi = 0xD800 + ((static_cast<unsigned>(cp) - 0x10000) >> 10);
              unsigned lo = 0xDC00 + ((static_cast<unsigned>(cp) - 0x10000) & 0x3FF);
              char b[14]; snprintf(b, sizeof(b), "\\u%04X\\u%04X", hi, lo); out += b;
            }
            i = hexEnd;
          } else {
            if (i + 5 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 2])) || !std::isxdigit(static_cast<unsigned char>(input[i + 3])) || !std::isxdigit(static_cast<unsigned char>(input[i + 4])) || !std::isxdigit(static_cast<unsigned char>(input[i + 5])))
              throw asvJSONError("invalid \\u escape in JSON5");
            out += "\\u"; out += input[i + 2]; out += input[i + 3]; out += input[i + 4]; out += input[i + 5];
            i += 5;
          }
        }
        else if (n == '\'') { out += '\''; i++; }
        else if (n == 'n') { out += "\\n"; i++; }
        else if (n == 't') { out += "\\t"; i++; }
        else if (n == 'r') { out += "\\r"; i++; }
        else if (n == 'b') { out += "\\b"; i++; }
        else if (n == 'f') { out += "\\f"; i++; }
        else {
          if (n >= '1' && n <= '9') throw asvJSONError("octal escape not allowed in JSON5");
          if (n == '"' || n == '\\') out += '\\';
          out += n; i++;
        }
      } else {
        if (static_cast<unsigned char>(c) < 0x20) escapeControl(c);
        else out += c;
        if (c == '"') str = NONE;
      }
      continue;
    }

    if (str == SINGLE) {
      if (c == '\\' && i + 1 < input.size()) {
        char n = input[++i];
        if (n == '\n') { /* line continuation: skip */ }
        else if (n == '\r') { if (i + 1 < input.size() && input[i + 1] == '\n') i++; }
        else if (static_cast<unsigned char>(n) == 0xE2 && i + 2 < input.size() &&
                 static_cast<unsigned char>(input[i + 1]) == 0x80 &&
                 (static_cast<unsigned char>(input[i + 2]) == 0xA8 || static_cast<unsigned char>(input[i + 2]) == 0xA9)) { i += 2; }
        else if (n == 'v') { out += "\\u000B"; }
        else if (n == '0') {
          if (i + 1 < input.size() && std::isdigit(static_cast<unsigned char>(input[i + 1])))
            throw asvJSONError("octal escape not allowed in JSON5");
          out += "\\u0000";
        }
        else if (n == 'x') {
          if (i + 2 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 1])) || !std::isxdigit(static_cast<unsigned char>(input[i + 2])))
            throw asvJSONError("invalid \\x escape in JSON5");
          out += "\\u00"; out += input[i + 1]; out += input[i + 2]; i += 2;
        }
        else if (n == 'u') {
          if (i + 1 < input.size() && input[i + 1] == '{') {
            size_t hexStart = i + 2;
            size_t hexEnd = hexStart;
            while (hexEnd < input.size() && std::isxdigit(static_cast<unsigned char>(input[hexEnd]))) hexEnd++;
            if (hexEnd == hexStart || hexEnd >= input.size() || input[hexEnd] != '}')
              throw asvJSONError("invalid \\u{...} escape in JSON5");
            if (hexEnd - hexStart > 6) throw asvJSONError("codepoint too large in \\u{...} escape");
            unsigned long long cp = 0;
            for (size_t k = hexStart; k < hexEnd; k++) cp = (cp << 4) | static_cast<unsigned>(hexVal(input[k]));
            if (cp > 0x10FFFF) throw asvJSONError("codepoint too large in \\u{...} escape");
            if (cp < 0x10000) {
              char b[8]; snprintf(b, sizeof(b), "\\u%04X", static_cast<unsigned>(cp)); out += b;
            } else {
              unsigned hi = 0xD800 + ((static_cast<unsigned>(cp) - 0x10000) >> 10);
              unsigned lo = 0xDC00 + ((static_cast<unsigned>(cp) - 0x10000) & 0x3FF);
              char b[14]; snprintf(b, sizeof(b), "\\u%04X\\u%04X", hi, lo); out += b;
            }
            i = hexEnd;
          } else {
            if (i + 4 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 1])) || !std::isxdigit(static_cast<unsigned char>(input[i + 2])) || !std::isxdigit(static_cast<unsigned char>(input[i + 3])) || !std::isxdigit(static_cast<unsigned char>(input[i + 4])))
              throw asvJSONError("invalid \\u escape in JSON5");
            out += "\\u";
            out += input[i + 1]; out += input[i + 2]; out += input[i + 3]; out += input[i + 4];
            i += 4;
          }
        }
        else if (n == '\'') { out += '\''; }
        else if (n == 'n') { out += "\\n"; }
        else if (n == 't') { out += "\\t"; }
        else if (n == 'r') { out += "\\r"; }
        else if (n == 'b') { out += "\\b"; }
        else if (n == 'f') { out += "\\f"; }
        else if (n == '"') { out += "\\\""; }
        else {
          if (n >= '1' && n <= '9') throw asvJSONError("octal escape not allowed in JSON5");
          if (n == '\\') out += '\\';
          out += n;
        }
      } else if (c == '\'') {
        out += '"';
        str = NONE;
      } else {
        if (static_cast<unsigned char>(c) < 0x20) escapeControl(c);
        else {
          if (c == '"') out += "\\\"";
          else out += c;
        }
      }
      continue;
    }

    // Comments outside strings (JSON5: //, /* */, #) are skipped.
    // Line-terminator check used by line comments: \n, \r, U+2028, U+2029.
    // JSON5 spec (Sec 8) treats all four as line terminators; a line comment
    // runs until any one of them or EOF.
    auto skipLineComment = [&]() {
      size_t j = i + 1;
      while (j < input.size()) {
        char nc = input[j];
        if (nc == '\n' || nc == '\r') break;
        // U+2028 / U+2029: full 3-byte prefix check so we do not false-match
        // on stray 0xA8 / 0xA9 bytes that happen to sit between other UTF-8.
        unsigned char uc = static_cast<unsigned char>(nc);
        if (uc == 0xA8 || uc == 0xA9) {
          if (j >= 2 && static_cast<unsigned char>(input[j - 1]) == 0x80 &&
              static_cast<unsigned char>(input[j - 2]) == 0xE2) break;
        }
        j++;
      }
      i = j;
    };
    if (c == '#') {
      radixJustEnded = false;
      skipLineComment();
      continue;
    }
    if (c == '/' && i + 1 < input.size() && input[i + 1] == '/') {
      radixJustEnded = false;
      skipLineComment();
      continue;
    }
    if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
      radixJustEnded = false;
      i += 2;
      while (i + 1 < input.size() && !(input[i] == '*' && input[i + 1] == '/')) i++;
      if (i + 1 >= input.size()) throw asvJSONError("unterminated block comment in JSON5");
      // Leave i ON the closing '/' so the loop's own i++ steps past it. Advancing
      // by 2 here skipped the character after the comment as well, which ate the
      // closing '}' or ']' whenever a block comment was the last thing on the
      // line: {"a": 1 /* c */} converted to {"a": 1  and then failed with
      // "Expected ',' or '}'". A space after the comment masked it.
      if (i + 1 < input.size()) i += 1;
      continue;
    }

    if (c == '"') { radixJustEnded = false; out += c; str = DOUBLE; continue; }
    if (c == '\'') { radixJustEnded = false; out += '"'; str = SINGLE; continue; }

    // Control character check (outside strings)
    if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != '\v' && c != '\f')
      throw asvJSONError("control character not allowed in JSON5");

    // Extended whitespace (JSON5 Sec 8)
    if (c == '\v' || c == '\f') continue;
    if (static_cast<unsigned char>(c) == 0xC2 && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xA0) { i++; continue; }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(input[i + 2]) == 0xA8 || static_cast<unsigned char>(input[i + 2]) == 0xA9)) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
        static_cast<unsigned char>(input[i + 2]) >= 0x80 && static_cast<unsigned char>(input[i + 2]) <= 0x8A) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x81 &&
        static_cast<unsigned char>(input[i + 2]) == 0x9F) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xE3 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
        static_cast<unsigned char>(input[i + 2]) == 0x80) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xEF && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xBB &&
        static_cast<unsigned char>(input[i + 2]) == 0xBF) { i += 2; continue; }

    // Trailing comma (skips comments between comma and bracket)
    if (c == ',') {
      size_t j = i + 1;
      bool isTrailing = false;
      while (j < input.size()) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r' || nc == '\v' || nc == '\f') { j++; continue; }
        auto unc = static_cast<unsigned char>(nc);
        if (unc == 0xC2 && j + 1 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xA0) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[j + 2]) == 0xA8 || static_cast<unsigned char>(input[j + 2]) == 0xA9)) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) >= 0x80 && static_cast<unsigned char>(input[j + 2]) <= 0x8A) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x81 &&
            static_cast<unsigned char>(input[j + 2]) == 0x9F) { j += 3; continue; }
        if (unc == 0xE3 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) == 0x80) { j += 3; continue; }
        if (unc == 0xEF && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xBB &&
            static_cast<unsigned char>(input[j + 2]) == 0xBF) { j += 3; continue; }
        if (nc == '/' && j + 1 < input.size()) {
          if (input[j + 1] == '/') {
            // Same line-terminator set as the main comment skip.
            while (j < input.size()) {
              char nj = input[j];
              if (nj == '\n' || nj == '\r') break;
              unsigned char nuc = static_cast<unsigned char>(nj);
              if (nuc == 0xA8 || nuc == 0xA9) {
                if (j >= 2 && static_cast<unsigned char>(input[j - 1]) == 0x80 &&
                    static_cast<unsigned char>(input[j - 2]) == 0xE2) break;
              }
              j++;
            }
            continue;
          }
          if (input[j + 1] == '*') {
            j += 2;
            while (j + 1 < input.size() && !(input[j] == '*' && input[j + 1] == '/')) j++;
            if (j + 1 >= input.size()) throw asvJSONError("unterminated block comment in JSON5");
            j += 2;
            continue;
          }
        }
        if (nc == '#') {
          while (j < input.size()) {
            char nj = input[j];
            if (nj == '\n' || nj == '\r') break;
            unsigned char nuc = static_cast<unsigned char>(nj);
            if (nuc == 0xA8 || nuc == 0xA9) {
              if (j >= 2 && static_cast<unsigned char>(input[j - 1]) == 0x80 &&
                  static_cast<unsigned char>(input[j - 2]) == 0xE2) break;
            }
            j++;
          }
          continue;
        }
        if (nc == '}' || nc == ']') isTrailing = true;
        break;
      }
      if (isTrailing) {
        size_t k = out.size();
        while (k > 0) { char pc = out[k - 1]; if (pc == ' ' || pc == '\t' || pc == '\n' || pc == '\r' || pc == '\v' || pc == '\f') { k--; continue; } break; }
        if (k == 0 || out[k - 1] == ',' || out[k - 1] == '{' || out[k - 1] == '[' || out[k - 1] == ':')
          throw asvJSONError("trailing comma requires a preceding value");
        i = j - 1; continue; // skip comma, let outer loop hit }
      }
      out += ',';
      radixJustEnded = false;
      continue;
    }

    // Unquoted key (supports Unicode identifiers)
    if (isIdStart(c)) {
      radixJustEnded = false;
      size_t start = i;
      while (i < input.size() && isIdCont(input[i])) {
        // Stop at multibyte whitespace (which the identifier continuation rule would otherwise swallow)
        auto uc = static_cast<unsigned char>(input[i]);
        if (uc == 0xC2 && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xA0) break;
        if (uc == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[i + 2]) == 0xA8 || static_cast<unsigned char>(input[i + 2]) == 0xA9)) break;
        if (uc == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
            static_cast<unsigned char>(input[i + 2]) >= 0x80 && static_cast<unsigned char>(input[i + 2]) <= 0x8A) break;
        if (uc == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x81 &&
            static_cast<unsigned char>(input[i + 2]) == 0x9F) break;
        if (uc == 0xE3 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
            static_cast<unsigned char>(input[i + 2]) == 0x80) break;
        if (uc == 0xEF && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xBB &&
            static_cast<unsigned char>(input[i + 2]) == 0xBF) break;
        i++;
      }
      std::string_view ident(input.data() + start, i - start);
      // Validate identifier characters
      for (size_t k = 0; k < ident.size(); k++) {
        auto uc = static_cast<unsigned char>(ident[k]);
        if (uc <= 0x20 || uc == 0x7F || uc == ':' || uc == '{' || uc == '}' || uc == '[' || uc == ']' || uc == ',' || uc == '"' || uc == '\'' || uc == '\\' || uc == '/' || uc == '#')
          throw asvJSONError("invalid character in JSON5 identifier");
      }
      i--;
      bool isKey = false;
      for (size_t j = i + 1; j < input.size(); j++) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r' || nc == '\v' || nc == '\f') continue;
        auto unc = static_cast<unsigned char>(nc);
        if (unc == 0xC2 && j + 1 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xA0) { j++; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[j + 2]) == 0xA8 || static_cast<unsigned char>(input[j + 2]) == 0xA9)) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) >= 0x80 && static_cast<unsigned char>(input[j + 2]) <= 0x8A) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x81 &&
            static_cast<unsigned char>(input[j + 2]) == 0x9F) { j += 2; continue; }
        if (unc == 0xE3 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) == 0x80) { j += 2; continue; }
        if (unc == 0xEF && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xBB &&
            static_cast<unsigned char>(input[j + 2]) == 0xBF) { j += 2; continue; }
        if (nc == '/') {
          if (j + 1 < input.size() && input[j + 1] == '/') { while (j < input.size() && input[j] != '\n') j++; continue; }
          if (j + 1 < input.size() && input[j + 1] == '*') {
            // Leave j ON the closing '/' (advance by 1, not 2): this is a
            // for-loop, so `continue` runs its own j++. Advancing by 2 here as
            // well skipped the byte after "*/", so the ':' in {a/* c */:1} was
            // skipped and the key went unquoted (invalid JSON). Mirrors the
            // tokenizer's block-comment handling above.
            j += 2;
            while (j + 1 < input.size() && !(input[j] == '*' && input[j + 1] == '/')) j++;
            if (j + 1 < input.size()) j += 1;
            continue;
          }
        }
        if (nc == '#') { while (j < input.size() && input[j] != '\n') j++; continue; }
        if (nc == ':') isKey = true;
        break;
      }
      if (isKey) {
        out += '"', out.append(ident.data(), ident.size()), out += '"';
      } else {
        out.append(ident.data(), ident.size());
      }
      continue;
    }

    // Hex / octal / binary numbers
    if (c == '0' && i + 1 < input.size()) {
      char n = input[i + 1];
      if ((n == 'x' || n == 'X') && i + 2 < input.size() && std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
        i += 2;
        std::string digits;
        while (i < input.size() && std::isxdigit(static_cast<unsigned char>(input[i]))) { digits += input[i++]; }
        i--;
        errno = 0;
        char* end = nullptr;
        unsigned long long val = strtoull(digits.c_str(), &end, 16);
        if (errno == ERANGE || end != digits.c_str() + digits.size())
          throw asvJSONError("number out of range in JSON5");
        out += std::to_string(val);
        radixJustEnded = true;
        continue;
      }
      if ((n == 'o' || n == 'O') && i + 2 < input.size() && input[i + 2] >= '0' && input[i + 2] <= '7') {
        i += 2;
        std::string digits;
        while (i < input.size() && input[i] >= '0' && input[i] <= '7') { digits += input[i++]; }
        i--;
        errno = 0;
        char* end = nullptr;
        unsigned long long val = strtoull(digits.c_str(), &end, 8);
        if (errno == ERANGE || end != digits.c_str() + digits.size())
          throw asvJSONError("number out of range in JSON5");
        out += std::to_string(val);
        radixJustEnded = true;
        continue;
      }
      if ((n == 'b' || n == 'B') && i + 2 < input.size() && (input[i + 2] == '0' || input[i + 2] == '1')) {
        i += 2;
        std::string digits;
        while (i < input.size() && (input[i] == '0' || input[i] == '1')) { digits += input[i++]; }
        i--;
        errno = 0;
        char* end = nullptr;
        unsigned long long val = strtoull(digits.c_str(), &end, 2);
        if (errno == ERANGE || end != digits.c_str() + digits.size())
          throw asvJSONError("number out of range in JSON5");
        out += std::to_string(val);
        radixJustEnded = true;
        continue;
      }
    }

    // Leading decimal: .5 -> 0.5
    if (c == '.' && !radixJustEnded && (i == 0 || !std::isdigit(static_cast<unsigned char>(input[i - 1]))) && i + 1 < input.size() && std::isdigit(static_cast<unsigned char>(input[i + 1]))) {
      out += '0';
      out += c;
      continue;
    }

    // A radix literal has no fraction part; reject an adjacent '.' (e.g. 0xFF.5)
    if (radixJustEnded && c == '.') throw asvJSONError("fraction not allowed after radix number in JSON5");

    // Signs (+/-) with whitespace-tolerant lookahead (JSON5: - Infinity, + 5)
    if (c == '+' || c == '-') {
      bool prevRadix = radixJustEnded;
      radixJustEnded = false;
      size_t j = i + 1;
      while (j < input.size()) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r' || nc == '\v' || nc == '\f') { j++; continue; }
        auto unc = static_cast<unsigned char>(nc);
        if (unc == 0xC2 && j + 1 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xA0) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[j + 2]) == 0xA8 || static_cast<unsigned char>(input[j + 2]) == 0xA9)) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) >= 0x80 && static_cast<unsigned char>(input[j + 2]) <= 0x8A) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x81 &&
            static_cast<unsigned char>(input[j + 2]) == 0x9F) { j += 3; continue; }
        if (unc == 0xE3 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) == 0x80) { j += 3; continue; }
        if (unc == 0xEF && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xBB &&
            static_cast<unsigned char>(input[j + 2]) == 0xBF) { j += 3; continue; }
        break;
      }
      bool isNaN_ = (j + 3 <= input.size() && input.compare(j, 3, "NaN") == 0);
      bool isInf = (j + 8 <= input.size() && input.compare(j, 8, "Infinity") == 0);
      bool isNum = (j < input.size() && (std::isdigit(static_cast<unsigned char>(input[j])) || input[j] == '.'));
      if (isNaN_) { if (c == '-') out += '-'; out += "NaN"; i = j + 2; continue; }
      if (isInf) {
        if (c == '-') out += '-';
        out += "Infinity"; i = j + 7; continue;
      }
      // A sign here may only start a new value (prefix position: previous
      // non-ws token is '{' '[' ',' ':' or start of document). An infix
      // '+'/'-' (e.g. `a: 1 + 2`) must not be silently swallowed so the
      // number becomes `12`; reject instead.
      bool prefix = (i == 0);
      for (size_t q = i; q-- > 0;) {
        char pc = input[q];
        if (pc == ' ' || pc == '\t' || pc == '\n' || pc == '\r' || pc == '\v' || pc == '\f') continue;
        auto u = static_cast<unsigned char>(pc);
        if (u == 0xC2 && q > 0 && static_cast<unsigned char>(input[q - 1]) == 0xA0) { continue; }
        if (u == 0xE2 && q > 1 && static_cast<unsigned char>(input[q - 1]) == 0x80 &&
            (static_cast<unsigned char>(input[q - 2]) == 0xA8 || static_cast<unsigned char>(input[q - 2]) == 0xA9)) { continue; }
        if (u == 0xE2 && q > 1 && static_cast<unsigned char>(input[q - 1]) == 0x80 &&
            static_cast<unsigned char>(input[q - 2]) >= 0x80 && static_cast<unsigned char>(input[q - 2]) <= 0x8A) { continue; }
        if (u == 0xE2 && q > 1 && static_cast<unsigned char>(input[q - 1]) == 0x81 &&
            static_cast<unsigned char>(input[q - 2]) == 0x9F) { continue; }
        if (u == 0xE3 && q > 1 && static_cast<unsigned char>(input[q - 1]) == 0x80 &&
            static_cast<unsigned char>(input[q - 2]) == 0x80) { continue; }
        if (u == 0xEF && q > 1 && static_cast<unsigned char>(input[q - 1]) == 0xBB &&
            static_cast<unsigned char>(input[q - 2]) == 0xBF) { continue; }
        prefix = (pc == '{' || pc == '[' || pc == ',' || pc == ':');
        break;
      }
      if (c == '+' || c == '-') {
        // An exponent sign is part of the number when it sits directly
        // behind e/E whose mantissa is a digit-bearing literal ('1e+05',
        // '5.e-3') -- keep it (the intermediate JSON accepts 'e+NN'). The
        // digit scan keeps an identifier-final 'e' ('true+1') and a radix
        // literal ('0x1e+5', prevRadix) from posing as an exponent, so both
        // still hit the stray-sign rejection below. The digits must follow
        // directly too: '1e+ 5' keeps a gap and is not an exponent.
        bool expSign = false;
        if (!prevRadix && i >= 2 && j == i + 1 && isNum && (input[i - 1] == 'e' || input[i - 1] == 'E')) {
          size_t q = i - 2;
          bool sawDot = false, sawDigit = false;
          for (;;) {
            char pc2 = input[q];
            if (std::isdigit(static_cast<unsigned char>(pc2))) sawDigit = true;
            else if (pc2 == '.' && !sawDot) sawDot = true;
            else break;
            if (q == 0) break;
            --q;
          }
          expSign = sawDigit;
        }
        if (!prefix && !expSign) throw asvJSONError("stray '+'/'-' sign in JSON5 number");
        if (c == '+' && !expSign) continue;
      }
      if (isNum && j > i + 1) { out += '-'; i = j - 1; continue; }
    }

    // Trailing decimal: 5. -> 5.0, 5.e10 -> 5.0e10
    if (c == '.' && i > 0 && std::isdigit(static_cast<unsigned char>(input[i - 1])) &&
        (i + 1 >= input.size() || !std::isdigit(static_cast<unsigned char>(input[i + 1])))) {
      out += ".0";
      continue;
    }

    out += c;
    radixJustEnded = false;
  }

  if (str != NONE) throw asvJSONError("unclosed string in JSON5");
  return out;
}

// ------------------------- Comment collection -------------------------
// JSON5 is bracket-delimited like RON, so comment binding reuses the RON
// collector model: own-line comments lead the following node, same-line
// comments trail the value just parsed, comments before a closer become the
// container's closing comments, and a trailing newline-separated block becomes
// the document footing.

struct Json5CommentEntry {
  std::string path;                   // "" is the root value
  std::vector<Comment> leading;
  std::vector<Comment> trailing;
  std::vector<Comment> closing;
  bool footing = false;
};

// Comment addresses are "<len>:<bytes>" segments joined with no separator (see
// json5PathSeg). They used to be joined with a 0x1F separator byte, which
// collided with itself: the JSON5 spec puts no restriction on quoted keys, so
// {"a\u001fb":1} is legal, and the collector builds the path "a\x1Fb" -- which
// json5FindPath then split into "a" and "b", looked both up, found neither, and
// dropped the comment silently. The encoder never produces such a key (it
// escapes 0x1F as \u001f), but the collector reads the ORIGINAL input, not the
// re-escaped text, so hand-written input reaches it. Length prefixes need no
// reserved byte at all, so every key survives byte for byte -- the same fix
// INI already made (iniPathSeg).
inline std::string json5PathSeg(std::string_view s) {
  return std::to_string(s.size()) + ":" + std::string(s);
}
inline std::string json5PathJoin(const std::string& base, std::string_view seg) {
  return base + json5PathSeg(seg);
}

inline asvJSONValue* json5FindPath(asvJSONValue* root, std::string_view path) {
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
    // the comment ({"[abc]": 1 /* c */} lost "c"). The collector writes "[N]"
    // only for ARRAY children, so brackets are a reliable signal there -- and
    // the size check below keeps an empty segment from reaching front().
    if (cur->type == asvJSONValue::ARRAY) {
      if (!cur->arr || seg.size() < 3 || seg.front() != '[' || seg.back() != ']') return nullptr;
      size_t idx = 0;
      for (size_t k = 1; k + 1 < seg.size(); k++) {
        char d = seg[k];
        if (d < '0' || d > '9') return nullptr;
        // Guard against overflow: same pattern as parseArrayIndex in schema.hpp.
        if (idx > (SIZE_MAX - static_cast<size_t>(d - '0')) / 10) return nullptr;
        idx = idx * 10 + static_cast<size_t>(d - '0');
      }
      if (idx >= cur->arr->size()) return nullptr;
      cur = (*cur->arr)[idx].get();
    } else if (cur->type == asvJSONValue::OBJECT) {
      if (!cur->obj) return nullptr;
      auto it = cur->obj->find(seg);
      if (it == cur->obj->end()) return nullptr;
      cur = it->second.get();
    } else {
      return nullptr;
    }
    pos = colon + 1 + len;
  }
  return cur;
}

inline void json5AppendUtf8(std::string& s, unsigned cp) {
  if (cp < 0x80) s += static_cast<char>(cp);
  else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Decode the raw text between quotes of a JSON5 string/identifier key so a
// comment path matches the decoded key in the parsed tree. Covers the escapes
// json5ToJson accepts (incl. \uXXXX surrogate pairs and \u{...}).
inline std::string json5DecodeString(std::string_view raw) {
  std::string s;
  s.reserve(raw.size());
  auto hexv = [](char c) -> int {
    auto u = static_cast<unsigned char>(c);
    if (u >= '0' && u <= '9') return u - '0';
    if (u >= 'a' && u <= 'f') return u - 'a' + 10;
    if (u >= 'A' && u <= 'F') return u - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < raw.size(); i++) {
    char c = raw[i];
    if (c != '\\') { s += c; continue; }
    if (i + 1 >= raw.size()) { s += '\\'; break; }
    char e = raw[++i];
    switch (e) {
      case 'n': s += '\n'; break;
      case 't': s += '\t'; break;
      case 'r': s += '\r'; break;
      case 'b': s += '\b'; break;
      case 'f': s += '\f'; break;
      case 'v': s += '\v'; break;
      case '0': s += '\0'; break;
      case '\n': break;
      case '\r': if (i + 1 < raw.size() && raw[i + 1] == '\n') i++; break;
      case 'x': {
        if (i + 2 < raw.size() && hexv(raw[i + 1]) >= 0 && hexv(raw[i + 2]) >= 0) {
          json5AppendUtf8(s, static_cast<unsigned>((hexv(raw[i + 1]) << 4) | hexv(raw[i + 2])));
          i += 2;
        } else s += "\\x";
        break;
      }
      case 'u': {
        if (i + 1 < raw.size() && raw[i + 1] == '{') {
          unsigned cp = 0; size_t k = i + 2;
          while (k < raw.size() && raw[k] != '}' && hexv(raw[k]) >= 0) { cp = (cp << 4) | static_cast<unsigned>(hexv(raw[k])); k++; }
          if (k < raw.size() && raw[k] == '}' && cp <= 0x10FFFF) { json5AppendUtf8(s, cp); i = k; }
          else s += "\\u{";
          break;
        }
        if (i + 4 < raw.size() && hexv(raw[i + 1]) >= 0 && hexv(raw[i + 2]) >= 0 &&
            hexv(raw[i + 3]) >= 0 && hexv(raw[i + 4]) >= 0) {
          unsigned hi = static_cast<unsigned>((hexv(raw[i + 1]) << 12) | (hexv(raw[i + 2]) << 8) | (hexv(raw[i + 3]) << 4) | hexv(raw[i + 4]));
          i += 4;
          if (hi >= 0xD800 && hi <= 0xDBFF && i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u' &&
              hexv(raw[i + 3]) >= 0 && hexv(raw[i + 4]) >= 0 && hexv(raw[i + 5]) >= 0 && hexv(raw[i + 6]) >= 0) {
            unsigned lo = static_cast<unsigned>((hexv(raw[i + 3]) << 12) | (hexv(raw[i + 4]) << 8) | (hexv(raw[i + 5]) << 4) | hexv(raw[i + 6]));
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              unsigned cp = 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
              json5AppendUtf8(s, cp);
              i += 6;
              break;
            }
          }
          json5AppendUtf8(s, hi);
        } else s += "\\u";
        break;
      }
      default: s += e; break;
    }
  }
  return s;
}

inline void json5AttachComments(asvJSONValue* root, const std::vector<Json5CommentEntry>& entries) {
  if (!root || entries.empty()) return;
  auto appendAll = [](std::vector<Comment>& dst, const std::vector<Comment>& src) {
    for (const auto& c : src) dst.push_back(c);
  };
  for (const auto& e : entries) {
    asvJSONValue* node = e.footing ? json5FindPath(root, "") : json5FindPath(root, e.path);
    if (!node) continue;
    if (!node->comments) node->comments = std::make_unique<Comments>();
    if (e.footing) {
      // A document footer belongs to the footing group only. Also copying it
      // into leading/trailing made toJSON5 print it twice, and since the
      // collector then saw two comments on re-parse, the count grew on every
      // round-trip. The comment that trailed the root value inline stays in
      // `trailing` (emitted right after the value, where it was written);
      // standalone footer lines go to `footing`.
      appendAll(node->comments->footing, e.leading);
      if (!e.trailing.empty()) appendAll(node->comments->trailing, e.trailing);
      continue;
    }
    if (!e.leading.empty()) appendAll(node->comments->leading, e.leading);
    if (!e.trailing.empty()) appendAll(node->comments->trailing, e.trailing);
    if (!e.closing.empty()) appendAll(node->comments->closing, e.closing);
  }
}

// Collect comments from a JSON5 document as path-anchored entries. The walk is
// a token-level parse of the JSON5 structure (strings, scalars, object keys and
// arrays), so comment positions resolve to the same JSON paths the parsed tree
// uses. Mirrors the RON collector's leading/trailing/closing/ footing model.
inline std::vector<Json5CommentEntry> json5CollectComments(std::string_view input) {
  struct Tok {
    char kind;        // '{' '}' '[' ']' ':' ',' 'S' (string) 'B' (bare) 'C' (comment)
    bool ownLine = false;  // comment: stood alone on its line
    bool block = false;    // comment: "/* ... */"
    std::string text;      // 'S': decoded string; 'B'/comment: raw source text
  };
  std::vector<Tok> toks;
  const size_t n = input.size();
  const auto isWs = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
  };
  // Is this comment the first thing on its line? A line break resets the scan
  // and a bare space/tab/\v/\f keeps it going. \n and a lone \r are line breaks,
  // as are U+2028 / U+2029 (E2 80 A8 / E2 80 A9) -- JSON5 line terminators that
  // the lexer also accepts as whitespace. \r is a break, not whitespace, so a
  // comment after a lone \r is own-line, not trailing.
  const auto ownLineStart = [&](size_t start) {
    size_t j = start;
    while (j > 0) {
      j--;
      unsigned char c = static_cast<unsigned char>(input[j]);
      if (c == '\n' || c == '\r') return true;   // line breaks
      // U+2028 / U+2029 (E2 80 A8 / E2 80 A9) are JSON5 line terminators too.
      if ((c == 0xA8 || c == 0xA9) && j >= 2 &&
          static_cast<unsigned char>(input[j - 1]) == 0x80 &&
          static_cast<unsigned char>(input[j - 2]) == 0xE2) return true;
      // NBSP (C2 A0) and the BOM (EF BB BF) are whitespace, not breaks -- skip
      // the whole sequence so its trailing byte does not read as content and
      // misclassify a genuinely own-line comment.
      if (c == 0xA0 && j >= 1 && static_cast<unsigned char>(input[j - 1]) == 0xC2) { j -= 1; continue; }
      if (c == 0xBF && j >= 2 && static_cast<unsigned char>(input[j - 1]) == 0xBB &&
          static_cast<unsigned char>(input[j - 2]) == 0xEF) { j -= 2; continue; }
      if (c != ' ' && c != '\t' && c != '\v' && c != '\f') return false;
    }
    return true;
  };
  const auto trimEnd = [](std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
  };
  size_t i = 0;
  while (i < n) {
    char c = input[i];
    if (isWs(c)) { i++; continue; }
    // Multi-byte whitespace must advance its FULL width: a bare i++ left the
    // trailing bytes (0xA0 of NBSP, the 0x80 0xA8 of U+2028, ...) to be lexed
    // as a spurious bare token, so a key became "\xA0K" and the comment bound to
    // the wrong path (or was dropped). NBSP is 2 bytes, U+2028 / U+2029 and the
    // BOM are 3 -- matching the widths the lexer skips in json5ToJson.
    if (static_cast<unsigned char>(c) == 0xC2 && i + 1 < n && static_cast<unsigned char>(input[i + 1]) == 0xA0) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < n && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(input[i + 2]) == 0xA8 || static_cast<unsigned char>(input[i + 2]) == 0xA9)) { i += 3; continue; }
    if (static_cast<unsigned char>(c) == 0xEF && i + 2 < n && static_cast<unsigned char>(input[i + 1]) == 0xBB &&
        static_cast<unsigned char>(input[i + 2]) == 0xBF) { i += 3; continue; }
    // Comments.
    bool isLineCmt = false, isBlockCmt = false;
    if (c == '#') isLineCmt = true;
    else if (c == '/' && i + 1 < n && input[i + 1] == '/') isLineCmt = true;
    else if (c == '/' && i + 1 < n && input[i + 1] == '*') isBlockCmt = true;
    if (isLineCmt || isBlockCmt) {
      Tok t;
      t.kind = 'C';
      t.ownLine = ownLineStart(i);
      if (isLineCmt) {
        size_t start = i + (c == '#' ? 1 : 2);
        size_t j = i + (c == '#' ? 1 : 2);
        size_t lineEnd = input.find('\n', j);
        if (lineEnd == std::string_view::npos) lineEnd = n;
        j = lineEnd;
        t.block = false;
        auto v = trimEnd(input.substr(start, j - start));
        t.text.assign(v.data(), v.size());
        i = j;
      } else {
        size_t j = i + 2;
        while (j + 1 < n && !(input[j] == '*' && input[j + 1] == '/')) j++;
        if (j + 1 >= n) throw asvJSONError("unterminated block comment in JSON5");
        size_t end = j;
        t.block = true;
        auto v = input.substr(i + 2, end - (i + 2));
        t.text.assign(v.data(), v.size());
        i = j + 2;
      }
      toks.push_back(std::move(t));
      continue;
    }
    if (c == '"' || c == '\'') {
      char q = c;
      size_t j = i + 1;
      while (j < n) {
        if (input[j] == '\\' && j + 1 < n) {
            if (input[j + 1] == '\r' && j + 2 < n && input[j + 2] == '\n') j += 3;
            else j += 2;
            continue;
        }
        if (input[j] == q) break;
        j++;
      }
      Tok t;
      t.kind = 'S';
      t.text = json5DecodeString(input.substr(i + 1, j - (i + 1)));
      toks.push_back(std::move(t));
      i = (j < n) ? j + 1 : j;
      continue;
    }
    if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
      Tok t;
      t.kind = c;
      toks.push_back(std::move(t));
      i++;
      continue;
    }
    // Bare token: identifiers, numbers, booleans, null, Infinity, ...
    size_t j = i;
    while (j < n && !isWs(input[j]) && input[j] != '{' && input[j] != '}' &&
           input[j] != '[' && input[j] != ']' && input[j] != ':' && input[j] != ',' &&
           input[j] != '"' && input[j] != '\'' && input[j] != '/' && input[j] != '#')
      j++;
    Tok t;
    t.kind = 'B';
    auto v = input.substr(i, j - i);
    t.text.assign(v.data(), v.size());
    toks.push_back(std::move(t));
    i = j;
  }

  struct P {
    const std::vector<Tok>& toks;
    size_t k = 0;
    std::vector<Json5CommentEntry> out;
    std::vector<Comment> pendLead;
    std::vector<Comment> pendTrail;
  } p{toks, 0, {}, {}, {}};

  auto joinPath = [](const std::string& path, const std::string& seg) {
    return json5PathJoin(path, seg);
  };
  auto skipComments = [&]() {
    while (p.k < toks.size() && toks[p.k].kind == 'C') {
      Comment cm;
      cm.text = toks[p.k].text;
      cm.block = toks[p.k].block;
      if (toks[p.k].ownLine) p.pendLead.push_back(std::move(cm));
      else p.pendTrail.push_back(std::move(cm));
      p.k++;
    }
  };
  auto commitLead = [&](std::string path) {
    if (p.pendLead.empty()) return;
    Json5CommentEntry e;
    e.path = std::move(path);
    e.leading.swap(p.pendLead);
    p.out.push_back(std::move(e));
  };
  auto commitTrail = [&](std::string path) {
    if (p.pendTrail.empty()) return;
    Json5CommentEntry e;
    e.path = std::move(path);
    e.trailing.swap(p.pendTrail);
    p.out.push_back(std::move(e));
  };
  auto commitClosing = [&](std::string path) {
    if (p.pendLead.empty() && p.pendTrail.empty()) return;
    Json5CommentEntry e;
    e.path = std::move(path);
    if (!p.pendTrail.empty()) e.trailing.swap(p.pendTrail);
    for (auto& c : p.pendLead) e.closing.push_back(std::move(c));
    p.pendLead.clear();
    if (!e.closing.empty() || !e.trailing.empty()) p.out.push_back(std::move(e));
  };

  std::function<void(const std::string&)> parseValue;
  std::function<void(const std::string&)> parseObject;
  std::function<void(const std::string&)> parseArray;

  parseValue = [&](const std::string& path) {
    skipComments();
    commitLead(path);
    if (p.k >= toks.size()) return;
    const char kind = toks[p.k].kind;
    if (kind == '{') { p.k++; parseObject(path); return; }
    if (kind == '[') { p.k++; parseArray(path); return; }
    if (kind == 'S' || kind == 'B') { p.k++; return; }   // scalar value token
    p.k++;              // tolerate a stray structural token (validated elsewhere)
  };

  parseObject = [&](const std::string& path) {
    while (true) {
      skipComments();
      if (p.k >= toks.size()) return;
      if (toks[p.k].kind == '}') { commitClosing(path); p.k++; return; }
      if (toks[p.k].kind == '{' || toks[p.k].kind == '[' || toks[p.k].kind == ':' ||
          toks[p.k].kind == ',') { p.k++; continue; }   // should not happen in valid JSON5
      // The token text is already the decoded key for 'S' (json5DecodeString ran in
      // the lexer) and the raw identifier for a bare key, so both cases read the
      // same field -- there is no quoted/bare distinction to make here.
      std::string key = toks[p.k].text;
      p.k++;
      skipComments();
      if (p.k < toks.size() && toks[p.k].kind == ':') p.k++;
      const std::string childPath = joinPath(path, key);
      parseValue(childPath);
      skipComments();
      commitTrail(childPath);
      if (p.k >= toks.size()) return;
      if (toks[p.k].kind == ',') {
        p.k++;
        skipComments();
        commitTrail(childPath);
        if (p.k < toks.size() && toks[p.k].kind == '}') { commitClosing(path); p.k++; return; }
        continue;
      }
      if (toks[p.k].kind == '}') { commitClosing(path); p.k++; return; }
      return;
    }
  };

  parseArray = [&](const std::string& path) {
    size_t idx = 0;
    while (true) {
      skipComments();
      if (p.k >= toks.size()) return;
      if (toks[p.k].kind == ']') { commitClosing(path); p.k++; return; }
      const std::string childPath = joinPath(path, "[" + std::to_string(idx) + "]");
      parseValue(childPath);
      skipComments();
      commitTrail(childPath);
      if (p.k >= toks.size()) return;
      if (toks[p.k].kind == ',') {
        p.k++;
        skipComments();
        commitTrail(childPath);
        if (p.k < toks.size() && toks[p.k].kind == ']') { commitClosing(path); p.k++; return; }
        idx++;
        continue;
      }
      if (toks[p.k].kind == ']') { commitClosing(path); p.k++; return; }
      return;
    }
  };

  if (!toks.empty()) parseValue("");
  skipComments();
  if (!p.pendLead.empty() || !p.pendTrail.empty()) {
    Json5CommentEntry e;
    e.footing = true;
    if (!p.pendLead.empty()) e.leading.swap(p.pendLead);
    if (!p.pendTrail.empty()) e.trailing.swap(p.pendTrail);
    p.out.push_back(std::move(e));
  }
  return p.out;
}

inline void json5SerializeVal(const asvJSONValue* v, std::string& out, int depth = 0, bool pretty = false);

// Serialize a value plus its same-line trailing comment. When `commaNeeded`
// and the trailing is a `//` comment (which swallows everything to end-of-line),
// the separating comma is emitted BEFORE the comment so the next entry starts
// fresh on its own line with no leading comma. Mirrors the RON encoder.
// Returns true when the output ended with a newline.
inline bool json5EmitValueWithTrailing(const asvJSONValue* v, std::string& out, int depth, bool pretty, bool commaNeeded) {
  json5SerializeVal(v, out, depth, pretty);
  if (!v->comments || v->comments->trailing.empty()) return false;
  const std::vector<Comment>& list = v->comments->trailing;
  bool sawLine = false;
  for (size_t i = 0; i < list.size(); i++) {
    const Comment& c = list[i];
    if (c.block) {
      out += " /*";
      out += c.text;
      out += "*/";
      continue;
    }
    // The separating comma belongs on the value's line, before the FIRST
    // trailing comment -- the caller skips its own comma once we report that a
    // //-comment ended the line.
    if (commaNeeded) { out += ','; commaNeeded = false; }
    // A //-comment ends its line, so only the first can share the value's line;
    // further line comments go on their own lines below it. They used to be
    // dropped by the `return true` here (reachable only from a programmatically
    // built tree -- the collector never binds two //-comments to one value).
    // Nothing is lost now: on re-read an own-line //-comment binds as a leading
    // comment of the next member (or as the container's closing comment when
    // there is no next member), so the text survives and only its placement is
    // normalized.
    out += sawLine ? "//" : " //";
    out += c.text;
    out += '\n';
    sawLine = true;
    if (i + 1 < list.size() && pretty)
      out += std::string(static_cast<size_t>(depth) * 2, ' ');
  }
  return sawLine;
}

inline void json5SerializeVal(const asvJSONValue* v, std::string& out, int depth, bool pretty) {
  if (!v) { out += "null"; return; }
  if (!asvJSONValue::checkNestingDepth(depth)) { out += "null"; return; }
  std::string pad;
  if (pretty) pad = std::string(static_cast<size_t>(depth) * 2, ' ');
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += "null"; break;
    case asvJSONValue::BOOL_VAL: out += v->flag ? "true" : "false"; break;
    case asvJSONValue::INT: out += std::to_string(v->num); break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d)) { out += (std::signbit(d) ? "-NaN" : "NaN"); break; }
      if (std::isinf(d)) { out += (d > 0 ? "Infinity" : "-Infinity"); break; }
      std::string r; fmtDoubleValDot0(d, r); out += r;
      break;
    }
    case asvJSONValue::STRING: {
      if (v->raw_number) { out.append(v->str_data.data(), v->str_data.size()); break; }
      out += '"';
      appendJsonEscaped(out, v->str_data);
      out += '"';
      break;
    }
    case asvJSONValue::OBJECT: {
      out += '{';
      if (pretty && v->obj && !v->obj->empty()) out += '\n';
      size_t i = 0;
      const size_t n = v->obj->size();
      bool prevNl = false; // previous field ended with a // comment -> comma already placed
      for (const auto& [k, child] : *(v->obj)) {
        if (i > 0 && !prevNl) {
          bool leadCmt = child->comments && !child->comments->leading.empty();
          out += (pretty || leadCmt) ? ",\n" : ",";
        }
        prevNl = false;
        if (child->comments) {
          for (const auto& c : child->comments->leading) {
            if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
            if (c.block) { out += "/*"; out += c.text; out += "*/"; }
            else { out += "//"; out += c.text; }
            out += '\n';
          }
        }
        if (pretty) { out += std::string(static_cast<size_t>(depth + 1) * 2, ' '); }
        bool needsQuote = k.empty() || k.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$0123456789") != std::string::npos || (k[0] >= '0' && k[0] <= '9');
        if (needsQuote) { out += '"'; appendJsonEscaped(out, k); out += '"'; }
        else out += k;
        out += ':';
        if (pretty) out += ' ';
        if (json5EmitValueWithTrailing(child.get(), out, depth + 1, pretty, /*commaNeeded=*/i + 1 < n))
          prevNl = true;
        i++;
      }
      if (v->comments && !v->comments->closing.empty()) {
        if (pretty) out += '\n';
        for (const auto& c : v->comments->closing) {
          if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
          else out += ", ";
          if (c.block) { out += "/*"; out += c.text; out += "*/"; }
          else { out += "//"; out += c.text; }
          if (pretty) out += '\n';
          else if (!c.block) out += '\n'; // non-pretty //-comment must not swallow the closing ']'
        }
      }
      if (pretty && v->obj && !v->obj->empty()) {
        if (out.back() != '\n') out += '\n';
        out += pad;
      }
      out += '}';
      break;
    }
    case asvJSONValue::ARRAY: {
      out += '[';
      if (pretty && v->arr && !v->arr->empty()) out += '\n';
      bool prevNl = false; // previous element ended with a // comment
      const size_t ne = v->arr ? v->arr->size() : 0;
      for (size_t i = 0; i < ne; i++) {
        if (i > 0 && !prevNl) {
          bool leadCmt = v->get(i)->comments && !v->get(i)->comments->leading.empty();
          out += (pretty || leadCmt) ? ",\n" : ",";
        }
        prevNl = false;
        if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
        if (v->get(i)->comments) {
          for (const auto& c : v->get(i)->comments->leading) {
            if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
            if (c.block) { out += "/*"; out += c.text; out += "*/"; }
            else { out += "//"; out += c.text; }
            out += '\n';
          }
        }
        if (json5EmitValueWithTrailing(v->get(i), out, depth + 1, pretty, /*commaNeeded=*/i + 1 < ne))
          prevNl = true;
      }
      if (v->comments && !v->comments->closing.empty()) {
        if (pretty) out += '\n';
          for (const auto& c : v->comments->closing) {
            if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
            else out += ", ";
            if (c.block) { out += "/*"; out += c.text; out += "*/"; }
            else { out += "//"; out += c.text; }
            if (pretty) out += '\n';
            else if (!c.block) out += '\n'; // non-pretty //-comment must not swallow the closing ']'
          }
      }
      if (pretty && v->arr && !v->arr->empty()) {
        if (out.back() != '\n') out += '\n';
        out += pad;
      }
      out += ']';
      break;
    }
    case asvJSONValue::DATETIME: {
      out += '"';
      fmtDateTimeVal(v->timestamp, v->datetime_ms, out, v->tz_offset_min);
      out += '"';
      break;
    }
    case asvJSONValue::BINARY: {
      out += '{';
      if (pretty) out += '\n';
      if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
      out += "\"$binary\":{\"base64\":\"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      out += "\",\"subType\":\"00\"}";
      if (pretty) { out += '\n'; out += pad; }
      out += '}';
      break;
    }
    case asvJSONValue::OBJECTID: {
      out += '{';
      if (pretty) out += '\n';
      if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
      out += "\"$oid\":\"";
      fmtObjectIdHexVal(v->str_data, out);
      out += "\"";
      if (pretty) { out += '\n'; out += pad; }
      out += '}';
      break;
    }
    case asvJSONValue::REGEX: {
      {
        size_t sep = v->str_data.rfind('\0');
        std::string_view pattern = (sep != std::string_view::npos) ? std::string_view(v->str_data.data(), sep) : std::string_view(v->str_data);
        std::string_view opts = (sep != std::string_view::npos) ? std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1) : std::string_view();
        out += '{';
        if (pretty) out += '\n';
        if (pretty) out += std::string(static_cast<size_t>(depth + 1) * 2, ' ');
        out += "\"$regex\":\"";
        appendJsonEscaped(out, pattern);
        out += "\"";
        if (!opts.empty()) {
          if (pretty) { out += ",\n"; out += std::string(static_cast<size_t>(depth + 1) * 2, ' '); }
          else out += ",";
          out += "\"$options\":\"";
          appendJsonEscaped(out, opts);
          out += "\"";
        }
        if (pretty) { out += '\n'; out += pad; }
        out += '}';
      }
      break;
    }
    case asvJSONValue::TIMESTAMP: {
      out += "{\"$timestamp\":{\"t\":";
      {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
        if (ec == std::errc()) out.append(buf, ptr - buf);
      }
      out += "}}";
      break;
    }
    case asvJSONValue::EXTENSION: {
      out += "{\"$binary\":{\"base64\":\"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      char hex[3];
      snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(v->ext_type));
      out += "\",\"subType\":\"";
      out += hex;
      out += "\"}}";
      break;
    }
    case asvJSONValue::CUSTOM_TAG: {
      // The same Extended-JSON convention the core JSON serializer uses
      // ({"$customTag":{"name":...,"value":...}}), which the JSON5 decoder
      // restores through the core object parser. The default branch used to
      // write a bare "null", so the tag and its payload were lost.
      out += "{\"$customTag\":{\"name\":\"";
      appendJsonEscaped(out, v->str_data);
      out += "\",\"value\":";
      // allowNaNInfinity: the JSON5 decoder accepts the NaN/Infinity literals,
      // so a non-finite double inside the tag survives as a literal.
      if (v->custom_value) v->custom_value->serialize(out, true);
      else out += "null";
      out += "}}";
      break;
    }
    default: out += "null"; break;
  }
}

inline std::string asvJSON::toJSON5(bool pretty) const {
  if (!root) return "null";
  std::string out;
  if (root->comments) {
    for (const auto& c : root->comments->leading) {
      if (c.block) { out += "/*"; out += c.text; out += "*/"; }
      else { out += "//"; out += c.text; }
      out += '\n';
    }
  }
  json5SerializeVal(root.get(), out, 0, pretty);
  if (root->comments) {
    if (!root->comments->trailing.empty()) {
      const auto& c = root->comments->trailing.front();
      out += ' ';
      if (c.block) { out += "/*"; out += c.text; out += "*/"; }
      else { out += "//"; out += c.text; }
      if (commentNeedsEol(c)) out += '\n';
    }
    for (const auto& c : root->comments->footing) {
      out += '\n';
      if (c.block) { out += "/*"; out += c.text; out += "*/"; }
      else { out += "//"; out += c.text; }
      out += '\n';
    }
  }
  return out;
}

inline bool asvJSON::fromJSON5(std::string_view input) {
  formatType = FormatType::Unknown;
  if (input.empty()) { lastError = "empty input"; return false; }
  bool prevNaN = allowNaNInfinity;
  allowNaNInfinity = true;
  try {
    std::string json = json5ToJson(input);
    bool ok = parse(std::string_view(json));
    allowNaNInfinity = prevNaN;
    if (ok) {
      formatType = FormatType::JSON5;
      auto comments = json5CollectComments(input);
      if (asvJSONValue* root = getRoot()) {
        json5AttachComments(root, comments);
      }
    }
    return ok;
  } catch (const asvJSONError& e) {
    allowNaNInfinity = prevNaN;
    lastError = e.what();
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromDotenv). Not
    // theoretical here: json5CollectComments pushes into toks and p.out,
    // json5AttachComments pushes into the node's comment vectors, and
    // json5ToJson grows `out` -- all throwing operations, unlike the value
    // factories, which use new(std::nothrow) and never throw. allowNaNInfinity
    // must be restored here too, or the object keeps the permissive flag after
    // a failed parse.
    allowNaNInfinity = prevNaN;
    lastError = e.what();
    return false;
  }
}

} // namespace asvJSONInternal
