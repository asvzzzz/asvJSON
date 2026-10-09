#pragma once
// CBOR (RFC 8949) serialization/parsing for asvJSON++
//
// Timestamp vs DateTime (asvJSON-specific extension of tag 1):
//   DATETIME is written as tag 1 + float64 (seconds, with a .ms fraction) and
//   TIMESTAMP as tag 1 + an integer number of seconds; the decoder tells them
//   apart by the type of the tagged item (INT -> TIMESTAMP, DOUBLE ->
//   DATETIME), so a round-trip is symmetric. RFC 8949 3.4.1 only says "tag 1 +
//   integer = epoch-based date/time" and does not draw that distinction, so a
//   hand-written C1 1A ... (tag 1 + uint) decodes here as TIMESTAMP, not as a
//   DATETIME, even if its author meant a date/time. That is a deliberate
//   design choice, needed because the two node types otherwise collapse into
//   one another; external consumers see a conformant tag 1 either way.
//
// Documented lossiness (none of it is a decode error -- the value survives, but
// in a different shape):
//   - DATETIME inherits float64's exact-integer range, because tag 1 carries it
//     as seconds.ms in a float64: |timestamp| <= 2^53 is exact, and beyond that
//     the seconds are rounded to the nearest representable double (up to +-512 s
//     at the far end of time_t) while datetime_ms is dropped entirely (it
//     rounds back to 0 ms). A TIMESTAMP (tag 1 + integer) has no such limit.
//   - A raw_number STRING -- the exact text of a literal that no int64/double
//     can hold, e.g. 123456789012345678901234567890 from the JSON parser -- is
//     written as an ordinary CBOR text string. CBOR has no bignum that would
//     carry the literal text, so the flag is lost and the value comes back as
//     a plain STRING (quoted again by toJSON()). Same trade-off as the
//     MessagePack out-of-range uint64 mapping, see msgpack.hpp.
//   - RFC 8949 sec 3.1 requires valid UTF-8 in a text string and our own decoder
//     rejects invalid sequences, so toCBOR() throws "CBOR invalid UTF-8 in text
//     string" instead of emitting an unreadable document. That reaches beyond
//     plain strings: makeRegex() and a CUSTOM_TAG name copy bytes verbatim, so
//     a pattern/tag name holding non-UTF-8 bytes is rejected here even though
//     every other serializer in the library emits it as-is. Use BINARY (major
//     type 2, deliberately unchecked) for arbitrary byte payloads.
//
// The depth guard is `depth > MAX_NESTING_DEPTH` with the root item at depth 0,
// and it is checked for EVERY item -- containers and scalars alike. The core JSON
// parser instead increments before checking and checks only containers, so the
// two do not have the same limit in container terms:
//
//   document shape                    JSON    CBOR
//   N containers, innermost empty      48      49
//   N containers, innermost holds x    48      48
//
// CBOR admits a 49th level only when the innermost container is empty: with no
// children, the deepest item parsed sits at depth N-1 and passes `> 48`; add any
// child and that child lands at depth N, which fails. So the one-level
// difference is real but narrow -- it needs a document whose deepest node is an
// empty container, and it makes CBOR the more permissive of the two. Both limits
// are hard caps, and tightening this to match JSON (`>=`, or starting the root at
// depth 1) would reject documents this parser already accepts today, so the
// asymmetry is deliberate and left in place.
//
// A definite-length array/map header is validated against the bytes actually
// left before anything is parsed or reserved: every element costs at least one
// byte (two per map pair), so a count larger than the remaining input cannot
// describe a valid document. The check matters because reserve() ran first --
// 48 nested `array(1000000)` headers (240 bytes of input) each reserved an 8 MB
// vector, 384 MB in total, before the first element was read.
#include "../core.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace asvJSONInternal {

// The encoder below writes float32/float64 (and tag 1) by copying their raw bit
// patterns into the output, and the decoder reads them back the same way, so
// both types must be the IEEE 754 binary32/binary64 layouts the RFC assumes.
static_assert(sizeof(float) == 4 && sizeof(double) == 8,
              "CBOR needs IEEE 754 binary32/binary64 float and double");

inline void cborWriteUint(std::vector<uint8_t>& out, uint8_t mt, uint64_t val) {
  uint8_t major = mt << 5;
  if (val <= 23) {
    out.push_back(major | static_cast<uint8_t>(val));
  } else if (val <= 0xFF) {
    out.push_back(major | 24); out.push_back(static_cast<uint8_t>(val));
  } else if (val <= 0xFFFF) {
    out.push_back(major | 25);
    out.push_back(static_cast<uint8_t>(val >> 8)); out.push_back(static_cast<uint8_t>(val));
  } else if (val <= 0xFFFFFFFFu) {
    out.push_back(major | 26);
    out.push_back(static_cast<uint8_t>(val >> 24)); out.push_back(static_cast<uint8_t>(val >> 16));
    out.push_back(static_cast<uint8_t>(val >> 8)); out.push_back(static_cast<uint8_t>(val));
  } else {
    out.push_back(major | 27);
    out.push_back(static_cast<uint8_t>(val >> 56)); out.push_back(static_cast<uint8_t>(val >> 48));
    out.push_back(static_cast<uint8_t>(val >> 40)); out.push_back(static_cast<uint8_t>(val >> 32));
    out.push_back(static_cast<uint8_t>(val >> 24)); out.push_back(static_cast<uint8_t>(val >> 16));
    out.push_back(static_cast<uint8_t>(val >> 8)); out.push_back(static_cast<uint8_t>(val));
  }
}

// CBOR signed integer: major type 0 for n >= 0, major type 1 for n < 0
// (encoded as -1 - n). The subtraction runs in uint64 so n == INT64_MIN does
// not overflow the negation (which would be UB). Shared by INT, TIMESTAMP and
// the EXTENSION type byte (an int8_t that can legitimately be negative, e.g.
// MessagePack-style fixext types) -- writing that byte as an unsigned major-0
// value made the tag-257 decoder reject it and drop the value back to a plain
// array.
inline void cborWriteSigned(std::vector<uint8_t>& out, int64_t n) {
  if (n >= 0) cborWriteUint(out, 0, static_cast<uint64_t>(n));
  else cborWriteUint(out, 1, static_cast<uint64_t>(-1) - static_cast<uint64_t>(n));
}


// RFC 8949 3.1: a text string MUST be valid UTF-8, and our own decoder rejects
// invalid sequences. Library-held bytes are not guaranteed to be valid UTF-8 --
// makeString/putString copy bytes verbatim, and a CUSTOM_TAG name or a REGEX
// body is not a JSON string at all -- so validate before writing instead of
// silently emitting a document that cannot be read back. Byte strings (major
// type 2) carry arbitrary bytes and are deliberately not checked.
inline void cborWriteText(std::vector<uint8_t>& out, const std::string& s) {
  if (!isValidUTF8(reinterpret_cast<const uint8_t*>(s.data()), s.size()))
    throw asvJSONError("CBOR invalid UTF-8 in text string");
  cborWriteUint(out, 3, s.size());
  out.insert(out.end(), s.begin(), s.end());
}


inline void asvJSONValue::toCBOR(std::vector<uint8_t>& out) const {
  using T = asvJSONValue::Type;
  switch (type) {
    case T::NULL_VAL:
      out.push_back(0xF6); // RFC 8949: 0xF6 = null (0xF7 is undefined)
      break;
    case T::BOOL_VAL:
      out.push_back(flag ? 0xF5 : 0xF4);
      break;
    case T::INT: {
      cborWriteSigned(out, num);
      break;
    }
    case T::DOUBLE: {
      if (is_float32) {
        out.push_back(0xFA);
        float f = static_cast<float>(dbl);
        uint32_t fval;
        memcpy(&fval, &f, sizeof(fval));
        out.push_back(static_cast<uint8_t>(fval >> 24));
        out.push_back(static_cast<uint8_t>(fval >> 16));
        out.push_back(static_cast<uint8_t>(fval >> 8));
        out.push_back(static_cast<uint8_t>(fval));
      } else {
        out.push_back(0xFB);
        uint64_t dval;
        memcpy(&dval, &dbl, sizeof(dval));
        out.push_back(static_cast<uint8_t>(dval >> 56));
        out.push_back(static_cast<uint8_t>(dval >> 48));
        out.push_back(static_cast<uint8_t>(dval >> 40));
        out.push_back(static_cast<uint8_t>(dval >> 32));
        out.push_back(static_cast<uint8_t>(dval >> 24));
        out.push_back(static_cast<uint8_t>(dval >> 16));
        out.push_back(static_cast<uint8_t>(dval >> 8));
        out.push_back(static_cast<uint8_t>(dval));
      }
      break;
    }
    case T::STRING: {
      cborWriteText(out, str_data);
      break;
    }
    case T::BINARY:
    case T::OBJECTID:
    case T::EXTENSION: {
      if (type == T::EXTENSION) {
        // Encode extension as tag 257 + array [extType, byteString]; keeps
        // the type even when the payload is empty (no data-loss to null).
        out.push_back(0xD9); out.push_back(0x01); out.push_back(0x01); // tag 257
        out.push_back(0x82); // array of 2 items
        cborWriteSigned(out, ext_type);
        cborWriteUint(out, 2, bin_data.size());
        out.insert(out.end(), bin_data.begin(), bin_data.end());
      } else if (type == T::OBJECTID && str_data.size() == 12) {
        // Encode ObjectId as tag 4 (MongoDB convention) + byte string
        out.push_back(0xC4); // tag 4
        cborWriteUint(out, 2, str_data.size());
        out.insert(out.end(), str_data.begin(), str_data.end());
      } else if (type == T::BINARY) {
        cborWriteUint(out, 2, bin_data.size());
        out.insert(out.end(), bin_data.begin(), bin_data.end());
      } else {
        // Null fallback -- in practice only reachable if an OBJECTID node somehow carries a non-12-byte payload. makeObjectId() enforces exactly 12 bytes and no other code path assigns type=OBJECTID, so this is defensive against externally-corrupted nodes rather than normal input; emitting null (rather than throwing from a pure serializer) matches how the other special types degrade here.
        out.push_back(0xF6); // null fallback
      }
      break;
    }
    case T::ARRAY: {
      if (!arr) { out.push_back(0x80); break; }
      cborWriteUint(out, 4, arr->size());
      for (const auto& v : *arr) v->toCBOR(out);
      break;
    }
    case T::OBJECT: {
      if (!obj) { out.push_back(0xA0); break; }
      cborWriteUint(out, 5, obj->size());
      for (const auto& [k, v] : *obj) {
        cborWriteText(out, k);
        v->toCBOR(out);
      }
      break;
    }
    case T::DATETIME: {
      // CBOR tag 1 (epoch-based date/time) + float64 (seconds.ms). Note: tz_offset_min is intentionally NOT encoded -- tag 1 represents an absolute UTC instant, so the original local-timezone offset cannot be recovered on a round-trip and will come back as the default. This matches how every other epoch-seconds based format here behaves; it's expected data loss, not a bug.
      out.push_back(0xC1); // tag 1
      double totalSec = static_cast<double>(timestamp) + static_cast<double>(datetime_ms) / 1000.0;
      out.push_back(0xFB);
      uint64_t dval;
      memcpy(&dval, &totalSec, sizeof(dval));
      out.push_back(static_cast<uint8_t>(dval >> 56));
      out.push_back(static_cast<uint8_t>(dval >> 48));
      out.push_back(static_cast<uint8_t>(dval >> 40));
      out.push_back(static_cast<uint8_t>(dval >> 32));
      out.push_back(static_cast<uint8_t>(dval >> 24));
      out.push_back(static_cast<uint8_t>(dval >> 16));
      out.push_back(static_cast<uint8_t>(dval >> 8));
      out.push_back(static_cast<uint8_t>(dval));
      break;
    }
    case T::TIMESTAMP: {
      // Encode as tag 1 + integer (seconds)
      out.push_back(0xC1); // tag 1
      cborWriteSigned(out, num);
      break;
    }
    case T::REGEX: {
      // CBOR tag 35 (regular expression) + text string "pattern\0options" (see makeRegex)
      out.push_back(0xD8); out.push_back(0x23); // tag 35
      cborWriteText(out, str_data);
      break;
    }
    case T::CUSTOM_TAG: {
      // CBOR tag 258 + array [name(string), value] -- mirrors EXTENSION's tag-257 layout, so the annotation survives round-trips instead of falling through to undefined (0xF7 -> null) as before. The name is always present; a JSON-null custom_value emits an explicit null element.
      out.push_back(0xD9); out.push_back(0x01); out.push_back(0x02); // tag 258 (uint16 argument)
      out.push_back(0x82); // array of 2 items
      cborWriteText(out, str_data);
      if (custom_value) custom_value->toCBOR(out); else out.push_back(0xF6); // null value -> explicit CBOR null
      break;
    }
    default:
      out.push_back(0xF7); break;
  }
}

// CBOR Parser

inline uint64_t cborReadArg(const uint8_t* data, size_t& pos, size_t dataLen, uint8_t info) {
  if (info <= 23) return info;
  if (info == 24) {
    if (pos >= dataLen) throw asvJSONError("CBOR: unexpected end (uint8)");
    return data[pos++];
  }
  if (info == 25) {
    if (pos + 2 > dataLen) throw asvJSONError("CBOR: unexpected end (uint16)");
    uint64_t v = (static_cast<uint64_t>(data[pos]) << 8) | data[pos + 1];
    pos += 2;
    return v;
  }
  if (info == 26) {
    if (pos + 4 > dataLen) throw asvJSONError("CBOR: unexpected end (uint32)");
    uint64_t v = (static_cast<uint64_t>(data[pos]) << 24) | (static_cast<uint64_t>(data[pos + 1]) << 16) |
                 (static_cast<uint64_t>(data[pos + 2]) << 8) | data[pos + 3];
    pos += 4;
    return v;
  }
  if (info == 27) {
    if (pos + 8 > dataLen) throw asvJSONError("CBOR: unexpected end (uint64)");
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | data[pos + i];
    pos += 8;
    return v;
  }
  throw asvJSONError("CBOR: reserved additional info");
}

// Contract: this either returns a non-null value or throws -- it never hands
// back a nullptr. The `!chunk` / `!val` / `!v` guards below are therefore dead
// by construction; they are kept as defense in depth against a future early
// return, so do not read a failure to trip them as a reachable state.
inline std::unique_ptr<asvJSONValue> parseCBOR(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) throw asvJSONError("CBOR nesting too deep");
  if (pos >= dataLen) throw asvJSONError("CBOR unexpected end");

  uint8_t byte = data[pos++];
  uint8_t major = byte >> 5;
  uint8_t info = byte & 0x1F;

  // Handle indefinite-length items
  if (info == 31) {
    // 0xFF is the break byte: it only ever terminates an indefinite-length
    // array/map/string (RFC 8949 sec 3.2.2). Anywhere else it is malformed,
    // including at the top level, where "unexpected indefinite item" was
    // actively misleading -- major type 7 has no indefinite form at all.
    if (major == 7) throw asvJSONError("CBOR break byte outside indefinite-length item");
    if (major == 4) { // indefinite-length array
      auto arr = asvJSONValue::makeArray();
      if (!arr) throw asvJSONError("CBOR array alloc failed");
      size_t count = 0;
      bool terminated = false;
      while (pos < dataLen) {
        if (data[pos] == 0xFF) { pos++; terminated = true; break; }
        if (count >= asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("CBOR array too large");
        arr->arr->push_back(parseCBOR(data, pos, dataLen, depth + 1));
        count++;
      }
      if (!terminated) throw asvJSONError("CBOR indefinite array missing terminator");
      return arr;
    }
    if (major == 5) { // indefinite-length map
      auto obj = asvJSONValue::makeObject();
      if (!obj) throw asvJSONError("CBOR map alloc failed");
      size_t count = 0;
      bool terminated = false;
      while (pos < dataLen) {
        if (data[pos] == 0xFF) { pos++; terminated = true; break; }
        if (count >= asvJSONValue::MAX_OBJECT_SIZE) throw asvJSONError("CBOR map too large");
        auto k = parseCBOR(data, pos, dataLen, depth + 1);
        if (!k || k->type != asvJSONValue::STRING) throw asvJSONError("CBOR non-string map key");
        auto v = parseCBOR(data, pos, dataLen, depth + 1);
        if (!v) throw asvJSONError("CBOR map value failed");
        obj->obj->insert_or_assign(k->str_data, std::move(v)); // last wins (duplicate keys), same convention as the JSON parser.
        count++;
      }
      if (!terminated) throw asvJSONError("CBOR indefinite map missing terminator");
      return obj;
    }
    if (major == 2 || major == 3) { // indefinite-length byte/text string
      std::vector<uint8_t> allData;
      size_t total = 0;
      bool terminated = false;
      while (pos < dataLen) {
        if (data[pos] == 0xFF) { pos++; terminated = true; break; }
        // RFC 8949 sec 3.2.2: chunks of an indefinite-length string must be definite-length.
        if ((data[pos] & 0x1F) == 31) throw asvJSONError("CBOR indefinite string with indefinite chunk");
        auto chunk = parseCBOR(data, pos, dataLen, depth + 1);
        if (!chunk) throw asvJSONError("CBOR indefinite string chunk");
        // RFC 8949 s3.2.2: an indefinite-length byte string (major 2) is made
        // up only of byte-string chunks, a text string (major 3) only of
        // text-string chunks. Require the matching major type.
        const uint8_t* src = nullptr;
        size_t srcLen = 0;
        if (major == 2) {
          if (chunk->type != asvJSONValue::BINARY) throw asvJSONError("CBOR indefinite bytes invalid chunk type");
          src = chunk->bin_data.data();
          srcLen = chunk->bin_data.size();
        } else {
          if (chunk->type != asvJSONValue::STRING) throw asvJSONError("CBOR indefinite text invalid chunk type");
          src = reinterpret_cast<const uint8_t*>(chunk->str_data.data());
          srcLen = chunk->str_data.size();
        }
        if (total + srcLen > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("CBOR indefinite string too large");
        if (srcLen > 0) allData.insert(allData.end(), src, src + srcLen);
        total += srcLen;
      }
      if (!terminated) throw asvJSONError("CBOR indefinite string missing terminator");
      if (major == 2) {
        auto v = asvJSONValue::makeBinary(allData.data(), allData.size());
        if (!v) throw asvJSONError("CBOR binary alloc failed");
        return v;
      } else {
        auto v = asvJSONValue::makeString(reinterpret_cast<const char*>(allData.data()), allData.size());
        if (!v) throw asvJSONError("CBOR string alloc failed");
        return v;
      }
    }
    throw asvJSONError("CBOR unexpected indefinite item");
  }

  switch (major) {
    case 0: { // Unsigned integer
      uint64_t val = cborReadArg(data, pos, dataLen, info);
      if (val > 0x7FFFFFFFFFFFFFFFULL) throw asvJSONError("CBOR unsigned integer out of range");
      return asvJSONValue::makeInt(static_cast<int64_t>(val));
    }
    case 1: { // Negative integer
      uint64_t val = cborReadArg(data, pos, dataLen, info);
      if (val > 0x7FFFFFFFFFFFFFFFULL) throw asvJSONError("CBOR negative integer out of range");
      return asvJSONValue::makeInt(static_cast<int64_t>(-1) - static_cast<int64_t>(val));
    }
    case 2: { // Byte string
      uint64_t len64 = cborReadArg(data, pos, dataLen, info);
      if (len64 > dataLen - pos) throw asvJSONError("CBOR byte string longer than input");
      auto v = asvJSONValue::makeBinary(data + pos, static_cast<size_t>(len64));
      pos += static_cast<size_t>(len64);
      if (!v) throw asvJSONError("CBOR binary alloc failed");
      return v;
    }
    case 3: { // Text string
      uint64_t len64 = cborReadArg(data, pos, dataLen, info);
      if (len64 > dataLen - pos) throw asvJSONError("CBOR text string longer than input");
      // RFC 8949 sec 3.1: text strings MUST be valid UTF-8; reject invalid sequences outright.
       if (!isValidUTF8(data + pos, static_cast<size_t>(len64))) throw asvJSONError("CBOR invalid UTF-8 in text string");
      auto v = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(len64));
      pos += static_cast<size_t>(len64);
      if (!v) throw asvJSONError("CBOR string alloc failed");
      return v;
    }
    case 4: { // Array
      uint64_t n64 = cborReadArg(data, pos, dataLen, info);
      if (n64 > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("CBOR array too large");
      // Every element is at least one byte, so a count above the remaining input
      // cannot be satisfied. Checked before makeArray()/reserve(): without it a
      // 5-byte header claiming 1000000 elements reserved 8 MB before the parse
      // then failed on the first missing element, and 48 nested such headers
      // (240 bytes of input) turned into 384 MB of vector capacity.
      if (n64 > static_cast<uint64_t>(dataLen - pos)) throw asvJSONError("CBOR array longer than input");
      auto arr = asvJSONValue::makeArray();
      if (!arr) throw asvJSONError("CBOR array alloc failed");
      size_t n = static_cast<size_t>(n64);
      arr->arr->reserve(n);
      for (size_t i = 0; i < n; i++) arr->arr->push_back(parseCBOR(data, pos, dataLen, depth + 1));
      return arr;
    }
    case 5: { // Map
      uint64_t n64 = cborReadArg(data, pos, dataLen, info);
      if (n64 > asvJSONValue::MAX_OBJECT_SIZE) throw asvJSONError("CBOR map too large");
      // Same bound as the array case, at two bytes per pair (key and value are
      // each at least one byte). pos <= dataLen is guaranteed by cborReadArg's
      // bounds checks, so the subtraction cannot wrap.
      if (n64 > static_cast<uint64_t>((dataLen - pos) / 2)) throw asvJSONError("CBOR map longer than input");
      auto obj = asvJSONValue::makeObject();
      if (!obj) throw asvJSONError("CBOR map alloc failed");
      size_t n = static_cast<size_t>(n64);
      for (size_t i = 0; i < n; i++) {
        auto k = parseCBOR(data, pos, dataLen, depth + 1);
        if (!k || k->type != asvJSONValue::STRING) throw asvJSONError("CBOR non-string map key");
        auto v = parseCBOR(data, pos, dataLen, depth + 1);
        if (!v) throw asvJSONError("CBOR map value failed");
        obj->obj->insert_or_assign(k->str_data, std::move(v)); // last wins (duplicate keys), same convention as the JSON parser.
      }
      return obj;
    }
    case 6: { // Tag
      uint64_t tag = cborReadArg(data, pos, dataLen, info);
      auto val = parseCBOR(data, pos, dataLen, depth + 1);
      if (!val) throw asvJSONError("CBOR tag value failed");
      if (tag == 1) {
        // Epoch-based date/time: seconds since 1970-01-01T00:00Z, can be
        // negative (pre-epoch). Split the fractional part the same way a
        // date parser would: floor towards -inf, keep ms in [0, 1000).
        if (val->type == asvJSONValue::DOUBLE) {
          double sec = val->dbl;
          // NaN/Infinity cannot be a date/time; reject loudly instead of
          // casting to time_t (which is UB on MSVC and yielded null).
          if (!std::isfinite(sec)) throw asvJSONError("CBOR tag 1: NaN/Infinity is not a valid date");
          double fl = std::floor(sec);
          // Casting an out-of-range double to time_t is UB on some platforms. The upper
          // bound must be a representable value strictly greater than any legal time_t:
          // static_cast<double>(INT64_MAX) rounds up to 2^63 (one past INT64_MAX), so use
          // that exact boundary and reject fl >= it -- otherwise the cast below is UB.
          const double kMaxT = static_cast<double>(static_cast<uint64_t>(std::numeric_limits<time_t>::max()) + 1);
          if (fl < static_cast<double>(std::numeric_limits<time_t>::min()) || fl >= kMaxT) {
            throw asvJSONError("CBOR tag 1: seconds out of time_t range");
          }
          time_t t = static_cast<time_t>(fl);
          int ms = static_cast<int>(std::floor((sec - fl) * 1000.0 + 0.5));
          // Rounding the fraction up to exactly 1000 must carry into the seconds
          // rather than being silently clamped (e.g. ...9999 -> 1000 ms would otherwise lose a second). Check before adding: t is already at max => overflow, not detectable after.
          if (ms >= 1000) {
            if (t > std::numeric_limits<time_t>::max() - 1) throw asvJSONError("CBOR tag 1: seconds out of time_t range");
            t += 1; ms = 0;
          }
          if (ms < 0) ms = 0;
          return asvJSONValue::makeDateTime(t, ms);
        }
        if (val->type == asvJSONValue::INT) {
          return asvJSONValue::makeTimestamp(val->num);
        }
        return val; // pass through unknown
      }
      if (tag == 4) {
        // ObjectId: tag 4 + byte string of exactly 12 bytes
        if (val->type == asvJSONValue::BINARY && val->bin_data.size() == 12) {
          std::string_view sv(reinterpret_cast<const char*>(val->bin_data.data()), 12);
          auto oid = asvJSONValue::makeObjectId(sv);
          if (oid) return oid;
        }
        return val;
      }
      if (tag == 35) {
        // Regular expression: stored as "pattern\0options" (NUL-separated, see makeRegex)
        if (val->type == asvJSONValue::STRING) {
          const std::string& s = val->str_data;
          size_t sep = s.find('\0');
          if (sep != std::string::npos) {
            std::string pattern = s.substr(0, sep);
            std::string opts = s.substr(sep + 1);
            auto re = asvJSONValue::makeRegex(pattern.c_str(), opts.empty() ? nullptr : opts.c_str());
            if (re) return re;
          }
          auto re = asvJSONValue::makeRegex(s.c_str(), nullptr);
          if (re) return re;
        }
        return val;
      }
      if (tag == 257) {
        // asvJSON Extension type: array [extType, byteString]
        if (val->type == asvJSONValue::ARRAY && val->arr && val->arr->size() == 2) {
          auto& extTypeVal = (*val->arr)[0];
          auto& dataVal = (*val->arr)[1];
          if (extTypeVal && dataVal && extTypeVal->type == asvJSONValue::INT && dataVal->type == asvJSONValue::BINARY && extTypeVal->num >= -128 && extTypeVal->num <= 127) {
            auto ext = asvJSONValue::makeExtension(static_cast<int8_t>(extTypeVal->num), dataVal->bin_data.data(), dataVal->bin_data.size());
            if (ext) return ext;
          }
        }
        return val;
      }
      if (tag == 258) {
        // asvJSON Custom Tag type: array [name(string), value] -- the name is a
        // required string, the value may be null. Mirrors EXTENSION's tag-257 layout so custom-tag annotations survive CBOR round-trips instead of being dropped to undefined/null (matching JSON/YAML/sexpr $customTag handling).
        if (val->type == asvJSONValue::ARRAY && val->arr && val->arr->size() == 2) {
          auto& nameVal = (*val->arr)[0];
          // Validate first, so a malformed payload falls through to the untouched array below instead of being partially consumed.
          if (nameVal && nameVal->type == asvJSONValue::STRING) {
            auto custom = std::make_unique<asvJSONValue>(); // may throw bad_alloc; contained by fromCBOR's catch(std::exception).
            custom->type = asvJSONValue::CUSTOM_TAG;
            try { custom->str_data.assign(nameVal->str_data); } catch (...) { return val; }
            auto dataVal = std::move((*val->arr)[1]); // may be a null value; moved unconditionally, same convention as the $customTag parser path in core.hpp.
            if (dataVal) custom->custom_value = std::move(dataVal);
            return custom;
          }
        }
        return val;
      }
      return val; // unknown tags pass through
    }
    case 7: { // Simple values and floats
      if (info == 20) return asvJSONValue::makeBool(false);
      if (info == 21) return asvJSONValue::makeBool(true);
      if (info == 22) return asvJSONValue::makeNull();
      if (info == 23) return asvJSONValue::makeNull(); // undefined -> null
      // Unassigned simple values 0..19 (RFC 8949 s3.3) have no JSON equivalent:
      // map them to null exactly like the info == 24 form below. Without this the
      // direct encoding (0xE0..0xF3) fell through to "unknown major type".
      if (info <= 19) return asvJSONValue::makeNull();
      if (info == 24) {
        if (pos >= dataLen) throw asvJSONError("CBOR: unexpected end (simple)");
        uint8_t sv = data[pos++];
        if (sv == 20) return asvJSONValue::makeBool(false);
        if (sv == 21) return asvJSONValue::makeBool(true);
        if (sv == 22) return asvJSONValue::makeNull();
        if (sv == 23) return asvJSONValue::makeNull();
        // RFC 8949 s3.3: simple values 24-31 are reserved and MUST error.
        if (sv >= 24 && sv <= 31) throw asvJSONError("CBOR: reserved simple value");
        return asvJSONValue::makeNull(); // unassigned simple value
      }
      if (info == 25) {
        // Half-precision float (IEEE 754): decode to double
        if (pos + 2 > dataLen) throw asvJSONError("CBOR: unexpected end (float16)");
        uint16_t half = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        uint16_t sign = (half >> 15) & 1;
        uint16_t exp = (half >> 10) & 0x1F;
        uint16_t mant = half & 0x3FF;
        double d;
        // Note: half-precision is decoded to DOUBLE (asvJSONValue has no float16 type),
        // so round-trip through CBOR will emit a float64 instead of the original float16.
        if (exp == 0) {
          d = (mant / 1024.0) * (1.0 / 16384.0);
        } else if (exp == 31) {
          d = mant ? NAN : INFINITY;
        } else {
          d = (1.0 + mant / 1024.0) * std::ldexp(1.0, static_cast<int>(exp) - 15);
        }
        if (sign) d = -d;
        return asvJSONValue::makeDouble(d); // makeDouble defaults is_float32=false
      }
      if (info == 26) {
        if (pos + 4 > dataLen) throw asvJSONError("CBOR: unexpected end (float32)");
        uint32_t fval = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) |
                        (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
        pos += 4;
        float f; memcpy(&f, &fval, sizeof(f));
        auto v = asvJSONValue::makeDouble(f);
        if (v) v->is_float32 = true;
        return v;
      }
      if (info == 27) {
        if (pos + 8 > dataLen) throw asvJSONError("CBOR: unexpected end (float64)");
        uint64_t dval = 0;
        for (int i = 0; i < 8; i++) dval = (dval << 8) | data[pos + i];
        pos += 8;
        double d; memcpy(&d, &dval, sizeof(d));
        return asvJSONValue::makeDouble(d);
      }
      if (info >= 28 && info <= 31) throw asvJSONError("CBOR: reserved encoding (additional information 28-31 are reserved by RFC 8949 s3.2)");
    }
    default:
      throw asvJSONError("CBOR: unknown major type");
  }
}

} // namespace asvJSONInternal
