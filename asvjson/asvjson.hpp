#pragma once
// asvJSON++ v1.17.0 - Main entry point
// Includes core + all formats by default.
// Define ASVJSON_DISABLE_MSG_PACK, ASVJSON_DISABLE_CBOR, ASVJSON_DISABLE_BSON,
// ASVJSON_DISABLE_TOON, ASVJSON_DISABLE_TRON, ASVJSON_DISABLE_GOON,
// ASVJSON_DISABLE_XML, ASVJSON_DISABLE_YAML, ASVJSON_DISABLE_CSV,
// ASVJSON_DISABLE_PROTOBUF, ASVJSON_DISABLE_TOML, ASVJSON_DISABLE_SEXPR,
// ASVJSON_DISABLE_JSON5, ASVJSON_DISABLE_INI, ASVJSON_DISABLE_UDE,
// ASVJSON_DISABLE_RON,
// ASVJSON_DISABLE_CPP,
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

#ifndef ASVJSON_DISABLE_RON
#include "formats/ron.hpp"
#endif

#ifndef ASVJSON_DISABLE_CPP
#include "formats/cpp.hpp"
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
  // Control chars (<0x20, excluding \n\r\t)  ->  binary.
  // Bytes >=0x80 are validated as UTF-8; non-UTF-8 byte sequences  ->  binary.
  bool isBinary = false;
  {
    size_t n = std::min(input.size(), size_t(256));
    size_t i = 0;
    while (i < n) {
      auto c = static_cast<unsigned char>(input[i]);
      if (c < 0x80) {
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
          isBinary = true;
        }
        i++;
        continue;
      }
      int len = 0;
      if ((c & 0xE0) == 0xC0) len = 2;
      else if ((c & 0xF0) == 0xE0) len = 3;
      else if ((c & 0xF8) == 0xF0) len = 4;
      else { isBinary = true; break; }
      // Truncated sequence at the scan boundary  -  do not flag as binary.
      if (i + len > n) break;
      bool valid = true;
      for (int j = 1; j < len; j++) {
        if ((static_cast<unsigned char>(input[i+j]) & 0xC0) != 0x80) {
          valid = false;
          break;
        }
      }
      if (!valid) { isBinary = true; break; }
      if (len == 3) {
        uint32_t cp = (static_cast<uint32_t>(c) & 0x0F) << 12 |
                      (static_cast<uint32_t>(input[i+1]) & 0x3F) << 6 |
                      (static_cast<uint32_t>(input[i+2]) & 0x3F);
        if (cp >= 0xD800 && cp <= 0xDFFF) { isBinary = true; break; }
      }
      i += len;
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
    // RON groups are parenthesized too. Heuristic: if the text contains a ':'
    // or ',' inside the group it is more likely a RON tuple/struct than an
    // S-expression.
    if (s.find(':') != std::string_view::npos || s.find(',') != std::string_view::npos) {
      if (fromRON(s)) return true;
    }
    if (fromSexpr(s)) return true;
  }

  // RON: starts with '[' '{' or a bare identifier/enum variant. Fall back
  // after JSON/JSON5/JSONLines and other text formats.
  if (f == '{' || f == '[') {
    if (fromRON(s)) return true;
  }

  // RON named structs / tuple structs / enum variants with data: `Name(...)`.
  // Try RON early when the input starts with an identifier followed by '(';
  // non-matching input fails quickly and falls through to other formats.
  if ((f >= 'a' && f <= 'z') || (f >= 'A' && f <= 'Z') || f == '_') {
    if (s.find('(') != std::string_view::npos && fromRON(s)) return true;
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

  // RON (structs/maps/lists with Rust-like syntax)
  if (fromRON(s)) return true;

  lastError = "Unable to detect format";
  return false;
}

static inline std::string asvjsonExtLower(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

static inline std::string asvjsonExtractExt(const std::string& filename) {
  auto pos = filename.rfind('.');
  if (pos == std::string::npos || pos == 0) return "";
  auto ext = filename.substr(pos + 1);
  // also strip .gz / .bz2 / .xz suffix
  if (ext == "gz" || ext == "bz2" || ext == "xz") {
    auto pos2 = filename.rfind('.', pos - 1);
    if (pos2 != std::string::npos && pos2 > 0) ext = filename.substr(pos2 + 1, pos - pos2 - 1);
  }
  return asvjsonExtLower(ext);
}

inline bool asvJSON::readFromFileAny(const std::string& filename) {
  std::ifstream in(filename, std::ios::binary);
  if (!in.is_open()) { lastError = "Failed to open file: " + filename; return false; }
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();

  auto ext = asvjsonExtractExt(filename);

  // Binary formats - pass raw bytes
  if (ext == "bson") { formatType = FormatType::BSON; return fromBSON(content); }
  if (ext == "mp" || ext == "msgpack" || ext == "msg") { formatType = FormatType::MessagePack; return fromMessagePack(content); }
  if (ext == "cbor") { formatType = FormatType::CBOR; return fromCBOR(content); }
  if (ext == "pb" || ext == "protobuf" || ext == "proto") { formatType = FormatType::Protobuf; return fromProtobuf(content); }

  // Text formats - use string_view dispatch
  if (ext == "json")  { formatType = FormatType::JSON;     return parse(content); }
  if (ext == "json5" || ext == "jsonc") { formatType = FormatType::JSON5;     return fromJSON5(content); }
  if (ext == "jsonl" || ext == "ndjson") { formatType = FormatType::JSONLines; return fromJSONLines(content); }
  if (ext == "yaml" || ext == "yml") { formatType = FormatType::YAML;     return fromYAML(content); }
  if (ext == "xml")  { formatType = FormatType::XML;      return fromXML(content); }
  if (ext == "csv")  { formatType = FormatType::CSV;      return fromCSV(content); }
  if (ext == "tsv")  { formatType = FormatType::CSV;      return fromCSV(content); }
  if (ext == "toml") { formatType = FormatType::TOML;     return fromTOML(content); }
  if (ext == "ini" || ext == "cfg" || ext == "conf") { formatType = FormatType::INI; return fromINI(content); }
  if (ext == "sexp" || ext == "sexpr" || ext == "scm") { formatType = FormatType::SExpr; return fromSexpr(content); }
  if (ext == "toon") { formatType = FormatType::TOON;     return fromTOON(content); }
  if (ext == "tron") { formatType = FormatType::TRON;     return fromTRON(content); }
  if (ext == "goon") { formatType = FormatType::GOON;     return fromGOON(content); }
  if (ext == "ude")  { formatType = FormatType::UDE;      return fromUDE(content); }
  if (ext == "ron")  { formatType = FormatType::RON;      return fromRON(content); }
  if (ext == "hpp" || ext == "h" || ext == "hxx" || ext == "cpp" || ext == "cc") { formatType = FormatType::CPP; return fromCpp(content); }
  if (ext == "ptxt" || ext == "protobuf-text") { formatType = FormatType::ProtobufText; return fromProtobufText(content); }

  // Unknown extension - auto-detect from content
  return fromAny(content);
}

inline bool asvJSON::writeToFileAny(const std::string& filename, bool pretty) const {
  auto ext = asvjsonExtractExt(filename);

  // Binary formats - write raw bytes
  if (ext == "bson") {
    auto data = toBSON();
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
  }
  if (ext == "mp" || ext == "msgpack" || ext == "msg") {
    auto data = toMessagePack();
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
  }
  if (ext == "cbor") {
    auto data = toCBOR();
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
  }
  if (ext == "pb" || ext == "protobuf" || ext == "proto") {
    auto data = toProtobuf();
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
  }

  // Text formats
  std::string content;
  if (ext == "json")  content = serialize(pretty);
  else if (ext == "json5" || ext == "jsonc") content = toJSON5(pretty);
  else if (ext == "jsonl" || ext == "ndjson") content = toJSONLines();
  else if (ext == "yaml" || ext == "yml") content = toYAML(pretty);
  else if (ext == "xml")  content = toXML();
  else if (ext == "csv")  content = toCSV();
  else if (ext == "tsv")  content = toCSV();
  else if (ext == "toml") content = toTOML();
  else if (ext == "ini" || ext == "cfg" || ext == "conf") content = toINI();
  else if (ext == "sexp" || ext == "sexpr" || ext == "scm") content = toSexpr();
  else if (ext == "toon") content = toTOON();
  else if (ext == "tron") content = toTRON();
  else if (ext == "goon") content = toGOON();
  else if (ext == "ude")  content = toUDE();
  else if (ext == "ron") content = toRON(pretty);
  else if (ext == "hpp" || ext == "h") content = toCpp();
  else if (ext == "ptxt" || ext == "protobuf-text") content = toProtobufText();
  else content = serialize(pretty);

  std::ofstream out(filename, std::ios::binary);
  if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
  out << content;
  return out.good();
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
