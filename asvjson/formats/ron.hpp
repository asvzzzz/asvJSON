#pragma once
// RON (Rusty Object Notation) serialization/parsing for asvJSON++
//
// RON is a configuration/data format with Rust-like syntax (the format used by
// the `ron` crate, https://docs.rs/ron). Supported subset covers the full RON
// value grammar:
//   Structs:    ( key: value, key2: ( nested: 3 ) )
//   Maps:       { "arbitrary": "keys", "are": "allowed" }
//   Lists:      [1, 2, 3]
//   Tuples:     (10, 20, 30)
//   Named:      GameConfig( width: 800, height: 600 )  ->  {"GameConfig": {...}}
//   Optionals:  Some(1) / None  -> 1 / null
//   Enum unit variants:  Up, Down  ->  strings
//   Numbers:    42, 1_000, 0xFF, 0o17, 0b1010, 3.14, .5, 5., 1e6,
//               suffixes i8..i128 / u8..u128 / f32 / f64 (stripped),
//               inf / -inf / NaN
//   Strings:    "escapes \n \t \x41 \u{1F600}"  (one-line continuation)
//   Raw:        r"literal", r#"no \escape"# , br##"bytes"##
//   Chars:      'a', '\n', b'Z'
//   Byte strings: b"bytes"
//   Ranges:     3..5, ..=7.3, 3.., .., ..=   (JSON has no range type, so a
//               range round-trips as {"$range":{"start":...,"end":...,"inclusive":...}})
//   Comments:   // line, nested /* block */, #![enable(...)] headers
//   Trailing commas: allowed in all containers
//   Limitations: br'c' raw byte chars are not supported (only b'c'); r#foo
//   raw identifiers decode to their bare ident text and re-encode as quoted
//   strings, so round-trip text differs though the value is preserved.
//
// JSON mapping:
//   () empty tuple -> {} empty object; None -> null; Some(x) -> x; bare idents
//   and enum variations -> strings. Named structs/tuples and qualified variants
//   keep their name as a JSON key: Name(...) -> {"Name": <group>},
//   Type::Variant(...) -> {"Type::Variant": <group>}. Special types round-trip
//   through the (Extended JSON) marker objects the JSON parser reconstructs:
//   {"$binary":...},
//   {"$oid":"..."}, {"$regex":..., "$options":...}, {"$timestamp":{"t":...}},
//   {"$customTag":{"name":...,"value":...}}, {"$set":[...]} (YAML sets).
//
// Decoder:  RON  ->  intermediate JSON  ->  parse() via the existing JSON parser.
// Encoder:  direct tree walk. A single-key object whose value is a group
// ({"GameConfig": {...}}) round-trips as its named form `GameConfig(...)`.

#include "../core.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace asvJSONInternal {

inline bool ronIsIdentStart(char c) noexcept {
  auto u = static_cast<unsigned char>(c);
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u >= 0x80;
}

inline bool ronIsIdentCont(char c) noexcept {
  auto u = static_cast<unsigned char>(c);
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
         (u >= '0' && u <= '9') || u == '_' || u >= 0x80;
}

inline int ronHexVal(char c) noexcept {
  auto u = static_cast<unsigned char>(c);
  if (u >= '0' && u <= '9') return u - '0';
  if (u >= 'a' && u <= 'f') return u - 'a' + 10;
  if (u >= 'A' && u <= 'F') return u - 'A' + 10;
  return -1;
}

// Reads the four hex digits of a brace-less `\uXXXX` escape starting at `p` and
// returns the code point, advancing `p` past them. Exactly four: the Rust-style
// short form is fixed-width, and a greedy read swallowed the following hex-looking
// text (`\u0041B` used to decode to U+041B, silently dropping the `B`). Code
// points above U+FFFF need `\u{...}` or a surrogate pair.
inline unsigned long ronReadU4(std::string_view in, size_t& p, const char* err) {
  if (p + 4 > in.size()) throw asvJSONError(err);
  unsigned long cp = 0;
  for (int d = 0; d < 4; ++d) {
    int v = ronHexVal(in[p + static_cast<size_t>(d)]);
    if (v < 0) throw asvJSONError(err);
    cp = (cp << 4) | static_cast<unsigned long>(v);
  }
  p += 4;
  return cp;
}

inline void ronAppendUtf8(std::string& s, unsigned long cp) noexcept {
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
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

inline std::string ronJsonStr(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  out += '"';
  appendJsonEscaped(out, s);
  out += '"';
  return out;
}

// A comment (or group of comments) collected during RON parsing, keyed by the
// JSON path of the node it belongs to. Path format matches yamlFindPath:
// "" root, "a\x1Fb\x1F[2]" nested keys / array indices.
struct RonCommentEntry {
  std::string path;
  std::vector<Comment> leading;
  std::vector<Comment> trailing;
  std::vector<Comment> closing;
  bool footing = false;
};

// Append a path segment ('\x1F'-separated) with no leading separator for the
// first segment (matches yamlFindPath convention: ""->"key"->"key\x1F[2]").
inline std::string ronJoinPath(const std::string& path, const std::string& seg) {
  if (path.empty()) return seg;
  return path + std::string("\x1F") + seg;
}

// Resolve a comment path against the parsed tree (mirrors yamlFindPath so ron.hpp
// does not depend on the YAML format being enabled).
inline asvJSONValue* ronFindPath(asvJSONValue* root, std::string_view path) {
  if (path.empty()) return root;
  asvJSONValue* cur = root;
  size_t pos = 0;
  while (pos <= path.size()) {
    size_t sep = path.find_first_of("\x1F", pos);
    std::string_view seg = path.substr(pos, sep == std::string_view::npos ? std::string_view::npos : sep - pos);
    if (seg.empty()) return nullptr;
    if (seg[0] == '[') {
      if (cur->type != asvJSONValue::ARRAY || !cur->arr || seg.size() < 3 || seg.back() != ']') return nullptr;
      size_t idx = 0;
      for (size_t k = 1; k + 1 < seg.size(); k++) {
        char d = seg[k];
        if (d < '0' || d > '9') return nullptr;
        idx = idx * 10 + static_cast<size_t>(d - '0');
      }
      if (idx >= cur->arr->size()) return nullptr;
      cur = (*cur->arr)[idx].get();
    } else {
      if (cur->type != asvJSONValue::OBJECT || !cur->obj) return nullptr;
      auto it = (*cur->obj).find(seg);
      if (it == cur->obj->end()) return nullptr;
      cur = it->second.get();
    }
    if (sep == std::string_view::npos) break;
    pos = sep + 1;
  }
  return cur;
}

struct RonParser {
  std::string_view in;
  size_t i = 0;

  // Comment collection state (see skipWsFrom). Pending comments are committed
  // to node paths by the container parsers.
  std::vector<RonCommentEntry>& comments;
  std::vector<Comment> pendLead;
  std::vector<Comment> pendTrail;

  RonParser(std::string_view input, size_t start, std::vector<RonCommentEntry>& out)
    : in(input), i(start), comments(out) {}

  size_t skipWsFrom(size_t p, bool collect = true) {
    while (p < in.size()) {
      char c = in[p];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++p; continue; }
      if (c == '/' && p + 1 < in.size()) {
        if (in[p + 1] == '/') {
          size_t mark = p;
          size_t start = p + 2;
          while (p < in.size() && in[p] != '\n') ++p;
          if (collect) {
            size_t end = p;
            while (end > start && (in[end - 1] == ' ' || in[end - 1] == '\t' || in[end - 1] == '\r')) --end;
            size_t b = mark;
            while (b > 0 && (in[b - 1] == ' ' || in[b - 1] == '\t')) --b;
            bool ownLine = (b == 0 || in[b - 1] == '\n');
            Comment cm;
            cm.text.assign(in.data() + start, end - start);
            cm.block = false;
            if (ownLine) pendLead.push_back(std::move(cm));
            else pendTrail.push_back(std::move(cm));
          }
          continue;
        }
        if (in[p + 1] == '*') {
          size_t start = p + 2;
          int depth = 1;
          size_t pos = p + 2;
          while (pos + 1 < in.size() && depth > 0) {
            if (in[pos] == '/' && in[pos + 1] == '*') { ++depth; pos += 2; }
            else if (in[pos] == '*' && in[pos + 1] == '/') { --depth; pos += 2; }
            else ++pos;
          }
          if (depth > 0) throw asvJSONError("RON: unterminated block comment");
          size_t end = pos;
          if (collect) {
            size_t b = p;
            while (b > 0 && (in[b - 1] == ' ' || in[b - 1] == '\t')) --b;
            bool ownLine = (b == 0 || in[b - 1] == '\n');
            Comment cm;
            cm.text.assign(in.data() + start, end - start - 2); // strip closing */
            cm.block = true;
            if (ownLine) pendLead.push_back(std::move(cm));
            else pendTrail.push_back(std::move(cm));
          }
          p = end;
          continue;
        }
      }
      if (c == '#' && p + 1 < in.size() && in[p + 1] == '!') {
        // #![enable(...)] extension header. The '[' is mandatory: scanning on
        // without it turned a bare `#!something` into a search for ']' that ate
        // the rest of the document before reporting the failure. The ']' search
        // is also bounded to the header's own line so a missing closer cannot
        // swallow the following documents.
        if (p + 2 >= in.size() || in[p + 2] != '[')
          throw asvJSONError("RON: unterminated extension header");
        size_t q = p + 3;
        while (q < in.size() && in[q] != ']' && in[q] != '\n') ++q;
        if (q >= in.size() || in[q] != ']')
          throw asvJSONError("RON: unterminated extension header");
        p = q + 1;
        continue;
      }
      break;
    }
    return p;
  }

  void skipWs() { i = skipWsFrom(i, true); }

  // Commit accumulated own-line comments as leading of the node at `path`.
  void commitLead(std::string path) {
    if (pendLead.empty()) return;
    RonCommentEntry e;
    e.path = std::move(path);
    e.leading.swap(pendLead);
    comments.push_back(std::move(e));
  }

  // Commit a same-line comment as trailing of the node at `path`.
  void commitTrail(std::string path) {
    if (pendTrail.empty()) return;
    RonCommentEntry e;
    e.path = std::move(path);
    e.trailing.swap(pendTrail);
    comments.push_back(std::move(e));
  }

  // Redirect a same-line comment collected right after ':' to leading of `path`
  // (it introduces the value, not trailing it), then any own-line lead. Used for
  // struct fields and map keys where a comment sits between ':' and its value.
  void commitValueLead(const std::string& path) {
    if (!pendTrail.empty()) {
      RonCommentEntry e;
      e.path = path;
      e.leading.swap(pendTrail);
      comments.push_back(std::move(e));
    }
    commitLead(path);
  }

  // Commit comments collected just before a container closer: they stay inside
  // the container (object/array "closing" comments), e.g. `{ a: 1, /* note */ }`.
  void commitClosing(std::string path) {
    if (pendLead.empty() && pendTrail.empty()) return;
    RonCommentEntry e;
    e.path = std::move(path);
    if (!pendTrail.empty()) { e.trailing.swap(pendTrail); }
    for (auto& c : pendLead) e.closing.push_back(std::move(c));
    pendLead.clear();
    if (!e.closing.empty() || !e.trailing.empty()) comments.push_back(std::move(e));
  }

  std::string readIdent() {
    if (i < in.size() && in[i] == 'r' && i + 1 < in.size() && in[i + 1] == '#') {
      i += 2;
      if (i >= in.size() || !ronIsIdentStart(in[i])) throw asvJSONError("RON: invalid raw identifier");
      std::string s;
      s += in[i++];
      while (i < in.size() && ronIsIdentCont(in[i])) s += in[i++];
      return s;
    }
    if (i < in.size() && ronIsIdentStart(in[i])) {
      std::string s;
      s += in[i++];
      while (i < in.size() && ronIsIdentCont(in[i])) s += in[i++];
      return s;
    }
    return std::string();
  }

  std::string parseString() {
    ++i; // opening quote
    std::string s;
    s.reserve(32);
    while (i < in.size()) {
      unsigned char c = static_cast<unsigned char>(in[i]);
      if (c == '"') { ++i; return ronJsonStr(s); }
      if (c == '\\') {
        if (i + 1 >= in.size()) throw asvJSONError("RON: unterminated string escape");
        char e = in[i + 1];
        switch (e) {
          case 'n': s += '\n'; i += 2; break;
          case 't': s += '\t'; i += 2; break;
          case 'r': s += '\r'; i += 2; break;
          case '0': s += '\0'; i += 2; break;
          case 'b': s += '\b'; i += 2; break;
          case 'f': s += '\f'; i += 2; break;
          case '\\': s += '\\'; i += 2; break;
          case '"': s += '"'; i += 2; break;
          case '\'': s += '\''; i += 2; break;
          case '\n': {
            i += 2;
            while (i < in.size() && (in[i] == ' ' || in[i] == '\t' || in[i] == '\n' || in[i] == '\r')) ++i;
            break;
          }
          case '\r': {
            i += 2;
            if (i < in.size() && in[i] == '\n') ++i;
            while (i < in.size() && (in[i] == ' ' || in[i] == '\t' || in[i] == '\n' || in[i] == '\r')) ++i;
            break;
          }
          case 'x': {
            if (i + 3 >= in.size() || std::isxdigit(static_cast<unsigned char>(in[i + 2])) == 0 ||
                std::isxdigit(static_cast<unsigned char>(in[i + 3])) == 0)
              throw asvJSONError("RON: invalid \\x escape");
            s += static_cast<char>(ronHexVal(in[i + 2]) * 16 + ronHexVal(in[i + 3]));
            i += 4;
            break;
          }
          case 'u': {
            if (i + 2 < in.size() && in[i + 2] == '{') {
              size_t hs = i + 3, he = hs;
              while (he < in.size() && std::isxdigit(static_cast<unsigned char>(in[he]))) ++he;
              if (he == hs || he >= in.size() || in[he] != '}' || he - hs > 6)
                throw asvJSONError("RON: invalid \\u{...} escape");
              unsigned long cp = 0;
              for (size_t k = hs; k < he; k++) cp = (cp << 4) | static_cast<unsigned long>(ronHexVal(in[k]));
              if (cp > 0x10FFFF) throw asvJSONError("RON: invalid \\u{...} escape");
              ronAppendUtf8(s, cp);
              i = he + 1;
              break;
            }
            size_t k = i + 2;
            unsigned long cp = ronReadU4(in, k, "RON: invalid \\u escape");
            // A high surrogate is only half a code point; pair it with the
            // following \uXXXX low surrogate, else the unpaired one encodes as
            // invalid UTF-8. Same rule as the parser for the char literal.
            if (cp >= 0xD800 && cp <= 0xDBFF && k + 6 <= in.size() && in[k] == '\\' && in[k + 1] == 'u' &&
                std::isxdigit(static_cast<unsigned char>(in[k + 2])) &&
                std::isxdigit(static_cast<unsigned char>(in[k + 3])) &&
                std::isxdigit(static_cast<unsigned char>(in[k + 4])) &&
                std::isxdigit(static_cast<unsigned char>(in[k + 5]))) {
              size_t k2 = k + 2;
              unsigned long lo = ronReadU4(in, k2, "RON: invalid \\u escape");
              if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                k = k2;
              }
            }
            if (cp >= 0xD800 && cp <= 0xDFFF) throw asvJSONError("RON: invalid \\u escape");
            ronAppendUtf8(s, cp);
            i = k;
            break;
          }
          default: throw asvJSONError("RON: invalid string escape");
        }
        continue;
      }
      s += static_cast<char>(c);
      ++i;
    }
    throw asvJSONError("RON: unterminated string");
  }

  std::string parseRawString() {
    size_t hashes = 0;
    while (i < in.size() && in[i] == '#') { ++hashes; ++i; }
    if (i >= in.size() || in[i] != '"') throw asvJSONError("RON: invalid raw string");
    ++i;
    size_t contentStart = i;
    while (true) {
      if (i >= in.size()) throw asvJSONError("RON: unterminated raw string");
      if (in[i] == '"') {
        bool ok = true;
        for (size_t k = 0; k < hashes; k++) {
          if (i + 1 + k >= in.size() || in[i + 1 + k] != '#') { ok = false; break; }
        }
        if (ok) {
          std::string s(in.data() + contentStart, i - contentStart);
          i += 1 + hashes;
          return ronJsonStr(s);
        }
      }
      ++i;
    }
  }

  std::string parseChar() {
    // At the opening apostrophe (byte-char prefix 'b' already consumed).
    if (i >= in.size() || in[i] != '\'') throw asvJSONError("RON: internal char error");
    ++i;
    if (i >= in.size()) throw asvJSONError("RON: unterminated char");
    std::string ch;
    char c = in[i];
    if (c == '\\') {
      if (i + 1 >= in.size()) throw asvJSONError("RON: unterminated char escape");
      char e = in[i + 1];
      switch (e) {
        case 'n': ch = "\n"; i += 2; break;
        case 't': ch = "\t"; i += 2; break;
        case 'r': ch = "\r"; i += 2; break;
        case '0': ch = "\0"; i += 2; break;
        case '\\': ch = "\\"; i += 2; break;
        case '\'': ch = "'"; i += 2; break;
        case '"': ch = "\""; i += 2; break;
        case 'x': {
          if (i + 3 >= in.size() || std::isxdigit(static_cast<unsigned char>(in[i + 2])) == 0 ||
              std::isxdigit(static_cast<unsigned char>(in[i + 3])) == 0)
            throw asvJSONError("RON: invalid \\x char escape");
          ch = static_cast<char>(ronHexVal(in[i + 2]) * 16 + ronHexVal(in[i + 3]));
          i += 4;
          break;
        }
        case 'u': {
          size_t k = i + 2;
          unsigned long cp = ronReadU4(in, k, "RON: invalid \\u char escape");
          if (cp >= 0xD800 && cp <= 0xDBFF && k + 6 <= in.size() && in[k] == '\\' && in[k + 1] == 'u' &&
              std::isxdigit(static_cast<unsigned char>(in[k + 2])) &&
              std::isxdigit(static_cast<unsigned char>(in[k + 3])) &&
              std::isxdigit(static_cast<unsigned char>(in[k + 4])) &&
              std::isxdigit(static_cast<unsigned char>(in[k + 5]))) {
            size_t k2 = k + 2;
            unsigned long lo = ronReadU4(in, k2, "RON: invalid \\u char escape");
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              k = k2;
            }
          }
          if (cp >= 0xD800 && cp <= 0xDFFF) throw asvJSONError("RON: invalid \\u char escape");
          ronAppendUtf8(ch, cp);
          i = k;
          break;
        }
        default: throw asvJSONError("RON: invalid char escape");
      }
    } else {
      unsigned char u = static_cast<unsigned char>(c);
      int len = 1;
      if (u >= 0xF0) len = 4;
      else if (u >= 0xE0) len = 3;
      else if (u >= 0xC0) len = 2;
      if (i + len > in.size()) len = static_cast<int>(in.size() - i);
      ch = std::string(in.data() + i, static_cast<size_t>(len));
      i += len;
    }
    if (i >= in.size() || in[i] != '\'') throw asvJSONError("RON: unterminated char");
    ++i;
    return ronJsonStr(ch);
  }

  void consumeNumberSuffix() {
    if (i >= in.size()) return;
    char c = in[i];
    if (c != 'i' && c != 'u' && c != 'f') return;
    size_t save = i;
    std::string t;
    while (i < in.size()) {
      char d = in[i];
      if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9')) { t += d; ++i; }
      else break;
    }
    static const char* const kKnown[] = {
      "i8", "i16", "i32", "i64", "i128",
      "u8", "u16", "u32", "u64", "u128",
      "f32", "f64"
    };
    bool ok = false;
    for (const auto* k : kKnown) {
      if (t == k) { ok = true; break; }
    }
    if (!ok) i = save;
  }

  // Parses an optionally signed number (decimal/float/radix) or the
  // inf/NaN literals into JSON number text. Does NOT descend into ranges.
  std::string parseSignedNumber() {
    skipWs();
    std::string sign;
    if (i < in.size() && (in[i] == '+' || in[i] == '-')) {
      if (in[i] == '-') sign = "-";
      ++i;
    }
    if (i + 3 <= in.size() && in.compare(i, 3, "inf") == 0 &&
        (i + 3 >= in.size() || !ronIsIdentCont(in[i + 3]))) {
      std::string r = sign + "Infinity";
      i += 3;
      return r;
    }
    if (i + 3 <= in.size() && in.compare(i, 3, "NaN") == 0 &&
        (i + 3 >= in.size() || !ronIsIdentCont(in[i + 3]))) {
      std::string r = sign + "NaN";
      i += 3;
      return r;
    }
    if (i >= in.size() ||
        !((in[i] >= '0' && in[i] <= '9') ||
          (in[i] == '.' && i + 1 < in.size() && in[i + 1] >= '0' && in[i + 1] <= '9')))
      throw asvJSONError("RON: invalid number");

    // Radix literals: 0x.. / 0o.. / 0b..
    if (in[i] == '0' && i + 1 < in.size()) {
      int radix = 0;
      if (in[i + 1] == 'x') radix = 16;
      else if (in[i + 1] == 'o') radix = 8;
      else if (in[i + 1] == 'b') radix = 2;
      if (radix) {
        i += 2;
        std::string digits;
        while (i < in.size()) {
          char d = in[i];
          if (d == '_') { ++i; continue; }
          bool ok = false;
          if (d >= '0' && d <= '9') ok = (d - '0') < radix;
          else if (d >= 'a' && d <= 'f') ok = (radix == 16);
          else if (d >= 'A' && d <= 'F') ok = (radix == 16);
          if (!ok) break;
          digits += d;
          ++i;
        }
        if (digits.empty()) throw asvJSONError("RON: invalid number");
        errno = 0;
        char* endp = nullptr;
        unsigned long long v = strtoull(digits.c_str(), &endp, radix);
        if (errno == ERANGE || endp != digits.c_str() + digits.size())
          throw asvJSONError("RON: number out of range");
        consumeNumberSuffix();
        return sign + std::to_string(v);
      }
    }

    // Decimal / float.
    std::string tok;
    while (i < in.size() && ((in[i] >= '0' && in[i] <= '9') || in[i] == '_')) { tok += in[i]; ++i; }
    if (i < in.size() && in[i] == '.' && !(i + 1 < in.size() && in[i + 1] == '.')) {
      tok += '.';
      ++i;
      while (i < in.size() && ((in[i] >= '0' && in[i] <= '9') || in[i] == '_')) { tok += in[i]; ++i; }
    }
    if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
      tok += in[i];
      ++i;
      if (i < in.size() && (in[i] == '+' || in[i] == '-')) { tok += in[i]; ++i; }
      size_t expStart = i;
      while (i < in.size() && ((in[i] >= '0' && in[i] <= '9') || in[i] == '_')) { tok += in[i]; ++i; }
      // The exponent needs at least one digit. Without this check `3.e` turned
      // into the invalid JSON token `3.0e`, which the JSON layer rejected with
      // a foreign "digit required after exponent" message instead of RON's.
      if (i == expStart) throw asvJSONError("RON: invalid number, digit required after exponent");
    }
    if (tok.empty()) throw asvJSONError("RON: invalid number");
    consumeNumberSuffix();

    std::string clean;
    clean.reserve(tok.size());
    for (char ch : tok) if (ch != '_') clean += ch;
    if (clean.empty()) throw asvJSONError("RON: invalid number");

    if (clean[0] == '.') clean = "0" + clean;          // .5 -> 0.5
    // Strip leading zeros in the integer part (before any '.' or exponent),
    // keeping at least one digit: 007 -> 7, 007.5 -> 7.5, 0.5 stays 0.5.
    {
      size_t cut = clean.find_first_of(".eE");
      size_t intLen = (cut == std::string::npos) ? clean.size() : cut;
      size_t ns = 0;
      while (ns + 1 < intLen && clean[ns] == '0') ++ns;
      if (ns > 0) clean = clean.substr(ns);
    }
    if (clean.find('.') != std::string::npos) {
      size_t pos = clean.find('.');
      if (pos + 1 == clean.size()) clean += '0';      // 5. -> 5.0
      else if (clean[pos + 1] == 'e' || clean[pos + 1] == 'E') clean.insert(pos + 1, "0"); // 5.e3 -> 5.0e3
    }
    return sign + clean;
  }

  // Builds {"$range":{...}} given the optional start JSON text; caller has
  // already consumed the "..". Position is at an optional '=' then the end.
  std::string ronRangeTail(const std::string& startJson) {
    bool inclusive = false;
    if (i < in.size() && in[i] == '=') { inclusive = true; ++i; }
    std::string endJson;
    skipWs();
    if (i < in.size() && in[i] != ')' && in[i] != ']' && in[i] != '}' && in[i] != ',' && in[i] != ':') {
      size_t save = i;
      try { endJson = parseSignedNumber(); }
      catch (const asvJSONError&) { i = save; }
    }
    std::string out = "{\"$range\":{\"start\":";
    out += startJson;
    if (!endJson.empty()) {
      out += ",\"end\":";
      out += endJson;
    }
    if (inclusive) out += ",\"inclusive\":true";
    out += "}}";
    return out;
  }

  // Start-less range form: "..", "..=end".
  std::string parseBareRange() {
    // At ".." or "..=".
    i += 2;
    bool inclusive = false;
    if (i < in.size() && in[i] == '=') { inclusive = true; ++i; }
    std::string endJson;
    skipWs();
    if (i < in.size() && in[i] != ')' && in[i] != ']' && in[i] != '}' && in[i] != ',' && in[i] != ':') {
      size_t save = i;
      try { endJson = parseSignedNumber(); }
      catch (const asvJSONError&) { i = save; }
    }
    std::string out = "{\"$range\":{";
    if (!endJson.empty()) {
      out += "\"end\":";
      out += endJson;
      if (inclusive) out += ",\"inclusive\":true";
    } else if (inclusive) {
      out += "\"inclusive\":true";
    }
    out += "}}";
    return out;
  }

  std::string parseNumberOrRange() {
    std::string num = parseSignedNumber();
    if (i + 1 < in.size() && in[i] == '.' && in[i + 1] == '.') {
      i += 2;
      return ronRangeTail(num);
    }
    return num;
  }

  // Detects whether a parenthesized group (positioned right after '(') is a
  // struct (first entry is `ident :`) or a tuple / list.
  bool ronGroupIsStruct() {
    size_t p = skipWsFrom(i, false);
    if (p >= in.size()) throw asvJSONError("RON: unterminated group");
    if (in[p] == ')') return true; // empty group -> object
    if (in[p] == 'r' && p + 1 < in.size() && in[p + 1] == '#') p += 2; // raw ident field
    if (p < in.size() && ronIsIdentStart(in[p])) {
      ++p;
      while (p < in.size() && ronIsIdentCont(in[p])) ++p;
      p = skipWsFrom(p, false);
      if (p < in.size() && in[p] == ':') return true;
    }
    return false;
  }

  std::string parseValue(int depth, const std::string& path, bool commit) {
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
      throw asvJSONError("RON: nesting too deep");
    skipWs();
    if (commit) commitLead(path);
    if (i >= in.size()) throw asvJSONError("RON: unexpected end of input");
    char c = in[i];
    if (c == '(') return parseParen(depth, path, commit);
    if (c == '[') return parseSeq(depth, path, commit);
    if (c == '{') return parseMap(depth, path, commit);
    if (c == '"') return parseString();
    if (c == '\'') return parseChar();
    if (c == 'b') {
      size_t save = i;
      if (i + 1 < in.size()) {
        if (in[i + 1] == '"') { ++i; return parseString(); }       // b"..."
        if (in[i + 1] == '\'') { ++i; return parseChar(); }        // b'c'
        if (in[i + 1] == 'r') {
          i += 2;
          if (i < in.size() && (in[i] == '"' || in[i] == '#')) return parseRawString(); // br#"..."
          i = save;
        }
      }
    }
    if (c == 'r') {
      size_t save = i;
      if (i + 1 < in.size() && (in[i + 1] == '"' || in[i + 1] == '#')) {
        // Could be a raw string or a raw identifier (r#foo). Only treat as a
        // raw string when the '#'/quote run is followed by '"'.
        size_t j = i + 1;
        while (j < in.size() && in[j] == '#') ++j;
        if (in[i + 1] == '"') { ++i; return parseRawString(); }
        if (j < in.size() && in[j] == '"') { i += 1; return parseRawString(); }
        i = save;
      }
    }
    if (c == '.' && i + 1 < in.size() && in[i + 1] == '.') return parseBareRange();
    if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) return parseNumberOrRange();

    std::string id = readIdent();
    if (id.empty()) throw asvJSONError("RON: unexpected character");
    if (id == "true") return "true";
    if (id == "false") return "false";
    if (id == "None") return "null";
    if (id == "Some") {
      skipWs();
      if (i < in.size() && in[i] == '(') {
        ++i;
        skipWs();
        if (i < in.size() && in[i] == ')') { commitClosing(path); ++i; return "null"; } // Some() -> null (lenient)
        // A comment right after '(' introduces the wrapped value, and the Some
        // wrapper is transparent (it decodes to that value), so it becomes the
        // value's leading comment. It has to be committed here: skipWs() above
        // left it pending, and the inner parseValue/outer container would
        // otherwise commit it under their own path -- duplicating it or pushing
        // it out to the root.
        commitValueLead(path);
        std::string inner = parseValue(depth + 1, path, commit);
        skipWs();
        if (i >= in.size() || in[i] != ')') throw asvJSONError("RON: expected ')' after Some(<value>)");
        ++i;
        return inner;
      }
      return ronJsonStr(id); // bare enum variant named "Some"
    }
    if (id == "inf") return "Infinity";
    if (id == "NaN") return "NaN";
    // Qualified enum variant / tuple name: Type::Variant( data ). The name is
    // kept as an object key so the data survives round-trips.
    if (i + 1 < in.size() && in[i] == ':' && in[i + 1] == ':') {
      size_t save = i;
      i += 2;
      std::string second = readIdent();
      if (!second.empty() && second != "true" && second != "false" && second != "None" &&
          second != "Some" && second != "inf" && second != "NaN") {
        id += "::";
        id += second;
      } else {
        i = save;
      }
    }
    skipWs();
    if (i < in.size() && in[i] == '(') {
      // Named struct / tuple struct / enum variant with data: keep the leading
      // name as an object key so it survives JSON round-trips.
      std::string childPath = ronJoinPath(path, id);
      std::string group = parseParen(depth, childPath, commit);
      std::string wrapped;
      wrapped += '{';
      wrapped += ronJsonStr(id);
      wrapped += ':';
      wrapped += group;
      wrapped += '}';
      return wrapped;
    }
    return ronJsonStr(id);                                       // bare enum-unit variant / ident
  }

  std::string parseParen(int depth, const std::string& path, bool commit) {
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) throw asvJSONError("RON: nesting too deep");
    ++i; // '('
    bool isStruct = ronGroupIsStruct();
    std::string out = isStruct ? "{" : "[";
    skipWs();
    if (i < in.size() && in[i] == ')') { commitClosing(path); ++i; return "{}"; } // empty group -> empty object
    bool first = true;
    size_t seqIdx = 0;
    while (true) {
      skipWs();
      if (i >= in.size()) throw asvJSONError("RON: unterminated group");
      std::string childPath;
      if (isStruct) {
        std::string key = readIdent();
        if (key.empty()) throw asvJSONError("RON: expected field name in struct");
        childPath = ronJoinPath(path, key);
         skipWs();
         if (i >= in.size() || in[i] != ':') throw asvJSONError("RON: expected ':' after field name");
         ++i;
         skipWs();
         commitValueLead(childPath);
         std::string val = parseValue(depth + 1, childPath, commit);
        if (!first) out += ',';
        first = false;
        out += ronJsonStr(key);
        out += ':';
        out += val;
      } else {
        childPath = ronJoinPath(path, "[" + std::to_string(seqIdx) + "]");
        commitLead(childPath);
        std::string val = parseValue(depth + 1, childPath, commit);
        ++seqIdx;
        if (!first) out += ',';
        first = false;
        out += val;
      }
      skipWs();
      commitTrail(childPath);
      if (i < in.size() && in[i] == ',') {
        ++i;
        skipWs();
        // A same-line comment after a separator is trailing of the entry that
        // just ended (skipWsFrom put it in pendTrail); a comment on its own
        // line stays in pendLead and will lead the following entry.
        commitTrail(childPath);
        if (i < in.size() && in[i] == ')') { commitClosing(path); ++i; out += isStruct ? '}' : ']'; return out; } // trailing comma
        continue;
      }
      if (i < in.size() && in[i] == ')') { commitClosing(path); ++i; out += isStruct ? '}' : ']'; return out; }
      throw asvJSONError("RON: expected ',' or ')'");
    }
  }

  std::string parseSeq(int depth, const std::string& path, bool commit) {
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) throw asvJSONError("RON: nesting too deep");
    ++i; // '['
    std::string out = "[";
    skipWs();
    if (i < in.size() && in[i] == ']') { commitClosing(path); ++i; out += ']'; return out; }
    bool first = true;
    size_t idx = 0;
    while (true) {
      skipWs();
      if (i >= in.size()) throw asvJSONError("RON: unterminated list");
      std::string childPath = ronJoinPath(path, "[" + std::to_string(idx) + "]");
      commitLead(childPath);
      std::string val = parseValue(depth + 1, childPath, commit);
      ++idx;
      if (!first) out += ',';
      first = false;
      out += val;
      skipWs();
      commitTrail(childPath);
      if (i < in.size() && in[i] == ',') {
        ++i;
        skipWs();
        commitTrail(childPath);
        if (i < in.size() && in[i] == ']') { commitClosing(path); ++i; out += ']'; return out; } // trailing comma
        continue;
      }
      if (i < in.size() && in[i] == ']') { commitClosing(path); ++i; out += ']'; return out; }
      throw asvJSONError("RON: expected ',' or ']'");
    }
  }

  std::string parseMap(int depth, const std::string& path, bool commit) {
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) throw asvJSONError("RON: nesting too deep");
    ++i; // '{'
    std::string out = "{";
    skipWs();
    if (i < in.size() && in[i] == '}') { commitClosing(path); ++i; out += '}'; return out; }
    bool first = true;
    while (true) {
      skipWs();
      if (i >= in.size()) throw asvJSONError("RON: unterminated map");
      std::string keyText = parseValue(depth + 1, ronJoinPath(path, "(KEY)"), false);
      std::string key;
      if (keyText.size() >= 2 && keyText[0] == '"') {
        key = keyText; // already a JSON string key
      } else {
        key += '"';
        appendJsonEscaped(key, keyText);
        key += '"';
      }
      std::string childPath;
      if (keyText.size() >= 2 && keyText[0] == '"') {
        std::string rawKey;
        try {
          // Full JSON unescape so `\uXXXX` control-char keys round-trip into the
          // comment path too (the naive single-char pass above only handled one
          // escape byte). allowNull=true keeps NUL bytes for exact matching.
          rawKey = unescapeJsonString(keyText.substr(1, keyText.size() - 2), true);
        } catch (...) {
          size_t q = 1;
          while (q + 1 < keyText.size()) {
            if (keyText[q] == '\\' && q + 1 < keyText.size()) { rawKey += keyText[q + 1]; q += 2; }
            else { rawKey += keyText[q]; ++q; }
          }
        }
        childPath = ronJoinPath(path, rawKey);
      } else {
        childPath = ronJoinPath(path, keyText);
      }
      skipWs();
      if (i >= in.size() || in[i] != ':') throw asvJSONError("RON: expected ':' after map key");
      ++i;
      skipWs();
      commitValueLead(childPath);
      std::string val = parseValue(depth + 1, childPath, commit);
      if (!first) out += ',';
      first = false;
      out += key;
      out += ':';
      out += val;
      skipWs();
      commitTrail(childPath);
      if (i < in.size() && in[i] == ',') {
        ++i;
        skipWs();
        commitTrail(childPath);
        if (i < in.size() && in[i] == '}') { commitClosing(path); ++i; out += '}'; return out; } // trailing comma
        continue;
      }
      if (i < in.size() && in[i] == '}') { commitClosing(path); ++i; out += '}'; return out; }
      throw asvJSONError("RON: expected ',' or '}'");
    }
  }
};

inline std::string ronToJson(std::string_view input, std::vector<RonCommentEntry>* comments = nullptr) {
  std::vector<RonCommentEntry> local;
  std::vector<RonCommentEntry>& entries = comments ? *comments : local;
  RonParser p{input, 0, entries};
  p.skipWs();
  if (p.i >= input.size()) throw asvJSONError("RON: empty input");
  std::string json = p.parseValue(0, "", true);
  p.i = p.skipWsFrom(p.i, true);
  if (!p.pendLead.empty()) {
    // Unreferenced comments at the bottom of the document become the root's
    // footing comments.
    p.comments.push_back(RonCommentEntry{});
    RonCommentEntry& e = p.comments.back();
    e.footing = true;
    e.leading.swap(p.pendLead);
  } else if (!p.pendTrail.empty()) {
    p.comments.push_back(RonCommentEntry{});
    RonCommentEntry& e = p.comments.back();
    e.trailing.swap(p.pendTrail);
    e.footing = true;
  }
  if (p.i != input.size()) throw asvJSONError("RON: trailing content after value");
  return json;
}

// ------------------------- Encoder -------------------------

inline bool ronIsValidFieldKey(const std::string& k) {
  if (k.empty()) return false;
  if (k == "true" || k == "false" || k == "None" || k == "Some" || k == "inf" || k == "NaN") return false;
  if (!ronIsIdentStart(k[0])) return false;
  for (size_t q = 1; q < k.size(); q++) {
    if (!ronIsIdentCont(k[q])) return false;
  }
  return true;
}

// A RON type name used as a struct/tuple/enum-variant qualifier. Only a single
// identifier or an `A::B` path (max two segments) is accepted; longer paths are
// rejected so the decoder's segment scanner cannot misread them. Both segments
// must be bare identifiers that are not RON literals -- the decoder's segment
// scanner stops a segment at a literal (true/false/None/Some/inf/NaN) and rolls
// back, so a name like `A::Some` would encode to RON the decoder then rejects.
// Such a key simply falls back to the map form, which quotes it.
inline bool ronIsValidTypeName(const std::string& k) {
  if (k.empty()) return false;
  size_t p = 0;
  for (int seg = 0; seg < 2; ++seg) {
    if (seg) {
      if (k.compare(p, 2, "::") != 0) return false;
      p += 2;
    }
    // Segment: a bare identifier that is not a RON literal.
    if (p >= k.size() || !ronIsIdentStart(k[p])) return false;
    size_t start = p;
    ++p;
    while (p < k.size() && ronIsIdentCont(k[p])) ++p;
    std::string seg_text(k.data() + start, p - start);
    if (seg_text == "true" || seg_text == "false" || seg_text == "None" ||
        seg_text == "Some" || seg_text == "inf" || seg_text == "NaN") return false;
    if (p == k.size()) return true;  // single segment, or the last one
    if (seg == 1) return false;     // more than two segments
  }
  return true;
}

inline void ronIndent(std::string& out, int depth) {
  out.append(static_cast<size_t>(depth) * 2, ' ');
}

inline void ronEmitNum(std::string& out, const asvJSONValue* v) {
  if (!v) { out += '0'; return; }
  switch (v->type) {
    case asvJSONValue::INT: out += std::to_string(v->num); break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d)) out += std::signbit(d) ? "-NaN" : "NaN";
      else if (std::isinf(d)) out += (d > 0 ? "inf" : "-inf");
      else { std::string s; fmtDoubleVal(d, s); out += s; }
      break;
    }
    case asvJSONValue::STRING:
      if (v->raw_number) out += v->str_data;
      else { out += '"'; appendJsonEscaped(out, v->str_data); out += '"'; }
      break;
    default: out += '0'; break;
  }
}

inline void ronEmitRange(const asvJSONValue* rng, std::string& out) {
  const auto* m = rng->obj.get();
  auto itStart = m->find("start");
  auto itEnd = m->find("end");
  auto itInc = m->find("inclusive");
  bool hasStart = (itStart != m->end());
  bool hasEnd = (itEnd != m->end());
  bool inclusive = (itInc != m->end() && itInc->second->type == asvJSONValue::BOOL_VAL && itInc->second->flag);
  if (hasStart) ronEmitNum(out, itStart->second.get());
  out += "..";
  if (inclusive) out += '=';
  if (hasEnd) ronEmitNum(out, itEnd->second.get());
}

// Ranges round-trip as a {"$range":{...}} object (see ronRangeTail/parseBareRange).
inline bool ronIsRangeObject(const asvJSONValue* v, const asvJSONValue*& rng) {
  if (!v || v->type != asvJSONValue::OBJECT || !v->obj || v->obj->size() != 1) return false;
  auto it = v->obj->find("$range");
  if (it == v->obj->end()) return false;
  const asvJSONValue* r = it->second.get();
  if (!r || r->type != asvJSONValue::OBJECT || !r->obj) return false;
  for (const auto& [k, child] : *(r->obj)) {
    if (k == "start" || k == "end") {
      if (child->type != asvJSONValue::INT && child->type != asvJSONValue::DOUBLE &&
          !(child->type == asvJSONValue::STRING && child->raw_number)) return false;
    } else if (k == "inclusive") {
      if (child->type != asvJSONValue::BOOL_VAL) return false;
    } else {
      return false;
    }
  }
  rng = r;
  return true;
}

inline void ronSerializeVal(const asvJSONValue* v, std::string& out, int depth, bool pretty);
inline void ronEmitClosing(std::string& out, const asvJSONValue* v, int depth, bool pretty);

// Serialize a value plus its same-line trailing comment. When `commaNeeded`
// (this is not the last entry and a separator is due) and the trailing is a
// `//` line comment which swallows everything to end-of-line, the separating
// comma is emitted BEFORE the comment so the next entry starts fresh on its
// own line with no leading comma: `[800, // note\n  600]`. Returns true when
// the output ended with a newline (the caller must not emit a separator).
inline bool ronEmitValueWithTrailing(const asvJSONValue* v, std::string& out, int depth, bool pretty, bool commaNeeded) {
  ronSerializeVal(v, out, depth, pretty);
  if (!v->comments || v->comments->trailing.empty()) return false;
  const auto& last = v->comments->trailing.back();
  if (commaNeeded && commentNeedsEol(last)) out += ',';
  for (const auto& c : v->comments->trailing) {
    out += ' ';
    if (c.block) { out += "/*"; out += c.text; out += "*/"; }
    else { out += "//"; out += c.text; }
  }
  if (commentNeedsEol(last)) { out += '\n'; return true; }
  return false;
}

// Serializes the field list of an object between wrapper delimiters
// (called after `(`/`{`, before `)`/`}`). Picks struct form when every key is
// a bare ident, quoted-key map form otherwise.
inline void ronSerializeObjFields(const asvJSONValue* v, std::string& out, int depth, bool pretty) {
  bool allIdents = true;
  for (const auto& [k, child] : *(v->obj)) {
    if (!ronIsValidFieldKey(k)) { allIdents = false; break; }
  }
  size_t i = 0;
  const size_t n = v->obj->size();
  bool prevNl = false; // previous field ended with a line comment -> comma already placed
  for (const auto& [k, child] : *(v->obj)) {
    if (i > 0 && !prevNl) {
      bool leadCmt = child->comments && !child->comments->leading.empty();
      out += (pretty || leadCmt) ? ",\n" : ",";
    }
    prevNl = false;
    if (child->comments) {
      for (const auto& c : child->comments->leading) {
        if (pretty) ronIndent(out, depth + 1);
        if (c.block) { out += "/*"; out += c.text; out += "*/"; }
        else { out += "//"; out += c.text; }
        out += '\n';
      }
    }
    if (pretty) ronIndent(out, depth + 1);
    if (allIdents) {
      out += k;
      out += ':';
      if (pretty) out += ' ';
    } else {
      out += '"';
      appendJsonEscaped(out, k);
      out += '"';
      out += ':';
      if (pretty) out += ' ';
    }
    if (ronEmitValueWithTrailing(child.get(), out, depth + 1, pretty, /*commaNeeded=*/i + 1 < n))
      prevNl = true;
    i++;
  }
}

// A trailing comment whose final line is a `//` line comment swallows anything
// appended after it on the same line (the parser treats it as part of the
// comment). Such comments need a trailing newline so separators/closers on the
// following line are not consumed. Block comments end with `*/` and are safe.
inline void ronSerializeVal(const asvJSONValue* v, std::string& out, int depth, bool pretty) {
  if (!v) { out += "None"; return; }
  if (!asvJSONValue::checkNestingDepth(depth)) { out += "None"; return; }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += "None"; break;
    case asvJSONValue::BOOL_VAL: out += (v->flag ? "true" : "false"); break;
    case asvJSONValue::INT: out += std::to_string(v->num); break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d)) out += std::signbit(d) ? "-NaN" : "NaN";
      else if (std::isinf(d)) out += (d > 0 ? "inf" : "-inf");
      else { std::string s; fmtDoubleVal(d, s); out += s; }
      break;
    }
    case asvJSONValue::STRING: {
      if (v->raw_number) { out += v->str_data; break; }
      out += '"';
      appendJsonEscaped(out, v->str_data);
      out += '"';
      break;
    }
    case asvJSONValue::OBJECT: {
      const asvJSONValue* rng = nullptr;
      if (ronIsRangeObject(v, rng)) { ronEmitRange(rng, out); break; }
      if (v->obj->empty()) { out += "{}"; break; }
      if (v->yaml_set) {
        // YAML !!set marker form: {"$set":["a","b"]}
        out += "{ \"$set\": [ ";
        bool sfirst = true;
        for (const auto& [k, child] : *(v->obj)) {
          if (!sfirst) out += ", ";
          sfirst = false;
          out += '"';
          appendJsonEscaped(out, k);
          out += '"';
        }
        out += " ] }";
        break;
      }
      bool allIdents = true;
      for (const auto& [k, child] : *(v->obj)) {
        if (!ronIsValidFieldKey(k)) { allIdents = false; break; }
      }

      // Named struct / named tuple / qualified enum variant: a single key whose
      // value is a group (object, or non-empty array) and whose key is a valid
      // RON type name. Emitted as `GameConfig(...)` so that RON -> JSON -> RON
      // round-trips keep the wrapping name.
      if (v->obj->size() == 1) {
        auto nit = v->obj->begin();
        const asvJSONValue* child = nit->second.get();
        bool childObjectOk = (child->type == asvJSONValue::OBJECT) && !child->yaml_set;
        if (childObjectOk) {
          const asvJSONValue* cr = nullptr;
          if (ronIsRangeObject(child, cr)) childObjectOk = false;
        }
        bool namedGroup = childObjectOk ||
                          (child->type == asvJSONValue::ARRAY && child->arr && !child->arr->empty());
        if (namedGroup && ronIsValidTypeName(nit->first)) {
          bool childEmpty = (child->type == asvJSONValue::OBJECT) && child->obj->empty();
          out += nit->first;
          out += '(';
          if (pretty && !childEmpty) out += '\n';
          // Emit the group's own leading comments before its fields/elements.
          // No extra separator: the '(' already delimits the comment, and in
          // pretty mode ronIndent() has already placed it at depth + 1.
          if (child->comments && !child->comments->leading.empty()) {
            for (const auto& c : child->comments->leading) {
              if (pretty) ronIndent(out, depth + 1);
              if (c.block) { out += "/*"; out += c.text; out += "*/"; }
              else { out += "//"; out += c.text; }
              if (!c.block && commentNeedsEol(c)) out += '\n';
            }
          }
          if (child->type == asvJSONValue::OBJECT) {
            ronSerializeObjFields(child, out, depth, pretty);
          } else {
            size_t i = 0;
            const size_t ne = child->arr->size();
            bool prevNl = false; // previous element ended with a line comment
            for (const auto& el : *(child->arr)) {
              if (i > 0 && !prevNl) {
                bool leadCmt = el->comments && !el->comments->leading.empty();
                out += (pretty || leadCmt) ? ",\n" : ",";
              }
              prevNl = false;
              // Same as the plain ARRAY case: without this the element's leading
              // comments were dropped here, since only the group's own comments
              // are emitted above.
              if (el->comments) {
                for (const auto& c : el->comments->leading) {
                  if (pretty) ronIndent(out, depth + 1);
                  if (c.block) { out += "/*"; out += c.text; out += "*/"; }
                  else { out += "//"; out += c.text; }
                  out += '\n';
                }
              }
              if (pretty) ronIndent(out, depth + 1);
              if (ronEmitValueWithTrailing(el.get(), out, depth + 1, pretty, /*commaNeeded=*/i + 1 < ne))
                prevNl = true;
              i++;
            }
          }
          // Same order as the plain struct/map forms below: closing comments
          // first, then the newline+indent that lines the ')' up. Doing it the
          // other way round emitted a blank line before the comments, and at
          // depth > 0 stacked the ')' indent on top of the comment indent.
          ronEmitClosing(out, child, depth, pretty);
          if (pretty && !childEmpty) {
            if (out.back() != '\n') out += '\n';
            ronIndent(out, depth);
          }
          out += ')';
          // Comments attached to the wrapped child itself (a `/` same-line
          // trailing after the last field, or closings before `)`) belong to
          // the `Name(...)` form and are emitted here around the closer.
          if (child->comments && !child->comments->trailing.empty()) {
            for (const auto& c : child->comments->trailing) {
              out += ' ';
              if (c.block) { out += "/*"; out += c.text; out += "*/"; }
              else { out += "//"; out += c.text; }
              if (commentNeedsEol(c)) out += '\n';
            }
          }
          break;
        }
      }

      if (allIdents) {
        // Struct form: ( key: value, ... )
        out += '(';
        if (pretty) out += '\n';
        ronSerializeObjFields(v, out, depth, pretty);
        ronEmitClosing(out, v, depth, pretty);
        if (pretty) {
          if (out.back() != '\n') out += '\n';
          ronIndent(out, depth);
        }
        out += ')';
      } else {
        // Map form (non-ident keys): { "key": value, ... }
        out += '{';
        if (pretty) out += '\n';
        ronSerializeObjFields(v, out, depth, pretty);
        ronEmitClosing(out, v, depth, pretty);
        if (pretty) {
          if (out.back() != '\n') out += '\n';
          ronIndent(out, depth);
        }
        out += '}';
      }
      break;
    }
    case asvJSONValue::ARRAY: {
      out += '[';
      if (pretty && (v->arr && !v->arr->empty())) out += '\n';
      if (v->arr) {
        size_t i = 0;
        const size_t ne = v->arr->size();
        bool prevNl = false; // previous element ended with a line comment
        for (const auto& el : *(v->arr)) {
          if (i > 0 && !prevNl) {
            bool leadCmt = el->comments && !el->comments->leading.empty();
            out += (pretty || leadCmt) ? ",\n" : ",";
          }
          prevNl = false;
          if (el->comments) {
            for (const auto& c : el->comments->leading) {
              if (pretty) ronIndent(out, depth + 1);
              if (c.block) { out += "/*"; out += c.text; out += "*/"; }
              else { out += "//"; out += c.text; }
              out += '\n';
            }
          }
          if (pretty) ronIndent(out, depth + 1);
          if (ronEmitValueWithTrailing(el.get(), out, depth + 1, pretty, /*commaNeeded=*/i + 1 < ne))
            prevNl = true;
          i++;
        }
      }
      if (pretty && (v->arr && !v->arr->empty())) {
        if (out.back() != '\n') out += '\n';
        ronIndent(out, depth);
      }
      ronEmitClosing(out, v, depth, pretty);
      out += ']';
      break;
    }
    case asvJSONValue::DATETIME: {
      out += '"';
      fmtDateTimeVal(v->timestamp, v->datetime_ms, out, v->tz_offset_min);
      out += '"';
      break;
    }
    case asvJSONValue::BINARY:
    case asvJSONValue::EXTENSION: {
      out += "{ \"$binary\": { \"base64\": \"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      out += "\", \"subType\": \"";
      if (v->type == asvJSONValue::EXTENSION) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(v->ext_type));
        out += hex;
      } else {
        out += "00";
      }
      out += "\" } }";
      break;
    }
    case asvJSONValue::OBJECTID: {
      out += "{ \"$oid\": \"";
      fmtObjectIdHexVal(v->str_data, out);
      out += "\" }";
      break;
    }
    case asvJSONValue::REGEX: {
      size_t sep = v->str_data.rfind('\0');
      std::string_view pattern = (sep != std::string_view::npos) ? std::string_view(v->str_data.data(), sep) : std::string_view(v->str_data);
      std::string_view opts = (sep != std::string_view::npos) ? std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1) : std::string_view();
      out += "{ \"$regex\": \"";
      appendJsonEscaped(out, pattern);
      out += "\"";
      if (!opts.empty()) {
        out += ", \"$options\": \"";
        appendJsonEscaped(out, opts);
        out += "\"";
      }
      out += " }";
      break;
    }
    case asvJSONValue::TIMESTAMP: {
      out += "{ \"$timestamp\": { \"t\": ";
      out += std::to_string(v->num);
      out += " } }";
      break;
    }
    case asvJSONValue::CUSTOM_TAG: {
      out += "{ \"$customTag\": { \"name\": \"";
      appendJsonEscaped(out, v->str_data);
      out += "\", \"value\": ";
      if (v->custom_value) ronSerializeVal(v->custom_value.get(), out, depth + 1, pretty);
      else out += "None";
      out += " } }";
      break;
    }
    default: out += "None"; break;
  }
}

// Emit "closing" comments (collected just before a container closer) right
// before the closer, each on its own line.
inline void ronEmitClosing(std::string& out, const asvJSONValue* v, int depth, bool pretty) {
  if (!v->comments || v->comments->closing.empty()) return;
  // Every closing comment goes on its own line, and no comma is emitted. The
  // parser classifies a comment as *closing* only via its own-line rule; a
  // same-line comment re-reads as a trailing comment of the preceding entry, so
  // the round-trip drifted. A comma per comment (the old form) was worse still:
  // it left comment 2+ exactly where the parser expects a field, so
  // `{"a":1,/*c1*/,/*c2*/}` failed to parse back.
  if (!out.empty() && out.back() != '\n') out += '\n';
  for (const auto& c : v->comments->closing) {
    if (pretty) ronIndent(out, depth + 1);
    if (c.block) { out += "/*"; out += c.text; out += "*/"; }
    else { out += "//"; out += c.text; }
    out += '\n';
  }
}

inline std::string asvJSON::toRON(bool pretty) const {
  if (!root) return "None";
  std::string out;
  if (root->comments) {
    for (const auto& c : root->comments->leading) {
      if (c.block) { out += "/*"; out += c.text; out += "*/"; }
      else { out += "//"; out += c.text; }
      out += '\n';
    }
  }
  ronSerializeVal(root.get(), out, 0, pretty);
  if (root->comments) {
    for (const auto& c : root->comments->trailing) {
      out += ' ';
      if (c.block) { out += "/*"; out += c.text; out += "*/"; }
      else { out += "//"; out += c.text; }
      if (commentNeedsEol(c)) out += '\n';
    }
    for (const auto& c : root->comments->footing) {
      out += '\n';
      if (c.block) { out += "/*"; out += c.text; out += "*/"; }
      else { out += "//"; out += c.text; }
    }
  }
  return out;
}

inline void asvJSONValue::toRON(std::string& out, bool pretty) const {
  ronSerializeVal(this, out, 0, pretty);
  if (comments && !comments->trailing.empty()) {
    for (const auto& c : comments->trailing) {
      out += ' ';
      if (c.block) { out += "/*"; out += c.text; out += "*/"; }
      else { out += "//"; out += c.text; }
      if (commentNeedsEol(c)) out += '\n';
    }
  }
}

// Attach collected comments to the parsed tree by resolved JSON path. Only
// nodes that still exist get their comments -- renamed/dropped fields keep the
// document parseable and lose their out-of-band comments.
inline void ronAttachComments(asvJSONValue* root, const std::vector<RonCommentEntry>& entries) {
  if (!root || entries.empty()) return;
  for (const RonCommentEntry& e : entries) {
    asvJSONValue* node = e.footing ? ronFindPath(root, "") : ronFindPath(root, e.path);
    if (!node) continue;
    if (!node->comments) node->comments = std::make_unique<Comments>();
    if (!e.leading.empty() && node->comments->leading.empty()) {
      node->comments->leading = e.leading;
    }
    if (!e.trailing.empty() && node->comments->trailing.empty()) {
      node->comments->trailing = e.trailing;
    }
    if (!e.closing.empty() && node->comments->closing.empty()) {
      node->comments->closing = e.closing;
    }
    if (e.footing) {
      for (const auto& c : e.leading) node->comments->footing.push_back(c);
      for (const auto& c : e.trailing) node->comments->footing.push_back(c);
    }
  }
}

inline bool asvJSON::fromRON(std::string_view input) {
  formatType = FormatType::Unknown;
  if (input.empty()) { lastError = "empty input"; return false; }
  // Strip UTF-8 BOM
  if (input.size() >= 3 &&
      static_cast<unsigned char>(input[0]) == 0xEF &&
      static_cast<unsigned char>(input[1]) == 0xBB &&
      static_cast<unsigned char>(input[2]) == 0xBF)
    input.remove_prefix(3);
  if (input.empty()) { lastError = "empty input"; return false; }
  bool prevNaN = allowNaNInfinity;
  allowNaNInfinity = true; // RON natively supports NaN / inf
  try {
    std::vector<RonCommentEntry> entries;
    std::string jsonText = ronToJson(input, &entries);
    bool ok = parse(std::string_view(jsonText));
    allowNaNInfinity = prevNaN;
    if (ok) {
      formatType = FormatType::RON;
      ronAttachComments(getRoot(), entries);
    }
    return ok;
  } catch (const asvJSONError& e) {
    allowNaNInfinity = prevNaN;
    lastError = e.what();
    return false;
  }
}

} // namespace asvJSONInternal