#pragma once
// UBJSON (Universal Binary JSON) serialization/parsing for asvJSON++.
//
// Marker set (big-endian for all multi-byte numerics, per ubjson.org):
//   Z null, N noop, T/F bool, i int8, U uint8, I int16, l int32, L int64,
//   d float32, D float64, H high-precision number (string payload),
//   C char (1 byte), S string, '[' array (with optional $type / #count),
//   '{' object (keys are bare length-prefixed strings, no S marker).
//
// Encoding maps asvJSON types onto the JSON-compatible subset:
//   BOOL    -> T/F           NULL -> Z
//   INT     -> smallest integer type (signed or unsigned) that fits
//   DOUBLE  -> D (double64), or d (float32) when is_float32 is set
//   STRING  -> S
//   BINARY payload -> typed uint8 array `[$U#<n>...`
//   OBJECTID payload (stored in str_data per BSON/XML/TRON) -> typed uint8
//     array `[$U#12...`; decodes to BINARY, type identity is lost on round-trip
//   EXTENSION payload + ext_type -> typed uint8 array `[$U#<n>...`; ext_type
//     is lost on round-trip, value decodes back to plain BINARY
//   ARRAY   -> '[' items ']'      OBJECT -> '{' key/value pairs '}'
//   DATETIME-> double seconds (decodes to DOUBLE, type identity is lost)
//   TIMESTAMP -> int64 seconds (decodes to INT, type identity is lost)
//   REGEX   -> S
// Decoding reverses the mapping; H stays a STRING (exact text, no precision
// loss), C becomes a 1-char STRING, and a typed uint8 array decodes to BINARY
// (the spec's binary-data convention, matching py-ubjson and the reference
// C library). Unoptimized containers may be counted (`[#<n>`, `{#<n>`) or
// unbounded (closed by `]`/`}`); `$` requires a `#` count per spec. `N` noop
// markers are skipped inside containers without consuming a count slot.
//
// UBJSON has no magic leading bytes (it starts with the same '{'/'[' as JSON),
// so fromAny() does not auto-detect it; use fromUBJSON() or the `.ubj`
// extension explicitly.

#include "../core.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace asvJSONInternal {

// ----------------------------------------------------------------------------
// Encoding

inline void ubjWriteBE(std::vector<uint8_t>& out, uint64_t v, int n) {
  for (int i = n - 1; i >= 0; i--)
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// Length/count prefix: smallest integer type (signed) that fits.
inline void ubjWriteLen(int64_t n, std::vector<uint8_t>& out) {
  uint64_t v = static_cast<uint64_t>(n);
  if (n < 0) throw asvJSONError("UBJSON: negative length");
  if (n <= 127) { out.push_back('i'); out.push_back(static_cast<uint8_t>(v)); }
  else if (n <= 255) { out.push_back('U'); out.push_back(static_cast<uint8_t>(v)); }
  else if (n <= 32767) { out.push_back('I'); ubjWriteBE(out, v, 2); }
  else if (n <= 2147483647LL) { out.push_back('l'); ubjWriteBE(out, v, 4); }
  else { out.push_back('L'); ubjWriteBE(out, v, 8); }
}

inline void ubjEncode(const asvJSONValue* v, std::vector<uint8_t>& out) {
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::NULL_VAL:
      out.push_back('Z');
      break;
    case T::BOOL_VAL:
      out.push_back(v->flag ? 'T' : 'F');
      break;
    case T::INT: {
      int64_t n = v->num;
      if (n >= -128 && n <= 127) { out.push_back('i'); out.push_back(static_cast<uint8_t>(static_cast<int8_t>(n))); }
      else if (n >= 0 && n <= 255) { out.push_back('U'); out.push_back(static_cast<uint8_t>(n)); }
      else if (n >= -32768 && n <= 32767) { out.push_back('I'); ubjWriteBE(out, static_cast<uint64_t>(static_cast<int16_t>(n)), 2); }
      else if (n >= -2147483648LL && n <= 2147483647LL) { out.push_back('l'); ubjWriteBE(out, static_cast<uint64_t>(static_cast<int32_t>(n)), 4); }
      else { out.push_back('L'); ubjWriteBE(out, static_cast<uint64_t>(n), 8); }
      break;
    }
    case T::DOUBLE: {
      if (v->is_float32) {
        out.push_back('d');
        float f = static_cast<float>(v->dbl);
        uint32_t bits;
        memcpy(&bits, &f, sizeof(bits));
        ubjWriteBE(out, bits, 4);
      } else {
        out.push_back('D');
        uint64_t bits;
        memcpy(&bits, &v->dbl, sizeof(bits));
        ubjWriteBE(out, bits, 8);
      }
      break;
    }
    case T::STRING:
      out.push_back('S');
      ubjWriteLen(static_cast<int64_t>(v->str_data.size()), out);
      out.insert(out.end(), v->str_data.begin(), v->str_data.end());
      break;
    case T::BINARY: {
      // Spec binary-data convention: a strongly typed array of uint8.
      out.push_back('[');
      out.push_back('$');
      out.push_back('U');
      out.push_back('#');
      ubjWriteLen(static_cast<int64_t>(v->bin_data.size()), out);
      out.insert(out.end(), v->bin_data.begin(), v->bin_data.end());
      break;
    }
    case T::OBJECTID: {
      // OBJECTID stores its 12 bytes in str_data (see makeObjectId / BSON/XML/TRON).
      // Emit as a typed uint8 array so the payload survives round-trip; decodes to BINARY.
      out.push_back('[');
      out.push_back('$');
      out.push_back('U');
      out.push_back('#');
      ubjWriteLen(static_cast<int64_t>(v->str_data.size()), out);
      out.insert(out.end(), v->str_data.begin(), v->str_data.end());
      break;
    }
    case T::EXTENSION: {
      // EXTENSION payload (bin_data) is emitted as a typed uint8 array; ext_type is lost.
      out.push_back('[');
      out.push_back('$');
      out.push_back('U');
      out.push_back('#');
      ubjWriteLen(static_cast<int64_t>(v->bin_data.size()), out);
      out.insert(out.end(), v->bin_data.begin(), v->bin_data.end());
      break;
    }
    case T::DATETIME: {
      double sec = static_cast<double>(v->timestamp) + static_cast<double>(v->datetime_ms) / 1000.0;
      out.push_back('D');
      uint64_t bits;
      memcpy(&bits, &sec, sizeof(bits));
      ubjWriteBE(out, bits, 8);
      break;
    }
    case T::TIMESTAMP: {
      int64_t n = v->num;
      if (n >= -128 && n <= 127) { out.push_back('i'); out.push_back(static_cast<uint8_t>(static_cast<int8_t>(n))); }
      else if (n >= 0 && n <= 255) { out.push_back('U'); out.push_back(static_cast<uint8_t>(n)); }
      else if (n >= -32768 && n <= 32767) { out.push_back('I'); ubjWriteBE(out, static_cast<uint64_t>(static_cast<int16_t>(n)), 2); }
      else if (n >= -2147483648LL && n <= 2147483647LL) { out.push_back('l'); ubjWriteBE(out, static_cast<uint64_t>(static_cast<int32_t>(n)), 4); }
      else { out.push_back('L'); ubjWriteBE(out, static_cast<uint64_t>(n), 8); }
      break;
    }
    case T::REGEX:
      out.push_back('S');
      ubjWriteLen(static_cast<int64_t>(v->str_data.size()), out);
      out.insert(out.end(), v->str_data.begin(), v->str_data.end());
      break;
    case T::ARRAY: {
      out.push_back('[');
      for (const auto& e : *v->arr) ubjEncode(e.get(), out);
      out.push_back(']');
      break;
    }
    case T::OBJECT: {
      out.push_back('{');
      for (const auto& [k, val] : *v->obj) {
        ubjWriteLen(static_cast<int64_t>(k.size()), out);
        out.insert(out.end(), k.begin(), k.end());
        ubjEncode(val.get(), out);
      }
      out.push_back('}');
      break;
    }
    default:
      out.push_back('Z');
      break;
  }
}

// ----------------------------------------------------------------------------
// Decoding

inline uint64_t ubjReadBE(const uint8_t* data, size_t pos, int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; i++) v = (v << 8) | data[pos + i];
  return v;
}

// Reads a length/count value introduced by a marker byte.
inline uint64_t ubjReadLen(const uint8_t* data, size_t& pos, size_t dataLen) {
  if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (length)");
  char m = static_cast<char>(data[pos++]);
  uint64_t v = 0;
  if (m == 'i') {
    if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (int8)");
    v = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(data[pos++])));
  } else if (m == 'U') {
    if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (uint8)");
    v = data[pos++];
  } else if (m == 'I') {
    if (pos + 2 > dataLen) throw asvJSONError("UBJSON: unexpected end (int16)");
    v = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(ubjReadBE(data, pos, 2))));
    pos += 2;
  } else if (m == 'l') {
    if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (int32)");
    v = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(ubjReadBE(data, pos, 4))));
    pos += 4;
  } else if (m == 'L') {
    if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (int64)");
    v = ubjReadBE(data, pos, 8);
    pos += 8;
  } else {
    throw asvJSONError("UBJSON: invalid length type marker");
  }
  return v;
}

inline std::string ubjReadRawString(const uint8_t* data, size_t& pos, size_t dataLen) {
  uint64_t n = ubjReadLen(data, pos, dataLen);
  if (n > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("UBJSON: string too long");
  if (n > dataLen - pos) throw asvJSONError("UBJSON: truncated string");
  std::string s(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(n));
  pos += static_cast<size_t>(n);
  return s;
}

inline std::unique_ptr<asvJSONValue> ubjParseTyped(const uint8_t* data, size_t& pos,
                                                   size_t dataLen, int depth, char m);
inline std::unique_ptr<asvJSONValue> ubjParseValue(const uint8_t* data, size_t& pos,
                                                   size_t dataLen, int depth);

inline std::unique_ptr<asvJSONValue> ubjParseTyped(const uint8_t* data, size_t& pos,
                                                   size_t dataLen, int depth, char m) {
  switch (m) {
    case 'Z': case 'N': return asvJSONValue::makeNull();
    case 'T': return asvJSONValue::makeBool(true);
    case 'F': return asvJSONValue::makeBool(false);
    case 'i': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (int8)");
      return asvJSONValue::makeInt(static_cast<int8_t>(data[pos++]));
    }
    case 'U': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (uint8)");
      return asvJSONValue::makeInt(data[pos++]);
    }
    case 'I': {
      if (pos + 2 > dataLen) throw asvJSONError("UBJSON: unexpected end (int16)");
      int64_t v = static_cast<int16_t>(ubjReadBE(data, pos, 2));
      pos += 2;
      return asvJSONValue::makeInt(v);
    }
    case 'l': {
      if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (int32)");
      int64_t v = static_cast<int32_t>(ubjReadBE(data, pos, 4));
      pos += 4;
      return asvJSONValue::makeInt(v);
    }
    case 'L': {
      if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (int64)");
      int64_t v = static_cast<int64_t>(ubjReadBE(data, pos, 8));
      pos += 8;
      return asvJSONValue::makeInt(v);
    }
    case 'd': {
      if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (float32)");
      uint32_t bits = static_cast<uint32_t>(ubjReadBE(data, pos, 4));
      pos += 4;
      float f;
      memcpy(&f, &bits, sizeof(f));
      auto v = asvJSONValue::makeDouble(f);
      if (v) v->is_float32 = true;
      return v;
    }
    case 'D': {
      if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (float64)");
      uint64_t bits = ubjReadBE(data, pos, 8);
      pos += 8;
      double d;
      memcpy(&d, &bits, sizeof(d));
      return asvJSONValue::makeDouble(d);
    }
    case 'C': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (char)");
      char c = static_cast<char>(data[pos++]);
      return asvJSONValue::makeString(&c, 1);
    }
    case 'S':
    case 'H': {
      std::string s = ubjReadRawString(data, pos, dataLen);
      auto v = asvJSONValue::makeString(s.data(), s.size());
      if (!v) throw asvJSONError("UBJSON: string alloc failed");
      return v;
    }
    case '[': case '{':
      return ubjParseValue(data, pos, dataLen, depth);
    default:
      throw asvJSONError("UBJSON: unknown typed value marker");
  }
}

inline std::unique_ptr<asvJSONValue> ubjParseValue(const uint8_t* data, size_t& pos,
                                                   size_t dataLen, int depth) {
  if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
    throw asvJSONError("UBJSON: nesting too deep");
  if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end of input");
  char m = static_cast<char>(data[pos++]);

  if (m == '[') {
    auto arr = asvJSONValue::makeArray();
    if (!arr) throw asvJSONError("UBJSON: array alloc failed");
    // Typed container: `[$<type>#<count>` + bare values (no end marker).
    if (pos < dataLen && data[pos] == '$') {
      pos++;
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end ($)");
      char t = static_cast<char>(data[pos++]);
        if (pos >= dataLen || data[pos] != '#') throw asvJSONError("UBJSON: typed container requires a count");
        pos++;
        uint64_t count = ubjReadLen(data, pos, dataLen);
        if (count > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("UBJSON: array too large");
      // The spec's binary-data convention: a typed uint8 array is raw bytes.
      if (t == 'U') {
        if (count > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("UBJSON: binary too large");
        if (count > dataLen - pos) throw asvJSONError("UBJSON: truncated binary");
        auto bin = asvJSONValue::makeBinary(data + pos, static_cast<size_t>(count));
        if (!bin) throw asvJSONError("UBJSON: binary alloc failed");
        pos += static_cast<size_t>(count);
        return bin;
      }
      // $Z/$N/$T/$F have no payload; each element's value is implicit.
      if (t == 'Z' || t == 'N') { for (uint64_t i = 0; i < count; i++) arr->arr->push_back(asvJSONValue::makeNull()); }
      else if (t == 'T' || t == 'F') { for (uint64_t i = 0; i < count; i++) arr->arr->push_back(asvJSONValue::makeBool(t == 'T')); }
        else {
        for (uint64_t i = 0; i < count; ) {
          if (pos >= dataLen) throw asvJSONError("UBJSON: unterminated typed array");
          if (data[pos] == 'N') { pos++; continue; }
          arr->arr->push_back(ubjParseTyped(data, pos, dataLen, depth + 1, t));
          i++;
        }
      }
      return arr;
    }
    // Counted container: `[#<count>` + values (no end marker).
    if (pos < dataLen && data[pos] == '#') {
      pos++;
      uint64_t count = ubjReadLen(data, pos, dataLen);
      if (count > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("UBJSON: array too large");
      while (count > 0) {
        if (pos >= dataLen) throw asvJSONError("UBJSON: unterminated counted array");
        if (data[pos] == 'N') { pos++; continue; }
        arr->arr->push_back(ubjParseValue(data, pos, dataLen, depth + 1));
        count--;
      }
      return arr;
    }
    // Unbounded array: values until `]`.
    for (;;) {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unterminated array");
      if (data[pos] == ']') { pos++; break; }
      if (data[pos] == 'N') { pos++; continue; }
      if (arr->arr->size() >= asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("UBJSON: array too large");
      arr->arr->push_back(ubjParseValue(data, pos, dataLen, depth + 1));
    }
    return arr;
  }

  if (m == '{') {
    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("UBJSON: object alloc failed");
    // Typed object: `{$<type>#<count>` + keys (+ bare values unless the type
    // is Z/N/T/F, which fix the value for every name with no payload).
    if (pos < dataLen && data[pos] == '$') {
      pos++;
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end ($)");
      char t = static_cast<char>(data[pos++]);
        if (pos >= dataLen || data[pos] != '#') throw asvJSONError("UBJSON: typed container requires a count");
        pos++;
        uint64_t count = ubjReadLen(data, pos, dataLen);
        if (count > asvJSONValue::MAX_OBJECT_SIZE) throw asvJSONError("UBJSON: object too large");
        for (uint64_t i = 0; i < count; ) {
          if (pos >= dataLen) throw asvJSONError("UBJSON: unterminated typed object");
          if (data[pos] == 'N') { pos++; continue; }
          std::string key = ubjReadRawString(data, pos, dataLen);
          std::unique_ptr<asvJSONValue> val;
          if (t == 'Z' || t == 'N') val = asvJSONValue::makeNull();
          else if (t == 'T') val = asvJSONValue::makeBool(true);
          else if (t == 'F') val = asvJSONValue::makeBool(false);
          else val = ubjParseTyped(data, pos, dataLen, depth + 1, t);
          if (!val) throw asvJSONError("UBJSON: object value alloc failed");
          obj->obj->insert_or_assign(key, std::move(val));
          i++;
        }
        return obj;
    }
    if (pos < dataLen && data[pos] == '#') {
      pos++;
      uint64_t count = ubjReadLen(data, pos, dataLen);
      if (count > asvJSONValue::MAX_OBJECT_SIZE) throw asvJSONError("UBJSON: object too large");
      while (count > 0) {
        if (pos >= dataLen) throw asvJSONError("UBJSON: unterminated counted object");
        if (data[pos] == 'N') { pos++; continue; }
        std::string key = ubjReadRawString(data, pos, dataLen);
        auto val = ubjParseValue(data, pos, dataLen, depth + 1);
        if (!val) throw asvJSONError("UBJSON: object value alloc failed");
        obj->obj->insert_or_assign(key, std::move(val));
        count--;
      }
      return obj;
    }
    // Unbounded object: keys (bare length-prefixed strings) + values.
    for (;;) {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unterminated object");
      if (data[pos] == '}') { pos++; break; }
      if (data[pos] == 'N') { pos++; continue; }
      if (obj->obj->size() >= asvJSONValue::MAX_OBJECT_SIZE) throw asvJSONError("UBJSON: object too large");
      std::string key = ubjReadRawString(data, pos, dataLen);
      auto val = ubjParseValue(data, pos, dataLen, depth + 1);
      if (!val) throw asvJSONError("UBJSON: object value alloc failed");
      obj->obj->insert_or_assign(key, std::move(val));
    }
    return obj;
  }

  switch (m) {
    case 'Z': case 'N': return asvJSONValue::makeNull();
    case 'T': return asvJSONValue::makeBool(true);
    case 'F': return asvJSONValue::makeBool(false);
    case 'i': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (int8)");
      return asvJSONValue::makeInt(static_cast<int8_t>(data[pos++]));
    }
    case 'U': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (uint8)");
      return asvJSONValue::makeInt(data[pos++]);
    }
    case 'I': {
      if (pos + 2 > dataLen) throw asvJSONError("UBJSON: unexpected end (int16)");
      int64_t v = static_cast<int16_t>(ubjReadBE(data, pos, 2));
      pos += 2;
      return asvJSONValue::makeInt(v);
    }
    case 'l': {
      if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (int32)");
      int64_t v = static_cast<int32_t>(ubjReadBE(data, pos, 4));
      pos += 4;
      return asvJSONValue::makeInt(v);
    }
    case 'L': {
      if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (int64)");
      int64_t v = static_cast<int64_t>(ubjReadBE(data, pos, 8));
      pos += 8;
      return asvJSONValue::makeInt(v);
    }
    case 'd': {
      if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (float32)");
      uint32_t bits = static_cast<uint32_t>(ubjReadBE(data, pos, 4));
      pos += 4;
      float f;
      memcpy(&f, &bits, sizeof(f));
      auto v = asvJSONValue::makeDouble(f);
      if (v) v->is_float32 = true;
      return v;
    }
    case 'D': {
      if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (float64)");
      uint64_t bits = ubjReadBE(data, pos, 8);
      pos += 8;
      double d;
      memcpy(&d, &bits, sizeof(d));
      return asvJSONValue::makeDouble(d);
    }
    case 'C': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (char)");
      char c = static_cast<char>(data[pos++]);
      return asvJSONValue::makeString(&c, 1);
    }
    case 'S': case 'H': {
      std::string s = ubjReadRawString(data, pos, dataLen);
      auto v = asvJSONValue::makeString(s.data(), s.size());
      if (!v) throw asvJSONError("UBJSON: string alloc failed");
      return v;
    }
    default:
      throw asvJSONError("UBJSON: unknown marker");
  }
}

inline void asvJSONValue::toUBJSON(std::vector<uint8_t>& out) const {
  ubjEncode(this, out);
}

inline std::vector<uint8_t> asvJSON::toUBJSON() const {
  std::vector<uint8_t> out;
  if (root) ubjEncode(root.get(), out);
  return out;
}

inline bool asvJSON::fromUBJSON(const void* data, size_t size) {
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  if (!data || size < 1) return false;
  const uint8_t* u = static_cast<const uint8_t*>(data);
  try {
    size_t p = 0;
    root = ubjParseValue(u, p, size, 0);
    if (!root || p != size) {
      root = nullptr;
      throw asvJSONError(p != size ? "UBJSON: trailing content after value" : "UBJSON: parse failed");
    }
    formatType = FormatType::UBJSON;
    return true;
  } catch (const asvJSONError& e) { lastError = e.what(); root = nullptr; return false; }
}

} // namespace asvJSONInternal

// string_view convenience overload
inline bool asvJSON::fromUBJSON(const std::string& data) {
  return fromUBJSON(data.data(), data.size());
}