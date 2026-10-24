// Minimal-build smoke test for the ASVJSON_DISABLE_<FORMAT> switches.
//
// The library documents ASVJSON_DISABLE_MSG_PACK ... ASVJSON_DISABLE_JSONLINES
// (asvjson/asvjson.hpp header comment), but the core dispatchers
// (fromAny / readFromFileAny / writeToFileAny) called every fromX()/toX()
// unconditionally. The decoder and encoder BODIES live in the format headers,
// so defining ANY one of those flags made even a program that never touched the
// disabled format fail to LINK ("undefined reference to
// asvJSON::fromYAML(...)"). This target defines EVERY flag at once and uses
// only the core dispatch API: it must compile, link and run, and each disabled
// format's extension must fall back to JSON instead of being dispatched.
//
// The flags are deliberately defined in the source file, not on the command
// line, so the check travels with the repository and covers the header build
// itself.
//
// SVG is paired with XML here on purpose: the header rejects XML-disabled +
// SVG-enabled builds with #error (fromSVG decodes through the XML parser), so
// the "all formats off" configuration must disable both.

#define ASVJSON_DISABLE_ADE
#define ASVJSON_DISABLE_BENCODE
#define ASVJSON_DISABLE_BSON
#define ASVJSON_DISABLE_CBOR
#define ASVJSON_DISABLE_CPP
#define ASVJSON_DISABLE_CSV
#define ASVJSON_DISABLE_DOTENV
#define ASVJSON_DISABLE_GOON
#define ASVJSON_DISABLE_INI
#define ASVJSON_DISABLE_JAVAPROP
#define ASVJSON_DISABLE_JSON5
#define ASVJSON_DISABLE_JSONLINES
#define ASVJSON_DISABLE_JSONPATH
#define ASVJSON_DISABLE_JSONSAX
#define ASVJSON_DISABLE_MSG_PACK
#define ASVJSON_DISABLE_PLIST
#define ASVJSON_DISABLE_PROTOBUF
#define ASVJSON_DISABLE_RON
#define ASVJSON_DISABLE_SCHEMA
#define ASVJSON_DISABLE_SEXPR
#define ASVJSON_DISABLE_SVG   // mandatory when XML is disabled (#error otherwise)
#define ASVJSON_DISABLE_TOML
#define ASVJSON_DISABLE_TOON
#define ASVJSON_DISABLE_TRON
#define ASVJSON_DISABLE_UBJSON
#define ASVJSON_DISABLE_XML
#define ASVJSON_DISABLE_YAML

#include "asvjson/asvjson.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static int failures = 0;

static void check(bool cond, const char* what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

int main() {
  // Core JSON is always available.
  {
    asvJSON j;
    check(j.parse(R"({"a":1,"b":[true,null]})"), "parse JSON");
    check(j.serialize() == R"({"a":1,"b":[true,null]})", "serialize JSON");
  }

  // One extension per disable-able format. Writing to it must fall back to
  // serialize() (the extension branch is compiled out), and reading the same
  // file back must NOT report the disabled format: the content
  // auto-detection claims the JSON text.
  struct ExtCase {
    const char* ext;
    asvJSONInternal::FormatType type;
  };
  static const ExtCase kCases[] = {
    {"bson", asvJSONInternal::FormatType::BSON},
    {"msgpack", asvJSONInternal::FormatType::MessagePack},
    {"cbor", asvJSONInternal::FormatType::CBOR},
    {"ubjson", asvJSONInternal::FormatType::UBJSON},
    {"protobuf", asvJSONInternal::FormatType::Protobuf},
    {"ptxt", asvJSONInternal::FormatType::ProtobufText},
    {"json5", asvJSONInternal::FormatType::JSON5},
    {"jsonl", asvJSONInternal::FormatType::JSONLines},
    {"yaml", asvJSONInternal::FormatType::YAML},
    {"xml", asvJSONInternal::FormatType::XML},
    {"svg", asvJSONInternal::FormatType::SVG},
    {"csv", asvJSONInternal::FormatType::CSV},
    {"toml", asvJSONInternal::FormatType::TOML},
    {"ini", asvJSONInternal::FormatType::INI},
    {"sexpr", asvJSONInternal::FormatType::SExpr},
    {"toon", asvJSONInternal::FormatType::TOON},
    {"tron", asvJSONInternal::FormatType::TRON},
    {"goon", asvJSONInternal::FormatType::GOON},
    {"ade", asvJSONInternal::FormatType::ADE},
    {"ron", asvJSONInternal::FormatType::RON},
    {"cpp", asvJSONInternal::FormatType::CPP},
    {"env", asvJSONInternal::FormatType::Dotenv},
    {"bencode", asvJSONInternal::FormatType::Bencode},
    {"properties", asvJSONInternal::FormatType::JavaProp},
    {"plist", asvJSONInternal::FormatType::Plist},
  };

  const std::string jsonTree = R"({"x":7})";
  for (const ExtCase& c : kCases) {
    const std::string path = std::string("test_minimal_disabled.") + c.ext;

    asvJSON w;
    check(w.parse(jsonTree), "parse for write");
    check(w.writeToFileAny(path), "writeToFileAny with the format disabled");
    {
      std::ifstream in(path, std::ios::binary);
      std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      check(content == jsonTree, "disabled-format write falls back to JSON");
    }

    asvJSON r;
    check(r.readFromFileAny(path), "readFromFileAny with the format disabled");
    check(r.getFormatType() != c.type, "disabled extension must not be dispatched");
    check(r.getInt("x") == 7, "JSON content survives the fallback dispatch");
    std::remove(path.c_str());
  }

  // fromAny() keeps working with every format branch compiled out.
  {
    asvJSON j;
    check(j.fromAny(R"([1,2,3])"), "fromAny JSON");
    check(j.fromAny(std::string("{\"a\":1}")), "fromAny JSON from std::string");
    // A YAML-esque document must NOT be claimed by the missing YAML decoder.
    check(!j.fromAny(std::string("---\na: 1\n")) || j.getFormatType() != asvJSONInternal::FormatType::YAML,
          "disabled YAML is not auto-detected");
  }

  if (failures == 0) std::printf("minimal build smoke: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
