#pragma once
// JSON5 serialization/parsing for asvJSON++
//
// JSON5 is a superset of JSON that adds:
//   - Unquoted object keys: {key: "value"}
//   - Single-quoted strings: 'value'
//   - Trailing commas: [1,2,3,]
//   - Hex numbers: 0xFF
//   - Octal numbers: 0o77
//   - Binary numbers: 0b1010
//   - Leading decimal: .5
//   - Plus sign: +42
//   - Comments // /* */ # (already handled by JSON parser)

#include "../core.hpp"

namespace asvJSONInternal {

static std::string json5ToJson(std::string_view input) {
  std::string out;
  out.reserve(input.size() + input.size() / 8);

  enum StrState { NONE, SINGLE, DOUBLE };
  StrState str = NONE;

  auto isIdStart = [](char ch) noexcept { auto u = static_cast<unsigned char>(ch); return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || ch == '_' || ch == '$' || u >= 0xC0; };
  auto isIdCont = [](char ch) noexcept { auto u = static_cast<unsigned char>(ch); return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || ch == '_' || ch == '$' || u >= 0x80; };

  // Hex digit value (0-15) or -1 for invalid
  auto hexVal = [](char c) -> int { auto u = static_cast<unsigned char>(c); if (u >= '0' && u <= '9') return u - '0'; if (u >= 'a' && u <= 'f') return u - 'a' + 10; if (u >= 'A' && u <= 'F') return u - 'A' + 10; return -1; };

  // Escape control characters (< 0x20) for strict JSON
  auto escapeControl = [&out](char c) {
    if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c == '\b') out += "\\b";
    else if (c == '\f') out += "\\f";
    else { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c)); out += buf; }
  };

  for (size_t i = 0; i < input.size(); i++) {
    char c = input[i];

    if (str == DOUBLE) {
      if (c == '\\' && i + 1 < input.size()) {
        char n = input[i + 1];
        if (n == '\n') { i++; }
        else if (n == '\r') { i++; if (i + 1 < input.size() && input[i + 1] == '\n') i++; }
        else if (n == 'v') { out += "\\u000B"; i++; }
        else if (n == '0') {
          if (i + 2 < input.size() && std::isdigit(static_cast<unsigned char>(input[i + 2])))
            throw asvJSONError("octal escape not allowed in JSON5");
          out += "\\u0000"; i++;
        }
        else if (n == 'x') {
          if (i + 3 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 2])) || !std::isxdigit(static_cast<unsigned char>(input[i + 3])))
            throw asvJSONError("invalid \\x escape in JSON5");
          out += "\\u00"; out += input[i + 2]; out += input[i + 3]; i += 3;
        }
        else if (n == 'u') {
          if (i + 2 < input.size() && input[i + 2] == '{') {
            size_t hexStart = i + 3;
            size_t hexEnd = hexStart;
            while (hexEnd < input.size() && std::isxdigit(static_cast<unsigned char>(input[hexEnd]))) hexEnd++;
            if (hexEnd == hexStart || hexEnd >= input.size() || input[hexEnd] != '}')
              throw asvJSONError("invalid \\u{...} escape in JSON5");
            unsigned long long cp = 0;
            for (size_t k = hexStart; k < hexEnd; k++) cp = (cp << 4) | static_cast<unsigned>(hexVal(input[k]));
            if (cp > 0x10FFFF) throw asvJSONError("codepoint too large in \\u{...} escape");
            if (cp < 0x10000) {
              char b[8]; snprintf(b, sizeof(b), "\\u%04X", static_cast<unsigned>(cp)); out += b;
            } else {
              unsigned hi = 0xD800 + ((static_cast<unsigned>(cp) - 0x10000) >> 10);
              unsigned lo = 0xDC00 + ((static_cast<unsigned>(cp) - 0x10000) & 0x3FF);
              char b[14]; snprintf(b, sizeof(b), "\\u%04X\\u%04X", hi, lo); out += b;
            }
            i = hexEnd;
          } else {
            if (i + 5 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 2])) || !std::isxdigit(static_cast<unsigned char>(input[i + 3])) || !std::isxdigit(static_cast<unsigned char>(input[i + 4])) || !std::isxdigit(static_cast<unsigned char>(input[i + 5])))
              throw asvJSONError("invalid \\u escape in JSON5");
            out += "\\u"; out += input[i + 2]; out += input[i + 3]; out += input[i + 4]; out += input[i + 5];
            i += 5;
          }
        }
        else if (n == '\'') { out += '\''; i++; }
        else { out += '\\'; out += n; i++; }
      } else {
        if (static_cast<unsigned char>(c) < 0x20) escapeControl(c);
        else out += c;
        if (c == '"') str = NONE;
      }
      continue;
    }

    if (str == SINGLE) {
      if (c == '\\' && i + 1 < input.size()) {
        char n = input[++i];
        if (n == '\n') { /* line continuation: skip */ }
        else if (n == '\r') { if (i + 1 < input.size() && input[i + 1] == '\n') i++; }
        else if (n == 'v') { out += "\\u000B"; }
        else if (n == '0') {
          if (i + 1 < input.size() && std::isdigit(static_cast<unsigned char>(input[i + 1])))
            throw asvJSONError("octal escape not allowed in JSON5");
          out += "\\u0000";
        }
        else if (n == 'x') {
          if (i + 2 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 1])) || !std::isxdigit(static_cast<unsigned char>(input[i + 2])))
            throw asvJSONError("invalid \\x escape in JSON5");
          out += "\\u00"; out += input[i + 1]; out += input[i + 2]; i += 2;
        }
        else if (n == 'u') {
          if (i + 1 < input.size() && input[i + 1] == '{') {
            size_t hexStart = i + 2;
            size_t hexEnd = hexStart;
            while (hexEnd < input.size() && std::isxdigit(static_cast<unsigned char>(input[hexEnd]))) hexEnd++;
            if (hexEnd == hexStart || hexEnd >= input.size() || input[hexEnd] != '}')
              throw asvJSONError("invalid \\u{...} escape in JSON5");
            unsigned long long cp = 0;
            for (size_t k = hexStart; k < hexEnd; k++) cp = (cp << 4) | static_cast<unsigned>(hexVal(input[k]));
            if (cp > 0x10FFFF) throw asvJSONError("codepoint too large in \\u{...} escape");
            if (cp < 0x10000) {
              char b[8]; snprintf(b, sizeof(b), "\\u%04X", static_cast<unsigned>(cp)); out += b;
            } else {
              unsigned hi = 0xD800 + ((static_cast<unsigned>(cp) - 0x10000) >> 10);
              unsigned lo = 0xDC00 + ((static_cast<unsigned>(cp) - 0x10000) & 0x3FF);
              char b[14]; snprintf(b, sizeof(b), "\\u%04X\\u%04X", hi, lo); out += b;
            }
            i = hexEnd;
          } else {
            if (i + 4 >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i + 1])) || !std::isxdigit(static_cast<unsigned char>(input[i + 2])) || !std::isxdigit(static_cast<unsigned char>(input[i + 3])) || !std::isxdigit(static_cast<unsigned char>(input[i + 4])))
              throw asvJSONError("invalid \\u escape in JSON5");
            out += "\\u";
            out += input[i + 1]; out += input[i + 2]; out += input[i + 3]; out += input[i + 4];
            i += 4;
          }
        }
        else if (n == '\'') { out += '\''; }
        else if (n == '"') { out += "\\\""; }
        else { out += '\\'; out += n; }
      } else if (c == '\'') {
        out += '"';
        str = NONE;
      } else {
        if (static_cast<unsigned char>(c) < 0x20) escapeControl(c);
        else {
          if (c == '"') out += "\\\"";
          else out += c;
        }
      }
      continue;
    }

    if (c == '"') { out += c; str = DOUBLE; continue; }
    if (c == '\'') { out += '"'; str = SINGLE; continue; }

    // Control character check (outside strings)
    if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != '\v' && c != '\f')
      throw asvJSONError("control character not allowed in JSON5");

    // Extended whitespace (JSON5 Sec 8)
    if (c == '\v' || c == '\f') continue;
    if (static_cast<unsigned char>(c) == 0xC2 && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xA0) { i++; continue; }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(input[i + 2]) == 0xA8 || static_cast<unsigned char>(input[i + 2]) == 0xA9)) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
        static_cast<unsigned char>(input[i + 2]) >= 0x80 && static_cast<unsigned char>(input[i + 2]) <= 0x8A) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x81 &&
        static_cast<unsigned char>(input[i + 2]) == 0x9F) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xE3 && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0x80 &&
        static_cast<unsigned char>(input[i + 2]) == 0x80) { i += 2; continue; }
    if (static_cast<unsigned char>(c) == 0xEF && i + 2 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xBB &&
        static_cast<unsigned char>(input[i + 2]) == 0xBF) { i += 2; continue; }

    // Trailing comma (skips comments between comma and bracket)
    if (c == ',') {
      size_t j = i + 1;
      bool isTrailing = false;
      while (j < input.size()) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r' || nc == '\v' || nc == '\f') { j++; continue; }
        auto unc = static_cast<unsigned char>(nc);
        if (unc == 0xC2 && j + 1 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xA0) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[j + 2]) == 0xA8 || static_cast<unsigned char>(input[j + 2]) == 0xA9)) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) >= 0x80 && static_cast<unsigned char>(input[j + 2]) <= 0x8A) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x81 &&
            static_cast<unsigned char>(input[j + 2]) == 0x9F) { j += 3; continue; }
        if (unc == 0xE3 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) == 0x80) { j += 3; continue; }
        if (unc == 0xEF && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xBB &&
            static_cast<unsigned char>(input[j + 2]) == 0xBF) { j += 3; continue; }
        if (nc == '/' && j + 1 < input.size()) {
          if (input[j + 1] == '/') { while (j < input.size() && input[j] != '\n') j++; continue; }
          if (input[j + 1] == '*') { j += 2; while (j + 1 < input.size() && !(input[j] == '*' && input[j + 1] == '/')) j++; if (j < input.size()) j += 2; continue; }
        }
        if (nc == '#') { while (j < input.size() && input[j] != '\n') j++; continue; }
        if (nc == '}' || nc == ']') isTrailing = true;
        break;
      }
      if (isTrailing) { i = j - 1; continue; } // skip comma, let outer loop hit }
      out += ',';
      continue;
    }

    // Unquoted key (supports Unicode identifiers)
    if (isIdStart(c)) {
      size_t start = i;
      while (i < input.size() && isIdCont(input[i])) i++;
      std::string_view ident(input.data() + start, i - start);
      // Validate identifier characters
      for (size_t k = 0; k < ident.size(); k++) {
        auto uc = static_cast<unsigned char>(ident[k]);
        if (uc <= 0x20 || uc == 0x7F || uc == ':' || uc == '{' || uc == '}' || uc == '[' || uc == ']' || uc == ',' || uc == '"' || uc == '\'' || uc == '\\' || uc == '/' || uc == '#')
          throw asvJSONError("invalid character in JSON5 identifier");
      }
      i--;
      bool isKey = false;
      for (size_t j = i + 1; j < input.size(); j++) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r' || nc == '\v' || nc == '\f') continue;
        auto unc = static_cast<unsigned char>(nc);
        if (unc == 0xC2 && j + 1 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xA0) { j++; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[j + 2]) == 0xA8 || static_cast<unsigned char>(input[j + 2]) == 0xA9)) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) >= 0x80 && static_cast<unsigned char>(input[j + 2]) <= 0x8A) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x81 &&
            static_cast<unsigned char>(input[j + 2]) == 0x9F) { j += 2; continue; }
        if (unc == 0xE3 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) == 0x80) { j += 2; continue; }
        if (unc == 0xEF && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xBB &&
            static_cast<unsigned char>(input[j + 2]) == 0xBF) { j += 2; continue; }
        if (nc == '/') {
          if (j + 1 < input.size() && input[j + 1] == '/') { while (j < input.size() && input[j] != '\n') j++; continue; }
          if (j + 1 < input.size() && input[j + 1] == '*') { j += 2; while (j + 1 < input.size() && !(input[j] == '*' && input[j + 1] == '/')) j++; if (j < input.size()) j += 2; continue; }
        }
        if (nc == '#') { while (j < input.size() && input[j] != '\n') j++; continue; }
        if (nc == ':') isKey = true;
        break;
      }
      if (isKey) {
        out += '"', out.append(ident.data(), ident.size()), out += '"';
      } else {
        out.append(ident.data(), ident.size());
      }
      continue;
    }

    // Hex / octal / binary numbers
    if (c == '0' && i + 1 < input.size()) {
      char n = input[i + 1];
      if ((n == 'x' || n == 'X') && i + 2 < input.size() && std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
        i += 2;
        std::string digits;
        while (i < input.size() && std::isxdigit(static_cast<unsigned char>(input[i]))) { digits += input[i++]; }
        i--;
        char* end = nullptr;
        unsigned long long val = strtoull(digits.c_str(), &end, 16);
        out += std::to_string(val);
        continue;
      }
      if ((n == 'o' || n == 'O') && i + 2 < input.size() && input[i + 2] >= '0' && input[i + 2] <= '7') {
        i += 2;
        std::string digits;
        while (i < input.size() && input[i] >= '0' && input[i] <= '7') { digits += input[i++]; }
        i--;
        char* end = nullptr;
        unsigned long long val = strtoull(digits.c_str(), &end, 8);
        out += std::to_string(val);
        continue;
      }
      if ((n == 'b' || n == 'B') && i + 2 < input.size() && (input[i + 2] == '0' || input[i + 2] == '1')) {
        i += 2;
        std::string digits;
        while (i < input.size() && (input[i] == '0' || input[i] == '1')) { digits += input[i++]; }
        i--;
        char* end = nullptr;
        unsigned long long val = strtoull(digits.c_str(), &end, 2);
        out += std::to_string(val);
        continue;
      }
    }

    // Leading decimal: .5 -> 0.5
    if (c == '.' && (i == 0 || !std::isdigit(static_cast<unsigned char>(input[i - 1]))) && i + 1 < input.size() && std::isdigit(static_cast<unsigned char>(input[i + 1]))) {
      out += '0';
      out += c;
      continue;
    }

    // Signs (+/-) with whitespace-tolerant lookahead (JSON5: - Infinity, + 5)
    if (c == '+' || c == '-') {
      size_t j = i + 1;
      while (j < input.size()) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r' || nc == '\v' || nc == '\f') { j++; continue; }
        auto unc = static_cast<unsigned char>(nc);
        if (unc == 0xC2 && j + 1 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xA0) { j += 2; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[j + 2]) == 0xA8 || static_cast<unsigned char>(input[j + 2]) == 0xA9)) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) >= 0x80 && static_cast<unsigned char>(input[j + 2]) <= 0x8A) { j += 3; continue; }
        if (unc == 0xE2 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x81 &&
            static_cast<unsigned char>(input[j + 2]) == 0x9F) { j += 3; continue; }
        if (unc == 0xE3 && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0x80 &&
            static_cast<unsigned char>(input[j + 2]) == 0x80) { j += 3; continue; }
        if (unc == 0xEF && j + 2 < input.size() && static_cast<unsigned char>(input[j + 1]) == 0xBB &&
            static_cast<unsigned char>(input[j + 2]) == 0xBF) { j += 3; continue; }
        break;
      }
      bool isNaN_ = (j + 3 <= input.size() && input.compare(j, 3, "NaN") == 0);
      bool isInf = (j + 8 <= input.size() && input.compare(j, 8, "Infinity") == 0);
      bool isNum = (j < input.size() && (std::isdigit(static_cast<unsigned char>(input[j])) || input[j] == '.'));
      if (isNaN_) { if (c == '-') out += '-'; out += "NaN"; i = j + 2; continue; }
      if (isInf) {
        if (c == '-') out += '-';
        out += "Infinity"; i = j + 7; continue;
      }
      if (c == '+') continue;
      if (isNum && j > i + 1) { out += '-'; i = j - 1; continue; }
    }

    // Trailing decimal: 5. -> 5.0, 5.e10 -> 5.0e10
    if (c == '.' && i > 0 && std::isdigit(static_cast<unsigned char>(input[i - 1])) &&
        (i + 1 >= input.size() || !std::isdigit(static_cast<unsigned char>(input[i + 1])))) {
      out += ".0";
      continue;
    }

    out += c;
  }

  if (str != NONE) throw asvJSONError("unclosed string in JSON5");
  return out;
}

static void json5SerializeVal(const asvJSONValue* v, std::string& out, int depth = 0) {
  if (!v) { out += "null"; return; }
  if (!asvJSONValue::checkNestingDepth(depth)) { out += "null"; return; }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += "null"; break;
    case asvJSONValue::BOOL_VAL: out += v->flag ? "true" : "false"; break;
    case asvJSONValue::INT: out += std::to_string(v->num); break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d)) { out += "NaN"; break; }
      if (std::isinf(d)) { out += (d > 0 ? "Infinity" : "-Infinity"); break; }
      std::string r; fmtDoubleVal(d, r); out += r;
      break;
    }
    case asvJSONValue::STRING: {
      out += '"';
      appendJsonEscaped(out, v->str_data);
      out += '"';
      break;
    }
    case asvJSONValue::OBJECT: {
      out += '{';
      bool first = true;
      for (const auto& [k, child] : *(v->obj)) {
        if (!first) out += ',';
        first = false;
        bool needsQuote = k.empty() || k.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$0123456789") != std::string::npos || (k[0] >= '0' && k[0] <= '9');
        if (needsQuote) { out += '"'; appendJsonEscaped(out, k); out += '"'; }
        else out += k;
        out += ':';
        json5SerializeVal(child.get(), out, depth + 1);
      }
      out += '}';
      break;
    }
    case asvJSONValue::ARRAY: {
      out += '[';
      for (size_t i = 0; i < v->size(); i++) {
        if (i > 0) out += ',';
        json5SerializeVal(v->get(i), out, depth + 1);
      }
      out += ']';
      break;
    }
    case asvJSONValue::DATETIME: {
      out += '"';
      fmtDateTimeVal(v->timestamp, v->datetime_ms, out);
      out += '"';
      break;
    }
    case asvJSONValue::BINARY: {
      out += "{\"$binary\":{\"base64\":\"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      out += "\",\"subType\":\"00\"}}";
      break;
    }
    case asvJSONValue::OBJECTID: {
      out += "{\"$oid\":\"";
      fmtObjectIdHexVal(v->str_data, out);
      out += "\"}";
      break;
    }
    case asvJSONValue::REGEX: {
      {
        size_t sep = v->str_data.rfind('|');
        std::string_view pattern = (sep != std::string_view::npos) ? v->str_data.substr(0, sep) : v->str_data;
        std::string_view opts = (sep != std::string_view::npos) ? v->str_data.substr(sep + 1) : "";
        out += "{\"$regex\":\"";
        appendJsonEscaped(out, pattern);
        out += "\"";
        if (!opts.empty()) {
          out += ",\"$options\":\"";
          appendJsonEscaped(out, opts);
          out += "\"";
        }
        out += "}";
      }
      break;
    }
    case asvJSONValue::TIMESTAMP: {
      out += "{\"$timestamp\":{\"t\":";
      {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
        if (ec == std::errc()) out.append(buf, ptr - buf);
      }
      out += "}}";
      break;
    }
    case asvJSONValue::EXTENSION: {
      out += "{\"$binary\":{\"base64\":\"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      char hex[3];
      snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(v->ext_type));
      out += "\",\"subType\":\"";
      out += hex;
      out += "\"}}";
      break;
    }
    default: out += "null"; break;
  }
}

inline std::string asvJSON::toJSON5() const {
  if (!root) return "null";
  std::string out;
  json5SerializeVal(root.get(), out);
  return out;
}

inline bool asvJSON::fromJSON5(std::string_view input) {
  if (input.empty()) { lastError = "empty input"; return false; }
  bool prevNaN = allowNaNInfinity;
  allowNaNInfinity = true;
  try {
    std::string json = json5ToJson(input);
    bool ok = parse(std::string_view(json));
    allowNaNInfinity = prevNaN;
    return ok;
  } catch (const asvJSONError& e) {
    allowNaNInfinity = prevNaN;
    lastError = e.what();
    return false;
  }
}

} // namespace asvJSONInternal
