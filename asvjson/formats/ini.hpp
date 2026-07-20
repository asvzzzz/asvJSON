#pragma once
// INI serialization/parsing for asvJSON++
// Conforms to the standard INI format with dot-notation section nesting.

#include "../core.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace asvJSONInternal {

// -- Helpers ----------------------------------------------------------------

static std::string_view iniTrim(std::string_view s) {
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// Unescape INI value (handles sequences like \n, \t, \\, \;, \#, \=, \:)
static std::string iniUnescape(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      switch (s[i + 1]) {
        case 'n': out += '\n'; i++; break;
        case 't': out += '\t'; i++; break;
        case 'r': out += '\r'; i++; break;
        case '0': out += '\0'; i++; break;
        case 'a': out += '\a'; i++; break;
        case 'b': out += '\b'; i++; break;
        case '\\': out += '\\'; i++; break;
        case '\"': out += '\"'; i++; break;
        case '\'': out += '\''; i++; break;
        case ';': out += ';'; i++; break;
        case '#': out += '#'; i++; break;
        case '=': out += '='; i++; break;
        case ':': out += ':'; i++; break;
        default: out += s[i]; break;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

// Escape an INI value for output
static std::string iniEscape(std::string_view s) {
  std::string out;
  for (auto c : s) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case '\\': out += "\\\\"; break;
      case ';': out += "\\;"; break;
      case '#': out += "\\#"; break;
      case '=': out += "\\="; break;
      case ':': out += "\\:"; break;
      default: out += c;
    }
  }
  return out;
}

// Navigate or create nested path in the object tree.
// pathParts is a vector of key names (already split on dots).
static asvJSONValue* iniNavigate(asvJSONValue* root, const std::vector<std::string>& pathParts, bool create) {
  asvJSONValue* cur = root;
  for (const auto& seg : pathParts) {
    if (!cur || cur->type != asvJSONValue::OBJECT || !cur->obj) {
      if (!create) return nullptr;
      throw asvJSONError("cannot navigate through non-object");
    }
    auto it = cur->obj->find(seg);
    if (it == cur->obj->end()) {
      if (!create) return nullptr;
      auto newObj = asvJSONValue::makeObject();
      auto* raw = newObj.get();
      cur->obj->emplace(seg, std::move(newObj));
      cur = raw;
    } else {
      cur = it->second.get();
    }
  }
  return cur;
}

// Format a value as INI text
static void iniFormatVal(const asvJSONValue* v, std::string& out) {
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::STRING:
      {
        std::string_view sv(v->str_data);
        bool needsQuotes = false;
        for (auto c : sv) {
          if (c == ' ' || c == '\t' || c == ';' || c == '#' || c == '=' || c == ':' || c == '"' || c == '\'' || c == '\\' || c == '\n' || c == '\r') {
            needsQuotes = true;
            break;
          }
        }
        if (needsQuotes) {
          out += '"';
          out += iniEscape(sv);
          out += '"';
        } else {
          out += sv;
        }
      }
      break;
    case T::INT:
      out += std::to_string(v->num);
      break;
    case T::DOUBLE:
      out += std::to_string(v->dbl);
      break;
    case T::BOOL_VAL:
      out += v->flag ? "true" : "false";
      break;
    case T::NULL_VAL:
      break;
    default:
      break;
  }
}

// Recursively emit object contents for a given section path (prefix).
// prefix empty means root level (no section header).
static void iniEmitObject(const asvJSONValue* obj, std::string& out, const std::string& prefix) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) return;
  for (const auto& [key, val] : *obj->obj) {
    if (val->type == asvJSONValue::OBJECT) {
      std::string newPrefix = prefix.empty() ? key : prefix + "." + key;
      // Check if this sub-object has any direct keys (not just sub-objects)
      bool hasDirectKeys = false;
      if (val->obj) {
        for (const auto& [sk, sv] : *val->obj) {
          if (sv->type != asvJSONValue::OBJECT) { hasDirectKeys = true; break; }
        }
      }
      // Emit section header only if this level has direct keys or is a leaf
      if (hasDirectKeys || !val->obj || val->obj->empty()) {
        if (!out.empty() && out.back() != '\n') out += '\n';
        out += '[' + newPrefix + "]\n";
      }
      iniEmitObject(val.get(), out, newPrefix);
    } else if (val->type == asvJSONValue::ARRAY) {
      // Arrays of objects: use [[key]] for each element
      if (val->arr) {
        bool allObjects = true;
        for (const auto& elem : *val->arr) {
          if (elem->type != asvJSONValue::OBJECT) { allObjects = false; break; }
        }
        if (allObjects && !val->arr->empty()) {
          for (const auto& elem : *val->arr) {
            std::string newPrefix = prefix.empty() ? key : prefix + "." + key;
            if (!out.empty() && out.back() != '\n') out += '\n';
            out += "[" + newPrefix + "]\n";
            if (elem->type == asvJSONValue::OBJECT) {
              iniEmitObject(elem.get(), out, newPrefix);
            }
          }
        } else {
          // Arrays of primitives: serialize as key=value, one per line
          for (const auto& elem : *val->arr) {
            out += key + " = ";
            iniFormatVal(elem.get(), out);
            out += '\n';
          }
        }
      }
    } else {
      out += key + " = ";
      iniFormatVal(val.get(), out);
      out += '\n';
    }
  }
}

// -- Encoder ----------------------------------------------------------------

inline void asvJSONValue::toINI(std::string& out) const {
  using T = asvJSONValue::Type;
  if (type == T::OBJECT) {
    // Root-level object: emit global keys first, then sub-objects as sections
    // First pass: non-object keys
    if (obj) {
      for (const auto& [key, val] : *obj) {
        if (val->type != T::OBJECT && val->type != T::ARRAY) {
          out += key + " = ";
          iniFormatVal(val.get(), out);
          out += '\n';
        }
      }
      // Second pass: object/array keys
      for (const auto& [key, val] : *obj) {
        if (val->type == T::OBJECT) {
          bool hasDirectKeys = false;
          if (val->obj) {
            for (const auto& [sk, sv] : *val->obj) {
              if (sv->type != T::OBJECT) { hasDirectKeys = true; break; }
            }
          }
          if (hasDirectKeys || !val->obj || val->obj->empty()) {
            if (!out.empty() && out.back() != '\n') out += '\n';
            out += '[' + key + "]\n";
          }
          iniEmitObject(val.get(), out, key);
        } else if (val->type == T::ARRAY && val->arr) {
          bool allObjects = true;
          for (const auto& elem : *val->arr) {
            if (elem->type != T::OBJECT) { allObjects = false; break; }
          }
          if (allObjects && !val->arr->empty()) {
            for (const auto& elem : *val->arr) {
              if (!out.empty() && out.back() != '\n') out += '\n';
              out += '[' + key + "]\n";
              if (elem->type == T::OBJECT) {
                iniEmitObject(elem.get(), out, key);
              }
            }
          } else {
            for (const auto& elem : *val->arr) {
              out += key + " = ";
              iniFormatVal(elem.get(), out);
              out += '\n';
            }
          }
        }
      }
    }
  } else if (type == T::ARRAY && arr) {
    out += "value = ";
    iniFormatVal(this, out);
    out += '\n';
  } else {
    out += "value = ";
    iniFormatVal(this, out);
    out += '\n';
  }
}

// -- Decoder ----------------------------------------------------------------

inline bool asvJSON::fromINI(std::string_view input) {
  try {
    if (input.empty()) {
      root = asvJSONValue::makeObject();
      return true;
    }

    root = asvJSONValue::makeObject();

    auto lines = splitLines(input);
    std::vector<std::string> sectionPath;  // current section path (empty = root)
    bool inSection = false;

    for (size_t lineNum = 0; lineNum < lines.size(); lineNum++) {
      std::string_view rawLine = lines[lineNum];
      std::string_view line = iniTrim(rawLine);
      if (line.empty()) continue;

      // Line continuation: if line ends with \, join with next line
      if (line.back() == '\\') {
        std::string continued(line.substr(0, line.size() - 1));
        lineNum++;
        while (lineNum < lines.size()) {
          std::string_view nextLine = iniTrim(lines[lineNum]);
          if (nextLine.empty()) { lineNum++; continue; }
          if (nextLine.back() == '\\') {
            continued += std::string(nextLine.substr(0, nextLine.size() - 1));
            lineNum++;
          } else {
            continued += std::string(nextLine);
            break;
          }
        }
        line = continued;
      }

      // Comments: ; or # at line start (after trim)
      if (line[0] == ';' || line[0] == '#') continue;

      // Section header
      if (line[0] == '[') {
        size_t end = line.rfind(']');
        if (end == std::string_view::npos) throw asvJSONError("unclosed section bracket");
        std::string_view sectionName = iniTrim(line.substr(1, end - 1));
        if (sectionName.empty()) throw asvJSONError("empty section name");

        // Split section name on dots for nesting
        sectionPath.clear();
        size_t dotStart = 0;
        for (size_t i = 0; i <= sectionName.size(); i++) {
          if (i == sectionName.size() || sectionName[i] == '.') {
            sectionPath.push_back(std::string(sectionName.substr(dotStart, i - dotStart)));
            dotStart = i + 1;
          }
        }
        inSection = true;
        continue;
      }

      // Key-value pair
      size_t eqPos = std::string_view::npos;
      size_t colPos = std::string_view::npos;

      for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '=') { eqPos = i; break; }
        if (line[i] == ':') { colPos = i; break; }
      }

      // Fallback: first space as delimiter
      size_t delim = eqPos != std::string_view::npos ? eqPos :
                     colPos != std::string_view::npos ? colPos : std::string_view::npos;

      if (delim == std::string_view::npos) {
        // Try first space as key/value delimiter
        for (size_t i = 0; i < line.size(); i++) {
          if (line[i] == ' ' || line[i] == '\t') {
            delim = i;
            break;
          }
        }
        if (delim == std::string_view::npos) continue;  // no delimiter, skip
      }

      std::string_view keyView = iniTrim(line.substr(0, delim));
      std::string_view valView = iniTrim(line.substr(delim + 1));

      if (keyView.empty()) continue;

      std::string key(keyView);

      // Parse value: if quoted, unescape; otherwise bare
      std::string valueStr;
      if (valView.size() >= 2 && valView.front() == '"' && valView.back() == '"') {
        valueStr = iniUnescape(valView.substr(1, valView.size() - 2));
      } else {
        valueStr = iniUnescape(valView);
      }

      // Set value in tree
      asvJSONValue* target;
      if (inSection && !sectionPath.empty()) {
        target = iniNavigate(root.get(), sectionPath, true);
      } else {
        target = root.get();
      }

      if (target) {
        auto val = asvJSONValue::makeString(valueStr.data(), valueStr.size());
        if (val) target->obj->insert_or_assign(key, std::move(val));
      }
    }

    return true;

  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
