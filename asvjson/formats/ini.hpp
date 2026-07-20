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

// Find first unescaped ; or # in a bare value (pos of inline comment, or npos)
static size_t iniFindComment(std::string_view s) {
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\') { i++; continue; }
    if (s[i] == ';' || s[i] == '#') return i;
  }
  return std::string_view::npos;
}

// Unescape INI value
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

// Navigate or create nested path in the object tree
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
      out += "null";
      break;
    default:
      break;
  }
}

// Try to infer a typed value from a parsed INI string.
// Quoted values always remain strings; bare values may be bool/int/double.
static std::unique_ptr<asvJSONValue> iniParseVal(const std::string& s, bool quoted) {
  if (quoted) {
    return asvJSONValue::makeString(s.data(), s.size());
  }
  // Case-insensitive bool
  if (s == "true" || s == "True" || s == "TRUE") return asvJSONValue::makeBool(true);
  if (s == "false" || s == "False" || s == "FALSE") return asvJSONValue::makeBool(false);
  // Try int (base 10 — no octal surprises)
  if (!s.empty()) {
    char* end = nullptr;
    long long ll = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == 0) return asvJSONValue::makeInt(static_cast<int64_t>(ll));
    // Try double
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == 0) return asvJSONValue::makeDouble(d);
  }
  return asvJSONValue::makeString(s.data(), s.size());
}

// Insert a value into an object by key, converting duplicate keys to arrays
static void iniSetValue(asvJSONValue* target, const std::string& key, std::unique_ptr<asvJSONValue> val) {
  if (!target || target->type != asvJSONValue::OBJECT || !target->obj) return;
  auto it = target->obj->find(key);
  if (it == target->obj->end()) {
    target->obj->emplace(key, std::move(val));
  } else {
    // Convert to array if not already
    if (it->second->type == asvJSONValue::ARRAY) {
      it->second->arr->push_back(std::move(val));
    } else {
      auto arr = asvJSONValue::makeArray();
      arr->arr->push_back(std::move(it->second));
      arr->arr->push_back(std::move(val));
      it->second = std::move(arr);
    }
  }
}

// Recursively emit object contents for a given section path (prefix).
// prefix == current section path; emitHeader controls whether to emit a section header.
static void iniEmitObject(const asvJSONValue* obj, std::string& out, const std::string& prefix,
                          bool emitHeader) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) return;

  // Check if this level has direct (non-object) keys
  bool hasDirectKeys = false;
  for (const auto& [key, val] : *obj->obj) {
    if (val->type != asvJSONValue::OBJECT) {
      hasDirectKeys = true; break;
    }
  }

  if (emitHeader && hasDirectKeys && !prefix.empty()) {
    if (!out.empty() && out.back() != '\n') out += '\n';
    out += '[' + prefix + "]\n";
  }

  for (const auto& [key, val] : *obj->obj) {
    if (val->type == asvJSONValue::OBJECT) {
      std::string newPrefix = prefix.empty() ? key : prefix + "." + key;
      bool subHasDirect = false;
      if (val->obj) {
        for (const auto& [sk, sv] : *val->obj) {
          if (sv->type != asvJSONValue::OBJECT) { subHasDirect = true; break; }
        }
      }
      iniEmitObject(val.get(), out, newPrefix, subHasDirect || !val->obj || val->obj->empty());
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      bool allObjects = true;
      for (const auto& elem : *val->arr) {
        if (elem->type != asvJSONValue::OBJECT) { allObjects = false; break; }
      }
      if (allObjects && !val->arr->empty()) {
        for (const auto& elem : *val->arr) {
          std::string newPrefix = prefix.empty() ? key : prefix + "." + key;
          if (!out.empty() && out.back() != '\n') out += '\n';
          out += "[[" + newPrefix + "]]\n";
          if (elem->type == asvJSONValue::OBJECT) {
            iniEmitObject(elem.get(), out, newPrefix, false);
          }
        }
      } else {
        for (const auto& elem : *val->arr) {
          out += key + " = ";
          iniFormatVal(elem.get(), out);
          out += '\n';
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
    if (obj) {
      // First pass: non-object, non-array keys
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
          bool hasDirect = false;
          if (val->obj) {
            for (const auto& [sk, sv] : *val->obj) {
              if (sv->type != T::OBJECT) { hasDirect = true; break; }
            }
          }
          iniEmitObject(val.get(), out, key, hasDirect || !val->obj || val->obj->empty());
        } else if (val->type == T::ARRAY && val->arr) {
          bool allObjects = true;
          for (const auto& elem : *val->arr) {
            if (elem->type != T::OBJECT) { allObjects = false; break; }
          }
          if (allObjects && !val->arr->empty()) {
            for (const auto& elem : *val->arr) {
              if (!out.empty() && out.back() != '\n') out += '\n';
              out += "[[" + key + "]]\n";
              if (elem->type == T::OBJECT) {
                iniEmitObject(elem.get(), out, key, false);
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
    std::vector<std::string> sectionPath;
    asvJSONValue* currentArrayElement = nullptr;
    bool inSection = false;

    for (size_t lineNum = 0; lineNum < lines.size(); lineNum++) {
      std::string rawLine = lines[lineNum];
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
        rawLine = std::move(continued);
        line = rawLine;
      }

      // Comments: ; or # at line start (after trim)
      if (line[0] == ';' || line[0] == '#') continue;

      // Section header [section] or [[section]]
      if (line[0] == '[') {
        size_t end = line.find(']');
        if (end == std::string_view::npos) throw asvJSONError("unclosed section bracket");

        bool isArraySection = false;
        size_t start = 1;
        size_t nameLen = end - start;

        if (line[1] == '[' && end + 1 < line.size() && line[end + 1] == ']') {
          isArraySection = true;
          start = 2;
          nameLen = end - start;
        }

        std::string_view sectionName = iniTrim(line.substr(start, nameLen));
        if (sectionName.empty()) throw asvJSONError("empty section name");

        // Split section name on dots for nesting, trimming each segment
        std::vector<std::string> newPath;
        size_t dotStart = 0;
        for (size_t i = 0; i <= sectionName.size(); i++) {
          if (i == sectionName.size() || sectionName[i] == '.') {
            newPath.push_back(std::string(iniTrim(sectionName.substr(dotStart, i - dotStart))));
            dotStart = i + 1;
          }
        }

        if (isArraySection) {
          std::string arrKey = newPath.back();
          newPath.pop_back();
          asvJSONValue* parent = root.get();
          if (!newPath.empty()) {
            parent = iniNavigate(root.get(), newPath, true);
          }
          if (parent && parent->type == asvJSONValue::OBJECT) {
            auto it = parent->obj->find(arrKey);
            if (it == parent->obj->end() || it->second->type != asvJSONValue::ARRAY) {
              auto newArr = asvJSONValue::makeArray();
              auto* arrPtr = newArr.get();
              parent->obj->insert_or_assign(arrKey, std::move(newArr));
              auto newObj = asvJSONValue::makeObject();
              currentArrayElement = newObj.get();
              arrPtr->arr->push_back(std::move(newObj));
            } else {
              asvJSONValue* arrVal = it->second.get();
              auto newObj = asvJSONValue::makeObject();
              currentArrayElement = newObj.get();
              arrVal->arr->push_back(std::move(newObj));
            }
          }
          inSection = false;
        } else {
          sectionPath = std::move(newPath);
          currentArrayElement = nullptr;
          inSection = true;
        }
        continue;
      }

      // Key-value pair
      size_t eqPos = std::string_view::npos;
      size_t colPos = std::string_view::npos;

      for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '=') { eqPos = i; break; }
        if (line[i] == ':') { colPos = i; break; }
      }

      size_t delim = eqPos != std::string_view::npos ? eqPos :
                     colPos != std::string_view::npos ? colPos : std::string_view::npos;

      if (delim == std::string_view::npos) {
        for (size_t i = 0; i < line.size(); i++) {
          if (line[i] == ' ' || line[i] == '\t') {
            delim = i;
            break;
          }
        }
        if (delim == std::string_view::npos) continue;
      }

      std::string_view keyView = iniTrim(line.substr(0, delim));
      std::string_view valView = iniTrim(line.substr(delim + 1));
      if (keyView.empty()) continue;
      std::string key(keyView);

      std::string valueStr;
      bool quoted = false;

      if (!valView.empty() && valView.front() == '"') {
        quoted = true;
        // Find closing quote
        size_t closeQuote = std::string_view::npos;
        bool escape = false;
        for (size_t i = 1; i < valView.size(); i++) {
          if (escape) { escape = false; continue; }
          if (valView[i] == '\\') { escape = true; continue; }
          if (valView[i] == '"') { closeQuote = i; break; }
        }
        if (closeQuote != std::string_view::npos) {
          valueStr = iniUnescape(valView.substr(1, closeQuote - 1));
        } else {
          // Multi-line quoted value: gather lines until closing quote
          std::string accum(valView.substr(1));
          lineNum++;
          bool found = false;
          while (lineNum < lines.size()) {
            std::string_view nextLine = lines[lineNum];
            // Strip trailing \r from CRLF line endings
            if (!nextLine.empty() && nextLine.back() == '\r') {
              nextLine.remove_suffix(1);
            }
            accum += '\n';
            escape = false;
            closeQuote = std::string_view::npos;
            for (size_t i = 0; i < nextLine.size(); i++) {
              if (escape) { escape = false; continue; }
              if (nextLine[i] == '\\') { escape = true; continue; }
              if (nextLine[i] == '"') { closeQuote = i; found = true; break; }
            }
            if (found) {
              accum += nextLine.substr(0, closeQuote);
              break;
            }
            accum += nextLine;
            lineNum++;
          }
          if (!found) throw asvJSONError("unclosed quoted value");
          valueStr = iniUnescape(accum);
        }
      } else {
        // Bare value: strip inline comments
        size_t commentPos = iniFindComment(valView);
        if (commentPos != std::string_view::npos) {
          valView = valView.substr(0, commentPos);
        }
        valView = iniTrim(valView);
        valueStr = iniUnescape(valView);
      }

      // Determine target
      asvJSONValue* target = nullptr;
      if (currentArrayElement) {
        target = currentArrayElement;
      } else if (inSection && !sectionPath.empty()) {
        target = iniNavigate(root.get(), sectionPath, true);
      } else {
        target = root.get();
      }

      if (target) {
        iniSetValue(target, key, iniParseVal(valueStr, quoted));
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
