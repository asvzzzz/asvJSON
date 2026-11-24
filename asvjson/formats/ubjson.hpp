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
//   DATETIME-> double seconds (decodes to DOUBLE, type identity is lost; the
//     fraction is double-precision, so a timestamp holds about 10 microseconds
//     of resolution, NOT exact milliseconds)
//   TIMESTAMP -> int64 seconds (decodes to INT, type identity is lost)
//   REGEX   -> S
// Decoding reverses the mapping; H stays a STRING (exact text, no precision
// loss), C becomes a 1-char STRING, and a typed uint8 array decodes to BINARY
// (the spec's binary-data convention, matching py-ubjson and the reference
// C library). The encoder has no C or H forms: C re-encodes as an S ('A' ->
// "A"), and strings always go out as S. Unoptimized containers may be counted
// (`[#<n>`, `{#<n>`) or unbounded (closed by `]`/`}`); `$` requires a `#`
// count per spec, while `#` alone is the ordinary counted form (the decoder
// accepts both; the encoder never writes `#` without `$`). For arrays and
// objects the encoder always writes the plain unbounded form -- the counted
// form is spec-legal (and preferred by some preallocating readers) but the
// simple form is a deliberate choice. The only counted form the encoder ever
// emits is the typed `$U#` array used for the binary payloads (BINARY /
// OBJECTID / EXTENSION, listed above).
//
// `N` is a no-op and never produces a value. Inside an UNTYPED container it is
// skipped without consuming a count slot (so a counted array whose no-ops are
// its only elements does NOT collapse to empty: `[#i3 N N N` falls off the end
// with "unterminated counted array", because the count 3 is still unsatisfied
// after three no-ops -- the alternative "no-op consumes a slot" reading is the
// one that would yield an empty array); at the top level any run of `N`
// before or after the document's value is skipped, and a document consisting
// only of no-ops is rejected ("no value"). Typed containers do NOT skip
// no-ops: their elements are bare payload bytes with no markers, so a 0x4E
// byte there is data (an int8 78), not a no-op -- in a typed object a 0x4E at
// a key-length position is simply an error. `N` in a *type* position is a
// different thing yet again -- `$N#n` declares an array of n no-ops, which per
// the spec have an empty body and yield an EMPTY array (a no-op is not a
// value; `$Z#n` is the null-filling form), while `{$N#n` fixes every value of
// a typed object to null. The array/object asymmetry is intentional: an array
// element may legitimately be empty (no-op = nothing), but an object entry
// must have a value, and there is no "no value" for a value slot -- so the
// object form fills null instead of omitting the pair.
//
// Typed containers: the `$<type>` marker describes the element payloads, so
// `[$i#3` is followed by three BARE int8 bytes with no per-element `i` marker.
// This matches the spec and py-ubjson; per-element markers after the count are
// trailing content and are rejected. The encoder never emits typed containers
// except for BINARY payloads -- it writes plain unbounded `['...']` / `{...}`.
//
// Encoding appends. asvJSONValue::toUBJSON(std::vector<uint8_t>&) pushes onto
// whatever the buffer already holds; it does not clear it. That is the shared
// convention for the binary encoders (toCBOR, toMessagePack and toBSON behave
// identically) and it is what lets a caller build one buffer incrementally. Use
// the no-argument asvJSON::toUBJSON() for a freshly allocated buffer.
//
// UBJSON has no magic leading bytes (it starts with the same '{'/'[' as JSON),
// so fromAny() does not auto-detect it; use fromUBJSON() or the `.ubj`
// extension explicitly.
//
// Limitations:
//   CUSTOM_TAG -> Z (null) on encode, type identity is lost - UBJSON has no
//     tag marker and no payload form that preserves a custom name/value pair.
//     DATETIME -> DOUBLE (fractional seconds survive as double precision, so
//     about 10 microseconds of resolution, not exact ms); TIMESTAMP -> INT64;
//     OBJECTID/EXTENSION -> BINARY (ext_type / type tag lost on round-trip).
//
// Encoder recursion is depth-limited (ubjEncode mirrors ubjParseValue's
// entry check, "UBJSON: nesting too deep"): a tree built programmatically
// past MAX_NESTING_DEPTH (the parser's own ceiling) would otherwise
// exhaust the stack during toUBJSON().

#include "../core.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace asvJSONInternal {

// The encoder writes float32/float64 (and reads them back) by copying raw bit
// patterns into the output and back, so both types must be the IEEE 754
// binary32/binary64 layouts the ubjson.org spec assumes.
static_assert(sizeof(float) == 4 && sizeof(double) == 8,
              "UBJSON needs IEEE 754 binary32/binary64 float and double");

// ----------------------------------------------------------------------------
// Encoding

inline void ubjWriteBE(std::vector<uint8_t>& out, uint64_t v, int n) {
  // Callers pass static_cast<uint64_t>(signed) values. Sign extension fills the
  // high bits (e.g. -32768 -> 0xFFFFFFFFFFFF8000), but only the low n bytes are
  // written, which is exactly the big-endian two's-complement representation of
  // the negative number. Easy to break in a refactor, hence this note.
  for (int i = n - 1; i >= 0; i--)
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// Length/count prefix: smallest type (signed or unsigned) that fits. 'i' has
// room for the signed range (0..127 is the overlap), 'U' covers 128..255 with
// the unsigned uint8 form. The values written are never negative (checked
// below), so the unsigned marker is only ever used for non-negative payloads.
inline void ubjWriteLen(int64_t n, std::vector<uint8_t>& out) {
  uint64_t v = static_cast<uint64_t>(n);
  if (n < 0) throw asvJSONError("UBJSON: negative length");
  if (n <= 127) { out.push_back('i'); out.push_back(static_cast<uint8_t>(v)); }
  else if (n <= 255) { out.push_back('U'); out.push_back(static_cast<uint8_t>(v)); }
  else if (n <= 32767) { out.push_back('I'); ubjWriteBE(out, v, 2); }
  else if (n <= 2147483647LL) { out.push_back('l'); ubjWriteBE(out, v, 4); }
  else { out.push_back('L'); ubjWriteBE(out, v, 8); }
}

inline void ubjEncode(const asvJSONValue* v, std::vector<uint8_t>& out, int depth = 0) {
  using T = asvJSONValue::Type;
  // Depth guard mirrors ubjParseValue's entry check (depth > MAX throws),
  // incremented per child on both sides: a programmatically built tree is
  // not bounded by MAX_NESTING_DEPTH and the encoder recursion overflowed
  // the stack.
  if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
    throw asvJSONError("UBJSON: nesting too deep");
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
      for (const auto& e : *v->arr) ubjEncode(e.get(), out, depth + 1);
      out.push_back(']');
      break;
    }
    case T::OBJECT: {
      out.push_back('{');
      for (const auto& [k, val] : *v->obj) {
        ubjWriteLen(static_cast<int64_t>(k.size()), out);
        out.insert(out.end(), k.begin(), k.end());
        ubjEncode(val.get(), out, depth + 1);
      }
      out.push_back('}');
      break;
    }
    default:
      // CUSTOM_TAG is the only core type not handled above. UBJSON has no
      // extension-tag marker, so there is no lossless encoding path - degrade
      // to null (Z) rather than emitting garbage. Documented in the header
      // limitations; callers that need round-trip fidelity should convert to
      // a supported form before calling toUBJSON().
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
  int64_t sv = 0;  // signed reading, so a negative length can be rejected.
                   // 'U' never touches sv (uint8 can't be negative) -- it stays 0.
  if (m == 'i') {
    if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (int8)");
    sv = static_cast<int8_t>(data[pos++]);
    v = static_cast<uint64_t>(sv);
  } else if (m == 'U') {
    if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (uint8)");
    v = data[pos++];
  } else if (m == 'I') {
    if (pos + 2 > dataLen) throw asvJSONError("UBJSON: unexpected end (int16)");
    sv = static_cast<int16_t>(ubjReadBE(data, pos, 2));
    pos += 2;
    v = static_cast<uint64_t>(sv);
  } else if (m == 'l') {
    if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (int32)");
    sv = static_cast<int32_t>(ubjReadBE(data, pos, 4));
    pos += 4;
    v = static_cast<uint64_t>(sv);
  } else if (m == 'L') {
    if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (int64)");
    sv = static_cast<int64_t>(ubjReadBE(data, pos, 8));
    pos += 8;
    v = static_cast<uint64_t>(sv);
  } else {
    throw asvJSONError("UBJSON: invalid length type marker");
  }
  // A negative signed length is malformed, not a huge unsigned one. Without this
  // the sign extension turned -1 into UINT64_MAX, which then tripped a downstream
  // size guard and reported the wrong cause ("string too long" for S i FF, "array
  // too large" for [ # i FF) instead of naming the actual defect.
  if (sv < 0) throw asvJSONError("UBJSON: negative length");
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

// Scalar value from an already-known marker. Shared by ubjParseValue (which
// reads the marker from the stream) and ubjParseTyped (which receives it from a
// typed container's $<type> header). Both paths previously duplicated the same
// ~100-line switch; a single switch here means a future scalar type cannot
// drift between the two.
inline std::unique_ptr<asvJSONValue> ubjParseScalar(const uint8_t* data, size_t& pos,
                                                    size_t dataLen, int depth, char m) {
  switch (m) {
    // 'N' is a no-op and never yields a value. REACHABLE (probe sw85): the
    // container loops skip no-ops in ELEMENT / KEY position only (per the
    // spec's formal grammar, a no-op is an element-level alternative, never
    // a value), so an 'N' in an object's VALUE slot arrives here straight
    // from ubjParseValue - "{a N 1}" and the counted "{#i1 U1 K N}" forms
    // both throw this named error (pinned in the UBJSON no-op test). The
    // top-level leading/trailing skips live in fromUBJSON, and the $N *type*
    // position is handled by the container branch, not here. Do not delete
    // this case as "dead code": it is the documented rejection point.
    case 'N': throw asvJSONError("UBJSON: no-op where a value is required");
    case 'Z': { auto v = asvJSONValue::makeNull(); if (!v) throw asvJSONError("UBJSON: null alloc failed"); return v; }
    case 'T': { auto v = asvJSONValue::makeBool(true); if (!v) throw asvJSONError("UBJSON: bool alloc failed"); return v; }
    case 'F': { auto v = asvJSONValue::makeBool(false); if (!v) throw asvJSONError("UBJSON: bool alloc failed"); return v; }
    case 'i': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (int8)");
      auto v = asvJSONValue::makeInt(static_cast<int8_t>(data[pos++]));
      if (!v) throw asvJSONError("UBJSON: integer alloc failed");
      return v;
    }
    case 'U': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (uint8)");
      auto v = asvJSONValue::makeInt(data[pos++]);
      if (!v) throw asvJSONError("UBJSON: integer alloc failed");
      return v;
    }
    case 'I': {
      if (pos + 2 > dataLen) throw asvJSONError("UBJSON: unexpected end (int16)");
      int64_t v = static_cast<int16_t>(ubjReadBE(data, pos, 2));
      pos += 2;
      auto r = asvJSONValue::makeInt(v);
      if (!r) throw asvJSONError("UBJSON: integer alloc failed");
      return r;
    }
    case 'l': {
      if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (int32)");
      int64_t v = static_cast<int32_t>(ubjReadBE(data, pos, 4));
      pos += 4;
      auto r = asvJSONValue::makeInt(v);
      if (!r) throw asvJSONError("UBJSON: integer alloc failed");
      return r;
    }
    case 'L': {
      if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (int64)");
      int64_t v = static_cast<int64_t>(ubjReadBE(data, pos, 8));
      pos += 8;
      auto r = asvJSONValue::makeInt(v);
      if (!r) throw asvJSONError("UBJSON: integer alloc failed");
      return r;
    }
    case 'd': {
      if (pos + 4 > dataLen) throw asvJSONError("UBJSON: unexpected end (float32)");
      uint32_t bits = static_cast<uint32_t>(ubjReadBE(data, pos, 4));
      pos += 4;
      float f;
      memcpy(&f, &bits, sizeof(f));
      auto v = asvJSONValue::makeDouble(f);
      if (!v) throw asvJSONError("UBJSON: float alloc failed");
      v->is_float32 = true;
      return v;
    }
    case 'D': {
      if (pos + 8 > dataLen) throw asvJSONError("UBJSON: unexpected end (float64)");
      uint64_t bits = ubjReadBE(data, pos, 8);
      pos += 8;
      double d;
      memcpy(&d, &bits, sizeof(d));
      auto v = asvJSONValue::makeDouble(d);
      if (!v) throw asvJSONError("UBJSON: float alloc failed");
      return v;
    }
    case 'C': {
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end (char)");
      char c = static_cast<char>(data[pos++]);
      auto v = asvJSONValue::makeString(&c, 1);
      if (!v) throw asvJSONError("UBJSON: string alloc failed");
      return v;
    }
    case 'S':
    case 'H': {
      std::string s = ubjReadRawString(data, pos, dataLen);
      auto v = asvJSONValue::makeString(s.data(), s.size());
      if (!v) throw asvJSONError("UBJSON: string alloc failed");
      return v;
    }
    case '[': case '{': {
      // Only reachable from ubjParseTyped (the declared $<type> element type):
      // ubjParseValue consumes the real marker before dispatching, so it never
      // forwards a container marker here. A container declared by the header
      // must begin with that very marker in the payload -- a mismatch means the
      // typed container is malformed (the declared type is a promise about the
      // payload format), not permission to re-interpret the element.
      if (pos >= dataLen) throw asvJSONError("UBJSON: unexpected end of input");
      char actual = static_cast<char>(data[pos]);
      if (actual != m)
        throw asvJSONError(m == '[' ? "UBJSON: typed array element marker mismatch"
                                    : "UBJSON: typed object element marker mismatch");
      return ubjParseValue(data, pos, dataLen, depth);
    }
    default:
      throw asvJSONError("UBJSON: unknown marker");
  }
}

inline std::unique_ptr<asvJSONValue> ubjParseTyped(const uint8_t* data, size_t& pos,
                                                   size_t dataLen, int depth, char m) {
  // Scalar element of a typed container; m is known from the $<type> header.
  return ubjParseScalar(data, pos, dataLen, depth, m);
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
      // The spec's binary-data convention: a typed uint8 array is raw bytes.
      // Its size bound is the BINARY payload limit (MAX_STRING_LEN, the same
      // limit makeBinary enforces internally via checkStringLen -- a binary
      // too large for the factory is rejected here so the call below never
      // parses into a nullptr slip), NOT the element-count bound: an encoder
      // may legally emit a binary whose size trips MAX_ARRAY_SIZE, so the
      // "array too large" guard below must not fire for it. It still bounds
      // every other $type, after this branch.
      if (t == 'U') {
        if (count > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("UBJSON: binary too large");
        if (count > dataLen - pos) throw asvJSONError("UBJSON: truncated binary");
        auto bin = asvJSONValue::makeBinary(data + pos, static_cast<size_t>(count));
        if (!bin) throw asvJSONError("UBJSON: binary alloc failed");
        pos += static_cast<size_t>(count);
        return bin;
      }
      if (count > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("UBJSON: array too large");
      // $Z/$T/$F have no payload; each element's value is implicit. $N is a
      // no-op: per spec its array is count no-op values with an EMPTY body --
      // a no-op produces no value, so the array is empty (deliberately unlike
      // $Z#n, which is n nulls).
      if (t == 'Z') {
        // makeNull/makeBool return nullptr on allocation failure. The general
        // element branch throws inside ubjParseScalar, but these implicit-value
        // forms never reach it, so the result must be checked here -- a null
        // unique_ptr pushed next would be dereferenced by any later walk.
        for (uint64_t i = 0; i < count; i++) {
          auto v = asvJSONValue::makeNull();
          if (!v) throw asvJSONError("UBJSON: array element alloc failed");
          arr->arr->push_back(std::move(v));
        }
      }
      else if (t == 'T' || t == 'F') {
        const bool b = (t == 'T');
        for (uint64_t i = 0; i < count; i++) {
          auto v = asvJSONValue::makeBool(b);
          if (!v) throw asvJSONError("UBJSON: array element alloc failed");
          arr->arr->push_back(std::move(v));
        }
      }
      else if (t == 'N') {
        // Empty: the no-op array carries no values (spec: "equivalent to an
        // array containing count N markers", and the body must be empty).
      }
      else {
        for (uint64_t i = 0; i < count; ) {
          if (pos >= dataLen) throw asvJSONError("UBJSON: unterminated typed array");
          // No no-op skip here: typed elements are bare payload bytes, so a
          // 0x4E byte is data (int8 78), not a marker to consume.
          auto elem = ubjParseTyped(data, pos, dataLen, depth + 1, t);
          if (!elem) throw asvJSONError("UBJSON: array element alloc failed");
          arr->arr->push_back(std::move(elem));
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
        auto elem = ubjParseValue(data, pos, dataLen, depth + 1);
        if (!elem) throw asvJSONError("UBJSON: array element alloc failed");
        arr->arr->push_back(std::move(elem));
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
      auto elem = ubjParseValue(data, pos, dataLen, depth + 1);
      if (!elem) throw asvJSONError("UBJSON: array element alloc failed");
      arr->arr->push_back(std::move(elem));
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
        // No no-op skip: the key-length byte is bare payload -- a 0x4E there
        // is an invalid length marker, not a no-op to consume.
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

  // All scalar types share one switch with the typed-container path; m was
  // read from the stream here but is handled identically everywhere.
  return ubjParseScalar(data, pos, dataLen, depth, m);
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
  lastError.clear();
  // Input size limit, symmetric with CBOR/BSON/MessagePack (12 MB: just above
  // MAX_STRING_LEN so the string-length guards fire before the cap does).
  constexpr size_t MAX_UBJSON_INPUT = 12u * 1024u * 1024u;
  if (size > MAX_UBJSON_INPUT) { lastError = "UBJSON input exceeds 12 MB limit"; return false; }
  if (!data || size < 1) { lastError = "UBJSON: empty input"; return false; }
  const uint8_t* u = static_cast<const uint8_t*>(data);
  try {
    size_t p = 0;
    // 'N' is a no-op, not a value. Skip any run of them before the document's
    // single value and after it; a document made only of no-ops carries no value
    // at all. Previously 'N' in value position returned null, while the very same
    // byte inside a container was skipped -- so [N] decoded to null but [i5][N]
    // failed with "trailing content".
    while (p < size && u[p] == 'N') p++;
    if (p >= size) throw asvJSONError("UBJSON: no value (only no-ops)");
    root = ubjParseValue(u, p, size, 0);
    while (p < size && u[p] == 'N') p++;
    if (!root || p != size) {
      root = nullptr;
      throw asvJSONError(p != size ? "UBJSON: trailing content after value" : "UBJSON: parse failed");
    }
    formatType = FormatType::UBJSON;
    return true;
  } catch (const asvJSONError& e) { lastError = e.what(); root = nullptr; dirty_ = false; return false; }
  catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromMessagePack).
    // Not theoretical here: ubjReadRawString allocates std::string and the
    // container loops push into std::vector -- only the value factories use
    // new(std::nothrow).
    lastError = e.what(); root = nullptr; dirty_ = false; return false;
  }
}

} // namespace asvJSONInternal

// Convenience overload: delegates to the byte-pointer form without copying.
// Takes `const std::string&` (not std::string_view) to mirror the MessagePack /
// BSON / CBOR string overloads in core.hpp -- the parse itself borrows the
// caller's buffer either way, so there is nothing to gain from a view here.
inline bool asvJSON::fromUBJSON(const std::string& data) {
  return fromUBJSON(data.data(), data.size());
}
