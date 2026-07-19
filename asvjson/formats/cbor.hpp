#pragma once
// CBOR (RFC 8949) serialization/parsing for asvJSON++

#include "../core.hpp"

namespace asvJSONInternal {

static void cborWriteUint(std::vector<uint8_t>& out, uint8_t mt, uint64_t val) {
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
      out.push_back(0xF7);
      break;
    case T::BOOL_VAL:
      out.push_back(flag ? 0xF5 : 0xF4);
      break;
    case T::INT: {
      int64_t n = num;
      if (n >= 0) {
        cborWriteUint(out, 0, static_cast<uint64_t>(n));
      } else {
        cborWriteUint(out, 1, static_cast<uint64_t>(-1 - n));
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
      const auto& data = (type == T::OBJECTID) ? str_data
                        : (type == T::EXTENSION && !bin_data.empty()) ? std::string_view(reinterpret_cast<const char*>(bin_data.data()), bin_data.size())
                        : std::string_view();
      if (type == T::EXTENSION && !bin_data.empty()) {
        // Encode extension as tag 257 + array [extType, byteString]
        out.push_back(0xD9); out.push_back(0x01); out.push_back(0x01); // tag 257
        out.push_back(0x82); // array of 2 items
        cborWriteUint(out, 0, static_cast<uint8_t>(ext_type));
        cborWriteUint(out, 2, bin_data.size());
        out.insert(out.end(), bin_data.begin(), bin_data.end());
      } else if (type == T::OBJECTID && str_data.size() == 12) {
        // Encode ObjectId as byte string (no standard tag, decodes to BINARY)
        cborWriteUint(out, 2, str_data.size());
        out.insert(out.end(), str_data.begin(), str_data.end());
      } else if (type == T::BINARY) {
        cborWriteUint(out, 2, bin_data.size());
        out.insert(out.end(), bin_data.begin(), bin_data.end());
      } else {
        out.push_back(0xF7); // null fallback
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
      // CBOR tag 1 (epoch-based date/time) + float64 (seconds.ms)
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
        cborWriteUint(out, 1, static_cast<uint64_t>(-1 - num));
      }
      break;
    }
    case T::REGEX: {
      // CBOR tag 35 (regular expression) + text string "pattern|options"
      out.push_back(0xD8); out.push_back(0x23); // tag 35
      cborWriteUint(out, 3, str_data.size());
      out.insert(out.end(), str_data.begin(), str_data.end());
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
      while (pos < dataLen) {
        if (data[pos] == 0xFF) { pos++; break; }
        arr->arr->push_back(parseCBOR(data, pos, dataLen, depth + 1));
      }
      return arr;
    }
    if (major == 5) { // indefinite-length map
      auto obj = asvJSONValue::makeObject();
      if (!obj) throw asvJSONError("CBOR map alloc failed");
      while (pos < dataLen) {
        if (data[pos] == 0xFF) { pos++; break; }
        auto k = parseCBOR(data, pos, dataLen, depth + 1);
        if (!k || k->type != asvJSONValue::STRING) throw asvJSONError("CBOR non-string map key");
        auto v = parseCBOR(data, pos, dataLen, depth + 1);
        if (!v) throw asvJSONError("CBOR map value failed");
        obj->obj->emplace(k->str_data, std::move(v));
      }
      return obj;
    }
    if (major == 2 || major == 3) { // indefinite-length byte/text string
      std::vector<uint8_t> allData;
      while (pos < dataLen) {
        if (data[pos] == 0xFF) { pos++; break; }
        auto chunk = parseCBOR(data, pos, dataLen, depth + 1);
        if (!chunk) throw asvJSONError("CBOR indefinite string chunk");
        const auto* src = (chunk->type == asvJSONValue::BINARY) ? chunk->bin_data.data() : nullptr;
        size_t srcLen = (chunk->type == asvJSONValue::BINARY) ? chunk->bin_data.size() : 0;
        if (!src && chunk->type == asvJSONValue::STRING) {
          src = reinterpret_cast<const uint8_t*>(chunk->str_data.data());
          srcLen = chunk->str_data.size();
        }
        if (!src) throw asvJSONError("CBOR indefinite string invalid chunk");
        allData.insert(allData.end(), src, src + srcLen);
      }
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
      return asvJSONValue::makeInt(static_cast<int64_t>(val));
    }
    case 1: { // Negative integer
      uint64_t val = cborReadArg(data, pos, dataLen, info);
      return asvJSONValue::makeInt(static_cast<int64_t>(-1) - static_cast<int64_t>(val));
    }
    case 2: { // Byte string
      uint64_t len64 = cborReadArg(data, pos, dataLen, info);
      if (len64 > SIZE_MAX || pos + static_cast<size_t>(len64) > dataLen) throw asvJSONError("CBOR binary overflow");
      auto v = asvJSONValue::makeBinary(data + pos, static_cast<size_t>(len64));
      pos += static_cast<size_t>(len64);
      if (!v) throw asvJSONError("CBOR binary alloc failed");
      return v;
    }
    case 3: { // Text string
      uint64_t len64 = cborReadArg(data, pos, dataLen, info);
      if (len64 > SIZE_MAX || pos + static_cast<size_t>(len64) > dataLen) throw asvJSONError("CBOR string overflow");
      auto v = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(len64));
      pos += static_cast<size_t>(len64);
      if (!v) throw asvJSONError("CBOR string alloc failed");
      return v;
    }
    case 4: { // Array
      uint64_t n64 = cborReadArg(data, pos, dataLen, info);
      if (n64 > 1000000) throw asvJSONError("CBOR array too large");
      auto arr = asvJSONValue::makeArray();
      if (!arr) throw asvJSONError("CBOR array alloc failed");
      size_t n = static_cast<size_t>(n64);
      arr->arr->reserve(n);
      for (size_t i = 0; i < n; i++) arr->arr->push_back(parseCBOR(data, pos, dataLen, depth + 1));
      return arr;
    }
    case 5: { // Map
      uint64_t n64 = cborReadArg(data, pos, dataLen, info);
      if (n64 > 1000000) throw asvJSONError("CBOR map too large");
      auto obj = asvJSONValue::makeObject();
      if (!obj) throw asvJSONError("CBOR map alloc failed");
      size_t n = static_cast<size_t>(n64);
      for (size_t i = 0; i < n; i++) {
        auto k = parseCBOR(data, pos, dataLen, depth + 1);
        if (!k || k->type != asvJSONValue::STRING) throw asvJSONError("CBOR non-string map key");
        auto v = parseCBOR(data, pos, dataLen, depth + 1);
        if (!v) throw asvJSONError("CBOR map value failed");
        obj->obj->emplace(k->str_data, std::move(v));
      }
      return obj;
    }
    case 6: { // Tag
      uint64_t tag = cborReadArg(data, pos, dataLen, info);
      auto val = parseCBOR(data, pos, dataLen, depth + 1);
      if (!val) throw asvJSONError("CBOR tag value failed");
      if (tag == 1) {
        // Epoch-based date/time
        if (val->type == asvJSONValue::DOUBLE) {
          double sec = val->dbl;
          time_t t = static_cast<time_t>(sec);
          int ms = static_cast<int>((sec - static_cast<double>(t)) * 1000.0 + 0.5);
          if (ms > 999) ms = 999;
          if (ms < 0) ms = 0;
          return asvJSONValue::makeDateTime(t, ms);
        }
        if (val->type == asvJSONValue::INT) {
          return asvJSONValue::makeDateTime(static_cast<time_t>(val->num), 0);
        }
        return val; // pass through unknown
      }
      if (tag == 35) {
        // Regular expression
        if (val->type == asvJSONValue::STRING) {
          const std::string& s = val->str_data;
          size_t sep = s.rfind('|');
          const char* optPtr = nullptr;
          if (sep != std::string::npos && sep + 1 < s.length()) optPtr = s.c_str() + sep + 1;
          auto re = asvJSONValue::makeRegex(sep != std::string::npos ? s.c_str() : s.c_str(), optPtr);
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
        return asvJSONValue::makeNull();
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
          d = (1.0 + mant / 1024.0) * pow(2.0, static_cast<int>(exp) - 15);
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
      if (info >= 28 && info <= 30) throw asvJSONError("CBOR: reserved simple value");
      throw asvJSONError("CBOR: unknown simple value");
    }
    default:
      throw asvJSONError("CBOR: unknown major type");
  }
}

} // namespace asvJSONInternal
