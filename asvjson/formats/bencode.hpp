#pragma once
// Bencode serialization/parsing for asvJSON++.
//
// Bencode is the BitTorrent encoding (used in .torrent files):
//   i42e              integer
//   4:spam            byte string
//   l4:spami42ee      list
//   d3:bar4:spam3:fooi42ee   dictionary (keys sorted as byte strings)
//
// Bencode has no booleans, null or floating-point values. Decoding maps
// integers to INT, byte strings to STRING when the bytes are valid UTF-8 and
// to BINARY otherwise, lists to ARRAY and dictionaries to OBJECT. Encoding
// accepts INT, STRING, BINARY, ARRAY and OBJECT; any other type (and an
// unsupported root) throws. Dictionary keys are emitted in ascending byte
// order as the specification requires.

#include "../core.hpp"

#include <algorithm>
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
  if (c == 'i') return asvJSONValue::makeInt(bencodeParseInt(s, pos));
  if (c == 'l') {
    pos++;
    auto arr = asvJSONValue::makeArray();
    if (!arr) throw asvJSONError("bencode: out of memory");
    for (;;) {
      if (pos >= s.size()) throw asvJSONError("bencode: unterminated list");
      if (s[pos] == 'e') { pos++; break; }
      if (arr->arr->size() >= asvJSONValue::MAX_ARRAY_SIZE)
        throw asvJSONError("bencode: list too large");
      arr->arr->push_back(bencodeParseValue(s, pos, depth + 1));
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

inline void bencodeEncode(const asvJSONValue* v, std::string& out) {
  if (!v) throw asvJSONError("bencode: invalid null value");
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
      for (const auto& e : *v->arr) bencodeEncode(e.get(), out);
      out += 'e';
      break;
    }
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
        bencodeEncode(val, out);
      }
      out += 'e';
      break;
    }
    default:
      throw asvJSONError("bencode: unsupported value type");
  }
}

inline void asvJSONValue::toBencode(std::string& out) const {
  bencodeEncode(this, out);
}

inline std::string asvJSON::toBencode() const {
  std::string out;
  if (root) bencodeEncode(root.get(), out);
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
  }
}

// Cheap signature used by fromAny() to try bencode on otherwise-binary input:
// a dictionary whose first key is a byte string (e.g. ".torrent" files).
inline bool bencodeLooksLikeDictionary(std::string_view s) {
  return s.size() >= 3 && s[0] == 'd' && s[1] >= '0' && s[1] <= '9' && s[2] == ':';
}

} // namespace asvJSONInternal
