#pragma once
// S-Expression serialization/parsing for asvJSON++
//
// S-Expression format:
//   (key "value" (nested 1 2 3))
//   Objects: alternating key-value pairs in a list (heuristic: even length, keys are any scalar token - string/symbol/number/bool/nil)
//   Arrays: plain list of values
//   Strings: "quoted" with standard C escapes
//   Numbers: as-is (must be valid JSON-like numbers)
//   Booleans: #t / #f
//   Null: nil
//   Comments: ; to end of line
//
// Serialization contract: toSexpr() never throws. A value that cannot be
// represented -- NaN/Infinity (no S-Expression token exists for them) or a
// nesting depth past MAX_NESTING_DEPTH on a programmatically-built tree --
// degrades to the sentinel `nil` rather than aborting the write; such a value
// comes back as null on reparse.

#include "../core.hpp"

namespace asvJSONInternal {

enum SexprTokenType { SEXPR_LPAREN, SEXPR_RPAREN, SEXPR_STRING, SEXPR_NUMBER, SEXPR_SYMBOL, SEXPR_TRUE, SEXPR_FALSE, SEXPR_NIL };

// S-Expression internal marker byte for quoted object keys. It is emitted as a
// LITERAL control byte in the source, so it can be recognized during
// tokenization (before \u/escape decoding) and can NEVER collide with a value
// string that merely begins with that byte -- the encoder always escapes such a
// value as "\u0001...". See sexprTokenize.
static const char SEXPR_KEY_MARK = '\x01';
// S-Expression internal marker token for arrays whose elements would be
// misread as an object by the object/list heuristic (an even-length group of
// scalars). The encoder emits a single literal SEXPR_ARRAY_MARK bare-symbol
// token as the first element of such arrays; the write is UNQUOTED, so it can
// never collide with a quoted string value that merely contains this byte
// (those are escaped as "\u0002..." and decode to a STRING token).
static const char SEXPR_ARRAY_MARK = '\x02';

struct SexprToken {
  SexprTokenType type;
  std::string value;
  bool marked = false; // true if source string began with a literal SEXPR_KEY_MARK key marker
};

// Strict JSON number grammar: an optional '-', a digit sequence without
// leading zeros, then an optional fraction and an optional exponent, with
// nothing left over. This is the single source of truth for "is this token a
// number": the tokenizer classifies with it and sexprTokenToJson re-checks
// with it, so the two can never disagree about which atoms are numbers.
inline bool sexprIsJsonNumber(const std::string& s) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[i] == '-') i++;
  if (i >= s.size()) return false;
  if (s[i] == '0') {
    i++;
    // leading zeros are not valid JSON ("01", "-01")
    if (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  } else if (s[i] >= '1' && s[i] <= '9') {
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
  } else {
    return false;
  }
  if (i < s.size() && s[i] == '.') {
    i++;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
  }
  return i == s.size();
}

inline std::vector<SexprToken> sexprTokenize(std::string_view input) {
  std::vector<SexprToken> tokens;
  size_t i = 0;
  while (i < input.size()) {
    char c = input[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { i++; continue; }
    if (c == ';') { while (i < input.size() && input[i] != '\n' && input[i] != '\r') i++; continue; }
    if (c == '(') { tokens.push_back({SEXPR_LPAREN, ""}); i++; continue; }
    if (c == ')') { tokens.push_back({SEXPR_RPAREN, ""}); i++; continue; }

    // String with proper unescaping
    if (c == '"') {
      std::string s;
      i++;
      // A LITERAL SEXPR_KEY_MARK control byte right after the opening quote is
      // the key marker emitted by the encoder. Values that merely begin with
      // that byte are escaped as "\u0001..." instead (appendJsonEscaped), so a
      // literal marker here can never collide with real data.
      bool marked = false;
      if (i < input.size() && input[i] == SEXPR_KEY_MARK) { marked = true; i++; }
      bool closed = false;
      while (i < input.size()) {
        if (input[i] == '\\' && i + 1 < input.size()) {
          i++;
          switch (input[i]) {
            case 'n': s += '\n'; break;
            case 't': s += '\t'; break;
            case 'r': s += '\r'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'u': case 'U': {
              bool isUpper = (input[i] == 'U');
              int count = isUpper ? 8 : 4;
              // reads `n` hex digits at input[p]; fills `out`, returns false on
              // short input or a non-hex character.
              auto readHex = [&](size_t p, int n, unsigned long& out) -> bool {
                if (p + static_cast<size_t>(n) > input.size()) return false;
                out = 0;
                for (int q = 0; q < n; q++) {
                  char h = input[p + q];
                  if (h >= '0' && h <= '9') out = (out << 4) | static_cast<unsigned long>(h - '0');
                  else if (h >= 'a' && h <= 'f') out = (out << 4) | static_cast<unsigned long>(h - 'a' + 10);
                  else if (h >= 'A' && h <= 'F') out = (out << 4) | static_cast<unsigned long>(h - 'A' + 10);
                  else return false;
                }
                return true;
              };
              auto emitUtf8 = [&](unsigned long cp) {
                if (cp < 0x80) s += static_cast<char>(cp);
                else if (cp < 0x800) {
                  s += static_cast<char>(0xC0 | (cp >> 6));
                  s += static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                  s += static_cast<char>(0xE0 | (cp >> 12));
                  s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                  s += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                  s += static_cast<char>(0xF0 | (cp >> 18));
                  s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                  s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                  s += static_cast<char>(0x80 | (cp & 0x3F));
                }
              };
              // Preserve an un-decodable escape verbatim (backslash included),
              // advancing only over the digits we actually copied.
              auto emitLiteral = [&]() -> size_t {
                size_t copied = 0;
                s += '\\'; s += isUpper ? 'U' : 'u';
                for (int q = 0; q < count; q++) {
                  size_t p = i + 1 + static_cast<size_t>(q);
                  if (p >= input.size()) break;
                  s += input[p];
                  copied++;
                }
                return copied;
              };
              unsigned long cp = 0;
              if (!readHex(i + 1, count, cp)) { i += emitLiteral(); break; }
              if (cp >= 0xD800 && cp <= 0xDBFF && !isUpper) {
                // High surrogate: combine with an immediately following \uDC00-\uDFFF.
                unsigned long lo = 0;
                if (i + 5 + 4 <= input.size() && input[i + 5] == '\\' &&
                    input[i + 6] == 'u' &&
                    readHex(i + 7, 4, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                  cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                  emitUtf8(cp);
                  i += 10; // consume high escape + low escape (see caller's i++)
                  break;
                }
                i += emitLiteral();
                break;
              }
              if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { i += emitLiteral(); break; }
              emitUtf8(cp);
              i += count;
              break;
            }
            default: s += '\\'; s += input[i]; break;
          }
          i++;
          continue;
        }
        if (input[i] == '"') { closed = true; i++; break; }
        s += input[i]; i++;
      }
      if (!closed) throw asvJSONError("unclosed string in S-Expression");
      tokens.push_back({SEXPR_STRING, std::move(s), marked});
      continue;
    }

    std::string val;
    while (i < input.size() && input[i] != ' ' && input[i] != '\t' && input[i] != '\n' && input[i] != '\r' && input[i] != '\f' && input[i] != '\v' && input[i] != '(' && input[i] != ')' && input[i] != '"' && input[i] != ';') {
      val += input[i]; i++;
    }

    if (val == "nil") tokens.push_back({SEXPR_NIL, ""});
    else if (val == "#t" || val == "true") tokens.push_back({SEXPR_TRUE, ""});
    else if (val == "#f" || val == "false") tokens.push_back({SEXPR_FALSE, ""});
    else {
      // Classify with the JSON number grammar and nothing else. The previous
      // ad-hoc scan accepted a leading '+', leading zeros and other forms JSON
      // rejects ("+5", "01", "007"); those became NUMBER tokens whose JSON form
      // was a *string*, and sexprKeyJson then quoted that string a second time
      // as an object key, yielding "\"+5\"" instead of "+5". Anything the JSON
      // grammar rejects stays a SYMBOL, which already stringifies correctly.
      if (sexprIsJsonNumber(val)) tokens.push_back({SEXPR_NUMBER, val});
      else tokens.push_back({SEXPR_SYMBOL, val});
    }
  }
  return tokens;
}

inline std::string sexprTokenToJson(const SexprToken& token) {
  switch (token.type) {
    case SEXPR_STRING: {
      std::string esc;
      esc.reserve(token.value.size() + 2);
      esc += '"';
      appendJsonEscaped(esc, token.value);
      esc += '"';
      return esc;
    }
    case SEXPR_NUMBER: {
      // The tokenizer only produces NUMBER for sexprIsJsonNumber() atoms, so
      // this is normally a bare JSON number. The check is kept as a guard: a
      // hand-built token degrades to a string here rather than letting invalid
      // JSON escape into the document.
      if (sexprIsJsonNumber(token.value)) return token.value;
      std::string esc;
      esc.reserve(token.value.size() + 2);
      esc += '"';
      appendJsonEscaped(esc, token.value);
      esc += '"';
      return esc;
    }
    case SEXPR_TRUE:   return "true";
    case SEXPR_FALSE:  return "false";
    case SEXPR_NIL:    return "null";
    case SEXPR_SYMBOL: {
      std::string esc;
      esc.reserve(token.value.size() + 2);
      esc += '"';
      appendJsonEscaped(esc, token.value);
      esc += '"';
      return esc;
    }
    default: return "null";
  }
}

inline bool sexprTypeIsKey(int t) {
  return t == SEXPR_SYMBOL || t == SEXPR_STRING || t == SEXPR_NUMBER ||
         t == SEXPR_TRUE || t == SEXPR_FALSE || t == SEXPR_NIL;
}

inline std::string sexprKeyJson(const std::string& item, int type) {
  if (type == SEXPR_NUMBER || type == SEXPR_TRUE ||
      type == SEXPR_FALSE || type == SEXPR_NIL) {
    // `item` is the token's JSON form -- "1.5" for a NUMBER, and the literals
    // "true" / "false" / "null" for TRUE/FALSE/NIL (their token.value is empty,
    // but that is not what arrives here). A JSON object key must be a string,
    // so the value is quoted and escaped; the extra quotes make it a JSON
    // string, not the identity.
    // Defensive, NOT currently reachable: for the four types above `item` is a
    // bare number or one of the unquoted literals true/false/null, so it can
    // never start with '"'. The check guards a future refactor where a quoted
    // string might arrive here -- quoting it again would embed the quotes in
    // the key ("\"x\"" instead of "x").
    if (!item.empty() && item.front() == '"') return item;
    std::string esc;
    esc.reserve(item.size() + 2);
    esc += '"';
    appendJsonEscaped(esc, item);
    esc += '"';
    return esc;
  }
  return item;
}

inline std::string sexprTokensToJson(const std::vector<SexprToken>& tokens, size_t& pos, int depth = 0) {
  if (depth >= asvJSONValue::MAX_NESTING_DEPTH) throw asvJSONError("S-Expression nesting too deep");

  if (pos >= tokens.size()) throw asvJSONError("unexpected end of S-Expression");
  if (tokens[pos].type == SEXPR_RPAREN) throw asvJSONError("unexpected closing parenthesis");

  // Top-level atom (not a list)
  if (tokens[pos].type != SEXPR_LPAREN) {
    return sexprTokenToJson(tokens[pos++]);
  }

  pos++; // skip '('
  // A leading literal ARRAY marker forces array interpretation, overriding
  // the object/list heuristic (the encoder emits it for even-length scalar
  // arrays that would otherwise decode as key/value objects). It is a bare
  // symbol (unquoted), so a real string value that merely contains the byte
  // is a STRING token and never matches this.
  bool forceArray = false;
  if (pos < tokens.size() && tokens[pos].type == SEXPR_SYMBOL &&
      tokens[pos].value.size() == 1 && static_cast<unsigned char>(tokens[pos].value[0]) == SEXPR_ARRAY_MARK) {
    forceArray = true;
    pos++;
  }
  std::vector<std::string> items;
  std::vector<int> itemTypes;
  // For strings the encoder printed as quoted keys (leading SEXPR_KEY_MARK):
  // `marked` records the fact and `markedRaw` keeps the ORIGINAL unescaped value
  // still carrying the marker byte. Stripping is deferred to materialization so
  // that a value which merely starts with that byte round-trips as data instead
  // of being misread as a key.
  std::vector<bool> marked;
  std::vector<std::string> markedRaw;
  while (pos < tokens.size() && tokens[pos].type != SEXPR_RPAREN) {
    if (tokens[pos].type == SEXPR_LPAREN) {
      std::string sub = sexprTokensToJson(tokens, pos, depth + 1);
      itemTypes.push_back(sub.empty() ? SEXPR_NIL : (sub[0] == '{' ? -2 : -1));
      items.push_back(std::move(sub));
      marked.push_back(false);
      markedRaw.push_back(std::string());
    } else if (tokens[pos].type == SEXPR_STRING && tokens[pos].marked) {
      // Marked string key from encoder -- marker already stripped by the
      // tokenizer; treat as a symbol key for the heuristic. Keep the original
      // marker-prefixed value in case this item is (mistakenly) used as a value.
      SexprToken stripped = tokens[pos];
      markedRaw.push_back(std::string(1, SEXPR_KEY_MARK) + stripped.value);
      items.push_back(sexprTokenToJson(stripped));
      itemTypes.push_back(SEXPR_SYMBOL);
      marked.push_back(true);
      pos++;
    } else {
      items.push_back(sexprTokenToJson(tokens[pos]));
      itemTypes.push_back(tokens[pos].type);
      marked.push_back(false);
      markedRaw.push_back(std::string());
      pos++;
    }
  }
  if (pos >= tokens.size()) throw asvJSONError("unclosed parenthesis in S-Expression");
  pos++; // skip ')'

  // JSON text for item i used as a VALUE: a marker-prefixed item used as data
  // must keep the marker byte (it was data, not a key).
  //
  // KNOWN QUIRK (issue #4): only hand-written input with a LITERAL \x01 byte
  // right after the opening quote (the key-marker spelling) is affected: if
  // such a marker-prefixed string lands in a value position, the \x01 byte is
  // preserved in the decoded value. Data that merely STARTS with byte 0x01 is
  // never affected -- the encoder escapes it as \u0001, which tokenizes to a
  // plain STRING with no marker. The encoder emits marked strings exclusively
  // in key positions, so round-trips of encoded documents are clean.
  auto valueJson = [&](size_t i) -> std::string {
    if (marked[i]) {
      std::string r;
      r += '"';
      appendJsonEscaped(r, markedRaw[i]);
      r += '"';
      return r;
    }
    return items[i];
  };
  // Object-vs-list heuristic for a parenthesized group:
  // - Odd-length groups are always lists.
  // - Even-length groups are objects when every even-indexed item is a key
  //   type (SYMBOL/STRING/NUMBER/TRUE/FALSE/NIL). A 2-item group is an object
  //   ONLY when the first item is a bare symbol (or a marked key): a quoted
  //   string in position 0 is data, so ("a" "b") is an array. For groups of
  //   4+ items a STRING on an even position IS treated as a key -- ("a" "b"
  //   "c" "d") decodes to {"a":"b","c":"d"} -- only the 2-item case
  //   distinguishes a SYMBOL key from a STRING value.
  // - When the group starts with a bare symbol key but the remaining items
  //   are not objects, the values are emitted as an array: {key: [v1, v2, ...]}.
  //   Quoted strings are data values, never keys, in this fallback.
  // This is intentionally heuristic -- S-Expressions have no inherent
  // object/list distinction, so ambiguous forms may not round-trip exactly.
  bool isObj = false;

  // Empty object marker: (() {}) -- empty group + bare {} symbol sentinel.
  // A data string "{}" is always quoted by the encoder, and a nested empty
  // object value is itself a parenthesized group, so the bare {} symbol can
  // only denote this marker (unlike the old (() #t) form, which collided with
  // the real data value [[], true]).
  if (!forceArray && items.size() == 2 && items[0] == "[]" && items[1] == "\"{}\""
      && itemTypes[0] == -1 && itemTypes[1] == SEXPR_SYMBOL) {
    return "{}";
  }

  if (!forceArray && items.size() >= 2 && items.size() % 2 == 0) {
    // Two-item lists: an object only when the first item is a bare symbol
    // (a quoted string or a value in position 0 is data -> a 2-element array).
    // Longer even-length groups are stricter: every even position must be a key
    // type (SYMBOL/STRING/NUMBER/TRUE/FALSE/NIL).
    if (items.size() == 2) {
      isObj = (itemTypes[0] == SEXPR_SYMBOL);
    } else {
      isObj = true;
      for (size_t j = 0; j < items.size(); j += 2) {
        if (!sexprTypeIsKey(itemTypes[j])) { isObj = false; break; }
      }
    }
  }

  // Fallback: if first item is a bare symbol key, emit {key: values}.
  // A quoted string in first position is data, not a key -- odd-length
  // groups are always lists, so ("a" "b" "c") stays an array.
  if (!forceArray && !isObj && items.size() >= 2 && itemTypes[0] == SEXPR_SYMBOL) {
    std::string key = items[0];
    if (items.size() == 2) {
      return '{' + key + ':' + valueJson(1) + '}';
    }
    // Check if all remaining items are objects -- merge into single object
    bool allObj = true;
    for (size_t j = 1; j < items.size(); j++) {
      if (itemTypes[j] != -2) { allObj = false; break; }
    }
    if (allObj) {
      std::string r = '{' + key + ":{";
      for (size_t j = 1; j < items.size(); j++) {
        if (j > 1) r += ',';
        // Each item is {"k":"v"} -- strip outer braces to merge key-value pairs
        const auto& s = items[j];
        if (s.size() >= 2 && s.front() == '{' && s.back() == '}') {
          r += s.substr(1, s.size() - 2);
        } else {
          r += s;
        }
      }
      r += "}}";
      return r;
    }
    std::string r = '{' + key + ":[";
    for (size_t j = 1; j < items.size(); j++) {
      if (j > 1) r += ',';
      r += valueJson(j);
    }
    r += "]}";
    return r;
  }

  std::string out;
  if (isObj) {
    out = '{';
    for (size_t j = 0; j < items.size(); j += 2) {
      if (j > 0) out += ',';
      out += sexprKeyJson(items[j], itemTypes[j]) + ':' + valueJson(j + 1);
    }
    out += '}';
  } else {
    out = '[';
    for (size_t j = 0; j < items.size(); j++) {
      if (j > 0) out += ',';
      out += valueJson(j);
    }
    out += ']';
  }
  return out;
}

inline void sexprSerializeVal(const asvJSONValue* v, std::string& out, int depth = 0) {
  if (!v) { out += "nil"; return; }
  if (!asvJSONValue::checkNestingDepth(depth)) { out += "nil"; return; }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += "nil"; break;
    case asvJSONValue::BOOL_VAL: out += v->flag ? "#t" : "#f"; break;
    case asvJSONValue::INT: out += std::to_string(v->num); break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      // NaN/Infinity are not valid S-Expression tokens. All sibling to*()
      // serializers emit a placeholder rather than throwing -- the public
      // contract is that to* returns a string and never throws (unlike from*).
      if (std::isnan(d) || std::isinf(d)) { out += "nil"; break; }
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
      out += '(';
      if (v->obj->empty()) {
        // Empty object: encode as (() {}) so the decoder can tell it apart
        // from the empty array (). The bare {} symbol cannot be a quoted
        // string value, so this marker never collides with real data.
        out += "() {})";
        break;
      }
      bool first = true;
      for (const auto& [k, child] : *(v->obj)) {
        if (!first) out += ' ';
        first = false;
        bool needsQuote = false;
        if (k.empty()) { needsQuote = true; }
        else {
          for (char c : k) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '+' && c != '*' && c != '/' && c != '?' && c != '!') {
              needsQuote = true; break;
            }
          }
          if (!needsQuote) {
            // Quote keys that would be misinterpreted as non-string tokens
            if (k == "nil" || k == "true" || k == "false" || k == "#t" || k == "#f") {
              needsQuote = true;
            } else if (std::isdigit(static_cast<unsigned char>(k[0]))) {
              needsQuote = true;
            } else if ((k[0] == '-' || k[0] == '+') && k.size() > 1 && std::isdigit(static_cast<unsigned char>(k[1]))) {
              needsQuote = true;
            }
          }
        }
        // The marker is a literal byte right after the opening quote; the escaped
        // text can never reproduce one, because appendJsonEscaped emits 0x01/
        // 0x02 as \u0001/\u0002. Keep that escaping, or the decoder's
        // marker-vs-data disambiguation silently breaks.
        if (needsQuote) { out += '"'; out += SEXPR_KEY_MARK; appendJsonEscaped(out, k); out += '"'; }
        else out += k;
        out += ' ';
        sexprSerializeVal(child.get(), out, depth + 1);
      }
      out += ')';
      break;
    }
    case asvJSONValue::ARRAY: {
      // Even-length arrays whose even positions are all scalar key-type
      // tokens would decode as an object (key/value heuristic). Disambiguate
      // by emitting a leading literal ARRAY marker token ("\x02").
      // This scalar set is the encoder counterpart of the decoder's
      // sexprTypeIsKey set: STRING / INT / DOUBLE / BOOL / NULL / DATETIME
      // emit exactly the tokens the object heuristic treats as key types on
      // even positions (SEXPR_STRING / NUMBER / TRUE / FALSE / NIL; DATETIME
      // and STRING both emit as SEXPR_STRING). Keep the two sets in sync when
      // adding a JSON type, or an ambiguous array would decode back as an
      // object instead of receiving the force-array marker.
      bool ambiguous = v->size() >= 4 && v->size() % 2 == 0;
      if (ambiguous) {
        for (size_t i = 0; i < v->size() && ambiguous; i += 2) {
          const asvJSONValue* el = v->get(i);
          int t = el ? el->type : asvJSONValue::NULL_VAL;
          if (!(t == asvJSONValue::STRING || t == asvJSONValue::INT ||
                t == asvJSONValue::DOUBLE || t == asvJSONValue::BOOL_VAL ||
                t == asvJSONValue::NULL_VAL || t == asvJSONValue::DATETIME)) {
            ambiguous = false;
          }
        }
      }
      out += '(';
      if (ambiguous) { out += SEXPR_ARRAY_MARK; out += ' '; }
      for (size_t i = 0; i < v->size(); i++) {
        if (i > 0) out += ' ';
        sexprSerializeVal(v->get(i), out, depth + 1);
      }
      out += ')';
      break;
    }
    // Special types round-trip through the Extended JSON conventions (an OBJECT
    // whose members hold the $binary/$oid/$regex/$timestamp markers), which the
    // decoder's JSON pass reconstructs into typed values. String keys are marked
    // so the heuristic treats them as keys. Without this, these values would be
    // dropped to `nil` and silently lost on round-trip.
    case asvJSONValue::BINARY:
    case asvJSONValue::EXTENSION: {
      out += "(\"";
      out += SEXPR_KEY_MARK; out += "$binary\" (\"";
      out += SEXPR_KEY_MARK; out += "base64\" \"";
      appendJsonEscaped(out, encodeBase64(v->bin_data.data(), v->bin_data.size()));
      out += "\" \"";
      out += SEXPR_KEY_MARK; out += "subType\" \"";
      if (v->type == asvJSONValue::EXTENSION) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(v->ext_type));
        out += hex;
      } else {
        out += "00";
      }
      out += "\"))";
      break;
    }
    case asvJSONValue::OBJECTID: {
      out += "(\"";
      out += SEXPR_KEY_MARK; out += "$oid\" \"";
      fmtObjectIdHexVal(v->str_data, out);
      out += "\")";
      break;
    }
    case asvJSONValue::REGEX: {
      size_t sep = v->str_data.rfind('\0');
      std::string_view pattern = (sep != std::string_view::npos) ? std::string_view(v->str_data.data(), sep) : std::string_view(v->str_data);
      std::string_view opts = (sep != std::string_view::npos) ? std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1) : std::string_view();
      out += "(\"";
      out += SEXPR_KEY_MARK; out += "$regex\" \"";
      appendJsonEscaped(out, pattern);
      out += "\"";
      if (!opts.empty()) {
        out += " \"";
        out += SEXPR_KEY_MARK; out += "$options\" \"";
        appendJsonEscaped(out, opts);
        out += "\"";
      }
      out += ")";
      break;
    }
    case asvJSONValue::TIMESTAMP: {
      out += "(\"";
      out += SEXPR_KEY_MARK; out += "$timestamp\" (\"";
      out += SEXPR_KEY_MARK; out += "t\" ";
      char buf[32];
      auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
      if (ec == std::errc()) out.append(buf, ptr - buf);
      out += "))";
      break;
    }
    case asvJSONValue::DATETIME: {
      // ISO-8601 string; the JSON parser auto-reconstructs DATETIME on decode.
      out += '"';
      fmtDateTimeVal(v->timestamp, v->datetime_ms, out, v->tz_offset_min);
      out += '"';
      break;
    }
    case asvJSONValue::CUSTOM_TAG: {
      // Serialize through the $customTag Extended-JSON convention (same shape
      // as toJSON), so the decoder's JSON object parser reconstructs it into a
      // CUSTOM_TAG node on round-trip. Without this, CUSTOM_TAG fell through
      // to `nil` and was silently lost; furthermore, in arrays at even positions
      // the bare nil token triggered the key/value heuristic and corrupted shape.
      out += "(\"";
      out += SEXPR_KEY_MARK; out += "$customTag\" (\"";
      out += SEXPR_KEY_MARK; out += "name\" \"";
      appendJsonEscaped(out, v->str_data);
      out += "\" \"";
      out += SEXPR_KEY_MARK; out += "value\" ";
      if (v->custom_value) sexprSerializeVal(v->custom_value.get(), out, depth + 1);
      else out += "nil";
      out += "))";
      break;
    }
    default: out += "nil"; break;
  }
}

inline std::string asvJSON::toSexpr() const {
  if (!root) return "nil";
  std::string out;
  sexprSerializeVal(root.get(), out);
  return out;
}

inline bool asvJSON::fromSexpr(std::string_view input) {
  // Reset state up front (like the other from* parsers) so a parse failure at
  // any point -- including in sexprTokenize -- never leaves stale data behind:
  // getRoot()/getKeys() must reflect "we parsed the new input" semantics.
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  try {
    if (input.empty()) throw asvJSONError("empty input");
    if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xEF &&
        static_cast<unsigned char>(input[1]) == 0xBB && static_cast<unsigned char>(input[2]) == 0xBF) {
      input.remove_prefix(3);
    }
    if (input.empty()) throw asvJSONError("empty input");
    auto tokens = sexprTokenize(input);
    if (tokens.empty()) throw asvJSONError("no tokens");
    size_t pos = 0;
    std::string json = sexprTokensToJson(tokens, pos);
    if (pos != tokens.size()) throw asvJSONError("unexpected tokens after S-Expression");
    bool ok = parse(std::string_view(json));
    if (ok) formatType = FormatType::SExpr;
    return ok;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromCpp).
    lastError = e.what();
    return false;
  }
}

} // namespace asvJSONInternal
