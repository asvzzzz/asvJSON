#pragma once
// Bencode serialization/parsing for asvJSON++.
//
// Bencode is the BitTorrent encoding (used in .torrent files):
//   i42e              integer
//   4:spam            byte string
//   l4:spami42ee      list
//   d3:bar4:spam3:fooi42ee   dictionary (keys sorted as byte strings)
//
// Bencode has no booleans, null or floating-point values.
//
// Decoding maps integers to INT, byte strings to STRING when the bytes are
// valid UTF-8 and to BINARY otherwise, lists to ARRAY and dictionaries to
// OBJECT.
//
// Encoding accepts every value type. INT, STRING, BINARY, ARRAY and OBJECT have
// native forms; the remaining types are mapped to the closest one (lossy, and
// spelled out in README.md; it mirrors the plist/BSON mappings):
//   BOOL -> integer 0/1, NULL -> empty byte string, DOUBLE -> decimal text,
//   DATETIME/TIMESTAMP -> integer seconds, OBJECTID -> byte string,
//   REGEX -> byte string of the PATTERN only (the options after the NUL are
//     dropped, exactly as plist does -- a regex with options round-trips as a
//     STRING of its pattern text, neither type nor options survive),
//   EXTENSION -> raw bytes, CUSTOM_TAG -> its payload value.
// So converting an arbitrary document to bencode never fails on bool/null/
// double; the encoder's default branch throws only for a type with no explicit
// mapping (any type core gains in the future, say). A byte string is the only
// empty form here, so NULL, an empty STRING and an empty BINARY all encode as
// `0:` and decode back as the empty STRING -- the empty-BINARY and NULL types
// cannot survive a round-trip. Like the parser, the encoder bounds recursion at
// MAX_NESTING_DEPTH: a deeper tree cannot be built by parsing, but one built
// programmatically throws "nesting too deep" instead of overflowing the stack.
//
// Dictionary keys are emitted in ascending byte order, as the specification
// requires. On input the order is NOT checked and duplicate keys are accepted
// with the last one winning -- a permissive reader, like most BitTorrent
// clients. The encoder always writes sorted output, so the result is canonical
// bencode either way.
//
// A document with no root at all (default-constructed or cleared) serializes to
// the empty string, which fromBencode rejects ("unexpected end of input"):
// bencode has no value for "nothing", so that state cannot round-trip.
// asvJSON::toMessagePack() and asvJSON::toCBOR() behave the same way.

#include "../core.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asvJSONInternal {

inline std::string bencodeParseString(std::string_view s, size_t& pos) {
  size_t start = pos;
  uint64_t len = 0;
  while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
    if (len > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("bencode: string too long");
    len = len * 10 + static_cast<uint64_t>(s[pos] - '0');
    pos++;
  }
  if (pos == start || pos >= s.size() || s[pos] != ':')
    throw asvJSONError("bencode: invalid byte-string length");
  // Safe to read s[start]: `pos != start` was checked above, so the length has
  // at least one digit in range.
  if (s[start] == '0' && pos - start > 1)
    throw asvJSONError("bencode: leading zero in byte-string length");
  if (len > asvJSONValue::MAX_STRING_LEN)
    throw asvJSONError("bencode: string too long");
  pos++; // ':'
  if (len > s.size() - pos) throw asvJSONError("bencode: truncated byte string");
  std::string out(s.data() + pos, static_cast<size_t>(len));
  pos += static_cast<size_t>(len);
  return out;
}

inline int64_t bencodeParseInt(std::string_view s, size_t& pos) {
  pos++; // 'i'
  size_t start = pos;
  bool neg = false;
  if (pos < s.size() && s[pos] == '-') { neg = true; pos++; }
  size_t digits = pos;
  while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
  if (pos == digits) throw asvJSONError("bencode: invalid integer");
  // Symmetric with the byte-string length bounds: int64 needs at most 19
  // digits, so reject longer lexemes before strtoll (a million digits should
  // not be scanned-and-converted just to hit ERANGE).
  if (pos - digits > 19) throw asvJSONError("bencode: integer too long");
  if (s[digits] == '0' && pos - digits > 1)
    throw asvJSONError("bencode: leading zero in integer");
  if (neg && s[digits] == '0')
    throw asvJSONError("bencode: negative zero");
  if (pos >= s.size() || s[pos] != 'e')
    throw asvJSONError("bencode: unterminated integer");
  std::string num(s.substr(start, pos - start));
  pos++; // 'e'
  errno = 0;
  char* end = nullptr;
  long long v = std::strtoll(num.c_str(), &end, 10);
  if (errno == ERANGE || !end || *end != 0)
    throw asvJSONError("bencode: integer out of range");
  return static_cast<int64_t>(v);
}

inline std::unique_ptr<asvJSONValue> bencodeParseValue(std::string_view s, size_t& pos, int depth) {
  if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
    throw asvJSONError("bencode: nesting too deep");
  if (pos >= s.size()) throw asvJSONError("bencode: unexpected end of input");
  char c = s[pos];
  if (c == 'i') {
    // makeInt uses new(std::nothrow): a bare `return` handed back the nullptr
    // it produces on OOM, which then landed in a list/dictionary as a null
    // element (dereferenced by any later tree walk). Every factory result in
    // this parser is checked; do not drop this guard (see CBOR/UBJSON/plist).
    auto v = asvJSONValue::makeInt(bencodeParseInt(s, pos));
    if (!v) throw asvJSONError("bencode: out of memory");
    return v;
  }
  if (c == 'l') {
    pos++;
    auto arr = asvJSONValue::makeArray();
    if (!arr) throw asvJSONError("bencode: out of memory");
    for (;;) {
      if (pos >= s.size()) throw asvJSONError("bencode: unterminated list");
      if (s[pos] == 'e') { pos++; break; }
      if (arr->arr->size() >= asvJSONValue::MAX_ARRAY_SIZE)
        throw asvJSONError("bencode: list too large");
      auto elem = bencodeParseValue(s, pos, depth + 1);
      if (!elem) throw asvJSONError("bencode: list element alloc failed");
      arr->arr->push_back(std::move(elem));
    }
    return arr;
  }
  if (c == 'd') {
    pos++;
    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("bencode: out of memory");
    for (;;) {
      if (pos >= s.size()) throw asvJSONError("bencode: unterminated dictionary");
      if (s[pos] == 'e') { pos++; break; }
      if (s[pos] < '0' || s[pos] > '9')
        throw asvJSONError("bencode: dictionary key must be a byte string");
      std::string key = bencodeParseString(s, pos);
      auto val = bencodeParseValue(s, pos, depth + 1);
      if (!val) throw asvJSONError("bencode: dictionary value alloc failed");
      if (obj->obj->size() >= asvJSONValue::MAX_OBJECT_SIZE)
        throw asvJSONError("bencode: dictionary too large");
      obj->obj->insert_or_assign(key, std::move(val));
    }
    return obj;
  }
  if (c >= '0' && c <= '9') {
    std::string bytes = bencodeParseString(s, pos);
    if (isValidUTF8(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size())) {
      auto v = asvJSONValue::makeString(bytes.data(), bytes.size());
      if (!v) throw asvJSONError("bencode: out of memory");
      return v;
    }
    auto v = asvJSONValue::makeBinary(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (!v) throw asvJSONError("bencode: byte string too long");
    return v;
  }
  throw asvJSONError("bencode: unexpected byte");
}

inline void bencodeEncode(const asvJSONValue* v, std::string& out, int depth) {
  if (!v) throw asvJSONError("bencode: invalid null value");
  // Mirror the parser's depth guard: parsing cannot build a tree this deep, but
  // a programmatic one can -- deep recursion here is a stack overflow, not an
  // error. Keep the bound identical on both sides.
  if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
    throw asvJSONError("bencode: nesting too deep");
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::INT:
      out += 'i';
      out += std::to_string(v->num);
      out += 'e';
      break;
    case T::STRING:
      out += std::to_string(v->str_data.size());
      out += ':';
      out += v->str_data;
      break;
    case T::BINARY:
      out += std::to_string(v->bin_data.size());
      out += ':';
      out.append(reinterpret_cast<const char*>(v->bin_data.data()), v->bin_data.size());
      break;
    case T::ARRAY: {
      if (v->arr->size() > asvJSONValue::MAX_ARRAY_SIZE)
        throw asvJSONError("bencode: list too large");
      out += 'l';
      for (const auto& e : *v->arr) bencodeEncode(e.get(), out, depth + 1);
      out += 'e';
      break;
    }
    // Types with no native bencode representation are mapped to the closest
    // form (documented lossy round-trips, mirrors the plist/BSON mappings):
    //   BOOL -> integer 0/1, NULL -> empty byte string, DOUBLE -> decimal text,
    //   DATETIME/TIMESTAMP -> integer seconds, OBJECTID/REGEX -> byte string,
    //   EXTENSION -> raw bytes, CUSTOM_TAG -> its payload value.
    case T::BOOL_VAL:
      out += (v->flag ? "i1e" : "i0e");
      break;
    case T::NULL_VAL:
      out += "0:";
      break;
    case T::DOUBLE: {
      std::string txt;
      if (std::isnan(v->dbl)) txt = "NaN";
      else if (std::isinf(v->dbl)) txt = (v->dbl < 0) ? "-Infinity" : "Infinity";
      else fmtDoubleValDot0(v->dbl, txt);
      out += std::to_string(txt.size());
      out += ':';
      out += txt;
      break;
    }
    case T::DATETIME:
      out += 'i';
      out += std::to_string(v->timestamp);
      out += 'e';
      break;
    case T::TIMESTAMP:
      out += 'i';
      out += std::to_string(v->num);
      out += 'e';
      break;
    case T::OBJECTID:
      out += std::to_string(v->str_data.size());
      out += ':';
      out += v->str_data;
      break;
    case T::REGEX: {
      // str_data is the canonical "pattern\0options" form (see makeRegex), so
      // the separator is NUL. Split at the LAST one, like plist (and UBJSON):
      // bencode has no regex type, so only the pattern is carried -- options
      // are dropped (documented in the header and README).
      size_t sep = v->str_data.rfind('\0');
      std::string_view pat = (sep != std::string::npos) ? std::string_view(v->str_data).substr(0, sep)
                                                        : std::string_view(v->str_data);
      out += std::to_string(pat.size());
      out += ':';
      out.append(pat.data(), pat.size());
      break;
    }
    case T::EXTENSION:
      out += std::to_string(v->bin_data.size());
      out += ':';
      out.append(reinterpret_cast<const char*>(v->bin_data.data()), v->bin_data.size());
      break;
    case T::CUSTOM_TAG:
      if (v->custom_value) bencodeEncode(v->custom_value.get(), out, depth + 1);
      else out += "0:";
      break;
    case T::OBJECT: {
      if (v->obj->size() > asvJSONValue::MAX_OBJECT_SIZE)
        throw asvJSONError("bencode: dictionary too large");
      std::vector<std::pair<std::string_view, const asvJSONValue*>> entries;
      entries.reserve(v->obj->size());
      for (const auto& [k, val] : *v->obj) entries.emplace_back(std::string_view(k), val.get());
      std::sort(entries.begin(), entries.end(),
                [](const std::pair<std::string_view, const asvJSONValue*>& a,
                   const std::pair<std::string_view, const asvJSONValue*>& b) {
                  size_t n = std::min(a.first.size(), b.first.size());
                  for (size_t i = 0; i < n; i++) {
                    unsigned char ca = static_cast<unsigned char>(a.first[i]);
                    unsigned char cb = static_cast<unsigned char>(b.first[i]);
                    if (ca != cb) return ca < cb;
                  }
                  return a.first.size() < b.first.size();
                });
      out += 'd';
      for (const auto& [k, val] : entries) {
        out += std::to_string(k.size());
        out += ':';
        out.append(k.data(), k.size());
        bencodeEncode(val, out, depth + 1);
      }
      out += 'e';
      break;
    }
    default:
      throw asvJSONError("bencode: unsupported value type");
  }
}

inline void asvJSONValue::toBencode(std::string& out) const {
  bencodeEncode(this, out, 0);
}

inline std::string asvJSON::toBencode() const {
  std::string out;
  // No root: nothing to encode. Bencode has no "nothing" value, so the result
  // is the empty string (see the note in the header comment).
  if (root) bencodeEncode(root.get(), out, 0);
  return out;
}

inline bool asvJSON::fromBencode(std::string_view input) {
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    size_t pos = 0;
    auto v = bencodeParseValue(input, pos, 0);
    if (pos != input.size()) throw asvJSONError("bencode: trailing content after value");
    root = std::move(v);
    formatType = FormatType::Bencode;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromUBJSON).
    // Not theoretical here: bencodeParseString / bencodeParseInt build
    // std::string with throwing constructors and the container loops push
    // into vector/map -- only the value factories use new(std::nothrow).
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

// Cheap signature used by fromAny() to try bencode on otherwise-binary input:
// a dictionary whose first key is a byte string (e.g. ".torrent" files).
inline bool bencodeLooksLikeDictionary(std::string_view s) {
  if (s.empty() || s[0] != 'd') return false;
  // Scan the whole key length, not just one digit: a first key of 10 bytes or
  // more ("d13:announce-list...") is common in real .torrent files, and testing
  // only s[2] == ':' rejected those. That was not a harmless false negative --
  // the text path then let JavaProp claim the document and silently return
  // {"d13":"announce-list..."}, while the binary path (real torrents, whose
  // payload has non-UTF-8 bytes) failed to detect any format at all.
  size_t i = 1;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
  // A leading zero in the key length ("d01:...") is invalid bencode -- the
  // parser rejects it -- so it is not a dictionary signature either, even
  // though it passes the scan. `d0:` (a zero-length first key) stays valid.
  if (i > 2 && s[1] == '0') return false;
  return i > 1 && i < s.size() && s[i] == ':';
}

} // namespace asvJSONInternal
