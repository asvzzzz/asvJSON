#pragma once
// GOON (Greatly Optimized Object Notation) serialization/parsing for asvJSON++
//
// JSON superset with YAML-like indentation:
//   name: John                   -- object members
//   - item                       -- list items
//   name[N]{col1,col2}:          -- tabular array; comma-separated rows follow
//   []:                          -- bare list with `- value` items
//   T / F / _ / ~                -- true / false / null / empty string
//   $: $1="value" then $1        -- dictionary references
// Special types use __BASE64__/__EXT__ prefixes. Decoder builds intermediate
// JSON then parses via the JSON parser; full round-trip via toGOON()/fromGOON().

#include "../core.hpp"

namespace asvJSONInternal {

const size_t MAX_REPEAT_COUNT = 10000;

class GoonDecodeError : public asvJSONError {
public:
  explicit GoonDecodeError(const std::string& msg) : asvJSONError(msg) {}
};

inline std::string goonLiteral(const asvJSONValue* v) {
  if (!v) return "_";
  switch (v->type) {
    case asvJSONValue::NULL_VAL: return "_";
    case asvJSONValue::BOOL_VAL: return v->flag ? "T" : "F";
    case asvJSONValue::STRING: return v->str_data.empty() ? "~" : std::string();
    default: return {};
  }
}

// Forward declarations for mutual recursion
inline std::string goonFormatSpecial(const asvJSONValue* v);
inline bool goonNeedsQuotes(const std::string& s);
inline void goonSerializeVal(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict, const GoonWriteOptions& opts);
inline void goonSerializeArray(const asvJSONValue* arr, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict, const GoonWriteOptions& opts);

// Serialize a value as valid JSON text. Used for elements inside inline
// `[...]` cells, which the decoder passes through to the JSON parser verbatim
// (so GOON literals like T/F/_/~ and __OID__ prefixes would be invalid there).
inline void goonJsonText(const asvJSONValue* v, std::string& out, int depth = 0) {
  if (!v || !asvJSONValue::checkNestingDepth(depth)) { out += "null"; return; }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += "null"; return;
    case asvJSONValue::BOOL_VAL: out += (v->flag ? "true" : "false"); return;
    case asvJSONValue::INT: out += std::to_string(v->num); return;
    case asvJSONValue::DOUBLE:
      if (std::isnan(v->dbl) || std::isinf(v->dbl)) out += "null";
      else fmtDoubleVal(v->dbl, out);
      return;
    case asvJSONValue::STRING:
      out += '"';
      appendJsonEscaped(out, v->str_data);
      out += '"';
      return;
    case asvJSONValue::DATETIME: {
      out += '"';
      fmtDateTimeVal(v->timestamp, v->datetime_ms, out);
      out += '"';
      return;
    }
    case asvJSONValue::BINARY:
      if (v->bin_data.empty()) { out += "\"\""; return; }
      out += "{\"$binary\":{\"base64\":\"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      out += "\",\"subType\":\"00\"}}";
      return;
    case asvJSONValue::EXTENSION: {
      if (v->bin_data.empty()) { out += "\"\""; return; }
      out += "{\"$binary\":{\"base64\":\"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      out += "\",\"subType\":\"";
      char hbuf[3];
      snprintf(hbuf, sizeof(hbuf), "%02x", static_cast<unsigned>(v->ext_type) & 0xFF);
      out += hbuf;
      out += "\"}}";
      return;
    }
    case asvJSONValue::OBJECTID: {
      std::string hex;
      fmtObjectIdHexVal(v->str_data, hex);
      out += "{\"$oid\":\"";
      out += hex;
      out += "\"}";
      return;
    }
    case asvJSONValue::TIMESTAMP:
      out += "{\"$timestamp\":{\"t\":";
      out += std::to_string(v->num);
      out += "}}";
      return;
    case asvJSONValue::REGEX: {
      size_t sep = v->str_data.rfind('\0');
      out += "{\"$regex\":\"";
      if (sep != std::string::npos) appendJsonEscaped(out, v->str_data.substr(0, sep));
      else appendJsonEscaped(out, v->str_data);
      out += "\",\"$options\":\"";
      if (sep != std::string::npos && sep + 1 < v->str_data.size())
        appendJsonEscaped(out, v->str_data.substr(sep + 1));
      out += "\"}";
      return;
    }
    case asvJSONValue::OBJECT: {
      out += '{';
      bool first = true;
      for (const auto& [k, child] : *(v->obj)) {
        if (!first) out += ',';
        first = false;
        out += '"';
        appendJsonEscaped(out, k);
        out += "\":";
        goonJsonText(child.get(), out, depth + 1);
      }
      out += '}';
      return;
    }
    case asvJSONValue::ARRAY: {
      out += '[';
      for (size_t i = 0; i < v->size(); i++) {
        if (i > 0) out += ',';
        goonJsonText(v->get(i), out, depth + 1);
      }
      out += ']';
      return;
    }
    default: out += "null"; return;
  }
}
inline std::string goonFormatSpecial(const asvJSONValue* v) {
  if (!v) return "_";
  switch (v->type) {
    case asvJSONValue::DATETIME: {
      std::string dt; fmtDateTimeVal(v->timestamp, v->datetime_ms, dt);
      return '"' + dt + '"';
    }
    case asvJSONValue::BINARY: {
      if (v->bin_data.empty()) return "\"\"";
      std::string r("__BASE64__");
      r += encodeBase64(v->bin_data.data(), v->bin_data.size());
      return r;
    }
    case asvJSONValue::OBJECTID: {
      std::string hex; fmtObjectIdHexVal(v->str_data, hex);
      return "__OID__" + hex;
    }
    case asvJSONValue::REGEX: {
      // Emit __REGEX__<escaped-pattern>|<opts> so the decoder can restore the
      // REGEX type. '|' and '\' inside the pattern and options are backslash-
      // escaped so a pattern that itself contains '|' round-trips losslessly.
      size_t sep = v->str_data.rfind('\0');
      std::string pat = (sep != std::string::npos) ? v->str_data.substr(0, sep) : v->str_data;
      std::string opts = (sep != std::string::npos && sep + 1 < v->str_data.size()) ? v->str_data.substr(sep + 1) : std::string();
      std::string esc;
      esc.reserve(pat.size());
      for (char c : pat) {
        if (c == '\\' || c == '|') esc += '\\';
        esc += c;
      }
      std::string result = "__REGEX__" + esc;
      if (!opts.empty()) {
        result += '|';
        for (char c : opts) {
          if (c == '\\' || c == '|') result += '\\';
          result += c;
        }
      }
      return result;
    }
    case asvJSONValue::TIMESTAMP:
      return "__TS__" + std::to_string(v->num);
    case asvJSONValue::EXTENSION: {
      if (v->bin_data.empty()) return "\"\"";
      std::string r("__EXT__");
      r += std::to_string(v->ext_type);
      r += '_';
      r += encodeBase64(v->bin_data.data(), v->bin_data.size());
      return r;
    }
    default: return {};
  }
}

inline bool goonNeedsQuotes(const std::string& s) {
  if (s.empty()) return true;
  if (s.front() == '"') return true;
  for (char c : s) if (c == ',' || c == '\n' || c == '\r' || c == ':') return true;
  if (s.front() == ' ' || s.back() == ' ') return true;
  if (s.front() == '$' || s.front() == '#' || s.front() == '^' || (s.front() >= '0' && s.front() <= '9') || s.front() == '-')
    return true;
  if (s.front() == '+' && s.size() > 1 && s[1] >= '0' && s[1] <= '9') return true;
  if (s.front() == '{' || s.front() == '[') return true;
  if (s.size() >= 2 && s[0] == '_' && s[1] == '_') return true;
  if (s == "T" || s == "F" || s == "_" || s == "~" || s == "true" || s == "false" || s == "null")
    return true;
  if (s == "NaN" || s == "Infinity" || s == "-Infinity") return true;
  return false;
}

inline void goonWriteString(const std::string& s, std::string& out, const std::unordered_map<std::string, int>* dict) {
  if (dict) {
    auto it = dict->find(s);
    if (it != dict->end()) {
      out += "$" + std::to_string(it->second);
      return;
    }
  }
  if (goonNeedsQuotes(s)) out += '"' + [](const std::string& x) {
    std::string r; appendJsonEscaped(r, x); return r;
  }(s) + '"';
  else out += s;
}

inline void goonWriteKey(const std::string& key, std::string& out) {
  if (key.empty()) { out += "\"\""; return; }
  if (goonNeedsQuotes(key)) {
    out += '"';
    appendJsonEscaped(out, key);
    out += '"';
  } else {
    out += key;
  }
}

// Build dictionary of frequently occurring strings for GOON compact mode
inline void goonBuildDictWalk(const asvJSONValue* v, std::unordered_map<std::string, int>& freq, int depth) {
  if (!v || !asvJSONValue::checkNestingDepth(depth)) return;
  switch (v->type) {
    case asvJSONValue::STRING:
      if (!v->str_data.empty()) freq[v->str_data]++;
      break;
    case asvJSONValue::OBJECT:
      if (v->obj)
        for (const auto& [k, child] : *(v->obj))
          goonBuildDictWalk(child.get(), freq, depth + 1);
      break;
    case asvJSONValue::ARRAY:
      for (size_t i = 0; i < v->size(); i++)
        goonBuildDictWalk(v->get(i), freq, depth + 1);
      break;
    default: break;
  }
}

inline std::unordered_map<std::string, int> goonBuildDict(const asvJSONValue* root) {
  std::unordered_map<std::string, int> freq;
  goonBuildDictWalk(root, freq, 0);
  std::unordered_map<std::string, int> dict;
  int idx = 0;
  for (const auto& [s, count] : freq)
    if (count >= 2) dict[s] = idx++;
  return dict;
}

inline void goonWriteDictHeader(const std::unordered_map<std::string, int>& dict, std::string& out) {
  if (dict.empty()) return;
  out += "$:";
  std::vector<std::pair<std::string, int>> entries(dict.begin(), dict.end());
  std::sort(entries.begin(), entries.end(),
    [](auto& a, auto& b) { return a.second < b.second; });
  for (auto& [s, idx] : entries)
    out += " $" + std::to_string(idx) + "=\"" + [](const std::string& x) {
      std::string r; appendJsonEscaped(r, x); return r;
    }(s) + "\",";
  out.pop_back();
  out += '\n';
}





inline void goonSerializeArray(const asvJSONValue* arr, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict = nullptr, const GoonWriteOptions& opts = {}) {
  if (!arr || arr->size() == 0) {
    if (!key.empty() || depth > 0) {
      std::string pad(static_cast<size_t>(indent) * 2, ' ');
      out += pad;
      goonWriteKey(key, out);
      out += ": []\n";
    } else out += "[]\n";
    return;
  }
  // Check if all elements are objects with the same keys -> tabular format
  bool tabular = (arr->size() > 0 && arr->get(0) && arr->get(0)->type == asvJSONValue::OBJECT);
  std::vector<std::string> cols;
  if (tabular) {
    if (arr->size() > 5 && opts.mode != GoonWriteOptions::Mode::Compact) {
      tabular = false;
    } else {
      for (size_t i = 1; i < arr->size(); i++) {
        if (!arr->get(i) || arr->get(i)->type != asvJSONValue::OBJECT) { tabular = false; break; }
      }
    }
  }
  if (tabular && arr->get(0) && arr->get(0)->type == asvJSONValue::OBJECT && arr->get(0)->obj) {
    for (const auto& [k, _] : *(arr->get(0)->obj)) cols.push_back(k);
    std::sort(cols.begin(), cols.end());

    for (size_t i = 1; i < arr->size(); i++) {
      auto* obj = arr->get(i);
      if (!obj || !obj->obj || obj->obj->size() != cols.size()) { tabular = false; break; }
      std::vector<std::string> rowKeys;
      rowKeys.reserve(obj->obj->size());
      for (const auto& [k, _] : *(obj->obj)) rowKeys.push_back(k);
      std::sort(rowKeys.begin(), rowKeys.end());
      if (rowKeys != cols) { tabular = false; break; }
    }
  }
  // A tabular header cannot represent column names containing separators
  // (`,` splits columns, `{}`/`"`/`\` break header parsing), so fall back to
  // list format which quotes keys robustly.
  if (tabular) {
    for (const auto& c : cols) {
      if (c.empty() || c.front() == ' ' || c.back() == ' ') { tabular = false; break; }
      bool bad = false;
      for (char ch : c) {
        if (ch == ',' || ch == '{' || ch == '}' || ch == '"' || ch == '\\' || ch == '\n' || ch == '\r') { bad = true; break; }
      }
      if (bad) { tabular = false; break; }
    }
  }
  if (tabular && !cols.empty()) {
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    if (!key.empty() || depth > 0) {
      out += pad;
      goonWriteKey(key, out);
    }
    out += "[" + std::to_string(arr->size()) + "]{" + [](const std::string& x) {
      std::string r; appendJsonEscaped(r, x); return r;
    }(cols[0]);
    for (size_t i = 1; i < cols.size(); i++)
      out += "," + [](const std::string& x) {
        std::string r; appendJsonEscaped(r, x); return r;
      }(cols[i]);
    out += "}:\n";
    // RLE: detect consecutive identical rows; ^: per-column previous-value ref
    std::string prevRow;
    int repeatCount = 0;
    std::vector<std::string> prevCols(cols.size());
    for (size_t r = 0; r < arr->size(); r++) {
      auto* obj = arr->get(r);
      std::string row;
      for (size_t c = 0; c < cols.size(); c++) {
        if (c > 0) row += ",";
        auto* child = obj->getConst(cols[c]);
        std::string cell;
        if (!child) { cell = "_"; }
        else {
          std::string lit = goonLiteral(child);
          if (!lit.empty()) { cell = lit; }
          else if (child->type == asvJSONValue::INT) cell = std::to_string(child->num);
          else if (child->type == asvJSONValue::DOUBLE) {
            double d = child->dbl;
            if (std::isnan(d) || std::isinf(d)) cell = "_";
            else { fmtDoubleVal(d, cell); }
          } else if (child->type == asvJSONValue::STRING) {
            goonWriteString(child->str_data, cell, dict);
          } else {
            std::string spec = goonFormatSpecial(child);
            if (!spec.empty()) cell = spec;
            else {
              cell += '"';
              appendJsonEscaped(cell, child->str_data);
              cell += '"';
            }
          }
        }
        if (r > 0 && !prevCols[c].empty() && cell == prevCols[c] && opts.mode != GoonWriteOptions::Mode::LLM) {
          row += '^';
        } else {
          row += cell;
          prevCols[c] = cell;
        }
      }
      if (!prevRow.empty() && row == prevRow && opts.mode != GoonWriteOptions::Mode::LLM) {
        repeatCount++;
      } else {
        if (!prevRow.empty()) {
          out += pad + "  " + prevRow;
          if (repeatCount > 1) out += "*" + std::to_string(std::min<size_t>(static_cast<size_t>(repeatCount), MAX_REPEAT_COUNT));
          out += '\n';
        }
        prevRow = row;
        repeatCount = 1;
      }
    }
    if (!prevRow.empty()) {
      out += pad + "  " + prevRow;
      if (repeatCount > 1) out += "*" + std::to_string(std::min<size_t>(static_cast<size_t>(repeatCount), MAX_REPEAT_COUNT));
      out += '\n';
    }
    return;
  }
  // List format
  std::string pad(static_cast<size_t>(indent) * 2, ' ');
  if (!key.empty() || depth > 0) {
    out += pad;
    goonWriteKey(key, out);
  }
  out += "[]:\n";
  for (size_t i = 0; i < arr->size(); i++) {
    auto* child = arr->get(i);
    if (!child) { out += pad + "  - _\n"; continue; }
    std::string lit = goonLiteral(child);
    if (!lit.empty()) {
      out += pad + "  - " + lit + "\n";
    } else if (child->type == asvJSONValue::OBJECT) {
      out += pad + "  -\n";
      if (child->obj) {
        for (const auto& [k, sub] : *(child->obj))
          goonSerializeVal(sub.get(), out, indent + 2, k, depth + 1, dict, opts);
      }
    } else if (child->type == asvJSONValue::ARRAY) {
      // Nested array inside a list item serializes as an inline `[...]` cell.
      // The decoder passes `[...]` through to the JSON parser verbatim, so the
      // elements must be valid JSON text (GOON literals like T/_/~ and the
      // __OID__ prefixes are invalid there and would corrupt the value).
      out += pad + "  - [";
      for (size_t j = 0; j < child->size(); j++) {
        if (j > 0) out += ',';
        goonJsonText(child->get(j), out, depth + 1);
      }
      out += "]\n";
    } else {
      out += pad + "  - ";
      if (child->type == asvJSONValue::INT) out += std::to_string(child->num);
      else if (child->type == asvJSONValue::DOUBLE) {
        fmtDoubleVal(child->dbl, out);
      } else {
        std::string spec = goonFormatSpecial(child);
        if (!spec.empty()) out += spec;
        else {
          goonWriteString(child->str_data, out, dict);
        }
      }
      out += '\n';
    }
  }
}

inline void goonSerializeVal(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict = nullptr, const GoonWriteOptions& opts = {}) {
  if (!v) return;
  if (!asvJSONValue::checkNestingDepth(depth)) return;
  std::string pad(static_cast<size_t>(indent) * 2, ' ');
  std::string lit = goonLiteral(v);
  if (!lit.empty()) {
    if (!key.empty() || depth > 0) {
      out += pad;
      goonWriteKey(key, out);
      out += ": " + lit + "\n";
    }
    else out += pad + lit + "\n";
    return;
  }
  switch (v->type) {
    case asvJSONValue::INT:
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out);
        out += ": " + std::to_string(v->num) + "\n";
      }
      else out += pad + std::to_string(v->num) + "\n";
      break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d) || std::isinf(d)) {
        if (!key.empty() || depth > 0) {
          out += pad;
          goonWriteKey(key, out);
          out += ": _\n";
        }
        else out += pad + "_\n";
        break;
      }
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out);
        out += ": ";
        fmtDoubleVal(d, out);
      } else {
        fmtDoubleVal(d, out);
      }
      out += '\n';
      break;
    }
    case asvJSONValue::STRING: {
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out);
        out += ": ";
        goonWriteString(v->str_data, out, dict);
        out += '\n';
      } else {
        goonWriteString(v->str_data, out, dict);
        out += '\n';
      }
      break;
    }
    case asvJSONValue::OBJECT: {
      if (!v->obj || v->obj->empty()) {
        if (!key.empty() || depth > 0) {
          out += pad;
          goonWriteKey(key, out);
          out += ": {}\n";
        }
        else out += pad + "{}\n";
        return;
      }
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out);
        out += ":\n";
      }
      int childIndent = indent + (!key.empty() || depth > 0 ? 1 : 0);
      for (const auto& [k, child] : *(v->obj))
        goonSerializeVal(child.get(), out, childIndent, k, depth + 1, dict, opts);
      break;
    }
    case asvJSONValue::ARRAY:
      goonSerializeArray(v, out, indent, key, depth, dict, opts);
      break;
    default: {
      std::string spec = goonFormatSpecial(v);
      if (!spec.empty()) {
        if (!key.empty()) {
          out += pad;
          goonWriteKey(key, out);
          out += ": " + spec + "\n";
        }
        else out += pad + spec + "\n";
      } else {
        if (!key.empty()) {
          out += pad;
          goonWriteKey(key, out);
          out += ": _\n";
        }
        else out += pad + "_\n";
      }
      break;
    }
  }
}

// GOON -> JSON text converter (GOON decoder)
inline std::string goonToJson(std::string_view input) {
  auto lines = splitLines(input);
  if (lines.empty()) return "{}";

  // Parse dictionary
  std::unordered_map<std::string, std::string> dict;
  size_t lineStart = 0;
  if (!lines.empty() && lines[0].size() >= 2 && lines[0][0] == '$' && lines[0][1] == ':') {
    std::string_view dline(lines[0]);
    size_t pos = 2;
    while (pos < dline.size()) {
      while (pos < dline.size() && (dline[pos] == ' ' || dline[pos] == '\t')) pos++;
      if (pos >= dline.size()) break;
      if (dline[pos] != '$') break;
      pos++; // '$'
      size_t nstart = pos;
      while (pos < dline.size() && dline[pos] >= '0' && dline[pos] <= '9') pos++;
      std::string idx(dline.substr(nstart, pos - nstart));
      if (pos >= dline.size() || dline[pos] != '=') break;
      pos++; // '='
      std::string val;
      if (pos < dline.size() && dline[pos] == '"') {
        pos++; // opening quote
        std::string raw;
        while (pos < dline.size()) {
          if (dline[pos] == '\\' && pos + 1 < dline.size()) {
            raw += dline[pos]; raw += dline[pos + 1]; pos += 2;
          } else if (dline[pos] == '"') {
            break;
          } else {
            raw += dline[pos]; pos++;
          }
        }
        val = unescapeJsonString(raw, false);
        if (pos < dline.size()) pos++; // closing quote
      } else {
        while (pos < dline.size() && dline[pos] != ',' && dline[pos] != ' ' && dline[pos] != '\t') {
          val += dline[pos]; pos++;
        }
      }
      dict[idx] = val;
      if (pos < dline.size() && dline[pos] == ',') pos++;
    }
    lineStart = 1;
  }

  // Resolve $N reference
  auto resolveRef = [&](std::string_view s) -> std::string {
    if (s.size() > 1 && s[0] == '$') {
      std::string idx(s.substr(1));
      auto it = dict.find(idx);
      if (it != dict.end()) return it->second;
    }
    return std::string(s);
  };

  // Find first content line
  size_t firstLine = lineStart;
  while (firstLine < lines.size() && lines[firstLine].empty()) firstLine++;
  if (firstLine >= lines.size()) return "{}";

  std::string_view firstContent = stripIndent(lines[firstLine]);
  bool rootIsArr = (firstContent.size() >= 2 && firstContent[0] == '[') ||
    (firstContent.size() >= 2 && firstContent.compare(0, 2, "[]") == 0) ||
    (firstContent.size() >= 1 && firstContent[0] == '-');

  std::string out;
  std::vector<FormatFrame> stack;
  int rootIndent = countIndent(lines[firstLine]);

  auto expandCell = [&](std::string_view raw) -> std::string {
    // A bare $N dictionary reference always denotes the dict *string* value,
    // so the resolved text must stay a quoted string — never reinterpreted as
    // a number/bool/special prefix (e.g. a repeated string "42" or "__TS__1").
    bool wasDictRef = raw.size() > 1 && raw[0] == '$';
    if (wasDictRef) {
      for (size_t di = 1; di < raw.size(); di++) {
        if (raw[di] < '0' || raw[di] > '9') { wasDictRef = false; break; }
      }
    }
    std::string s = resolveRef(raw);
    if (wasDictRef) {
      std::string r;
      appendJsonEscaped(r, s);
      return '"' + r + '"';
    }
    // T->true, F->false, _->null, ~->""
    if (s == "T") return "true";
    if (s == "F") return "false";
    if (s == "_") return "null";
    if (s == "~") return "\"\"";
    // Number
    if ((!s.empty() && (s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) ||
      (s.size() > 1 && s[0] == '+' && s[1] >= '0' && s[1] <= '9')) {
      bool isNum = true;
      bool hasDot = false, hasExp = false;
      for (size_t ci = (s[0] == '-' || s[0] == '+') ? 1 : 0; ci < s.size(); ci++) {
        if (s[ci] >= '0' && s[ci] <= '9') continue;
        if (s[ci] == '.' && !hasDot && !hasExp) { hasDot = true; continue; }
        if ((s[ci] == 'e' || s[ci] == 'E') && !hasExp && ci > 0 && ci + 1 < s.size()) {
          hasExp = true; continue;
        }
        if ((s[ci] == '+' || s[ci] == '-') && hasExp && ci > 0 && (s[ci-1] == 'e' || s[ci-1] == 'E')) continue;
        isNum = false; break;
      }
      if (isNum && !s.empty()) {
        if (s.size() > 1 && s[0] == '+') s = s.substr(1);
        return s;
      }
    }
    // NaN/Infinity
    if (s == "NaN" || s == "Infinity" || s == "-Infinity") return s;
    // GOON special prefixes → JSON convention. Applied ONLY to unquoted cells:
    // a quoted value is literal text, so a string like "__TS__42" (which the
    // encoder now quotes) stays a string instead of becoming a timestamp.
    if (s.size() >= 1 && s.front() != '"') {
      std::string_view unquoted = s;
      if (unquoted.substr(0, 6) == "__TS__") {
        return "{\"$timestamp\":{\"t\":" + std::string(unquoted.substr(6)) + "}}";
      }
      if (unquoted.substr(0, 7) == "__OID__") {
        return "{\"$oid\":\"" + std::string(unquoted.substr(7)) + "\"}";
      }
      if (unquoted.substr(0, 10) == "__BASE64__") {
        return "{\"$binary\":{\"base64\":\"" + std::string(unquoted.substr(10)) + "\",\"subType\":\"00\"}}";
      }
      if (unquoted.substr(0, 9) == "__REGEX__") {
        // The encoder escapes '|' and '\' inside the pattern, so split at the
        // LAST unescaped '|' and unescape the pattern/options sides.
        std::string content(unquoted.substr(9));
        size_t sep = std::string::npos;
        size_t bs = 0;
        for (size_t ci = 0; ci < content.size(); ci++) {
          if (content[ci] == '\\') { bs++; continue; }
          if (content[ci] == '|') {
            if (bs % 2 == 0) sep = ci;
            bs = 0;
          } else bs = 0;
        }
        auto unescapeReg = [](const std::string& x) {
          std::string r;
          for (size_t i = 0; i < x.size(); i++) {
            if (x[i] == '\\' && i + 1 < x.size() && (x[i + 1] == '\\' || x[i + 1] == '|')) {
              r += x[i + 1]; i++;
            } else r += x[i];
          }
          return r;
        };
        std::string pat = (sep != std::string::npos) ? unescapeReg(content.substr(0, sep)) : unescapeReg(content);
        std::string opts = (sep != std::string::npos) ? unescapeReg(content.substr(sep + 1)) : std::string();
        std::string patEsc, optsEsc;
        appendJsonEscaped(patEsc, pat);
        appendJsonEscaped(optsEsc, opts);
        return "{\"$regex\":\"" + patEsc + "\",\"$options\":\"" + optsEsc + "\"}";
      }
      if (unquoted.substr(0, 7) == "__EXT__") {
        std::string rest(unquoted.substr(7));
        size_t us = rest.find('_');
        if (us != std::string::npos) {
          int type = static_cast<int>(std::strtol(rest.substr(0, us).c_str(), nullptr, 10));
          std::string b64 = rest.substr(us + 1);
          char hbuf[3];
          snprintf(hbuf, sizeof(hbuf), "%02x", static_cast<unsigned>(type) & 0xFF);
          return "{\"$binary\":{\"base64\":\"" + b64 + "\",\"subType\":\"" + hbuf + "\"}}";
        }
        return "{\"$binary\":{\"base64\":\"" + rest + "\",\"subType\":\"00\"}}";
      }
    }
    // Already JSON value
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '{' && s.back() == '}') || (s.front() == '[' && s.back() == ']')))
      return s;
    // Quote it
    return '"' + [](std::string_view x) {
      std::string r; appendJsonEscaped(r, x); return r;
    }(s) + '"';
  };

  // Bare scalar root document (e.g. a root INT/STRING/DATETIME serialized by
  // toGOON()). Without this, a bare first line fell through every branch of
  // the object path and silently produced "{}". Expanded to JSON here so
  // scalar roots round-trip exactly like the JSON layer.
  if (!rootIsArr) {
    bool hasColon = false;
    {
      bool inQ = false;
      for (size_t i = 0; i < firstContent.size(); i++) {
        if (firstContent[i] == '"' && (i == 0 || firstContent[i - 1] != '\\')) inQ = !inQ;
        else if (!inQ && firstContent[i] == ':') { hasColon = true; break; }
      }
    }
    bool looksScalar = !hasColon &&
      !firstContent.empty() && firstContent[0] != '-' &&
      firstContent[0] != '{' && firstContent[0] != '[' && firstContent[0] != ']' &&
      firstContent[0] != '}';
    if (looksScalar) {
      for (size_t k = firstLine + 1; k < lines.size(); k++) {
        if (!lines[k].empty()) {
          throw GoonDecodeError("GOON: unexpected trailing content after root value");
        }
      }
      return expandCell(firstContent);
    }
  }

  // TOON-compatible comma splitter for GOON decoder
  auto goonSplitCommas = [](std::string_view s) -> std::vector<std::string> {
    std::vector<std::string> result;
    std::string cur;
    bool inQ = false;
    bool escape = false;
    for (size_t i = 0; i < s.size(); i++) {
      char c = s[i];
      if (escape) { cur += c; escape = false; continue; }
      if (c == '\\' && inQ) { cur += c; escape = true; continue; }
      if (c == '"') { cur += c; inQ = !inQ; continue; }
      if (c == ',' && !inQ) {
        result.push_back(std::move(cur));
        cur.clear();
        continue;
      }
      cur += c;
    }
    result.push_back(std::move(cur));
    return result;
  };

  // Helper to escape keys inline
  auto goonEscKey = [](std::string_view k) -> std::string {
    std::string r; appendJsonEscaped(r, k); return r;
  };

  out = rootIsArr ? '[' : '{';
  stack.push_back({rootIsArr ? 'A' : 'O', true, rootIndent, true, true});

  for (size_t li = firstLine; li < lines.size(); li++) {
    auto& line = lines[li];
    if (line.empty()) continue;
    int indent = countIndent(line);
    std::string_view content = stripIndent(line);

    closeFrames(stack, out, indent);
    if (stack.empty()) break;
    auto& curFrame = stack.back();

    // Tabular array header: name[N]{col1,col2}: or name[]{col1,col2}: or name[]:
    {
      size_t openB = content.find('[');
      if (openB != std::string_view::npos && openB < content.size() - 1) {
        size_t closeB = content.find(']', openB);
        if (closeB != std::string_view::npos) {
          size_t afterB = closeB + 1;
          bool isHeader = false;
          if (afterB < content.size() && content[afterB] == ':') isHeader = true;
          if (!isHeader && afterB < content.size() && content[afterB] == '{') {
            size_t closeBr = content.find('}', afterB);
            if (closeBr != std::string_view::npos && closeBr + 1 < content.size() && content[closeBr + 1] == ':')
              isHeader = true;
          }
          if (isHeader) {
            bool hasFields = afterB < content.size() && content[afterB] == '{';
            std::vector<std::string> fields;
            if (hasFields) {
              size_t closeBr = content.find('}', afterB);
              if (closeBr != std::string_view::npos) {
                std::string_view fc = content.substr(afterB + 1, closeBr - afterB - 1);
                auto fieldParts = goonSplitCommas(fc);
                for (auto& f : fieldParts) {
                  size_t fs = 0;
                  while (fs < f.size() && f[fs] == ' ') fs++;
                  size_t fe = f.size();
                  while (fe > fs && f[fe - 1] == ' ') fe--;
                  fields.push_back(f.substr(fs, fe - fs));
                }
                afterB = closeBr + 1;
              }
            }
            if (afterB < content.size() && content[afterB] == ':') {
          bool rootArr = !stack.empty() && stack.back().isRoot && stack.back().type == 'A';
          if (!rootArr) {
            addComma(stack, out);
            if (!stack.empty()) stack.back().first = false;
            // Emit key name before [ if present
            if (openB > 0) {
              std::string_view key = content.substr(0, openB);
              while (!key.empty() && key.back() == ' ') key = key.substr(0, key.size() - 1);
              out += '"' + goonEscKey(key) + "\":";
            }
            out += '[';
            stack.push_back({'A', true, indent, true, false});
          }
          size_t afterColon = afterB + 1;
          while (afterColon < content.size() && content[afterColon] == ' ') afterColon++;
          if (hasFields && !fields.empty()) {
            // Tabular: read subsequent rows. prevValues carries the previous
            // ROW's cell values so a `^` reference resolves across rows (the
            // encoder emits `^` between consecutive rows, not within one).
            std::vector<std::string> prevValues(fields.size(), "");
            for (size_t rl = li + 1; rl < lines.size(); rl++) {
              auto& rline = lines[rl];
              if (rline.empty()) continue;
              int rindent = countIndent(rline);
              if (rindent <= indent) break;
              std::string_view rcont = stripIndent(rline);
              // Check for RLE: value*N (respecting quotes)
              size_t asterisk = std::string_view::npos;
              {
                bool inQAst = false;
                for (size_t ci = 0; ci < rcont.size(); ci++) {
                  char c_char = rcont[ci];
                  if (c_char == '"') { inQAst = !inQAst; }
                  else if (c_char == '*' && ci > 0 && !inQAst) { asterisk = ci; break; }
                }
              }
              int repeat = 1;
              std::string_view rowStr = rcont;
              if (asterisk != std::string_view::npos) {
                std::string countStr(rcont.substr(asterisk + 1));
                repeat = atoi(countStr.c_str());
                if (repeat < 1) repeat = 1;
                else if (repeat > 10000) {
                  throw GoonDecodeError("RLE repeat count exceeds limit of 10000 at line " + std::to_string(li + 1));
                }
                rowStr = rcont.substr(0, asterisk);
              }

              for (int ri = 0; ri < repeat; ri++) {
                std::vector<std::string> vals;
                  {
                    std::string cur2;
                    bool inQ = false;
                    bool escape = false;
                    for (size_t ci = 0; ci < rowStr.size(); ci++) {
                      char c2 = rowStr[ci];
                      if (!escape && c2 == '\\') { escape = true; cur2 += c2; continue; }
                      if (!escape && c2 == '"') { inQ = !inQ; cur2 += c2; continue; }
                      escape = false;
                      if (c2 == ',' && !inQ) { vals.push_back(cur2); cur2.clear(); continue; }
                      cur2 += c2;
                    }
                  vals.push_back(cur2);
                }
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += '{';
                for (size_t fi = 0; fi < fields.size(); fi++) {
                  if (fi > 0) out += ',';
                  out += '"' + goonEscKey(fields[fi]) + "\":";
                  if (fi < vals.size()) {
                    std::string cell = vals[fi];
                    if (cell == "\"^\"") {
                      cell = "^";
                      prevValues[fi] = "^";
                    } else if (cell == "^" && !prevValues[fi].empty()) {
                      cell = prevValues[fi];
                    } else if (cell != "^") {
                      prevValues[fi] = cell;
                    }
                    out += expandCell(cell);
                  } else {
                    out += "null";
                  }
                }
                out += '}';
              }
            }
            if (!rootArr) {
              out += ']';
              if (!stack.empty()) stack.pop_back();
            }
            continue;
          } else if (afterColon < content.size()) {
            // Inline list: [N]: val1,val2,val3 or []: val1,val2
            std::string_view inlineContent = content.substr(afterColon);
            std::vector<std::string> inlineVals;
            {
              std::string cur2;
              bool inQ = false;
              bool escape = false;
              for (size_t ci = 0; ci < inlineContent.size(); ci++) {
                char c2 = inlineContent[ci];
                if (!escape && c2 == '\\') { escape = true; cur2 += c2; continue; }
                if (!escape && c2 == '"') { inQ = !inQ; cur2 += c2; continue; }
                escape = false;
                if (c2 == ',' && !inQ) {
                  size_t vs = 0; while (vs < cur2.size() && cur2[vs] == ' ') vs++;
                  size_t ve = cur2.size(); while (ve > vs && cur2[ve-1] == ' ') ve--;
                  inlineVals.push_back(cur2.substr(vs, ve - vs));
                  cur2.clear(); continue;
                }
                cur2 += c2;
              }
              size_t vs = 0; while (vs < cur2.size() && cur2[vs] == ' ') vs++;
              size_t ve = cur2.size(); while (ve > vs && cur2[ve-1] == ' ') ve--;
              if (!cur2.empty()) inlineVals.push_back(cur2.substr(vs, ve - vs));
            }
            for (auto& v : inlineVals) {
              if (!stack.empty() && !stack.back().first) out += ',';
              if (!stack.empty()) stack.back().first = false;
              out += expandCell(v);
            }
            // Read subsequent list items from indented lines
            for (size_t rl = li + 1; rl < lines.size(); rl++) {
              auto& rline = lines[rl];
              if (rline.empty()) continue;
              int rindent = countIndent(rline);
              if (rindent <= indent) break;
              std::string_view rcont = stripIndent(rline);
              if (rcont.size() >= 2 && rcont[0] == '-' && rcont[1] == ' ') {
                out += ',';
                out += expandCell(rcont.substr(2));
              }
            }
            if (!rootArr) {
              out += ']';
              if (!stack.empty()) stack.pop_back();
            }
            continue;
          } else {
            // No inline values, no fields -> list items from subsequent lines
            size_t lastRl = li;
            bool deferToMain = false;
            for (size_t rl = li + 1; rl < lines.size() && !deferToMain; rl++) {
              auto& rline = lines[rl];
              if (rline.empty()) continue;
              int rindent = countIndent(rline);
              if (rindent <= indent) break;
              std::string_view rcont = stripIndent(rline);
              if (rcont == "-" || (rcont.size() >= 2 && rcont[0] == '-' && rcont[1] == ' ' &&
                  rcont.size() >= 3 && (rcont[2] == '[' || rcont[2] == '{'))) {
                // Complex list item (bare object / nested flow array or object):
                // leave the array frame open and let the main loop add array
                // elements so nested bodies stay inside the array.
                if (lastRl > li) li = lastRl;
                deferToMain = true;
              } else if (rcont.size() >= 2 && rcont[0] == '-' && rcont[1] == ' ') {
                lastRl = rl;
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += expandCell(rcont.substr(2));
              } else {
                lastRl = rl;
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += expandCell(rcont);
              }
            }
            if (lastRl > li) li = lastRl;
            // If we deferred a complex item, keep the leading '[' open so the
            // main loop appends elements into the same array.
            if (deferToMain && !rootArr) {
              // The array is still open; just let the main loop continue.
              continue;
            }
            if (!rootArr) {
              out += ']';
              if (!stack.empty()) stack.pop_back();
            }
            continue;
          }
          continue;
        }
      }
    }
      }
    }

    // Plain []: with no fields -> list (rare, but spec allows)
    if (content.size() >= 2 && content.compare(0, 2, "[]") == 0 && content.find(':') != std::string_view::npos) {
      size_t colon = content.find(':');
      addComma(stack, out);
      if (!stack.empty()) stack.back().first = false;
      out += '[';
      stack.push_back({'A', true, indent, true, false});
      size_t after = colon + 1;
      while (after < content.size() && content[after] == ' ') after++;
      if (after < content.size()) {
        std::string_view rest = content.substr(after);
        std::vector<std::string> inlineVals;
        {
          std::string cur2;
        bool inQ = false;
        bool escape = false;
        for (size_t ci = 0; ci < rest.size(); ci++) {
          char c2 = rest[ci];
          if (!escape && c2 == '\\') { escape = true; cur2 += c2; continue; }
          if (!escape && c2 == '"') { inQ = !inQ; cur2 += c2; continue; }
          escape = false;
          if (c2 == ',' && !inQ) {
            size_t vs = 0; while (vs < cur2.size() && cur2[vs] == ' ') vs++;
            size_t ve = cur2.size(); while (ve > vs && cur2[ve-1] == ' ') ve--;
            inlineVals.push_back(cur2.substr(vs, ve - vs));
            cur2.clear(); continue;
          }
          cur2 += c2;
        }
          size_t vs = 0; while (vs < cur2.size() && cur2[vs] == ' ') vs++;
          size_t ve = cur2.size(); while (ve > vs && cur2[ve-1] == ' ') ve--;
          if (!cur2.empty()) inlineVals.push_back(cur2.substr(vs, ve - vs));
        }
        for (auto& v : inlineVals) {
          if (!stack.empty() && !stack.back().first) out += ',';
          if (!stack.empty()) stack.back().first = false;
          out += expandCell(v);
        }
      }
      // Read list items from subsequent lines (with lastRl to prevent double-processing)
      size_t lastRl = li;
      for (size_t rl = li + 1; rl < lines.size(); rl++) {
        auto& rline = lines[rl];
        if (rline.empty()) continue;
        int rindent = countIndent(rline);
        if (rindent <= indent) break;
        std::string_view rcont = stripIndent(rline);
        if (rcont.size() >= 2 && rcont[0] == '-' && rcont[1] == ' ') {
          out += ',';
          out += expandCell(rcont.substr(2));
          lastRl = rl;
        } else {
          // Not a simple "- " item - let outer loop handle it
          break;
        }
      }
      if (lastRl > li) li = lastRl;
      out += ']';
      if (!stack.empty()) stack.pop_back();
      continue;
    }

    // List item: - value
    if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
      addComma(stack, out);
      curFrame.first = false;
      out += expandCell(content.substr(2));
      continue;
    }
    // Bare "-" means an inline object
    if (content == "-") {
      addComma(stack, out);
      curFrame.first = false;
      out += '{';
      stack.push_back({'O', true, indent, true, false});
      continue;
    }

    // key: value
    size_t colonPos = std::string_view::npos;
    {
      bool inQ = false;
      for (size_t ci = 0; ci < content.size(); ci++) {
        if (content[ci] == '"' && (ci == 0 || content[ci - 1] != '\\')) { inQ = !inQ; continue; }
        if (!inQ && content[ci] == ':') { colonPos = ci; break; }
      }
    }
    if (colonPos != std::string_view::npos) {
      std::string_view key = content.substr(0, colonPos);
      while (!key.empty() && key.back() == ' ') key = key.substr(0, key.size() - 1);
      std::string keyStr;
      if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
        // Quoted key content is JSON-escaped text; decode it first so that
        // `\"`/`\\`/`\uXXXX` sequences are not escaped a second time.
        keyStr = goonEscKey(unescapeJsonString(key.substr(1, key.size() - 2), false));
      } else {
        keyStr = goonEscKey(key);
      }
      std::string_view valPart = content.substr(colonPos + 1);
      while (!valPart.empty() && valPart.front() == ' ') valPart = valPart.substr(1);

      addComma(stack, out);
      curFrame.first = false;

if (valPart.empty()) {
  out += '"' + keyStr + "\":{";
  stack.push_back({'O', true, indent, true, false});
} else if (valPart == "{}") {
  out += '"' + keyStr + "\":{}";
} else if (valPart == "[]") {
  out += '"' + keyStr + "\":[]";
} else {
  out += '"' + keyStr + "\":" + expandCell(valPart);
}
    }
  }

  // Close all remaining frames
  while (!stack.empty()) {
    out += (stack.back().type == 'O') ? '}' : ']';
    stack.pop_back();
  }

  while (!out.empty() && (out.back() == ' ' || out.back() == '\n' || out.back() == '\r')) out.pop_back();

  return out;
}

inline std::string asvJSON::toGOON(const GoonWriteOptions& opts) const {
  if (!root) return "_\n";
  auto dict = goonBuildDict(root.get());
  std::string out;
  goonWriteDictHeader(dict, out);
  goonSerializeVal(root.get(), out, 0, "", 0, &dict, opts);
  return out;
}

inline bool asvJSON::fromGOON(std::string_view input) {
  formatType = FormatType::Unknown;
  try {
    if (input.empty()) throw asvJSONError("empty input");
    std::string json = goonToJson(input);
    bool ok = parse(std::string_view(json));
    if (ok) formatType = FormatType::GOON;
    return ok;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    return false;
  }
}

} // namespace asvJSONInternal
