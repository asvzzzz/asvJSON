#pragma once
// TOON (Token-Oriented Object Notation) serialization/parsing for asvJSON++
//
// Compact JSON-like format using 2-space indentation instead of braces:
//   key: value            -- object member (value may be a bare token or JSON)
//   - item                -- array element
//   key:                  -- nested object / array (children indented)
//   key[N]: v1,v2         -- inline primitive array with length header (Sec. 9.1)
//   key[N]: / - items     -- list-form array with length header (Sec. 9.4)
//   key[N]{"f1","f2"}:    -- tabular array; subsequent indented lines are rows (DECODE-ONLY:
//                           the encoder never emits this shape; uniform-object arrays render as
//                           list-form instead, so tabular is a one-way parse path)
// Strings are quoted only when required; full round-trip via toTOON()/fromTOON().
// Nesting past the core MAX_NESTING_DEPTH (48) is a hard error from toTOON()
// ("TOON: maximum nesting depth exceeded"), never a silent truncation.

#include "../core.hpp"

namespace asvJSONInternal {

inline std::string toonJsonEscape(const std::string& s) {
  std::string r;
  r.reserve(s.size() + 4);
  appendJsonEscaped(r, s);
  return r;
}

// Deliberately a first-character test for '"', '{' and '[' (plus a full grammar
// check for numbers and the bare literals): the encoder uses this to decide
// whether a STRING must be quoted, and "{a: 1}" / "[1,2" have to be quoted as
// strings. The decoder cannot rely on it -- it re-checks those three shapes with
// the JSON parser in toonJsonQuoteBare before passing them through.
// Loose "is this token shaped like a JSON number" test: no leading-zero rule.
// This is the ENCODER's test. It has to be looser than the real grammar, because
// a STRING whose text merely looks numeric still has to be quoted -- "007",
// "00.5" and "-01" are not JSON numbers, but emitting them bare would hand the
// JSON parser a leading zero to choke on, so they must be quoted like any other
// number-shaped string. Using the strict grammar here (as this function used to)
// quoted nothing for "007" and the round-trip broke.
//
// Kept separate from toonIsJsonValue on purpose: the two want opposite answers
// for a leading-zero token, so they cannot be one function.
inline bool toonLooksNumeric(std::string_view s) {
  if (s.empty()) return false;
  size_t i = (s.front() == '-') ? 1 : 0;
  if (i >= s.size()) return s.front() == '-';   // bare '-' is number-shaped too
  bool hasDigit = false;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') { i++; hasDigit = true; }
  if (i < s.size() && s[i] == '.') {
    i++;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { i++; hasDigit = true; }
  }
  if (hasDigit && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
  }
  if (!hasDigit) return false;
  // Anything left over (a second '.', letters) means it is not a number at all.
  return i == s.size();
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
    if (s[i] < '0' || s[i] > '9') return false;    // must start with a digit
    // JSON's int production is "zero / (digit1-9 *DIGIT)": exactly one '0', or a
    // non-zero lead. The digit run used to be consumed greedily, so "01", "00",
    // "-01" and "00.5" all passed here. On the encode side that was harmless --
    // toonValNeedsQuotes then quotes the STRING "01" and INT/DOUBLE never carry
    // a leading zero -- but the decoder shares this function, and a hand-written
    // "k: 01" would be copied through unquoted, leaving the intermediate JSON with
    // a leading zero that the JSON parser then rejects. Rejecting here keeps the
    // two sides on one grammar.
    if (s[i] == '0' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') return false;
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

inline bool toonValNeedsQuotes(const std::string& s, bool forKey) {
  if (s.empty()) return true;
  if (toonLooksNumeric(s)) return true;           // number-shaped, incl. leading-zero text like "007"
  if (toonIsJsonValue(s)) return true;             // true / false / null / "{...}" / "[...]"
  if (s.front() == '-') return true;               // leading '-' could be misread as a list item / root array
  for (char c : s) if (c == ',' || c == '\n' || c == '\r' || c == ':') return true;
  if (s.front() == ' ' || s.back() == ' ') return true;
  if (forKey && s.find('[') != std::string::npos) return true;  // keys like a[3] collide with key[N]: headers
  if (s.find('"') != std::string::npos) return true;  // embedded quotes must not break quote-aware key parsing
  return false;
}

inline std::string toonQuoteVal(const std::string& s, bool forKey = true) {
  if (toonValNeedsQuotes(s, forKey)) return '"' + toonJsonEscape(s) + '"';
  return s;
}

inline std::string toonLeafVal(const asvJSONValue* v) {
  if (!v) return "null";
  switch (v->type) {
    case asvJSONValue::NULL_VAL: return "null";
    case asvJSONValue::STRING: return toonQuoteVal(v->str_data, false);  // values may hold '[' bare; only keys need the key[N]: collision guard
    case asvJSONValue::INT: return std::to_string(v->num);
    case asvJSONValue::BOOL_VAL: return v->flag ? "true" : "false";
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      // NaN/Inf are not JSON numbers, so they render as the bare literal `null`
      // (unquoted). This matches the core JSON serializer. The consequence on a
      // round-trip: the DOUBLE comes back as a NULL_VAL, not as a NaN/Inf DOUBLE.
      // Unavoidable -- JSON has no encoding for either -- and it is the same
      // asymmetry every other text format here has.
      if (std::isnan(d) || std::isinf(d)) return "null";
      std::string r; fmtDoubleVal(d, r);
      // A DOUBLE must round-trip as a float literal even when its value is
      // integral (fmtDoubleVal can emit "100" for 100.0); without this the JSON
      // parser would re-read it as INT and lose the type.
      if (r.find('.') == std::string::npos && r.find('e') == std::string::npos && r.find('E') == std::string::npos)
        r += ".0";
      return r;
    }
    case asvJSONValue::OBJECT:
    case asvJSONValue::ARRAY: {
      // Unreachable: every caller routes a container through the length-bearing
      // ("key[N]:") or list-form branch and excludes it from allPrim, so
      // toonLeafVal is only ever handed primitives. The old inline "{"..."}" /
      // "["..."]" emitters here were dead code that would have produced invalid
      // TOON (unquoted keys) if a future caller reached them -- fail loudly.
      throw asvJSONError("TOON: internal error, container reached the leaf emitter");
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
      // QUOTED, so a DATETIME is always a TOON string cell and never collides
      // with a bare literal. Note the fallback: fmtDateTimeVal writes the text
      // "null" for a time_t it cannot convert (out of range for the platform's
      // localtime/gmtime), which lands here as the 4-character STRING "null",
      // not as a null value -- a string cell cannot express null without
      // ambiguity, and a bare `null` would have read back as NULL_VAL, silently
      // changing the type. Matches the core JSON serializer; the DATETIME does
      // not survive such a round-trip as a DATETIME.
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

// Spec (toon-format/spec) Sec. 9.2/Sec. 9.4: a nested array rendered as a list item.
//   - empty inner array:  "- [0]:"
//   - primitive inner array: "- [M]: v1,v2,..."  (Sec. 9.2)
//   - non-uniform / array of objects: "- [M]:" header, then nested items at
//     depth +1 relative to the hyphen line (Sec. 9.4). Tabular form is not
//     available in list-item position, so nested objects/arrays use list form.
inline void toonNestedArrayItem(const asvJSONValue* v, std::string& out, int indent, int depth) {
  // Self-recursion happens only through the ARRAY branch below (never through a
  // valToToon round-trip), so the depth guard must live here -- and it throws
  // rather than silently truncating, matching valToToon.
  // depth+1, matching valToToon: the root container is level 1 here too, and
  // this function always receives an ARRAY, so the container-only condition is
  // already implied. See the long note in valToToon.
  if (!asvJSONValue::checkNestingDepth(depth + 1))
    throw asvJSONError("TOON: maximum nesting depth exceeded");
  if (!v || v->type != asvJSONValue::ARRAY || v->size() == 0) {
    out += std::string(static_cast<size_t>(indent) * 2, ' ') + "- [0]:\n";
    return;
  }
  bool allPrim = true;
  for (size_t i = 0; i < v->size(); i++) {
    auto* e = v->get(i);
    // BINARY/EXTENSION/OBJECTID/REGEX/TIMESTAMP/DATETIME count as non-primitives
    // even though toonLeafVal renders them: their JSON-object text carries
    // depth-0 commas (e.g. {"$binary":{...,"subType":".."}}) and the decoder's
    // cell splitter is quote-aware, not brace-aware, so they cannot live in an
    // inline row next to a length header without being cut into extra cells.
    if (e && (e->type == asvJSONValue::OBJECT || e->type == asvJSONValue::ARRAY ||
               e->type == asvJSONValue::BINARY || e->type == asvJSONValue::EXTENSION ||
               e->type == asvJSONValue::OBJECTID || e->type == asvJSONValue::REGEX ||
               e->type == asvJSONValue::TIMESTAMP || e->type == asvJSONValue::DATETIME)) { allPrim = false; break; }
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
  // A tree parsed or built past MAX_NESTING_DEPTH must not silently vanish
  // from the output (the old behavior returned and left a truncated document):
  // throw, so toTOON() reports data loss instead of hiding it.
  //
  // The guard counts CONTAINERS, and the root container is level 1.
  //
  // Two corrections over the old `checkNestingDepth(depth)`. depth+1 because the
  // core JSON parser does ++parseDepth before checking in parseObject, so its
  // root object is level 1 and `depth` raw made TOON accept one level more than
  // the core -- toTOON() could emit a document fromTOON() would reject. And the
  // check is now restricted to containers, because valToToon also visits scalar
  // leaves: counting them would put the leaf of a 48-level object tree at level
  // 49 and reject a tree the core parses fine (the core never increments for a
  // scalar, so a scalar's "level" is just its parent's).
  if ((v->type == asvJSONValue::OBJECT || v->type == asvJSONValue::ARRAY) &&
      !asvJSONValue::checkNestingDepth(depth + 1))
    throw asvJSONError("TOON: maximum nesting depth exceeded");
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
    // Spec Sec. 9.1: length-bearing header "key[N]:" for non-empty arrays. All
    // primitive members render inline on the header line ("key[N]: v1,v2");
    // otherwise the header opens a list whose items nest at indent + 1 (Sec. 9.4).
    // BINARY/EXTENSION/OBJECTID/REGEX/TIMESTAMP/DATETIME are non-primitives here
    // for the same reason as in toonNestedArrayItem: their toonLeafVal text
    // contains depth-0 commas that the decoder's quote-aware cell splitter would
    // cut into extra inline cells.
    bool allPrim = true;
    for (size_t i = 0; i < v->size(); i++) {
      auto* e = v->get(i);
      if (e && (e->type == asvJSONValue::OBJECT || e->type == asvJSONValue::ARRAY ||
                e->type == asvJSONValue::BINARY || e->type == asvJSONValue::EXTENSION ||
                e->type == asvJSONValue::OBJECTID || e->type == asvJSONValue::REGEX ||
                e->type == asvJSONValue::TIMESTAMP || e->type == asvJSONValue::DATETIME)) { allPrim = false; break; }
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

// Cheap, allocation-free check for a '"'-leading token: the escape-aware closing
// quote must be the last non-whitespace character. This is exactly the JSON
// string rule, so it replaces a full parse on the hot path -- every quoted inline
// value in a document lands here. Anything that fails is a TOON string.
inline bool toonJsonStringIsComplete(std::string_view s) {
  if (s.size() < 2 || s.front() != '"') return false;
  size_t i = 1;
  while (i < s.size()) {
    char c = s[i];
    if (c == '\\') { i += 2; continue; }        // skip the escaped char
    if (c == '"') {                            // closing quote: only blanks may follow
      for (size_t k = i + 1; k < s.size(); k++) {
        char w = s[k];
        if (w != ' ' && w != '\t' && w != '\r' && w != '\n') return false;
      }
      return true;
    }
    i++;
  }
  return false;                                 // unterminated
}

// Wraps a bare TOON string value in JSON quotes if not already a valid JSON value.
//
// toonIsJsonValue() only inspects the first character for '"', '{' and '[' (it has
// to: the encoder relies on that to quote a STRING value that happens to look
// like JSON, e.g. "{a: 1}"). So on the decode side that first-character test is
// not enough -- "key: [1,2", "key: {a: 1}" and "key: \"unterminated" all passed
// and were copied verbatim into the intermediate JSON, breaking it. Numbers and
// the bare literals are already fully covered by the grammar in
// toonIsJsonValue, and quoted strings by toonJsonStringIsComplete, so neither
// pays for a parse. The '{'/'[' forms do use the parser: balancing alone is not
// enough ({a: 1} is balanced but not JSON), and they are cold anyway -- the
// encoder emits containers through the header/list forms, never inline, so these
// only appear in hand-written input.
inline std::string toonJsonQuoteBare(std::string_view s) {
  if (toonIsJsonValue(s)) {
    const char f = s.front();
    if (f == '"') {
      if (toonJsonStringIsComplete(s)) return std::string(s);
    } else if (f == '{' || f == '[') {
      asvJSON probe;
      if (probe.parse(s)) return std::string(s);
    } else {
      return std::string(s);                    // number / true / false / null
    }
  }
  return '"' + toonJsonEscape(std::string(s)) + '"';
}

// TOON value splitter - splits by comma respecting quoted strings, escapes and
// nested {} / [] groups.
//
// The brace awareness is required, not defensive. Brackets can legitimately hold
// a comma even in input the encoder never produces: a hand-written
// "key[1]: {a: 1, b: 2}" is an array of one object, and with a quote-only splitter
// the inner comma cut it into "{a: 1" / " b: 2}". Neither shard is valid JSON, so
// both became strings and the array silently grew to two elements with mangled
// text. Tracking depth keeps the group in one cell; toonJsonQuoteBare then hands
// the whole thing to the JSON parser, which accepts the quoted-key object and
// rejects the TOON-style unquoted-key one (so "{a: 1}" degrades to a string, as
// it already did, but as ONE cell rather than two).
//
// Depth also matters for the types the encoder DOES put in a cell only when it
// cannot avoid it: toonLeafVal renders BINARY/EXTENSION/OBJECTID/REGEX/TIMESTAMP
// as JSON objects whose text has depth-0 commas. The allPrim checks in valToToon
// and toonNestedArrayItem keep them out of inline rows, so this is not what
// protects them -- the encoder must not emit the form at all, because a brace-
// aware splitter would keep the object intact and the decoder would then have to
// decide whether the cell is a primitive or a container.
inline std::vector<std::string> toonSplitCommas(std::string_view s) {
  std::vector<std::string> result;
  std::string cur;
  bool inQuotes = false;
  bool escape = false;
  int brace = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (escape) { cur += c; escape = false; continue; }
    if (c == '\\' && inQuotes) { cur += c; escape = true; continue; }
    if (c == '"') { cur += c; inQuotes = !inQuotes; continue; }
    if (!inQuotes) {
      if (c == '{' || c == '[') brace++;
      else if (c == '}' || c == ']') { if (brace > 0) brace--; }
    }
    if (c == ',' && !inQuotes && brace == 0) {
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
  // A trailing ':' is optional here: when the ']' is the final character, i ==
  // val.size() and the guard below is skipped, so "- [3]" (no colon) still parses
  // as an opener and then reports the length mismatch. A bare "- [3]" DOES reach
  // this path as a list item (the content[1] == ' ' guard matches it): with the
  // colon it is a list-form opener carrying declLen, which is the intended
  // reading, not the backward-compat JSON literal "- [1,2]".
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
// Note: this scans only quotes, not brackets -- a ':' inside an unquoted {a: 1}
// value would be seen as unquoted. That is harmless because the FIRST unquoted
// ':' is the separator, so for "key: {a: 1}" it lands right after "key" and the
// value is the rest of the line. (There is no '{' JSON branch; the value is
// routed through toonJsonQuoteBare, which quotes it since it is not valid JSON.)

// Decodes a TOON key token (optionally double-quoted) into the JSON-escaped
// text BETWEEN quotes. A quoted token's inner text is already JSON-escaped
// (the encoder wrote it verbatim via appendJsonEscaped), so it is passed
// through unchanged -- re-escaping would double-escape \n, \" etc. into
// literal backslashes. Bare tokens are JSON-escaped here.
inline std::string toonDecodeKey(std::string_view key) {
  // A token counts as a quoted key only when its quotes enclose a complete JSON
  // string. A token whose final quote is escaped (e.g. the four characters of
  // `"a\": 1`, where the trailing \ " is an escaped quote, not a closer) would
  // otherwise have both quotes stripped and emit broken intermediate JSON; the
  // escape-aware completeness check catches it and the whole token falls back to
  // the bare-escape path so the value survives as an ordinary (quoted) key.
  if (key.size() >= 2 && key.front() == '"' && key.back() == '"' && toonJsonStringIsComplete(key))
    return std::string(key.substr(1, key.size() - 2));
  return toonJsonEscape(std::string(key));
}

// TOON -> JSON text converter
inline std::string toonToJson(std::string_view input) {
  auto lines = splitLines(input);
  if (lines.empty()) return "{}";

  size_t firstNonEmpty = 0;
  // Whitespace-only lines are "empty" for this scan: a bare scalar followed by a
  // stray " " line used to leave onlyScalarLine false and then fail the scalar
  // line in the main loop as unrecognized.
  while (firstNonEmpty < lines.size() && stripIndent(lines[firstNonEmpty]).empty()) firstNonEmpty++;
  if (firstNonEmpty >= lines.size()) return "{}";

  std::string_view firstContent = stripIndent(lines[firstNonEmpty]);
  bool rootIsArr = (firstContent.size() >= 2 && firstContent[0] == '[') || (firstContent.size() >= 1 && firstContent[0] == '-');

  // Bare root scalar (no ':', no array header, no '- value' list form): the
  // encoder emits such roots as a single token line, so parse it directly
  // instead of treating it as an empty object/array.
  {
    size_t colon = toonFindFirstUnquoted(firstContent, ':');
    size_t lb = toonFindFirstUnquoted(firstContent, '[');
    bool listForm = (firstContent.size() >= 2 && firstContent[0] == '-' && firstContent[1] == ' ') || firstContent == "-";
    bool onlyScalarLine = true;
    for (size_t sli = firstNonEmpty + 1; sli < lines.size() && onlyScalarLine; sli++)
      if (!stripIndent(lines[sli]).empty()) onlyScalarLine = false;
    // A single-line root that is itself a complete JSON value is taken as-is.
    // This covers the "[]\n" the encoder emits for an empty root array -- the
    // length-bearing header branch below needs rb > lb + 1, i.e. at least one
    // length digit, so a bare "[]" used to fall through to the line loop and throw
    // "unrecognized line 1". It also covers a root "{...}" / "[1,2]" literal.
    if (onlyScalarLine && !listForm && firstContent.size() >= 2 &&
        (firstContent[0] == '{' || firstContent[0] == '[')) {
      asvJSON probe;
      if (probe.parse(firstContent)) return std::string(firstContent);
      // Not JSON, so not a value this format can carry structurally. It may still
      // be a length-bearing header ("[N]:" or "[N]{fields}:"), which must reach
      // the header branch below and report its own errors. Anything else is a
      // bare string -- the same reading the identical text already gets after a
      // "key: " -- so a root "[1,2" and a root "{a: 1" behave alike instead of
      // one becoming a string and the other throwing. (A malformed header such as
      // "[3] 1,2,3" is likewise kept as a string rather than silently wrapped in
      // an array, matching what "key: [3] 1,2,3" already does.)
      bool headerShaped = false;
      if (firstContent[0] == '[') {
        size_t rb = firstContent.find(']');
        if (rb != std::string_view::npos && rb > 1) {
          size_t k = rb + 1;
          while (k < firstContent.size() && (firstContent[k] == ' ' || firstContent[k] == '\t')) k++;
          headerShaped = (k < firstContent.size()) && (firstContent[k] == ':' || firstContent[k] == '{');
        }
      }
      if (!headerShaped) return toonJsonQuoteBare(firstContent);
    }
    if (onlyScalarLine && colon == std::string_view::npos && lb == std::string_view::npos && !listForm)
      return toonJsonQuoteBare(firstContent);
  }

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
    if (content.empty()) continue;   // whitespace-only line: nothing to process

    // Validate any open list-form array frame that this line's lower indent will
    // close, mirroring closeFrames' pop condition exactly. Declared length is only
    // ever set on TOON list-form frames (declLen > 0), so ordinary arrays are skipped.
    for (size_t i = stack.size(); i-- > 0; ) {
      if (stack[i].isRoot) break;
      if (indent > stack[i].align) break;   // this frame stays open -- stop here
      if (stack[i].type == 'A' && stack[i].declLen > 0 && stack[i].itemCount != stack[i].declLen)
        throw asvJSONError("TOON: array length mismatch at line " + std::to_string(li + 1));
    }

    closeFrames(stack, out, indent);
    if (stack.empty()) break;
    auto& curFrame = stack.back();   // captured before any push_back below; a later push may reallocate the vector and invalidate this reference.

    // array header: [optional key][N][{fields}]:  (spec Sec. 9.1-Sec. 9.4, Sec. 10)
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
                if (vals.size() != fields.size())
                  throw asvJSONError("TOON: row " + std::to_string(rl) + " has " + std::to_string(vals.size()) + " values, expected " + std::to_string(fields.size()));
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
              // list form: frame left open; deeper "- value" items fill it. Record the
              // declared length so the item count can be validated when this frame closes.
              if (!stack.empty()) stack.back().declLen = arrLen;
              continue;
            }
          }
        }
      }
    }

    // list item: - value
    //
    // Note on a bare "- ": it does NOT decode to the empty string. content is
    // trimmed before this test, so "- " arrives as "-", fails the size-2 check,
    // and falls through to the bare-dash branch below, which opens an OBJECT
    // list item. The empty string has to be written - "" (quoted), which is also
    // what the encoder emits for it. Rejected earlier as a possible reading of
    // "- " being an empty string: the hyphen is the list-item marker, and a
    // marker with no value is the documented object-item form, not a scalar.
    if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
      addComma(stack, out);
      curFrame.first = false;
      if (curFrame.type == 'A') curFrame.itemCount++;   // one element of the enclosing array (scalar or nested)
      std::string_view val = content.substr(2);
      // nested array header form "- [M]: ..." / "- [M]" (spec Sec. 9.2/Sec. 9.4)
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
          // non-primitive list-form opener: leave frame open. Its deeper "- value"
          // items are validated against this declared length when the frame closes.
          stack.push_back({'A', true, indent, true, false});
          if (!stack.empty()) stack.back().declLen = arrLen;
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
      if (curFrame.type == 'A') curFrame.itemCount++;   // one object element of the enclosing array
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
      continue;   // handled as a member -- skip the unrecognized-line guard below
    }

    throw asvJSONError("TOON: unrecognized line " + std::to_string(li + 1));
  }

  // Close all remaining frames (EOF). Any still-open list-form array must have met
  // its declared length by now.
  while (!stack.empty()) {
    if (stack.back().type == 'A' && stack.back().declLen > 0 && stack.back().itemCount != stack.back().declLen)
      throw asvJSONError("TOON: array length mismatch (declared " + std::to_string(stack.back().declLen) + ", got " + std::to_string(stack.back().itemCount) + ")");
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
