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

  for (size_t i = 0; i < input.size(); i++) {
    char c = input[i];

    if (str == DOUBLE) {
      out += c;
      if (c == '\\' && i + 1 < input.size()) { out += input[++i]; }
      else if (c == '"') { str = NONE; }
      continue;
    }

    if (str == SINGLE) {
      if (c == '\\' && i + 1 < input.size()) {
        char n = input[++i];
        if (n == '\'') out += '\'';
        else { out += '\\'; out += n; }
      } else if (c == '\'') {
        out += '"';
        str = NONE;
      } else {
        if (c == '"') out += "\\\"";
        else out += c;
      }
      continue;
    }

    if (c == '"') { out += c; str = DOUBLE; continue; }
    if (c == '\'') { out += '"'; str = SINGLE; continue; }

    // Trailing comma (skips comments between comma and bracket)
    if (c == ',') {
      size_t j = i + 1;
      bool isTrailing = false;
      while (j < input.size()) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r') { j++; continue; }
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

    // Unquoted key
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
      size_t start = i;
      while (i < input.size() && (std::isalnum(static_cast<unsigned char>(input[i])) || input[i] == '_' || input[i] == '$')) i++;
      std::string_view ident(input.data() + start, i - start);
      i--;
      bool isKey = false;
      for (size_t j = i + 1; j < input.size(); j++) {
        char nc = input[j];
        if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r') continue;
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

    // Plus sign on numbers
    if (c == '+' && i + 1 < input.size() && (std::isdigit(static_cast<unsigned char>(input[i + 1])) || input[i + 1] == '.')) {
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
