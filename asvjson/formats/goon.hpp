#pragma once
// GOON serialization/parsing for asvJSON++

#include "../core.hpp"

namespace asvJSONInternal {

static void fmtBase64JsonVal(const uint8_t* data, size_t len, std::string& out) {
  out += "\"__BASE64__";
  out += encodeBase64(data, len);
  out += '"';
}

static void fmtExtJsonVal(int8_t type, const uint8_t* data, size_t len, std::string& out) {
  out += "\"__EXT__";
  out += std::to_string(type);
  out += '_';
  out += encodeBase64(data, len);
  out += '"';
}

static std::string goonLiteral(const asvJSONValue* v) {
  if (!v) return "_";
  switch (v->type) {
    case asvJSONValue::NULL_VAL: return "_";
    case asvJSONValue::BOOL_VAL: return v->flag ? "T" : "F";
    case asvJSONValue::STRING: return v->str_data.empty() ? "~" : std::string();
    default: return {};
  }
}

static bool goonIsSimpleValue(const asvJSONValue* v) {
  return v && v->type != asvJSONValue::OBJECT && v->type != asvJSONValue::ARRAY;
}

static std::string goonFormatSpecial(const asvJSONValue* v) {
  if (!v) return "_";
  switch (v->type) {
    case asvJSONValue::DATETIME: {
      std::string dt; fmtDateTimeVal(v->timestamp, v->datetime_ms, dt);
      return '"' + dt + '"';
    }
    case asvJSONValue::BINARY: {
      if (v->bin_data.empty()) return "\"\"";
      std::string r; fmtBase64JsonVal(v->bin_data.data(), v->bin_data.size(), r); return r;
    }
    case asvJSONValue::OBJECTID: {
      std::string hex; fmtObjectIdHexVal(v->str_data, hex);
      return '"' + hex + '"';
    }
    case asvJSONValue::REGEX: {
      std::string re; fmtRegexVal(v->str_data, re);
      return '"' + re + '"';
    }
    case asvJSONValue::TIMESTAMP:
      return std::to_string(v->num);
    case asvJSONValue::EXTENSION: {
      if (v->bin_data.empty()) return "\"\"";
      std::string r; fmtExtJsonVal(v->ext_type, v->bin_data.data(), v->bin_data.size(), r); return r;
    }
    default: return {};
  }
}

static bool goonNeedsQuotes(const std::string& s) {
  if (s.empty()) return true;
  if (s.front() == '"') return true;
  for (char c : s) if (c == ',' || c == '\n' || c == '\r' || c == ':') return true;
  if (s.front() == ' ' || s.back() == ' ') return true;
  if (s.front() == '$' || s.front() == '#' || s.front() == '^' || (s.front() >= '0' && s.front() <= '9') || s.front() == '-')
    return true;
  if (s == "T" || s == "F" || s == "_" || s == "~" || s == "true" || s == "false" || s == "null")
    return true;
  return false;
}

static void goonWriteString(const std::string& s, std::string& out, const std::unordered_map<std::string, int>* dict) {
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

// Build dictionary of frequently occurring strings for GOON compact mode
static void goonBuildDictWalk(const asvJSONValue* v, std::unordered_map<std::string, int>& freq, int depth) {
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

static std::unordered_map<std::string, int> goonBuildDict(const asvJSONValue* root) {
  std::unordered_map<std::string, int> freq;
  goonBuildDictWalk(root, freq, 0);
  std::unordered_map<std::string, int> dict;
  int idx = 0;
  for (const auto& [s, count] : freq)
    if (count >= 2) dict[s] = idx++;
  return dict;
}

static void goonWriteDictHeader(const std::unordered_map<std::string, int>& dict, std::string& out) {
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

static void goonSerializeVal(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth = 0, const std::unordered_map<std::string, int>* dict = nullptr);

static void goonSerializeArray(const asvJSONValue* arr, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict = nullptr) {
  if (!arr || arr->size() == 0) {
    if (!key.empty()) {
      std::string pad(static_cast<size_t>(indent) * 2, ' ');
      std::string escaped; appendJsonEscaped(escaped, key);
      out += pad + escaped + ": []\n";
    } else out += "[]\n";
    return;
  }
  // Check if all elements are objects with the same keys -> tabular format
  bool tabular = (arr->size() > 0 && arr->get(0) && arr->get(0)->type == asvJSONValue::OBJECT);
  std::vector<std::string> cols;
  if (tabular) {
    for (size_t i = 1; i < arr->size(); i++) {
      if (!arr->get(i) || arr->get(i)->type != asvJSONValue::OBJECT) { tabular = false; break; }
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
  if (tabular && !cols.empty()) {
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    if (!key.empty()) {
      std::string escaped; appendJsonEscaped(escaped, key);
      out += pad + escaped;
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
        if (r > 0 && !prevCols[c].empty() && cell == prevCols[c]) {
          row += '^';
        } else {
          row += cell;
          prevCols[c] = cell;
        }
      }
      if (!prevRow.empty() && row == prevRow) {
        repeatCount++;
      } else {
        if (!prevRow.empty()) {
          out += pad + "  " + prevRow;
          if (repeatCount > 1) out += "*" + std::to_string(repeatCount);
          out += '\n';
        }
        prevRow = row;
        repeatCount = 1;
      }
    }
    if (!prevRow.empty()) {
      out += pad + "  " + prevRow;
      if (repeatCount > 1) out += "*" + std::to_string(repeatCount);
      out += '\n';
    }
    return;
  }
  // List format
  std::string pad(static_cast<size_t>(indent) * 2, ' ');
  if (!key.empty()) {
    std::string escaped; appendJsonEscaped(escaped, key);
    out += pad + escaped;
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
          goonSerializeVal(sub.get(), out, indent + 2, k, depth + 1, dict);
      }
    } else if (child->type == asvJSONValue::ARRAY) {
      out += pad + "  - ";
      out += '[';
      for (size_t j = 0; j < child->size(); j++) {
        if (j > 0) out += ',';
        auto* el = child->get(j);
        if (!el) out += "_";
        else {
          std::string elLit = goonLiteral(el);
          if (!elLit.empty()) out += elLit;
          else {
            std::string spec = goonFormatSpecial(el);
            if (!spec.empty()) out += spec;
            else if (el->type == asvJSONValue::INT) out += std::to_string(el->num);
            else if (el->type == asvJSONValue::DOUBLE) {
              fmtDoubleVal(el->dbl, out);
            } else {
              goonWriteString(el->str_data, out, dict);
            }
          }
        }
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

static void goonSerializeVal(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict) {
  if (!v) return;
  if (!asvJSONValue::checkNestingDepth(depth)) return;
  std::string pad(static_cast<size_t>(indent) * 2, ' ');
  std::string lit = goonLiteral(v);
  if (!lit.empty()) {
    if (!key.empty()) {
      std::string escaped; appendJsonEscaped(escaped, key);
      out += pad + escaped + ": " + lit + "\n";
    }
    else out += pad + lit + "\n";
    return;
  }
  switch (v->type) {
    case asvJSONValue::INT:
      if (!key.empty()) {
        std::string escaped; appendJsonEscaped(escaped, key);
        out += pad + escaped + ": " + std::to_string(v->num) + "\n";
      }
      else out += pad + std::to_string(v->num) + "\n";
      break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d) || std::isinf(d)) {
        if (!key.empty()) {
          std::string escaped; appendJsonEscaped(escaped, key);
          out += pad + escaped + ": _\n";
        }
        else out += pad + "_\n";
        break;
      }
      if (!key.empty()) {
        std::string escaped; appendJsonEscaped(escaped, key);
        out += pad + escaped + ": ";
        fmtDoubleVal(d, out);
      } else {
        fmtDoubleVal(d, out);
      }
      out += '\n';
      break;
    }
    case asvJSONValue::STRING: {
      if (!key.empty()) {
        std::string escaped; appendJsonEscaped(escaped, key);
        out += pad + escaped + ": ";
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
        if (!key.empty()) {
          std::string escaped; appendJsonEscaped(escaped, key);
          out += pad + escaped + ": {}\n";
        }
        else out += pad + "{}\n";
        return;
      }
      if (!key.empty()) {
        std::string escaped; appendJsonEscaped(escaped, key);
        out += pad + escaped + ":\n";
      }
      int childIndent = indent + (key.empty() ? 0 : 1);
      for (const auto& [k, child] : *(v->obj))
        goonSerializeVal(child.get(), out, childIndent, k, depth + 1, dict);
      break;
    }
    case asvJSONValue::ARRAY:
      goonSerializeArray(v, out, indent, key, depth, dict);
      break;
    default: {
      std::string spec = goonFormatSpecial(v);
      if (!spec.empty()) {
        if (!key.empty()) {
          std::string escaped; appendJsonEscaped(escaped, key);
          out += pad + escaped + ": " + spec + "\n";
        }
        else out += pad + spec + "\n";
      } else {
        if (!key.empty()) {
          std::string escaped; appendJsonEscaped(escaped, key);
          out += pad + escaped + ": _\n";
        }
        else out += pad + "_\n";
      }
      break;
    }
  }
}

// GOON -> JSON text converter (GOON decoder)
static std::string goonToJson(std::string_view input) {
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
    std::string s = resolveRef(raw);
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
    // Already JSON value
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '{' && s.back() == '}') || (s.front() == '[' && s.back() == ']')))
      return s;
    // Quote it
    return '"' + [](std::string_view x) {
      std::string r; appendJsonEscaped(r, x); return r;
    }(s) + '"';
  };

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
            // Tabular: read subsequent rows
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
                  if (rcont[ci] == '"') { inQAst = !inQAst; }
                  else if (rcont[ci] == '*' && ci > 0 && !inQAst) { asterisk = ci; break; }
                }
              }
              int repeat = 1;
              std::string_view rowStr = rcont;
              if (asterisk != std::string_view::npos) {
                std::string countStr(rcont.substr(asterisk + 1));
                repeat = atoi(countStr.c_str());
                if (repeat > 10000) repeat = 10000;
                if (repeat < 1) repeat = 1;
                rowStr = rcont.substr(0, asterisk);
              }
              std::vector<std::string> prevValues(fields.size(), "");
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
            for (size_t rl = li + 1; rl < lines.size(); rl++) {
              auto& rline = lines[rl];
              if (rline.empty()) continue;
              int rindent = countIndent(rline);
              if (rindent <= indent) break;
              lastRl = rl;
              std::string_view rcont = stripIndent(rline);
              if (rcont.size() >= 2 && rcont[0] == '-' && rcont[1] == ' ') {
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += expandCell(rcont.substr(2));
              } else if (rcont == "-") {
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += '{';
                stack.push_back({'O', true, rindent, true, false});
              } else {
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += expandCell(rcont);
              }
            }
            if (lastRl > li) li = lastRl;
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
    size_t colonPos = content.find(':');
    if (colonPos != std::string_view::npos) {
      std::string_view key = content.substr(0, colonPos);
      while (!key.empty() && key.back() == ' ') key = key.substr(0, key.size() - 1);
      std::string keyStr;
      if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
        keyStr = goonEscKey(key.substr(1, key.size() - 2));
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

} // namespace asvJSONInternal
using namespace asvJSONInternal;

inline std::string asvJSON::toGOON() const {
  if (!root) return "_\n";
  auto dict = goonBuildDict(root.get());
  std::string out;
  goonSerializeVal(root.get(), out, 0, "", 0, &dict);
  return out;
}

inline bool asvJSON::fromGOON(std::string_view input) {
  try {
    if (input.empty()) throw asvJSONError("empty input");
    std::string json = goonToJson(input);
    return parse(std::string_view(json));
  } catch (const asvJSONError& e) {
    lastError = e.what();
    return false;
  }
}
