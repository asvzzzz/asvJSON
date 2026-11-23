#pragma once
// asvJSON++ v1.26.11 - Main entry point
// Includes core + all formats by default.
// Define ASVJSON_DISABLE_MSG_PACK, ASVJSON_DISABLE_CBOR, ASVJSON_DISABLE_BSON,
// ASVJSON_DISABLE_TOON, ASVJSON_DISABLE_TRON, ASVJSON_DISABLE_GOON,
// ASVJSON_DISABLE_XML, ASVJSON_DISABLE_YAML, ASVJSON_DISABLE_CSV,
// ASVJSON_DISABLE_PROTOBUF, ASVJSON_DISABLE_TOML, ASVJSON_DISABLE_SEXPR,
// ASVJSON_DISABLE_JSON5, ASVJSON_DISABLE_INI, ASVJSON_DISABLE_ADE,
// ASVJSON_DISABLE_RON,
// ASVJSON_DISABLE_CPP, ASVJSON_DISABLE_DOTENV, ASVJSON_DISABLE_BENCODE,
// ASVJSON_DISABLE_UBJSON, ASVJSON_DISABLE_JAVAPROP, ASVJSON_DISABLE_PLIST,
// ASVJSON_DISABLE_SCHEMA, ASVJSON_DISABLE_JSONPATH, ASVJSON_DISABLE_JSONSAX,
// ASVJSON_DISABLE_JSONLINES
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

#if !defined(ASVJSON_DISABLE_SVG) && defined(ASVJSON_DISABLE_XML)
#error "SVG requires XML support; define neither ASVJSON_DISABLE_XML nor ASVJSON_DISABLE_SVG, or disable both"
#endif
#ifndef ASVJSON_DISABLE_SVG
#include "formats/svg.hpp"
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

#ifndef ASVJSON_DISABLE_ADE
#include "formats/ade.hpp"
#endif

#ifndef ASVJSON_DISABLE_RON
#include "formats/ron.hpp"
#endif

#ifndef ASVJSON_DISABLE_CPP
#include "formats/cpp.hpp"
#endif

#ifndef ASVJSON_DISABLE_DOTENV
#include "formats/dotenv.hpp"
#endif

#ifndef ASVJSON_DISABLE_BENCODE
#include "formats/bencode.hpp"
#endif

#ifndef ASVJSON_DISABLE_UBJSON
#include "formats/ubjson.hpp"
#endif

#ifndef ASVJSON_DISABLE_JAVAPROP
#include "formats/javaprop.hpp"
#endif

#ifndef ASVJSON_DISABLE_PLIST
#include "formats/plist.hpp"
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

#ifndef ASVJSON_DISABLE_JSONLINES
#include "formats/jsonlines.hpp"
#endif

namespace asvJSONInternal {

inline bool asvJSON::fromAny(std::string_view input) {
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  lastError.clear();
  try {
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
  #ifndef ASVJSON_DISABLE_BSON
            if (fromBSON(input.data(), input.size())) return true;
  #endif
          }
        }
      }
  #ifndef ASVJSON_DISABLE_BENCODE
      // Bencode dictionaries (".torrent" files) start with `d<digits>:`. Gated by
      // that signature so it is not attempted for arbitrary binary data.
      if (bencodeLooksLikeDictionary(input)) {
        if (fromBencode(input)) return true;
      }
  #endif
  #ifndef ASVJSON_DISABLE_MSG_PACK
      // The pointer overloads borrow the caller's buffer: the old std::string
      // temporaries copied the whole (up to multi-MB) input per probe.
      if (fromMessagePack(input.data(), input.size())) return true;
  #endif
  #ifndef ASVJSON_DISABLE_CBOR
      if (fromCBOR(input.data(), input.size())) return true;
  #endif
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
      // Apple plist: an XML document declaring a <plist> root. Checked before
      // the generic XML parser so plists are not claimed with the generic
      // @attr/#text mapping.
  #ifndef ASVJSON_DISABLE_PLIST
      if (plistLooksLike(s)) {
        if (fromPlist(s)) return true;
      }
  #endif
  #ifndef ASVJSON_DISABLE_XML
      if (fromXML(s)) return true;
  #endif
    }

    // JSON / JSON5: starts with '{' or '['
    if (f == '{' || f == '[') {
      if (parse(s)) return true;          // JSON
  #ifndef ASVJSON_DISABLE_JSON5
      if (fromJSON5(s)) return true;      // JSON5 superset
  #endif
  #ifndef ASVJSON_DISABLE_JSONLINES
      // Multiple JSON lines: starts with '{'/'[' and has newlines
      if (s.find('\n') != std::string_view::npos) {
        if (fromJSONLines(s)) return true;
      }
  #endif
    }

    // S-Expression: starts with '('
    if (f == '(') {
      // RON groups are parenthesized too. Heuristic: if the text contains a ':'
      // or ',' inside the group it is more likely a RON tuple/struct than an
      // S-expression.
  #ifndef ASVJSON_DISABLE_RON
      if (s.find(':') != std::string_view::npos || s.find(',') != std::string_view::npos) {
        if (fromRON(s)) return true;
      }
  #endif
  #ifndef ASVJSON_DISABLE_SEXPR
      if (fromSexpr(s)) return true;
  #endif
    }

    // RON: starts with '[' '{' or a bare identifier/enum variant. Fall back
    // after JSON/JSON5/JSONLines and other text formats.
  #ifndef ASVJSON_DISABLE_RON
    if (f == '{' || f == '[') {
      if (fromRON(s)) return true;
    }
  #endif

    // RON named structs / tuple structs / enum variants with data: `Name(...)`.
    // Try RON early when the input starts with an identifier followed by '(';
    // non-matching input fails quickly and falls through to other formats.
  #ifndef ASVJSON_DISABLE_RON
    if ((f >= 'a' && f <= 'z') || (f >= 'A' && f <= 'Z') || f == '_') {
      if (s.find('(') != std::string_view::npos && fromRON(s)) return true;
    }
  #endif

  #ifndef ASVJSON_DISABLE_BENCODE
    // Bencode dictionaries: `d<digits>:` signature (the whole key length is
    // scanned, so a 10+ byte first key works). Tried before the loose YAML-like
    // formats (GOON/TOON) which otherwise claim short, colon-heavy inputs like
    // "d1:ai1ee" as ordinary mappings, and before JavaProp, which would read
    // "d13:announce-list..." as the single property {"d13": "..."}.
    if (bencodeLooksLikeDictionary(s)) {
      if (fromBencode(s)) return true;
    }
  #endif

    // YAML document markers
  #ifndef ASVJSON_DISABLE_YAML
    if (s.size() >= 3 && s[0] == '-' && s[1] == '-' && s[2] == '-') {
      if (fromYAML(s)) return true;
    }
    if (s.size() >= 5 && s.compare(0, 5, "%YAML") == 0) {
      if (fromYAML(s)) return true;
    }
  #endif

  #ifndef ASVJSON_DISABLE_ADE
    // ADE: "# ADE" header
    if (s.size() >= 5 && s.compare(0, 5, "# ADE") == 0) {
      if (fromADE(s)) return true;
    }
  #endif

  #ifndef ASVJSON_DISABLE_TRON
    // TRON: first non-ws line starts with "class "
    {
      size_t p = 0;
      while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
      if (p + 6 <= s.size() && s.compare(p, 6, "class ") == 0) {
        if (fromTRON(s)) return true;
      }
    }
  #endif

    // TOML / INI: both have [section] headers and '=' signs.
    // Heuristic: quoted values ("...") or inline tables ({...}) -> TOML first.
    // Plain key=value -> INI first.
    if (s.find('[') != std::string_view::npos &&
        s.find('=') != std::string_view::npos) {
      bool hasQuotedValues = (s.find('"') != std::string_view::npos);
      bool hasInlineTables = (s.find('{') != std::string_view::npos);
      if (hasQuotedValues || hasInlineTables) {
  #ifndef ASVJSON_DISABLE_TOML
        if (fromTOML(s)) return true;
  #endif
  #ifndef ASVJSON_DISABLE_INI
        if (fromINI(s)) return true;
  #endif
      } else {
  #ifndef ASVJSON_DISABLE_INI
        if (fromINI(s)) return true;
  #endif
  #ifndef ASVJSON_DISABLE_TOML
        if (fromTOML(s)) return true;
  #endif
      }
    }

    // INI without '=': section headers + key:value or key<space>value.
    // Require an actual SECTION HEADER (a line whose first non-space char is
    // '['), not merely a '[' anywhere: TOON/GOON length headers like
    // "c[2]: 1,2" and "c[]:" used to satisfy the old s.find('[') test, so
    // fromAny() claimed the document as INI before reaching YAML/GOON/TOON.
  #ifndef ASVJSON_DISABLE_INI
    {
      bool hasSectionHeader = false;
      size_t p = 0;
      while (p < s.size()) {
        size_t e = s.find('\n', p);
        if (e == std::string_view::npos) e = s.size();
        size_t q = p;
        while (q < e && (s[q] == ' ' || s[q] == '\t' || s[q] == '\r')) q++;
        if (q < e && s[q] == '[') { hasSectionHeader = true; break; }
        if (e == s.size()) break;
        p = e + 1;
      }
      if (hasSectionHeader) {
        if (fromINI(s)) return true;
      }
    }
  #endif

    // Dotenv: flat KEY=VALUE lines (no [section] headers). Only attempted after
    // the section-style TOML/INI paths above, so a `[section]` document is never
    // mistaken for dotenv.
  #ifndef ASVJSON_DISABLE_DOTENV
    if (s.find('=') != std::string_view::npos && s.find('[') == std::string_view::npos) {
      if (fromDotenv(s)) return true;
    }
  #endif

    // Java .properties: '!'/'#' comments, `key=value` / `key:value` (unspaced
    // colon) pairs, `\uXXXX` escapes. Attempted after TOML/INI/dotenv (which
    // need '[section]' or '=') so those documents are never mis-claimed; ':' or
    // whitespace followed by a space is left for the later YAML/GOON/TOON path.
  #ifndef ASVJSON_DISABLE_JAVAPROP
    if (javapropLooksLike(s)) {
      if (fromJavaProp(s)) return true;
    }
  #endif

  #ifndef ASVJSON_DISABLE_CSV
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
  #endif

  #ifndef ASVJSON_DISABLE_YAML
    // YAML (most flexible text format)
    if (fromYAML(s)) return true;
  #endif

  #ifndef ASVJSON_DISABLE_GOON
    // GOON (YAML-like with special syntax)
    if (fromGOON(s)) return true;
  #endif

  #ifndef ASVJSON_DISABLE_TOON
    // TOON (indentation-based)
    if (fromTOON(s)) return true;
  #endif

  #ifndef ASVJSON_DISABLE_JSONLINES
    // JSONLines: multiple JSON values, one per line
    if (fromJSONLines(s)) return true;
  #endif

  #ifndef ASVJSON_DISABLE_RON
    // RON (structs/maps/lists with Rust-like syntax)
    if (fromRON(s)) return true;
  #endif

  #ifndef ASVJSON_DISABLE_BENCODE
    // Bencode (BitTorrent encoding). Last because its grammar is loose enough
    // to match short inputs; a full, consuming parse is required to succeed.
    if (fromBencode(s)) return true;
  #endif

    lastError = "Unable to detect format";
    return false;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error must not escape: the three std::string probe
    // copies (BSON/MessagePack/CBOR detection) and the lastError assignments
    // allocate, and fromAny's contract is false + lastError like every from*.
    lastError = e.what();
    root = nullptr; dirty_ = false;
    return false;
  }
}

static inline std::string asvjsonExtLower(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

static inline std::string asvjsonExtractExt(const std::string& filename) {
  auto pos = filename.rfind('.');
  if (pos == std::string::npos) return "";
  if (pos == 0 && filename.size() <= 1) return "";
  // A leading-dot basename such as ".env" has pos == 0; still expose the
  // suffix as the extension so dotfiles can be dispatched by name.
  auto ext = filename.substr(pos + 1);
  // also strip .gz / .bz2 / .xz suffix
  if (ext == "gz" || ext == "bz2" || ext == "xz") {
    auto pos2 = filename.rfind('.', pos - 1);
    if (pos2 != std::string::npos && pos2 > 0) ext = filename.substr(pos2 + 1, pos - pos2 - 1);
  }
  return asvjsonExtLower(ext);
}

inline bool asvJSON::readFromFileAny(const std::string& filename) {
  lastError.clear();
  if (filename.find('\0') != std::string::npos) {   // embedded NUL truncates the
    lastError = "Invalid file name: embedded NUL";  // CRT path but not the
    return false;                                   // extension dispatch below
  }
  // The bool + lastError contract: std::string building here (content,
  // ext, the lastError concat) must never escape as an exception.
  try {
    // Size cap BEFORE reading: every format's MAX_*_INPUT is <= 32 MB, so a
    // larger file can only be rejected - reading it first is the DoS.
    constexpr size_t MAX_FILE_INPUT = 64u * 1024u * 1024u;
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) { lastError = "Failed to open file: " + filename; return false; }
    in.seekg(0, std::ios::end);
    const std::streamoff sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz < 0 || static_cast<size_t>(sz) > MAX_FILE_INPUT) {
      lastError = "File too large: " + filename;
      in.close();
      return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    auto ext = asvjsonExtractExt(filename);

    // Every format branch below is compiled OUT together with the format itself.
    // The decoder BODY lives in the format header, so a build that defines
    // ASVJSON_DISABLE_<X> still saw the call site here and failed to LINK
    // ("undefined reference to asvJSON::fromYAML") even though the program never
    // touched YAML - readFromFileAny/writeToFileAny/fromAny are core API. A
    // disabled format's extension now simply falls through to the content
    // auto-detection below.

    // Binary formats - pass raw bytes
#ifndef ASVJSON_DISABLE_BSON
    if (ext == "bson") { formatType = FormatType::BSON; return fromBSON(content); }
#endif
#ifndef ASVJSON_DISABLE_MSG_PACK
    if (ext == "mp" || ext == "msgpack" || ext == "msg") { formatType = FormatType::MessagePack; return fromMessagePack(content); }
#endif
#ifndef ASVJSON_DISABLE_CBOR
    if (ext == "cbor") { formatType = FormatType::CBOR; return fromCBOR(content); }
#endif
#ifndef ASVJSON_DISABLE_UBJSON
    if (ext == "ubj" || ext == "ubjson") { formatType = FormatType::UBJSON; return fromUBJSON(content); }
#endif
#ifndef ASVJSON_DISABLE_PROTOBUF
    if (ext == "pb" || ext == "protobuf" || ext == "proto") { formatType = FormatType::Protobuf; return fromProtobuf(content); }
#endif

    // Text formats - use string_view dispatch
    if (ext == "json")  { formatType = FormatType::JSON;     return parse(content); }
#ifndef ASVJSON_DISABLE_JSON5
    if (ext == "json5" || ext == "jsonc") { formatType = FormatType::JSON5;     return fromJSON5(content); }
#endif
#ifndef ASVJSON_DISABLE_JSONLINES
    if (ext == "jsonl" || ext == "ndjson") { formatType = FormatType::JSONLines; return fromJSONLines(content); }
#endif
#ifndef ASVJSON_DISABLE_YAML
    if (ext == "yaml" || ext == "yml") { formatType = FormatType::YAML;     return fromYAML(content); }
#endif
#ifndef ASVJSON_DISABLE_XML
    if (ext == "xml")  { formatType = FormatType::XML;      return fromXML(content); }
#endif
#ifndef ASVJSON_DISABLE_SVG
    // .svg was missing from both file dispatchers: a .svg file used to be
    // written as JSON and read back through fromAny as generic XML/JSON, so
    // FormatType::SVG was unreachable through the file API.
    if (ext == "svg")  { formatType = FormatType::SVG;      return fromSVG(content); }
#endif
#ifndef ASVJSON_DISABLE_CSV
    if (ext == "csv")  { formatType = FormatType::CSV;      return fromCSV(content); }
    if (ext == "tsv")  { formatType = FormatType::CSV;      return fromCSV(content); }
#endif
#ifndef ASVJSON_DISABLE_TOML
    if (ext == "toml") { formatType = FormatType::TOML;     return fromTOML(content); }
#endif
#ifndef ASVJSON_DISABLE_INI
    if (ext == "ini" || ext == "cfg" || ext == "conf") { formatType = FormatType::INI; return fromINI(content); }
#endif
#ifndef ASVJSON_DISABLE_SEXPR
    if (ext == "sexp" || ext == "sexpr" || ext == "scm") { formatType = FormatType::SExpr; return fromSexpr(content); }
#endif
#ifndef ASVJSON_DISABLE_TOON
    if (ext == "toon") { formatType = FormatType::TOON;     return fromTOON(content); }
#endif
#ifndef ASVJSON_DISABLE_TRON
    if (ext == "tron") { formatType = FormatType::TRON;     return fromTRON(content); }
#endif
#ifndef ASVJSON_DISABLE_GOON
    if (ext == "goon") { formatType = FormatType::GOON;     return fromGOON(content); }
#endif
#ifndef ASVJSON_DISABLE_ADE
    if (ext == "ade")  { formatType = FormatType::ADE;      return fromADE(content); }
#endif
#ifndef ASVJSON_DISABLE_RON
    if (ext == "ron")  { formatType = FormatType::RON;      return fromRON(content); }
#endif
#ifndef ASVJSON_DISABLE_CPP
    if (ext == "hpp" || ext == "h" || ext == "hxx" || ext == "cpp" || ext == "cc") { formatType = FormatType::CPP; return fromCpp(content); }
#endif
#ifndef ASVJSON_DISABLE_PROTOBUF
    if (ext == "ptxt" || ext == "protobuf-text") { formatType = FormatType::ProtobufText; return fromProtobufText(content); }
#endif
#ifndef ASVJSON_DISABLE_DOTENV
    if (ext == "env" || ext == "dotenv") { formatType = FormatType::Dotenv; return fromDotenv(content); }
#endif
#ifndef ASVJSON_DISABLE_BENCODE
    if (ext == "torrent" || ext == "bencode" || ext == "benc") { formatType = FormatType::Bencode; return fromBencode(content); }
#endif
#ifndef ASVJSON_DISABLE_JAVAPROP
    if (ext == "properties" || ext == "props") { formatType = FormatType::JavaProp; return fromJavaProp(content); }
#endif
#ifndef ASVJSON_DISABLE_PLIST
    if (ext == "plist") { formatType = FormatType::Plist; return fromPlist(content); }
#endif

    // Unknown extension (or a format compiled out above) - auto-detect from content
    return fromAny(content);
  } catch (const asvJSONError& e) {
    lastError = e.what();
    return false;
  } catch (const std::exception& e) {
    // bad_alloc / length_error from std::string growth must never escape
    // the bool + lastError contract (see the from* parsers).
    lastError = e.what();
    return false;
  }
}

inline bool asvJSON::writeToFileAny(const std::string& filename, bool pretty) const {
  lastError.clear();
  if (filename.find('\0') != std::string::npos) {   // see readFromFileAny: the
    lastError = "Invalid file name: embedded NUL";  // extension comes from the
    return false;                                   // bytes past the NUL
  }
  // The bool + lastError contract: encoders (toBencode nesting, toXML NaN
  // attributes, ...) and the std::string building below must never escape
  // as an exception past this API.
  try {
    auto ext = asvjsonExtractExt(filename);

    // Binary formats - write raw bytes. Guarded like readFromFileAny: the
    // encoder body is part of the disabled header, so an unguarded call site
    // broke the link in every ASVJSON_DISABLE_<X> build.
#ifndef ASVJSON_DISABLE_BSON
    if (ext == "bson") {
      auto data = toBSON();
      std::ofstream out(filename, std::ios::binary);
      if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
      out.write(reinterpret_cast<const char*>(data.data()), data.size());
      return out.good();
    }
#endif
#ifndef ASVJSON_DISABLE_MSG_PACK
    if (ext == "mp" || ext == "msgpack" || ext == "msg") {
      auto data = toMessagePack();
      std::ofstream out(filename, std::ios::binary);
      if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
      out.write(reinterpret_cast<const char*>(data.data()), data.size());
      return out.good();
    }
#endif
#ifndef ASVJSON_DISABLE_CBOR
    if (ext == "cbor") {
      auto data = toCBOR();
      std::ofstream out(filename, std::ios::binary);
      if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
      out.write(reinterpret_cast<const char*>(data.data()), data.size());
      return out.good();
    }
#endif
#ifndef ASVJSON_DISABLE_UBJSON
    if (ext == "ubj" || ext == "ubjson") {
      auto data = toUBJSON();
      std::ofstream out(filename, std::ios::binary);
      if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
      out.write(reinterpret_cast<const char*>(data.data()), data.size());
      return out.good();
    }
#endif
#ifndef ASVJSON_DISABLE_PROTOBUF
    if (ext == "pb" || ext == "protobuf" || ext == "proto") {
      auto data = toProtobuf();
      std::ofstream out(filename, std::ios::binary);
      if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
      out.write(reinterpret_cast<const char*>(data.data()), data.size());
      return out.good();
    }
#endif

    // Text formats
    std::string content;
    if (ext == "json")  content = serialize(pretty);
#ifndef ASVJSON_DISABLE_JSON5
    else if (ext == "json5" || ext == "jsonc") content = toJSON5(pretty);
#endif
#ifndef ASVJSON_DISABLE_JSONLINES
    else if (ext == "jsonl" || ext == "ndjson") content = toJSONLines();
#endif
#ifndef ASVJSON_DISABLE_YAML
    else if (ext == "yaml" || ext == "yml") content = toYAML(pretty);
#endif
#ifndef ASVJSON_DISABLE_XML
    else if (ext == "xml")  content = toXML();
#endif
#ifndef ASVJSON_DISABLE_SVG
    else if (ext == "svg")  content = toSVG();
#endif
#ifndef ASVJSON_DISABLE_CSV
    else if (ext == "csv")  content = toCSV();
    else if (ext == "tsv")  content = toCSV();
#endif
#ifndef ASVJSON_DISABLE_TOML
    else if (ext == "toml") content = toTOML();
#endif
#ifndef ASVJSON_DISABLE_INI
    else if (ext == "ini" || ext == "cfg" || ext == "conf") content = toINI();
#endif
#ifndef ASVJSON_DISABLE_SEXPR
    else if (ext == "sexp" || ext == "sexpr" || ext == "scm") content = toSexpr();
#endif
#ifndef ASVJSON_DISABLE_TOON
    else if (ext == "toon") content = toTOON();
#endif
#ifndef ASVJSON_DISABLE_TRON
    else if (ext == "tron") content = toTRON();
#endif
#ifndef ASVJSON_DISABLE_GOON
    else if (ext == "goon") content = toGOON();
#endif
#ifndef ASVJSON_DISABLE_ADE
    else if (ext == "ade")  content = toADE();
#endif
#ifndef ASVJSON_DISABLE_RON
    else if (ext == "ron") content = toRON(pretty);
#endif
#ifndef ASVJSON_DISABLE_CPP
    // The read side accepts hxx/cpp/cc as C++ too; the write side only knew
    // hpp/h, so writeToFileAny("x.cpp") wrote JSON while readFromFileAny("x.cpp")
    // parsed the file as C++ - the two dispatchers must agree.
    else if (ext == "hpp" || ext == "h" || ext == "hxx" || ext == "cpp" || ext == "cc") content = toCpp();
#endif
#ifndef ASVJSON_DISABLE_PROTOBUF
    else if (ext == "ptxt" || ext == "protobuf-text") content = toProtobufText();
#endif
#ifndef ASVJSON_DISABLE_DOTENV
    else if (ext == "env" || ext == "dotenv") content = toDotenv();
#endif
#ifndef ASVJSON_DISABLE_BENCODE
    else if (ext == "torrent" || ext == "bencode" || ext == "benc") content = toBencode();
#endif
#ifndef ASVJSON_DISABLE_JAVAPROP
    else if (ext == "properties" || ext == "props") content = toJavaProp();
#endif
#ifndef ASVJSON_DISABLE_PLIST
    else if (ext == "plist") content = toPlist();
#endif
    else content = serialize(pretty);

    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) { const_cast<asvJSON*>(this)->lastError = "Failed to open file: " + filename; return false; }
    out << content;
    return out.good();
  } catch (const asvJSONError& e) {
    const_cast<asvJSON*>(this)->lastError = e.what();
    return false;
  } catch (const std::exception& e) {
    const_cast<asvJSON*>(this)->lastError = e.what();
    return false;
  }
}

#ifndef ASVJSON_DISABLE_JSONSAX
inline bool asvJSON::parseSax(std::string_view input, JsonSaxHandler& handler) {
  lastError.clear();
  try {
    JsonSaxParser parser;
    parser.allowNaNInfinity = allowNaNInfinity;
    parser.validateUTF8 = validateUTF8;
    bool ok = parser.parse(input, handler);
    if (!ok) lastError = std::string(parser.getLastError());
    return ok;
  } catch (const asvJSONError& e) {
    // A handler may throw asvJSONError (or anything else); a throw from an
    // unwrapped early-return path of parse() -- empty input, UTF-8 -- used
    // to escape this API. bool + lastError is the contract.
    lastError = e.what();
    return false;
  } catch (const std::exception& e) {
    lastError = e.what();
    return false;
  }
}
#endif

} // namespace asvJSONInternal
