#pragma once
// CBOR (RFC 8949) serialization/parsing for asvJSON++

#include "../core.hpp"
#include <limits>

namespace asvJSONInternal {

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
      int64_t n = num;
      if (n >= 0) {
        cborWriteUint(out, 0, static_cast<uint64_t>(n));
      } else {
        // CBOR negative-integer encoding: -1 - n. Compute in uint64 to avoid
        // signed overflow UB when n == INT64_MIN (whose negation is undefined).
        cborWriteUint(out, 1, static_cast<uint64_t>(-1) - static_cast<uint64_t>(n));
      }
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
      cborWriteUint(out, 3, str_data.size());
      out.insert(out.end(), str_data.begin(), str_data.end());
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
        cborWriteUint(out, 0, static_cast<uint8_t>(ext_type));
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
        cborWriteUint(out, 3, k.size());
        out.insert(out.end(), k.begin(), k.end());
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
      if (num >= 0) {
        cborWriteUint(out, 0, static_cast<uint64_t>(num));
      } else {
        // CBOR negative-integer encoding: -1 - num. Compute in uint64 to avoid
        // signed overflow UB when num == INT64_MIN (whose negation is undefined).
        cborWriteUint(out, 1, static_cast<uint64_t>(-1) - static_cast<uint64_t>(num));
      }
      break;
    }
    case T::REGEX: {
      // CBOR tag 35 (regular expression) + text string "pattern\0options" (see makeRegex)
      out.push_back(0xD8); out.push_back(0x23); // tag 35
      cborWriteUint(out, 3, str_data.size());
      out.insert(out.end(), str_data.begin(), str_data.end());
      break;
    }
    case T::CUSTOM_TAG: {
      // CBOR tag 258 + array [name(string), value] -- mirrors EXTENSION's tag-257 layout, so the annotation survives round-trips instead of falling through to undefined (0xF7 -> null) as before. The name is always present; a JSON-null custom_value emits an explicit null element.
      out.push_back(0xD9); out.push_back(0x01); out.push_back(0x02); // tag 258 (uint16 argument)
      out.push_back(0x82); // array of 2 items
      cborWriteUint(out, 3, str_data.size());
      out.insert(out.end(), str_data.begin(), str_data.end());
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

inline std::unique_ptr<asvJSONValue> parseCBOR(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) throw asvJSONError("CBOR nesting too deep");
  if (pos >= dataLen) throw asvJSONError("CBOR unexpected end");

  uint8_t byte = data[pos++];
  uint8_t major = byte >> 5;
  uint8_t info = byte & 0x1F;

  // Handle indefinite-length items
  if (info == 31) {
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
      if (len64 > dataLen - pos) throw asvJSONError("CBOR binary overflow");
      auto v = asvJSONValue::makeBinary(data + pos, static_cast<size_t>(len64));
      pos += static_cast<size_t>(len64);
      if (!v) throw asvJSONError("CBOR binary alloc failed");
      return v;
    }
    case 3: { // Text string
      uint64_t len64 = cborReadArg(data, pos, dataLen, info);
      if (len64 > dataLen - pos) throw asvJSONError("CBOR string overflow");
      auto v = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(len64));
      pos += static_cast<size_t>(len64);
      if (!v) throw asvJSONError("CBOR string alloc failed");
      return v;
    }
    case 4: { // Array
      uint64_t n64 = cborReadArg(data, pos, dataLen, info);
      if (n64 > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("CBOR array too large");
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
          if (extTypeVal && dataVal && extTypeVal->type == asvJSONValue::INT && dataVal->type == asvJSONValue::BINARY) {
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
        if (exp == 0) {
          d = (mant / 1024.0) * (1.0 / 16384.0);
        } else if (exp == 31) {
          d = mant ? NAN : INFINITY;
        } else {
          d = (1.0 + mant / 1024.0) * std::pow(2.0, static_cast<int>(exp) - 15);
        }
        if (sign) d = -d;
        auto v = asvJSONValue::makeDouble(d);
        if (v) v->is_float32 = false;
        return v;
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
