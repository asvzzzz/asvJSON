#pragma once
// Dotenv (.env) serialization/parsing for asvJSON++.
//
// Dotenv is a flat KEY=VALUE configuration format used for environment
// variables:
//
//   # comment
//   export APP_NAME="My App"
//   PORT=8080
//   DEBUG=true
//
// Bare values are type-inferred (null/bool/int/double/string) mirroring the
// INI convention for numbers, booleans and null. Additionally a full ISO-8601
// timestamp (`YYYY-MM-DDTHH:MM:SS[.fff][Z|+/-hh:mm]`) is inferred as DATETIME
// (INI infers the same full-ISO form since v1.26.x; both formats leave a bare
// date like `2024-01-01` -- too short for the ISO grammar -- a STRING).
// DATETIME is therefore LOSSLESS on re-encode: toDotenv() writes the bare ISO
// text (quoting it would make it decode as a STRING) and ms + timezone offset
// come back. A bare date like `2024-01-01` is too short for
// the parser and stays a STRING. Container braces are not INI syntax either:
// `K={}` stays the string "{}". Single-quoted values are always literal strings; double-quoted
// values process C-style escapes and may span multiple lines; a control byte
// with no short escape form (0x01-0x06, 0x0E-0x1F, 0x7F) is written as \xHH so
// no control byte ever lands raw in the file -- keys included: a key is written
// verbatim (dotenv has no quoted-key form), so a key holding a control byte is
// rejected as unencodable by both the parser and the encoder rather than
// emitted with the byte in it. Unknown escape sequences (e.g. \q) are preserved
// verbatim rather than decoded; \' is the one exception, decoded to ' as an
// input-compat alias inside double quotes (it is not "unknown"). Inline comments
// are stripped from bare values only when the '#' is preceded by space or tab
// WITHIN THE VALUE (`KEY=v # note`). The whitespace immediately after '=' is
// trimmed first, so a '#' at the start of that trimmed value is data whether or
// not the source line had a space before it: `KEY=#text` and `KEY= #text` both
// parse to the string "#text", and `KEY=#` to "#", instead of silently
// collapsing to an empty value plus a comment. Duplicate keys follow last-wins. Comment text is stored without
// the leading '#' but with its following space, so `# hello` parses to
// " hello" and re-emits as "# hello" (the writer is symmetric -- no
// progressive space drift on round-trip).
//
// Nested objects and arrays have no dotenv syntax: toDotenv() writes their
// compact JSON text (a documented lossy round-trip -- the text survives but
// decodes back as a string). BINARY and EXTENSION are written as their base64
// text and decode back as STRING. Those text forms are quoted only when a bare
// cell would re-read as a different type (a '=' / '#' / space or a numeric
// spelling), so the encoder is idempotent: decoding then re-encoding a value
// reproduces the same line.
//
// Known lossy cases, all cosmetic rather than data-affecting:
// - Blank lines are not part of the data model, so a blank line between a leading
//   comment and its key ("# c1\n\nKEY=v") is dropped on re-encode. The parser
//   discards blank lines outright; representing them would need a sentinel that
//   does not collide with a bare "#" comment (which parses to empty text) and a
//   new flag on the shared Comment struct used by every format.
// - Comments always precede the first key as whole lines, and only a value's own
//   line can hold an inline comment, so comment placement is normalized.
// - Several trailing comments on one value share the single inline slot: they are
//   emitted joined on the value's line ("K=1 # a # b") and re-parse as ONE
//   comment whose text is the whole joined string. Nothing is lost, only the
//   comment count folds (the joined text re-emits verbatim).
// - A comment whose text ends with '\n' does not survive as one comment: it is
//   written one '#'-prefixed line per text line, so "foo\n" becomes "#foo\n#\n"
//   and re-parses as TWO comments ("foo" and ""). Splitting on re-read is
//   unavoidable given the one-line-per-text-line mapping; the text is preserved,
//   the comment count changes. Round-tripping such a comment exactly would need
//   a marker that distinguishes a blank comment line from a real one.
// - The root's own trailing and closing comments have no place to go: dotenv is a
//   flat key/value list with no closing delimiter, and an inline trailing comment
//   has no final line to sit on. toDotenv() writes the root's leading and footing
//   comments and silently drops these two. Per-key leading/trailing and the root
//   footing are all preserved.
// - toDotenv() REQUIRES an OBJECT root: a scalar or array root throws
//   `dotenv: root must be an object` (a flat KEY=VALUE list has no place for a
//   bare value). Nested objects/arrays under a key degrade to JSON text, but the
//   root itself has no such spelling.

#include "../core.hpp"

#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace asvJSONInternal {

inline std::string_view dotenvTrimL(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++;
  return s.substr(i);
}
inline std::string_view dotenvTrimR(std::string_view s) {
  size_t e = s.size();
  while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
  return s.substr(0, e);
}
inline std::string_view dotenvTrim(std::string_view s) { return dotenvTrimR(dotenvTrimL(s)); }

// Infer a typed value from a bare dotenv string. With quoted=true the
// input is a string value (always STRING, no inference). Both fromDotenv's
// quoted paths and the encoder's quoting probe funnel through this one
// function, so the MAX_STRING_LEN limit lives in a single place (makeString
// returns null on overflow).
// Mirrors the INI convention: JSON-shaped numbers only, leading-zero/hex
// literals kept as text, out-of-range numbers preserved as raw_number strings.
inline std::unique_ptr<asvJSONValue> dotenvParseVal(const std::string& s, bool quoted) {
  if (quoted) return asvJSONValue::makeString(s.data(), s.size());
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
    errno = 0;
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == 0 && end == s.c_str() + s.size()) {
      if (errno == ERANGE) return asvJSONValue::makeStringView(std::string_view(s));
      return asvJSONValue::makeDouble(d);
    }
  }
  return asvJSONValue::makeString(s.data(), s.size());
}

// Process C-style escapes inside a double-quoted dotenv value. The table is
// the exact inverse of dotenvEscape: \0 \a \b \f \v \n \r \t \\ \" use short
// escapes, and every remaining control byte (0x01-0x06, 0x0E-0x1F, 0x7F) uses
// the \xHH hex form, so no control byte is ever written raw (a raw one would
// truncate the line in an external reader; INI's unescape already reads \xHH
// too, and dotenv now both reads and writes it). '$' is deliberately NOT
// decoded: this parser never expands variables, the writer never emits '\$',
// and treating '\$' as an unknown escape keeps it verbatim -- exactly like any
// other unrecognized sequence. '\'' stays as an input-compat alias inside
// double quotes.
inline int dotenvHexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline std::string dotenvUnescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      // \xHH first, mirroring INI: the switch below would otherwise decode
      // "\x41" to 'x' and leave "41" behind.
      if (s[i + 1] == 'x' && i + 3 < s.size() &&
          dotenvHexVal(s[i + 2]) >= 0 && dotenvHexVal(s[i + 3]) >= 0) {
        out += (char)((dotenvHexVal(s[i + 2]) << 4) | dotenvHexVal(s[i + 3]));
        i += 3;
        continue;
      }
      char e = s[++i];
      switch (e) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case '0': out += '\0'; break;
        case 'a': out += '\a'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'v': out += '\v'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case '\'': out += '\''; break;
        default: out += '\\'; out += e; break;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

inline std::string dotenvEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\0': out += "\\0"; break;
      case '\a': out += "\\a"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\v': out += "\\v"; break;
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      default: {
        // No short form for the remaining controls (0x01-0x06, 0x0E-0x1F) or
        // DEL (0x7F): emit them as \xHH so no control byte is ever written
        // raw. dotenvUnescape reads \xHH back.
        unsigned char uc = (unsigned char)c;
        if (uc < 0x20 || uc == 0x7f) {
          static const char hexdig[] = "0123456789ABCDEF";
          out += '\\'; out += 'x';
          out += hexdig[uc >> 4]; out += hexdig[uc & 0x0F];
        } else {
          out += c;
        }
        break;
      }
    }
  }
  return out;
}

// Keys are written VERBATIM -- there is no quoting form for a key in dotenv, so
// every key byte must be safe to emit raw. The original test excluded only the
// handful of characters the parser itself treats as structure (' ', '\t', '\r',
// '\n', '=', '#', the two quotes), which let every OTHER control byte through:
// dotenvKeyCharOk('\0') and dotenvKeyCharOk('\x01') were both true, so a key read
// from {"a\u0000b":1} put a raw NUL in the file. That contradicts the header's
// promise that "no control byte ever lands raw in the file" -- true of VALUES,
// which are \xHH-escaped, false of keys.
//
// Reject the whole C0 range plus space, and DEL. That subsumes the old ' ', '\t',
// '\r', '\n' cases; the quoting/structure cases stay. The parser rejects such a
// key the same way, so an unencodable key is a parse error rather than an
// encoder that cannot round-trip its own output.
inline bool dotenvKeyCharOk(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  if (uc <= 0x20 || uc == 0x7F) return false;   // C0 controls + space + DEL
  return c != '=' && c != '#' && c != '"' && c != '\'';
}

inline void dotenvWriteCommentLines(std::string& out, const std::vector<Comment>& list) {
  for (const auto& c : list) {
    std::string_view text(c.text);
    size_t start = 0;
    for (;;) {
      size_t nl = text.find('\n', start);
      std::string_view seg = text.substr(start, nl == std::string_view::npos ? text.size() - start : nl - start);
      out += "#";
      out += seg;
      out += '\n';
      if (nl == std::string_view::npos) break;
      start = nl + 1;
    }
  }
}

inline void dotenvFormatVal(const asvJSONValue* v, std::string& out) {
  using T = asvJSONValue::Type;
  // Emit `text` bare when a bare value re-reads as `keep`, quoted otherwise.
  // One rule for a STRING and for every value that has no dotenv syntax of its
  // own (DATETIME/BINARY/EXTENSION/containers/specials): the decoded tree holds
  // the TEXT, so it re-encodes through this same probe and must produce the
  // same bytes. The old always-quoted special branches made
  // enc(decode(enc(t))) differ from enc(t); DATETIME was needlessly lossy too,
  // because a bare ISO value re-reads as DATETIME (dotenvParseVal infers it).
  auto emitText = [&](std::string_view text, T keep, bool rawNumber) {
    bool needQuote = text.empty();
    for (char c : text) {
      // ';' is not special here (unlike INI) and earns no quotes. Control
      // bytes -- everything below 0x20 plus DEL (0x7F) -- force quoting
      // because the escape table (dotenvEscape) only runs inside quotes;
      // without this a NUL/bell/DEL would be written raw and could be
      // truncated or mangled by an external reader (DEL behaviour varies
      // across tr/sed/editors). '\n'/'\r'/'\t' are listed explicitly below
      // and also caught by the range check.
      if ((unsigned char)c < 0x20 || c == 0x7f || c == ' ' || c == '\t' ||
          c == '#' || c == '=' || c == '"' || c == '\'' || c == '\\' ||
          c == '\n' || c == '\r') {
        needQuote = true;
        break;
      }
    }
    if (!needQuote && !rawNumber) {
      auto bare = dotenvParseVal(std::string(text), false);
      if (!bare || bare->type != keep) needQuote = true;
    }
    if (needQuote) {
      out += '"';
      out += dotenvEscape(text);
      out += '"';
    } else {
      out.append(text.data(), text.size());
    }
  };
  switch (v->type) {
    case T::STRING: {
      emitText(v->str_data, T::STRING, v->raw_number);
      break;
    }
    case T::INT: out += std::to_string(v->num); break;
    case T::DOUBLE: {
      // inf/nan must be spelled out and never get the ".0" suffix: fmtDoubleVal
      // returns "inf"/"-inf"/"nan" (MSVC spells a negative nan "-nan(ind)"), none
      // of which contain '.', 'e' or 'E', so the suffix below would turn them into
      // "inf.0"/"nan.0"/"-nan(ind).0" -- strtoll stops at 'i' and strtod stops at
      // '.', so the re-parse yields a STRING and the round-trip loses the type.
      // Bare "inf"/"nan" is safe in the other direction: dotenvParseVal's strtod
      // maps it straight back to a DOUBLE, and that is also why a STRING "inf" is
      // quoted by the STRING case above (its probe reports a non-STRING type).
      if (std::isnan(v->dbl)) { out += std::signbit(v->dbl) ? "-nan" : "nan"; break; }
      if (std::isinf(v->dbl)) { out += (v->dbl < 0 ? "-inf" : "inf"); break; }
      // A double with an integral value must round-trip as a float (otherwise
      // "100000" would re-decode as an INT). Same rule as INI/ADE.
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
      // fmtDateTimeVal renders an unconvertible timestamp as the literal "null";
      // the probe then reports NULL, so it is quoted and re-reads as the STRING
      // "null" (the same degenerate-case convention as INI/TOML/CSV).
      emitText(s, T::DATETIME, false);
      break;
    }
    case T::BINARY:
    case T::EXTENSION: {
      emitText(encodeBase64(v->bin_data.data(), v->bin_data.size()), T::STRING, false);
      break;
    }
    default: {
      // Objects, arrays and other special types have no dotenv syntax: emit
      // their compact JSON text, quoted only when a bare cell would re-read as
      // something other than that text.
      std::string s;
      try {
        v->serialize(s, false);
      } catch (const std::exception& e) {
        throw asvJSONError(std::string("dotenv: cannot serialize value: ") + e.what());
      }
      emitText(s, T::STRING, false);
      break;
    }
  }
}

inline void asvJSONValue::toDotenv(std::string& out) const {
  if (type != OBJECT || !obj)
    throw asvJSONError("dotenv: root must be an object");
  // Root leading comments go above the first key. fromDotenv never produces them
  // (everything before the first key becomes the first key's leading comments),
  // so dotenv-native round-trips are unaffected, but a tree carrying root leading
  // comments from another format used to lose them. INI emits the same pair.
  if (comments && !comments->leading.empty())
    dotenvWriteCommentLines(out, comments->leading);
  for (const auto& [key, val] : *obj) {
    if (key.empty())
      throw asvJSONError("dotenv: empty key");
    for (char c : key)
      if (!dotenvKeyCharOk(c))
        throw asvJSONError("dotenv: invalid character in key \"" + key + "\"");
    if (!asvJSONValue::checkStringLen(key.size()))
      throw asvJSONError("dotenv: key too long");
    if (val->comments && !val->comments->leading.empty())
      dotenvWriteCommentLines(out, val->comments->leading);
    out += key;
    out += '=';
    dotenvFormatVal(val.get(), out);
    if (val->comments && !val->comments->trailing.empty()) {
      // Dotenv has exactly one inline comment slot per value (" # text"), so all
      // trailing comments share the value's line. Emit every one of them joined by
      // " # " instead of keeping only the last: the parser reads the whole
      // remainder of the line as one comment, so nothing is lost and the text
      // re-emits verbatim on the next pass. Dropping the earlier ones (the
      // previous rbegin()-only behavior) silently lost data on trees built
      // programmatically, where more than one trailing comment is possible.
      // A block or multi-line comment cannot share a value line, so if any is
      // present they all go on their own lines instead.
      bool allInline = true;
      for (const auto& c : val->comments->trailing)
        if (c.block || c.text.find('\n') != std::string::npos) { allInline = false; break; }
      if (allInline) {
        for (const auto& c : val->comments->trailing) {
          out += " #";
          out += c.text;
        }
        out += '\n';
      } else {
        out += '\n';
        dotenvWriteCommentLines(out, val->comments->trailing);
      }
    } else {
      out += '\n';
    }
  }
  if (comments && !comments->footing.empty())
    dotenvWriteCommentLines(out, comments->footing);
}

inline std::string asvJSON::toDotenv() const {
  std::string out;
  if (root) root->toDotenv(out);
  return out;
}

inline bool asvJSON::fromDotenv(std::string_view input) {
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  lastError.clear();
  try {
    // Strip UTF-8 BOM (EF BB BF) if present (mirrors the JSON parser) BEFORE
    // the size cap, so BOM + exactly-32 MB content is not rejected on a
    // 3-byte technicality.
    if (input.size() >= 3 && (unsigned char)input[0] == 0xEF &&
        (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
      input.remove_prefix(3);

    // Input size limit — symmetric with GOON/TOON/JSON5/YAML's MAX_*_INPUT,
    // now enforced on the BOM-stripped view.
    constexpr size_t MAX_DOTENV_INPUT = 32u * 1024u * 1024u;
    if (input.size() > MAX_DOTENV_INPUT) { lastError = "Dotenv input exceeds 32 MB limit"; return false; }

    if (input.empty()) throw asvJSONError("empty input");

    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("out of memory");

    const size_t npos = std::string_view::npos;
    std::vector<std::string> lines = splitLines(input);
    const size_t n = lines.size();
    std::vector<Comment> pending;

    for (size_t li = 0; li < n; li++) {
      std::string_view line = dotenvTrim(lines[li]);
      if (line.empty()) continue;
      if (line[0] == '#') {
        Comment c;
        c.text.assign(line.data() + 1, line.size() - 1);
        pending.push_back(std::move(c));
        continue;
      }
      std::string_view body = line;
      if (body.size() >= 7 && body.compare(0, 6, "export") == 0 &&
          (body[6] == ' ' || body[6] == '\t'))
        body = dotenvTrimL(body.substr(6));

      size_t eq = body.find('=');
      if (eq == npos)
        throw asvJSONError("dotenv: expected '=' at line " + std::to_string(li + 1));
      std::string key(dotenvTrim(body.substr(0, eq)));
      if (key.empty())
        throw asvJSONError("dotenv: empty key at line " + std::to_string(li + 1));
      for (char c : key)
        if (!dotenvKeyCharOk(c))
          throw asvJSONError("dotenv: invalid character in key at line " + std::to_string(li + 1));
      if (!asvJSONValue::checkStringLen(key.size()))
        throw asvJSONError("dotenv: key too long at line " + std::to_string(li + 1));

      std::string_view rest = dotenvTrimL(body.substr(eq + 1));
      std::unique_ptr<asvJSONValue> val;
      std::vector<Comment> trailing;

      if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) {
        char q = rest[0];
        const size_t quoteStartLine = li; // li advances inside the continuation loop
        std::string raw;
        bool closed = false;
        std::string_view cur = rest.substr(1);
        for (;;) {
          size_t i = 0;
          for (; i < cur.size(); i++) {
            char c = cur[i];
            if (q == '"' && c == '\\' && i + 1 < cur.size()) {
              raw += c;
              raw += cur[i + 1];
              i++;
              continue;
            }
            if (c == q) { closed = true; break; }
            raw += c;
          }
          if (closed) {
            std::string_view tail = dotenvTrim(cur.substr(i + 1));
            if (!tail.empty() && tail[0] != '#')
              throw asvJSONError("dotenv: trailing content after quoted value at line " +
                                 std::to_string(li + 1));
            if (!tail.empty()) {
              Comment c;
              c.text.assign(tail.data() + 1, tail.size() - 1);
              trailing.push_back(std::move(c));
            }
            break;
          }
          if (li + 1 >= n)
            throw asvJSONError("dotenv: unterminated quoted value at line " +
                               std::to_string(quoteStartLine + 1));
          raw += '\n';
          li++;
          cur = lines[li];
          // Defense in depth, mirroring INI. splitLines() already drops the CR of
          // a CRLF pair (detail/common.hpp), so for a well-formed CRLF file this
          // never fires -- measured byte-identical with and without it. It is kept
          // so the two formats read the same and so the invariant does not depend
          // on splitLines staying as it is. A CR that is NOT before a '\n' (an
          // interior one, e.g. K="a<CR>b") is not a line ending and is data, so it
          // is kept; INI keeps it too.
          if (!cur.empty() && cur.back() == '\r') cur.remove_suffix(1);
        }
        std::string text = (q == '"') ? dotenvUnescape(raw) : raw;
        val = dotenvParseVal(text, true);
        if (!val)
          throw asvJSONError("dotenv: string too long at line " + std::to_string(quoteStartLine + 1));
      } else {
        std::string_view v = rest;
        size_t cut = npos;
        // A '#' only starts an inline comment when it is preceded by space or
        // tab on the value line. A '#' at the very start of the value
        // (`KEY=#text`) is data and survives as the string "#text" instead of
        // silently collapsing into an empty value plus a comment.
        for (size_t i = 0; i < v.size(); i++) {
          if (v[i] == '#' && i > 0 && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            cut = i;
            break;
          }
        }
        std::string_view valsv = (cut == npos) ? v : v.substr(0, cut);
        valsv = dotenvTrimR(valsv);
        if (cut != npos) {
          std::string_view ct = v.substr(cut + 1);
          Comment c;
          c.text.assign(ct.data(), ct.size());
          trailing.push_back(std::move(c));
        }
        val = dotenvParseVal(std::string(valsv), false);
        if (!val)
          throw asvJSONError("dotenv: value too long at line " + std::to_string(li + 1));
      }

      if (!pending.empty() || !trailing.empty()) {
        val->comments.reset(new Comments());
        if (!pending.empty()) {
          val->comments->leading.swap(pending);
          pending.clear();
        }
        if (!trailing.empty()) val->comments->trailing = std::move(trailing);
      }

      auto it = obj->obj->find(key);
      if (it == obj->obj->end()) {
        // The 32 MB input budget fits ~8M unique 4-byte lines (`a=1\n`), so
        // the map needs the same growth cap the other direct-tree decoders
        // (CSV/INI/JavaProp/protobuf) apply before every emplace.
        if (!asvJSONValue::checkObjectSize(obj->obj->size() + 1))
          throw asvJSONError("dotenv: object too large");
        obj->obj->emplace(key, std::move(val));
      } else {
        it->second = std::move(val);
      }
    }

    if (!pending.empty()) {
      obj->comments.reset(new Comments());
      obj->comments->footing.swap(pending);
    }

    root = std::move(obj);
    formatType = FormatType::Dotenv;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromMessagePack).
    // Not theoretical here: `new Comments()` and the vector growth in splitLines
    // / pending.push_back are throwing operations, unlike the value factories,
    // which use new(std::nothrow) and so never throw.
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  }
}

} // namespace asvJSONInternal
