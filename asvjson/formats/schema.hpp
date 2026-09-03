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
#include <regex>
#include <set>

namespace asvJSONInternal {

class SchemaValidator {
public:
  bool validate(const asvJSONValue* value, const asvJSONValue* schema) {
    error_.clear();
    path_ = "";
    if (!schema) return true;
    rootSchema_ = schema;
    return validateNode(value, schema);
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
  // Tracks how deeply nested $ref resolution currently is for the value being
  // validated. A self-/mutually-referential schema (e.g. `{"$ref":"#"}` or
  // `"a":{"$ref":"#/definitions/a"}`) would otherwise recurse forever through
  // validateNode and overflow the stack. Cap at MAX_NESTING_DEPTH like the
  // parsers do; a legitimate recursive schema follows the document's finite
  // depth, so a chain deeper than MAX_NESTING_DEPTH is necessarily a loop.
  int refDepth_ = 0;

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

  static bool isSchema(const asvJSONValue* s) {
    return !s || (s->type == asvJSONValue::OBJECT) || isBoolTrue(s) || isBoolFalse(s);
  }

  const asvJSONValue* resolveRef(std::string_view ref) const {
    if (ref.size() < 2 || ref[0] != '#') return nullptr;
    ref.remove_prefix(1);
    if (ref.empty()) return rootSchema_;
    if (ref[0] != '/') return nullptr;
    ref.remove_prefix(1);
    const asvJSONValue* cur = rootSchema_;
    while (!ref.empty()) {
      auto slash = ref.find('/');
      std::string_view seg = (slash == std::string_view::npos) ? ref : ref.substr(0, slash);
      if (!cur || cur->type != asvJSONValue::OBJECT || !cur->obj) return nullptr;
      // Decode JSON Pointer segment (~0 -> ~, ~1 -> /)
      std::string key;
      key.reserve(seg.size());
      for (size_t i = 0; i < seg.size(); i++) {
        if (seg[i] == '~' && i + 1 < seg.size()) {
          if (seg[i + 1] == '0') { key += '~'; i++; }
          else if (seg[i + 1] == '1') { key += '/'; i++; }
          else key += seg[i];
        } else {
          key += seg[i];
        }
      }
      auto it = cur->obj->find(key);
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
        double da = (a->type == asvJSONValue::INT) ? static_cast<double>(a->num) : a->dbl;
        double db = (b->type == asvJSONValue::INT) ? static_cast<double>(b->num) : b->dbl;
        return da == db;
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
      case asvJSONValue::DOUBLE: return t == "number";
      case asvJSONValue::STRING: return t == "string";
      case asvJSONValue::ARRAY: return t == "array";
      case asvJSONValue::OBJECT: return t == "object";
      default: return false;
    }
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
        if (refDepth_ >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) {
          setError("Circular $ref detected at: " + refNode->str_data);
          return false;
        }
        refDepth_++;
        bool ok = validateNode(value, resolved);
        refDepth_--;
        return ok;
      }
    }

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
        int64_t minLen = node->num;
        int64_t actualLen = 0;
        for (size_t i = 0; i < s.size(); ) {
          auto c = static_cast<unsigned char>(s[i]);
          if (c < 0x80) { actualLen++; i++; }
          else if (c < 0xC0) { i++; }
          else if (c < 0xE0) { actualLen++; i += 2; }
          else if (c < 0xF0) { actualLen++; i += 3; }
          else { actualLen++; i += 4; }
        }
        if (actualLen < minLen) {
          setError("String length " + std::to_string(actualLen) + " is less than minLength " + std::to_string(minLen));
          return false;
        }
      }
    }

    if (schemaHas(schema, "maxLength")) {
      auto node = schemaGet(schema, "maxLength");
      if (node && node->type == asvJSONValue::INT) {
        int64_t maxLen = node->num;
        int64_t actualLen = 0;
        for (size_t i = 0; i < s.size(); ) {
          auto c = static_cast<unsigned char>(s[i]);
          if (c < 0x80) { actualLen++; i++; }
          else if (c < 0xC0) { i++; }
          else if (c < 0xE0) { actualLen++; i += 2; }
          else if (c < 0xF0) { actualLen++; i += 3; }
          else { actualLen++; i += 4; }
        }
        if (actualLen > maxLen) {
          setError("String length " + std::to_string(actualLen) + " is greater than maxLength " + std::to_string(maxLen));
          return false;
        }
      }
    }

    if (schemaHas(schema, "pattern")) {
      auto node = schemaGet(schema, "pattern");
      if (node && node->type == asvJSONValue::STRING) {
        try {
          std::regex re(node->str_data);
          if (!std::regex_search(s, re)) {
            setError("String does not match pattern '" + node->str_data + "'");
            return false;
          }
        } catch (const std::regex_error&) {
          setError("Invalid regex pattern '" + node->str_data + "'");
          return false;
        }
      }
    }

    return true;
  }

  bool validateNumber(const asvJSONValue* value, const asvJSONValue* schema) {
    if (!isNumberType(value)) return true;
    double val = getNumberValue(value);

    if (schemaHas(schema, "minimum")) {
      auto node = schemaGet(schema, "minimum");
      if (node && isNumberType(node)) {
        double min = getNumberValue(node);
        if (val < min) {
          setError("Value " + std::to_string(val) + " is less than minimum " + std::to_string(min));
          return false;
        }
      }
    }

    if (schemaHas(schema, "maximum")) {
      auto node = schemaGet(schema, "maximum");
      if (node && isNumberType(node)) {
        double max = getNumberValue(node);
        if (val > max) {
          setError("Value " + std::to_string(val) + " is greater than maximum " + std::to_string(max));
          return false;
        }
      }
    }

    if (schemaHas(schema, "exclusiveMinimum")) {
      auto node = schemaGet(schema, "exclusiveMinimum");
      if (node && isNumberType(node)) {
        double min = getNumberValue(node);
        if (val <= min) {
          setError("Value " + std::to_string(val) + " is not greater than exclusiveMinimum " + std::to_string(min));
          return false;
        }
      }
    }

    if (schemaHas(schema, "exclusiveMaximum")) {
      auto node = schemaGet(schema, "exclusiveMaximum");
      if (node && isNumberType(node)) {
        double max = getNumberValue(node);
        if (val >= max) {
          setError("Value " + std::to_string(val) + " is not less than exclusiveMaximum " + std::to_string(max));
          return false;
        }
      }
    }

    if (schemaHas(schema, "multipleOf")) {
      auto node = schemaGet(schema, "multipleOf");
      if (node && isNumberType(node)) {
        double mof = getNumberValue(node);
        if (mof <= 0) {
          setError("multipleOf must be greater than 0");
          return false;
        }
        double remainder = std::fmod(val, mof);
        if (std::fabs(remainder) > 1e-9 && std::fabs(remainder - mof) > 1e-9) {
          setError("Value " + std::to_string(val) + " is not a multiple of " + std::to_string(mof));
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

    if (schemaHas(schema, "contains")) {
      auto node = schemaGet(schema, "contains");
      if (node && items) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
          SchemaValidator subVal;
          subVal.rootSchema_ = rootSchema_;
          subVal.path_ = path_ + "/" + std::to_string(i);
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
              setError("Additional items not allowed at index " + std::to_string(i));
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
          try {
            compiledPatterns.push_back({std::regex(patKV.first), patKV.second.get()});
          } catch (const std::regex_error&) {
            setError("Invalid regex pattern in patternProperties: '" + patKV.first + "'");
            return false;
          }
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
