#pragma once
// Apple Property List (XML plist) serialization/parsing for asvJSON++.
//
// The classic XML .plist format used by Apple configuration files, Info.plist,
// and CFPreferences:
//
//   <?xml version="1.0" encoding="UTF-8"?>
//   <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
//   <plist version="1.0">
//   <dict>
//     <key>name</key>
//     <string>My App</string>
//     <key>port</key>
//     <integer>8080</integer>
//   </dict>
//   </plist>
//
// Apple value types map onto the native JSON types: <string> -> STRING,
// <integer> -> INT, <real> -> DOUBLE, <true/>/<false/> -> BOOL, <date> ->
// DATETIME, <data> -> BINARY, <dict> (alternating <key>/value) -> OBJECT and
// <array> -> ARRAY. XML comments and the optional XML declaration/DOCTYPE are
// accepted on parse and dropped on output. Tag attributes are accepted and
// ignored (the standard <plist version="1.0"> and stray attributes alike);
// the attribute run is scanned quote-aware, so a '>' inside an attribute
// value (legal in XML 1.0) does not end the tag. A
// <![CDATA[...]]> section inside a text element is read as its raw content
// (entities inside the section stay literal). The reader is deliberately
// lenient about markup inside text content: a nested tag such as
// <string>a<string>b</string> keeps its literal text instead of failing.
//
// plist has no null type: null values are written as an empty <string/> and
// read back as an empty string (a documented lossy round-trip). An empty
// STRING is written the same way -- both decode to "", so they must not use
// two different spellings (<string/> vs <string></string>). Nested
// objects/arrays are first-class. Special types degrade to their underlying
// representation: REGEX -> <string>, TIMESTAMP -> <integer>, OBJECTID and
// EXTENSION -> <data>, CUSTOM_TAG -> its payload value (tag name dropped).
//
// <data> decoding tolerates missing or stray '=' padding: the trailing '='
// run is stripped and the significant part is re-padded to a multiple of four
// (a single leftover significant character is genuinely ambiguous and is
// rejected).
//
// A C0 control byte other than tab, LF and CR has NO legal XML 1.0 spelling
// (the Char production excludes it and a character reference may not name it
// either), so toPlist() REPORTS it with an asvJSONError instead of writing it
// raw - the document would otherwise be one that no conforming parser
// accepts. The decoder stays lenient and reads such a byte back as it stands.
//
// <date> keeps the value's stored timezone offset when one is present. Apple's
// plist spec wants UTC with a trailing Z, so documents destined for Apple
// tooling should use offset-less datetimes; the offset form parses back, so
// plist round-trips are lossless. A DATETIME whose timestamp gmtimeSafe cannot
// convert renders as the literal text "null"; that is NOT a valid <date>, so it
// is written as <string>null</string> and re-reads as STRING (the INI/TOML/CSV
// convention) instead of producing a document fromPlist rejects outright.
// NaN/Infinity write as <real>nan</real> /
// <real>inf</real> and parse back through strtod -- a non-standard extension
// Apple readers will not understand, mirroring the JSON layer's
// allowNaNInfinity convention.
//
// The depth counter advances once per container level: parseValue ->
// parseDict/parseArray (same depth) -> parseValue(depth + 1). That matches
// MAX_NESTING_DEPTH exactly, so every tree the encoders accept can be parsed
// back (the old two-steps-per-level accounting halved the effective limit and
// rejected a legal 40-deep document that toPlist() had just written). On the
// encoder side the level counter is threaded separately from the output indent:
// a CUSTOM_TAG has no element of its own (its payload rides at the caller's
// indent) but still counts as one level, so a chain of nested custom_value
// nodes is bounded too (an unbounded recursion overflowed the stack).

#include "../core.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asvJSONInternal {

// Unescape XML entities in a text node: the five predefined entities plus
// numeric character references (&#NN; and &#xHH;). Unknown entities pass
// through literally.
inline std::string plistUnescape(std::string_view raw) {
  if (raw.find('&') == std::string_view::npos) return std::string(raw);
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size();) {
    char c = raw[i];
    if (c != '&') { out += c; i++; continue; }
    size_t semi = raw.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 12) { out += c; i++; continue; }
    std::string_view ent = raw.substr(i + 1, semi - i - 1);
    bool consumed = true;
    if (ent == "amp") out += '&';
    else if (ent == "lt") out += '<';
    else if (ent == "gt") out += '>';
    else if (ent == "quot") out += '"';
    else if (ent == "apos") out += '\'';
    else if (ent.size() >= 2 && ent[0] == '#') {
      unsigned cp = 0;
      bool ok = true;
      const char* digits = ent.data() + 1;
      size_t dn = ent.size() - 1;
      bool hex = (dn >= 1 && (digits[0] == 'x' || digits[0] == 'X'));
      size_t start = hex ? 1 : 0;
      if (dn - start == 0) ok = false;
      for (size_t k = start; ok && k < dn; k++) {
        char d = digits[k];
        unsigned v;
        if (d >= '0' && d <= '9') v = static_cast<unsigned>(d - '0');
        else if (hex && d >= 'a' && d <= 'f') v = static_cast<unsigned>(d - 'a' + 10);
        else if (hex && d >= 'A' && d <= 'F') v = static_cast<unsigned>(d - 'A' + 10);
        else { ok = false; break; }
        // `v` is a single digit (0-15), so only the accumulated cp can exceed
        // the scalar range here.
        if (cp > 0x10FFFF) { ok = false; break; }
        cp = cp * (hex ? 16u : 10u) + v;
      }
      if (!ok || cp == 0 || cp > 0x10FFFF) { out += c; i++; continue; }
      // A surrogate code point is not a valid Unicode scalar value: encoding it
      // would emit ill-formed UTF-8 (WTF-8). Substitute U+FFFD, as ADE does.
      if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
      std::string utf8;
      appendUtf8Codepoint(utf8, cp);
      out += utf8;
    } else {
      consumed = false;
    }
    if (!consumed) { out += c; i++; continue; }
    i = semi + 1;
  }
  return out;
}

// True when "<plist" at i really opens a <plist> element: the name has to end
// there. "<plistx>" is a different element, so the character after the name
// must terminate it (whitespace, '>' for a start tag, '/' for an empty tag).
inline bool isPlistWrapperAt(std::string_view s, size_t i) {
  if (s.compare(i, 6, "<plist") != 0 || i + 6 >= s.size()) return false;
  char c = s[i + 6];
  return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Heuristic for fromAny(): an XML document whose first real element is a
// <plist> root. A bare find("<plist") also fired on the word appearing inside
// a comment and on elements like <plistx>, so the XML prologue (BOM,
// <?...?>, <!--...-->, <!DOCTYPE...>) is skipped and the first element name is
// what decides.
inline bool plistLooksLike(std::string_view s) {
  size_t p = 0;
  // UTF-8 BOM: fromPlist strips it, so detection must tolerate it too.
  if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
      (unsigned char)s[2] == 0xBF) p = 3;
  for (;;) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) p++;
    if (p >= s.size()) return false;
    if (s.compare(p, 2, "<?") == 0) {
      size_t e = s.find("?>", p + 2);
      if (e == std::string_view::npos) return false;
      p = e + 2;
      continue;
    }
    if (s.compare(p, 4, "<!--") == 0) {
      size_t e = s.find("-->", p + 4);
      if (e == std::string_view::npos) return false;
      p = e + 3;
      continue;
    }
    if (s.compare(p, 9, "<!DOCTYPE") == 0) {
      size_t q = p + 9;
      int brackets = 0;
      for (; q < s.size(); q++) {
        if (s[q] == '[') brackets++;
        else if (s[q] == ']') brackets--;
        else if (s[q] == '>' && brackets <= 0) break;
      }
      if (q >= s.size()) return false;
      p = q + 1;
      continue;
    }
    return isPlistWrapperAt(s, p);
  }
}

// Write a single value element (indent already applied except for the dict
// <key> lines, which are emitted by the OBJECT case directly). `depth` counts
// nesting LEVELS, which is not always the same as `indent`: a CUSTOM_TAG has no
// element of its own, so its payload is written at the caller's indent, yet the
// tag still occupies one level (core serialize()/appendJsonToken and the
// MessagePack/CBOR encoders count it the same way). Keeping the two separate is
// what bounds the recursion - sharing one counter let a chain of nested
// custom_value nodes recurse without limit and overflow the stack.
inline void plistWriteVal(const asvJSONValue* v, std::string& out, int indent, int depth) {
  using T = asvJSONValue::Type;
  const std::string pad(static_cast<size_t>(indent * 2), ' ');
  switch (v->type) {
    case T::STRING:
      // An empty STRING self-closes exactly like NULL (which plist renders as
      // <string/>). Both decode back as "", so they must produce the SAME text:
      // writing "" as <string></string> made enc(decode(enc(null))) differ from
      // enc(null) even though the tree was unchanged.
      if (v->str_data.empty()) out += pad + "<string/>\n";
      else out += pad + "<string>" + xmlEscapeContent(v->str_data) + "</string>\n";
      break;
    case T::INT:
      out += pad + "<integer>" + std::to_string(v->num) + "</integer>\n";
      break;
    case T::DOUBLE: {
      // Non-finite values have their own spelling: fmtDoubleVal renders them as
      // "nan"/"inf"/"-inf" (no '.', 'e' or 'E'), so the integral-suffix rule
      // below would turn them into "nan.0", which strtod rejects ("nan.0"
      // parses "nan" and stops at the '.'). Write them bare, like the INI
      // encoder, and let the parser's strtod bring them back. The header
      // promises <real>nan</real>/<real>inf</real> and this keeps it true.
      if (std::isnan(v->dbl)) { out += pad + "<real>nan</real>\n"; break; }
      if (std::isinf(v->dbl)) {
        out += pad + "<real>" + (v->dbl < 0 ? "-inf" : "inf") + "</real>\n";
        break;
      }
      std::string tmp;
      fmtDoubleVal(v->dbl, tmp);
      if (tmp.find('.') == std::string::npos && tmp.find('e') == std::string::npos &&
          tmp.find('E') == std::string::npos)
        tmp += ".0";
      out += pad + "<real>" + xmlEscapeContent(tmp) + "</real>\n";
      break;
    }
    case T::BOOL_VAL:
      out += pad;
      out += v->flag ? "<true/>\n" : "<false/>\n";
      break;
    case T::NULL_VAL:
      out += pad + "<string/>\n";
      break;
    case T::DATETIME: {
      std::string s;
      fmtDateTimeVal(v->timestamp, v->datetime_ms, s, v->tz_offset_min);
      // A timestamp gmtimeSafe cannot convert renders as the literal text
      // "null". Emitted as <date>null</date> the document could not be read
      // back at all ("plist: invalid <date>"), so write it as a <string>: the
      // text survives and re-reads as STRING - the same convention INI/TOML/
      // CSV/dotenv use for this degenerate case.
      if (s == "null") out += pad + "<string>null</string>\n";
      else out += pad + "<date>" + xmlEscapeContent(s) + "</date>\n";
      break;
    }
    case T::BINARY:
    case T::EXTENSION:
      out += pad + "<data>" + encodeBase64(v->bin_data.data(), v->bin_data.size()) + "</data>\n";
      break;
    case T::OBJECTID:
      out += pad + "<data>" + encodeBase64(reinterpret_cast<const uint8_t*>(v->str_data.data()), v->str_data.size()) + "</data>\n";
      break;
    case T::TIMESTAMP:
      out += pad + "<integer>" + std::to_string(v->num) + "</integer>\n";
      break;
    case T::REGEX: {
      // str_data is the canonical "pattern\0options" form (see makeRegex), so
      // the separator is NUL. Splitting on '|' instead truncated every pattern
      // containing an alternation ("^(a|b)$" -> "^(a") and, when options were
      // absent, copied the trailing NUL into the XML text. plist has no regex
      // type, so options are dropped and the pattern is carried as a <string>.
      std::string_view full(v->str_data);
      size_t sep = full.rfind('\0');
      std::string_view pat = (sep != std::string_view::npos) ? full.substr(0, sep) : full;
      out += pad + "<string>" + xmlEscapeContent(pat) + "</string>\n";
      break;
    }
    case T::CUSTOM_TAG:
      // plist has no tag element: the payload is written in place (same indent),
      // but the tag still consumes a nesting level. Without this check the
      // recursion followed custom_value forever - a 2000-deep chain overflowed
      // the stack in toPlist() (same class as the toCpp()/JSON5 encoder guards).
      if (!asvJSONValue::checkNestingDepth(depth + 1)) {
        out += pad + "<string/>\n"; // degrade to null at limit
        break;
      }
      if (v->custom_value) plistWriteVal(v->custom_value.get(), out, indent, depth + 1);
      else out += pad + "<string/>\n";
      break;
    case T::ARRAY: {
      // Core parse() uses ++parseDepth before check (1-based), the encoder level
      // counter is 0-based and increments per container, so the child check is
      // depth + 1. For dict/array depth and indent advance together, so the
      // emitted-container limit (48) is unchanged by threading depth.
      if (!asvJSONValue::checkNestingDepth(depth + 1)) {
        out += pad + "<string/>\n"; // degrade to null at limit
        break;
      }
      out += pad + "<array>\n";
      // `obj`/`arr` are guaranteed non-null by the factories; the guard is
      // defense in depth for a hand-built node (same convention as the TOON
      // and S-expression encoders) and renders it as the empty container.
      if (v->arr) for (const auto& item : *v->arr) plistWriteVal(item.get(), out, indent + 1, depth + 1);
      out += pad + "</array>\n";
      break;
    }
    case T::OBJECT: {
      if (!asvJSONValue::checkNestingDepth(depth + 1)) {
        out += pad + "<string/>\n"; // degrade to null at limit
        break;
      }
      out += pad + "<dict>\n";
      if (v->obj) for (const auto& [k, val] : *v->obj) {
        out += pad + "  <key>" + xmlEscapeContent(k) + "</key>\n";
        plistWriteVal(val.get(), out, indent + 1, depth + 1);
      }
      out += pad + "</dict>\n";
      break;
    }
    default:
      throw asvJSONError("plist: unsupported value type");
  }
}

inline void asvJSONValue::toPlist(std::string& out) const {
  out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
  out += "<plist version=\"1.0\">\n";
  plistWriteVal(this, out, 0, 0);
  out += "</plist>\n";
}

inline std::string asvJSON::toPlist() const {
  std::string out;
  if (root) root->toPlist(out);
  return out;
}

// Push-down XML-syntax reader for the plist value grammar. Text between tags
// is read verbatim (entities are unescaped on consumption).
struct PlistReader {
  std::string_view s;
  size_t pos = 0;

  PlistReader(std::string_view input, size_t p) : s(input), pos(p) {}

  [[noreturn]] void fail(const std::string& msg) const { throw asvJSONError("plist: " + msg); }

  static bool wsChar(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

  bool atEnd() const { return pos >= s.size(); }

  // Skip whitespace and <!-- --> comments.
  void skipWs() {
    for (;;) {
      if (atEnd()) return;
      if (wsChar(s[pos])) { pos++; continue; }
      if (s.compare(pos, 4, "<!--") == 0) {
        size_t e = s.find("-->", pos + 4);
        if (e == std::string_view::npos) fail("unterminated comment");
        pos = e + 3;
        continue;
      }
      return;
    }
  }

  // At '<': read the tag name and optional attributes, return {name,
  // selfClosing}; pos is advanced past '>' (or '/>'). The attribute run is
  // scanned QUOTE-AWARE: '>' is legal inside an XML attribute VALUE
  // (XML 1.0 s2.3 - only '<' and '&' must be escaped there), so
  // <plist version="1.0" note="a>b"> must not end the tag at the '>' inside
  // the quotes (the old plain find('>') left the tail "b\">" as content and
  // the parse died with "expected '<'").
  std::pair<std::string, bool> openTag() {
    if (atEnd() || s[pos] != '<') fail("expected '<'");
    size_t p = pos + 1;
    while (p < s.size() && !wsChar(s[p]) && s[p] != '>' && s[p] != '/') p++;
    if (p == pos + 1) fail("empty tag name");
    std::string name(s.data() + pos + 1, p - pos - 1);
    size_t q = p;
    char quote = 0;
    while (q < s.size()) {
      char c = s[q];
      if (quote) { if (c == quote) quote = 0; }
      else if (c == '"' || c == '\'') quote = c;
      else if (c == '>') break;
      q++;
    }
    if (q >= s.size()) fail(quote ? "unterminated attribute quote in tag <" + name + ">"
                                  : "unterminated tag <" + name + ">");
    bool selfClose = false;
    size_t check = q;
    while (check > p && wsChar(s[check - 1])) check--;
    if (check > p && s[check - 1] == '/') selfClose = true;
    pos = q + 1;
    return {std::move(name), selfClose};
  }

  // Consume "</name>" if that is the text at pos, tolerating whitespace before
  // the '>' the way openTag() does ("</dict >" is valid XML). Returns false
  // when a different tag (or a longer name such as "</dictx>") is there.
  bool acceptClose(std::string_view name) {
    if (pos + 2 + name.size() > s.size() || s[pos] != '<' || s[pos + 1] != '/') return false;
    if (s.compare(pos + 2, name.size(), name) != 0) return false;
    size_t q = pos + 2 + name.size();
    while (q < s.size() && wsChar(s[q])) q++;
    if (q >= s.size() || s[q] != '>') return false;
    pos = q + 1;
    return true;
  }

  // Read the raw text up to the matching '</tag>' and advance past it. XML
  // comments inside the text are skipped -- they are markup, not part of the
  // value -- and whitespace before the closing '>' is tolerated. Entities are
  // unescaped on every ordinary run of characters, but the content of a CDATA
  // section is character data and stays literal (XML 1.0 s2.7). Callers
  // receive already-unescaped text and MUST NOT apply plistUnescape again.
  std::string textUntil(std::string_view tag) {
    std::string out;
    std::string raw;
    size_t i = pos;
    auto flushRaw = [&]() {
      if (!raw.empty()) {
        out += plistUnescape(raw);
        raw.clear();
      }
    };
    for (;;) {
      if (i >= s.size()) fail("missing closing </" + std::string(tag) + ">");
      if (s.compare(i, 4, "<!--") == 0) {
        size_t e = s.find("-->", i + 4);
        if (e == std::string_view::npos) fail("unterminated comment");
        i = e + 3;
        continue;
      }
      // A CDATA section is raw text: consume it whole minus the
      // <![CDATA[ ... ]]> delimiters. "]]>" ends a section, so the closing tag
      // may appear verbatim inside the content, and entities inside the
      // section pass through unescaped.
      if (s.compare(i, 9, "<![CDATA[") == 0) {
        size_t e = s.find("]]>", i + 9);
        if (e == std::string_view::npos) fail("unterminated CDATA section");
        flushRaw();
        out.append(s.data() + i + 9, e - (i + 9));
        i = e + 3;
        continue;
      }
      if (i + 2 < s.size() && s[i] == '<' && s[i + 1] == '/' &&
          s.compare(i + 2, tag.size(), tag) == 0) {
        size_t q = i + 2 + tag.size();
        while (q < s.size() && wsChar(s[q])) q++;
        if (q < s.size() && s[q] == '>') {
          pos = q + 1;
          flushRaw();
          return out;
        }
      }
      raw += s[i];
      i++;
    }
  }

  static std::string_view trimSv(std::string_view t) {
    size_t b = 0, n = t.size();
    while (b < n && (t[b] == ' ' || t[b] == '\t' || t[b] == '\r' || t[b] == '\n')) b++;
    while (n > b && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n')) n--;
    return t.substr(b, n - b);
  }

  std::unique_ptr<asvJSONValue> parseValue(int depth);
  std::unique_ptr<asvJSONValue> parseDict(int depth);
  std::unique_ptr<asvJSONValue> parseArray(int depth);
};

inline std::unique_ptr<asvJSONValue> PlistReader::parseValue(int depth) {
  // No top-level depth check here: scalars at depth == MAX are legal (core
  // parse() does not increment depth for leaf values). The container checks
  // below enforce the limit only when a dict/array is encountered.
  skipWs();
  if (atEnd() || s[pos] != '<') fail("expected element");
  auto [name, sc] = openTag();

  if (name == "true" || name == "false") {
    if (!sc) {
      skipWs();
      if (!acceptClose(name)) fail("expected </" + name + ">");
    }
    return asvJSONValue::makeBool(name == "true");
  }

  if (name == "string") {
    std::string t = sc ? std::string() : textUntil("string");
    if (!asvJSONValue::checkStringLen(t.size())) fail("string too long");
    return asvJSONValue::makeString(t.data(), t.size());
  }

  if (name == "integer") {
    std::string t = sc ? std::string() : textUntil("integer");
    std::string_view v = trimSv(t);
    if (v.empty()) fail("empty <integer>");
    std::string tmp(v);
    errno = 0;
    char* end = nullptr;
    long long l = std::strtoll(tmp.c_str(), &end, 10);
    if (errno == ERANGE || end == tmp.c_str() || *end != '\0') fail("invalid <integer>");
    return asvJSONValue::makeInt(l);
  }

  if (name == "real") {
    std::string t = sc ? std::string() : textUntil("real");
    std::string_view v = trimSv(t);
    if (v.empty()) fail("empty <real>");
    std::string tmp(v);
    errno = 0;
    char* end = nullptr;
    double d = std::strtod(tmp.c_str(), &end);
    if (errno == ERANGE || end == tmp.c_str() || *end != '\0') fail("invalid <real>");
    return asvJSONValue::makeDouble(d);
  }

  if (name == "date") {
    std::string t = sc ? std::string() : textUntil("date");
    std::string_view v = trimSv(t);
    time_t ts = 0;
    int ms = 0;
    int tz = 0;
    if (!tryParseDateTime(v, ts, &ms, &tz)) fail("invalid <date>");
    return asvJSONValue::makeDateTime(ts, ms, tz);
  }

  if (name == "data") {
    std::string t = sc ? std::string() : textUntil("data");
    std::string clean;
    clean.reserve(t.size());
    for (char c : t) {
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') clean += c;
    }
    // decodeBase64Fast needs a length that is a multiple of 4; hand-written
    // plists routinely omit the trailing '=', so re-pad the significant part
    // instead of rejecting them. Stray '=' already present in the input are
    // stripped first, so the re-pad lands on the true significant length --
    // piling new '=' on top of existing ones would corrupt the length (e.g.
    // "ab=" -> "ab===") and wrongly reject over-padded manual plists.
    if (clean.size() % 4 != 0) {
      size_t sig = clean.size();
      while (sig > 0 && clean[sig - 1] == '=') sig--;
      size_t rem = sig % 4;
      if (rem == 1) fail("invalid <data> base64 length");
      clean.resize(sig);
      clean.append((4 - rem) % 4, '=');
    }
    bool err = false;
    auto bytes = decodeBase64Fast(clean.data(), clean.size(), &err);
    if (err) fail("invalid <data> base64");
    if (bytes.size() > asvJSONValue::MAX_STRING_LEN) fail("data too large");
    return asvJSONValue::makeBinary(bytes.data(), bytes.size());
  }

  if (name == "dict") {
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) fail("nesting too deep");
    return sc ? asvJSONValue::makeObject() : parseDict(depth);
  }

  if (name == "array") {
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) fail("nesting too deep");
    return sc ? asvJSONValue::makeArray() : parseArray(depth);
  }

  fail("unexpected element <" + name + ">");
}

inline std::unique_ptr<asvJSONValue> PlistReader::parseDict(int depth) {
  auto obj = asvJSONValue::makeObject();
  if (!obj) fail("out of memory");
  for (;;) {
    skipWs();
    if (atEnd()) fail("unterminated <dict>");
    if (acceptClose("dict")) break;
    // openTag() tolerates valid-XML whitespace/attributes ("<key >", "<key x=1>")
    // that the old exact-string expect("<key>", ...) rejected.
    {
      auto [ktag, ksc] = openTag();
      if (ktag != "key" || ksc) fail("expected <key> in <dict>");
    }
    std::string k = textUntil("key");
    if (!asvJSONValue::checkStringLen(k.size())) fail("key too long");
    if (obj->obj->size() >= asvJSONValue::MAX_OBJECT_SIZE) fail("object too large");
    skipWs();
    auto v = parseValue(depth + 1);
    if (!v) fail("out of memory");
    auto it = obj->obj->find(k);
    if (it == obj->obj->end()) obj->obj->emplace(k, std::move(v));
    else it->second = std::move(v);
  }
  return obj;
}

inline std::unique_ptr<asvJSONValue> PlistReader::parseArray(int depth) {
  auto arr = asvJSONValue::makeArray();
  if (!arr) fail("out of memory");
  for (;;) {
    skipWs();
    if (atEnd()) fail("unterminated <array>");
    if (acceptClose("array")) break;
    if (arr->arr->size() >= asvJSONValue::MAX_ARRAY_SIZE) fail("array too large");
    auto v = parseValue(depth + 1);
    if (!v) fail("out of memory");
    arr->arr->push_back(std::move(v));
  }
  return arr;
}

inline bool asvJSON::fromPlist(std::string_view input) {
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  lastError.clear();
  // Input size limit — symmetric with GOON/TOON/JSON5/YAML's MAX_*_INPUT.
  constexpr size_t MAX_PLIST_INPUT = 32u * 1024u * 1024u;
  if (input.size() > MAX_PLIST_INPUT) { lastError = "Plist input exceeds 32 MB limit"; return false; }
  try {
    // Strip UTF-8 BOM (EF BB BF) if present (mirrors the JSON parser).
    if (input.size() >= 3 && (unsigned char)input[0] == 0xEF &&
        (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
      input = input.substr(3);

    if (input.empty()) throw asvJSONError("empty input");

    PlistReader r(input, 0);
    r.skipWs();

    // Optional XML declaration: <?xml ... ?>
    if (r.s.compare(r.pos, 2, "<?") == 0) {
      size_t e = r.s.find("?>", r.pos + 2);
      if (e == std::string_view::npos) throw asvJSONError("plist: unterminated XML declaration");
      r.pos = e + 2;
      r.skipWs();
    }

    // Optional DOCTYPE: <!DOCTYPE ...> (may contain a bracketed internal subset).
    if (r.s.compare(r.pos, 9, "<!DOCTYPE") == 0) {
      size_t q = r.pos + 9;
      int brackets = 0;
      for (; q < r.s.size(); q++) {
        if (r.s[q] == '[') brackets++;
        else if (r.s[q] == ']') brackets--;
        else if (r.s[q] == '>' && brackets <= 0) break;
      }
      if (q >= r.s.size()) throw asvJSONError("plist: unterminated DOCTYPE");
      r.pos = q + 1;
      r.skipWs();
    }

    std::unique_ptr<asvJSONValue> val;
    if (isPlistWrapperAt(r.s, r.pos)) {
      auto[name, sc] = r.openTag();
      if (sc) throw asvJSONError("plist: empty <plist/>");
      r.skipWs();
      val = r.parseValue(0);
      if (!val) throw asvJSONError("plist: out of memory");
      r.skipWs();
      if (!r.acceptClose("plist")) r.fail("expected </plist>");
      r.skipWs();
      if (!r.atEnd()) throw asvJSONError("plist: trailing content after </plist>");
    } else {
      // Lenient: a bare plist value without the <plist> wrapper.
      val = r.parseValue(0);
      if (!val) throw asvJSONError("plist: out of memory");
      r.skipWs();
      if (!r.atEnd()) throw asvJSONError("plist: trailing content");
    }

    root = std::move(val);
    formatType = FormatType::Plist;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromMessagePack).
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  }
}

} // namespace asvJSONInternal