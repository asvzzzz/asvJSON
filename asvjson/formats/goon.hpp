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
// Special types use __BASE64__/__EXT__/__TS__/__OID__/__REGEX__ prefixes (an
// empty payload still emits its prefix). Decoder builds intermediate JSON then
// parses via the JSON parser; full round-trip via toGOON()/fromGOON().
// CUSTOM_TAG has no GOON-native form, so it rides the JSON convention
// {"$customTag":{"name":...,"value":...}} that the core parser recognizes --
// the tag name and payload both survive instead of degrading to null.
//
// DATETIME round-trip limitation: DATETIME values serialize as ISO-8601 strings
// (e.g. "2024-01-01T12:00:00Z"), which the core JSON parser auto-detects and
// re-parses as DATETIME on read -- so a STRING that happens to hold an ISO-8601
// text cannot be round-tripped as STRING; it will decode back as DATETIME. The
// encoder does NOT special-case these strings (unquoted or quoted), because the
// decoder sees the same token either way and re-interprets it. This is a known
// lossy boundary, not a bug in the encoder.
//
// Number grammar: a leading '+' is not part of a number, so hand-written
// "+5" / "+Infinity" / "+NaN" are plain STRINGS (the encoder quotes such
// strings too). A sign inside an exponent ("1e+5") is accepted.
//
// Prototype keys: the spec asks implementations to reject the keys
// __proto__ / constructor / prototype, which guards against prototype-chain
// pollution in JavaScript hosts. That class of bug cannot occur here -- keys
// live in a std::map/unordered_map, never in an object prototype -- so they are
// accepted as ordinary keys and round-trip like any other member.
//
// Known limitations: Tabular column names are NOT unescaped on read
// (except when explicitly quoted) - the encoder never emits escaped bare
// identifiers so this is a robustness-only gap for external input.  Column
// sorting (`autoSort`) runs unconditionally in Compact mode; Balanced/LLM
// preserve insertion order (already lost by unordered_map).
//
// Tabular row cells: the flat `cell1,cell2` row syntax has no escape, so the
// decoder splits rows with a quote- AND brace-aware scan (commas inside a
// balanced marker group belong to that cell) and the encoder refuses the table
// form for any cell that goonCellFit() rejects, falling back to list format
// rather than corrupting the row. A cell is rejected when it carries:
//   - a depth-0 comma (e.g. `__REGEX__a,b|c`) -- it would split the cell;
//   - a raw newline -- it would end the row;
//   - unbalanced `{`/`[`/`}`/`]` or an unterminated quote (e.g. `a[b`) -- the
//     decoder's depth tracking would run past the cell and merge the columns.
// A depth-0 ':' (e.g. `__REGEX__a:b`) is NOT a rejection: the table stays
// tabular and the rows are indented one level deeper, which keeps it
// unambiguous. See goonCellFit().
//
// Tabular detection threshold: an array of objects is written as a table only
// when it has at most 5 elements, in the Balanced and LLM modes; a longer array
// falls back to the list form (past a handful of rows the table saves little and
// the list reads better). Compact mode always keeps the table. This is a
// formatting choice, not a data change -- both forms round-trip.
//
// REGEX markers: the payload is a bare token (no quotes, no braces), so
// goonEscapeRegPayload() backslash-escapes every byte that could otherwise end
// the cell, the row, the line, or look like structure: '\' and '|' (the '|'
// separates pattern from options), ':', '\n' and '\r'. A pattern may legally
// contain a real newline, and a raw one used to split one cell into two bogus
// rows. Because '\' is escaped first, a pattern holding a literal backslash-n
// stays distinguishable from one holding a real newline. The decoder undoes
// exactly this set, so hand-written documents using the older unescaped form
// still decode unchanged.
//
// Root values: a complete single-line flow collection (`{"a":1}`, `[1,2,3]`), a
// special-marker token, or a bare scalar is a VALUE, not a block mapping. The
// marker case matters because a pattern may contain an unquoted ':'
// (`__REGEX__a:b`), which used to split into {"__REGEX__a":"b"} and lose the
// type. A block header (`[N]{...}:`, `[]:`, `key: value`) still wins over these
// shortcuts.

#include "../core.hpp"

namespace asvJSONInternal {

inline constexpr size_t MAX_REPEAT_COUNT = 10000;
// Cap on intermediate JSON output to prevent memory amplification from RLE /
// tabular rows (a short GOON document can expand to gigabytes of JSON).
inline constexpr size_t MAX_GOON_OUTPUT = 64u * 1024u * 1024u;

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
  if (!v) { out += "null"; return; }
  // Container cap at depth + 1, containers only: this inline JSON cell is
  // parsed by the core parser, whose container cap counts EVERY level the
  // GOON document already emitted, so an inline cell that nests MAX levels
  // under a root array overshoots the limit and the document fromGOON() then
  // rejects it. Scalars still render at any depth.
  if ((v->type == asvJSONValue::OBJECT || v->type == asvJSONValue::ARRAY) &&
      !asvJSONValue::checkNestingDepth(depth + 1)) { out += "null"; return; }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += "null"; return;
    case asvJSONValue::BOOL_VAL: out += (v->flag ? "true" : "false"); return;
    case asvJSONValue::INT: out += std::to_string(v->num); return;
    case asvJSONValue::DOUBLE:
      if (!fmtNaNInfVal(v->dbl, true, out)) fmtDoubleValDot0(v->dbl, out);
      return;
    case asvJSONValue::STRING:
      out += '"';
      appendJsonEscaped(out, v->str_data);
      out += '"';
      return;
    case asvJSONValue::DATETIME: {
      out += '"';
      fmtDateTimeVal(v->timestamp, v->datetime_ms, out, v->tz_offset_min);
      out += '"';
      return;
    }
    case asvJSONValue::BINARY:
      // An empty payload still uses the $binary marker (base64 of nothing is
      // "") so a zero-length BINARY decodes back to BINARY instead of "".
      out += "{\"$binary\":{\"base64\":\"";
      out += encodeBase64(v->bin_data.data(), v->bin_data.size());
      out += "\",\"subType\":\"00\"}}";
      return;
    case asvJSONValue::EXTENSION: {
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
    case asvJSONValue::CUSTOM_TAG: {
      // Unknown tags ride the {"$customTag":{"name":...,"value":...}} marker
      // the core JSON parser recognizes, so both the tag name and its payload
      // survive a round-trip instead of degrading to null. A bare tag (no
      // payload) carries an explicit null value, matching yaml/ron.
      out += "{\"$customTag\":{\"name\":\"";
      appendJsonEscaped(out, v->str_data);
      out += "\",\"value\":";
      if (v->custom_value) goonJsonText(v->custom_value.get(), out, depth + 1);
      else out += "null";
      out += "}}";
      return;
    }
    default: out += "null"; return;
  }
}
// Escape one half (pattern or options) of a REGEX marker payload. The set is
// exactly what the decoder's unescapeRegPayload() undoes; keep the two in sync.
inline std::string goonEscapeRegPayload(std::string_view x) {
  std::string r;
  r.reserve(x.size());
  for (char c : x) {
    switch (c) {
      case '\\': r += "\\\\"; break;
      case '|':  r += "\\|";  break;
      case ':':  r += "\\:";  break;
      case '\n': r += "\\n";  break;
      case '\r': r += "\\r";  break;
      default:   r += c;      break;
    }
  }
  return r;
}
inline std::string goonFormatSpecial(const asvJSONValue* v) {
  if (!v) return "_";
  switch (v->type) {
    case asvJSONValue::DATETIME: {
      std::string dt; fmtDateTimeVal(v->timestamp, v->datetime_ms, dt, v->tz_offset_min);
      return '"' + dt + '"';
    }
    case asvJSONValue::BINARY: {
      // No empty-payload special case: "__BASE64__" with nothing after it is
      // the marker for a zero-length BINARY and the decoder restores the type.
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
      // REGEX type. The payload is a bare token -- no quotes, no braces -- so
      // every byte that could terminate a cell, a row or a line is escaped:
      //   '\' and '|'  structural (the '|' separates pattern from options)
      //   ':'          a depth-0 colon makes a decoder mistake the cell for a
      //                `key: value` sibling (and at the root, the whole
      //                document for a mapping)
      //   '\n' '\r'    a raw newline ends the ROW, splitting one cell into two
      //                (and a REGEX pattern may legitimately contain one)
      // The decoder unescapes exactly this set, and since '\' is itself escaped
      // first, a pattern containing a literal backslash-n stays distinguishable
      // from one containing a real newline.
      size_t sep = v->str_data.rfind('\0');
      std::string pat = (sep != std::string::npos) ? v->str_data.substr(0, sep) : v->str_data;
      std::string opts = (sep != std::string::npos && sep + 1 < v->str_data.size()) ? v->str_data.substr(sep + 1) : std::string();
      std::string result = "__REGEX__" + goonEscapeRegPayload(pat);
      if (!opts.empty()) result += '|' + goonEscapeRegPayload(opts);
      return result;
    }
    case asvJSONValue::TIMESTAMP:
      return "__TS__" + std::to_string(v->num);
    case asvJSONValue::EXTENSION: {
      // "__EXT__<type>_" with an empty payload is a valid zero-length
      // EXTENSION; the decoder maps the marker to a typed $binary value.
      std::string r("__EXT__");
      r += std::to_string(v->ext_type);
      r += '_';
      r += encodeBase64(v->bin_data.data(), v->bin_data.size());
      return r;
    }
    case asvJSONValue::CUSTOM_TAG: {
      // The block/list/table path has no dedicated GOON tag form, so reuse the
      // JSON marker text goonJsonText builds (decoded by the same JSON parser).
      std::string r;
      goonJsonText(v, r);
      return r;
    }
    default: return {};
  }
}

inline bool goonNeedsQuotes(const std::string& s) {
  if (s.empty()) return true;
  if (s.front() == '"') return true;
  // A '"' anywhere inside an unquoted key would confuse the decoder's
  // quote-tracking scan: it opens inQ at the interior '"', never closes it,
  // and a later ':' is consumed as "inside quotes" rather than as the
  // separator. Quote the whole key so goonWriteKey emits "key" instead.
  if (s.find('"') != std::string::npos) return true;
  // Control characters (tab included) and DEL cannot appear in an unquoted
  // scalar: a root value of just "\t" was emitted bare, stripped as
  // whitespace, and rejected as "unrecognized line". Quoting routes it through
  // appendJsonEscaped, which escapes the byte.
  for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return true;
  for (char c : s) if (c == ',' || c == ':' || c == '*') return true;
  if (s.front() == ' ' || s.back() == ' ') return true;
  if (s.front() == '$' || s.front() == '#' || s.front() == '^' || (s.front() >= '0' && s.front() <= '9') || s.front() == '-')
    return true;
  if (s.front() == '+' && s.size() > 1 && s[1] >= '0' && s[1] <= '9') return true;
  // A leading closing bracket is just as ambiguous at the root: a bare "}"
  // line is rejected as "unrecognized line ... in object context" and "]" is
  // read as a flow-collection closer. Quote both, like their openers.
  if (s.front() == '{' || s.front() == '[' || s.front() == '}' || s.front() == ']') return true;
  if (s.size() >= 2 && s[0] == '_' && s[1] == '_') return true;
  if (s == "T" || s == "F" || s == "_" || s == "~" || s == "true" || s == "false" || s == "null")
    return true;
  if (s == "NaN" || s == "Infinity" || s == "-Infinity") return true;
  return false;
}

inline void goonWriteString(const std::string& s, std::string& out, const std::unordered_map<std::string, int>* dict, const GoonWriteOptions& opts = {}) {
  if (dict) {
    auto it = dict->find(s);
    if (it != dict->end()) {
      out += "$" + std::to_string(it->second);
      return;
    }
  }
  if (opts.alwaysQuoteStrings || goonNeedsQuotes(s)) out += '"' + [](const std::string& x) {
    std::string r; appendJsonEscaped(r, x); return r;
  }(s) + '"';
  else out += s;
}

inline void goonWriteKey(const std::string& key, std::string& out, const GoonWriteOptions& opts = {}) {
  if (key.empty() || opts.alwaysQuoteKeys || goonNeedsQuotes(key) ||
      key.find('[') != std::string::npos || key.find(']') != std::string::npos ||
      key.find('"') != std::string::npos) {
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
  // Spec: dictionary holds strings longer than 2 chars, ordered by savings
  // ((len - 2) * occurrences) so the most valuable entries win the smallest indices.
  std::vector<std::pair<long long, std::string>> cand;
  for (const auto& [s, count] : freq)
    if (count >= 2 && s.size() > 2)
      cand.push_back({static_cast<long long>(s.size() - 2) * count, s});
  std::sort(cand.begin(), cand.end(),
    [](const auto& a, const auto& b) {
      if (a.first != b.first) return a.first > b.first;
      return a.second < b.second;
    });
  // Cap the dictionary so a huge document cannot produce an unbounded `$:`
  // header (the most valuable entries keep the smallest indices).
  static constexpr size_t MAX_GOON_DICT_ENTRIES = 256;
  if (cand.size() > MAX_GOON_DICT_ENTRIES) cand.resize(MAX_GOON_DICT_ENTRIES);
  std::unordered_map<std::string, int> dict;
  for (size_t i = 0; i < cand.size(); i++) dict[cand[i].second] = static_cast<int>(i);
  return dict;
}

inline void goonWriteDictHeader(const std::unordered_map<std::string, int>& dict, std::string& out) {
  if (dict.empty()) return;
  out += "$:";
  std::vector<std::pair<std::string, int>> entries(dict.begin(), dict.end());
  std::sort(entries.begin(), entries.end(),
    [](auto& a, auto& b) { return a.second < b.second; });
  for (auto& [s, idx] : entries)
    out += "$" + std::to_string(idx) + "=\"" + [](const std::string& x) {
      std::string r; appendJsonEscaped(r, x); return r;
    }(s) + "\",";
  out.pop_back();
  out += '\n';
}





// Why a rendered cell cannot appear in the flat `val1,val2` tabular row syntax.
//   Ok        representable as-is
//   BareColon a depth-0 ':' -- the table still works, but the rows must be
//             indented one level deeper (see goonSerializeArray's bareColon path).
//             Reason: the decoder's "is this a sibling mapping line" scan, which
//             decides whether a row sitting at the header's own indentation ends
//             the table, also honours '\' only inside quotes -- so a cell like
//             `__REGEX__a\:b` still reads as `key: value` to that scan.
//   Unsafe    not representable at all -- fall back to list format
// Beyond the separator question, a cell must not contain a raw newline (it ends
// the ROW, splitting one cell into two) and must leave the decoder's quote/depth
// tracking balanced: a stray '[' or an unterminated quote makes the splitter run
// past the cell, merging cells or splitting one -- `a[b,1` read back as a single
// cell "a[b,1" with the second column null. A depth-0 '*' is rejected too, since
// it would start an RLE repeat count. Empty cells are always fine.
//
// DELIBERATE ASYMMETRY with the decoder's row splitter (the depth/quote scan in
// goonToJson that cuts a row into cells). The three relevant scans do not agree
// on what '\' means, so each is described by what it suppresses:
//
//   this function    '\' escapes ONLY inside quotes.
//   row splitter     '\' sets a flag anywhere; the next character then skips the
//                    quote-delimiter branch ONLY. It is still brace-counted and
//                    still tested as a ',' separator -- which is why `a\{b}` and
//                    `a\,b` behave the way they do.
//   RLE scan         '\' sets a flag anywhere and the next character is skipped
//                    ENTIRELY (before quote, brace and '*' handling), and it
//                    additionally requires ci > 0.
//
// The first two are what matters here, and they disagree only on hand-written
// cells containing a lone backslash outside quotes. The direction is safe:
//
//   - This function is the stricter of the two, so the disagreement always
//     resolves toward Unsafe -> list format, where every value is quoted and
//     re-joined. `a\{b,c` is Unsafe here (unbalanced brace) although the row
//     splitter would have kept it in one cell; the extra fallback is wasted
//     work, never a corrupted row.
//   - The reverse -- this function saying Ok while the row splitter still cut
//     the cell -- cannot happen, which is the property that actually matters.
//     Braces are counted identically in both: a backslash outside quotes sets no
//     escape flag here, and the row splitter still increments depth for the
//     brace it follows, so `a\{b}` balances in both. And an escaped '"' outside
//     quotes leaves this function with inQ set, which the final
//     `depth != 0 || inQ` check turns into Unsafe.
//
// So the two are deliberately NOT synchronized. Loosening this to match the row
// splitter would let a lone `\{` emit a cell the splitter mangles; tightening the
// row splitter to match would change how existing documents decode.
enum class GoonCellFit { Ok, BareColon, Unsafe };
inline GoonCellFit goonCellFit(std::string_view cell) {
  if (cell.empty()) return GoonCellFit::Ok;
  bool inQ = false, esc = false;
  int depth = 0;
  bool sawColon = false;
  for (char c : cell) {
    if (esc) { esc = false; continue; }
    // Only meaningful inside quotes: see the asymmetry note above.
    if (inQ && c == '\\') { esc = true; continue; }
    if (c == '"') { inQ = !inQ; continue; }
    if (inQ) continue;
    if (c == '\n' || c == '\r') return GoonCellFit::Unsafe;
    if (c == '{' || c == '[') { depth++; continue; }
    if (c == '}' || c == ']') { if (depth == 0) return GoonCellFit::Unsafe; depth--; continue; }
    if (depth != 0) continue;
    // A bare comma has no escape in the row syntax, so the table is
    // unrepresentable -- a comma wins over a colon, as before.
    if (c == ',') return GoonCellFit::Unsafe;
    // '*' starts an RLE repeat count (`row*N`), which the decoder's RLE scan
    // looks for at depth 0 outside quotes; a non-numeric tail there is the error
    // "GOON: invalid RLE repeat". A STRING is safe because goonNeedsQuotes()
    // quotes any text containing '*', but a REGEX payload bypasses that check
    // and is emitted bare, so `a*b` as a pattern needs this guard.
    // Slightly over-strict: that scan requires ci > 0, so a '*' at row position 0
    // is not treated as a marker. This function sees a cell, not its column, and
    // a leading '*' is only at row position 0 in the FIRST column -- tracking the
    // column to allow the other cases would complicate the API for a pattern
    // (`*` first) that most regex engines reject anyway. Kept strict: the cost
    // is a list-format fallback, never a mis-parsed row.
    if (c == '*') return GoonCellFit::Unsafe;
    if (c == ':') sawColon = true;
  }
  if (depth != 0 || inQ) return GoonCellFit::Unsafe;
  return sawColon ? GoonCellFit::BareColon : GoonCellFit::Ok;
}

inline void goonSerializeArray(const asvJSONValue* arr, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict = nullptr, const GoonWriteOptions& opts = {}) {
  if (!arr || arr->size() == 0) {
    if (!key.empty() || depth > 0) {
      int iw = opts.indentWidth >= 1 ? opts.indentWidth : 1;
      std::string pad(static_cast<size_t>(indent) * static_cast<size_t>(iw), ' ');
      out += pad;
      goonWriteKey(key, out, opts);
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
    // Spec: sort columns alphabetically in Compact mode only; Balanced/LLM keep
    // the (unordered_map) iteration order.  Since ObjectMapT is insertion-ordered
    // by default this is a formal deviation from round-trip fidelity, not a practical bug.
    if (opts.mode == GoonWriteOptions::Mode::Compact) std::sort(cols.begin(), cols.end());

    for (size_t i = 1; i < arr->size(); i++) {
      auto* obj = arr->get(i);
      if (!obj || !obj->obj || obj->obj->size() != cols.size()) { tabular = false; break; }
      std::vector<std::string> rowKeys;
      rowKeys.reserve(obj->obj->size());
      for (const auto& [k, _] : *(obj->obj)) rowKeys.push_back(k);
      if (opts.mode == GoonWriteOptions::Mode::Compact) std::sort(rowKeys.begin(), rowKeys.end());
      if (rowKeys != cols) { tabular = false; break; }
    }
  }
  // A tabular cell cannot represent a nested OBJECT/ARRAY value (the flat
  // `val1,val2` cell syntax has no container encoding), so fall back to list
  // format which serializes object/array items robustly.
  if (tabular) {
    for (size_t r = 0; r < arr->size() && tabular; r++) {
      auto* obj = arr->get(r);
      if (!obj || !obj->obj) { tabular = false; break; }
      for (const auto& c : cols) {
        auto* ch = obj->getConst(c);
        if (ch && (ch->type == asvJSONValue::OBJECT || ch->type == asvJSONValue::ARRAY)) { tabular = false; break; }
      }
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
  // Render every cell up front: the flat `val1,val2` row syntax has no escape
  // for a bare comma, so a cell carrying one forces the list format below.
  int iw = opts.indentWidth >= 1 ? opts.indentWidth : 1;
  std::string pad(static_cast<size_t>(indent) * static_cast<size_t>(iw), ' ');
  std::vector<std::vector<std::string>> grid;
  bool bareColon = false;
  // Any goonCellFit() == Unsafe verdict, whatever its cause (comma, '*', raw
  // newline, unbalanced brace, unterminated quote) -- not just a comma, hence
  // the name. Set -> the table is unrepresentable in the flat row syntax and
  // falls back to list format.
  bool forceList = false;
  if (tabular && !cols.empty()) {
    auto buildCells = [&](const asvJSONValue* obj) {
      std::vector<std::string> cells;
      cells.reserve(cols.size());
      for (size_t c = 0; c < cols.size(); c++) {
        auto* child = obj->getConst(cols[c]);
        std::string cell;
        if (!child) { cell = "_"; }
        else {
          std::string lit = goonLiteral(child);
          if (!lit.empty()) { cell = lit; }
          else if (child->type == asvJSONValue::INT) cell = std::to_string(child->num);
          else if (child->type == asvJSONValue::DOUBLE) {
            double d = child->dbl;
            if (!fmtNaNInfVal(d, true, cell)) fmtDoubleValDot0(d, cell);
          } else if (child->type == asvJSONValue::STRING) {
            goonWriteString(child->str_data, cell, dict, opts);
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
        cells.push_back(cell);
      }
      return cells;
    };
    grid.reserve(arr->size());
    for (size_t r = 0; r < arr->size(); r++) {
      grid.push_back(buildCells(arr->get(r)));
      // Keep scanning after a BareColon verdict: a later row may hold a cell
      // that is Unsafe outright, and Unsafe wins over BareColon.
      for (const auto& cell : grid.back()) {
        GoonCellFit fit = goonCellFit(cell);
        if (fit == GoonCellFit::Unsafe) { forceList = true; break; }
        if (fit == GoonCellFit::BareColon) bareColon = true;
      }
      if (forceList) break;
    }
    // A cell the flat `val1,val2` row syntax cannot carry makes the whole table
    // unrepresentable, so fall back to list format, which quotes such values
    // robustly. See goonCellFit() for the full list of causes.
    if (forceList) tabular = false;
  }
  if (tabular && !cols.empty()) {
    // Spec minimalIndent: rows of llm/compact tables sit at the header's own
    // indentation, so the table stays aligned to its header ("minimal"). That
    // layout is only unambiguous while no bare cell contains ':' (a regex
    // pattern with a colon, etc.)  -  the decoder would mistake such a cell for a
    // `key: value` sibling. Balanced mode always keeps rows one level past the
    // header, and ambiguous tables fall back to the same one-level layout.
    bool minimalRows = (opts.mode != GoonWriteOptions::Mode::Balanced) && !bareColon;
    std::string rowPad = minimalRows ? pad : pad + std::string(static_cast<size_t>(iw), ' ');
    if (!key.empty() || depth > 0) {
      out += pad;
      goonWriteKey(key, out, opts);
    }
    // Field names go through goonWriteKey, NOT a bare appendJsonEscaped: the
    // decoder reads a BARE field name verbatim (only surrounding spaces are
    // trimmed) and unescapes only a fully QUOTED name. Writing `a\tb` escaped
    // but unquoted made the key come back as the literal 4 chars `a\tb`, and a
    // key holding a comma split into two columns - so a table whose column name
    // contained a TAB, a control byte, a quote, a comma or a '}' silently
    // changed the key and enc(dec(enc(t))) != enc(t).
    out += "[" + std::to_string(arr->size()) + "]{";
    goonWriteKey(cols[0], out, opts);
    for (size_t i = 1; i < cols.size(); i++) {
      out += ",";
      goonWriteKey(cols[i], out, opts);
    }
    out += "}:\n";
    // RLE (all modes): consecutive identical rows become a single `row*N`;
    // otherwise each column uses `^` for a repeated previous cell. Runs longer
    // than MAX_REPEAT_COUNT are emitted in chunks (a plain row for a leftover
    // chunk of 1) so data is never silently lost.
    std::vector<std::string> prevCells;
    std::vector<std::string> prevCols(cols.size());
    int repeatCount = 0;
    bool havePrev = false;
    auto emitCells = [&](const std::vector<std::string>& cells, int count, bool withCaret) {
      std::string row;
      for (size_t c = 0; c < cols.size(); c++) {
        if (c > 0) row += ',';
        if (withCaret && opts.mode != GoonWriteOptions::Mode::LLM && !prevCols[c].empty() && cells[c] == prevCols[c]) {
          row += '^';
        } else {
          row += cells[c];
          prevCols[c] = cells[c];
        }
      }
      int remaining = count;
      while (remaining > 0) {
        out += rowPad + row;
        int chunk = static_cast<int>((std::min)(static_cast<size_t>(remaining), MAX_REPEAT_COUNT));
        if (chunk > 1) out += "*" + std::to_string(chunk);
        out += '\n';
        remaining -= chunk;
      }
      if (!withCaret) prevCols = cells;
    };
    for (const auto& cells : grid) {
      if (havePrev && cells == prevCells) {
        repeatCount++;
        continue;
      }
      if (havePrev) {
        emitCells(prevCells, repeatCount, repeatCount == 1);
      }
      prevCells = cells;
      repeatCount = 1;
      havePrev = true;
    }
    if (havePrev) {
      emitCells(prevCells, repeatCount, repeatCount == 1);
    }
    return;
  }
  // List format
  std::string itemIndent(static_cast<size_t>(iw), ' ');
  if (!key.empty() || depth > 0) {
    out += pad;
    goonWriteKey(key, out, opts);
  }
  out += "[]:\n";
  for (size_t i = 0; i < arr->size(); i++) {
    auto* child = arr->get(i);
    if (!child) { out += pad + itemIndent + "- _\n"; continue; }
    std::string lit = goonLiteral(child);
    if (!lit.empty()) {
      out += pad + itemIndent + "- " + lit + "\n";
    } else if (child->type == asvJSONValue::OBJECT) {
      out += pad + itemIndent + "-\n";
      if (child->obj) {
        // Keys of an object list-item sit one level deeper than the `-` marker
        // (indent + 2 levels from the list header).
        for (const auto& [k, sub] : *(child->obj))
          goonSerializeVal(sub.get(), out, indent + 2, k, depth + 1, dict, opts);
      }
    } else if (child->type == asvJSONValue::ARRAY) {
      // Nested array inside a list item serializes as an inline `[...]` cell.
      // The decoder passes `[...]` through to the JSON parser verbatim, so the
      // elements must be valid JSON text (GOON literals like T/_/~ and the
      // __OID__ prefixes are invalid there and would corrupt the value).
      // The cell's OWN bracket is written here, not by goonJsonText, so this
      // level must be counted here too: the item sits at level depth + 2, and
      // past the cap the item degrades to GOON null (`- _`) instead of
      // emitting a cell the core parser would reject (wave 53: a chain of
      // nested arrays encoded one container too deep and fromGOON() failed
      // with "Maximum nesting depth exceeded").
      if (!asvJSONValue::checkNestingDepth(depth + 2)) {
        out += pad + itemIndent + "- _\n";
        continue;
      }
      out += pad + itemIndent + "- [";
      for (size_t j = 0; j < child->size(); j++) {
        if (j > 0) out += ',';
        // goonJsonText caps at `depth + 1`, i.e. it treats its `depth` argument
        // as "the level this node's PARENT occupies". The cell's own bracket is
        // the item at level depth + 2, so its elements start one level deeper.
        goonJsonText(child->get(j), out, depth + 2);
      }
      out += "]\n";
    } else {
      out += pad + itemIndent + "- ";
      if (child->type == asvJSONValue::INT) out += std::to_string(child->num);
      else if (child->type == asvJSONValue::DOUBLE) {
        if (!fmtNaNInfVal(child->dbl, true, out)) fmtDoubleValDot0(child->dbl, out);
      } else {
        std::string spec = goonFormatSpecial(child);
        if (!spec.empty()) out += spec;
        else {
          goonWriteString(child->str_data, out, dict, opts);
        }
      }
      out += '\n';
    }
  }
}

inline void goonSerializeVal(const asvJSONValue* v, std::string& out, int indent, const std::string& key, int depth, const std::unordered_map<std::string, int>* dict = nullptr, const GoonWriteOptions& opts = {}) {
  if (!v) return;
  int iw = opts.indentWidth >= 1 ? opts.indentWidth : 1;
  std::string pad(static_cast<size_t>(indent) * static_cast<size_t>(iw), ' ');
  // Container cap at depth + 1, containers only (the JSON5/TOON convention).
  // The old guard (`checkNestingDepth(depth)`) let the encoder emit MAX+1
  // containers while fromGOON() accepts MAX, so a deep raw-API tree produced
  // a document its own decoder rejected; and returning without writing
  // anything dropped the whole key line. A past-cap container now degrades to
  // the GOON null literal, so the key survives and the document parses.
  if ((v->type == asvJSONValue::OBJECT || v->type == asvJSONValue::ARRAY) &&
      !asvJSONValue::checkNestingDepth(depth + 1)) {
    if (!key.empty()) {
      out += pad;
      goonWriteKey(key, out, opts);
      out += ": _\n";
    } else out += pad + "_\n";
    return;
  }
  std::string lit = goonLiteral(v);
  if (!lit.empty()) {
    if (!key.empty() || depth > 0) {
      out += pad;
      goonWriteKey(key, out, opts);
      out += ": " + lit + "\n";
    }
    else out += pad + lit + "\n";
    return;
  }
  switch (v->type) {
    case asvJSONValue::INT:
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out, opts);
        out += ": " + std::to_string(v->num) + "\n";
      }
      else out += pad + std::to_string(v->num) + "\n";
      break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      std::string dv;
      if (!fmtNaNInfVal(d, true, dv)) fmtDoubleValDot0(d, dv);
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out, opts);
        out += ": " + dv + "\n";
      } else {
        out += pad + dv + '\n';
      }
      break;
    }
    case asvJSONValue::STRING: {
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out, opts);
        out += ": ";
        goonWriteString(v->str_data, out, dict, opts);
        out += '\n';
      } else {
        goonWriteString(v->str_data, out, dict, opts);
        out += '\n';
      }
      break;
    }
    case asvJSONValue::OBJECT: {
      if (!v->obj || v->obj->empty()) {
        if (!key.empty() || depth > 0) {
          out += pad;
          goonWriteKey(key, out, opts);
          out += ": {}\n";
        }
        else out += pad + "{}\n";
        return;
      }
      if (!key.empty() || depth > 0) {
        out += pad;
        goonWriteKey(key, out, opts);
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
          goonWriteKey(key, out, opts);
          out += ": " + spec + "\n";
        }
        else out += pad + spec + "\n";
      } else {
        if (!key.empty()) {
          out += pad;
          goonWriteKey(key, out, opts);
          out += ": _\n";
        }
        else out += pad + "_\n";
      }
      break;
    }
  }
}

// Inline `{...}`/`[...]` cells: quote unquoted identifier keys so the text is
// valid JSON (`{a:1}` -> `{"a":1}`). Nested objects/arrays and already-quoted
// pairs are preserved. Returns the input unchanged if no quoting was needed or
// the text is malformed (the caller then passes it through as-is).
static bool qgoonObj(std::string_view s, std::string& r, std::size_t& i, int depth = 0);
static bool qgoonArr(std::string_view s, std::string& r, std::size_t& i, int depth = 0);

static bool qgoonObj(std::string_view s, std::string& r, std::size_t& i, int depth) {
  if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) return false;
  r += s[i++];
  bool changed = false;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) r += s[i++];
    if (i >= s.size() || s[i] == '}') {
      if (i < s.size()) r += s[i++];
      return changed;
    }
    if (s[i] == '"') {
      r += s[i++];
      bool esc = false;
      while (i < s.size()) {
        char k = s[i]; r += k; i++;
        if (esc) esc = false;
        else if (k == '\\') esc = true;
        else if (k == '"') break;
      }
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) r += s[i++];
      if (i < s.size() && s[i] == ':') r += s[i++];
    } else {
      std::string tok;
      while (i < s.size() && s[i] != ':' && s[i] != ',' && s[i] != '}' && s[i] != '{' && s[i] != ' ' && s[i] != '\t') {
        tok += s[i]; i++;
      }
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
      if (i < s.size() && s[i] == ':') {
        if (!tok.empty()) { r += '"'; r += tok; r += '"'; changed = true; }
        r += s[i++];
      } else {
        return false; // malformed: no ':' after key position
      }
    }
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) r += s[i++];
    if (i >= s.size()) return changed;
    if (s[i] == '{') {
      if (!qgoonObj(s, r, i, depth + 1)) return false;
      changed = true;
    } else if (s[i] == '[') {
      if (!qgoonArr(s, r, i, depth + 1)) return false;
      changed = true;
    } else if (s[i] == '"') {
      r += s[i++];
      bool esc = false;
      while (i < s.size()) {
        char k = s[i]; r += k; i++;
        if (esc) esc = false;
        else if (k == '\\') esc = true;
        else if (k == '"') break;
      }
    } else {
      while (i < s.size() && s[i] != ',' && s[i] != '}') { r += s[i]; i++; }
    }
    if (i < s.size() && s[i] == ',') { r += s[i++]; continue; }
    if (i < s.size() && s[i] == '}') { r += s[i++]; return changed; }
    if (i >= s.size()) return changed;
    return false;
  }
  return changed;
}

static bool qgoonArr(std::string_view s, std::string& r, std::size_t& i, int depth) {
  if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) return false;
  r += s[i++];
  bool changed = false;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) r += s[i++];
    if (i >= s.size() || s[i] == ']') {
      if (i < s.size()) r += s[i++];
      return changed;
    }
    if (s[i] == '{') {
      if (!qgoonObj(s, r, i, depth + 1)) return false;
      changed = true;
    } else if (s[i] == '[') {
      if (!qgoonArr(s, r, i, depth + 1)) return false;
      changed = true;
    } else if (s[i] == '"') {
      r += s[i++];
      bool esc = false;
      while (i < s.size()) {
        char k = s[i]; r += k; i++;
        if (esc) esc = false;
        else if (k == '\\') esc = true;
        else if (k == '"') break;
      }
    } else {
      while (i < s.size() && s[i] != ',' && s[i] != ']') { r += s[i]; i++; }
    }
    if (i < s.size() && s[i] == ',') { r += s[i++]; continue; }
    if (i < s.size() && s[i] == ']') { r += s[i++]; return changed; }
    if (i >= s.size()) return changed;
    return false;
  }
  return changed;
}

static std::string goonQuoteInlineKeys(std::string_view s) {
  std::string r;
  std::size_t i = 0;
  if (s.size() >= 2 && s[0] == '{' && s.back() == '}') {
    if (qgoonObj(s, r, i) && i == s.size()) return r;
  } else if (s.size() >= 2 && s[0] == '[' && s.back() == ']') {
    if (qgoonArr(s, r, i) && i == s.size()) return r;
  }
  return std::string(s);
}

// Quote/escape-aware scan: returns the index of the first `target` that is
// NOT inside a double-quoted JSON string. Inside quotes, `\` escapes the next
// char (so `\"` never closes a quoted key); outside quotes `\` is literal, so a
// bare key ending in `\` (e.g. `a\: v`) still finds its separator. Callers that
// previously used a `content[ci-1] != '\\'` toggle mis-handled keys whose final
// character was a backslash (`"foo:bar\\": v`), because the escaped quote was
// never counted and the line was silently dropped.
inline size_t goonFindUnquoted(std::string_view s, char target) {
  bool inQ = false, esc = false;
  for (size_t ci = 0; ci < s.size(); ci++) {
    char c = s[ci];
    if (esc) { esc = false; continue; }
    if (inQ && c == '\\') { esc = true; continue; }
    if (c == '"') { inQ = !inQ; continue; }
    if (!inQ && c == target) return ci;
  }
  return std::string_view::npos;
}

// True when `s` is an integer literal: at least one digit, optional '-' sign.
// Used to validate the __TS__ payload before splicing it into JSON.
inline bool goonAllIntDigits(std::string_view s) {
  size_t i = (!s.empty() && s[0] == '-') ? 1 : 0;
  if (i >= s.size()) return false;
  for (; i < s.size(); i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

// True when `s` begins with one of the GOON special-marker prefixes. Such a token
// is a VALUE, never the left half of a `key: value` pair: its payload is opaque
// and may contain an unquoted ':' (a REGEX pattern `a:b`) or ','. Splitting a
// root `__REGEX__a:b` at that colon produced {"__REGEX__a":"b"} -- the REGEX type
// destroyed and the document no longer re-encoded as a marker. Keys with such a
// prefix are unambiguous in practice because goonNeedsQuotes() forces a key
// starting with "__" to be quoted.
inline bool goonIsSpecialMarker(std::string_view s) {
  // Fast path: if the token is shorter than the prefix it cannot match.
  if (s.size() >= 9 && s.compare(0, 9, "__REGEX__") == 0) return true;
  if (s.size() >= 6 && s.compare(0, 6, "__TS__") == 0) return true;
  if (s.size() >= 7 && s.compare(0, 7, "__OID__") == 0) return true;
  if (s.size() >= 10 && s.compare(0, 10, "__BASE64__") == 0) return true;
  if (s.size() >= 7 && s.compare(0, 7, "__EXT__") == 0) return true;
  return false;
}

// True when `s` is a single self-contained flow collection: it opens with the
// given bracket, closes it before any trailing content, and stays balanced
// (braces inside quoted strings are ignored). Used to accept a root value such
// as {"$customTag":{...}} as one scalar instead of a block mapping.
inline bool goonIsCompleteFlow(std::string_view s, char open, char close) {
  if (s.size() < 2 || s.front() != open) return false;
  int depth = 0;
  bool inQ = false, esc = false;
  for (size_t ci = 0; ci < s.size(); ci++) {
    char c = s[ci];
    if (esc) { esc = false; continue; }
    if (inQ && c == '\\') { esc = true; continue; }
    if (c == '"') { inQ = !inQ; continue; }
    if (inQ) continue;
    if (c == open) depth++;
    else if (c == close) {
      depth--;
      if (depth == 0) {
        // Nothing but spaces/tabs may follow the closing bracket.
        for (size_t k = ci + 1; k < s.size(); k++)
          if (s[k] != ' ' && s[k] != '\t') return false;
        return true;
      }
      if (depth < 0) return false;
    }
  }
  return false;
}

// GOON -> JSON text converter (GOON decoder)
// GOON -> JSON text converter (GOON decoder).
// `maxOut` caps intermediate JSON to prevent memory amplification from RLE /
// tabular rows / dict-references. Pass SIZE_MAX to disable the cap (useful for
// testing; the default is MAX_GOON_OUTPUT = 64 MB).
inline std::string goonToJson(std::string_view input, size_t maxOut = MAX_GOON_OUTPUT) {
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
        try { val = unescapeJsonString(raw, true); }
        catch (const std::runtime_error&) { throw asvJSONError("GOON: invalid escape in string"); }
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
      bool digits = true;
      for (size_t di = 1; di < s.size(); di++)
        if (s[di] < '0' || s[di] > '9') { digits = false; break; }
      std::string idx(s.substr(1));
      auto it = dict.find(idx);
      if (it != dict.end()) return it->second;
      // Spec: a $N reference whose index is out of bounds is a decode error.
      if (digits)
        throw GoonDecodeError("GOON: dictionary reference $" + idx + " out of bounds");
    }
    return std::string(s);
  };

  // Find first content line
  size_t firstLine = lineStart;
  while (firstLine < lines.size() && lines[firstLine].empty()) firstLine++;
  if (firstLine >= lines.size()) return "{}";

  std::string_view firstContent = stripIndent(lines[firstLine]);
  // A leading '-' marks a root sequence ONLY when it is a list marker ("-",
  // "- item"); negative-number scalars ("-5", "-1.5", "-Infinity", "-NaN")
  // must not be mistaken for an array root.
  bool minusScalar = (firstContent.size() >= 2 && firstContent[0] == '-') &&
    ((firstContent[1] >= '0' && firstContent[1] <= '9') ||
     firstContent[1] == '.' || firstContent[1] == 'i' || firstContent[1] == 'I' ||
     firstContent[1] == 'n' || firstContent[1] == 'N');
  bool rootIsArr = (firstContent.size() >= 2 && firstContent[0] == '[') ||
    (firstContent.size() >= 1 && firstContent[0] == '-' && !minusScalar);

  std::string out;
  std::vector<FormatFrame> stack;
  int rootIndent = countIndent(lines[firstLine]);

  // Guard against memory amplification: RLE / tabular rows can expand a short
  // GOON document into gigabytes of intermediate JSON. Check after each major
  // append in the hot loops so we reject before allocating unboundedly.
  auto checkOut = [&](const char* ctx) {
    if (maxOut != static_cast<size_t>(-1) && out.size() > maxOut)
      throw GoonDecodeError(std::string("GOON: output exceeds ") + std::to_string(maxOut / 1024 / 1024) + " MB limit" + (ctx ? std::string(" at ") + ctx : ""));
  };

  auto expandCell = [&](std::string_view raw) -> std::string {
    // A bare $N dictionary reference always denotes the dict *string* value,
    // so the resolved text must stay a quoted string  -  never reinterpreted as
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
    // Also accept bare JSON literals true/false/null so block-form and
    // inline-flow give the same type for the same text (JSON-superset promise).
    if (s == "T") return "true";
    if (s == "F") return "false";
    if (s == "_") return "null";
    if (s == "~") return "\"\"";
    if (s == "true") return "true";
    if (s == "false") return "false";
    if (s == "null") return "null";
    // Number. Per the GOON number grammar a leading '+' is NOT part of a
    // number, so "+5" stays the string "+5" -- which is also what the encoder
    // writes (goonNeedsQuotes quotes a leading '+'). A sign inside the exponent
    // ("1e+5") is still accepted below.
    if (!s.empty() && (s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) {
      bool isNum = true;
      bool hasDot = false, hasExp = false;
      for (size_t ci = (s[0] == '-') ? 1 : 0; ci < s.size(); ci++) {
        if (s[ci] >= '0' && s[ci] <= '9') continue;
        if (s[ci] == '.' && !hasDot && !hasExp) { hasDot = true; continue; }
        if ((s[ci] == 'e' || s[ci] == 'E') && !hasExp && ci > 0 && ci + 1 < s.size()) {
          hasExp = true; continue;
        }
        if ((s[ci] == '+' || s[ci] == '-') && hasExp && ci > 0 && (s[ci-1] == 'e' || s[ci-1] == 'E')) continue;
        isNum = false; break;
      }
      if (isNum && !s.empty()) return s;
    }
    // NaN/Infinity
    if (s == "NaN" || s == "-NaN" || s == "Infinity" || s == "-Infinity") return s;
    // GOON special prefixes  ->  JSON convention. Applied ONLY to unquoted cells:
    // a quoted value is literal text, so a string like "__TS__42" (which the
    // encoder now quotes) stays a string instead of becoming a timestamp.
    if (s.size() >= 1 && s.front() != '"') {
      std::string_view unquoted = s;
      // Fast path: if the token is shorter than the prefix it cannot match.
      if (unquoted.size() >= 6 && unquoted.compare(0, 6, "__TS__") == 0) {
        // Require a numeric payload. A bare `__TS__` has none, and interpolating
        // nothing produced `{"$timestamp":{"t":}}` -- invalid JSON that the JSON
        // layer then rejected with a foreign "Unexpected: }" message. Fall through
        // to the string quoting below instead, so the token stays a plain string.
        std::string_view tsd(unquoted.substr(6));
        if (goonAllIntDigits(tsd))
          return "{\"$timestamp\":{\"t\":" + std::string(tsd) + "}}";
      }
      if (unquoted.size() >= 7 && unquoted.compare(0, 7, "__OID__") == 0) {
        return "{\"$oid\":\"" + std::string(unquoted.substr(7)) + "\"}";
      }
      if (unquoted.size() >= 10 && unquoted.compare(0, 10, "__BASE64__") == 0) {
        return "{\"$binary\":{\"base64\":\"" + std::string(unquoted.substr(10)) + "\",\"subType\":\"00\"}}";
      }
      if (unquoted.size() >= 9 && unquoted.compare(0, 9, "__REGEX__") == 0) {
        // The encoder escapes '|', '\', ':', '\n' and '\r' inside the payload,
        // so split at the LAST unescaped '|' and unescape both sides. Because
        // '\' is escaped first, a payload holding a literal backslash-n stays
        // distinguishable from one holding a real newline.
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
            if (x[i] == '\\' && i + 1 < x.size()) {
              char n = x[i + 1];
              // Mirrors goonEscapeRegPayload()'s set exactly. An unrecognised
              // escape is left as-is (backslash included) rather than silently
              // dropping the backslash.
              if (n == '\\' || n == '|' || n == ':') { r += n; i++; continue; }
              if (n == 'n') { r += '\n'; i++; continue; }
              if (n == 'r') { r += '\r'; i++; continue; }
            }
            r += x[i];
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
      if (unquoted.size() >= 7 && unquoted.compare(0, 7, "__EXT__") == 0) {
        std::string rest(unquoted.substr(7));
        size_t us = rest.find('_');
        if (us != std::string::npos) {
          // The extension type is an int8_t, so only [-128, 127] is
          // representable. A bare strtol + & 0xFF mask used to silently wrap a
          // hand-written __EXT__300_... to 0x2c (44), losing the intended type
          // -- the same no-silent-data-loss rule enforced in TRON/ADE. Reject an
          // out-of-range or overflowing field instead (errno == ERANGE, plus the
          // end-pointer check, also catches trailing garbage).
          const std::string num = rest.substr(0, us);
          errno = 0;
          char* endp = nullptr;
          long ltype = std::strtol(num.c_str(), &endp, 10);
          if (num.empty() || endp == num.c_str() || *endp != '\0' || errno == ERANGE ||
              ltype < -128 || ltype > 127)
            throw GoonDecodeError("GOON: extension type out of range [-128, 127]");
          std::string b64 = rest.substr(us + 1);
          // The prefix declares a typed extension: an UNDECODABLE payload must
          // error like an out-of-range type does, not build an envelope whose
          // base64 the JSON layer (strict since the $binary hardening) would
          // silently refuse - the old path fabricated EXTENSION(type, EMPTY).
          {
            bool berr = false;
            decodeBase64Fast(b64.data(), b64.size(), &berr);
            if (berr) throw GoonDecodeError("GOON: extension payload is not valid base64");
          }
          char hbuf[3];
          snprintf(hbuf, sizeof(hbuf), "%02x", static_cast<unsigned>(static_cast<int>(ltype)) & 0xFF);
          return "{\"$binary\":{\"base64\":\"" + b64 + "\",\"subType\":\"" + hbuf + "\"}}";
        }
        return "{\"$binary\":{\"base64\":\"" + rest + "\",\"subType\":\"00\"}}";
      }
    }
    // Already JSON value (inline `{a:1}` cells get their unquoted keys quoted
    // so they parse as JSON)
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '{' && s.back() == '}') || (s.front() == '[' && s.back() == ']')))
      return goonQuoteInlineKeys(s);
    // Quote it
    return '"' + [](std::string_view x) {
      std::string r; appendJsonEscaped(r, x); return r;
    }(s) + '"';
  };

  // Bare scalar root document (e.g. a root INT/STRING/DATETIME serialized by
  // toGOON()). Without this, a bare first line fell through every branch of
  // the object path and silently produced "{}". Expanded to JSON here so
  // scalar roots round-trip exactly like the JSON layer.
  // Root value shortcuts. These run BEFORE the rootIsArr/rootIsObject split below
  // because a complete single-line flow collection looks like the start of a block
  // array (`[`) or block mapping, and must be recognized as a value instead.
  auto noTrailingLines = [&]() {
    for (size_t k = firstLine + 1; k < lines.size(); k++)
      if (!lines[k].empty())
        throw GoonDecodeError("GOON: unexpected trailing content after root value");
  };
  // A root marker object (CUSTOM_TAG, or any complete single-line flow object) is
  // a value, not a block mapping: {"$customTag":{...}}. A multi-line flow object
  // still falls through to the block path below.
  if (!rootIsArr && goonIsCompleteFlow(firstContent, '{', '}')) {
    noTrailingLines();
    return expandCell(firstContent);
  }
  // A single-line flow ARRAY at the root is a value too: `[1,2,3]` previously fell
  // through to the array path and failed with "unrecognized line 1 in object
  // context". GOON is a JSON superset, so a root JSON array must decode.
  if (goonIsCompleteFlow(firstContent, '[', ']')) {
    noTrailingLines();
    return expandCell(firstContent);
  }
  // A special-marker token at the root is a value, never `key: value`; see
  // goonIsSpecialMarker(). Without this a root REGEX pattern containing ':' parsed
  // as a mapping ({"__REGEX__a":"b"}), losing the type.
  if (goonIsSpecialMarker(firstContent)) {
    noTrailingLines();
    return expandCell(firstContent);
  }
  if (!rootIsArr) {
    bool hasColon = goonFindUnquoted(firstContent, ':') != std::string_view::npos;
    bool looksScalar = !hasColon &&
      !firstContent.empty() && (firstContent[0] != '-' || minusScalar) &&
      firstContent[0] != '{' && firstContent[0] != '[' && firstContent[0] != ']' &&
      firstContent[0] != '}';
    if (looksScalar) {
      noTrailingLines();
      if (firstContent == "null") return "null";
      if (firstContent == "true") return "true";
      if (firstContent == "false") return "false";
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
  stack.push_back({rootIsArr ? 'A' : 'O', true, rootIndent, true, true, 0, 0, {}, -1});
  // stack starts with the root frame at depth 0. After N pushes, size == N+1
  // and depth == N. JSON/sexpr/TRON allow containers up to depth MAX, so we
  // must allow size == MAX+1 and reject only at MAX+2 (depth == MAX+1).
  if (stack.size() > asvJSONValue::MAX_NESTING_DEPTH + 1) throw GoonDecodeError("GOON: nesting too deep");

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
      // The header's `[...]` must be found OUTSIDE any quoted key prefix
      // (alwaysQuoteKeys / keys containing `[`/`]` produce `"a[2]"[1]{b}:`).
      size_t openB = goonFindUnquoted(content, '[');
      if (openB != std::string_view::npos && openB + 1 < content.size()) {
        // Both closers must be quote-aware too. A plain find(']') found the
        // bracket inside a quoted LIST VALUE (`- ["x",false,"[]:","0x10"]`),
        // saw the ':' right after it and treated the list item as a tabular
        // array header, emitting only the text after that colon as the value.
        size_t relClose = goonFindUnquoted(content.substr(openB), ']');
        size_t closeB = (relClose == std::string_view::npos) ? std::string_view::npos : openB + relClose;
        if (closeB != std::string_view::npos) {
          size_t afterB = closeB + 1;
          bool isHeader = false;
          if (afterB < content.size() && content[afterB] == ':') isHeader = true;
          if (!isHeader && afterB < content.size() && content[afterB] == '{') {
            size_t relBr = goonFindUnquoted(content.substr(afterB), '}');
            size_t closeBr = (relBr == std::string_view::npos) ? std::string_view::npos : afterB + relBr;
            if (closeBr != std::string_view::npos && closeBr + 1 < content.size() && content[closeBr + 1] == ':')
              isHeader = true;
          }
          if (isHeader) {
            bool hasFields = afterB < content.size() && content[afterB] == '{';
            std::vector<std::string> fields;
            if (hasFields) {
              // Quote-aware: a quoted field name may contain '}' (e.g.
              // t[1]{"a}b"}:). A plain find('}') closed the field list inside
              // the quote, so the header was not recognized at all.
              size_t relBr = goonFindUnquoted(content.substr(afterB), '}');
              size_t closeBr = (relBr == std::string_view::npos) ? std::string_view::npos : afterB + relBr;
              if (closeBr != std::string_view::npos) {
                std::string_view fc = content.substr(afterB + 1, closeBr - afterB - 1);
                auto fieldParts = goonSplitCommas(fc);
                for (auto& f : fieldParts) {
                  size_t fs = 0;
                  while (fs < f.size() && f[fs] == ' ') fs++;
                  size_t fe = f.size();
                  while (fe > fs && f[fe - 1] == ' ') fe--;
                  std::string raw = f.substr(fs, fe - fs);
                  // Quoted field names are JSON-escaped by the encoder; unescape them
                  // here so the key round-trips correctly.  The GOON grammar says bare
                  // tabular column names must not contain quotes/commas/spaces, so only
                  // explicitly quoted names need unescaping (robustness for external input).
                  if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                    try {
                      fields.push_back(unescapeJsonString(std::string_view(raw.data() + 1, raw.size() - 2), true));
                    } catch (const std::runtime_error&) {
                      fields.push_back(raw);  // keep raw on decode error
                    }
                  } else {
                    fields.push_back(raw);
                  }
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
              // Quoted keys (from alwaysQuoteKeys or keys containing `[`/`]`)
              // are unescaped back to their literal value here.
              if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
                try {
                  std::string uk = unescapeJsonString(key.substr(1, key.size() - 2), true);
                  out += '"' + goonEscKey(uk) + "\":";
                } catch (const std::runtime_error&) {
                  throw asvJSONError("GOON: invalid escape in string");
                }
              } else {
                out += '"' + goonEscKey(key) + "\":";
              }
            }
            out += '[';
            stack.push_back({'A', true, indent, true, false, 0, 0, {}, -1});
            if (stack.size() > asvJSONValue::MAX_NESTING_DEPTH + 1) throw GoonDecodeError("GOON: nesting too deep");
          }
          size_t afterColon = afterB + 1;
          while (afterColon < content.size() && content[afterColon] == ' ') afterColon++;
          if (hasFields && !fields.empty()) {
            // Tabular: read subsequent rows. prevValues carries the previous
            // ROW's cell values so a `^` reference resolves across rows (the
            // encoder emits `^` between consecutive rows, not within one).
            std::vector<std::string> prevValues(fields.size(), "");
            std::vector<bool> hasPrev(fields.size(), false);  // tracks whether column has had any prior row
            size_t lastRl = li;
            for (size_t rl = li + 1; rl < lines.size(); rl++) {
              auto& rline = lines[rl];
              if (rline.empty()) continue;
              int rindent = countIndent(rline);
              if (rindent < indent) break;
              if (rindent == indent) {
                // minimalIndent tables put rows at the header's own
                // indentation; end the table at the first structured line
                // there. A mapping sibling has its first `:` before any
                // unquoted `,` (a row *cell* that contains `:`  -  e.g. a regex
                // `__REGEX__a:b`  -  only appears after its column separator),
                // and a `- ` item could never be a row cell emitted bare.
                std::string_view rc3 = stripIndent(rline);
                bool structured = false;
                if (rc3.size() >= 2 && rc3[0] == '-' && rc3[1] == ' ') {
                  structured = true;
                } else {
                  bool inQb = false;
                  bool escb = false;
                  bool seenComma = false;
                  int depthb = 0;
                  for (size_t ci2 = 0; ci2 < rc3.size(); ci2++) {
                    char cb2 = rc3[ci2];
                    if (escb) { escb = false; continue; }
                    if (inQb && cb2 == '\\') { escb = true; continue; }
                    if (cb2 == '"') { inQb = !inQb; continue; }
                    if (inQb) continue;
                    // Colons/brackets inside a balanced group (a marker cell)
                    // belong to that cell, not to the surrounding structure.
                    if (cb2 == '{' || cb2 == '[') depthb++;
                    else if (cb2 == '}' || cb2 == ']') { if (depthb > 0) depthb--; }
                    else if (depthb > 0) continue;
                    if (cb2 == ',') { seenComma = true; continue; }
                    if (cb2 == ':') {
                      if (seenComma) { seenComma = false; continue; }
                      structured = true;
                      break;
                    }
                  }
                }
                if (structured) break;
              }
              lastRl = rl;
              std::string_view rcont = stripIndent(rline);
              // Check for RLE: value*N (respecting quotes, \" escapes and
              // balanced groups -- a `*` inside a marker cell is not a repeat)
              size_t asterisk = std::string_view::npos;
              {
                bool inQAst = false;
                bool escAst = false;
                int depthAst = 0;
                for (size_t ci = 0; ci < rcont.size(); ci++) {
                  char c_char = rcont[ci];
                  if (escAst) { escAst = false; continue; }
                  if (c_char == '\\') { escAst = true; continue; }
                  if (c_char == '"') { inQAst = !inQAst; continue; }
                  if (inQAst) continue;
                  if (c_char == '{' || c_char == '[') depthAst++;
                  else if (c_char == '}' || c_char == ']') { if (depthAst > 0) depthAst--; }
                  else if (c_char == '*' && ci > 0 && depthAst == 0) { asterisk = ci; break; }
                }
              }
              int repeat = 1;
              std::string_view rowStr = rcont;
              if (asterisk != std::string_view::npos) {
                std::string countStr(rcont.substr(asterisk + 1));
                // The count must be a pure digit run (leading/trailing spaces
                // tolerated); anything else (e.g. trailing cells after `*N`)
                // would silently drop data.
                size_t ds = 0;
                while (ds < countStr.size() && (countStr[ds] == ' ' || countStr[ds] == '\t')) ds++;
                size_t de = ds;
                while (de < countStr.size() && countStr[de] >= '0' && countStr[de] <= '9') de++;
                size_t dEnd = de;
                while (dEnd < countStr.size() && (countStr[dEnd] == ' ' || countStr[dEnd] == '\t')) dEnd++;
                if (de == ds || dEnd != countStr.size())
                  throw GoonDecodeError("GOON: invalid RLE repeat at line " + std::to_string(li + 1));
                // Parse with overflow detection: atoi() on an arbitrary digit
                // run is UB on overflow (an unbounded count is not a
                // representable int). strtol + errno turns it into an error.
                errno = 0;
                char* endp = nullptr;
                long rcv = std::strtol(countStr.substr(ds, de - ds).c_str(), &endp, 10);
                (void)endp;
                const std::string errOver = "RLE repeat count exceeds limit of " + std::to_string(MAX_REPEAT_COUNT) + " at line " + std::to_string(li + 1);
                if (errno == ERANGE) throw GoonDecodeError(errOver);
                if (rcv < 1) repeat = 1;
                else if (rcv > static_cast<long>(MAX_REPEAT_COUNT)) throw GoonDecodeError(errOver);
                else repeat = static_cast<int>(rcv);
                rowStr = rcont.substr(0, asterisk);
              }

              // Hoist cell splitting AND expansion out of the repeat loop:
              // expandCell is deterministic and the same cell text is repeated,
              // so we split and expand once and reuse. Prevents O(repeat)
              // allocations per row.  Must keep RAW cells for caret detection
              // (rawVals[fi] == "^") while prevValues stores EXPANDED values so
              // that caret resolution yields already-expanded JSON text.
              std::vector<std::string> rawVals;
              {
                std::string cur2;
                bool inQ = false;
                bool escape = false;
                int depth = 0;
                for (size_t ci = 0; ci < rowStr.size(); ci++) {
                  char c2 = rowStr[ci];
                  if (!escape && c2 == '\\') { escape = true; cur2 += c2; continue; }
                  if (!escape && c2 == '"') { inQ = !inQ; cur2 += c2; continue; }
                  escape = false;
                  if (!inQ) {
                    if (c2 == '{' || c2 == '[') depth++;
                    else if (c2 == '}' || c2 == ']') { if (depth > 0) depth--; }
                  }
                  if (c2 == ',' && !inQ && depth == 0) { rawVals.push_back(cur2); cur2.clear(); continue; }
                  cur2 += c2;
                }
              rawVals.push_back(cur2);
              }
              // Expand cells once, reuse for each repeat.
              std::vector<std::string> vals = rawVals;
              for (size_t fi = 0; fi < vals.size(); fi++)
                vals[fi] = expandCell(vals[fi]);
              // Pad vals (not rawVals) so missing trailing cells emit "null"
              // without setting hasPrev — a bare ^ in a later row for that
              // column must still error ("no previous value"), matching the
              // pre-hoist behaviour where absent cells fell through to the
              // else { out += "null"; } branch and left hasPrev untouched.
              while (vals.size() < fields.size()) vals.push_back("null");
              for (int ri = 0; ri < repeat; ri++) {
                if (!stack.empty() && !stack.back().first) out += ',';
                if (!stack.empty()) stack.back().first = false;
                out += '{';
                for (size_t fi = 0; fi < fields.size(); fi++) {
                  if (fi > 0) out += ',';
                  out += '"' + goonEscKey(fields[fi]) + "\":";
                  if (fi < rawVals.size()) {
                    std::string cell = vals[fi];   // expanded for output
                    if (rawVals[fi] == "^") {
                      // Spec: '^' references the previous row's value; a quoted
                      // "^" is an ordinary literal string. Using '^' with no
                      // prior value (first row / first appearance of a column)
                      // is a decode error.  Must check hasPrev, not prevValues.empty(),
                      // because an empty-cell reference in row N should resolve to the
                      // previous non-empty value for that column (or error if none).
                      if (!hasPrev[fi])
                        throw GoonDecodeError("GOON: column reference '^' with no previous value at line " + std::to_string(li + 1));
                      cell = prevValues[fi];
                    } else {
                      // Store EXPANDED value in prevValues so caret in later rows
                      // resolves against the expanded JSON text.
                      prevValues[fi] = vals[fi];
                    }
                    hasPrev[fi] = true;
                    out += cell;   // already expanded above or resolved from prevValues
                  } else {
                    out += "null";
                  }
                }
                out += '}';
                checkOut("RLE row");
              }
            }
            if (lastRl > li) li = lastRl;
            if (!rootArr) {
              out += ']';
              checkOut("tabular close");
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
            size_t lastConsumedRl = li;
            for (size_t rl = li + 1; rl < lines.size(); rl++) {
              auto& rline2 = lines[rl];
              if (rline2.empty()) continue;
              int rindent2 = countIndent(rline2);
              if (rindent2 <= indent) break;
              std::string_view rcont2 = stripIndent(rline2);
              if (rcont2.size() >= 2 && rcont2[0] == '-' && rcont2[1] == ' ') {
                out += ',';
                out += expandCell(rcont2.substr(2));
                lastConsumedRl = rl;
              }
            }
            // Advance past the consumed list items so the outer loop does not
            // re-emit them as list headers.
            if (lastConsumedRl > li) li = lastConsumedRl;
            if (!rootArr) {
              out += ']';
              checkOut("inline-list close");
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
              checkOut("deferred-list close");
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

    // List item: - value
    if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
      // A `- x` line is only a valid list item inside an array frame; inside
      // an object it would append a bare value into the mapping (invalid JSON).
      if (curFrame.type != 'A')
        throw GoonDecodeError("GOON: list item outside of array frame at line " + std::to_string(li + 1));
      addComma(stack, out);
      curFrame.first = false;
      out += expandCell(content.substr(2));
      checkOut("- value");
      continue;
    }
    // Bare "-" means an inline object
    if (content == "-") {
      if (curFrame.type != 'A')
        throw GoonDecodeError("GOON: list item outside of array frame at line " + std::to_string(li + 1));
      addComma(stack, out);
      curFrame.first = false;
      out += '{';
      stack.push_back({'O', true, indent, true, false, 0, 0, {}, -1});
      if (stack.size() > asvJSONValue::MAX_NESTING_DEPTH + 1) throw GoonDecodeError("GOON: nesting too deep");
      continue;
    }

    // key: value
    size_t colonPos = goonFindUnquoted(content, ':');
    if (colonPos != std::string_view::npos) {
      std::string_view key = content.substr(0, colonPos);
      while (!key.empty() && key.back() == ' ') key = key.substr(0, key.size() - 1);
      std::string keyStr;
      if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
        // Quoted key content is JSON-escaped text; decode it first so that
        // `\"`/`\\`/`\uXXXX` sequences are not escaped a second time.
        std::string decoded;
        try { decoded = unescapeJsonString(key.substr(1, key.size() - 2), true); }
        catch (const std::runtime_error&) { throw asvJSONError("GOON: invalid escape in string"); }
        keyStr = goonEscKey(decoded);
      } else {
        keyStr = goonEscKey(key);
      }
      std::string_view valPart = content.substr(colonPos + 1);
      while (!valPart.empty() && valPart.front() == ' ') valPart = valPart.substr(1);

      addComma(stack, out);
      curFrame.first = false;

      if (valPart.empty()) {
        out += '"' + keyStr + "\":{";
        stack.push_back({'O', true, indent, true, false, 0, 0, {}, -1});
        if (stack.size() > asvJSONValue::MAX_NESTING_DEPTH + 1) throw GoonDecodeError("GOON: nesting too deep");
      } else if (valPart == "{}") {
        out += '"' + keyStr + "\":{}";
      } else if (valPart == "[]") {
        out += '"' + keyStr + "\":[]";
      } else {
        out += '"' + keyStr + "\":" + expandCell(valPart);
        checkOut("key: value");
      }
    } else {
      // toGOON() emits an empty root object/array as a bare `{}`/`[]` line
      // whose (empty) content is implied by the already-open root frame.
      // Any other separator-less line is not valid GOON in this context;
      // previously it fell off the loop and was silently dropped (data loss).
      if (!(curFrame.isRoot && li == firstLine && (content == "{}" || content == "[]")))
        throw GoonDecodeError("GOON: unrecognized line " + std::to_string(li + 1) +
                              (curFrame.type == 'A' ? " in array context" : " in object context"));
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
  std::string out;
  if (opts.mode != GoonWriteOptions::Mode::LLM) {
    auto dict = goonBuildDict(root.get());
    goonWriteDictHeader(dict, out);
    goonSerializeVal(root.get(), out, 0, "", 0, &dict, opts);
  } else {
    // Spec mode table: llm = no dictionary, no references.
    goonSerializeVal(root.get(), out, 0, "", 0, nullptr, opts);
  }
  return out;
}

inline bool asvJSON::fromGOON(std::string_view input) {
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  lastError.clear();
  if (input.empty()) { lastError = "empty input"; return false; }
  // UTF-8 BOM, like every other from* decoder: without this the BOM became the
  // first key's leading bytes ("\xEF\xBB\xBFkey").
  if (input.size() >= 3 && (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
    input.remove_prefix(3);
  if (input.empty()) { lastError = "empty input"; return false; }
  // Security: reject inputs larger than 10 MB to prevent memory exhaustion.
  constexpr size_t MAX_GOON_INPUT = 10u * 1024u * 1024u;
  if (input.size() > MAX_GOON_INPUT) { lastError = "GOON input exceeds 10 MB limit"; return false; }
  bool prevNaN = allowNaNInfinity;
  allowNaNInfinity = true;
  try {
    std::string json = goonToJson(input);
    bool ok = parse(std::string_view(json));
    allowNaNInfinity = prevNaN;
    if (ok) formatType = FormatType::GOON;
    else root = nullptr;
    return ok;
  } catch (const asvJSONError& e) {
    allowNaNInfinity = prevNaN;
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error etc. must not escape: the public contract is
    // bool + lastError, like the other from* parsers (see fromMessagePack).
    // Restore the permissive flag here too -- a leaked allowNaNInfinity
    // would silently change later parses (same lesson as fromJSON5).
    allowNaNInfinity = prevNaN;
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  }
}

} // namespace asvJSONInternal
