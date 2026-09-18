#pragma once
// Dotenv (.env) serialization/parsing for asvJSON++.
//
// Dotenv is a flat KEY=VALUE configuration format used for environment
// variables:
//
//   # comment
//   export APP_NAME="My App"
//   PORT=8080
//   DEBUG=true
//
// Bare values are type-inferred (null/bool/int/double/datetime/string) exactly
// like INI; single-quoted values are always literal strings; double-quoted
// values process C-style escapes and may span multiple lines. Inline comments
// (` # ...`) are stripped from bare values. Duplicate keys follow last-wins.
//
// Nested objects and arrays have no dotenv syntax: toDotenv() writes their
// compact JSON text as a quoted string (a documented lossy round-trip -- the
// text survives but decodes back as a string).

#include "../core.hpp"

#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace asvJSONInternal {

inline std::string_view dotenvTrimL(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++;
  return s.substr(i);
}
inline std::string_view dotenvTrimR(std::string_view s) {
  size_t e = s.size();
  while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
  return s.substr(0, e);
}
inline std::string_view dotenvTrim(std::string_view s) { return dotenvTrimR(dotenvTrimL(s)); }

// Infer a typed value from a bare dotenv string (quoted values stay strings).
// Mirrors the INI convention: JSON-shaped numbers only, leading-zero/hex
// literals kept as text, out-of-range numbers preserved as raw_number strings.
inline std::unique_ptr<asvJSONValue> dotenvParseVal(const std::string& s, bool quoted) {
  if (quoted) return asvJSONValue::makeString(s.data(), s.size());
  if (s == "null") return asvJSONValue::makeNull();
  if (s == "true" || s == "True" || s == "TRUE") return asvJSONValue::makeBool(true);
  if (s == "false" || s == "False" || s == "FALSE") return asvJSONValue::makeBool(false);
  {
    time_t ts = 0; int ms = 0; int tz = 0;
    if (tryParseDateTime(s, ts, &ms, &tz))
      return asvJSONValue::makeDateTime(ts, ms, tz);
  }
  if (!s.empty()) {
    size_t i0 = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (s.size() > i0 + 1 && s[i0] == '0' && s[i0 + 1] >= '0' && s[i0 + 1] <= '9')
      return asvJSONValue::makeString(s.data(), s.size());
    if (s.size() > i0 + 1 && s[i0] == '0' && (s[i0 + 1] == 'x' || s[i0 + 1] == 'X'))
      return asvJSONValue::makeString(s.data(), s.size());
    errno = 0;
    char* end = nullptr;
    long long ll = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == 0 && end == s.c_str() + s.size()) {
      if (errno == ERANGE) {
        auto v = asvJSONValue::makeStringView(std::string_view(s));
        if (v) v->raw_number = true;
        return v;
      }
      return asvJSONValue::makeInt(static_cast<int64_t>(ll));
    }
    errno = 0;
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == 0 && end == s.c_str() + s.size()) {
      if (errno == ERANGE) return asvJSONValue::makeStringView(std::string_view(s));
      return asvJSONValue::makeDouble(d);
    }
  }
  return asvJSONValue::makeString(s.data(), s.size());
}

// Process C-style escapes inside a double-quoted dotenv value.
inline std::string dotenvUnescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char e = s[++i];
      switch (e) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case '\'': out += '\''; break;
        case '$': out += '$'; break;
        default: out += '\\'; out += e; break;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

inline std::string dotenvEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      default: out += c; break;
    }
  }
  return out;
}

inline bool dotenvKeyCharOk(char c) {
  return c != ' ' && c != '\t' && c != '\r' && c != '\n' &&
         c != '=' && c != '#' && c != '"' && c != '\'';
}

inline void dotenvWriteCommentLines(std::string& out, const std::vector<Comment>& list) {
  for (const auto& c : list) {
    std::string_view text(c.text);
    size_t start = 0;
    for (;;) {
      size_t nl = text.find('\n', start);
      std::string_view seg = text.substr(start, nl == std::string_view::npos ? text.size() - start : nl - start);
      out += "# ";
      out += seg;
      out += '\n';
      if (nl == std::string_view::npos) break;
      start = nl + 1;
    }
  }
}

inline void dotenvFormatVal(const asvJSONValue* v, std::string& out) {
  using T = asvJSONValue::Type;
  switch (v->type) {
    case T::STRING: {
      std::string_view sv(v->str_data);
      bool needQuote = sv.empty();
      for (char c : sv) {
        if (c == ' ' || c == '\t' || c == '#' || c == '=' || c == '"' ||
            c == '\'' || c == '\\' || c == ';' || c == '\n' || c == '\r') {
          needQuote = true;
          break;
        }
      }
      if (!needQuote && !v->raw_number) {
        auto bare = dotenvParseVal(std::string(sv), false);
        if (bare && bare->type != T::STRING) needQuote = true;
      }
      if (needQuote) {
        out += '"';
        out += dotenvEscape(sv);
        out += '"';
      } else {
        out += sv;
      }
      break;
    }
    case T::INT: out += std::to_string(v->num); break;
    case T::DOUBLE: {
      std::string tmp;
      fmtDoubleVal(v->dbl, tmp);
      if (tmp.find('.') == std::string::npos && tmp.find('e') == std::string::npos &&
          tmp.find('E') == std::string::npos)
        tmp += ".0";
      out += tmp;
      break;
    }
    case T::BOOL_VAL: out += v->flag ? "true" : "false"; break;
    case T::NULL_VAL: out += "null"; break;
    case T::DATETIME: {
      std::string s;
      fmtDateTimeVal(v->timestamp, v->datetime_ms, s, v->tz_offset_min);
      out += '"';
      out += dotenvEscape(s);
      out += '"';
      break;
    }
    case T::BINARY:
    case T::EXTENSION: {
      out += '"';
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      out += '"';
      break;
    }
    default: {
      // Objects, arrays and other special types have no dotenv syntax: emit
      // their compact JSON text as a quoted string.
      std::string s;
      v->serialize(s, false);
      out += '"';
      out += dotenvEscape(s);
      out += '"';
      break;
    }
  }
}

inline void asvJSONValue::toDotenv(std::string& out) const {
  if (type != OBJECT || !obj)
    throw asvJSONError("dotenv: root must be an object");
  for (const auto& [key, val] : *obj) {
    if (key.empty())
      throw asvJSONError("dotenv: empty key");
    for (char c : key)
      if (!dotenvKeyCharOk(c))
        throw asvJSONError("dotenv: invalid character in key \"" + key + "\"");
    if (val->comments && !val->comments->leading.empty())
      dotenvWriteCommentLines(out, val->comments->leading);
    out += key;
    out += '=';
    dotenvFormatVal(val.get(), out);
    if (val->comments && !val->comments->trailing.empty()) {
      auto it = val->comments->trailing.rbegin();
      if (!it->block && it->text.find('\n') == std::string::npos) {
        out += " #";
        out += it->text;
        out += '\n';
      } else {
        out += '\n';
        dotenvWriteCommentLines(out, val->comments->trailing);
      }
    } else {
      out += '\n';
    }
  }
  if (comments && !comments->footing.empty())
    dotenvWriteCommentLines(out, comments->footing);
}

inline std::string asvJSON::toDotenv() const {
  std::string out;
  if (root) root->toDotenv(out);
  return out;
}

inline bool asvJSON::fromDotenv(std::string_view input) {
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    if (input.size() >= 3 && (unsigned char)input[0] == 0xEF &&
        (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
      input = input.substr(3);

    auto obj = asvJSONValue::makeObject();
    if (!obj) throw asvJSONError("out of memory");

    const size_t npos = std::string_view::npos;
    std::vector<std::string> lines = splitLines(input);
    const size_t n = lines.size();
    std::vector<Comment> pending;

    for (size_t li = 0; li < n; li++) {
      std::string_view line = dotenvTrim(lines[li]);
      if (line.empty()) continue;
      if (line[0] == '#') {
        Comment c;
        c.text.assign(line.data() + 1, line.size() - 1);
        pending.push_back(std::move(c));
        continue;
      }
      std::string_view body = line;
      if (body.size() >= 7 && body.compare(0, 6, "export") == 0 &&
          (body[6] == ' ' || body[6] == '\t'))
        body = dotenvTrimL(body.substr(6));

      size_t eq = body.find('=');
      if (eq == npos)
        throw asvJSONError("dotenv: expected '=' at line " + std::to_string(li + 1));
      std::string key(dotenvTrim(body.substr(0, eq)));
      if (key.empty())
        throw asvJSONError("dotenv: empty key at line " + std::to_string(li + 1));
      for (char c : key)
        if (!dotenvKeyCharOk(c))
          throw asvJSONError("dotenv: invalid character in key at line " + std::to_string(li + 1));

      std::string_view rest = dotenvTrimL(body.substr(eq + 1));
      std::unique_ptr<asvJSONValue> val;
      std::vector<Comment> trailing;

      if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) {
        char q = rest[0];
        std::string raw;
        bool closed = false;
        std::string_view cur = rest.substr(1);
        for (;;) {
          size_t i = 0;
          for (; i < cur.size(); i++) {
            char c = cur[i];
            if (q == '"' && c == '\\' && i + 1 < cur.size()) {
              raw += c;
              raw += cur[i + 1];
              i++;
              continue;
            }
            if (c == q) { closed = true; break; }
            raw += c;
          }
          if (closed) {
            std::string_view tail = dotenvTrim(cur.substr(i + 1));
            if (!tail.empty() && tail[0] != '#')
              throw asvJSONError("dotenv: trailing content after quoted value at line " +
                                 std::to_string(li + 1));
            if (!tail.empty()) {
              Comment c;
              c.text.assign(tail.data() + 1, tail.size() - 1);
              trailing.push_back(std::move(c));
            }
            break;
          }
          if (li + 1 >= n)
            throw asvJSONError("dotenv: unterminated quoted value at line " +
                               std::to_string(li + 1));
          raw += '\n';
          li++;
          cur = lines[li];
        }
        std::string text = (q == '"') ? dotenvUnescape(raw) : raw;
        val = asvJSONValue::makeString(text.data(), text.size());
        if (!val)
          throw asvJSONError("dotenv: string too long at line " + std::to_string(li + 1));
      } else {
        std::string_view v = rest;
        size_t cut = npos;
        for (size_t i = 0; i < v.size(); i++) {
          if (v[i] == '#' && (i == 0 || v[i - 1] == ' ' || v[i - 1] == '\t')) {
            cut = i;
            break;
          }
        }
        std::string_view valsv = (cut == npos) ? v : v.substr(0, cut);
        valsv = dotenvTrimR(valsv);
        if (cut != npos) {
          std::string_view ct = v.substr(cut + 1);
          Comment c;
          c.text.assign(ct.data(), ct.size());
          trailing.push_back(std::move(c));
        }
        val = dotenvParseVal(std::string(valsv), false);
        if (!val)
          throw asvJSONError("dotenv: value too long at line " + std::to_string(li + 1));
      }

      if (!pending.empty() || !trailing.empty()) {
        val->comments.reset(new Comments());
        if (!pending.empty()) {
          val->comments->leading.swap(pending);
          pending.clear();
        }
        if (!trailing.empty()) val->comments->trailing = std::move(trailing);
      }

      auto it = obj->obj->find(key);
      if (it == obj->obj->end()) obj->obj->emplace(key, std::move(val));
      else it->second = std::move(val);
    }

    if (!pending.empty()) {
      obj->comments.reset(new Comments());
      obj->comments->footing.swap(pending);
    }

    root = std::move(obj);
    formatType = FormatType::Dotenv;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
