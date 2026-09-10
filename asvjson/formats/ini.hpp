#pragma once
// INI serialization/parsing for asvJSON++
// Conforms to the standard INI format with dot-notation section nesting.

#include "../core.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace asvJSONInternal {

// -- Helpers ----------------------------------------------------------------

inline std::string_view iniTrim(std::string_view s) {
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// Find first unescaped ; or # in a bare value (pos of inline comment, or npos)
inline size_t iniFindComment(std::string_view s) {
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\') { i++; continue; }
    if (s[i] == ';' || s[i] == '#') return i;
  }
  return std::string_view::npos;
}

// Unescape INI value
inline std::string iniUnescape(std::string_view s) {
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
inline std::string iniEscape(std::string_view s) {
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

// Escape a key for use inside a section header [...] (brackets/backslash)
inline std::string iniEscapeSectionKey(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (auto c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '[': out += "\\["; break;
      case ']': out += "\\]"; break;
      default: out += c;
    }
  }
  return out;
}

// Unescape a section path segment read from a header
inline std::string iniUnescapeSectionKey(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      out += s[i + 1];
      i++;
    } else {
      out += s[i];
    }
  }
  return out;
}

// Navigate or create nested path in the object tree
inline asvJSONValue* iniNavigate(asvJSONValue* root, const std::vector<std::string>& pathParts, bool create) {
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
  // The final segment must also be an object: a section header (or dotted key)
  // targeting an existing scalar or array would otherwise silently drop every
  // following key-value pair.
  if (!cur || cur->type != asvJSONValue::OBJECT)
    throw asvJSONError("cannot navigate through non-object");
  return cur;
}

// Try to infer a typed value from a parsed INI string.
// Quoted values always remain strings; bare values may be null/bool/int/double.
inline std::unique_ptr<asvJSONValue> iniParseVal(const std::string& s, bool quoted);

// Format a value as INI text
inline void iniFormatVal(const asvJSONValue* v, std::string& out) {
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
        // A bare text that would decode as a different type (bool/int/double/null)
        // must be quoted so the round-trip preserves the STRING type. raw_number
        // literals (e.g. overflowed integers) stay bare so they re-decode via the
        // overflow path.
        if (!needsQuotes && !v->raw_number) {
          auto bare = iniParseVal(std::string(sv), false);
          if (bare && bare->type != T::STRING) needsQuotes = true;
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
      fmtDoubleVal(v->dbl, out);
      break;
    case T::BOOL_VAL:
      out += v->flag ? "true" : "false";
      break;
    case T::NULL_VAL:
      out += "null";
      break;
    case T::OBJECT:
      out += "{}";
      break;
    case T::ARRAY:
      out += "[]";
      break;
    default:
      break;
  }
}

// Try to infer a typed value from a parsed INI string.
// Quoted values always remain strings; bare values may be null/bool/int/double.
inline std::unique_ptr<asvJSONValue> iniParseVal(const std::string& s, bool quoted) {
  if (quoted) {
    return asvJSONValue::makeString(s.data(), s.size());
  }
  // Bare "null" decodes to the null type (encoder emits the literal, and a
  // STRING "null" is quoted so it stays text -- see iniFormatVal).
  if (s == "null") return asvJSONValue::makeNull();
  // Bare "{}" / "[]" represent empty containers (the encoder emits them for
  // object/array values that have no children).
  if (s == "{}") return asvJSONValue::makeObject();
  if (s == "[]") return asvJSONValue::makeArray();
  // Case-insensitive bool
  if (s == "true" || s == "True" || s == "TRUE") return asvJSONValue::makeBool(true);
  if (s == "false" || s == "False" || s == "FALSE") return asvJSONValue::makeBool(false);
  // Try int (base 10 -- no octal surprises)
  if (!s.empty()) {
    // JSON numbers never carry leading zeros ("042", "-042", "+042"), so those
    // literals stay STRINGs (otherwise "zip = 042" silently becomes 42 on a
    // re-parse). "0", "-0", "0.5", "-0.5" still parse as numbers.
    size_t i0 = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (s.size() > i0 + 1 && s[i0] == '0' && s[i0 + 1] >= '0' && s[i0 + 1] <= '9')
      return asvJSONValue::makeString(s.data(), s.size());
    errno = 0;
    char* end = nullptr;
    long long ll = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == 0 && end == s.c_str() + s.size()) {
      if (errno == ERANGE) {
        // Out-of-range integer: preserve the exact literal so the round-trip is
        // lossless (same convention as core JSON and UDE) instead of silently
        // clamping to LLONG_MAX.
        auto v = asvJSONValue::makeStringView(std::string_view(s));
        if (v) v->raw_number = true;
        return v;
      }
      return asvJSONValue::makeInt(static_cast<int64_t>(ll));
    }
    // Try double
    errno = 0;
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == 0 && end == s.c_str() + s.size()) {
      if (errno == ERANGE) {
        // Out-of-range double (e.g. "1e999"): preserve the literal as text
        // instead of silently degrading it to +inf/-inf/0 on the round-trip.
        return asvJSONValue::makeStringView(std::string_view(s));
      }
      return asvJSONValue::makeDouble(d);
    }
  }
  return asvJSONValue::makeString(s.data(), s.size());
}

// Insert a value into an object by key, converting duplicate keys to arrays
inline void iniSetValue(asvJSONValue* target, const std::string& key, std::unique_ptr<asvJSONValue> val) {
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

// Encoder classification of one child value: returns true when the child is
// emitted as bare `key = value` lines (scalar, primitive array, empty
// container), false when it becomes a [[...]] table (array of objects) or a
// dotted section (non-empty object). Arrays that cannot be represented at all
// (mixed object/scalar contents, or nested array elements) throw.
inline bool iniIsBareChild(const asvJSONValue* v) {
  if (v->type == asvJSONValue::ARRAY && v->arr) {
    if (v->arr->empty()) return true;
    bool hasObj = false, hasPrim = false;
    for (const auto& e : *v->arr) {
      if (e->type == asvJSONValue::ARRAY)
        throw asvJSONError("nested array cannot be represented in INI");
      if (e->type == asvJSONValue::OBJECT) hasObj = true;
      else hasPrim = true;
    }
    if (hasObj && hasPrim)
      throw asvJSONError("mixed array cannot be represented in INI");
    return !hasObj; // all primitives -> repeated key = value lines
  }
  if (v->type == asvJSONValue::OBJECT)
    return !v->obj || v->obj->empty();
  return true;
}

// Recursively emit object contents for a given section path (prefix).
// prefix == current section path; emitHeader controls whether to emit a section header.
inline void iniEmitElement(const asvJSONValue* elem, std::string& out, const std::string& prefix);
inline void iniEmitObject(const asvJSONValue* obj, std::string& out, const std::string& prefix,
                           bool emitHeader, bool pretty = true) {
  if (!obj || obj->type != asvJSONValue::OBJECT || !obj->obj) return;

  // Check if this level has direct (bare-line-producing) keys
  bool hasDirectKeys = false;
  for (const auto& [key, val] : *obj->obj) {
    if (iniIsBareChild(val.get())) {
      hasDirectKeys = true; break;
    }
  }

  if (emitHeader && hasDirectKeys && !prefix.empty()) {
    if (!out.empty() && out.back() != '\n') out += '\n';
    out += '[' + prefix + "]\n";
  }

  // Pass 1: bare-line producers (scalars, primitive arrays, empty containers).
  // These MUST precede [[...]] table headers so the decoder does not assign
  // them to a currentArrayElement opened by a preceding table.
  for (const auto& [key, val] : *obj->obj) {
    std::string dottedKey = prefix.empty() ? key : prefix + "." + key;
    if (!key.empty() && key[0] == '[')
      throw asvJSONError("cannot represent key starting with '[' in INI");
    if (val->type == asvJSONValue::OBJECT) {
      if (val->obj && val->obj->empty()) {
        out += (pretty ? key : dottedKey) + " = {}\n";
      }
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      if (val->arr->empty()) {
        out += (pretty ? key : dottedKey) + " = []\n";
      } else if (iniIsBareChild(val.get())) {
        for (const auto& elem : *val->arr) {
          out += (pretty ? key : dottedKey) + " = ";
          iniFormatVal(elem.get(), out);
          out += '\n';
        }
      }
    } else {
      out += (pretty ? key : dottedKey) + " = ";
      iniFormatVal(val.get(), out);
      out += '\n';
    }
  }

  // Pass 2: objects and object-arrays (produce section headers / [[...]] tables).
  for (const auto& [key, val] : *obj->obj) {
    if (val->type == asvJSONValue::OBJECT) {
      std::string escKey = iniEscapeSectionKey(key);
      std::string newPrefix = prefix.empty() ? escKey : prefix + "." + escKey;
      bool subHasDirect = false;
      if (val->obj) {
        for (const auto& [sk, sv] : *val->obj) {
          if (iniIsBareChild(sv.get())) { subHasDirect = true; break; }
        }
      }
      iniEmitObject(val.get(), out, newPrefix, subHasDirect || !val->obj || val->obj->empty(), pretty);
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      bool isObj = true;
      for (const auto& elem : *val->arr) {
        if (elem->type != asvJSONValue::OBJECT) { isObj = false; break; }
      }
      if (isObj && !val->arr->empty()) {
        std::string escKey = iniEscapeSectionKey(key);
        for (const auto& elem : *val->arr) {
          std::string newPrefix = prefix.empty() ? escKey : prefix + "." + escKey;
          if (!out.empty() && out.back() != '\n') out += '\n';
          out += "[[" + newPrefix + "]]\n";
          if (elem->type == asvJSONValue::OBJECT) {
            iniEmitElement(elem.get(), out, newPrefix);
          }
        }
      }
    }
  }
}

// Emit the contents of one array-of-objects element (under a [[...]] header).
// Direct scalar / primitive-array keys go bare within the current element;
// nested objects are flattened with dotted keys and nested arrays-of-objects
// reuse [[full.path]] headers. Section headers must NOT be used here -- a
// "[x]" header after "[[items]]" would resolve at document scope, not inside
// the current element (the old encoder emitted e.g. "[items.host]" which
// failed to re-parse). prefix is the escaped full path of the array itself.

// Flatten object descendants of an array element into dotted leaf keys; nested
// arrays-of-objects become [[prefix.dotted]] headers resolved back into the
// element by the array-aware decoder.
inline void iniEmitElementFlat(const asvJSONValue* obj, const std::string& dk, std::string& out, const std::string& prefix) {
  // Pass 1: bare-line producers
  for (const auto& [key, val] : *obj->obj) {
    std::string full = dk.empty() ? key : dk + "." + key;
    if (!key.empty() && key[0] == '[')
      throw asvJSONError("cannot represent key starting with '[' in INI");
    if (val->type == asvJSONValue::OBJECT) {
      if (val->obj && val->obj->empty()) {
        out += full + " = {}\n";
      }
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      if (val->arr->empty()) {
        out += full + " = []\n";
      } else if (iniIsBareChild(val.get())) {
        for (const auto& e : *val->arr) {
          out += full + " = ";
          iniFormatVal(e.get(), out);
          out += '\n';
        }
      }
    } else {
      out += full + " = ";
      iniFormatVal(val.get(), out);
      out += '\n';
    }
  }
  // Pass 2: objects and object-arrays
  for (const auto& [key, val] : *obj->obj) {
    std::string full = dk.empty() ? key : dk + "." + key;
    if (val->type == asvJSONValue::OBJECT && val->obj) {
      iniEmitElementFlat(val.get(), full, out, prefix);
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      bool allObjects = true;
      for (const auto& e : *val->arr) if (e->type != asvJSONValue::OBJECT) { allObjects = false; break; }
      if (allObjects && !val->arr->empty()) {
        for (const auto& e : *val->arr) {
          std::string np = prefix.empty() ? full : prefix + "." + full;
          if (!out.empty() && out.back() != '\n') out += '\n';
          out += "[[" + np + "]]\n";
          iniEmitElement(e.get(), out, np);
        }
      }
    }
  }
}

inline void iniEmitElement(const asvJSONValue* elem, std::string& out, const std::string& prefix) {
  if (!elem || elem->type != asvJSONValue::OBJECT || !elem->obj) return;
  // Pass 1: bare-line producers
  for (const auto& [key, val] : *elem->obj) {
    if (!key.empty() && key[0] == '[')
      throw asvJSONError("cannot represent key starting with '[' in INI");
    if (val->type == asvJSONValue::OBJECT) {
      if (val->obj && val->obj->empty()) {
        out += key + " = {}\n";
      }
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      if (val->arr->empty()) {
        out += key + " = []\n";
      } else if (iniIsBareChild(val.get())) {
        for (const auto& e : *val->arr) {
          out += key + " = ";
          iniFormatVal(e.get(), out);
          out += '\n';
        }
      }
    } else {
      out += key + " = ";
      iniFormatVal(val.get(), out);
      out += '\n';
    }
  }
  // Pass 2: objects and object-arrays
  for (const auto& [key, val] : *elem->obj) {
    if (val->type == asvJSONValue::OBJECT && val->obj) {
      iniEmitElementFlat(val.get(), key, out, prefix);
    } else if (val->type == asvJSONValue::ARRAY && val->arr) {
      bool allObjects = true;
      for (const auto& e : *val->arr) if (e->type != asvJSONValue::OBJECT) { allObjects = false; break; }
      if (allObjects && !val->arr->empty()) {
        for (const auto& e : *val->arr) {
          std::string np = prefix.empty() ? key : prefix + "." + key;
          if (!out.empty() && out.back() != '\n') out += '\n';
          out += "[[" + np + "]]\n";
          iniEmitElement(e.get(), out, np);
        }
      }
    }
  }
}

// -- Encoder ----------------------------------------------------------------

inline void asvJSONValue::toINI(std::string& out, bool pretty) const {
  using T = asvJSONValue::Type;
  if (type == T::OBJECT) {
    if (obj) {
      if (!pretty) {
        // Compact: dotted keys for scalars/objects, [[...]] headers for
        // arrays-of-objects.  Bare lines must precede tables.
        for (const auto& [key, val] : *obj) {
          if (!key.empty() && key[0] == '[')
            throw asvJSONError("cannot represent key starting with '[' in INI");
          if (val->type == T::OBJECT) {
            if (val->obj && val->obj->empty()) {
              out += key + " = {}\n";
            }
          } else if (val->type == T::ARRAY && val->arr) {
            if (val->arr->empty()) {
              out += key + " = []\n";
            } else if (iniIsBareChild(val.get())) {
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
        // Pass 2: objects and object-arrays
        for (const auto& [key, val] : *obj) {
          if (val->type == T::OBJECT) {
            iniEmitObject(val.get(), out, iniEscapeSectionKey(key), false, false);
          } else if (val->type == T::ARRAY && val->arr) {
            bool allObjects = true;
            for (const auto& elem : *val->arr)
              if (elem->type != T::OBJECT) { allObjects = false; break; }
            if (allObjects && !val->arr->empty()) {
              std::string escKey = iniEscapeSectionKey(key);
              for (const auto& elem : *val->arr) {
                if (!out.empty() && out.back() != '\n') out += '\n';
                out += "[[" + escKey + "]]\n";
                if (elem->type == T::OBJECT)
                  iniEmitElement(elem.get(), out, escKey);
              }
            }
          }
        }
      } else {
        // Pretty: first pass -- bare-line producers
        for (const auto& [key, val] : *obj) {
          if (!key.empty() && key[0] == '[')
            throw asvJSONError("cannot represent key starting with '[' in INI");
          if (val->type == T::OBJECT) {
            if (val->obj && val->obj->empty()) {
              out += key + " = {}\n";
            }
          } else if (val->type == T::ARRAY && val->arr) {
            if (val->arr->empty()) {
              out += key + " = []\n";
            } else if (iniIsBareChild(val.get())) {
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
        // Second pass: objects and object-arrays
        for (const auto& [key, val] : *obj) {
          if (val->type == T::OBJECT) {
            bool hasDirect = false;
            if (val->obj) {
              for (const auto& [sk, sv] : *val->obj) {
                if (iniIsBareChild(sv.get())) { hasDirect = true; break; }
              }
            }
            iniEmitObject(val.get(), out, iniEscapeSectionKey(key), hasDirect || !val->obj || val->obj->empty(), true);
          } else if (val->type == T::ARRAY && val->arr) {
            bool allObjects = true;
            for (const auto& elem : *val->arr) {
              if (elem->type != T::OBJECT) { allObjects = false; break; }
            }
            if (allObjects && !val->arr->empty()) {
              std::string escKey = iniEscapeSectionKey(key);
              for (const auto& elem : *val->arr) {
                if (!out.empty() && out.back() != '\n') out += '\n';
                out += "[[" + escKey + "]]\n";
                if (elem->type == T::OBJECT) {
                  iniEmitElement(elem.get(), out, escKey);
                }
              }
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
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    if (input.empty()) {
      root = asvJSONValue::makeObject();
      return true;
    }

    // Strip UTF-8 BOM (EF BB BF) if present (mirrors the JSON/CSV parsers).
    if (input.size() >= 3 && (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
      input = input.substr(3);

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
        // Find the first unescaped closing bracket (keys may contain \] and \[)
        auto findCloseBracket = [&](size_t from) -> size_t {
          size_t i = from;
          while (i < line.size()) {
            if (line[i] == '\\' && i + 1 < line.size()) { i += 2; continue; }
            if (line[i] == ']') return i;
            i++;
          }
          return std::string_view::npos;
        };

        bool isArraySection = (line.size() > 1 && line[1] == '[');
        size_t start = isArraySection ? 2 : 1;
        size_t end = findCloseBracket(start);
        if (end == std::string_view::npos) throw asvJSONError("unclosed section bracket");
        if (isArraySection) {
          if (end + 1 >= line.size() || line[end + 1] != ']') throw asvJSONError("malformed array section header");
        }
        // Only whitespace and an optional ;/# comment may follow the header;
        // anything else is dropped silently today and would corrupt the layout.
        {
          size_t t = end + (isArraySection ? 2 : 1);
          while (t < line.size() && (line[t] == ' ' || line[t] == '\t')) t++;
          if (t < line.size() && line[t] != ';' && line[t] != '#')
            throw asvJSONError("unexpected content after section header");
        }
        size_t nameLen = end - start;

        std::string_view sectionName = iniTrim(line.substr(start, nameLen));
        if (sectionName.empty()) throw asvJSONError("empty section name");

        // Split section name on dots for nesting, trimming each segment
        std::vector<std::string> newPath;
        size_t dotStart = 0;
        for (size_t i = 0; i <= sectionName.size(); i++) {
          if (i == sectionName.size() || sectionName[i] == '.') {
            newPath.push_back(iniUnescapeSectionKey(iniTrim(sectionName.substr(dotStart, i - dotStart))));
            dotStart = i + 1;
          }
        }

        if (isArraySection) {
          std::string arrKey = newPath.back();
          newPath.pop_back();
          asvJSONValue* parent = root.get();
          // Walk the enclosing path. When a path node is an array (e.g. "items"
          // in "[[items.host]]"), descend into its last element: the new table
          // array belongs inside that element, not at document scope (the old
          // code silently dropped such headers entirely).
          auto descendArrays = [&]() -> void {
            while (parent && parent->type == asvJSONValue::ARRAY && parent->arr && !parent->arr->empty())
              parent = parent->arr->back().get();
          };
          descendArrays();
          for (const auto& seg : newPath) {
            if (!parent || parent->type != asvJSONValue::OBJECT)
              throw asvJSONError("cannot navigate through non-object");
            auto it = parent->obj->find(seg);
            if (it == parent->obj->end()) {
              auto newObj = asvJSONValue::makeObject();
              auto* raw = newObj.get();
              parent->obj->emplace(seg, std::move(newObj));
              parent = raw;
            } else {
              parent = it->second.get();
            }
            descendArrays();
          }
          if (!parent || parent->type != asvJSONValue::OBJECT)
            throw asvJSONError("cannot navigate through non-object");
          auto it = parent->obj->find(arrKey);
          if (it == parent->obj->end()) {
            auto newArr = asvJSONValue::makeArray();
            auto* arrPtr = newArr.get();
            parent->obj->emplace(arrKey, std::move(newArr));
            auto newObj = asvJSONValue::makeObject();
            currentArrayElement = newObj.get();
            arrPtr->arr->push_back(std::move(newObj));
          } else if (it->second->type == asvJSONValue::ARRAY) {
            auto newObj = asvJSONValue::makeObject();
            currentArrayElement = newObj.get();
            it->second->arr->push_back(std::move(newObj));
          } else {
            // Reject instead of silently overwriting the existing value.
            throw asvJSONError("array section conflicts with existing value for \"" + arrKey + "\"");
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
          // Only whitespace and an optional ;/# comment may follow the closing
          // quote; anything else is rejected instead of being silently dropped
          // (a "k = \"a\"b" line would otherwise lose the "b").
          size_t t = closeQuote + 1;
          while (t < valView.size() && (valView[t] == ' ' || valView[t] == '\t')) t++;
          if (t < valView.size() && valView[t] != ';' && valView[t] != '#')
            throw asvJSONError("unexpected content after quoted value");
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
        // Split dotted keys into nested objects (e.g. "general.name" -> {"general":{"name":...}})
        size_t dotPos = key.find('.');
        if (dotPos != std::string::npos) {
          std::vector<std::string> keyPath;
          size_t start = 0;
          for (size_t i = 0; i <= key.size(); i++) {
            if (i == key.size() || key[i] == '.') {
              keyPath.push_back(key.substr(start, i - start));
              start = i + 1;
            }
          }
          std::string leafKey = keyPath.back();
          keyPath.pop_back();
          asvJSONValue* parent = iniNavigate(target, keyPath, true);
          if (parent) {
            iniSetValue(parent, leafKey, iniParseVal(valueStr, quoted));
          }
        } else {
          iniSetValue(target, key, iniParseVal(valueStr, quoted));
        }
      }
    }

    formatType = FormatType::INI;
    return true;

  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
