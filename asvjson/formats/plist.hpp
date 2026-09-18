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
        if (v > 0x10FFFF || cp > 0x10FFFF) { ok = false; break; }
        cp = cp * (hex ? 16u : 10u) + v;
      }
      if (!ok || cp == 0 || cp > 0x10FFFF) { out += c; i++; continue; }
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

// Heuristic for fromAny(): an XML document declaring a <plist> root.
inline bool plistLooksLike(std::string_view s) {
  size_t p = 0;
  while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) p++;
  if (p >= s.size() || s[p] != '<') return false;
  return s.find("<plist") != std::string_view::npos;
}

inline void plistIndent(std::string& out, int indent) {
  for (int i = 0; i < indent; i++) { out += "  "; }
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
    case T::OBJECTID:
      out += pad + "<data>" + encodeBase64(v->bin_data.data(), v->bin_data.size()) + "</data>\n";
      break;
    case T::TIMESTAMP:
      out += pad + "<integer>" + std::to_string(v->timestamp) + "</integer>\n";
      break;
    case T::REGEX: {
      std::string_view pat(v->str_data);
      size_t bar = pat.find('|');
      if (bar != std::string_view::npos) pat = pat.substr(0, bar);
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

  void expect(const char* literal, const std::string& what) {
    size_t n = std::char_traits<char>::length(literal);
    if (s.size() - pos < n || s.compare(pos, n, literal) != 0) fail(what);
    pos += n;
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

  // Read raw text up to the first '</tag>'; pos advances past it.
  std::string textUntil(const std::string& tag) {
    std::string closer = "</" + tag + ">";
    size_t e = s.find(closer, pos);
    if (e == std::string_view::npos) fail("missing closing </" + tag + ">");
    std::string out(s.data() + pos, e - pos);
    pos = e + closer.size();
    return out;
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
      expect(name == "true" ? "</true>" : "</false>", "expected </" + name + ">");
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
    auto val = asvJSONValue::makeInt(l);
    if (val) val->raw_number = true;
    return val;
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
    if (clean.size() % 4 != 0) fail("invalid <data> base64 length");
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
    if (s.compare(pos, 7, "</dict>") == 0) { pos += 7; break; }
    expect("<key>", "expected <key> in <dict>");
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
    if (s.compare(pos, 8, "</array>") == 0) { pos += 8; break; }
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
    if (r.s.compare(r.pos, 6, "<plist") == 0) {
      auto[name, sc] = r.openTag();
      if (sc) throw asvJSONError("plist: empty <plist/>");
      r.skipWs();
      val = r.parseValue(0);
      r.skipWs();
      r.expect("</plist>", "expected </plist>");
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
  }
}

} // namespace asvJSONInternal