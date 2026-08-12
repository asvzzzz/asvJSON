#pragma once
// TOON (Token-Oriented Object Notation) serialization/parsing for asvJSON++
//
// Compact JSON-like format using 2-space indentation instead of braces:
//   key: value            -- object member (value may be a bare token or JSON)
//   - item                -- array element
//   key:                  -- nested object / array (children indented)
//   key[N]{"f1","f2"}:    -- tabular array; subsequent indented lines are rows
// Strings are quoted only when required; full round-trip via toTOON()/fromTOON().

#include "../core.hpp"

namespace asvJSONInternal {

inline std::string toonJsonEscape(const std::string& s) {
  std::string r;
  r.reserve(s.size() + 4);
  appendJsonEscaped(r, s);
  return r;
}

inline bool toonIsJsonValue(std::string_view s) {
  if (s.empty()) return false;
  if (s.front() == '"' || s.front() == '{' || s.front() == '[') return true;
  if (s.front() == '-' || (s.front() >= '0' && s.front() <= '9')) {
    // Validate full number — reject e.g. "1.12.0" (two dots)
    bool hasDot = false, hasExp = false;
    for (size_t i = (s.front() == '-' || s.front() == '+') ? 1 : 0; i < s.size(); i++) {
      if (s[i] >= '0' && s[i] <= '9') continue;
      if (s[i] == '.') { if (hasDot || hasExp) return false; hasDot = true; continue; }
      if (s[i] == 'e' || s[i] == 'E') { if (hasExp) return false; hasExp = true; continue; }
      if ((s[i] == '+' || s[i] == '-') && hasExp && i + 1 < s.size() && s[i-1] != '.' && (i == 0 || (s[i-1] != '+' && s[i-1] != '-'))) continue;
      return false;
    }
    return true;
  }
  if (s == "true" || s == "false" || s == "null") return true;
  return false;
}

inline bool toonValNeedsQuotes(const std::string& s) {
  if (s.empty()) return true;
  if (s.front() == '"') return true;
  for (char c : s) if (c == ',' || c == '\n' || c == '\r') return true;
  if (s.front() == ' ' || s.back() == ' ') return true;
  if (s == "null" || s == "true" || s == "false") return true;
  return false;
}

inline std::string toonQuoteVal(const std::string& s) {
  if (toonValNeedsQuotes(s)) return '"' + toonJsonEscape(s) + '"';
  return s;
}

inline std::string toonLeafVal(const asvJSONValue* v) {
  if (!v) return "null";
  switch (v->type) {
    case asvJSONValue::NULL_VAL: return "null";
    case asvJSONValue::STRING: return toonQuoteVal(v->str_data);
    case asvJSONValue::INT: return std::to_string(v->num);
    case asvJSONValue::BOOL_VAL: return v->flag ? "true" : "false";
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d) || std::isinf(d)) return "null";
      std::string r; fmtDoubleVal(d, r); return r;
    }
    case asvJSONValue::OBJECT: {
      std::string r = "{";
      bool first = true;
      for (const auto& [k, child] : *(v->obj)) {
        if (!first) r += ",";
        first = false;
        r += toonQuoteVal(k) + ":" + toonLeafVal(child.get());
      }
      r += "}";
      return r;
    }
    case asvJSONValue::ARRAY: {
      std::string r = "[";
      for (size_t i = 0; i < v->size(); i++) {
        auto* elem = v->get(i);
        if (!elem) continue;
        if (i > 0) r += ",";
        r += toonLeafVal(elem);
      }
      r += "]";
      return r;
    }
    default: return "null";
  }
}

inline void valToToon(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth = 0) {
  if (!v) return;
  if (!asvJSONValue::checkNestingDepth(depth)) return;
  std::string pad(static_cast<size_t>(indent) * 2, ' ');
  if (v->type == asvJSONValue::OBJECT) {
    if (v->obj->empty()) {
      if (!key.empty()) out += pad + toonQuoteVal(key) + ": {}\n";
      else out += pad + "{}\n";
      return;
    }
    if (!key.empty()) out += pad + toonQuoteVal(key) + ":\n";
    int childIndent = indent + (key.empty() ? 0 : 1);
    for (const auto& [k, child] : *(v->obj))
      valToToon(child.get(), out, childIndent, k, depth + 1);
  } else if (v->type == asvJSONValue::ARRAY) {
    if (v->size() == 0) {
      if (!key.empty()) out += pad + toonQuoteVal(key) + ": []\n";
      else out += "[]\n";
      return;
    }
    if (key.empty()) {
      for (size_t i = 0; i < v->size(); i++) {
        auto* child = v->get(i);
        if (!child) continue;
        if (child->type == asvJSONValue::OBJECT) {
          out += pad + "-\n";
          for (const auto& [k, sub] : *(child->obj))
            valToToon(sub.get(), out, indent + 1, k);
        } else if (child->type == asvJSONValue::ARRAY) {
          valToToon(child, out, indent, "");
        } else {
          out += pad + "- " + toonLeafVal(child) + "\n";
        }
      }
    } else {
      out += pad + toonQuoteVal(key) + ": []\n";
      for (size_t i = 0; i < v->size(); i++) {
        auto* child = v->get(i);
        if (!child) continue;
        if (child->type == asvJSONValue::OBJECT) {
          out += pad + "  -\n";
          for (const auto& [k, sub] : *(child->obj))
            valToToon(sub.get(), out, indent + 2, k);
        } else {
          out += pad + "  - " + toonLeafVal(child) + "\n";
        }
      }
    }
  } else {
    if (!key.empty())
      out += pad + toonQuoteVal(key) + ": " + toonLeafVal(v) + "\n";
    else
      out += pad + toonLeafVal(v) + "\n";
  }
}

// Wraps a bare TOON string value in JSON quotes if not already a valid JSON value
inline std::string toonJsonQuoteBare(std::string_view s) {
  if (toonIsJsonValue(s)) return std::string(s);
  return '"' + toonJsonEscape(std::string(s)) + '"';
}

// TOON value splitter - splits by comma respecting quoted strings and escapes
inline std::vector<std::string> toonSplitCommas(std::string_view s) {
  std::vector<std::string> result;
  std::string cur;
  bool inQuotes = false;
  bool escape = false;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (escape) { cur += c; escape = false; continue; }
    if (c == '\\' && inQuotes) { cur += c; escape = true; continue; }
    if (c == '"') { cur += c; inQuotes = !inQuotes; continue; }
    if (c == ',' && !inQuotes) {
      result.push_back(std::move(cur));
      cur.clear();
      continue;
    }
    cur += c;
  }
  result.push_back(std::move(cur));
  return result;
}

// TOON -> JSON text converter
inline std::string toonToJson(std::string_view input) {
  auto lines = splitLines(input);
  if (lines.empty()) return "{}";

  size_t firstNonEmpty = 0;
  while (firstNonEmpty < lines.size() && lines[firstNonEmpty].empty()) firstNonEmpty++;
  if (firstNonEmpty >= lines.size()) return "{}";

  std::string_view firstContent = stripIndent(lines[firstNonEmpty]);
  bool rootIsArr = (firstContent.size() >= 2 && firstContent[0] == '[') || (firstContent.size() >= 1 && firstContent[0] == '-');

  std::string out;
  std::vector<FormatFrame> stack;
  int rootIndent = countIndent(lines[firstNonEmpty]);

  out = rootIsArr ? '[' : '{';
	stack.push_back({rootIsArr ? 'A' : 'O', true, rootIndent, true, true});

  for (size_t li = firstNonEmpty; li < lines.size(); li++) {
    auto& line = lines[li];
    if (line.empty()) continue;
    int indent = countIndent(line);
    std::string_view content = stripIndent(line);

    closeFrames(stack, out, indent);
    if (stack.empty()) break;
    auto& curFrame = stack.back();

    // check for array header [n]{fields}:
    if (content.size() >= 2 && content[0] == '[') {
      size_t closeB = content.find(']');
      if (closeB != std::string_view::npos) {
        size_t afterB = closeB + 1;
        bool hasFields = afterB < content.size() && content[afterB] == '{';
        std::vector<std::string> fields;
        if (hasFields) {
          size_t closeBr = content.find('}', afterB);
          if (closeBr != std::string_view::npos) {
            std::string_view fc = content.substr(afterB + 1, closeBr - afterB - 1);
            auto fieldParts = toonSplitCommas(fc);
            for (auto& f : fieldParts) {
              size_t fs = 0; while (fs < f.size() && f[fs] == ' ') fs++;
              size_t fe = f.size(); while (fe > fs && f[fe-1] == ' ') fe--;
              fields.push_back(f.substr(fs, fe - fs));
            }
            afterB = closeBr + 1;
          }
        }
        if (afterB < content.size() && content[afterB] == ':') {
    addComma(stack, out);
    if (!stack.empty()) stack.back().first = false;
    out += '[';
    stack.push_back({'A', true, indent, true, false});
          size_t afterColon = afterB + 1;
          while (afterColon < content.size() && content[afterColon] == ' ') afterColon++;
          if (hasFields) {
            // tabular - read subsequent indented lines as rows
            for (size_t rl = li + 1; rl < lines.size(); rl++) {
              auto& rline = lines[rl];
              if (rline.empty()) continue;
              int rindent = countIndent(rline);
              if (rindent <= indent) break;
              std::string_view rcont = stripIndent(rline);
              auto vals = toonSplitCommas(rcont);
              if (!stack.empty() && !stack.back().first) out += ',';
              if (!stack.empty()) stack.back().first = false;
              out += '{';
              for (size_t fi = 0; fi < fields.size() && fi < vals.size(); fi++) {
                if (fi > 0) out += ',';
                out += '"' + toonJsonEscape(fields[fi]) + "\":" + toonJsonQuoteBare(vals[fi]);
              }
              out += '}';
            }
            out += ']';
            if (!stack.empty()) stack.pop_back();
            continue;
          } else if (afterColon < content.size()) {
            // inline array items
            std::string_view inlineContent = content.substr(afterColon);
            auto inlineVals = toonSplitCommas(inlineContent);
            for (auto& v : inlineVals) {
              size_t vs = 0; while (vs < v.size() && v[vs] == ' ') vs++;
              size_t ve = v.size(); while (ve > vs && v[ve-1] == ' ') ve--;
              if (!stack.empty() && !stack.back().first) out += ',';
              if (!stack.empty()) stack.back().first = false;
              out += toonJsonQuoteBare(v.substr(vs, ve - vs));
            }
            // after inline items, read list items from subsequent lines
            for (size_t rl = li + 1; rl < lines.size(); rl++) {
              auto& rline = lines[rl];
              if (rline.empty()) continue;
              int rindent = countIndent(rline);
              if (rindent <= indent) break;
              std::string_view rcont = stripIndent(rline);
              if (rcont.size() >= 2 && rcont[0] == '-' && rcont[1] == ' ') {
                out += ',';
                std::string vv(rcont.substr(2));
                size_t vs = 0; while (vs < vv.size() && vv[vs] == ' ') vs++;
                size_t ve = vv.size(); while (ve > vs && vv[ve-1] == ' ') ve--;
                vv = vv.substr(vs, ve - vs);
                out += toonJsonQuoteBare(vv);
              }
            }
            out += ']';
            if (!stack.empty()) stack.pop_back();
            continue;
          }
          continue;
        }
      }
    }

    // list item: - value
    if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
      addComma(stack, out);
      curFrame.first = false;
      std::string_view val = content.substr(2);
      out += toonJsonQuoteBare(val);
      continue;
    }
    // bare "-" means an inline object list item
    if (content == "-") {
      addComma(stack, out);
      curFrame.first = false;
      out += '{';
      stack.push_back({'O', true, indent, true, false});
      continue;
    }

    // key: value
    size_t colonPos = content.find(':');
    if (colonPos != std::string_view::npos) {
      std::string_view key = content.substr(0, colonPos);
      while (!key.empty() && key.back() == ' ') key = key.substr(0, key.size() - 1);

      std::string keyStr;
      if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
        keyStr = toonJsonEscape(std::string(key.substr(1, key.size() - 2)));
      } else {
        keyStr = toonJsonEscape(std::string(key));
      }

      std::string_view valPart = content.substr(colonPos + 1);
      while (!valPart.empty() && valPart.front() == ' ') valPart = valPart.substr(1);

      addComma(stack, out);
      curFrame.first = false;

      if (valPart.empty()) {
        out += '"' + keyStr + "\":{";
        stack.push_back({'O', true, indent, true, false});
      } else if (valPart == "[]") {
        out += '"' + keyStr + "\":[";
        stack.push_back({'A', true, indent, true, false});
      } else if (valPart == "{}") {
        out += '"' + keyStr + "\":{}";
      } else {
        out += '"' + keyStr + "\":" + toonJsonQuoteBare(valPart);
      }
    }
  }

  // Close all remaining frames (EOF)
  while (!stack.empty()) {
    out += (stack.back().type == 'O') ? '}' : ']';
    stack.pop_back();
  }

  while (!out.empty() && (out.back() == ' ' || out.back() == '\n' || out.back() == '\r')) out.pop_back();

  return out;
}

inline std::string asvJSON::toTOON() const {
  if (!root) return "null\n";
  std::string out;
  valToToon(root.get(), out, 0, "");
  return out;
}

inline bool asvJSON::fromTOON(std::string_view input) {
  std::string json = toonToJson(input);
  return parse(std::string_view(json));
}

} // namespace asvJSONInternal
