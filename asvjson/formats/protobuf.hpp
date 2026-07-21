#pragma once
// Protocol Buffers (wire format + text format) for asvJSON++
// Supports schema-less dynamic encoding (field numbers as keys) and JSON-driven schema

#include "../core.hpp"
#include <cstring>
#include <cmath>
#include <cinttypes>

namespace asvJSONInternal {

// ======================= Varint helpers =======================

static void protoWriteVarint(std::vector<uint8_t>& out, uint64_t val) {
  while (val >= 0x80) {
    out.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
    val >>= 7;
  }
  out.push_back(static_cast<uint8_t>(val));
}

static uint64_t protoReadVarint(const uint8_t* data, size_t size, size_t& pos) {
  uint64_t val = 0;
  int shift = 0;
  while (pos < size) {
    uint8_t b = data[pos++];
    val |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) return val;
    shift += 7;
    if (shift > 63) return 0;
  }
  return 0;
}

static int64_t protoZigZagDecode(uint64_t val) {
  return (val >> 1) ^ -(static_cast<int64_t>(val) & 1);
}

static uint64_t protoZigZagEncode(int64_t val) {
  return (static_cast<uint64_t>(val) << 1) ^ static_cast<uint64_t>(val >> 63);
}

static void protoWriteTag(std::vector<uint8_t>& out, int fieldNum, int wireType) {
  protoWriteVarint(out, (static_cast<uint64_t>(fieldNum) << 3) | static_cast<uint64_t>(wireType));
}

static int protoReadTag(const uint8_t* data, size_t size, size_t& pos, int& fieldNum) {
  if (pos >= size) return -1;
  uint64_t tag = protoReadVarint(data, size, pos);
  fieldNum = static_cast<int>(tag >> 3);
  return static_cast<int>(tag & 7);
}

// ======================= Schema types =======================

struct ProtoField;
struct ProtoSchema {
  std::unordered_map<std::string, ProtoField> fields;
  std::unordered_map<int, std::string> numberToName;
};

struct ProtoField {
  int number = 0;
  std::string type;
  bool repeated = false;
  bool packed = true;
  std::shared_ptr<ProtoSchema> schema;
};

static ProtoField protoParseOneField(const asvJSONValue* val) {
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

static std::shared_ptr<ProtoSchema> protoParseSchema(const asvJSONValue* schemaVal) {
  auto s = std::make_shared<ProtoSchema>();
  if (!schemaVal || schemaVal->type != asvJSONValue::OBJECT || !schemaVal->obj) return s;
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
static void protoSetField(asvJSONValue* obj, const std::string& key, std::unique_ptr<asvJSONValue> val) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) return;
  auto it = obj->obj->find(key);
  if (it != obj->obj->end()) {
    // Repeated field — convert to array if not already
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

static void protoEncodeScalar(std::vector<uint8_t>& out, const asvJSONValue* val, const std::string& type) {
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

static void protoEncodeFieldValue(std::vector<uint8_t>& out, const asvJSONValue* val, const ProtoField& field);

static void protoEncodeMessage(std::vector<uint8_t>& out, const asvJSONValue* val, const ProtoSchema* schema) {
  if (!val || val->type != asvJSONValue::OBJECT || !val->obj) return;
  for (const auto& [key, fieldVal] : *val->obj) {
    if (!fieldVal) continue;
    if (schema) {
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
    } else {
      int fnum = static_cast<int>(std::strtol(key.c_str(), nullptr, 10));
      if (fnum <= 0) continue;
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
            protoEncodeMessage(sub, fieldVal.get(), nullptr);
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

static void protoEncodeFieldValue(std::vector<uint8_t>& out, const asvJSONValue* val, const ProtoField& field) {
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

static std::vector<uint8_t> protoEncodeRoot(const asvJSONValue* root, const ProtoSchema* schema) {
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

static std::unique_ptr<asvJSONValue> protoDecodeWireValue(const uint8_t* data, size_t size, size_t& pos,
                                                           int wireType, const std::string& type,
                                                           const ProtoSchema* schema, int depth = 0) {
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
      if (pos + static_cast<size_t>(len) > size) { pos += static_cast<size_t>(len); return nullptr; }
      size_t end = pos + static_cast<size_t>(len);

      if (type == "message" || type.empty()) {
        if (depth > asvJSONValue::MAX_NESTING_DEPTH) { pos = end; return nullptr; }
        auto obj = asvJSONValue::makeObject();
        if (!obj) { pos = end; return nullptr; }
        while (pos < end) {
          int fnum = 0; int wt = protoReadTag(data, size, pos, fnum);
          if (wt < 0 || wt > 5 || fnum <= 0) { pos = end; break; }
          if (wt == 3 || wt == 4) break;
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
          }
          size_t beforePos = pos;
          auto val = protoDecodeWireValue(data, size, pos, wt, ftype, subSchema, depth + 1);
          if (!val) { pos = end; break; }
          if (repeated) {
            auto* existing = obj->get(fname);
            if (existing && existing->type == asvJSONValue::ARRAY && existing->arr) {
              if (val->type == asvJSONValue::ARRAY && val->arr) {
                for (auto& e : *val->arr) if (e) existing->arr->push_back(std::move(e));
              } else {
                existing->arr->push_back(std::move(val));
              }
            } else if (val->type == asvJSONValue::ARRAY && val->arr) {
              // Packed repeated -- use the array directly
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
        pos = end;
        return obj;
      } else if (type == "string" || (wireType == 2 && type.empty())) {
        auto s = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(len));
        pos = end; return s;
      } else if (type == "bytes" || type == "binary") {
        auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
        if (v) { v->type = asvJSONValue::BINARY; v->bin_data.assign(data + pos, data + end); }
        pos = end; return v;
      } else {
        // Packed repeated field -- decode according to element type
        auto arr = asvJSONValue::makeArray();
        if (arr && arr->arr) {
          if (type == "float" || type == "fixed32" || type == "sfixed32") {
            while (pos + 4 <= end) {
              uint32_t raw = 0;
              for (int i = 0; i < 4; i++) raw |= static_cast<uint32_t>(data[pos++]) << (i * 8);
              if (type == "float") { float f; std::memcpy(&f, &raw, sizeof(f)); arr->arr->push_back(asvJSONValue::makeDouble(static_cast<double>(f))); }
              else { arr->arr->push_back(asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int32_t>(raw)))); }
            }
          } else if (type == "double" || type == "fixed64" || type == "sfixed64") {
            while (pos + 8 <= end) {
              uint64_t raw = 0;
              for (int i = 0; i < 8; i++) raw |= static_cast<uint64_t>(data[pos++]) << (i * 8);
              if (type == "double") { double d; std::memcpy(&d, &raw, sizeof(d)); arr->arr->push_back(asvJSONValue::makeDouble(d)); }
              else { arr->arr->push_back(asvJSONValue::makeInt(static_cast<int64_t>(raw))); }
            }
          } else if (type == "sint32" || type == "sint64") {
            while (pos < end) { uint64_t v = protoReadVarint(data, size, pos); arr->arr->push_back(asvJSONValue::makeInt(protoZigZagDecode(v))); }
          } else if (type == "bool") {
            while (pos < end) { uint64_t v = protoReadVarint(data, size, pos); arr->arr->push_back(asvJSONValue::makeBool(v != 0)); }
          } else {
            // int32, int64, uint32, uint64, enum, or unknown -- packed varint
            while (pos < end) { uint64_t v = protoReadVarint(data, size, pos); arr->arr->push_back(asvJSONValue::makeInt(static_cast<int64_t>(v))); }
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
      return asvJSONValue::makeInt(static_cast<int64_t>(raw));
    }
  }
  return nullptr;
}

static std::unique_ptr<asvJSONValue> protoDecodeRoot(const uint8_t* data, size_t size, const ProtoSchema* schema, int depth = 0) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) return nullptr;
  size_t pos = 0;
  auto root = asvJSONValue::makeObject();
  if (!root) return nullptr;
  while (pos < size) {
    int fnum = 0; int wt = protoReadTag(data, size, pos, fnum);
    if (wt < 0 || wt > 5 || fnum <= 0) break;
    if (wt == 3 || wt == 4) break;

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
    }
    size_t beforePos = pos;
    auto val = protoDecodeWireValue(data, size, pos, wt, ftype, subSchema, depth + 1);
    if (!val) { pos = beforePos; pos++; continue; }

    if (repeated) {
      auto* existing = root->get(fname);
      if (existing && existing->type == asvJSONValue::ARRAY && existing->arr) {
        if (val->type == asvJSONValue::ARRAY && val->arr) {
          for (auto& e : *val->arr) if (e) existing->arr->push_back(std::move(e));
        } else {
          existing->arr->push_back(std::move(val));
        }
      } else if (val->type == asvJSONValue::ARRAY && val->arr) {
        // Packed repeated -- use the array directly
        protoSetField(root.get(), fname, std::move(val));
      } else {
        auto arr = asvJSONValue::makeArray();
        if (arr && arr->arr) arr->arr->push_back(std::move(val));
        if (arr) protoSetField(root.get(), fname, std::move(arr));
      }
    } else {
      protoSetField(root.get(), fname, std::move(val));
    }
  }
  return root;
}

// ======================= Text format parser =======================

static void protoTextSkipWS(const std::string& text, size_t& pos) {
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

static std::string protoTextParseStringLit(const std::string& text, size_t& pos) {
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
          if (cp<0x80) r+=static_cast<char>(cp);
          else if (cp<0x800) { r+=static_cast<char>(0xC0|(cp>>6)); r+=static_cast<char>(0x80|(cp&0x3F)); }
          else { r+=static_cast<char>(0xE0|(cp>>12)); r+=static_cast<char>(0x80|((cp>>6)&0x3F)); r+=static_cast<char>(0x80|(cp&0x3F)); }
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

static std::string protoTextParseIdent(const std::string& text, size_t& pos) {
  std::string id;
  while (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
    id += text[pos++];
  }
  return id;
}

static std::unique_ptr<asvJSONValue> protoTextParseScalar(const std::string& text, size_t& pos) {
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
    return asvJSONValue::makeInt(std::strtoll(num.c_str(), nullptr, 10));
  }
  std::string id = protoTextParseIdent(text, pos);
  if (id.empty()) return nullptr;
  if (id == "true" || id == "True" || id == "t") return asvJSONValue::makeBool(true);
  if (id == "false" || id == "False" || id == "f") return asvJSONValue::makeBool(false);
  if (id == "inf" || id == "Inf" || id == "infinity" || id == "Infinity") return asvJSONValue::makeDouble(std::numeric_limits<double>::infinity());
  if (id == "nan" || id == "NaN") return asvJSONValue::makeDouble(std::numeric_limits<double>::quiet_NaN());
  return asvJSONValue::makeString(id.data(), id.size());
}

static void protoTextParseBody(const std::string& text, size_t& pos, asvJSONValue* parent, int depth = 0) {
  if (depth > asvJSONValue::MAX_NESTING_DEPTH) return;
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
    } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      fname = protoTextParseIdent(text, pos);
    } else break;

    protoTextSkipWS(text, pos);
    bool hasColon = (pos < text.size() && text[pos] == ':');
    if (hasColon) { pos++; protoTextSkipWS(text, pos); }
    if (pos >= text.size()) break;

    if (text[pos] == '{' || text[pos] == '<') {
      char close = (text[pos] == '{') ? '}' : '>';
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
        bool first = true;
        while (pos < text.size()) {
          protoTextSkipWS(text, pos);
          if (pos >= text.size() || text[pos] == ']') { if (pos < text.size()) pos++; break; }
          if (!first && text[pos] == ',') { pos++; protoTextSkipWS(text, pos); }
          first = false;
          if (pos < text.size() && (text[pos] == '{' || text[pos] == '<')) {
            char close = (text[pos] == '{') ? '}' : '>'; pos++;
            auto elem = asvJSONValue::makeObject();
            if (elem) { protoTextParseBody(text, pos, elem.get(), depth + 1); arr->arr->push_back(std::move(elem)); }
            else { int d = 1; while (pos < text.size() && d>0) { if (text[pos]=='{'||text[pos]=='<') d++; else if (text[pos]=='}'||text[pos]=='>') d--; if (d>0) pos++; } }
          } else {
            auto elem = protoTextParseScalar(text, pos);
            if (elem) arr->arr->push_back(std::move(elem));
          }
          protoTextSkipWS(text, pos);
          if (pos < text.size() && text[pos] == ',') { pos++; protoTextSkipWS(text, pos); }
        }
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

static void protoTextQuote(const std::string& s, std::string& out) {
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

static void protoTextSerializeVal(const asvJSONValue* val, std::string& out, int indent);

static void protoTextSerializeMsg(const asvJSONValue* val, std::string& out, int indent) {
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

static void protoTextSerializeVal(const asvJSONValue* val, std::string& out, int indent) {
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
        for (const auto& e : *val->arr) {
          if (!e) continue;
          for (int i = 0; i < indent; i++) out += "  ";
          protoTextSerializeMsg(e.get(), out, indent);
          out += '\n';
        }
        // Remove trailing whitespace lines
        while (out.size() >= 2 && out[out.size()-1] == '\n' && out[out.size()-2] == ' ') out.resize(out.size() - 2);
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
  if (!data || size < 1) return false;
  try {
    std::shared_ptr<ProtoSchema> schema;
    if (!schemaJson.empty()) {
      asvJSON sj;
      if (sj.parse(schemaJson)) schema = protoParseSchema(sj.getRoot());
    }
    root = protoDecodeRoot(static_cast<const uint8_t*>(data), size, schema.get());
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
  try {
    size_t pos = 0;
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
  return fromProtobuf(static_cast<const void*>(data.data()), data.size(), schemaJson);
}

} // namespace asvJSONInternal
