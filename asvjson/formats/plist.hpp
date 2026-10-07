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
// accepted on parse and dropped on output.
//
// plist has no null type: null values are written as an empty <string/> and
// read back as an empty string (a documented lossy round-trip). Nested
// objects/arrays are first-class. Special types degrade to their underlying
// representation: REGEX -> <string>, TIMESTAMP -> <integer>, OBJECTID and
// EXTENSION -> <data>, CUSTOM_TAG -> its payload value (tag name dropped).
//
// <data> decoding tolerates missing or stray '=' padding: the trailing '='
// run is stripped and the significant part is re-padded to a multiple of four
// (a single leftover significant character is genuinely ambiguous and is
// rejected).
//
// <date> keeps the value's stored timezone offset when one is present. Apple's
// plist spec wants UTC with a trailing Z, so documents destined for Apple
// tooling should use offset-less datetimes; the offset form parses back, so
// plist round-trips are lossless. NaN/Infinity write as <real>nan</real> /
// <real>inf</real> and parse back through strtod -- a non-standard extension
// Apple readers will not understand, mirroring the JSON layer's
// allowNaNInfinity convention.
//
// parseValue recurses two depth steps per container level (<dict>/<array>
// cost parseValue -> parseDict/parseArray -> parseValue), so
// MAX_NESTING_DEPTH is effectively halved for plist documents. Conservative,
// never exceeded.

#include "../core.hpp"

#include <cerrno>
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
      // would emit ill-formed UTF-8 (WTF-8). Substitute U+FFFD, as UDE does.
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
// <key> lines, which are emitted by the OBJECT case directly).
inline void plistWriteVal(const asvJSONValue* v, std::string& out, int indent) {
  using T = asvJSONValue::Type;
  const std::string pad(static_cast<size_t>(indent * 2), ' ');
  switch (v->type) {
    case T::STRING:
      out += pad + "<string>" + xmlEscapeContent(v->str_data) + "</string>\n";
      break;
    case T::INT:
      out += pad + "<integer>" + std::to_string(v->num) + "</integer>\n";
      break;
    case T::DOUBLE: {
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
      out += pad + "<date>" + xmlEscapeContent(s) + "</date>\n";
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
      if (v->custom_value) plistWriteVal(v->custom_value.get(), out, indent);
      else out += pad + "<string/>\n";
      break;
    case T::ARRAY: {
      out += pad + "<array>\n";
      for (const auto& item : *v->arr) plistWriteVal(item.get(), out, indent + 1);
      out += pad + "</array>\n";
      break;
    }
    case T::OBJECT: {
      out += pad + "<dict>\n";
      for (const auto& [k, val] : *v->obj) {
        out += pad + "  <key>" + xmlEscapeContent(k) + "</key>\n";
        plistWriteVal(val.get(), out, indent + 1);
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
  plistWriteVal(this, out, 0);
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
  // selfClosing}; pos is advanced past '>' (or '/>').
  std::pair<std::string, bool> openTag() {
    if (atEnd() || s[pos] != '<') fail("expected '<'");
    size_t p = pos + 1;
    while (p < s.size() && !wsChar(s[p]) && s[p] != '>' && s[p] != '/') p++;
    if (p == pos + 1) fail("empty tag name");
    std::string name(s.data() + pos + 1, p - pos - 1);
    size_t q = p;
    while (q < s.size() && s[q] != '>') q++;
    if (q >= s.size()) fail("unterminated tag <" + name + ">");
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

  // Read raw text up to the matching '</tag>' and advance past it. XML
  // comments inside the text are skipped -- they are markup, not part of the
  // value -- and whitespace before the closing '>' is tolerated.
  std::string textUntil(std::string_view tag) {
    std::string out;
    size_t i = pos;
    for (;;) {
      if (i >= s.size()) fail("missing closing </" + std::string(tag) + ">");
      if (s.compare(i, 4, "<!--") == 0) {
        size_t e = s.find("-->", i + 4);
        if (e == std::string_view::npos) fail("unterminated comment");
        i = e + 3;
        continue;
      }
      if (i + 2 < s.size() && s[i] == '<' && s[i + 1] == '/' &&
          s.compare(i + 2, tag.size(), tag) == 0) {
        size_t q = i + 2 + tag.size();
        while (q < s.size() && wsChar(s[q])) q++;
        if (q < s.size() && s[q] == '>') { pos = q + 1; return out; }
      }
      out += s[i];
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
  if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) fail("nesting too deep");
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
    std::string t = sc ? std::string() : plistUnescape(textUntil("string"));
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

  if (name == "dict") return sc ? asvJSONValue::makeObject() : parseDict(depth + 1);

  if (name == "array") return sc ? asvJSONValue::makeArray() : parseArray(depth + 1);

  fail("unexpected element <" + name + ">");
}

inline std::unique_ptr<asvJSONValue> PlistReader::parseDict(int depth) {
  auto obj = asvJSONValue::makeObject();
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
    std::string k = plistUnescape(textUntil("key"));
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
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    if (input.size() >= 3 && (unsigned char)input[0] == 0xEF &&
        (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
      input = input.substr(3);

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
      r.skipWs();
      if (!r.acceptClose("plist")) r.fail("expected </plist>");
      r.skipWs();
      if (!r.atEnd()) throw asvJSONError("plist: trailing content after </plist>");
    } else {
      // Lenient: a bare plist value without the <plist> wrapper.
      val = r.parseValue(0);
      r.skipWs();
      if (!r.atEnd()) throw asvJSONError("plist: trailing content");
    }

    root = std::move(val);
    formatType = FormatType::Plist;
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