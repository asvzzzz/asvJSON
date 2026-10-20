#pragma once
// Protocol Buffers (wire format + text format) for asvJSON++
// Supports schema-less dynamic encoding (field numbers as keys) and JSON-driven schema.
//
// Known limitations (documented, not errors):
// - NULL values and the special types DATETIME / OBJECTID / REGEX / TIMESTAMP /
//   CUSTOM_TAG are not serialized by the text format (protoTextSerializeVal has
//   no case for them); a toProtobufText()/fromProtobufText() round-trip drops
//   nulls and all five special types. A null array element is skipped without
//   leaving a separator behind, so [1, null, 2] is written as [1, 2] (valid text),
//   never as [1, , 2] -- the same holds for the bracketed array-of-messages form,
//   whose trailing comma is placed between written elements only. A NULL root has
//   no text representation at all: toProtobufText() returns an empty string for it
//   (a bare newline would parse back as an empty document), which
//   fromProtobufText() then rejects.
// - A JSON-driven schema must give every field an integer "id"; a field without
//   one throws asvJSONError instead of being dropped from the schema silently.
// - Protobuf groups (wire types 3/4) are deprecated and not supported; a
//   group-typed field terminates the enclosing message frame.
// - Arrays with mixed element types are not faithfully representable in the
//   schema-less wire format: they are marked with the unknown type code "A",
//   and each element is then decoded purely by its wire type (varint -> int,
//   64/32-bit fixed -> int built from the raw bits, length-delimited -> a
//   sub-message when the payload parses as one, otherwise BINARY). Only the
//   length-delimited case is a real heuristic, so a double element of a mixed
//   array comes back as an integer carrying its IEEE-754 bits rather than as
//   garbage: the type is not recovered, and no double/int guessing is
//   attempted. (A length-delimited element of an UNKNOWN type never becomes a
//   STRING -- an untyped LEN is a sub-message or BINARY, never text.)
// - A nested array (an ARRAY element of an array) has no protobuf counterpart in
//   the schema-less wire format: there is no per-element type tag to tell a
//   length-delimited sub-array from a string or a sub-message. toProtobuf()
//   throws asvJSONError for it rather than dropping the element silently; use a
//   message (OBJECT) with repeated fields, or the text format, which supports
//   nested arrays. Non-object elements of a top-level ARRAY root are skipped
//   (see the array-root wrapper below).
// - Text-format integer literals accept C-style forms (0x.. hex and leading-0
//   octal), including the signed minimum (-0x8000000000000000 /
//   -01000000000000000000000, which is INT64_MIN rather than an overflow
//   literal); unpaired \u surrogate escapes are replaced with U+FFFD.
// - Field numbers are limited to 2^29-1; malformed varints (unterminated or a
//   10th byte carrying bits beyond the low bit) are rejected. A schema-less
//   LEN payload that merely *starts* like a sub-message but then fails to
//   parse falls back to the binary branch instead of aborting the decode.
// - A key that is not a valid protobuf identifier is written in the text format
//   in brackets, with ']' and '\' escaped inside the name, so a name containing
//   a bracket round-trips.
// - A schema-driven string/bytes/message field whose JSON value has a
//   different type throws asvJSONError from toProtobuf() rather than silently
//   encoding an empty field. The scalar fields (int*, uint*, sint*, fixed*,
//   sfixed*, float, double, bool, enum) follow the same rule: only lossless
//   widenings are accepted -- INT for any number, BOOL (0/1) for any number,
//   BOOL for bool, INT/DOUBLE 0 or 1 for bool, and a DOUBLE for an integer
//   field only when it is an exact in-range integer. Any other value (STRING,
//   OBJECT, ARRAY, NULL, BINARY, ...) throws, and so does a DOUBLE that cannot
//   be converted (fractional, NaN, infinite, out of int64 range) or an unknown
//   /missing field type. A negative INT in an unsigned field keeps its
//   two's-complement bits (protobuf has no unsigned JSON mapping, and the
//   decoder reads the bits back as the same value).
// - "enum" is carried as its integer ordinal (int32 on the wire) and decodes
//   back as an INT. The JSON-driven schema has no name table, so a symbolic
//   enum name ("RED") is not accepted: a STRING value for an enum field throws.
// - A schema field marked "repeated" whose JSON value is not an ARRAY is
//   encoded as a single element and decodes as a one-element array, which
//   round-trips in both directions.
// - A top-level JSON array is wrapped into a synthetic message (marker field
//   19001 + repeated field 1) so the elements survive a schema-less round-trip
//   intact; non-object array elements cannot be represented in the wrapper and
//   are skipped. Fields in the reserved range 19000-19999 (the schema-less name
//   map and the array-root marker) must not be used for user data.
// - BINARY/EXTENSION values are serialized as quoted strings by the text
//   format, so a toProtobufText()/fromProtobufText() round-trip turns them into
//   STRING (the raw type is lost). The schema-less WIRE format keeps them apart:
//   an EXTENSION records its type byte in the name map, so it decodes back as
//   an EXTENSION with the same ext_type, while a BINARY decodes as BINARY. The
//   name map holds one type byte per field, so an ARRAY of extensions qualifies
//   only when every element is an EXTENSION of the same type; a mixed one
//   decodes as BINARY.
// - Surface JSON values composed only of the sign of a number ("-", "+") are
//   rejected on parse instead of being coerced to 0.
// - toProtobuf() only encodes OBJECT and ARRAY roots. A scalar root (STRING,
//   INT, DOUBLE, BOOL, ...) silently produces an empty buffer, and fromProtobuf
//   rejects empty input, so a scalar-root round-trip yields false.
// - The empty object root "{}" is not round-trippable with the wire format: it
//   encodes to zero bytes, and fromProtobuf rejects length 0 input as invalid.
//   Empty ARRAY roots round-trip via the 19001 wrapper marker; empty OBJECT
//   roots have no wire representation (an empty protobuf message IS zero bytes,
//   so this is inherent, not a bug). "{}" nested as a field is preserved (it is
//   encoded with a schema-less "m" type code / decoded from a zero-length LEN).
// - The text format accepts a top-level message, a top-level array (the
//   "[...]" protoTextSerializeVal writes for an ARRAY root, including "[]") and
//   a top-level scalar; all three round-trip. The empty object root "{}" is
//   still rejected by the parser, as it is by fromProtobuf.
// - Text-format float literals accept a trailing f/F suffix ("1.5f").

#include "../core.hpp"
#include "../detail/base64.hpp"
#include <cstring>
#include <cmath>
#include <cinttypes>
#include <functional>

namespace asvJSONInternal {

// ======================= Varint helpers =======================

// Field number used to embed the JSON name map in schema-less encoding.
// Uses the protobuf-reserved range 19000-19999 to avoid colliding with
// legitimate user field numbers (536870911 was previously used).
inline constexpr int protoNameMapFieldNum = 19000;

// Reserved field number of the synthetic array-root wrapper. An ARRAY root is
// encoded as a message containing this marker field (wire type 0, value 1)
// followed by repeated field 1 submessages (one per element); decoding unwraps
// them back into an ARRAY. Stream-concatenating the raw messages would merge
// the elements' fields (last-wins for scalars, colliding schema-less name maps)
// and lose data.
inline constexpr int protoArrayRootFieldNum = 19001;

// Largest legal protobuf field number (2^29-1 per the spec).
inline constexpr uint64_t protoMaxFieldNum = (1ULL << 29) - 1;

// Sentinel for "this field carries no extension type byte". Chosen outside the
// int8_t range of asvJSONValue::ext_type so that EXTENSION(0) -- a perfectly
// valid node -- is still distinguishable from a BINARY of the same bytes.
inline constexpr int protoNoExtType = 256;

inline void protoWriteVarint(std::vector<uint8_t>& out, uint64_t val) {
  while (val >= 0x80) {
    out.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
    val >>= 7;
  }
  out.push_back(static_cast<uint8_t>(val));
}

inline uint64_t protoReadVarint(const uint8_t* data, size_t size, size_t& pos) {
  uint64_t val = 0;
  int shift = 0;
  while (pos < size) {
    uint8_t b = data[pos++];
    // The 10th byte (shift == 63) can only contribute its low bit; any bits
    // above it would overflow the 64-bit value and must be rejected (protobuf
    // accepts at most 10 bytes per varint).
    if (shift == 63 && (b & 0x7E)) throw asvJSONError("malformed varint: 10th byte overflow");
    val |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) return val;
    shift += 7;
    if (shift > 63) throw asvJSONError("malformed varint: too many bytes");
  }
  throw asvJSONError("malformed varint: unexpected end");
}

inline int64_t protoZigZagDecode(uint64_t val) {
  return (val >> 1) ^ -(static_cast<int64_t>(val) & 1);
}

inline uint64_t protoZigZagEncode(int64_t val) {
  return (static_cast<uint64_t>(val) << 1) ^ static_cast<uint64_t>(val >> 63);
}

inline void protoWriteTag(std::vector<uint8_t>& out, int fieldNum, int wireType) {
  protoWriteVarint(out, (static_cast<uint64_t>(fieldNum) << 3) | static_cast<uint64_t>(wireType));
}

inline int protoReadTag(const uint8_t* data, size_t size, size_t& pos, int& fieldNum) {
  if (pos >= size) return -1;
  uint64_t tag = protoReadVarint(data, size, pos);
  uint64_t rawNum = tag >> 3;
  // A field number above the protobuf maximum (2^29-1) is invalid; rejecting
  // it prevents forged truncation to a small legitimate-looking field number.
  if (rawNum > protoMaxFieldNum || rawNum == 0) {
    fieldNum = -1;
    return -2;
  }
  fieldNum = static_cast<int>(rawNum);
  return static_cast<int>(tag & 7);
}

// Skip the value of a field whose contents we do not want to decode (reserved
// fields like the schema-less name map / array-root marker). Advances pos past
// the value bytes for any of the fixed/varint/length-delimited wire types;
// returns false for groups (3/4) or when the payload is truncated, in which
// case pos is left unchanged.
inline bool protoSkipWireValue(const uint8_t* data, size_t size, size_t& pos, int wt) {
  switch (wt) {
    case 0: protoReadVarint(data, size, pos); return true;
    case 1: if (pos + 8 > size) return false; pos += 8; return true;
    case 5: if (pos + 4 > size) return false; pos += 4; return true;
    case 2: {
      uint64_t len = protoReadVarint(data, size, pos);
      if (len > static_cast<uint64_t>(SIZE_MAX) || static_cast<size_t>(len) > size - pos) return false;
      pos += static_cast<size_t>(len);
      return true;
    }
    default: return false; // groups (3/4) not supported
  }
}

// ======================= Schema types =======================

struct ProtoField {
  int number = 0;
  std::string type;
  bool repeated = false;
  bool packed = true;
  std::shared_ptr<struct ProtoSchema> schema;
};

struct ProtoSchema {
  std::unordered_map<std::string, ProtoField> fields;
  std::unordered_map<int, std::string> numberToName;
};

inline ProtoField protoParseOneField(const asvJSONValue* val) {
  ProtoField f;
  if (!val || val->type != asvJSONValue::OBJECT)
    throw asvJSONError("Protobuf: schema field must be an object");
  auto* n = val->get("id");
  if (!n) throw asvJSONError("Protobuf: schema field is missing the required integer \"id\"");
  if (n->type != asvJSONValue::INT)
    throw asvJSONError("Protobuf: field id must be an integer");
  int64_t id = n->num;
  if (id < 1 || id > static_cast<int64_t>(protoMaxFieldNum))
    throw asvJSONError("Protobuf: field id out of range [1, " + std::to_string(protoMaxFieldNum) + "]");
  f.number = static_cast<int>(id);
  auto* t = val->get("type");
  if (t && t->type == asvJSONValue::STRING) f.type.assign(t->str_data.data(), t->str_data.size());
  auto* r = val->get("repeated");
  if (r && r->type == asvJSONValue::BOOL_VAL) f.repeated = r->flag;
  auto* p = val->get("packed");
  if (p && p->type == asvJSONValue::BOOL_VAL) f.packed = p->flag;
  if (f.type == "message") {
    auto* sub = val->get("fields");
    if (sub && sub->type == asvJSONValue::OBJECT) {
      f.schema = std::make_shared<ProtoSchema>();
      f.schema->fields.reserve(sub->obj->size());
      f.schema->numberToName.reserve(sub->obj->size());
      for (const auto& [k, v] : *sub->obj) {
        if (!v) continue;
        auto pf = protoParseOneField(v.get());
        f.schema->fields[k] = pf;
        f.schema->numberToName[pf.number] = k;
      }
    }
  }
  // "enum" is kept as its own type name (it encodes as a varint like int32) so
  // that a type error on an enum field names "enum" instead of the int32 it is
  // aliased to; the schema carries no name table, so a symbolic enum name is
  // never accepted and the value always decodes back as an INT.
  return f;
}

inline std::shared_ptr<ProtoSchema> protoParseSchema(const asvJSONValue* schemaVal) {
  auto s = std::make_shared<ProtoSchema>();
  if (!schemaVal || schemaVal->type != asvJSONValue::OBJECT || !schemaVal->obj) return s;
  s->fields.reserve(schemaVal->obj->size());
  s->numberToName.reserve(schemaVal->obj->size());
  for (const auto& [name, val] : *schemaVal->obj) {
    if (!val) continue;
    auto pf = protoParseOneField(val.get());
    s->fields[name] = pf;
    s->numberToName[pf.number] = name;
  }
  return s;
}

// Helper: set a field on a JSON value object by key
inline void protoSetField(asvJSONValue* obj, const std::string& key, std::unique_ptr<asvJSONValue> val) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) return;
  if (!val) return; // a null node would be dereferenced by every later tree walk
  auto it = obj->obj->find(key);
  if (it != obj->obj->end()) {
    // Repeated field -- convert to array if not already
    if (it->second->type == asvJSONValue::ARRAY && it->second->arr) {
      it->second->arr->push_back(std::move(val));
    } else {
      auto arr = asvJSONValue::makeArray();
      if (arr && arr->arr) {
        arr->arr->push_back(std::move(it->second));
        arr->arr->push_back(std::move(val));
        it->second = std::move(arr);
      }
    }
  } else {
    (*obj->obj)[key] = std::move(val);
  }
}

// ======================= Wire format encoder =======================

// Compact type code for a value, used inside the schema-less name map so the
// decoder can recover the JSON type that the raw wire format cannot express.
// Lowercase = scalar, Uppercase = repeated (array) of that element type.
inline std::string protoSchemaLessTypeCode(const asvJSONValue* v) {
  if (!v) return "u";
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::STRING: return "s";
    case T::INT: return "i";
    case T::DOUBLE: return "d";
    case T::BOOL_VAL: return "b";
    case T::NULL_VAL: return "n";
    case T::BINARY: case T::EXTENSION: return "y";
    case T::OBJECT: return "m";
    case T::ARRAY: {
      if (!v->arr) return "A";
      // All elements must share one type code; a mixed-typed array is not
      // representable in the wire format, so emit the unknown code ("A") and
      // let the decoder fall back to per-element heuristics instead of
      // silently mis-decoding every element as the first one's type.
      std::string c;
      for (const auto& e : *v->arr) {
        if (!e) continue;
        std::string ec = protoSchemaLessTypeCode(e.get());
        if (c.empty()) c = ec;
        else if (ec != c) return "A";
      }
      if (c.empty()) return "A";
      if (c == "s") return "S"; if (c == "i") return "I"; if (c == "d") return "D";
      if (c == "b") return "B"; if (c == "n") return "N"; if (c == "y") return "Y";
      if (c == "m") return "M";
      return "A";
    }
    default: return "u";
  }
}

// The EXTENSION type byte of a field for the name map, or protoNoExtType when
// the field carries none. An array qualifies only if every element is an
// EXTENSION with the same type byte: the map holds one "x" per field, and a
// mixed array decodes to the same type for all of them.
inline int protoFieldExtType(const asvJSONValue* v) {
  if (!v) return protoNoExtType;
  if (v->type == asvJSONValue::EXTENSION) return static_cast<int>(v->ext_type);
  if (v->type != asvJSONValue::ARRAY || !v->arr) return protoNoExtType;
  int t = protoNoExtType;
  for (const auto& e : *v->arr) {
    if (!e) continue;
    if (e->type != asvJSONValue::EXTENSION) return protoNoExtType;
    int et = static_cast<int>(e->ext_type);
    if (t == protoNoExtType) t = et;
    else if (t != et) return protoNoExtType;
  }
  return t;
}

// Map a name-map type code back to a protobuf wire type string and repeated flag.
inline std::string protoLessCodeToType(const std::string& code, bool& repeated) {
  repeated = false;
  if (code.empty() || code == "u" || code == "A" || code == "n") return "";
  if (code == "N") { repeated = true; return ""; }
  char c = code[0];
  if (c >= 'A' && c <= 'Z') { repeated = true; c = static_cast<char>(c + 32); }
  switch (c) {
    case 's': return "string";
    case 'i': return "int64";
    case 'd': return "double";
    case 'b': return "bool";
    case 'y': return "bytes";
    case 'm': return "message";
    default: return "";
  }
}

// Scalar conversion for a schema-driven field. The declared type picks the wire
// encoding, so a JSON value of a different type must be converted on purpose:
// the encoder used to read val->dbl for every non-INT value, which silently
// wrote 0 (or the bits of an unrelated union member) for a STRING, OBJECT,
// NULL, BINARY or DATETIME node. Only lossless widenings are accepted now, and
// anything else throws like the string/bytes/message fields already do.
inline double protoScalarToDouble(const asvJSONValue* val, const std::string& type) {
  switch (val->type) {
    case asvJSONValue::DOUBLE: return val->dbl;
    case asvJSONValue::INT: return static_cast<double>(val->num);
    case asvJSONValue::BOOL_VAL: return val->flag ? 1.0 : 0.0;
    default: throw asvJSONError("Protobuf encode: " + type + " field does not hold a number");
  }
}

inline int64_t protoScalarToInt(const asvJSONValue* val, const std::string& type) {
  switch (val->type) {
    case asvJSONValue::INT: return val->num;
    case asvJSONValue::BOOL_VAL: return val->flag ? 1 : 0;
    case asvJSONValue::DOUBLE: {
      double d = val->dbl;
      // A fractional, non-finite or out-of-int64-range double has no integer
      // reading: the old static_cast silently produced 1 for 1.5 and was
      // undefined for NaN/inf/huge values.
      if (!std::isfinite(d) || d != std::trunc(d) ||
          d < -9223372036854775808.0 || d >= 9223372036854775808.0)
        throw asvJSONError("Protobuf encode: " + type + " field requires an integer value");
      return static_cast<int64_t>(d);
    }
    default: throw asvJSONError("Protobuf encode: " + type + " field does not hold a number");
  }
}

inline void protoEncodeScalar(std::vector<uint8_t>& out, const asvJSONValue* val, const std::string& type) {
  if (!val) return;
  if (type == "int32" || type == "int64" || type == "enum" ||
      type == "uint32" || type == "uint64" || type == "sint32" || type == "sint64") {
    int64_t n = protoScalarToInt(val, type);
    if (type == "sint32" || type == "sint64") protoWriteVarint(out, protoZigZagEncode(n));
    else protoWriteVarint(out, static_cast<uint64_t>(n));
  } else if (type == "fixed64" || type == "sfixed64") {
    uint64_t bits = static_cast<uint64_t>(protoScalarToInt(val, type));
    for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "double") {
    double d = protoScalarToDouble(val, type);
    uint64_t bits; std::memcpy(&bits, &d, sizeof(bits));
    for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "fixed32" || type == "sfixed32") {
    // The int32 round-trip keeps the two's-complement bits of a value that does
    // not fit, which is exactly what the decoder reads back for sfixed32.
    uint32_t bits = static_cast<uint32_t>(static_cast<int32_t>(protoScalarToInt(val, type)));
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "float") {
    float f = static_cast<float>(protoScalarToDouble(val, type));
    uint32_t bits; std::memcpy(&bits, &f, sizeof(bits));
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "bool") {
    bool b;
    if (val->type == asvJSONValue::BOOL_VAL) b = val->flag;
    else {
      double d = protoScalarToDouble(val, type);
      if (d != 0.0 && d != 1.0)
        throw asvJSONError("Protobuf encode: bool field requires true, false, 0 or 1");
      b = (d != 0.0);
    }
    out.push_back(b ? 1 : 0);
  } else {
    // An unknown or missing field type is a schema error: guessing a varint
    // wrote a value the caller never asked for (and 0 for a non-INT node).
    throw asvJSONError("Protobuf encode: unsupported field type \"" + type + "\"");
  }
}

inline void protoEncodeFieldValue(std::vector<uint8_t>& out, const asvJSONValue* val, const ProtoField& field);

inline void protoEncodeMessage(std::vector<uint8_t>& out, const asvJSONValue* val, const ProtoSchema* schema) {
  if (!val || val->type != asvJSONValue::OBJECT || !val->obj) return;
  if (schema) {
    for (const auto& [key, fieldVal] : *val->obj) {
      if (!fieldVal) continue;
      auto it = schema->fields.find(key);
      if (it == schema->fields.end()) continue;
      const auto& field = it->second;
      if (field.repeated && fieldVal->type == asvJSONValue::ARRAY && fieldVal->arr) {
        bool isPackable = (field.type != "string" && field.type != "bytes" && field.type != "message");
        if (field.packed && isPackable && !fieldVal->arr->empty()) {
          std::vector<uint8_t> packed;
          for (const auto& elem : *fieldVal->arr) {
            if (elem) protoEncodeScalar(packed, elem.get(), field.type);
          }
          if (!packed.empty()) {
            protoWriteTag(out, field.number, 2);
            protoWriteVarint(out, packed.size());
            out.insert(out.end(), packed.begin(), packed.end());
          }
        } else {
          for (const auto& elem : *fieldVal->arr) {
            if (!elem) continue;
            int wt = (field.type == "double" || field.type == "fixed64" || field.type == "sfixed64") ? 1 :
                     (field.type == "float" || field.type == "fixed32" || field.type == "sfixed32") ? 5 :
                     (field.type == "string" || field.type == "bytes" || field.type == "message") ? 2 : 0;
            protoWriteTag(out, field.number, wt);
            if (wt == 2) {
              ProtoField tmpF = field;
              std::vector<uint8_t> fv;
              protoEncodeFieldValue(fv, elem.get(), tmpF);
              out.insert(out.end(), fv.begin(), fv.end());
            } else {
              protoEncodeScalar(out, elem.get(), field.type);
            }
          }
        }
      } else {
        int wt = (field.type == "double" || field.type == "fixed64" || field.type == "sfixed64") ? 1 :
                 (field.type == "float" || field.type == "fixed32" || field.type == "sfixed32") ? 5 :
                 (field.type == "string" || field.type == "bytes" || field.type == "message") ? 2 : 0;
        protoWriteTag(out, field.number, wt);
        if (wt == 2) {
          ProtoField tmpF = field;
          std::vector<uint8_t> fv;
          protoEncodeFieldValue(fv, fieldVal.get(), tmpF);
          out.insert(out.end(), fv.begin(), fv.end());
        } else {
          protoEncodeScalar(out, fieldVal.get(), field.type);
        }
      }
    }
  } else {
    // Collect all fields first for sequential numbering + name map
    struct ProtoFieldEntry { std::string key; const asvJSONValue* val; };
    std::vector<ProtoFieldEntry> fields;
    fields.reserve(val->obj->size());
    for (const auto& [k, v] : *val->obj) {
      if (v) fields.push_back({k, v.get()});
    }
    // Build JSON name map at the reserved field number (19000-19999).
    // Each entry carries the field name and a compact type code so the decoder
    // can reconstruct string/message/double/array types that are ambiguous in
    // the raw wire format (a length-delimited field could be a string or a
    // sub-message; a fixed-width field could be a double or an int).
    if (!fields.empty()) {
      std::string nm = "{";
      for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) nm += ",";
        nm += "\"" + std::to_string(i + 1) + "\":{\"n\":\"";
        for (char c : fields[i].key) {
          if (c == '"') nm += "\\\"";
          else if (c == '\\') nm += "\\\\";
          else if (c == '\n') nm += "\\n";
          else if (c == '\r') nm += "\\r";
          else if (c == '\t') nm += "\\t";
          else if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c); nm += buf; }
          else nm += c;
        }
        nm += "\",\"t\":\"";
        nm += protoSchemaLessTypeCode(fields[i].val);
        nm += "\"";
        // An EXTENSION stores its type byte in the name map ("x"), so the
        // decoder rebuilds an EXTENSION instead of a bare BINARY of the same
        // bytes. A BINARY gets no "x" and decodes as BINARY, as before. The
        // same applies to an array of EXTENSIONs, which the element writer
        // emits as repeated length-delimited fields under this one name.
        // The "x" value is a number, so it goes after the type code is closed.
        int fieldExt = protoFieldExtType(fields[i].val);
        if (fieldExt != protoNoExtType) {
          nm += ",\"x\":";
          nm += std::to_string(fieldExt);
        }
        nm += "}";
      }
      nm += '}';
      protoWriteTag(out, protoNameMapFieldNum, 2);
      protoWriteVarint(out, nm.size());
      out.insert(out.end(), nm.begin(), nm.end());
    }
    for (size_t fi = 0; fi < fields.size(); fi++) {
      const auto& [key, fieldVal] = fields[fi];
      int fnum = static_cast<int>(fi + 1);
      switch (fieldVal->type) {
      case asvJSONValue::INT:
        protoWriteTag(out, fnum, 0);
        protoWriteVarint(out, static_cast<uint64_t>(fieldVal->num));
        break;
      case asvJSONValue::BOOL_VAL:
        protoWriteTag(out, fnum, 0);
        out.push_back(fieldVal->flag ? 1 : 0);
        break;
      case asvJSONValue::DOUBLE:
        protoWriteTag(out, fnum, 1);
        { uint64_t bits; std::memcpy(&bits, &fieldVal->dbl, sizeof(bits));
          for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8))); }
        break;
      case asvJSONValue::STRING:
        protoWriteTag(out, fnum, 2);
        { const auto& s = fieldVal->str_data;
          protoWriteVarint(out, s.size()); out.insert(out.end(), s.begin(), s.end()); }
        break;
      case asvJSONValue::BINARY:
      case asvJSONValue::EXTENSION:
        protoWriteTag(out, fnum, 2);
        { auto bin = fieldVal->getBinary();
          protoWriteVarint(out, bin.size()); out.insert(out.end(), bin.begin(), bin.end()); }
        break;
      case asvJSONValue::OBJECT:
        protoWriteTag(out, fnum, 2);
        { std::vector<uint8_t> sub;
          protoEncodeMessage(sub, fieldVal, nullptr);
          protoWriteVarint(out, sub.size()); out.insert(out.end(), sub.begin(), sub.end()); }
        break;
      case asvJSONValue::ARRAY:
        if (fieldVal->arr) {
          for (const auto& elem : *fieldVal->arr) {
            if (!elem) continue;
            switch (elem->type) {
              case asvJSONValue::INT:
                protoWriteTag(out, fnum, 0); protoWriteVarint(out, static_cast<uint64_t>(elem->num)); break;
              case asvJSONValue::BOOL_VAL:
                protoWriteTag(out, fnum, 0); out.push_back(elem->flag ? 1 : 0); break;
              case asvJSONValue::DOUBLE:
                protoWriteTag(out, fnum, 1);
                { uint64_t bits; std::memcpy(&bits, &elem->dbl, sizeof(bits));
                  for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8))); }
                break;
              case asvJSONValue::STRING:
                protoWriteTag(out, fnum, 2);
                { const auto& s = elem->str_data;
                  protoWriteVarint(out, s.size()); out.insert(out.end(), s.begin(), s.end()); }
                break;
              case asvJSONValue::OBJECT:
                protoWriteTag(out, fnum, 2);
                { std::vector<uint8_t> sub; protoEncodeMessage(sub, elem.get(), nullptr);
                  protoWriteVarint(out, sub.size()); out.insert(out.end(), sub.begin(), sub.end()); }
                break;
              case asvJSONValue::BINARY:
              case asvJSONValue::EXTENSION:
                protoWriteTag(out, fnum, 2);
                { auto bin = elem->getBinary();
                  protoWriteVarint(out, bin.size()); out.insert(out.end(), bin.begin(), bin.end()); }
                break;
              case asvJSONValue::ARRAY:
                // A nested array has no wire counterpart: the name map records
                // one type code per field, not per element, so a
                // length-delimited sub-array could not be told from a string or
                // a sub-message on the way back. The old default: break dropped
                // the element without a word, turning {"a":[[1,2]]} into
                // {"a":[]}; throw so the caller sees the data it cannot encode.
                throw asvJSONError("Protobuf encode: nested arrays are not representable in the schema-less wire format");
              default: break;
            }
          }
        }
        break;
      default: break;
      }
    }
  }
}

inline void protoEncodeFieldValue(std::vector<uint8_t>& out, const asvJSONValue* val, const ProtoField& field) {
  if (!val) return;
  if (field.type == "string") {
    if (val->type != asvJSONValue::STRING)
      throw asvJSONError("Protobuf encode: string field does not hold a string value");
    const auto& s = val->str_data;
    protoWriteVarint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
  } else if (field.type == "bytes") {
    std::vector<uint8_t> bin;
    if (val->type == asvJSONValue::BINARY || val->type == asvJSONValue::EXTENSION) bin = val->getBinary();
    else if (val->type == asvJSONValue::STRING) bin.assign(val->str_data.begin(), val->str_data.end());
    else throw asvJSONError("Protobuf encode: bytes field does not hold a binary value");
    protoWriteVarint(out, bin.size());
    out.insert(out.end(), bin.begin(), bin.end());
  } else if (field.type == "message") {
    if (val->type != asvJSONValue::OBJECT)
      throw asvJSONError("Protobuf encode: message field does not hold an object");
    // An empty message has no nested fields, so the wire emits zero bytes and
    // the decoder sees nothing -- indistinguishable from a missing field. Reject
    // it explicitly so the caller knows the value is unrepresentable rather than
    // silently encoding a no-op length-delimited blob.
    if (!val->obj || val->obj->empty())
      throw asvJSONError("Protobuf encode: message field holds an empty object");
    std::vector<uint8_t> sub;
    protoEncodeMessage(sub, val, field.schema.get());
    protoWriteVarint(out, sub.size());
    out.insert(out.end(), sub.begin(), sub.end());
  } else {
    protoEncodeScalar(out, val, field.type);
  }
}

inline std::vector<uint8_t> protoEncodeRoot(const asvJSONValue* root, const ProtoSchema* schema) {
  std::vector<uint8_t> out;
  if (!root) return out;
  if (root->type == asvJSONValue::OBJECT) {
    protoEncodeMessage(out, root, schema);
  } else if (root->type == asvJSONValue::ARRAY && root->arr) {
    // A top-level array of objects can not be represented by a stream of
    // concatenated messages (their fields would merge and the schema-less name
    // maps would collide). Wrap the elements in a synthetic message: marker
    // field 19001 (wire type 0, value 1) followed by repeated field 1, each a
    // sub-message carrying one element. An empty array still emits the marker
    // so the decoder recovers an empty ARRAY instead of nothing.
    protoWriteTag(out, protoArrayRootFieldNum, 0);
    protoWriteVarint(out, 1);
    for (const auto& elem : *root->arr) {
      if (!elem || elem->type != asvJSONValue::OBJECT) continue;
      protoWriteTag(out, 1, 2);
      std::vector<uint8_t> sub;
      protoEncodeMessage(sub, elem.get(), schema);
      protoWriteVarint(out, sub.size());
      out.insert(out.end(), sub.begin(), sub.end());
    }
  }
  return out;
}

// ======================= Wire format decoder =======================

inline std::unique_ptr<asvJSONValue> protoDecodeWireValue(const uint8_t* data, size_t size, size_t& pos,
                                                           int wireType, const std::string& type,
                                                           const ProtoSchema* schema, int depth = 0,
                                                           int extType = protoNoExtType);

// Name/type map embedded at the reserved schema-less field number.
struct ProtoLessMap {
  std::unordered_map<int, std::string> names; // fnum -> JSON key
  std::unordered_map<int, std::string> types; // fnum -> type code
  std::unordered_map<int, int> extTypes;      // fnum -> EXTENSION type byte
  bool present = false;
};

// Extract the embedded name/type map (field 19000) from a raw schema-less
// message region [begin, end). Tolerates the old single-string value form
// {"1":"name"} whose entries carry no type code (types stay empty -> legacy
// heuristic), and the new {"1":{"n":"name","t":"code"}} form.
inline ProtoLessMap protoExtractLessMap(const uint8_t* data, size_t begin, size_t end) {
  ProtoLessMap m;
  size_t pos = begin;
  size_t foundStart = end, foundLen = 0;
  bool found = false;
  try {
    while (pos < end) {
      int fnum = 0; int wt = protoReadTag(data, end, pos, fnum);
      if (wt < 0 || wt > 5 || fnum <= 0) break;
      if (fnum == protoNameMapFieldNum && wt == 2) {
        uint64_t len = protoReadVarint(data, end, pos);
        if (len > end - pos) break;
        foundStart = pos; foundLen = static_cast<size_t>(len); found = true; break;
      }
      if (wt == 0) { protoReadVarint(data, end, pos); }
      else if (wt == 1) { if (pos + 8 <= end) pos += 8; else break; }
      else if (wt == 5) { if (pos + 4 <= end) pos += 4; else break; }
      else if (wt == 2) { uint64_t len = protoReadVarint(data, end, pos); if (len <= end - pos) pos += static_cast<size_t>(len); else break; }
      else break;
    }
  } catch (...) {}
  if (!found || foundLen == 0) return m;
  std::string js(reinterpret_cast<const char*>(data + foundStart), foundLen);
  asvJSON nm;
  if (!nm.parse(js)) return m;
  auto* mapRoot = nm.getRoot();
  if (!mapRoot || mapRoot->type != asvJSONValue::OBJECT || !mapRoot->obj) return m;
  for (const auto& [fs, nameVal] : *mapRoot->obj) {
    int fnum = 0; try { fnum = std::stoi(fs); } catch (...) { continue; }
    if (fnum <= 0) continue;
    if (!nameVal) continue;
    if (nameVal->type == asvJSONValue::STRING) {
      m.names[fnum] = nameVal->str_data;
      m.types[fnum] = "";
    } else if (nameVal->type == asvJSONValue::OBJECT && nameVal->obj) {
      auto* nv = nameVal->get("n");
      auto* tv = nameVal->get("t");
      auto* xv = nameVal->get("x");
      if (nv && nv->type == asvJSONValue::STRING) m.names[fnum] = nv->str_data;
      if (tv && tv->type == asvJSONValue::STRING) m.types[fnum] = tv->str_data;
      if (xv && xv->type == asvJSONValue::INT) m.extTypes[fnum] = static_cast<int>(xv->num);
    }
  }
  m.present = true;
  return m;
}

// Decode a protobuf message body (sequence of fields) in region [begin, end).
// Handles the schema-less name/type map (field 19000) and schema-driven names.
inline std::unique_ptr<asvJSONValue> protoDecodeMessageBody(const uint8_t* data, size_t begin, size_t end,
                                                           const ProtoSchema* schema, int depth) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) return nullptr;
  auto obj = asvJSONValue::makeObject();
  if (!obj) return nullptr;
  ProtoLessMap less;
  if (!schema) less = protoExtractLessMap(data, begin, end);
  size_t pos = begin;
  while (pos < end) {
    int fnum = 0; int wt = protoReadTag(data, end, pos, fnum);
    if (wt < 0 || wt > 5 || fnum <= 0) break;
    if (wt == 3 || wt == 4) break;
    if (!schema && fnum == protoNameMapFieldNum) {
      // Skip the embedded name map regardless of wire type. A varint-typed
      // 19000 (malformed input) must advance past its value or the next
      // iteration reads a tag out of the middle of it. If the value is
      // truncated (a fixed-size field shorter than its width) stop the body
      // decode rather than letting the parser drift.
      if (!protoSkipWireValue(data, end, pos, wt)) break;
      continue;
    }
    std::string fname = std::to_string(fnum);
    std::string ftype;
    bool repeated = false;
    int extType = protoNoExtType;
    const ProtoSchema* subSchema = nullptr;
    if (schema) {
      auto it = schema->numberToName.find(fnum);
      if (it != schema->numberToName.end()) {
        fname = it->second;
        auto fit = schema->fields.find(fname);
        if (fit != schema->fields.end()) {
          ftype = fit->second.type; repeated = fit->second.repeated;
          if (fit->second.schema) subSchema = fit->second.schema.get();
        }
      }
    } else {
      auto it = less.types.find(fnum);
      if (it != less.types.end()) {
        ftype = protoLessCodeToType(it->second, repeated);
        auto nit = less.names.find(fnum);
        if (nit != less.names.end()) fname = nit->second;
      }
      auto xit = less.extTypes.find(fnum);
      if (xit != less.extTypes.end()) extType = xit->second;
    }
    size_t beforePos = pos;
    auto val = protoDecodeWireValue(data, end, pos, wt, ftype, subSchema, depth + 1, extType);
    if (!val) { if (pos == beforePos) pos++; continue; }
    if (repeated) {
      auto* existing = obj->get(fname);
      if (existing && existing->type == asvJSONValue::ARRAY && existing->arr) {
        if (val->type == asvJSONValue::ARRAY && val->arr) {
          for (auto& e : *val->arr) if (e) existing->arr->push_back(std::move(e));
        } else {
          existing->arr->push_back(std::move(val));
        }
      } else if (val->type == asvJSONValue::ARRAY && val->arr) {
        protoSetField(obj.get(), fname, std::move(val));
      } else {
        auto arr = asvJSONValue::makeArray();
        if (arr && arr->arr) arr->arr->push_back(std::move(val));
        if (arr) protoSetField(obj.get(), fname, std::move(arr));
      }
    } else {
      protoSetField(obj.get(), fname, std::move(val));
    }
  }
  // Schema-less: empty arrays and null values produce no wire bytes (protobuf
  // cannot represent them), so without the name map they'd silently vanish.
  // Recover them from the embedded type codes -- the wire field is absent
  // precisely because the value was empty/null. ("N" null-arrays are left alone
  // as their element count is not recoverable from the wire.)
  if (!schema && less.present) {
    for (const auto& [fnum, code] : less.types) {
      if (code != "A" && code != "n" && code != "N") continue;
      std::string fname = std::to_string(fnum);
      auto nit = less.names.find(fnum);
      if (nit != less.names.end()) fname = nit->second;
      if (obj->obj->find(fname) != obj->obj->end()) continue;
      if (code == "A" || code == "N") {
        auto arr = asvJSONValue::makeArray();
        if (arr) (*obj->obj)[fname] = std::move(arr);
      } else {
        auto nv = asvJSONValue::makeNull();
        if (!nv) throw asvJSONError("Protobuf null field alloc failed");
        (*obj->obj)[fname] = std::move(nv);
      }
    }
  }
  return obj;
}

// Push a nothrow-factory element into a packed-field array, rejecting a null
// (OOM) result: a null element would be dereferenced by every later tree walk.
inline void protoPushPacked(std::vector<std::unique_ptr<asvJSONValue>>& dst, std::unique_ptr<asvJSONValue> v) {
  if (!v) throw asvJSONError("Protobuf packed field element alloc failed");
  dst.push_back(std::move(v));
}

inline std::unique_ptr<asvJSONValue> protoDecodeWireValue(const uint8_t* data, size_t size, size_t& pos,
                                                           int wireType, const std::string& type,
                                                           const ProtoSchema* schema, int depth,
                                                           int extType) {
  switch (wireType) {
    case 0: {
      uint64_t raw = protoReadVarint(data, size, pos);
      if (type == "sint32" || type == "sint64") {
        return asvJSONValue::makeInt(protoZigZagDecode(raw));
      } else if (type == "bool") {
        return asvJSONValue::makeBool(raw != 0);
      } else if (type == "double" || type == "float") {
        return asvJSONValue::makeDouble(static_cast<double>(raw));
      } else {
        return asvJSONValue::makeInt(static_cast<int64_t>(raw));
      }
    }
    case 1: {
      if (pos + 8 > size) return nullptr;
      uint64_t raw = 0;
      for (int i = 0; i < 8; i++) raw |= static_cast<uint64_t>(data[pos++]) << (i * 8);
      if (type == "double" || type == "float") {
        double d; std::memcpy(&d, &raw, sizeof(d)); return asvJSONValue::makeDouble(d);
      }
      return asvJSONValue::makeInt(static_cast<int64_t>(raw));
    }
    case 2: {
      uint64_t len = protoReadVarint(data, size, pos);
      if (len > static_cast<uint64_t>(SIZE_MAX) || static_cast<size_t>(len) > size - pos) { return nullptr; }
      size_t end = pos + static_cast<size_t>(len);

      if (type == "message" || type.empty()) {
        if (depth > asvJSONValue::MAX_NESTING_DEPTH) { pos = end; return nullptr; }
        if (type == "message") {
          auto obj = protoDecodeMessageBody(data, pos, end, schema, depth);
          if (!obj) { pos = end; return nullptr; }
          pos = end; return obj;
        }
        // Schema-less unknown type: try interpreting the payload as a
        // sub-message (embedded name map or structural fields). A blob that
        // merely *starts* like a message - e.g. a byte string whose first byte
        // looks like a valid tag followed by an unterminated varint - must not
        // crash the whole decode with a malformed-varint error; on any trouble
        // we fall through to the string/binary branch instead.
        try {
          size_t save = pos;
          auto obj = protoDecodeMessageBody(data, save, end, nullptr, depth);
          // Accept the object when it decoded as a message. A zero-length
          // payload that decodes to nothing is a valid empty sub-message
          // ({"a":{}}); a non-empty payload that produced no fields is left to
          // the binary/string branch (NUL-prefixed byte strings).
          if (obj && obj->obj && (!obj->obj->empty() || end == save)) { pos = end; return obj; }
        } catch (const std::exception&) {}
      }
      if (type == "string") {
        size_t start = end - static_cast<size_t>(len);
        auto s = asvJSONValue::makeString(reinterpret_cast<const char*>(data + start), static_cast<size_t>(len));
        pos = end; if (!s) throw asvJSONError("Protobuf string too long"); return s;
      } else if (type == "bytes" || type == "binary" || type.empty()) {
        if (static_cast<size_t>(len) > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("Protobuf binary field exceeds MAX_STRING_LEN");
        size_t start = end - static_cast<size_t>(len);
        pos = end;
        // The name map told us this LEN was written from an EXTENSION node, so
        // rebuild one with its type byte instead of a bare BINARY.
        if (extType != protoNoExtType) {
          auto ev = asvJSONValue::makeExtension(static_cast<int8_t>(extType), data + start, static_cast<size_t>(len));
          if (!ev) throw asvJSONError("Protobuf extension field alloc failed");
          return ev;
        }
        auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
        if (!v) throw asvJSONError("Protobuf binary alloc failed");
        v->type = asvJSONValue::BINARY; v->bin_data.assign(data + start, data + end);
        return v;
      } else {
        // Packed repeated field -- decode according to element type
        if (static_cast<size_t>(end - pos) > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("Protobuf packed field too large");
        auto arr = asvJSONValue::makeArray();
        if (arr && arr->arr) {
          if (type == "float" || type == "fixed32" || type == "sfixed32") {
            while (pos + 4 <= end) {
              uint32_t raw = 0;
              for (int i = 0; i < 4; i++) raw |= static_cast<uint32_t>(data[pos++]) << (i * 8);
              if (type == "float") { float f; std::memcpy(&f, &raw, sizeof(f)); protoPushPacked(*arr->arr, asvJSONValue::makeDouble(static_cast<double>(f))); }
              else if (type == "sfixed32") { protoPushPacked(*arr->arr, asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int32_t>(raw)))); }
              else { protoPushPacked(*arr->arr, asvJSONValue::makeInt(static_cast<uint64_t>(raw))); }
            }
            if (pos != end) throw asvJSONError("Protobuf packed fixed-size field has trailing data");
          } else if (type == "double" || type == "fixed64" || type == "sfixed64") {
            while (pos + 8 <= end) {
              uint64_t raw = 0;
              for (int i = 0; i < 8; i++) raw |= static_cast<uint64_t>(data[pos++]) << (i * 8);
              if (type == "double") { double d; std::memcpy(&d, &raw, sizeof(d)); protoPushPacked(*arr->arr, asvJSONValue::makeDouble(d)); }
              else { protoPushPacked(*arr->arr, asvJSONValue::makeInt(static_cast<int64_t>(raw))); }
            }
            if (pos != end) throw asvJSONError("Protobuf packed fixed-size field has trailing data");
          } else if (type == "sint32" || type == "sint64") {
            while (pos < end) { uint64_t v = protoReadVarint(data, end, pos); protoPushPacked(*arr->arr, asvJSONValue::makeInt(protoZigZagDecode(v))); }
          } else if (type == "bool") {
            while (pos < end) { uint64_t v = protoReadVarint(data, end, pos); protoPushPacked(*arr->arr, asvJSONValue::makeBool(v != 0)); }
          } else {
            // int32, int64, uint32, uint64, enum, or unknown -- packed varint
            while (pos < end) { uint64_t v = protoReadVarint(data, end, pos); protoPushPacked(*arr->arr, asvJSONValue::makeInt(static_cast<int64_t>(v))); }
          }
        } else pos = end;
        return arr;
      }
    }
    case 5: {
      if (pos + 4 > size) return nullptr;
      uint32_t raw = 0;
      for (int i = 0; i < 4; i++) raw |= static_cast<uint32_t>(data[pos++]) << (i * 8);
      if (type == "float") { float f; std::memcpy(&f, &raw, sizeof(f)); return asvJSONValue::makeDouble(static_cast<double>(f)); }
      else if (type == "sfixed32") return asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int32_t>(raw)));
      return asvJSONValue::makeInt(static_cast<uint64_t>(raw));
    }
  }
  return nullptr;
}

inline std::unique_ptr<asvJSONValue> protoDecodeRoot(const uint8_t* data, size_t size, const ProtoSchema* schema, int depth = 0) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) return nullptr;
  // Detect the array-root wrapper (marker field 19001 with varint value 1).
  // If the top level is a wrapped array of submessages, collect the repeated
  // field-1 elements into an ARRAY root; a plain message falls through to the
  // normal body decoder.
  {
    size_t probe = 0;
    bool arrayWrap = false;
    while (probe < size) {
      int fnum = 0; int wt = protoReadTag(data, size, probe, fnum);
      if (wt < 0 || fnum <= 0) break;
      if (fnum == protoArrayRootFieldNum && wt == 0) {
        uint64_t v = protoReadVarint(data, size, probe);
        if (v == 1) arrayWrap = true;
        break;
      }
      if (!protoSkipWireValue(data, size, probe, wt)) break;
    }
    if (arrayWrap) {
      auto arr = asvJSONValue::makeArray();
      if (!arr) return nullptr;
      size_t pos = 0;
      while (pos < size) {
        int fnum = 0; int wt = protoReadTag(data, size, pos, fnum);
        if (wt < 0 || fnum <= 0) break;
        if (fnum == protoArrayRootFieldNum || fnum != 1 || wt != 2) {
          if (!protoSkipWireValue(data, size, pos, wt)) break;
          continue;
        }
        uint64_t len = protoReadVarint(data, size, pos);
        if (len > static_cast<uint64_t>(SIZE_MAX) || static_cast<size_t>(len) > size - pos) break;
        size_t end = pos + static_cast<size_t>(len);
        auto elem = protoDecodeMessageBody(data, pos, end, schema, depth + 1);
        if (elem && arr && arr->arr) arr->arr->push_back(std::move(elem));
        pos = end;
      }
      return arr;
    }
  }
  return protoDecodeMessageBody(data, 0, size, schema, depth);
}

// ======================= Text format parser =======================

inline void protoTextSkipWS(const std::string& text, size_t& pos) {
  while (pos < text.size()) {
    char c = text[pos];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { pos++; continue; }
    if (c == '#') { pos++; while (pos < text.size() && text[pos] != '\n') pos++; continue; }
    if (c == '/' && pos + 1 < text.size() && text[pos+1] == '/') {
      pos += 2; while (pos < text.size() && text[pos] != '\n') pos++; continue;
    }
    break;
  }
}

inline std::string protoTextParseStringLit(const std::string& text, size_t& pos) {
  if (pos >= text.size()) return {};
  char q = text[pos];
  if (q != '"' && q != '\'') return {};
  pos++;
  std::string r;
  while (pos < text.size()) {
    char c = text[pos++];
    if (c == q) return r;
    if (c == '\\') {
      if (pos >= text.size()) break; char e = text[pos++];
      switch (e) {
        case 'a': r += '\a'; break; case 'b': r += '\b'; break;
        case 'f': r += '\f'; break; case 'n': r += '\n'; break;
        case 'r': r += '\r'; break; case 't': r += '\t'; break;
        case 'v': r += '\v'; break; case '\\': r += '\\'; break;
        case '\'': r += '\''; break; case '"': r += '"'; break;
        case 'x': {
          int v = 0; bool ok = false;
          if (pos < text.size()) { char h = text[pos];
            if (('0'<=h&&h<='9')||('a'<=h&&h<='f')||('A'<=h&&h<='F')) { pos++;
              v = (h<='9')?(h-'0'):(h<='F'?(h-'A'+10):(h-'a'+10)); ok = true;
              if (pos < text.size()) { h = text[pos];
                if (('0'<=h&&h<='9')||('a'<=h&&h<='f')||('A'<=h&&h<='F'))
                  { pos++; v = (v<<4)|((h<='9')?(h-'0'):(h<='F'?(h-'A'+10):(h-'a'+10))); ok = true; } } }
          }
          if (ok) r += static_cast<char>(v); else { r += '\\'; r += 'x'; } break;
        }
        case 'u': {
          uint32_t cp = 0; int hexCount = 0;
          size_t uStart = pos;
          for (int i = 0; i < 4 && pos < text.size(); i++) { char h = text[pos++];
            if (h>='0'&&h<='9') { cp=(cp<<4)|(h-'0'); hexCount++; }
            else if (h>='a'&&h<='f') { cp=(cp<<4)|(h-'a'+10); hexCount++; }
            else if (h>='A'&&h<='F') { cp=(cp<<4)|(h-'A'+10); hexCount++; }
            else break; }
          if (hexCount < 4) { pos = uStart; appendUtf8Codepoint(r, 0xFFFD); break; }
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos + 1 < text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
              size_t loStart = pos;
              pos += 2; uint32_t lo = 0;
              for (int i = 0; i < 4 && pos < text.size(); i++) { char h = text[pos++];
                if (h>='0'&&h<='9') lo=(lo<<4)|(h-'0');
                else if (h>='a'&&h<='f') lo=(lo<<4)|(h-'a'+10);
                else if (h>='A'&&h<='F') lo=(lo<<4)|(h-'A'+10);
                else { pos = loStart; break; } }
              if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              } else {
                // Not a valid pair: rewind so the "\u..." is reparsed as its
                // own escape, and emit U+FFFD for the lone high surrogate.
                pos = loStart;
                cp = 0xFFFD;
              }
            } else {
              cp = 0xFFFD; // lone high surrogate
            }
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD; // lone low surrogate
          }
          appendUtf8Codepoint(r, cp);
          break;
        }
        default:
          if (e >= '0' && e <= '7') {
            int ov = e - '0';
            if (pos < text.size() && text[pos] >= '0' && text[pos] <= '7') { ov = (ov << 3) | (text[pos++] - '0'); }
            if (pos < text.size() && text[pos] >= '0' && text[pos] <= '7') { ov = (ov << 3) | (text[pos++] - '0'); }
            r += static_cast<char>(ov);
          } else r += e;
      }
    } else r += c;
  }
  return r;
}

// Skip a nested construct up to and including its closing delimiter, used by
// the depth-guard and allocation-failure paths so the caller resumes on the
// next sibling instead of mis-parsing the inner content. Quoted literals are
// consumed whole, otherwise a brace or bracket inside a string value (e.g.
// a: "}{") would skew the depth count and cut the skip short.
//
// opens and closes MUST be disjoint: the opens test runs first, so a character
// listed in both would increment the depth instead of decrementing it and the
// scan would run to EOF, swallowing every following sibling. Pass only the
// opening characters ("[", "{<"), never a full bracket set.
inline void protoTextSkipNested(const std::string& text, size_t& pos, std::string_view opens, std::string_view closes) {
  int d = 1;
  while (pos < text.size() && d > 0) {
    char c = text[pos];
    if (c == '"' || c == '\'') { protoTextParseStringLit(text, pos); continue; }
    if (opens.find(c) != std::string_view::npos) d++;
    else if (closes.find(c) != std::string_view::npos) d--;
    if (d > 0) pos++;
  }
  if (pos < text.size()) pos++;
}

inline std::string protoTextParseIdent(const std::string& text, size_t& pos) {
  std::string id;
  while (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
    id += text[pos++];
  }
  return id;
}

inline std::unique_ptr<asvJSONValue> protoTextParseScalar(const std::string& text, size_t& pos) {
  protoTextSkipWS(text, pos);
  if (pos >= text.size()) return nullptr;
  char c = text[pos];
  if (c == '"' || c == '\'') {
    std::string s = protoTextParseStringLit(text, pos);
    return asvJSONValue::makeString(s.data(), s.size());
  }
  if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) {
    bool neg = (c == '-');
    if (c == '-' || c == '+') pos++;
    protoTextSkipWS(text, pos);
    if (pos >= text.size()) return nullptr; // lone sign: not a valid number
    // Signed inf / infinity / nan words, or any alphabetic token after a sign.
    // "-inf" / "+nan" -> double; everything else (e.g. "-abc", "+infx") survives
    // as a raw-number string instead of disappearing as nullptr.
    if (std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos] == '_') {
      std::string w = protoTextParseIdent(text, pos);
      if (w == "inf" || w == "infinity" || w == "Inf" || w == "Infinity")
        return asvJSONValue::makeDouble(neg ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
      if (w == "nan" || w == "NaN") {
        double v = std::numeric_limits<double>::quiet_NaN();
        return asvJSONValue::makeDouble(neg ? -v : v);
      }
      // Non-inf/nan alphabetic token after a sign: consume any trailing alpha/
      // underscore and return the whole run (including the sign) as a raw-number
      // string so "-abc", "+infx" survive losslessly instead of being eaten as nullptr.
      {
        std::string id = std::move(w);
        if (pos < text.size() &&
            (std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos] == '_'))
          id += protoTextParseIdent(text, pos);
        auto v = asvJSONValue::makeString((neg ? "-" : "+") + id);
        if (v) v->raw_number = true;
        return v;
      }
    }
    // C-style hex integer literal: 0x...
    if (text[pos] == '0' && pos + 1 < text.size() && (text[pos + 1] == 'x' || text[pos + 1] == 'X')) {
      pos += 2;
      std::string hnum; if (neg) hnum = '-'; hnum += "0x";
      uint64_t hv = 0; bool ok = true; bool any = false;
      while (pos < text.size()) {
        unsigned char h = static_cast<unsigned char>(text[pos]);
        unsigned d;
        if (h >= '0' && h <= '9') d = h - '0';
        else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
        else break;
        if (hv > (std::numeric_limits<uint64_t>::max() - d) / 16) ok = false;
        hv = hv * 16 + d;
        hnum += static_cast<char>(h); any = true; pos++;
      }
      if (!any) {
        // "0x" with no hex digits: return the bare literal as a raw number so
        // the token is consumed (pos stays advanced) and no data is lost, same
        // convention as a decimal token with trailing junk.
        auto v = asvJSONValue::makeString(std::move(hnum));
        if (v) v->raw_number = true;
        return v;
      }
      // Trailing junk ("0x1z"): keep the whole run as a raw-number literal
      // instead of silently truncating to the valid hex prefix.
      if (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
        hnum += protoTextParseIdent(text, pos);
        auto v = asvJSONValue::makeString(std::move(hnum));
        if (v) v->raw_number = true;
        return v;
      }
      if (ok && (hv <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
                 (neg && hv == static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1)))
        // -0x8000000000000000 is INT64_MIN, not an overflow: a magnitude check
        // cannot compare 2^63 against INT64_MAX, so the one representable
        // negative overflow is named explicitly instead of being kept as a
        // raw-number literal (which encoded fine on the wire but came back as
        // a STRING from the text parser).
        return asvJSONValue::makeInt(neg
          ? (hv == static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1
               ? std::numeric_limits<int64_t>::min()
               : -static_cast<int64_t>(hv))
          : static_cast<int64_t>(hv));
      // Out-of-range integer: preserve the exact literal so the round-trip
      // is lossless (same convention as the decimal overflow below).
      auto v = asvJSONValue::makeString(std::move(hnum));
      if (v) v->raw_number = true;
      return v;
    }
    // Decimal / octal / scientific notation. Do NOT use protoTextParseIdent for
    // the token: it greedily consumes 'e'/'E' as identifier characters, so
    // "1e5" was swallowed whole (never treated as an exponent) and "1e+10"
    // even stranded the position on the '+', aborting the whole message.
    std::string mantissa;
    bool sawInt = false, sawFrac = false, hasDot = false, hasExp = false;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') { mantissa += text[pos++]; sawInt = true; }
    if (pos < text.size() && text[pos] == '.') {
      mantissa += text[pos++]; hasDot = true;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') { mantissa += text[pos++]; sawFrac = true; }
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
      size_t save = pos;
      pos++;
      std::string ex;
      if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ex += text[pos++];
      size_t dStart = pos;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ex += text[pos++];
      if (pos > dStart && !ex.empty()) { mantissa += 'e'; mantissa += ex; hasExp = true; }
      else pos = save; // no digits after 'e': it is not an exponent
    }
    if (!sawInt && !sawFrac) return nullptr; // nothing numeric after the sign
    std::string num; if (neg) num = '-'; num += mantissa;
    // Protobuf text-format float suffix: "1.5f" is a float literal. Consume it
    // and produce the DOUBLE the value names -- but only when the suffix is the
    // whole tail of the token. A trailing alphanumeric run ("123field", "1e5x")
    // must fall through to the junk branch below so it survives losslessly as a
    // raw number instead of becoming a DOUBLE plus an orphaned identifier.
    if (pos < text.size() && (text[pos] == 'f' || text[pos] == 'F') &&
        (pos + 1 >= text.size() ||
         (!std::isalnum(static_cast<unsigned char>(text[pos + 1])) && text[pos + 1] != '_'))) {
      pos++;
      return asvJSONValue::makeDouble(std::strtod(num.c_str(), nullptr));
    }
    // Trailing junk -- alphanumeric or another dot ("123abc", "1e5x", "1.2.3") --
    // is kept as part of the literal so it round-trips losslessly as a raw number
    // instead of being silently split into a number plus stray characters.
    if (pos < text.size() &&
        (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_' || text[pos] == '.')) {
      size_t tailStart = pos;
      while (pos < text.size() &&
             (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_' || text[pos] == '.'))
        pos++;
      num.append(text, tailStart, pos - tailStart);
      auto v = asvJSONValue::makeString(std::move(num));
      if (v) v->raw_number = true;
      return v;
    }
    // Octal literal (leading 0, only 0-7).
    bool isOctal = !hasDot && !hasExp && mantissa.size() > 1 && mantissa[0] == '0';
    if (isOctal) for (size_t i = 1; i < mantissa.size() && isOctal; i++)
      if (mantissa[i] < '0' || mantissa[i] > '7') isOctal = false;
    if (isOctal) {
      uint64_t ov = 0;
      bool ok = true;
      for (size_t i = 1; i < mantissa.size(); i++) {
        unsigned d = static_cast<unsigned>(mantissa[i] - '0');
        if (ov > (std::numeric_limits<uint64_t>::max() - d) / 8) { ok = false; break; }
        ov = ov * 8 + d;
      }
      if (ok && (ov <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
                 (neg && ov == static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1)))
        return asvJSONValue::makeInt(neg
          ? (ov == static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1
               ? std::numeric_limits<int64_t>::min()
               : -static_cast<int64_t>(ov))
          : static_cast<int64_t>(ov));
      std::string onum; if (neg) onum = '-'; onum += mantissa;
      auto v = asvJSONValue::makeString(std::move(onum));
      if (v) v->raw_number = true;
      return v;
    }
    if (hasDot || hasExp) return asvJSONValue::makeDouble(std::strtod(num.c_str(), nullptr));
    long long l;
    auto [ip, iec] = std::from_chars(num.c_str(), num.c_str() + num.size(), l);
    if (iec == std::errc() && ip == num.c_str() + num.size()) return asvJSONValue::makeInt(l);
    // Out-of-range integer: preserve the exact literal so the round-trip is lossless
    // (same convention as core JSON and ADE).
    auto v = asvJSONValue::makeString(std::move(num));
    if (v) v->raw_number = true;
    return v;
  }
  std::string id = protoTextParseIdent(text, pos);
  if (id.empty()) return nullptr;
  if (id == "true" || id == "True" || id == "t") return asvJSONValue::makeBool(true);
  if (id == "false" || id == "False" || id == "f") return asvJSONValue::makeBool(false);
  if (id == "inf" || id == "Inf" || id == "infinity" || id == "Infinity") return asvJSONValue::makeDouble(std::numeric_limits<double>::infinity());
  if (id == "nan" || id == "NaN") return asvJSONValue::makeDouble(std::numeric_limits<double>::quiet_NaN());
  return asvJSONValue::makeString(id.data(), id.size());
}

// Parse the elements of a `[ ... ]` list (pos just past the `[`), consuming up
// to and including the matching `]`. Element may be a message, a nested array,
// or a scalar; nested arrays recurse so the library's own nested-array output
// round-trips instead of stalling the parser in an infinite loop.
inline void protoTextParseBody(const std::string& text, size_t& pos, asvJSONValue* parent, int depth);

inline void protoTextParseArrayBody(const std::string& text, size_t& pos, asvJSONValue* arr, int depth) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) {
    protoTextSkipNested(text, pos, "[", "]");
    return;
  }
  if (!arr || !arr->arr) {
    protoTextSkipNested(text, pos, "[", "]");
    return;
  }
  while (pos < text.size()) {
    protoTextSkipWS(text, pos);
    if (pos >= text.size() || text[pos] == ']') { if (pos < text.size()) pos++; break; }
    if (text[pos] == '{' || text[pos] == '<') {
      pos++;
      auto elem = asvJSONValue::makeObject();
      if (elem) { protoTextParseBody(text, pos, elem.get(), depth + 1); arr->arr->push_back(std::move(elem)); }
      else { protoTextSkipNested(text, pos, "{<", "}>"); }
    } else if (text[pos] == '[') {
      pos++;
      auto elem = asvJSONValue::makeArray();
      if (elem && elem->arr) { protoTextParseArrayBody(text, pos, elem.get(), depth + 1); arr->arr->push_back(std::move(elem)); }
      else { protoTextSkipNested(text, pos, "[", "]"); }
    } else {
      size_t before = pos;
      auto elem = protoTextParseScalar(text, pos);
      if (elem) arr->arr->push_back(std::move(elem));
      // A scalar that consumed nothing (garbage element like ';') must still
      // advance or the loop below spins forever.
      if (pos == before && pos < text.size()) pos++;
    }
    protoTextSkipWS(text, pos);
    if (pos < text.size() && text[pos] == ',') { pos++; protoTextSkipWS(text, pos); }
  }
}

inline void protoTextParseBody(const std::string& text, size_t& pos, asvJSONValue* parent, int depth = 0) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) {
    // Skip to the matching closing brace so the parent frame does not
    // mis-parse this body's fields as its own.
    protoTextSkipNested(text, pos, "{<", "}>");
    return;
  }
  while (pos < text.size()) {
    protoTextSkipWS(text, pos);
    if (pos >= text.size()) return;
    char c = text[pos];
    if (c == '}' || c == '>') { pos++; return; }

    std::string fname;
    if (c == '[') {
      pos++;
      // Bracketed (extension-style) name: the text format escapes a ']' inside
      // the name with a backslash, so the scan must not stop at the first one
      // it meets -- "[a]b]" used to parse as the name "a" and leave "b]" to be
      // mistaken for a sibling field.
      while (pos < text.size()) {
        char nc = text[pos];
        if (nc == '\\' && pos + 1 < text.size()) { fname += text[pos + 1]; pos += 2; continue; }
        if (nc == ']') { pos++; break; }
        fname += nc;
        pos++;
      }
    } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || (c >= '0' && c <= '9')) {
      fname = protoTextParseIdent(text, pos);
    } else break;

    protoTextSkipWS(text, pos);
    bool hasColon = (pos < text.size() && text[pos] == ':');
    if (hasColon) { pos++; protoTextSkipWS(text, pos); }
    if (pos >= text.size()) break;

    if (text[pos] == '{' || text[pos] == '<') {
      pos++;
      auto obj = asvJSONValue::makeObject();
      if (!obj) { protoTextSkipNested(text, pos, "{<", "}>"); continue; }
      protoTextParseBody(text, pos, obj.get(), depth + 1);
      protoSetField(parent, fname, std::move(obj));
      protoTextSkipWS(text, pos);
      if (pos < text.size() && (text[pos] == ';' || text[pos] == ',')) pos++;
    } else if (text[pos] == '[') {
      pos++;
      auto arr = asvJSONValue::makeArray();
      if (arr && arr->arr) {
        protoTextParseArrayBody(text, pos, arr.get(), depth + 1);
      } else {
        protoTextSkipNested(text, pos, "[", "]");
      }
      protoSetField(parent, fname, std::move(arr));
      protoTextSkipWS(text, pos);
      if (pos < text.size() && (text[pos] == ';' || text[pos] == ',')) pos++;
    } else {
      auto val = protoTextParseScalar(text, pos);
      if (val) protoSetField(parent, fname, std::move(val));
      protoTextSkipWS(text, pos);
      if (pos < text.size() && (text[pos] == ';' || text[pos] == ',')) pos++;
    }
  }
}

// ======================= Text format serializer =======================

inline void protoTextQuote(const std::string& s, std::string& out) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '\a': out += "\\a"; break; case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break; case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
      case '\v': out += "\\v"; break; case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\%03o", static_cast<unsigned char>(c)); out += buf; }
        else out += c;
    }
  }
  out += '"';
}

inline bool protoTextSerializeVal(const asvJSONValue* val, std::string& out, int indent);

inline void protoTextSerializeMsg(const asvJSONValue* val, std::string& out, int indent) {
  if (!val || val->type != asvJSONValue::OBJECT || !val->obj) { out += "{}"; return; }
  auto isValidProtoIdent = [](const std::string& k) -> bool {
    if (k.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(k[0])) || k[0] == '_')) return false;
    for (size_t i = 1; i < k.size(); i++)
      if (!std::isalnum(static_cast<unsigned char>(k[i])) && k[i] != '_') return false;
    return true;
  };
  out += "{\n";
  for (const auto& [k, fv] : *val->obj) {
    if (!fv || fv->type == asvJSONValue::NULL_VAL) continue;
    // Serialize the value into a scratch buffer first: a type the text format
    // cannot represent writes nothing, and emitting the key anyway would leave
    // a bare "k: " line with no value behind. The return value is the signal
    // ("cannot represent"), not an emptiness test on the buffer.
    std::string vs;
    if (!protoTextSerializeVal(fv.get(), vs, indent + 1)) continue;
    for (int i = 0; i < indent + 1; i++) out += "  ";
    if (isValidProtoIdent(k)) out += k;
    else {
      // Bracketed (extension-style) name: ']' ends the name for the parser, so
      // a name containing one has to escape it, and the escape character itself
      // with it -- "[a]b]" would otherwise be read back as the name "a".
      out += '[';
      for (char c : k) {
        if (c == ']' || c == '\\') out += '\\';
        out += c;
      }
      out += ']';
    }
    if (fv->type == asvJSONValue::OBJECT) out += ' ';
    else out += ": ";
    out += vs;
    out += '\n';
  }
  for (int i = 0; i < indent; i++) out += "  ";
  out += '}';
}

// Returns true when something was actually appended to out. A value the text
// format cannot represent (null, and any type with no case below) writes
// nothing, and callers use the return value to avoid leaving a dangling
// separator behind -- an element that wrote nothing must not consume ", ".
inline bool protoTextSerializeVal(const asvJSONValue* val, std::string& out, int indent) {
  if (!val || val->type == asvJSONValue::NULL_VAL) return false;
  switch (val->type) {
    case asvJSONValue::BOOL_VAL: out += val->flag ? "true" : "false"; break;
    case asvJSONValue::INT: { char buf[32]; snprintf(buf, sizeof(buf), "%" PRId64, val->num); out += buf; break; }
    case asvJSONValue::DOUBLE: {
      if (std::isnan(val->dbl)) { out += "nan"; break; }
      if (std::isinf(val->dbl)) { out += (val->dbl > 0) ? "inf" : "-inf"; break; }
      fmtDoubleValDot0(val->dbl, out); break;
    }
    case asvJSONValue::STRING: protoTextQuote(val->str_data, out); break;
    case asvJSONValue::BINARY:
    case asvJSONValue::EXTENSION: {
      auto bin = val->getBinary();
      protoTextQuote(std::string(bin.begin(), bin.end()), out); break;
    }
    case asvJSONValue::OBJECT: protoTextSerializeMsg(val, out, indent); break;
    case asvJSONValue::ARRAY: {
      if (!val->arr || val->arr->empty()) { out += "[]"; break; }
      bool allMsg = true;
      for (const auto& e : *val->arr) if (!e || e->type != asvJSONValue::OBJECT) { allMsg = false; break; }
      if (allMsg) {
        // Array of messages serializes as a bracketed list so each element is a
        // distinct parsed sub-message (the bare repeated-block form was
        // malformed and only the first element survived a round-trip). The
        // elements are joined into a body buffer first: a separator belongs
        // between two WRITTEN elements, so a trailing null cannot leave the
        // invalid "},\n]" behind.
        std::string body;
        for (const auto& e : *val->arr) {
          if (!e) continue;
          if (!body.empty()) body += ",\n";
          for (int j = 0; j < indent + 1; j++) body += "  ";
          protoTextSerializeMsg(e.get(), body, indent + 1);
        }
        out += "[\n";
        out += body;
        if (!body.empty()) out += '\n';
        for (int j = 0; j < indent; j++) out += "  ";
        out += ']';
      } else {
        out += '[';
        bool firstWritten = false;
        for (size_t i = 0; i < val->arr->size(); i++) {
          if (!(*val->arr)[i]) continue;
          if (firstWritten) out += ", ";
          // A null element writes nothing; roll the separator back so the
          // result is "[1, 2]" and never the invalid "[1, , 2]".
          if (!protoTextSerializeVal((*val->arr)[i].get(), out, indent)) {
            if (firstWritten) out.resize(out.size() - 2);
            continue;
          }
          firstWritten = true;
        }
        out += ']';
      }
      break;
    }
    default: return false;
  }
  return true;
}

// ======================= asvJSON wrapper implementations =======================

inline std::vector<uint8_t> asvJSON::toProtobuf(const std::string& schemaJson) const {
  if (!root) return {};
  std::shared_ptr<ProtoSchema> schema;
  if (!schemaJson.empty()) {
    asvJSON sj;
    if (sj.parse(schemaJson)) schema = protoParseSchema(sj.getRoot());
  }
  return protoEncodeRoot(root.get(), schema.get());
}

inline bool asvJSON::fromProtobuf(const void* data, size_t size, const std::string& schemaJson) {
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  if (!data || size < 1) { lastError = "protobuf: empty input"; return false; }
  try {
    std::shared_ptr<ProtoSchema> schema;
    if (!schemaJson.empty()) {
      asvJSON sj;
      if (sj.parse(schemaJson)) schema = protoParseSchema(sj.getRoot());
    }
    root = protoDecodeRoot(static_cast<const uint8_t*>(data), size, schema.get());
    if (root) formatType = FormatType::Protobuf;
    return root != nullptr;
  } catch (const std::exception& e) { lastError = e.what(); root = nullptr; return false; }
}

inline std::string asvJSON::toProtobufText() const {
  std::string out;
  // Only terminate the document when a value was actually written: a NULL root
  // has no text representation, and a bare "\n" would be a document that
  // fromProtobufText() then rejects.
  if (root && protoTextSerializeVal(root.get(), out, 0)) out += '\n';
  return out;
}

// True when the '[' at `pos` opens a bracketed (extension-style) field name
// rather than an array: the name's closing ']' is followed by ':'. The scan
// honours the '\' escape the writer uses inside a name, so a ']' in the name
// itself does not end it.
inline bool protoTextIsBracketedName(const std::string& text, size_t pos) {
  size_t i = pos + 1;
  while (i < text.size()) {
    if (text[i] == '\\' && i + 1 < text.size()) { i += 2; continue; }
    if (text[i] == ']') {
      i++;
      while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) i++;
      return i < text.size() && text[i] == ':';
    }
    if (text[i] == '\n') return false;
    i++;
  }
  return false;
}

inline bool asvJSON::fromProtobufText(const std::string& text) {
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    size_t pos = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
      pos = 3;
    protoTextSkipWS(text, pos);
    if (pos >= text.size()) { lastError = "protobuf text: empty input"; return false; }
    if (text[pos] == '{') {
      pos++;
      auto obj = asvJSONValue::makeObject();
      if (!obj) { lastError = "protobuf text: out of memory"; return false; }
      protoTextParseBody(text, pos, obj.get(), 1);
      if (obj->obj && !obj->obj->empty()) root = std::move(obj);
    } else if (text[pos] == '[' && !protoTextIsBracketedName(text, pos)) {
      // Top-level array. protoTextSerializeVal writes a bare "[...]" for an
      // ARRAY root, so the parser has to accept that form; without this branch
      // toProtobufText() -> fromProtobufText() failed for every array root.
      // A '[' that opens a bracketed field name falls through to the implicit
      // object branch below: "[a\]]: 7" is a message, not a list.
      pos++;
      auto arr = asvJSONValue::makeArray();
      if (!arr) { lastError = "protobuf text: out of memory"; return false; }
      protoTextParseArrayBody(text, pos, arr.get(), 1);
      if (arr->arr) root = std::move(arr);
    } else {
      // Auto-detect implicit object: a bracketed field name, or an identifier
      // followed by ':'
      size_t save = pos;
      size_t probe = pos;
      if (protoTextIsBracketedName(text, probe)) {
        // The name itself is consumed by protoTextParseBody; the probe only
        // has to land on the ':' that follows the closing ']'.
        while (probe < text.size() && text[probe] != ']') { if (text[probe] == '\\' && probe + 1 < text.size()) probe++; probe++; }
        if (probe < text.size()) probe++;
        protoTextSkipWS(text, probe);
      } else {
        if (protoTextParseIdent(text, probe).empty()) probe = save;
        else protoTextSkipWS(text, probe);
      }
      if (probe < text.size() && text[probe] == ':') {
        auto obj = asvJSONValue::makeObject();
        if (!obj) { lastError = "protobuf text: out of memory"; return false; }
        protoTextParseBody(text, pos, obj.get(), 1);
        if (obj->obj && !obj->obj->empty()) root = std::move(obj);
      } else {
        pos = save;
        auto val = protoTextParseScalar(text, pos);
        if (val) root = std::move(val);
      }
    }
    if (root) { formatType = FormatType::ProtobufText; return true; }
    lastError = "protobuf text: no value";
    return false;
  } catch (const std::exception& e) { lastError = e.what(); root = nullptr; return false; }
}

inline std::vector<uint8_t> asvJSON::protobufFromString(const std::string& jsonStr) {
  asvJSON j;
  if (!j.parse(jsonStr)) return {};
  return j.toProtobuf();
}

inline std::string asvJSON::stringFromProtobuf(const uint8_t* data, size_t size) {
  asvJSON j;
  if (!j.fromProtobuf(data, size)) return {};
  return j.serialize();
}

inline bool asvJSON::fromProtobuf(const std::string& data, const std::string& schemaJson) {
  try {
    // Auto-detect base64 (common usage: passing base64-encoded protobuf) but only
    // when the string is made of base64 alphabet chars, its length is a multiple
    // of 4, and it is the *canonical* base64 of its own decode (re-encode
    // round-trip). The re-encode check prevents a genuine binary protobuf whose
    // bytes happen to fall inside the base64 alphabet from being misread as
    // base64 text.
    if (data.size() >= 4 && (data.size() % 4) == 0) {
      bool looksLikeBase64 = true;
      for (size_t i = 0; i < data.size(); i++) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') continue;
        looksLikeBase64 = false;
        break;
      }
      if (looksLikeBase64) {
        auto decoded = decodeBase64Fast(data.data(), data.size());
        if (!decoded.empty()) {
          std::string enc = encodeBase64(decoded.data(), decoded.size());
          // Accept only the canonical padded re-encode. The unpadded comparison
          // is unreachable here (an unpadded input has length % 4 != 0 and never
          // enters this branch), so it was removed.
          bool canonical = (enc == data);
          if (canonical &&
              fromProtobuf(static_cast<const void*>(decoded.data()), decoded.size(), schemaJson))
            return true;
        }
      }
    }
    // Fall back to raw binary
    return fromProtobuf(static_cast<const void*>(data.data()), data.size(), schemaJson);
  } catch (const std::exception& e) { lastError = e.what(); root = nullptr; return false; }
}

} // namespace asvJSONInternal

