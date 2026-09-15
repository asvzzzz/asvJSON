#pragma once
// JSON Schema validation for asvJSON++
// Supports draft-07 core validation keywords:
//   type, const, enum, allOf, anyOf, oneOf, not,
//   if/then/else, $ref (local),
//   string: minLength, maxLength, pattern,
//   number: minimum, maximum, exclusiveMinimum, exclusiveMaximum, multipleOf,
//   array:  minItems, maxItems, uniqueItems, items, contains,
//   object: required, properties, patternProperties, additionalProperties,
//           minProperties, maxProperties, propertyNames, dependentRequired, dependencies.

#include "../core.hpp"
#include <cstring>
#include <limits>
#include <memory>
#include <cmath>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace asvJSONInternal {

class SchemaValidator {
public:
  bool validate(const asvJSONValue* value, const asvJSONValue* schema) {
    error_.clear();
    path_ = "";
    if (!schema) return true;
    static const asvJSONValue kNull; // default-constructed node is NULL_VAL
    rootSchema_ = schema;
    refDepth_ = std::make_shared<int>(0);
    patternCache_ = std::make_shared<PatternCache>();
    return validateNode(value ? value : &kNull, schema);
  }

  bool validate(const asvJSONValue* value, const asvJSONValue* schema, std::string& errorOut) {
    bool ok = validate(value, schema);
    errorOut = error_;
    return ok;
  }

  [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
  const asvJSONValue* rootSchema_ = nullptr;
  std::string error_;
  std::string path_;
  // Shared by every sub-validator forked by anyOf/oneOf/not/if/contains (and
  // copied into them), so it tracks how deeply nested $ref resolution currently
  // is across the WHOLE validation, not just along one linear ref chain. A
  // self-/mutually-referential schema (e.g. `{"$ref":"#"}`,
  // `"a":{"$ref":"#/definitions/a"}`, or a cycle reached again from inside an
  // anyOf/oneOf/not/if branch) would otherwise recurse forever through
  // validateNode and overflow the stack. Cap at MAX_NESTING_DEPTH like the
  // parsers do; a legitimate recursive schema follows the document's finite
  // depth, so a chain deeper than MAX_NESTING_DEPTH is necessarily a loop.
  std::shared_ptr<int> refDepth_ = std::make_shared<int>(0);
  // Compiled-regex cache shared across the whole validation so repeated
  // `pattern` / `patternProperties` evaluations compile each unique regex once
  // per validate() call instead of once per string node / per object.
  struct PatternCache {
    std::unordered_map<std::string, std::regex> ok;
    std::unordered_set<std::string> bad;
  };
  std::shared_ptr<PatternCache> patternCache_;

  void setError(const std::string& msg) {
    if (!error_.empty()) return;
    error_ = path_.empty() ? msg : (path_ + ": " + msg);
  }

  void pushPath(const std::string& seg) {
    if (seg.find('\"') != std::string::npos || seg.find('\\') != std::string::npos || seg.find('/') != std::string::npos) {
      std::string escaped;
      escaped.reserve(seg.size() + 2);
      escaped += '\"';
      for (char c : seg) {
        if (c == '\"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else escaped += c;
      }
      escaped += '\"';
      path_ += "/" + escaped;
    } else {
      path_ += "/" + seg;
    }
  }

  void pushIndexPath(size_t idx) {
    path_ += "/" + std::to_string(idx);
  }


  static const asvJSONValue* schemaGet(const asvJSONValue* s, const std::string& key) {
    if (!s || s->type != asvJSONValue::OBJECT || !s->obj) return nullptr;
    auto it = s->obj->find(key);
    return it != s->obj->end() ? it->second.get() : nullptr;
  }

  static bool schemaHas(const asvJSONValue* s, const std::string& key) {
    return schemaGet(s, key) != nullptr;
  }

  static bool isBoolTrue(const asvJSONValue* s) {
    return s && s->type == asvJSONValue::BOOL_VAL && s->flag;
  }

  static bool isBoolFalse(const asvJSONValue* s) {
    return s && s->type == asvJSONValue::BOOL_VAL && !s->flag;
  }

  // Percent-decodes a single URI-fragment token (%XX -> byte). Bare '%' and
  // malformed escapes are left as-is.
  static std::string percentDecode(std::string_view seg) {
    std::string out;
    out.reserve(seg.size());
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    for (size_t i = 0; i < seg.size(); i++) {
      if (seg[i] == '%' && i + 2 < seg.size()) {
        int hi = hex(seg[i + 1]);
        int lo = hex(seg[i + 2]);
        if (hi >= 0 && lo >= 0) { out += static_cast<char>((hi << 4) | lo); i += 2; }
        else out += seg[i];
      } else {
        out += seg[i];
      }
    }
    return out;
  }

  // Decodes one JSON Pointer reference token: URI percent-escaping first, then
  // the ~0 (-> ~) / ~1 (-> /) escapes.
  static std::string decodePointerToken(std::string_view seg) {
    std::string decoded = percentDecode(seg);
    std::string out;
    out.reserve(decoded.size());
    for (size_t i = 0; i < decoded.size(); i++) {
      if (decoded[i] == '~' && i + 1 < decoded.size()) {
        if (decoded[i + 1] == '0') { out += '~'; i++; }
        else if (decoded[i + 1] == '1') { out += '/'; i++; }
        else out += decoded[i];
      } else {
        out += decoded[i];
      }
    }
    return out;
  }

  const asvJSONValue* resolveRef(std::string_view ref) const {
    if (ref.empty() || ref[0] != '#') return nullptr;
    ref.remove_prefix(1);
    if (ref.empty()) return rootSchema_;
    if (ref[0] != '/') return nullptr;
    ref.remove_prefix(1);
    const asvJSONValue* cur = rootSchema_;
    // JSON Pointer reference tokens are the pieces between '/' (the first may
    // be an empty string, correctly resolving to the object key "").
    while (true) {
      auto slash = ref.find('/');
      std::string_view seg = (slash == std::string_view::npos) ? ref : ref.substr(0, slash);
      if (!cur || cur->type != asvJSONValue::OBJECT || !cur->obj) return nullptr;
      auto it = cur->obj->find(decodePointerToken(seg));
      if (it == cur->obj->end()) return nullptr;
      cur = it->second.get();
      if (slash == std::string_view::npos) break;
      ref.remove_prefix(slash + 1);
    }
    return cur;
  }

  static bool valuesEqual(const asvJSONValue* a, const asvJSONValue* b) {
    if (!a || !b) return a == b;
    if (a->type != b->type) {
      if ((a->type == asvJSONValue::INT || a->type == asvJSONValue::DOUBLE) &&
          (b->type == asvJSONValue::INT || b->type == asvJSONValue::DOUBLE)) {
        return compareNumbers(a, b) == 0;
      }
      return false;
    }
    switch (a->type) {
      case asvJSONValue::NULL_VAL: return true;
      case asvJSONValue::BOOL_VAL: return a->flag == b->flag;
      case asvJSONValue::INT: return a->num == b->num;
      case asvJSONValue::DOUBLE: return a->dbl == b->dbl;
      case asvJSONValue::STRING: return a->str_data == b->str_data;
      case asvJSONValue::ARRAY: {
        if (!a->arr || !b->arr) return a->arr == b->arr;
        if (a->arr->size() != b->arr->size()) return false;
        for (size_t i = 0; i < a->arr->size(); i++)
          if (!valuesEqual((*a->arr)[i].get(), (*b->arr)[i].get())) return false;
        return true;
      }
      case asvJSONValue::OBJECT: {
        if (!a->obj || !b->obj) return a->obj == b->obj;
        if (a->obj->size() != b->obj->size()) return false;
        for (auto& kv : *a->obj) {
          auto it = b->obj->find(kv.first);
          if (it == b->obj->end()) return false;
          if (!valuesEqual(kv.second.get(), it->second.get())) return false;
        }
        return true;
      }
      default: return false;
    }
  }

  static std::string valueTypeName(const asvJSONValue* v) {
    if (!v) return "null";
    switch (v->type) {
      case asvJSONValue::NULL_VAL: return "null";
      case asvJSONValue::BOOL_VAL: return "boolean";
      case asvJSONValue::INT: return "integer";
      case asvJSONValue::DOUBLE: return "number";
      case asvJSONValue::STRING: return "string";
      case asvJSONValue::ARRAY: return "array";
      case asvJSONValue::OBJECT: return "object";
      default: return "unknown";
    }
  }

  static bool isIntegerType(const asvJSONValue* v) {
    return v && v->type == asvJSONValue::INT;
  }

  static bool isNumberType(const asvJSONValue* v) {
    return v && (v->type == asvJSONValue::INT || v->type == asvJSONValue::DOUBLE);
  }

  static double getNumberValue(const asvJSONValue* v) {
    if (!v) return 0;
    if (v->type == asvJSONValue::INT) return static_cast<double>(v->num);
    if (v->type == asvJSONValue::DOUBLE) return v->dbl;
    return 0;
  }

  // Exact cross-type int64/double ordering. Avoids the precision loss of
  // converting a wide int64 to double (e.g. 2^63-1 vs the double 2^63), which
  // would otherwise make distinct numbers compare equal.
  static int compareIntDouble(int64_t a, double b) {
    if (b != b) return 0; // NaN: unordered, treat as equal
    if (b == std::numeric_limits<double>::infinity()) return -1;
    if (b == -std::numeric_limits<double>::infinity()) return 1;
    if (!(b >= -9223372036854775808.0 && b < 9223372036854775808.0)) return b < 0 ? 1 : -1;
    int64_t ib = static_cast<int64_t>(b);
    if (a < ib) return -1;
    if (a > ib) return 1;
    if (static_cast<double>(ib) == b) return 0;  // b is exactly a
    return b < 0 ? 1 : -1;                          // a is trunc(b), fractional b is strictly larger/smaller
  }

  static bool intEqualsDouble(int64_t a, double b) { return compareIntDouble(a, b) == 0; }

  // Exact numeric ordering for two JSON numbers; INT/INT pairs never go
  // through double.
  static int compareNumbers(const asvJSONValue* a, const asvJSONValue* b) {
    if (a->type == asvJSONValue::INT && b->type == asvJSONValue::INT) {
      if (a->num < b->num) return -1;
      if (a->num > b->num) return 1;
      return 0;
    }
    if (a->type == asvJSONValue::INT) return compareIntDouble(a->num, b->dbl);
    if (b->type == asvJSONValue::INT) return -compareIntDouble(b->num, a->dbl);
    if (a->dbl < b->dbl) return -1;
    if (a->dbl > b->dbl) return 1;
    return 0;
  }

  // Unicode code-point count of a UTF-8 string, robust for malformed input:
  // stray continuation bytes count as one character each and a truncated
  // multibyte tail counts once, so the total never runs away.
  static int64_t utf8Length(const std::string& s) {
    int64_t n = 0;
    size_t i = 0;
    while (i < s.size()) {
      auto c = static_cast<unsigned char>(s[i]);
      if (c < 0x80) { n++; i++; }
      else if (c < 0xC0) { n++; i++; } // stray continuation byte
      else {
        size_t len = (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        if (i + len > s.size()) len = 1; // truncated tail: count once, stop
        n++;
        i += len;
      }
    }
    return n;
  }

  bool schemaPassesTypeCheck(const asvJSONValue* value, const asvJSONValue* typeNode) const {
    if (!typeNode) return true;
    if (typeNode->type == asvJSONValue::STRING) {
      return checkSingleType(value, typeNode->str_data);
    }
    if (typeNode->type == asvJSONValue::ARRAY && typeNode->arr) {
      for (const auto& t : *typeNode->arr) {
        if (t->type == asvJSONValue::STRING && checkSingleType(value, t->str_data))
          return true;
      }
      return false;
    }
    return true;
  }

  static bool checkSingleType(const asvJSONValue* v, std::string_view t) {
    switch (v->type) {
      case asvJSONValue::NULL_VAL: return t == "null";
      case asvJSONValue::BOOL_VAL: return t == "boolean";
      case asvJSONValue::INT: return t == "integer" || t == "number";
      case asvJSONValue::DOUBLE:
        if (t == "number") return true;
        if (t == "integer") return std::isfinite(v->dbl) && std::trunc(v->dbl) == v->dbl;
        return false;
      case asvJSONValue::STRING: return t == "string";
      case asvJSONValue::ARRAY: return t == "array";
      case asvJSONValue::OBJECT: return t == "object";
      default: return false;
    }
  }

  // Returns a compiled regex for `pat` from the validator-wide cache. On a
  // bad pattern sets valid=false (the pattern is remembered so later lookups
  // are fast) and returns nullptr without touching error_.
  const std::regex* getCompiled(const std::string& pat, bool& valid) {
    if (!patternCache_) patternCache_ = std::make_shared<PatternCache>();
    auto it = patternCache_->ok.find(pat);
    if (it != patternCache_->ok.end()) { valid = true; return &it->second; }
    if (patternCache_->bad.count(pat) != 0) { valid = false; return nullptr; }
    try {
      auto res = patternCache_->ok.emplace(pat, std::regex(pat));
      valid = true;
      return &res.first->second;
    } catch (const std::regex_error&) {
      patternCache_->bad.insert(pat);
      valid = false;
      return nullptr;
    }
  }

  // Rejects keyword values whose shape is invalid per draft-07 (e.g. a
  // non-array `enum`, a negative `minLength`, a non-numeric `minimum`).
  // Malformed schemas fail validation instead of being silently ignored.
  bool validateKeywordShapes(const asvJSONValue* schema) {
    if (auto t = schemaGet(schema, "type")) {
      bool okShape = t->type == asvJSONValue::STRING;
      if (t->type == asvJSONValue::ARRAY && t->arr) {
        okShape = true;
        for (const auto& e : *t->arr)
          if (e->type != asvJSONValue::STRING) { okShape = false; break; }
      }
      if (!okShape) {
        setError("Invalid schema: 'type' must be a string or an array of strings");
        return false;
      }
    }
    if (auto e = schemaGet(schema, "enum")) {
      if (e->type != asvJSONValue::ARRAY || !e->arr) {
        setError("Invalid schema: 'enum' must be an array");
        return false;
      }
    }
    if (auto r = schemaGet(schema, "required")) {
      if (r->type != asvJSONValue::ARRAY || !r->arr) {
        setError("Invalid schema: 'required' must be an array of strings");
        return false;
      }
      for (const auto& e : *r->arr)
        if (e->type != asvJSONValue::STRING) {
          setError("Invalid schema: 'required' entries must be strings");
          return false;
        }
    }
    if (auto u = schemaGet(schema, "uniqueItems")) {
      if (u->type != asvJSONValue::BOOL_VAL) {
        setError("Invalid schema: 'uniqueItems' must be a boolean");
        return false;
      }
    }
    static const char* const nonNegInts[] = {"minLength", "maxLength", "minItems", "maxItems", "minProperties", "maxProperties"};
    for (const char* k : nonNegInts) {
      if (auto n = schemaGet(schema, k)) {
        if (n->type != asvJSONValue::INT || n->num < 0) {
          setError(std::string("Invalid schema: '") + k + "' must be a non-negative integer");
          return false;
        }
      }
    }
    static const char* const numbers[] = {"minimum", "maximum", "multipleOf"};
    for (const char* k : numbers) {
      if (auto n = schemaGet(schema, k)) {
        if (!isNumberType(n)) {
          setError(std::string("Invalid schema: '") + k + "' must be a number");
          return false;
        }
      }
    }
    // exclusiveMinimum/exclusiveMaximum take the draft-07 numeric form or the
    // draft-04 boolean (legacy) form.
    static const char* const exclusives[] = {"exclusiveMinimum", "exclusiveMaximum"};
    for (const char* k : exclusives) {
      if (auto n = schemaGet(schema, k)) {
        if (n->type != asvJSONValue::BOOL_VAL && !isNumberType(n)) {
          setError(std::string("Invalid schema: '") + k + "' must be a number or boolean");
          return false;
        }
      }
    }
    if (auto m = schemaGet(schema, "multipleOf")) {
      if (isNumberType(m) && getNumberValue(m) <= 0) {
        setError("Invalid schema: 'multipleOf' must be greater than 0");
        return false;
      }
    }
    if (auto p = schemaGet(schema, "pattern")) {
      if (p->type != asvJSONValue::STRING) {
        setError("Invalid schema: 'pattern' must be a string");
        return false;
      }
    }
    // A schema value is either a boolean (true/false schema) or an object.
    auto isSchema = [](const asvJSONValue* v) {
      return v->type == asvJSONValue::OBJECT || v->type == asvJSONValue::BOOL_VAL;
    };
    // allOf / anyOf / oneOf -- non-empty arrays of schemas.
    static const char* const ofArrays[] = {"allOf", "anyOf", "oneOf"};
    for (const char* k : ofArrays) {
      if (auto n = schemaGet(schema, k)) {
        if (n->type != asvJSONValue::ARRAY || !n->arr || n->arr->empty()) {
          setError(std::string("Invalid schema: '") + k + "' must be a non-empty array of schemas");
          return false;
        }
        for (const auto& e : *n->arr)
          if (!isSchema(e.get())) {
            setError(std::string("Invalid schema: '") + k + "' entries must be schemas");
            return false;
          }
      }
    }
    // not / if / then / else / contains / propertyNames -- single schemas.
    static const char* const singleSchemas[] = {"not", "if", "then", "else", "contains", "propertyNames"};
    for (const char* k : singleSchemas) {
      if (auto n = schemaGet(schema, k)) {
        if (!isSchema(n)) {
          setError(std::string("Invalid schema: '") + k + "' must be a schema");
          return false;
        }
      }
    }
    // items -- a schema, or an (possibly empty) array of schemas (tuple form).
    if (auto it = schemaGet(schema, "items")) {
      bool ok = isSchema(it);
      if (it->type == asvJSONValue::ARRAY && it->arr) {
        ok = true;
        for (const auto& e : *it->arr)
          if (!isSchema(e.get())) { ok = false; break; }
      }
      if (!ok) {
        setError("Invalid schema: 'items' must be a schema or an array of schemas");
        return false;
      }
    }
    if (auto ai = schemaGet(schema, "additionalItems")) {
      if (!isSchema(ai)) {
        setError("Invalid schema: 'additionalItems' must be a schema or boolean");
        return false;
      }
    }
    if (auto ap = schemaGet(schema, "additionalProperties")) {
      if (!isSchema(ap)) {
        setError("Invalid schema: 'additionalProperties' must be a schema or boolean");
        return false;
      }
    }
    // properties / patternProperties / definitions -- objects of schemas.
    static const char* const schemaMaps[] = {"properties", "patternProperties", "definitions"};
    for (const char* k : schemaMaps) {
      if (auto n = schemaGet(schema, k)) {
        if (n->type != asvJSONValue::OBJECT) {
          setError(std::string("Invalid schema: '") + k + "' must be an object");
          return false;
        }
      }
    }
    // dependencies -- object; each value is a schema or an array of strings.
    if (auto d = schemaGet(schema, "dependencies")) {
      if (d->type != asvJSONValue::OBJECT) {
        setError("Invalid schema: 'dependencies' must be an object");
        return false;
      }
      if (d->obj) {
        for (const auto& [k, v] : *d->obj) {
          bool ok = isSchema(v.get());
          if (v->type == asvJSONValue::ARRAY && v->arr) {
            ok = true;
            for (const auto& e : *v->arr)
              if (e->type != asvJSONValue::STRING) { ok = false; break; }
          }
          if (!ok) {
            setError("Invalid schema: 'dependencies' entries must be schemas or arrays of strings");
            return false;
          }
        }
      }
    }
    // dependentRequired -- object of arrays of strings.
    if (auto dr = schemaGet(schema, "dependentRequired")) {
      if (dr->type != asvJSONValue::OBJECT) {
        setError("Invalid schema: 'dependentRequired' must be an object");
        return false;
      }
      if (dr->obj) {
        for (const auto& [k, v] : *dr->obj) {
          bool ok = v->type == asvJSONValue::ARRAY && v->arr;
          if (ok) {
            for (const auto& e : *v->arr)
              if (e->type != asvJSONValue::STRING) { ok = false; break; }
          }
          if (!ok) {
            setError("Invalid schema: 'dependentRequired' entries must be arrays of strings");
            return false;
          }
        }
      }
    }
    if (auto f = schemaGet(schema, "format")) {
      if (f->type != asvJSONValue::STRING) {
        setError("Invalid schema: 'format' must be a string");
        return false;
      }
    }
    return true;
  }

  bool validateNode(const asvJSONValue* value, const asvJSONValue* schema) {
    if (isBoolTrue(schema)) return true;
    if (isBoolFalse(schema)) { setError("False schema rejects all values"); return false; }
    if (!schema || schema->type != asvJSONValue::OBJECT) return true;

    if (schemaHas(schema, "$ref")) {
      auto refNode = schemaGet(schema, "$ref");
      if (refNode && refNode->type == asvJSONValue::STRING) {
        auto resolved = resolveRef(refNode->str_data);
        if (!resolved) { setError("Unresolved $ref: " + refNode->str_data); return false; }
        if (*refDepth_ >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) {
          setError("Circular $ref detected at: " + refNode->str_data);
          return false;
        }
        (*refDepth_)++;
        bool ok = validateNode(value, resolved);
        (*refDepth_)--;
        return ok;
      }
    }

    if (!validateKeywordShapes(schema)) return false;

    const asvJSONValue* typeNode = schemaGet(schema, "type");
    if (!schemaPassesTypeCheck(value, typeNode)) {
      std::string expected;
      if (typeNode->type == asvJSONValue::STRING) {
        expected = "'" + std::string(typeNode->str_data) + "'";
      } else if (typeNode->type == asvJSONValue::ARRAY && typeNode->arr) {
        expected = "[";
        for (size_t i = 0; i < typeNode->arr->size(); i++) {
          if (i > 0) expected += ", ";
          expected += "'" + std::string((*typeNode->arr)[i]->str_data) + "'";
        }
        expected += "]";
      }
      setError("Expected type " + expected + ", got '" + valueTypeName(value) + "'");
      return false;
    }

    if (schemaHas(schema, "const")) {
      auto constVal = schemaGet(schema, "const");
      if (!valuesEqual(value, constVal)) {
        setError("Value does not match const");
        return false;
      }
    }

    if (schemaHas(schema, "enum")) {
      auto enumNode = schemaGet(schema, "enum");
      if (enumNode && enumNode->type == asvJSONValue::ARRAY && enumNode->arr) {
        bool found = false;
        for (const auto& ev : *enumNode->arr) {
          if (valuesEqual(value, ev.get())) { found = true; break; }
        }
        if (!found) { setError("Value not in enum"); return false; }
      }
    }

    if (!validateComposition(value, schema)) return false;

    if (!validateString(value, schema)) return false;
    if (!validateNumber(value, schema)) return false;
    if (!validateArray(value, schema)) return false;
    if (!validateObject(value, schema)) return false;

    return true;
  }

  bool validateComposition(const asvJSONValue* value, const asvJSONValue* schema) {
    if (schemaHas(schema, "allOf")) {
      auto node = schemaGet(schema, "allOf");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        for (size_t i = 0; i < node->arr->size(); i++) {
          if (!validateNode(value, (*node->arr)[i].get())) return false;
        }
      }
    }

    if (schemaHas(schema, "anyOf")) {
      auto node = schemaGet(schema, "anyOf");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        for (const auto& sub : *node->arr) {
          SchemaValidator subVal;
          subVal.rootSchema_ = rootSchema_;
          subVal.path_ = path_;
          subVal.refDepth_ = refDepth_;
          subVal.patternCache_ = patternCache_;
          if (subVal.validateNode(value, sub.get())) return true;
        }
        setError("Does not match any schema in anyOf");
        return false;
      }
    }

    if (schemaHas(schema, "oneOf")) {
      auto node = schemaGet(schema, "oneOf");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        int matches = 0;
        for (const auto& sub : *node->arr) {
          SchemaValidator subVal;
          subVal.rootSchema_ = rootSchema_;
          subVal.path_ = path_;
          subVal.refDepth_ = refDepth_;
          subVal.patternCache_ = patternCache_;
          if (subVal.validateNode(value, sub.get())) matches++;
        }
        if (matches != 1) {
          setError(matches == 0 ? "Does not match any schema in oneOf" : "Matches more than one schema in oneOf");
          return false;
        }
      }
    }

    if (schemaHas(schema, "not")) {
      auto node = schemaGet(schema, "not");
      if (node) {
        SchemaValidator subVal;
        subVal.rootSchema_ = rootSchema_;
        subVal.path_ = path_;
        subVal.refDepth_ = refDepth_;
        subVal.patternCache_ = patternCache_;
        if (subVal.validateNode(value, node)) {
          setError("Matches the 'not' schema");
          return false;
        }
      }
    }

    if (schemaHas(schema, "if")) {
      auto ifNode = schemaGet(schema, "if");
      if (ifNode) {
        SchemaValidator ifVal;
        ifVal.rootSchema_ = rootSchema_;
        ifVal.path_ = path_;
        ifVal.refDepth_ = refDepth_;
        ifVal.patternCache_ = patternCache_;
        bool cond = ifVal.validateNode(value, ifNode);
        if (cond && schemaHas(schema, "then")) {
          return validateNode(value, schemaGet(schema, "then"));
        }
        if (!cond && schemaHas(schema, "else")) {
          return validateNode(value, schemaGet(schema, "else"));
        }
      }
    }

    return true;
  }

  bool validateString(const asvJSONValue* value, const asvJSONValue* schema) {
    if (value->type != asvJSONValue::STRING) return true;
    const std::string& s = value->str_data;

    if (schemaHas(schema, "minLength")) {
      auto node = schemaGet(schema, "minLength");
      if (node && node->type == asvJSONValue::INT) {
        int64_t actualLen = utf8Length(s);
        if (actualLen < node->num) {
          setError("String length " + std::to_string(actualLen) + " is less than minLength " + std::to_string(node->num));
          return false;
        }
      }
    }

    if (schemaHas(schema, "maxLength")) {
      auto node = schemaGet(schema, "maxLength");
      if (node && node->type == asvJSONValue::INT) {
        int64_t actualLen = utf8Length(s);
        if (actualLen > node->num) {
          setError("String length " + std::to_string(actualLen) + " is greater than maxLength " + std::to_string(node->num));
          return false;
        }
      }
    }

    if (schemaHas(schema, "pattern")) {
      auto node = schemaGet(schema, "pattern");
      if (node && node->type == asvJSONValue::STRING) {
        bool validPattern = false;
        const std::regex* re = getCompiled(node->str_data, validPattern);
        if (!validPattern) {
          setError("Invalid regex pattern '" + node->str_data + "'");
          return false;
        }
        if (re && !std::regex_search(s, *re)) {
          setError("String does not match pattern '" + node->str_data + "'");
          return false;
        }
      }
    }

    return true;
  }

  bool validateNumber(const asvJSONValue* value, const asvJSONValue* schema) {
    if (!isNumberType(value)) return true;

    // exclusiveMinimum/exclusiveMaximum accept both the draft-07 numeric form
    // and the draft-04 boolean (legacy) form that makes `minimum`/`maximum`
    // strict.
    const asvJSONValue* minNode = schemaGet(schema, "minimum");
    const asvJSONValue* maxNode = schemaGet(schema, "maximum");
    bool strictMin = false;
    bool strictMax = false;
    if (auto exMin = schemaGet(schema, "exclusiveMinimum")) {
      if (exMin->type == asvJSONValue::BOOL_VAL) {
        strictMin = exMin->flag;
      } else if (isNumberType(exMin)) {
        strictMin = true;
        minNode = exMin;
      }
    }
    if (auto exMax = schemaGet(schema, "exclusiveMaximum")) {
      if (exMax->type == asvJSONValue::BOOL_VAL) {
        strictMax = exMax->flag;
      } else if (isNumberType(exMax)) {
        strictMax = true;
        maxNode = exMax;
      }
    }

    if (minNode && isNumberType(minNode) &&
        (strictMin ? compareNumbers(value, minNode) <= 0 : compareNumbers(value, minNode) < 0)) {
      if (strictMin) {
        setError("Value " + std::to_string(getNumberValue(value)) + " is not greater than exclusiveMinimum " + std::to_string(getNumberValue(minNode)));
      } else {
        setError("Value " + std::to_string(getNumberValue(value)) + " is less than minimum " + std::to_string(getNumberValue(minNode)));
      }
      return false;
    }

    if (maxNode && isNumberType(maxNode) &&
        (strictMax ? compareNumbers(value, maxNode) >= 0 : compareNumbers(value, maxNode) > 0)) {
      if (strictMax) {
        setError("Value " + std::to_string(getNumberValue(value)) + " is not less than exclusiveMaximum " + std::to_string(getNumberValue(maxNode)));
      } else {
        setError("Value " + std::to_string(getNumberValue(value)) + " is greater than maximum " + std::to_string(getNumberValue(maxNode)));
      }
      return false;
    }

    if (schemaHas(schema, "multipleOf")) {
      auto node = schemaGet(schema, "multipleOf");
      if (node && isNumberType(node) && getNumberValue(node) > 0) {
        bool isMultiple = false;
        if (value->type == asvJSONValue::INT && node->type == asvJSONValue::INT) {
          // Exact integer arithmetic: no double conversion, so values at the
          // full int64 range are checked precisely.
          isMultiple = (value->num % node->num == 0);
        } else {
          double val = getNumberValue(value);
          double mof = getNumberValue(node);
          double remainder = std::fmod(val, mof);
          double tolerance = std::fabs(mof) * 1e-9;
          if (tolerance < 1e-12) tolerance = 1e-12;
          isMultiple = std::fabs(remainder) <= tolerance || std::fabs(remainder - mof) <= tolerance;
        }
        if (!isMultiple) {
          setError("Value " + std::to_string(getNumberValue(value)) + " is not a multiple of " + std::to_string(getNumberValue(node)));
          return false;
        }
      }
    }

    return true;
  }

  bool validateArray(const asvJSONValue* value, const asvJSONValue* schema) {
    if (value->type != asvJSONValue::ARRAY) return true;
    const auto& items = value->arr;
    size_t count = items ? items->size() : 0;

    if (schemaHas(schema, "minItems")) {
      auto node = schemaGet(schema, "minItems");
      if (node && node->type == asvJSONValue::INT && count < static_cast<size_t>(node->num)) {
        setError("Array has " + std::to_string(count) + " items, minItems is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "maxItems")) {
      auto node = schemaGet(schema, "maxItems");
      if (node && node->type == asvJSONValue::INT && count > static_cast<size_t>(node->num)) {
        setError("Array has " + std::to_string(count) + " items, maxItems is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "uniqueItems")) {
      auto node = schemaGet(schema, "uniqueItems");
      if (node && node->type == asvJSONValue::BOOL_VAL && node->flag) {
        // Fast path: arrays of scalars use a hash set; containers fall back to
        // the O(n^2) deep-compare below.
        bool allScalars = true;
        for (size_t i = 0; i < count; i++) {
          auto t = (*items)[i]->type;
          if (t != asvJSONValue::STRING && t != asvJSONValue::INT &&
              t != asvJSONValue::DOUBLE && t != asvJSONValue::BOOL_VAL &&
              t != asvJSONValue::NULL_VAL) { allScalars = false; break; }
          if (t == asvJSONValue::DOUBLE && (*items)[i]->dbl != (*items)[i]->dbl) { allScalars = false; break; } // NaN: keep deep compare
        }
        if (allScalars) {
          std::unordered_map<std::string, size_t> first;
          first.reserve(count);
          for (size_t i = 0; i < count; i++) {
            std::string token;
            const auto& nodePtr = (*items)[i];
            switch (nodePtr->type) {
              case asvJSONValue::NULL_VAL: token = "n"; break;
              case asvJSONValue::BOOL_VAL: token = nodePtr->flag ? "b1" : "b0"; break;
              case asvJSONValue::INT: token = "i" + std::to_string(nodePtr->num); break;
              case asvJSONValue::DOUBLE: {
                double d = nodePtr->dbl;
                if (d == 0.0) { token = "i0"; break; } // normalizes +0.0 / -0.0
                if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
                  int64_t id = static_cast<int64_t>(d);
                  if (static_cast<double>(id) == d) { token = "i" + std::to_string(id); break; } // exactly integral: same value type as INT
                }
                uint64_t bits = 0;
                std::memcpy(&bits, &d, sizeof(bits));
                token = "d" + std::to_string(bits);
                break;
              }
              case asvJSONValue::STRING:
                token.reserve(nodePtr->str_data.size() + 1);
                token += 's';
                token += nodePtr->str_data;
                break;
              default: break;
            }
            auto found = first.find(token);
            if (found != first.end()) {
              setError("Array items at indices " + std::to_string(found->second) + " and " + std::to_string(i) + " are not unique");
              return false;
            }
            first.emplace(std::move(token), i);
          }
        } else {
          for (size_t i = 0; i < count; i++) {
            for (size_t j = i + 1; j < count; j++) {
              if (valuesEqual((*items)[i].get(), (*items)[j].get())) {
                setError("Array items at indices " + std::to_string(i) + " and " + std::to_string(j) + " are not unique");
                return false;
              }
            }
          }
        }
      }
    }

    if (schemaHas(schema, "contains")) {
      auto node = schemaGet(schema, "contains");
      if (node && items) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
          SchemaValidator subVal;
          subVal.rootSchema_ = rootSchema_;
          subVal.path_ = path_ + "/" + std::to_string(i);
          subVal.refDepth_ = refDepth_;
          subVal.patternCache_ = patternCache_;
          if (subVal.validateNode((*items)[i].get(), node)) { found = true; break; }
        }
        if (!found) {
          setError("Array does not contain a matching item for 'contains' schema");
          return false;
        }
      }
    }

    const asvJSONValue* itemsSchema = schemaGet(schema, "items");
    if (items && itemsSchema) {
      if (itemsSchema->type == asvJSONValue::ARRAY && itemsSchema->arr) {
        for (size_t i = 0; i < count; i++) {
          const asvJSONValue* itemSchema = nullptr;
          if (i < itemsSchema->arr->size()) {
            itemSchema = (*itemsSchema->arr)[i].get();
          } else if (schemaHas(schema, "additionalItems")) {
            itemSchema = schemaGet(schema, "additionalItems");
            if (isBoolFalse(itemSchema)) {
              std::string savedPath = path_;
              pushIndexPath(i);
              setError("Additional items not allowed at index " + std::to_string(i));
              path_ = savedPath;
              return false;
            }
          }
          if (itemSchema && !isBoolTrue(itemSchema)) {
            std::string oldPath = path_;
            pushIndexPath(i);
            if (!validateNode((*items)[i].get(), itemSchema)) { path_ = oldPath; return false; }
            path_ = oldPath;
          }
        }
      } else {
        for (size_t i = 0; i < count; i++) {
          std::string oldPath = path_;
          pushIndexPath(i);
          if (!validateNode((*items)[i].get(), itemsSchema)) { path_ = oldPath; return false; }
          path_ = oldPath;
        }
      }
    }

    return true;
  }

  bool validateObject(const asvJSONValue* value, const asvJSONValue* schema) {
    if (value->type != asvJSONValue::OBJECT) return true;
    const auto& obj = value->obj;
    size_t propCount = obj ? obj->size() : 0;

    if (schemaHas(schema, "minProperties")) {
      auto node = schemaGet(schema, "minProperties");
      if (node && node->type == asvJSONValue::INT && propCount < static_cast<size_t>(node->num)) {
        setError("Object has " + std::to_string(propCount) + " properties, minProperties is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "maxProperties")) {
      auto node = schemaGet(schema, "maxProperties");
      if (node && node->type == asvJSONValue::INT && propCount > static_cast<size_t>(node->num)) {
        setError("Object has " + std::to_string(propCount) + " properties, maxProperties is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "required")) {
      auto node = schemaGet(schema, "required");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        for (const auto& req : *node->arr) {
          if (req->type == asvJSONValue::STRING) {
            if (!obj || obj->find(req->str_data) == obj->end()) {
              setError("Missing required property '" + req->str_data + "'");
              return false;
            }
          }
        }
      }
    }

    if (obj) {
      const asvJSONValue* propsSchema = schemaGet(schema, "properties");
      const asvJSONValue* patternPropsSchema = schemaGet(schema, "patternProperties");
      const asvJSONValue* additionalPropsSchema = schemaGet(schema, "additionalProperties");

      std::set<std::string> matchedByPattern;

      if (patternPropsSchema && patternPropsSchema->type == asvJSONValue::OBJECT && patternPropsSchema->obj) {
        struct CompiledPattern {
          std::regex re;
          const asvJSONValue* schema;
        };
        std::vector<CompiledPattern> compiledPatterns;
        compiledPatterns.reserve(patternPropsSchema->obj->size());
        for (auto& patKV : *patternPropsSchema->obj) {
          bool validPattern = false;
          const std::regex* re = getCompiled(patKV.first, validPattern);
          if (!validPattern) {
            setError("Invalid regex pattern in patternProperties: '" + patKV.first + "'");
            return false;
          }
          compiledPatterns.push_back({*re, patKV.second.get()});
        }
        for (auto& kv : *obj) {
          for (const auto& cp : compiledPatterns) {
            if (std::regex_search(kv.first, cp.re)) {
              matchedByPattern.insert(kv.first);
              std::string oldPath = path_;
              pushPath(kv.first);
              if (!validateNode(kv.second.get(), cp.schema)) { path_ = oldPath; return false; }
              path_ = oldPath;
            }
          }
        }
      }

      if (propsSchema && propsSchema->type == asvJSONValue::OBJECT && propsSchema->obj) {
        for (auto& kv : *obj) {
          auto it = propsSchema->obj->find(kv.first);
          if (it != propsSchema->obj->end()) {
            std::string oldPath = path_;
            pushPath(kv.first);
            if (!validateNode(kv.second.get(), it->second.get())) { path_ = oldPath; return false; }
            path_ = oldPath;
          }
        }
      }

      if (additionalPropsSchema) {
        if (isBoolFalse(additionalPropsSchema)) {
          for (auto& kv : *obj) {
            bool known = matchedByPattern.count(kv.first) > 0;
            if (!known && propsSchema && propsSchema->type == asvJSONValue::OBJECT && propsSchema->obj) {
              known = propsSchema->obj->count(kv.first) > 0;
            }
            if (!known) {
              setError("Additional property '" + kv.first + "' not allowed");
              return false;
            }
          }
        } else if (additionalPropsSchema->type == asvJSONValue::OBJECT) {
          for (auto& kv : *obj) {
            bool known = matchedByPattern.count(kv.first) > 0;
            if (!known && propsSchema && propsSchema->type == asvJSONValue::OBJECT && propsSchema->obj) {
              known = propsSchema->obj->count(kv.first) > 0;
            }
            if (!known) {
              std::string oldPath = path_;
              pushPath(kv.first);
              if (!validateNode(kv.second.get(), additionalPropsSchema)) { path_ = oldPath; return false; }
              path_ = oldPath;
            }
          }
        }
      }

      if (schemaHas(schema, "propertyNames")) {
        auto node = schemaGet(schema, "propertyNames");
        if (node && obj) {
          for (auto& kv : *obj) {
            asvJSONValue nameVal;
            nameVal.type = asvJSONValue::STRING;
            nameVal.str_data = kv.first;
            if (!validateNode(&nameVal, node)) {
              setError("Property name '" + kv.first + "' is invalid");
              return false;
            }
          }
        }
      }

      if (schemaHas(schema, "dependentRequired")) {
        auto node = schemaGet(schema, "dependentRequired");
        if (node && node->type == asvJSONValue::OBJECT && node->obj && obj) {
          for (auto& depKV : *node->obj) {
            if (obj->count(depKV.first) > 0 && depKV.second->type == asvJSONValue::ARRAY && depKV.second->arr) {
              for (const auto& req : *depKV.second->arr) {
                if (req->type == asvJSONValue::STRING && obj->count(req->str_data) == 0) {
                  setError("Property '" + depKV.first + "' requires property '" + req->str_data + "'");
                  return false;
                }
              }
            }
          }
        }
      }

      if (schemaHas(schema, "dependencies")) {
        auto node = schemaGet(schema, "dependencies");
        if (node && node->type == asvJSONValue::OBJECT && node->obj && obj) {
          for (auto& depKV : *node->obj) {
            if (obj->count(depKV.first) > 0) {
              if (depKV.second->type == asvJSONValue::ARRAY && depKV.second->arr) {
                for (const auto& req : *depKV.second->arr) {
                  if (req->type == asvJSONValue::STRING && obj->count(req->str_data) == 0) {
                    setError("Property '" + depKV.first + "' requires property '" + req->str_data + "'");
                    return false;
                  }
                }
              } else if (depKV.second->type == asvJSONValue::OBJECT) {
                if (!validateNode(value, depKV.second.get())) return false;
              }
            }
          }
        }
      }
    }

    return true;
  }
};

inline bool asvJSON::validate(const asvJSON& schema, std::string& error) const {
  SchemaValidator v;
  return v.validate(root.get(), schema.root.get(), error);
}

inline bool asvJSON::validate(const asvJSON& schema) const {
  SchemaValidator v;
  return v.validate(root.get(), schema.root.get());
}

} // namespace asvJSONInternal
