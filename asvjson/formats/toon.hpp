#pragma once
// TOON (Token-Oriented Object Notation) serialization/parsing for asvJSON++
//
// Compact JSON-like format using 2-space indentation instead of braces:
//   key: value            -- object member (value may be a bare token or JSON)
//   - item                -- array element
//   key:                  -- nested object / array (children indented)
//   key[N]: v1,v2         -- inline primitive array with length header (§9.1)
//   key[N]: / - items     -- list-form array with length header (§9.4)
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
  if (s == "true" || s == "false" || s == "null") return true;
  if (s.front() == '-' || (s.front() >= '0' && s.front() <= '9')) {
    // Validate the full JSON number grammar so we never emit/keep text (e.g.
    // bare "-", "1.", "1e", "-.5") that the JSON parser would reject.
    size_t i = (s.front() == '-') ? 1 : 0;
    if (i >= s.size()) return false;               // bare '-'
    bool hasIntDigit = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { i++; hasIntDigit = true; }
    if (i < s.size() && s[i] == '.') {
      if (!hasIntDigit) return false;              // '.x', '-.x' are not valid JSON
      i++;
      if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;  // a '.' must be followed by a digit
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      i++;
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
      if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;  // exponent digits required
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
    }
    return i == s.size();
  }
  return false;
}

inline bool toonValNeedsQuotes(const std::string& s) {
  if (s.empty()) return true;
  if (toonIsJsonValue(s)) return true;             // number-shaped / true / false / null / "{...}" / "[...]"
  if (s.front() == '-') return true;               // leading '-' could be misread as a list item / root array
  for (char c : s) if (c == ',' || c == '\n' || c == '\r' || c == ':') return true;
  if (s.front() == ' ' || s.back() == ' ') return true;
  if (s.find('[') != std::string::npos) return true;  // keys like a[3] collide with key[N]: headers
  if (s.find('"') != std::string::npos) return true;  // embedded quotes must not break quote-aware key parsing
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
    case asvJSONValue::BINARY:
    case asvJSONValue::EXTENSION: {
      std::string r = "{\"$binary\":{\"base64\":\"";
      appendJsonEscaped(r, encodeBase64(v->bin_data.data(), v->bin_data.size()));
      r += "\",\"subType\":\"";
      if (v->type == asvJSONValue::EXTENSION) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(v->ext_type));
        r += hex;
      } else {
        r += "00";
      }
      r += "\"}}";
      return r;
    }
    case asvJSONValue::OBJECTID: {
      std::string r = "{\"$oid\":\"";
      fmtObjectIdHexVal(v->str_data, r);
      r += "\"}";
      return r;
    }
    case asvJSONValue::REGEX: {
      size_t sep = v->str_data.rfind('\0');
      std::string_view pattern = (sep != std::string_view::npos) ? std::string_view(v->str_data.data(), sep) : std::string_view(v->str_data);
      std::string_view opts = (sep != std::string_view::npos) ? std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1) : std::string_view();
      std::string r = "{\"$regex\":\"";
      appendJsonEscaped(r, pattern);
      r += "\"";
      if (!opts.empty()) {
        r += ",\"$options\":\"";
        appendJsonEscaped(r, opts);
        r += "\"";
      }
      r += "}";
      return r;
    }
    case asvJSONValue::TIMESTAMP: {
      std::string r = "{\"$timestamp\":{\"t\":";
      char buf[32];
      auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
      if (ec == std::errc()) r.append(buf, ptr - buf);
      r += "}}";
      return r;
    }
    case asvJSONValue::DATETIME: {
      std::string r = "\"";
      fmtDateTimeVal(v->timestamp, v->datetime_ms, r, v->tz_offset_min);
      r += '"';
      return r;
    }
    default: return "null";
  }
}

inline void toonNestedArrayItem(const asvJSONValue* v, std::string& out, int indent, int depth);
inline void valToToon(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth);

// Spec (toon-format/spec) §9.2/§9.4: a nested array rendered as a list item.
//   - empty inner array:  "- [0]:"
//   - primitive inner array: "- [M]: v1,v2,..."  (§9.2)
//   - non-uniform / array of objects: "- [M]:" header, then nested items at
//     depth +1 relative to the hyphen line (§9.4). Tabular form is not
//     available in list-item position, so nested objects/arrays use list form.
inline void toonNestedArrayItem(const asvJSONValue* v, std::string& out, int indent, int depth) {
  if (!v || v->type != asvJSONValue::ARRAY || v->size() == 0) {
    out += std::string(static_cast<size_t>(indent) * 2, ' ') + "- [0]:\n";
    return;
  }
  bool allPrim = true;
  for (size_t i = 0; i < v->size(); i++) {
    auto* e = v->get(i);
    if (e && (e->type == asvJSONValue::OBJECT || e->type == asvJSONValue::ARRAY ||
              e->type == asvJSONValue::BINARY || e->type == asvJSONValue::EXTENSION ||
              e->type == asvJSONValue::OBJECTID || e->type == asvJSONValue::REGEX ||
              e->type == asvJSONValue::TIMESTAMP)) { allPrim = false; break; }
  }
  std::string pad(static_cast<size_t>(indent) * 2, ' ');
  std::string head = pad + "- [" + std::to_string(v->size()) + "]:";
  if (allPrim) {
    out += head + " ";
    for (size_t i = 0; i < v->size(); i++) {
      if (i > 0) out += ",";
      out += toonLeafVal(v->get(i));
    }
    out += "\n";
    return;
  }
  out += head + "\n";
  std::string cpad(static_cast<size_t>(indent + 1) * 2, ' ');
  for (size_t i = 0; i < v->size(); i++) {
    auto* e = v->get(i);
    if (!e) continue;
    if (e->type == asvJSONValue::OBJECT) {
      out += cpad + "-\n";
      for (const auto& [k, sub] : *(e->obj))
        valToToon(sub.get(), out, indent + 2, k, depth + 1);
    } else if (e->type == asvJSONValue::ARRAY) {
      toonNestedArrayItem(e, out, indent + 1, depth + 1);
    } else {
      out += cpad + "- " + toonLeafVal(e) + "\n";
    }
  }
}

inline void valToToon(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth = 0) {
  if (!v) return;
  if (!asvJSONValue::checkNestingDepth(depth)) return;
  // An EMPTY key is only the document root when nothing wraps it; an object
  // member whose key is the empty string ("" ) must still be emitted as a
  // quoted key or the round-trip loses it.
  const bool isRoot = key.empty() && depth == 0;
  std::string pad(static_cast<size_t>(indent) * 2, ' ');
  if (v->type == asvJSONValue::OBJECT) {
    if (v->obj->empty()) {
      if (!isRoot) out += pad + toonQuoteVal(key) + ": {}\n";
      else out += pad + "{}\n";
      return;
    }
    if (!isRoot) out += pad + toonQuoteVal(key) + ":\n";
    int childIndent = indent + (isRoot ? 0 : 1);
    for (const auto& [k, child] : *(v->obj))
      valToToon(child.get(), out, childIndent, k, depth + 1);
  } else if (v->type == asvJSONValue::ARRAY) {
    if (v->size() == 0) {
      if (!isRoot) out += pad + toonQuoteVal(key) + ": []\n";
      else out += "[]\n";
      return;
    }
    // Spec §9.1: length-bearing header "key[N]:" for non-empty arrays. All
    // primitive members render inline on the header line ("key[N]: v1,v2");
    // otherwise the header opens a list whose items nest at indent + 1 (§9.4).
    bool allPrim = true;
    for (size_t i = 0; i < v->size(); i++) {
      auto* e = v->get(i);
      if (e && (e->type == asvJSONValue::OBJECT || e->type == asvJSONValue::ARRAY ||
                e->type == asvJSONValue::BINARY || e->type == asvJSONValue::EXTENSION ||
                e->type == asvJSONValue::OBJECTID || e->type == asvJSONValue::REGEX ||
                e->type == asvJSONValue::TIMESTAMP)) { allPrim = false; break; }
    }
    std::string head = pad + (isRoot ? "" : toonQuoteVal(key)) + "[" + std::to_string(v->size()) + "]:";
    if (allPrim) {
      out += head + " ";
      for (size_t i = 0; i < v->size(); i++) {
        if (i > 0) out += ",";
        out += toonLeafVal(v->get(i));
      }
      out += "\n";
      return;
    }
    out += head + "\n";
    std::string cpad(static_cast<size_t>(indent + 1) * 2, ' ');
    for (size_t i = 0; i < v->size(); i++) {
      auto* child = v->get(i);
      if (!child) continue;
      if (child->type == asvJSONValue::OBJECT) {
        out += cpad + "-\n";
        for (const auto& [k, sub] : *(child->obj))
          valToToon(sub.get(), out, indent + 2, k, depth + 1);
      } else if (child->type == asvJSONValue::ARRAY) {
        toonNestedArrayItem(child, out, indent + 1, depth + 1);
      } else {
        out += cpad + "- " + toonLeafVal(child) + "\n";
      }
    }
  } else {
    if (!isRoot)
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

// Recognizes a nested-array list-item header: "- [M]:" or "- [M]" (M digits,
// no leading zeros; "0" only canonical zero). Distinguishes it from the
// backward-compat JSON literal form "- [1,2]": a literal has a ',' before the
// closing ']' or content after the ']' other than ':'.
inline bool toonParseListArrayHeader(std::string_view val, size_t& len) {
  if (val.size() < 3 || val[0] != '[') return false;
  size_t i = 1;
  if (i >= val.size() || val[i] < '0' || val[i] > '9') return false;
  size_t n = 0;
  if (val[i] == '0') {
    n = 0; i++;
  } else {
    while (i < val.size() && val[i] >= '0' && val[i] <= '9') {
      if (n > 1000000000ULL / 10) return false;
      n = n * 10 + static_cast<size_t>(val[i] - '0');
      i++;
    }
  }
  if (i >= val.size() || val[i] != ']') return false;
  i++;
  if (i < val.size() && val[i] != ':') return false;
  len = n;
  return true;
}

// Finds the index of the first occurrence of `target` that is not inside a
// double-quoted, escape-aware string within `s`. Returns npos if absent.
inline size_t toonFindFirstUnquoted(std::string_view s, char target) {
  bool inQ = false, esc = false;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (esc) { esc = false; continue; }
    if (c == '\\' && inQ) { esc = true; continue; }
    if (c == '"') { inQ = !inQ; continue; }
    if (c == target && !inQ) return i;
  }
  return std::string_view::npos;
}

// Decodes a TOON key token (optionally double-quoted) into the JSON-escaped
// text BETWEEN quotes. A quoted token's inner text is already JSON-escaped
// (the encoder wrote it verbatim via appendJsonEscaped), so it is passed
// through unchanged -- re-escaping would double-escape \n, \" etc. into
// literal backslashes. Bare tokens are JSON-escaped here.
inline std::string toonDecodeKey(std::string_view key) {
  if (key.size() >= 2 && key.front() == '"' && key.back() == '"')
    return std::string(key.substr(1, key.size() - 2));
  return toonJsonEscape(std::string(key));
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

    // array header: [optional key][N][{fields}]:  (spec §9.1-§9.4, §10)
    // The header's first unquoted ':' must occur after the [N]{fields} segment,
    // and an unquoted '[' must appear before any such ':'. This generalizes the
    // old root-only "content[0]=='['" check to also cover named "key[N]:" headers.
    {
      size_t firstColon = toonFindFirstUnquoted(content, ':');
      size_t lb = toonFindFirstUnquoted(content, '[');
      // Skip list items ("- [...]") here -- they hold their own "- [M]:" nested
      // header handling in the list-item branch below.
      bool isListItem = !content.empty() && content[0] == '-';
      if (lb != std::string_view::npos && !isListItem && (firstColon == std::string_view::npos || lb < firstColon)) {
        std::string_view keyRaw = content.substr(0, lb);
        while (!keyRaw.empty() && (keyRaw.back() == ' ' || keyRaw.back() == '\t')) keyRaw = keyRaw.substr(0, keyRaw.size() - 1);
        size_t rb = content.find(']', lb + 1);
        bool okArrLen = false;
        size_t arrLen = 0;
        if (rb != std::string_view::npos && rb > lb + 1) {
          std::string_view num = content.substr(lb + 1, rb - lb - 1);
          bool digits = !num.empty() && !(num[0] == '0' && num.size() > 1);
          size_t acc = 0;
          for (char c : num) {
            if (c < '0' || c > '9') { digits = false; break; }
            acc = acc * 10 + static_cast<size_t>(c - '0');
            if (acc > 1000000000) { digits = false; break; }
          }
          if (digits) { arrLen = acc; okArrLen = true; }
        }
        if (okArrLen) {
          size_t afterB = rb + 1;
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
            // If this header is the already-open root array with an empty key,
            // do NOT open another '['/frame -- fill the existing root frame.
            bool isRootHeader = stack.back().isRoot && stack.back().type == 'A' && keyRaw.empty();
            size_t afterColon = afterB + 1;
            while (afterColon < content.size() && (content[afterColon] == ' ' || content[afterColon] == '\t')) afterColon++;
            bool hasInline = false;
            for (size_t p = afterColon; p < content.size(); p++)
              if (content[p] != ' ' && content[p] != '\t') { hasInline = true; break; }

            if (!isRootHeader) {
              addComma(stack, out);
              if (!stack.empty()) stack.back().first = false;
              if (!keyRaw.empty()) out += '"' + toonDecodeKey(keyRaw) + "\":";
              out += '[';
              stack.push_back({'A', true, indent, true, false});
            }

            if (hasFields) {
              // tabular - read subsequent indented lines as rows
              size_t rowCount = 0;
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
                  out += '"' + toonDecodeKey(fields[fi]) + "\":" + toonJsonQuoteBare(vals[fi]);
                }
                out += '}';
                rowCount++;
                li = rl; // consume this row so the outer loop doesn't re-read it
              }
              if (rowCount != arrLen)
                throw asvJSONError("TOON: tabular array length mismatch (declared " + std::to_string(arrLen) + ", got " + std::to_string(rowCount) + ")");
              if (!isRootHeader) { out += ']'; stack.pop_back(); }
              continue;
            } else if (hasInline) {
              // inline (primitive) array cells
              size_t cellCount = 0;
              auto inlineVals = toonSplitCommas(content.substr(afterColon));
              for (auto& v : inlineVals) {
                size_t vs = 0; while (vs < v.size() && v[vs] == ' ') vs++;
                size_t ve = v.size(); while (ve > vs && v[ve-1] == ' ') ve--;
                if (vs == ve) continue;
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += toonJsonQuoteBare(v.substr(vs, ve - vs));
                cellCount++;
              }
              if (cellCount != arrLen)
                throw asvJSONError("TOON: array length mismatch (declared " + std::to_string(arrLen) + ", got " + std::to_string(cellCount) + ")");
              if (!isRootHeader) { out += ']'; stack.pop_back(); }
              continue;
            } else if (arrLen == 0) {
              // empty array header (e.g. "key[0]:" / "[0]:")
              if (!isRootHeader) { out += ']'; stack.pop_back(); }
              // root: its '[' is closed by the EOF pass
              continue;
            } else {
              // list form: frame left open; deeper "- value" items fill it.
              continue;
            }
          }
        }
      }
    }

    // list item: - value
    if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
      addComma(stack, out);
      curFrame.first = false;
      std::string_view val = content.substr(2);
      // nested array header form "- [M]: ..." / "- [M]" (spec §9.2/§9.4)
      size_t arrLen = 0;
      if (toonParseListArrayHeader(val, arrLen)) {
        std::string_view rest = val;
        size_t lb = rest.find('[');
        size_t rb = rest.find(']', lb);
        std::string_view after = rest.substr(rb + 1); // starts with ':' or empty
        out += '[';
        if (!after.empty() && after.size() > 1) {
          // inline primitive array: "- [M]: v1,v2,..."
          size_t ac = 1;
          while (ac < after.size() && after[ac] == ' ') ac++;
          std::string_view inlineContent = after.substr(ac);
          auto cells = toonSplitCommas(inlineContent);
          size_t cellCount = 0;
          for (auto& c0 : cells) {
            std::string c(c0);
            size_t cs = 0; while (cs < c.size() && c[cs] == ' ') cs++;
            size_t ce = c.size(); while (ce > cs && c[ce-1] == ' ') ce--;
            if (cs == ce) continue;
            if (cellCount > 0) out += ',';
            out += toonJsonQuoteBare(c.substr(cs, ce - cs));
            cellCount++;
          }
          if (cellCount != arrLen)
            throw asvJSONError("TOON: nested array length mismatch (declared " + std::to_string(arrLen) + ", got " + std::to_string(cellCount) + ")");
          out += ']';
        } else if (arrLen == 0) {
          // empty inner array "- [0]:" -> []
          out += ']';
        } else {
          // non-primitive list-form opener: leave frame open
          stack.push_back({'A', true, indent, true, false});
        }
        continue;
      }
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

    // key: value  (first ':' OUTSIDE any quoted key is the separator)
    size_t colonPos = toonFindFirstUnquoted(content, ':');
    if (colonPos == std::string_view::npos) colonPos = content.find(':');
    if (colonPos != std::string_view::npos) {
      std::string_view key = content.substr(0, colonPos);
      while (!key.empty() && key.back() == ' ') key = key.substr(0, key.size() - 1);

      std::string keyStr = toonDecodeKey(key);

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
  formatType = FormatType::Unknown;
  std::string json;
  try {
    json = toonToJson(input);
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
  bool ok = parse(std::string_view(json));
  if (ok) formatType = FormatType::TOON;
  return ok;
}

} // namespace asvJSONInternal
