#pragma once
// asvJSON++ v1.14.0 - Main entry point
// Includes core + all formats by default.
// Define ASVJSON_DISABLE_MSG_PACK, ASVJSON_DISABLE_CBOR, ASVJSON_DISABLE_BSON,
// ASVJSON_DISABLE_TOON, ASVJSON_DISABLE_TRON, ASVJSON_DISABLE_GOON,
// ASVJSON_DISABLE_XML, ASVJSON_DISABLE_YAML, ASVJSON_DISABLE_CSV,
// ASVJSON_DISABLE_PROTOBUF, ASVJSON_DISABLE_TOML, ASVJSON_DISABLE_SEXPR,
// ASVJSON_DISABLE_JSON5, ASVJSON_DISABLE_INI, ASVJSON_DISABLE_UDE,
// ASVJSON_DISABLE_SCHEMA, ASVJSON_DISABLE_JSONPATH, ASVJSON_DISABLE_JSONSAX
// before including this header to exclude specific formats.

#include "core.hpp"

#ifndef ASVJSON_DISABLE_MSG_PACK
#include "formats/msgpack.hpp"
#endif

#ifndef ASVJSON_DISABLE_CBOR
#include "formats/cbor.hpp"
#endif

#ifndef ASVJSON_DISABLE_BSON
#include "formats/bson.hpp"
#endif

#ifndef ASVJSON_DISABLE_TOON
#include "formats/toon.hpp"
#endif

#ifndef ASVJSON_DISABLE_TRON
#include "formats/tron.hpp"
#endif

#ifndef ASVJSON_DISABLE_GOON
#include "formats/goon.hpp"
#endif

#ifndef ASVJSON_DISABLE_XML
#include "formats/xml.hpp"
#endif

#ifndef ASVJSON_DISABLE_YAML
#include "formats/yaml.hpp"
#endif

#ifndef ASVJSON_DISABLE_CSV
#include "formats/csv.hpp"
#endif

#ifndef ASVJSON_DISABLE_PROTOBUF
#include "formats/protobuf.hpp"
#endif

#ifndef ASVJSON_DISABLE_TOML
#include "formats/toml.hpp"
#endif

#ifndef ASVJSON_DISABLE_SEXPR
#include "formats/sexpr.hpp"
#endif

#ifndef ASVJSON_DISABLE_JSON5
#include "formats/json5.hpp"
#endif

#ifndef ASVJSON_DISABLE_INI
#include "formats/ini.hpp"
#endif

#ifndef ASVJSON_DISABLE_UDE
#include "formats/ude.hpp"
#endif

#ifndef ASVJSON_DISABLE_SCHEMA
#include "formats/schema.hpp"
#endif

#ifndef ASVJSON_DISABLE_JSONPATH
#include "formats/jsonpath.hpp"
#endif

#ifndef ASVJSON_DISABLE_JSONSAX
#include "formats/jsonsax.hpp"
#endif

namespace asvJSONInternal {

inline bool asvJSON::fromAny(std::string_view input) {
  formatType = FormatType::Unknown;
  if (input.empty()) { lastError = "empty input"; return false; }

  // Strip UTF-8 BOM
  if (input.size() >= 3 &&
      static_cast<unsigned char>(input[0]) == 0xEF &&
      static_cast<unsigned char>(input[1]) == 0xBB &&
      static_cast<unsigned char>(input[2]) == 0xBF)
    input.remove_prefix(3);

  // --- Binary detection ---
  // If any of the first 256 bytes is >0x7F or a control char (not \n\r\t),
  // treat as binary.
  bool isBinary = false;
  {
    size_t n = std::min(input.size(), size_t(256));
    for (size_t i = 0; i < n; i++) {
      auto c = static_cast<unsigned char>(input[i]);
      if (c > 0x7F || (c < 0x20 && c != '\n' && c != '\r' && c != '\t')) {
        isBinary = true;
        break;
      }
    }
  }

  if (isBinary) {
    // BSON: 4-byte LE size must equal input.size(), byte[4] valid type
    if (input.size() >= 5) {
      uint32_t sz = static_cast<uint32_t>(static_cast<unsigned char>(input[0]))
        | (static_cast<uint32_t>(static_cast<unsigned char>(input[1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(input[2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(input[3])) << 24);
      if (sz == static_cast<uint32_t>(input.size())) {
        auto t = static_cast<unsigned char>(input[4]);
        if ((t >= 0x01 && t <= 0x08) || t == 0x0A ||
            (t >= 0x10 && t <= 0x13) || t == 0xFF) {
          if (fromBSON(std::string(input))) return true;
        }
      }
    }
    if (fromMessagePack(std::string(input))) return true;
    if (fromCBOR(std::string(input))) return true;
    lastError = "Unable to detect binary format";
    return false;
  }

  // --- Text format detection ---
  // Skip leading whitespace
  size_t start = 0;
  while (start < input.size() && (input[start] == ' ' || input[start] == '\t' ||
         input[start] == '\n' || input[start] == '\r'))
    start++;
  std::string_view s = input.substr(start);
  if (s.empty()) { lastError = "empty input"; return false; }

  char f = s[0];

  // XML: always starts with '<'
  if (f == '<') {
    if (fromXML(s)) return true;
  }

  // JSON / JSON5: starts with '{' or '['
  if (f == '{' || f == '[') {
    if (parse(s)) return true;          // JSON
    if (fromJSON5(s)) return true;      // JSON5 superset
    // Multiple JSON lines: starts with '{'/'[' and has newlines
    if (s.find('\n') != std::string_view::npos) {
      if (fromJSONLines(s)) return true;
    }
  }

  // S-Expression: starts with '('
  if (f == '(') {
    if (fromSexpr(s)) return true;
  }

  // YAML document markers
  if (s.size() >= 3 && s[0] == '-' && s[1] == '-' && s[2] == '-') {
    if (fromYAML(s)) return true;
  }
  if (s.size() >= 5 && s.compare(0, 5, "%YAML") == 0) {
    if (fromYAML(s)) return true;
  }

  // UDE: "# UDE" header
  if (s.size() >= 5 && s.compare(0, 5, "# UDE") == 0) {
    if (fromUDE(s)) return true;
  }

  // TRON: first non-ws line starts with "class "
  {
    size_t p = 0;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
    if (p + 6 <= s.size() && s.compare(p, 6, "class ") == 0) {
      if (fromTRON(s)) return true;
    }
  }

  // TOML / INI: both have [section] headers and '=' signs.
  // Heuristic: quoted values ("...") or inline tables ({...}) -> TOML first.
  // Plain key=value -> INI first.
  if (s.find('[') != std::string_view::npos &&
      s.find('=') != std::string_view::npos) {
    bool hasQuotedValues = (s.find('"') != std::string_view::npos);
    bool hasInlineTables = (s.find('{') != std::string_view::npos);
    if (hasQuotedValues || hasInlineTables) {
      if (fromTOML(s)) return true;
      if (fromINI(s)) return true;
    } else {
      if (fromINI(s)) return true;
      if (fromTOML(s)) return true;
    }
  }

  // INI without '=': section headers + key:value or key<space>value
  if (s.find('[') != std::string_view::npos) {
    if (fromINI(s)) return true;
  }

  // CSV: multiple lines with high delimiter density
  {
    size_t nl = 0, dl = 0;
    size_t n = std::min(s.size(), size_t(2048));
    for (size_t i = 0; i < n; i++) {
      if (s[i] == '\n') nl++;
      else if (s[i] == ',' || s[i] == '\t' || s[i] == ';') dl++;
    }
    if (nl > 0 && dl > nl) {
      if (fromCSV(s)) return true;
    }
  }

  // YAML (most flexible text format)
  if (fromYAML(s)) return true;

  // GOON (YAML-like with special syntax)
  if (fromGOON(s)) return true;

  // TOON (indentation-based)
  if (fromTOON(s)) return true;

  // JSONLines: multiple JSON values, one per line
  if (fromJSONLines(s)) return true;

  lastError = "Unable to detect format";
  return false;
}

#ifndef ASVJSON_DISABLE_JSONSAX
inline bool asvJSON::parseSax(std::string_view input, JsonSaxHandler& handler) {
  JsonSaxParser parser;
  parser.allowNaNInfinity = allowNaNInfinity;
  parser.validateUTF8 = validateUTF8;
  bool ok = parser.parse(input, handler);
  if (!ok) lastError = std::string(parser.getLastError());
  return ok;
}
#endif

} // namespace asvJSONInternal
