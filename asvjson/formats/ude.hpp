#pragma once
// UDE (Unified Data Exchange) format support for asvJSON++
// See UDE.md for the full specification.
//
// Features:
//   - Optional "# UDE vX.Y" header
//   - Multiple documents separated by "// --- End of Document ---" lines
//   - Comments: #, //, /* ... */ (outside strings)
//   - Keys: identifiers, quoted strings, dotted keys (a.b.c => nested objects)
//   - Values: quoted/unquoted strings, decimal/hex/octal/binary numbers,
//     booleans (case-insensitive), null, inline or multi-line arrays/objects,
//     block scalars (| and > with indent + chomp indicators)
//   - Anchors (&name) and aliases (*name) with cycle detection
//   - Tags: !base64, !bin, !datetime, !ext, !regex; unknown tags preserve value
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

inline std::string_view udeTrim(std::string_view s) {
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r' || s[0] == '\n')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
  return s;
}

inline std::string udeToLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  return out;
}

inline bool udeIsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool udeIsIdentCont(char c) {
  return udeIsIdentStart(c) || (c >= '0' && c <= '9') || c == '-';
}

inline bool udeIsUnquotedChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '.' || c == '_' || c == '~' || c == '+' || c == '/' || c == '=' || c == '-';
}

inline int udeHexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Recognized document separator lines (exact match only, so that a comment
// merely containing the phrase "End of Document" does not split the stream).
inline bool udeIsDocSeparator(std::string_view line) {
  return line == "// --- End of Document ---" ||
         line == "// UDE document separator";
}

// Split a UDE stream into documents on document separator lines.
inline std::vector<std::string> splitUdeDocuments(const std::string& text) {
  auto lines = splitLines(text);
  std::vector<std::string> docs;
  std::string cur;
  for (const auto& line : lines) {
    std::string_view t = udeTrim(line);
    if (udeIsDocSeparator(t)) {
      if (!cur.empty()) docs.push_back(cur);
      cur.clear();
      continue;
    }
    cur += line;
    cur += '\n';
  }
  if (!cur.empty()) docs.push_back(cur);
  if (docs.empty()) docs.push_back("");
  return docs;
}

// Try to interpret a bare token as a number (decimal / hex / octal / binary).
// Returns nullptr when the token is not a valid UDE number literal.
inline std::unique_ptr<asvJSONValue> udeTryParseNumber(const std::string& tok) {
  if (tok.empty()) return nullptr;
  size_t i = 0;
  bool neg = false;
  // Per the UDE grammar SIGNED_INT ::= '-'? INT, a leading '+' is not a
  // valid numeric sign; such tokens are treated as unquoted strings.
  if (tok[0] == '-') { neg = true; i = 1; }

  // hex / octal / binary (the optional '-' belongs to the token itself)
  if (i + 1 < tok.size() && tok[i] == '0') {
    int base = 0;
    char n = tok[i + 1];
    if (n == 'x' || n == 'X') base = 16;
    else if (n == 'o' || n == 'O') base = 8;
    else if (n == 'b' || n == 'B') base = 2;
    if (base != 0) {
      size_t j = i + 2;
      if (j >= tok.size()) return nullptr;
      unsigned long long v = 0;
      for (; j < tok.size(); j++) {
        int d = udeHexDigit(tok[j]);
        if (d < 0 || d >= base) return nullptr;
        v = v * static_cast<unsigned long long>(base) + static_cast<unsigned long long>(d);
      }
      if (neg && v > static_cast<unsigned long long>(INT64_MAX)) return nullptr;
      int64_t iv = neg ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
      return asvJSONValue::makeInt(iv);
    }
  }

  // decimal int / float
  size_t j = i;
  while (j < tok.size() && tok[j] >= '0' && tok[j] <= '9') j++;
  if (j == i) return nullptr;                       // no digits
  if (tok[i] == '0' && j > i + 1) return nullptr;   // leading zeros are invalid

  bool isFloat = false;
  if (j < tok.size() && tok[j] == '.') {
    isFloat = true;
    j++;
    while (j < tok.size() && tok[j] >= '0' && tok[j] <= '9') j++;
  }
  if (j < tok.size() && (tok[j] == 'e' || tok[j] == 'E')) {
    isFloat = true;
    j++;
    if (j < tok.size() && (tok[j] == '+' || tok[j] == '-')) j++;
    size_t expStart = j;
    while (j < tok.size() && tok[j] >= '0' && tok[j] <= '9') j++;
    if (j == expStart) return nullptr;              // "1e" is invalid
  }
  if (j != tok.size()) return nullptr;              // trailing junk

  if (isFloat) {
    char* end = nullptr;
    double d = std::strtod(tok.c_str(), &end);
    if (!end || *end != 0 || !std::isfinite(d)) return nullptr;
    return asvJSONValue::makeDouble(d);
  }
  errno = 0;
  char* end = nullptr;
  long long ll = std::strtoll(tok.c_str(), &end, 10);
  if (errno == ERANGE || !end || *end != 0) return nullptr;
  return asvJSONValue::makeInt(static_cast<int64_t>(ll));
}

// Fold a block scalar: a single newline between two non-empty lines becomes a
// space; a run of one or more blank lines is preserved as a single paragraph
// break. A trailing newline is left in place so that chomping can adjust it.
inline std::string udeFoldScalar(std::string_view s) {
  std::string res;
  res.reserve(s.size());
  size_t i = 0;
  size_t n = s.size();
  bool para = false; // last emitted character was a paragraph break '\n'
  while (i < n) {
    size_t eol = s.find('\n', i);
    std::string_view seg = (eol == std::string_view::npos) ? s.substr(i) : s.substr(i, eol - i);
    if (res.empty()) {
      res.append(seg);
      para = false;
    } else if (seg.empty()) {
      if (!para) { res += '\n'; para = true; }
    } else {
      if (para) {
        res.append(seg);
      } else {
        res += ' ';
        res.append(seg);
      }
      para = false;
    }
    if (eol == std::string_view::npos) break;
    i = eol + 1;
  }
  if (!s.empty() && s.back() == '\n' && (res.empty() || res.back() != '\n'))
    res += '\n';
  return res;
}

// ====================== Parser ======================

class UDEParser {
public:
  explicit UDEParser(std::string_view text, bool strict) : text_(text), strict_(strict) {}

  std::unique_ptr<asvJSONValue> parseDocument();

private:
  // A parsed key plus whether it was quoted. Quoted keys are always literal;
  // unquoted dotted keys are expanded into nested objects.
  struct ParsedKey {
    std::string name;
    bool quoted = false;
  };

  std::string_view text_;
  bool strict_ = false;
  size_t pos_ = 0;
  int lineNum_ = 1;
  // Anchors store a deep copy of the anchored value so that aliases never
  // reference values that may be moved or destroyed by later key insertions.
  std::unordered_map<std::string, std::unique_ptr<asvJSONValue>> anchors_;
  // Anchor names currently being resolved (cycle detection).
  std::unordered_set<std::string> resolving_anchors_;
  int aliasDepth_ = 0;

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
      throw asvJSONError("UDE: unterminated block comment at line " + std::to_string(lineNum_));
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
        i += 2;
        while (i + 1 < text_.size() && !(text_[i] == '*' && text_[i + 1] == '/')) i++;
        i += 2;
        continue;
      }
      return false;
    }
  }

  void expect(char c, const char* what) {
    if (eof() || peek() != c) throw asvJSONError(std::string("UDE: expected '") + what + "' at line " + std::to_string(lineNum_));
    pos_++;
  }

  ParsedKey parseKey() {
    skipWs();
    if (eof()) throw asvJSONError("UDE: expected key at line " + std::to_string(lineNum_));
    ParsedKey pk;
    if (peek() == '"') {
      auto v = parseQuotedString();
      pk.name = std::move(v->str_data);
      pk.quoted = true;
      return pk;
    }
    size_t start = pos_;
    while (!eof() && udeIsUnquotedChar(text_[pos_])) pos_++;
    if (pos_ == start) throw asvJSONError("UDE: invalid key at line " + std::to_string(lineNum_));
    pk.name.assign(text_.data() + start, pos_ - start);
    if (strict_) {
      // The spec (Section 3) requires all keys to be quoted in strict mode.
      throw asvJSONError("UDE: strict mode requires all keys to be quoted (key: " + pk.name + ")");
    }
    return pk;
  }

  inline bool udeKeyIsIdentLike(const std::string& key) {
    if (key.empty()) return false;
    size_t segStart = 0;
    int segs = 0;
    for (size_t i = 0; i <= key.size(); i++) {
      if (i == key.size() || key[i] == '.') {
        std::string_view seg(key.data() + segStart, i - segStart);
        if (seg.empty() || !udeIsIdentStart(seg[0])) return false;
        for (char c : seg) if (!udeIsIdentCont(c)) return false;
        segs++;
        segStart = i + 1;
      }
    }
    return segs >= 1;
  }

  std::unique_ptr<asvJSONValue> parseValue() {
    skipWsAndComments();
    if (eof()) throw asvJSONError("UDE: expected value at line " + std::to_string(lineNum_));
    char c = peek();
    if (c == '"') return parseQuotedString();
    if (c == '[') return parseArray();
    if (c == '{') return parseObject();
    if (c == '!') return parseTagged();
    if (c == '&') return parseAnchored();
    if (c == '*') return parseAlias();
    if (c == '|' || c == '>') return parseBlockScalar();
    return parseScalar();
  }

  // Unescape UDE strings – only \n, \t, \\\\ and \" are allowed.
  static std::string udeUnescape(const std::string_view& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
      char c = raw[i];
      if (c == '\\') {
        if (i + 1 >= raw.size()) throw asvJSONError("UDE: invalid escape at end of string");
        char e = raw[++i];
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case '\\': out.push_back('\\'); break;
          case '"': out.push_back('"'); break;
          default: throw asvJSONError(std::string("UDE: invalid escape sequence \\" ) + e + " in string");
        }
      } else {
        out.push_back(c);
      }
    }
    return out;
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
    if (eof()) throw asvJSONError("UDE: unterminated string at line " + std::to_string(lineNum_));
    std::string_view raw(text_.data() + start, pos_ - start);
    pos_++; // consume '"'
    try {
      return asvJSONValue::makeStringView(udeUnescape(raw));
    } catch (const std::exception&) {
      throw asvJSONError("UDE: invalid escape sequence in string at line " + std::to_string(lineNum_));
    }
  }

  std::unique_ptr<asvJSONValue> parseArray() {
    pos_++; // consume '['
    auto arr = asvJSONValue::makeArray();
    while (true) {
      skipWsAndComments();
      if (eof()) throw asvJSONError("UDE: unterminated array at line " + std::to_string(lineNum_));
      if (peek() == ']') { pos_++; break; }
      if (!asvJSONValue::checkArraySize(arr->arr->size() + 1))
        throw asvJSONError("UDE: array too large");
      arr->arr->push_back(parseValue());
      skipWsAndComments();
      if (eof()) throw asvJSONError("UDE: unterminated array at line " + std::to_string(lineNum_));
      if (peek() == ',') { pos_++; continue; }
      if (peek() == ']') { pos_++; break; }
      // newline-separated element (already consumed above)
    }
    return arr;
  }

  std::unique_ptr<asvJSONValue> parseObject() {
    pos_++; // consume '{'
    auto obj = asvJSONValue::makeObject();
    while (true) {
      skipWsAndComments();
      if (eof()) throw asvJSONError("UDE: unterminated object at line " + std::to_string(lineNum_));
      if (peek() == '}') { pos_++; break; }
      ParsedKey pk = parseKey();
      skipWs();
      expect(':', ":");
      std::unique_ptr<asvJSONValue> val;
      if (atLineEndSkipping()) {
        if (strict_) throw asvJSONError("UDE: missing value after ':' at line " + std::to_string(lineNum_));
        val = asvJSONValue::makeNull();
      } else {
        val = parseValue();
      }
      insertKey(obj.get(), std::move(pk), std::move(val));
      skipWsAndComments();
      if (eof()) throw asvJSONError("UDE: unterminated object at line " + std::to_string(lineNum_));
      if (peek() == ',') { pos_++; continue; }
      if (peek() == '}') { pos_++; break; }
    }
    return obj;
  }

  std::unique_ptr<asvJSONValue> parseBlockScalar() {
    char indicator = text_[pos_++]; // '|' or '>'
    int explicitIndent = -1;
    char chomp = 0;
    if (!eof() && text_[pos_] >= '0' && text_[pos_] <= '9') { explicitIndent = text_[pos_] - '0'; pos_++; }
    if (!eof() && (text_[pos_] == '+' || text_[pos_] == '-')) { chomp = text_[pos_]; pos_++; }
    // skip the rest of the header line
    while (!eof() && text_[pos_] != '\n') pos_++;
    if (!eof()) { pos_++; lineNum_++; }

    std::vector<std::string> content;
    //bool terminatedByBlank = false; // removed - no longer needed
    size_t baseIndent = (explicitIndent >= 0) ? static_cast<size_t>(explicitIndent) : static_cast<size_t>(-1);

    while (!eof()) {
      size_t nl = text_.find('\n', pos_);
      std::string_view line = (nl == std::string_view::npos)
        ? text_.substr(pos_) : text_.substr(pos_, nl - pos_);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

      size_t indent = 0;
      while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) indent++;
      bool blank = (indent == line.size());

      if (baseIndent == static_cast<size_t>(-1)) {
        if (!blank) {
          baseIndent = indent;
          content.emplace_back(line.substr(indent));
        } else {
          content.emplace_back(""); // leading blank line is preserved until the base indent is known
        }
      } else if (!blank) {
        if (indent < baseIndent) break; // dedent terminates the scalar
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

    // Join content lines (each element separated by a single newline). The
    // terminating empty line is not content, but the break that follows the
    // last content line is: append it when the scalar was cut short by one.
    std::string out;
    for (size_t i = 0; i < content.size(); i++) {
      if (i > 0) out += '\n';
      out += content[i];
    }


    if (indicator == '>') {
      // Folded scalar: first fold (NEWLINE -> space), then chomping is
      // applied to the folded result. An empty line terminates the scalar
      // (Appendix A) rather than forming a paragraph break.
      out = udeFoldScalar(out);
    }

    // Apply chomping: '-' strips all trailing newlines, '+' keeps all, and
    // the default (clip) keeps a single trailing newline (Section 6).
    if (chomp == '-') {
      while (!out.empty() && out.back() == '\n') out.pop_back();
    } else if (chomp != '+') {
      size_t k = 0;
      while (k < out.size() && out[out.size() - 1 - k] == '\n') k++;
      if (k > 1) out.resize(out.size() - (k - 1));
    }

    return asvJSONValue::makeStringView(out);
  }

  std::unique_ptr<asvJSONValue> parseScalar() {
    size_t start = pos_;
    while (!eof() && udeIsUnquotedChar(text_[pos_])) pos_++;
    if (pos_ == start) throw asvJSONError("UDE: expected value at line " + std::to_string(lineNum_));
    std::string tok(text_.substr(start, pos_ - start));

    std::string lower = udeToLower(tok);
    if (lower == "true") return asvJSONValue::makeBool(true);
    if (lower == "false") return asvJSONValue::makeBool(false);
    if (lower == "null") return asvJSONValue::makeNull();

    auto num = udeTryParseNumber(tok);
    if (num) return num;

    if (strict_) {
      for (char c : tok) {
        unsigned char u = static_cast<unsigned char>(c);
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')))
          throw asvJSONError("UDE: strict mode requires quoting value: " + tok);
      }
    }
    return asvJSONValue::makeStringView(tok);
  }

  std::string parseAnchorName() {
    pos_++; // consume '&' or '*'
    size_t start = pos_;
    while (!eof() && udeIsIdentCont(text_[pos_])) pos_++;
    if (pos_ == start) throw asvJSONError("UDE: expected anchor name at line " + std::to_string(lineNum_));
    return std::string(text_.substr(start, pos_ - start));
  }

  void registerAnchor(const std::string& name, const asvJSONValue* v) {
    if (anchors_.size() > 1000000) throw asvJSONError("UDE: too many anchors");
    auto c = cloneValue(v);
    if (!c) throw asvJSONError("UDE: failed to store anchor &" + name);
    anchors_[name] = std::move(c);
  }

  std::unique_ptr<asvJSONValue> parseAnchored() {
    std::string name = parseAnchorName();
    skipWsAndComments();
    auto val = parseValue();
    registerAnchor(name, val.get());
    return val;
  }

  std::unique_ptr<asvJSONValue> parseAlias() {
    std::string name = parseAnchorName();
    auto it = anchors_.find(name);
    if (it == anchors_.end()) throw asvJSONError("UDE: undefined alias *" + name);
    // Cycle detection: if the anchor is already being resolved, the alias
    // graph contains a cycle and must be rejected (Appendix A, security).
    if (resolving_anchors_.count(name))
      throw asvJSONError("UDE: cyclic reference detected for alias *" + name);
    if (aliasDepth_ >= 1024) throw asvJSONError("UDE: anchor resolution depth exceeded");
    resolving_anchors_.insert(name);
    aliasDepth_++;
    std::unique_ptr<asvJSONValue> res;
    try {
      res = cloneValue(it->second.get());
    } catch (...) {
      resolving_anchors_.erase(name);
      aliasDepth_--;
      throw;
    }
    resolving_anchors_.erase(name);
    aliasDepth_--;
    if (!res) throw asvJSONError("UDE: failed to resolve alias *" + name);
    return res;
  }

  std::unique_ptr<asvJSONValue> applyTag(const std::string& tag, std::unique_ptr<asvJSONValue> val) {
    if (tag == "base64") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("UDE: !base64 requires a string value");
      bool err = false;
      auto bytes = decodeBase64Fast(val->str_data.data(), val->str_data.size(), &err);
      if (err) throw asvJSONError("UDE: invalid base64 data");
      auto b = asvJSONValue::makeBinary(bytes.data(), bytes.size());
      if (!b) throw asvJSONError("UDE: binary data too large");
      return b;
    }
    if (tag == "bin") {
      std::vector<uint8_t> bytes;
      if (val->type == asvJSONValue::STRING) {
        std::string hex = val->str_data;
        if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
        if (hex.empty() || hex.size() % 2 != 0) throw asvJSONError("UDE: !bin requires even-length hex string");
        for (size_t i = 0; i < hex.size(); i += 2) {
          int h1 = udeHexDigit(hex[i]), h2 = udeHexDigit(hex[i + 1]);
          if (h1 < 0 || h2 < 0) throw asvJSONError("UDE: invalid hex in !bin");
          bytes.push_back(static_cast<uint8_t>((h1 << 4) | h2));
        }
      } else if (val->type == asvJSONValue::INT) {
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
        throw asvJSONError("UDE: !bin requires a hex literal or string");
      }
      auto b = asvJSONValue::makeBinary(bytes.data(), bytes.size());
      if (!b) throw asvJSONError("UDE: binary data too large");
      return b;
    }
    if (tag == "datetime") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("UDE: !datetime requires a string value");
      time_t ts = 0;
      int ms = 0;
      if (!tryParseDateTime(val->str_data, ts, &ms))
        throw asvJSONError("UDE: invalid datetime: " + val->str_data);
      return asvJSONValue::makeDateTime(ts, ms);
    }
    if (tag == "ext") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("UDE: !ext requires a string value");
      const std::string& s = val->str_data;
      size_t colon = s.find(':');
      if (colon == std::string::npos) throw asvJSONError("UDE: !ext requires \"type:data\"");
      errno = 0;
      char* end = nullptr;
      long type = std::strtol(s.c_str(), &end, 10);
      if (errno == ERANGE || !end || end != s.c_str() + static_cast<long>(colon))
        throw asvJSONError("UDE: invalid ext type");
      bool err = false;
      auto bytes = decodeBase64Fast(s.data() + colon + 1, s.size() - colon - 1, &err);
      if (err) throw asvJSONError("UDE: invalid base64 in !ext");
      auto e = asvJSONValue::makeExtension(static_cast<int8_t>(type), bytes.data(), bytes.size());
      if (!e) throw asvJSONError("UDE: extension data too large");
      return e;
    }
    if (tag == "regex") {
      if (val->type != asvJSONValue::STRING) throw asvJSONError("UDE: !regex requires a string value");
      const std::string& s = val->str_data;
      size_t sep = s.find('|');
      std::string pattern = (sep == std::string::npos) ? s : s.substr(0, sep);
      std::string opts = (sep == std::string::npos) ? std::string() : s.substr(sep + 1);
      auto r = asvJSONValue::makeRegex(pattern.c_str(), opts.empty() ? nullptr : opts.c_str());
      if (!r) throw asvJSONError("UDE: invalid regex");
      return r;
    }
    // Preserve unknown tags using CUSTOM_TAG type.
    auto custom = std::make_unique<asvJSONValue>();
    custom->type = asvJSONValue::CUSTOM_TAG;
    // Serialize the original value to preserve it for round‑trip
    std::string serialized;
    if (val->type == asvJSONValue::STRING) {
      serialized = val->str_data;
    } else {
      // Use JSON serialization of the value (no pretty printing)
      val->serialize(serialized, false);
    }
    custom->str_data = tag + " " + serialized;
    return custom;
  }

  std::unique_ptr<asvJSONValue> parseTagged() {
    pos_++; // consume '!'
    size_t start = pos_;
    while (!eof() && udeIsIdentCont(text_[pos_])) pos_++;
    if (pos_ == start) throw asvJSONError("UDE: expected tag name at line " + std::to_string(lineNum_));
    std::string tag(text_.substr(start, pos_ - start));
    skipWsAndComments();
    std::string anchor;
    if (!eof() && peek() == '&') {
      anchor = parseAnchorName();
      skipWsAndComments();
    }
    auto val = parseValue();
    // Apply the tag first so the anchor references the final tagged value.
    val = applyTag(tag, std::move(val));
    if (!anchor.empty()) registerAnchor(anchor, val.get());
    return val;
  }

  void insertKey(asvJSONValue* obj, ParsedKey pk, std::unique_ptr<asvJSONValue> val) {
    if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) {
      throw asvJSONError("UDE: cannot insert key into non-object");
    }
    // Dotted keys are expanded only when unquoted; a quoted key such as
    // "a.b.c" is always a literal key (Section 3 of the UDE spec).
    if (!pk.quoted && udeKeyIsIdentLike(pk.name) && pk.name.find('.') != std::string::npos) {
      // dotted key: create nested objects
      std::vector<std::string> segs;
      size_t segStart = 0;
      for (size_t i = 0; i <= pk.name.size(); i++) {
        if (i == pk.name.size() || pk.name[i] == '.') {
          segs.push_back(pk.name.substr(segStart, i - segStart));
          segStart = i + 1;
        }
      }
      asvJSONValue* cur = obj;
      for (size_t i = 0; i + 1 < segs.size(); i++) {
        auto it = cur->obj->find(segs[i]);
        if (it == cur->obj->end()) {
          auto no = asvJSONValue::makeObject();
          auto* raw = no.get();
          cur->obj->emplace(segs[i], std::move(no));
          cur = raw;
        } else if (it->second->type == asvJSONValue::OBJECT) {
          cur = it->second.get();
        } else {
        // Existing key is not an object – cannot expand dotted key further.
        throw asvJSONError("UDE: key conflict – intermediate key '" + segs[i] + "' is not an object");
        }
      }
      const std::string& leaf = segs.back();
      auto it = cur->obj->find(leaf);
      if (it != cur->obj->end()) {
        if (strict_) throw asvJSONError("UDE: duplicate key '" + leaf + "' in strict mode");
        cur->obj->insert_or_assign(leaf, std::move(val));
      } else {
        cur->obj->emplace(leaf, std::move(val));
      }
    } else {
      auto it = obj->obj->find(pk.name);
      if (it != obj->obj->end()) {
        if (strict_) throw asvJSONError("UDE: duplicate key '" + pk.name + "' in strict mode");
        obj->obj->insert_or_assign(std::move(pk.name), std::move(val));
      } else {
        obj->obj->emplace(std::move(pk.name), std::move(val));
      }
    }
  }

};

std::unique_ptr<asvJSONValue> UDEParser::parseDocument() {
  skipWsAndComments();
    if (eof()) return asvJSONValue::makeObject(); // empty document

    std::unique_ptr<asvJSONValue> root = asvJSONValue::makeObject();
    bool any = false;
    while (true) {
      skipWsAndComments();
      if (eof()) break;
      char c = peek();
      if (c == '[' || c == '{') {
        if (any) throw asvJSONError("UDE: unexpected top-level collection at line " + std::to_string(lineNum_));
        auto coll = (c == '[') ? parseArray() : parseObject();
        skipWsAndComments();
        if (!eof()) throw asvJSONError("UDE: trailing content after top-level collection");
        return coll;
      }
      if (c == '|' || c == '>') {
        auto val = parseBlockScalar();
        skipWsAndComments();
        if (!eof()) throw asvJSONError("UDE: trailing content after block scalar");
        return val;
      }
      ParsedKey key = parseKey();
      if (key.name.empty()) throw asvJSONError("UDE: expected key at line " + std::to_string(lineNum_));
      skipWs();
      if (eof() || peek() != ':') throw asvJSONError("UDE: expected ':' after key '" + key.name + "' at line " + std::to_string(lineNum_));
      pos_++;
      std::unique_ptr<asvJSONValue> val;
      if (atLineEndSkipping()) {
        if (strict_) throw asvJSONError("UDE: missing value after ':' at line " + std::to_string(lineNum_));
        val = asvJSONValue::makeNull();
      } else {
        val = parseValue();
      }
      insertKey(root.get(), std::move(key), std::move(val));
      any = true;
    }
    return root;
  }

// Fall back to a plain string for a single-line document that failed to parse.
// Only applied when the line cannot be a structured UDE document at all, so
// that malformed documents and strict-mode rejections report their error
// instead of silently degrading to a plain string.
inline std::unique_ptr<asvJSONValue> udePlainTextFallback(const std::string& text) {
  std::string_view t = udeTrim(text);
  if (t.empty()) return nullptr;
  if (t.find('\n') != std::string_view::npos) return nullptr;
  if (t.find(':') != std::string_view::npos) return nullptr;
  char c = t[0];
  if (c == '[' || c == '{' || c == '|' || c == '>' || c == '!' || c == '&' || c == '*' || c == '"') return nullptr;
  return asvJSONValue::makeStringView(t);
}

inline std::unique_ptr<asvJSONValue> parseUDE(const std::string& text, bool strict) {
  std::vector<std::string> docs = splitUdeDocuments(text);
  if (docs.size() > 1) {
    auto arr = asvJSONValue::makeArray();
    for (const auto& doc : docs) {
      std::unique_ptr<asvJSONValue> v;
      try {
        UDEParser p(doc, strict);
        v = p.parseDocument();
      } catch (const asvJSONError&) {
        v = udePlainTextFallback(doc);
        if (!v) throw;
      }
      if (!asvJSONValue::checkArraySize(arr->arr->size() + 1))
        throw asvJSONError("UDE: too many documents");
      arr->arr->push_back(std::move(v));
    }
    return arr;
  }
  try {
    UDEParser p(text, strict);
    return p.parseDocument();
  } catch (const asvJSONError&) {
    auto v = udePlainTextFallback(text);
    if (!v) throw;
    return v;
  }
}

// ====================== Serializer ======================

// A key can be written bare only if it is a plain identifier. Keys containing
// dots must be quoted so that they round-trip as literal keys rather than
// being re-expanded as dotted (nested) keys by the parser.
inline bool udeKeySafe(const std::string& key) {
  if (key.empty() || key.find('.') != std::string::npos) return false;
  if (!udeIsIdentStart(key[0])) return false;
  for (char c : key) if (!udeIsIdentCont(c)) return false;
  return true;
}

inline void udeWriteQuoted(std::ostream& os, std::string_view s) {
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
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
          os << buf;
        } else {
          os << static_cast<char>(c);
        }
    }
  }
  os << '"';
}

inline void udeWriteKey(std::ostream& os, const std::string& k, bool strict) {
  // Strict mode requires all keys to be quoted (Section 3).
  if (!strict && udeKeySafe(k)) os << k;
  else udeWriteQuoted(os, k);
}

inline bool udeStringNeedsQuote(std::string_view s, bool strict) {
  if (s.empty()) return true;
  std::string lower = udeToLower(s);
  if (lower == "true" || lower == "false" || lower == "null") return true;
  if (udeTryParseNumber(std::string(s))) return true;
  for (unsigned char c : s) {
    if (!udeIsUnquotedChar(static_cast<char>(c))) return true;
    if (strict && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return true;
  }
  return false;
}

inline void udeWriteIndent(std::ostream& os, int indent) {
  for (int i = 0; i < indent; i++) os << "  ";
}

// A string can be written as a block scalar only when it round-trips through
// the parser. Since an empty line (a line with no characters after the break)
// terminates the scalar once the base indent is known (Appendix A), the
// string must not contain any blank line after content except a single
// trailing blank line, and must contain at least one content line.
inline bool udeCanBlockEncode(std::string_view s) {
  if (s.find('\n') == std::string_view::npos) return false;
  size_t trailing = 0;
  while (trailing < s.size() && s[s.size() - 1 - trailing] == '\n') trailing++;
  if (trailing >= 2) return false; // only a single trailing blank line survives
  bool seenContent = false;
  bool trailingBlank = false;
  size_t i = 0;
  while (i < s.size()) {
    size_t nl = s.find('\n', i);
    std::string_view line = (nl == std::string_view::npos) ? s.substr(i) : s.substr(i, nl - i);
    bool blank = true;
    for (char c : line) if (c != ' ' && c != '\t' && c != '\r') { blank = false; break; }
    if (nl == std::string_view::npos) {
      // Final line without a trailing break; a blank one after content would
      // terminate the scalar and lose the break.
      if (blank && seenContent) return false;
      if (!blank) {
        if (trailingBlank) return false; // blank line followed by more content
        seenContent = true;
      }
      break;
    }
    if (blank) {
      if (seenContent) {
        if (trailingBlank) return false; // two or more trailing blank lines
        trailingBlank = true;
      }
    } else {
      if (trailingBlank) return false; // blank line followed by more content
      if (!seenContent && !line.empty() &&
          (line[0] == ' ' || line[0] == '\t' || line[0] == '\r'))
        return false; // leading whitespace would be absorbed into the base indent
      seenContent = true;
    }
    i = nl + 1;
  }
  return seenContent;
}

// Write a string as a block scalar. A single trailing newline is preserved by
// emitting a '|+' header plus one trailing blank line so that the value
// round-trips exactly. Strings that cannot round-trip as a block scalar are
// written as quoted strings by the caller instead.
inline void udeWriteBlockScalar(std::ostream& os, std::string_view s) {
  size_t k = 0;
  while (k < s.size() && s[s.size() - 1 - k] == '\n') k++;
  os << (k > 0 ? "|+\n" : "|\n");
  std::string_view body = s.substr(0, s.size() - k);
  size_t i = 0;
  while (i < body.size()) {
    size_t eol = body.find('\n', i);
    if (eol == std::string_view::npos) eol = body.size();
    os << "  " << body.substr(i, eol - i) << '\n';
    i = eol + 1;
  }
  for (size_t j = 0; j < k; j++) os << '\n';
}

// Forward declaration
inline void udeWriteValue(std::ostream& os, const asvJSONValue* v, int indent, bool lineContext, bool strict);

inline void udeWriteObject(std::ostream& os, const asvJSONValue* v, int indent, bool strict) {
  if (indent == 0) {
    bool first = true;
    for (const auto& [k, sub] : *v->obj) {
      if (!first) os << '\n';
      first = false;
      udeWriteKey(os, k, strict);
      os << ": ";
      udeWriteValue(os, sub.get(), 1, true, strict);
    }
  } else {
    os << '{';
    bool first = true;
    for (const auto& [k, sub] : *v->obj) {
      if (!first) os << ", ";
      first = false;
      udeWriteKey(os, k, strict);
      os << ": ";
      udeWriteValue(os, sub.get(), indent + 1, false, strict);
    }
    os << '}';
  }
}

inline void udeWriteArray(std::ostream& os, const asvJSONValue* v, int indent, bool multiline, bool strict) {
  if (!v->arr || v->arr->empty()) { os << "[]"; return; }
  bool allObjs = true;
  for (const auto& e : *v->arr) if (e->type != asvJSONValue::OBJECT) { allObjs = false; break; }
  if (allObjs && multiline) {
    os << "[\n";
    for (size_t i = 0; i < v->arr->size(); i++) {
      udeWriteIndent(os, indent);
      udeWriteObject(os, v->arr->at(i).get(), indent + 1, strict);
      if (i + 1 < v->arr->size()) os << ',';
      os << '\n';
    }
    udeWriteIndent(os, indent - 1);
    os << ']';
  } else {
    os << '[';
    for (size_t i = 0; i < v->arr->size(); i++) {
      if (i) os << ", ";
      udeWriteValue(os, v->arr->at(i).get(), indent + 1, false, strict);
    }
    os << ']';
  }
}

inline void udeWriteValue(std::ostream& os, const asvJSONValue* v, int indent, bool lineContext, bool strict) {
  using T = asvJSONValue::Type;
  if (!v) { os << "null"; return; }
  switch (v->type) {
    case T::OBJECT:
      if (v->obj) udeWriteObject(os, v, indent, strict);
      else os << "{}";
      break;
    case T::ARRAY:
      udeWriteArray(os, v, indent, lineContext, strict);
      break;
    case T::STRING: {
      if (lineContext && udeCanBlockEncode(v->str_data)) {
        udeWriteBlockScalar(os, v->str_data);
      } else if (udeStringNeedsQuote(v->str_data, strict)) {
        udeWriteQuoted(os, v->str_data);
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
      fmtDoubleVal(v->dbl, tmp);
      os << tmp;
      break;
    }
    case T::BOOL_VAL: os << (v->flag ? "true" : "false"); break;
    case T::NULL_VAL: os << "null"; break;
    case T::DATETIME: {
      std::string dt;
      fmtDateTimeVal(v->timestamp, v->datetime_ms, dt);
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
        os << "!regex ";
        std::string r;
        fmtRegexVal(v->str_data, r);
        os << r;
        break;
      }
      case T::CUSTOM_TAG: {
        // str_data format: "tagName serializedValue"
        size_t space = v->str_data.find(' ');
        if (space != std::string::npos) {
          std::string tag = v->str_data.substr(0, space);
          std::string valStr = v->str_data.substr(space + 1);
          os << '!' << tag << ' ';
          // Decide quoting
          if (udeStringNeedsQuote(valStr, strict)) {
            udeWriteQuoted(os, valStr);
          } else {
            os << valStr;
          }
        } else {
          // malformed, fallback to raw string
          os << "!" << v->str_data;
        }
        break;
      }
      case T::TIMESTAMP: {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);

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

inline void serializeUDE(std::ostream& os, const asvJSONValue& val, int indent, bool strict) {
  if (val.type == asvJSONValue::OBJECT && val.obj && indent == 0) {
    bool first = true;
    for (const auto& [k, sub] : *val.obj) {
      if (!first) os << '\n';
      first = false;
      udeWriteKey(os, k, strict);
      os << ": ";
      udeWriteValue(os, sub.get(), 1, true, strict);
    }
  } else {
    udeWriteValue(os, &val, indent, false, strict);
    os << '\n';
  }
}

// ====================== asvJSON member methods ======================

inline std::string asvJSON::toUDE(bool strict) const {
  std::ostringstream os;
  if (root) asvJSONInternal::serializeUDE(os, *root, 0, strict);
  return os.str();
}

inline bool asvJSON::fromUDE(std::string_view input, bool strict) {
  try {
    root = nullptr;
    auto parsed = asvJSONInternal::parseUDE(std::string(input), strict);
    if (!parsed) {
      lastError = "UDE: failed to parse input";
      return false;
    }
    root = std::move(parsed);
    return true;
  } catch (const std::exception& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
