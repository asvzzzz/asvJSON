#pragma once
// Protocol Buffers (wire format + text format) for asvJSON++
// Supports schema-less dynamic encoding (field numbers as keys) and JSON-driven schema

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
  // A field number that does not fit in int is invalid (max protobuf field
  // number is 2^29-1); rejecting it prevents forged truncation to a small
  // legitimate-looking field number.
  if (rawNum > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
      rawNum == 0) {
    fieldNum = -1;
    return -2;
  }
  fieldNum = static_cast<int>(rawNum);
  return static_cast<int>(tag & 7);
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
  if (!val || val->type != asvJSONValue::OBJECT) return f;
  auto* n = val->get("id");
  if (n && n->type == asvJSONValue::INT) f.number = static_cast<int>(n->num);
  auto* t = val->get("type");
  if (t && t->type == asvJSONValue::STRING) f.type.assign(t->str_data.data(), t->str_data.size());
  auto* r = val->get("repeated");
  if (r && r->type == asvJSONValue::BOOL_VAL) f.repeated = r->flag;
  auto* p = val->get("packed");
  if (p && p->type == asvJSONValue::BOOL_VAL) f.packed = p->flag;
  if (f.type == "enum") {
    f.type = "int32";
  } else if (f.type == "message") {
    auto* sub = val->get("fields");
    if (sub && sub->type == asvJSONValue::OBJECT) {
      f.schema = std::make_shared<ProtoSchema>();
      f.schema->fields.reserve(sub->obj->size());
      f.schema->numberToName.reserve(sub->obj->size());
      for (const auto& [k, v] : *sub->obj) {
        if (!v) continue;
        auto pf = protoParseOneField(v.get());
        if (pf.number > 0) {
          f.schema->fields[k] = pf;
          f.schema->numberToName[pf.number] = k;
        }
      }
    }
  }
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
    if (pf.number > 0) {
      s->fields[name] = pf;
      s->numberToName[pf.number] = name;
    }
  }
  return s;
}

// Helper: set a field on a JSON value object by key
inline void protoSetField(asvJSONValue* obj, const std::string& key, std::unique_ptr<asvJSONValue> val) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) return;
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
      if (!v->arr || v->arr->empty()) return "A";
      const asvJSONValue* first = nullptr;
      for (const auto& e : *v->arr) if (e) { first = e.get(); break; }
      if (!first) return "A";
      std::string c = protoSchemaLessTypeCode(first);
      if (c == "s") return "S"; if (c == "i") return "I"; if (c == "d") return "D";
      if (c == "b") return "B"; if (c == "n") return "N"; if (c == "y") return "Y";
      if (c == "m") return "M";
      return "A";
    }
    default: return "u";
  }
}

// Map a name-map type code back to a protobuf wire type string and repeated flag.
inline std::string protoLessCodeToType(const std::string& code, bool& repeated) {
  repeated = false;
  if (code.empty() || code == "u" || code == "A" || code == "n" || code == "N") return "";
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

inline void protoEncodeScalar(std::vector<uint8_t>& out, const asvJSONValue* val, const std::string& type) {
  if (!val) return;
  if (type == "int32" || type == "int64") {
    int64_t n = (val->type == asvJSONValue::INT) ? val->num : static_cast<int64_t>(val->dbl);
    protoWriteVarint(out, static_cast<uint64_t>(n));
  } else if (type == "uint32" || type == "uint64") {
    uint64_t n = (val->type == asvJSONValue::INT) ? static_cast<uint64_t>(val->num) : static_cast<uint64_t>(val->dbl);
    protoWriteVarint(out, n);
  } else if (type == "sint32" || type == "sint64") {
    int64_t n = (val->type == asvJSONValue::INT) ? val->num : static_cast<int64_t>(val->dbl);
    protoWriteVarint(out, protoZigZagEncode(n));
  } else if (type == "fixed64" || type == "sfixed64") {
    int64_t n = (val->type == asvJSONValue::INT) ? val->num : static_cast<int64_t>(val->dbl);
    uint64_t bits = static_cast<uint64_t>(n);
    for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "double") {
    double d = (val->type == asvJSONValue::DOUBLE) ? val->dbl : static_cast<double>(val->num);
    uint64_t bits; std::memcpy(&bits, &d, sizeof(bits));
    for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "fixed32" || type == "sfixed32") {
    int32_t n = (val->type == asvJSONValue::INT) ? static_cast<int32_t>(val->num) : static_cast<int32_t>(val->dbl);
    uint32_t bits = static_cast<uint32_t>(n);
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "float") {
    float f = (val->type == asvJSONValue::DOUBLE) ? static_cast<float>(val->dbl) : static_cast<float>(val->num);
    uint32_t bits; std::memcpy(&bits, &f, sizeof(bits));
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  } else if (type == "bool") {
    out.push_back((val->type == asvJSONValue::BOOL_VAL && val->flag) ? 1 : 0);
  } else {
    int64_t n = (val->type == asvJSONValue::INT) ? val->num : 0;
    protoWriteVarint(out, static_cast<uint64_t>(n));
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
        nm += '"' + std::to_string(i + 1) + "\":{\"n\":\"";
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
        nm += "\"}";
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
    std::string s;
    if (val->type == asvJSONValue::STRING) s.assign(val->str_data.data(), val->str_data.size());
    protoWriteVarint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
  } else if (field.type == "bytes") {
    std::vector<uint8_t> bin;
    if (val->type == asvJSONValue::BINARY || val->type == asvJSONValue::EXTENSION) bin = val->getBinary();
    else if (val->type == asvJSONValue::STRING) bin.assign(val->str_data.begin(), val->str_data.end());
    protoWriteVarint(out, bin.size());
    out.insert(out.end(), bin.begin(), bin.end());
  } else if (field.type == "message") {
    std::vector<uint8_t> sub;
    if (val->type == asvJSONValue::OBJECT) {
      protoEncodeMessage(sub, val, field.schema.get());
    }
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
    for (const auto& elem : *root->arr) {
      if (elem && elem->type == asvJSONValue::OBJECT) protoEncodeMessage(out, elem.get(), schema);
    }
  }
  return out;
}

// ======================= Wire format decoder =======================

inline std::unique_ptr<asvJSONValue> protoDecodeWireValue(const uint8_t* data, size_t size, size_t& pos,
                                                           int wireType, const std::string& type,
                                                           const ProtoSchema* schema, int depth = 0);

// Name/type map embedded at the reserved schema-less field number.
struct ProtoLessMap {
  std::unordered_map<int, std::string> names; // fnum -> JSON key
  std::unordered_map<int, std::string> types; // fnum -> type code
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
      if (nv && nv->type == asvJSONValue::STRING) m.names[fnum] = nv->str_data;
      if (tv && tv->type == asvJSONValue::STRING) m.types[fnum] = tv->str_data;
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
      if (wt == 2) { uint64_t len = protoReadVarint(data, end, pos); if (len <= end - pos) pos += static_cast<size_t>(len); }
      continue;
    }
    std::string fname = std::to_string(fnum);
    std::string ftype;
    bool repeated = false;
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
    }
    size_t beforePos = pos;
    auto val = protoDecodeWireValue(data, end, pos, wt, ftype, subSchema, depth + 1);
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
      if (code != "A" && code != "n") continue;
      std::string fname = std::to_string(fnum);
      auto nit = less.names.find(fnum);
      if (nit != less.names.end()) fname = nit->second;
      if (obj->obj->find(fname) != obj->obj->end()) continue;
      if (code == "A") {
        auto arr = asvJSONValue::makeArray();
        if (arr) (*obj->obj)[fname] = std::move(arr);
      } else {
        (*obj->obj)[fname] = asvJSONValue::makeNull();
      }
    }
  }
  return obj;
}

inline std::unique_ptr<asvJSONValue> protoDecodeWireValue(const uint8_t* data, size_t size, size_t& pos,
                                                           int wireType, const std::string& type,
                                                           const ProtoSchema* schema, int depth) {
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
        auto obj = protoDecodeMessageBody(data, pos, end, type == "message" ? schema : nullptr, depth);
        if (!obj) { pos = end; return nullptr; }
        if (type == "message") { pos = end; return obj; }
        // Schema-less unknown type: use the parsed sub-message only if it has
        // structural content (via an embedded name map or real fields); otherwise
        // fall through to the BINARY branch so byte strings stay opaque.
        if (obj->obj && !obj->obj->empty()) { pos = end; return obj; }
      }
      if (type == "string") {
        size_t start = end - static_cast<size_t>(len);
        auto s = asvJSONValue::makeString(reinterpret_cast<const char*>(data + start), static_cast<size_t>(len));
        pos = end; if (!s) throw asvJSONError("Protobuf string too long"); return s;
      } else if (type == "bytes" || type == "binary" || type.empty()) {
        if (static_cast<size_t>(len) > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("Protobuf binary field exceeds MAX_STRING_LEN");
        size_t start = end - static_cast<size_t>(len);
        auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
        pos = end;
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
              if (type == "float") { float f; std::memcpy(&f, &raw, sizeof(f)); arr->arr->push_back(asvJSONValue::makeDouble(static_cast<double>(f))); }
              else if (type == "sfixed32") { arr->arr->push_back(asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int32_t>(raw)))); }
              else { arr->arr->push_back(asvJSONValue::makeInt(static_cast<uint64_t>(raw))); }
            }
            if (pos != end) throw asvJSONError("Protobuf packed fixed-size field has trailing data");
          } else if (type == "double" || type == "fixed64" || type == "sfixed64") {
            while (pos + 8 <= end) {
              uint64_t raw = 0;
              for (int i = 0; i < 8; i++) raw |= static_cast<uint64_t>(data[pos++]) << (i * 8);
              if (type == "double") { double d; std::memcpy(&d, &raw, sizeof(d)); arr->arr->push_back(asvJSONValue::makeDouble(d)); }
              else { arr->arr->push_back(asvJSONValue::makeInt(static_cast<int64_t>(raw))); }
            }
            if (pos != end) throw asvJSONError("Protobuf packed fixed-size field has trailing data");
          } else if (type == "sint32" || type == "sint64") {
            while (pos < end) { uint64_t v = protoReadVarint(data, end, pos); arr->arr->push_back(asvJSONValue::makeInt(protoZigZagDecode(v))); }
          } else if (type == "bool") {
            while (pos < end) { uint64_t v = protoReadVarint(data, end, pos); arr->arr->push_back(asvJSONValue::makeBool(v != 0)); }
          } else {
            // int32, int64, uint32, uint64, enum, or unknown -- packed varint
            while (pos < end) { uint64_t v = protoReadVarint(data, end, pos); arr->arr->push_back(asvJSONValue::makeInt(static_cast<int64_t>(v))); }
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
          int v = 0;
          if (pos < text.size()) { char h = text[pos];
            if (('0'<=h&&h<='9')||('a'<=h&&h<='f')||('A'<=h&&h<='F')) { pos++;
              v = (h<='9')?(h-'0'):(h<='F'?(h-'A'+10):(h-'a'+10));
              if (pos < text.size()) { h = text[pos];
                if (('0'<=h&&h<='9')||('a'<=h&&h<='f')||('A'<=h&&h<='F'))
                  { pos++; v = (v<<4)|((h<='9')?(h-'0'):(h<='F'?(h-'A'+10):(h-'a'+10))); }}}
          }
          r += static_cast<char>(v); break;
        }
        case 'u': {
          uint32_t cp = 0;
          for (int i = 0; i < 4 && pos < text.size(); i++) { char h = text[pos++];
            if (h>='0'&&h<='9') cp=(cp<<4)|(h-'0');
            else if (h>='a'&&h<='f') cp=(cp<<4)|(h-'a'+10);
            else if (h>='A'&&h<='F') cp=(cp<<4)|(h-'A'+10); }
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos + 1 < text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
              pos += 2; uint32_t lo = 0;
              for (int i = 0; i < 4 && pos < text.size(); i++) { char h = text[pos++];
                if (h>='0'&&h<='9') lo=(lo<<4)|(h-'0');
                else if (h>='a'&&h<='f') lo=(lo<<4)|(h-'a'+10);
                else if (h>='A'&&h<='F') lo=(lo<<4)|(h-'A'+10); }
              if (lo >= 0xDC00 && lo <= 0xDFFF)
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
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
    if (pos >= text.size()) { if (neg) return asvJSONValue::makeInt(0); return nullptr; }
    std::string tmp = protoTextParseIdent(text, pos);
    if (tmp == "inf" || tmp == "infinity" || tmp == "Inf" || tmp == "Infinity") {
      return asvJSONValue::makeDouble(neg ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
    }
    if (tmp == "nan" || tmp == "NaN") {
      double v = std::numeric_limits<double>::quiet_NaN();
      return asvJSONValue::makeDouble(neg ? -v : v);
    }
    std::string num;
    if (neg) num = '-';
    num += tmp;
    bool hasDot = false; bool hasExp = false;
    while (pos < text.size()) {
      char ch = text[pos];
      if (ch >= '0' && ch <= '9') { num += ch; pos++; }
      else if (ch == '.' && !hasDot && !hasExp) { num += ch; hasDot = true; pos++; }
      else if ((ch == 'e' || ch == 'E') && !hasExp) { num += ch; hasExp = true; pos++; }
      else if ((ch == '+' || ch == '-') && hasExp && !num.empty() && (num.back() == 'e' || num.back() == 'E')) { num += ch; pos++; }
      else break;
    }
    if (num.empty() || num == "-") return asvJSONValue::makeInt(0);
    if (hasDot || hasExp) return asvJSONValue::makeDouble(std::strtod(num.c_str(), nullptr));
    long long l;
    auto [ip, iec] = std::from_chars(num.c_str(), num.c_str() + num.size(), l);
    if (iec == std::errc() && ip == num.c_str() + num.size()) return asvJSONValue::makeInt(l);
    // Out-of-range integer: preserve the exact literal so the round-trip is lossless
    // (same convention as core JSON and UDE).
    auto v = asvJSONValue::makeStringView(std::string_view(num));
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
    int d = 1;
    while (pos < text.size() && d > 0) {
      if (text[pos] == '[') d++; else if (text[pos] == ']') d--;
      if (d > 0) pos++;
    }
    if (pos < text.size()) pos++;
    return;
  }
  if (!arr || !arr->arr) {
    int d = 1;
    while (pos < text.size() && d > 0) {
      if (text[pos] == '[') d++; else if (text[pos] == ']') d--;
      if (d > 0) pos++;
    }
    if (pos < text.size()) pos++;
    return;
  }
  bool first = true;
  while (pos < text.size()) {
    protoTextSkipWS(text, pos);
    if (pos >= text.size() || text[pos] == ']') { if (pos < text.size()) pos++; break; }
    if (!first && text[pos] == ',') { pos++; protoTextSkipWS(text, pos); }
    first = false;
    if (text[pos] == '{' || text[pos] == '<') {
      pos++;
      auto elem = asvJSONValue::makeObject();
      if (elem) { protoTextParseBody(text, pos, elem.get(), depth + 1); arr->arr->push_back(std::move(elem)); }
      else { int d = 1; while (pos < text.size() && d>0) { if (text[pos]=='{'||text[pos]=='<') d++; else if (text[pos]=='}'||text[pos]=='>') d--; if (d>0) pos++; } }
    } else if (text[pos] == '[') {
      pos++;
      auto elem = asvJSONValue::makeArray();
      if (elem && elem->arr) { protoTextParseArrayBody(text, pos, elem.get(), depth + 1); arr->arr->push_back(std::move(elem)); }
      else { int d = 1; while (pos < text.size() && d > 0) { if (text[pos] == '[') d++; else if (text[pos] == ']') d--; if (d > 0) pos++; } if (pos < text.size()) pos++; }
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
    int d = 1;
    while (pos < text.size() && d > 0) {
      if (text[pos] == '{' || text[pos] == '<') d++;
      else if (text[pos] == '}' || text[pos] == '>') d--;
      if (d > 0) pos++;
    }
    if (pos < text.size()) pos++;
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
while (pos < text.size() && text[pos] != ']') { fname += text[pos++]; }
      if (pos < text.size()) pos++;
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
      if (!obj) { int d = 1; while (pos < text.size() && d>0) { if (text[pos]=='{'||text[pos]=='<') d++; else if (text[pos]=='}'||text[pos]=='>') d--; if (d>0) pos++; } if (pos<text.size()) pos++; continue; }
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
        int d = 1;
        while (pos < text.size() && d > 0) {
          if (text[pos] == '[') d++; else if (text[pos] == ']') d--;
          if (d > 0) pos++;
        }
        if (pos < text.size()) pos++;
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

inline void protoTextSerializeVal(const asvJSONValue* val, std::string& out, int indent);

inline void protoTextSerializeMsg(const asvJSONValue* val, std::string& out, int indent) {
  if (!val || val->type != asvJSONValue::OBJECT || !val->obj) { out += "{}"; return; }
  out += "{\n";
  for (const auto& [k, fv] : *val->obj) {
    if (!fv || fv->type == asvJSONValue::NULL_VAL) continue;
    for (int i = 0; i < indent + 1; i++) out += "  ";
    out += k;
    if (fv->type == asvJSONValue::OBJECT) {
      out += ' '; protoTextSerializeVal(fv.get(), out, indent + 1);
    } else {
      out += ": "; protoTextSerializeVal(fv.get(), out, indent + 1);
    }
    out += '\n';
  }
  for (int i = 0; i < indent; i++) out += "  ";
  out += '}';
}

inline void protoTextSerializeVal(const asvJSONValue* val, std::string& out, int indent) {
  if (!val || val->type == asvJSONValue::NULL_VAL) return;
  switch (val->type) {
    case asvJSONValue::BOOL_VAL: out += val->flag ? "true" : "false"; break;
    case asvJSONValue::INT: { char buf[32]; snprintf(buf, sizeof(buf), "%" PRId64, val->num); out += buf; break; }
    case asvJSONValue::DOUBLE: {
      if (std::isnan(val->dbl)) { out += "nan"; break; }
      if (std::isinf(val->dbl)) { out += (val->dbl > 0) ? "inf" : "-inf"; break; }
      char buf[64]; snprintf(buf, sizeof(buf), "%.16g", val->dbl); out += buf; break;
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
      for (const auto& e : *val->arr) if (e && e->type != asvJSONValue::OBJECT) { allMsg = false; break; }
      if (allMsg) {
        // Array of messages serializes as a bracketed list so each element is a
        // distinct parsed sub-message (the bare repeated-block form was
        // malformed and only the first element survived a round-trip).
        out += "[\n";
        for (size_t i = 0; i < val->arr->size(); i++) {
          const auto& e = (*val->arr)[i];
          if (!e) continue;
          for (int j = 0; j < indent + 1; j++) out += "  ";
          protoTextSerializeMsg(e.get(), out, indent + 1);
          if (i + 1 < val->arr->size()) out += ',';
          out += '\n';
        }
        for (int j = 0; j < indent; j++) out += "  ";
        out += ']';
      } else {
        out += '[';
        for (size_t i = 0; i < val->arr->size(); i++) {
          if ((*val->arr)[i]) {
            if (i > 0) out += ", ";
            protoTextSerializeVal((*val->arr)[i].get(), out, indent);
          }
        }
        out += ']';
      }
      break;
    }
    default: break;
  }
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
  if (!data || size < 1) return false;
  try {
    std::shared_ptr<ProtoSchema> schema;
    if (!schemaJson.empty()) {
      asvJSON sj;
      if (sj.parse(schemaJson)) schema = protoParseSchema(sj.getRoot());
    }
    root = protoDecodeRoot(static_cast<const uint8_t*>(data), size, schema.get());
    if (root) formatType = FormatType::Protobuf;
    return root != nullptr;
  } catch (const asvJSONError& e) { lastError = e.what(); root = nullptr; return false; }
}

inline std::string asvJSON::toProtobufText() const {
  std::string out;
  if (root) { protoTextSerializeVal(root.get(), out, 0); out += '\n'; }
  return out;
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
    if (pos >= text.size()) return false;
    if (text[pos] == '{') {
      pos++;
      auto obj = asvJSONValue::makeObject();
      if (!obj) return false;
      protoTextParseBody(text, pos, obj.get(), 1);
      if (obj->obj && !obj->obj->empty()) root = std::move(obj);
    } else {
      // Auto-detect implicit object: identifier followed by ':'
      size_t save = pos;
      std::string maybeIdent = protoTextParseIdent(text, pos);
      protoTextSkipWS(text, pos);
      bool isObject = !maybeIdent.empty() && pos < text.size() && text[pos] == ':';
      pos = save;
      if (isObject) {
        auto obj = asvJSONValue::makeObject();
        if (!obj) return false;
        protoTextParseBody(text, pos, obj.get(), 1);
        if (obj->obj && !obj->obj->empty()) root = std::move(obj);
      } else {
        auto val = protoTextParseScalar(text, pos);
        if (val) root = std::move(val);
      }
    }
    if (root) formatType = FormatType::ProtobufText;
    return root != nullptr;
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
  // Auto-detect base64: if data is all valid Base64 and length is multiple of 4,
  // try base64 decoding first (common usage: passing base64-encoded protobuf)
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
        if (fromProtobuf(static_cast<const void*>(decoded.data()), decoded.size(), schemaJson))
          return true;
      }
    }
  }
  // Fall back to raw binary
  return fromProtobuf(static_cast<const void*>(data.data()), data.size(), schemaJson);
}

} // namespace asvJSONInternal

