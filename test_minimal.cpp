// Minimal-build smoke test for the ASVJSON_DISABLE_<FORMAT> switches.
//
// The library documents ASVJSON_DISABLE_MSG_PACK ... ASVJSON_DISABLE_JSONLINES
// (asvjson/asvjson.hpp header comment), but the core dispatchers
// (fromAny / readFromFileAny / writeToFileAny) called every fromX()/toX()
// unconditionally. The decoder and encoder BODIES live in the format headers,
// so defining ANY one of those flags made even a program that never touched the
// disabled format fail to LINK ("undefined reference to
// asvJSON::fromYAML(...)"). This target defines a representative set of flags
// and uses only the core dispatch API: it must compile, link and run.
//
// The flags are deliberately defined in the source file, not on the command
// line, so the check travels with the repository and covers the header build
// itself.

#define ASVJSON_DISABLE_XML
#define ASVJSON_DISABLE_SVG   // mandatory when XML is disabled (#error otherwise)
#define ASVJSON_DISABLE_YAML
#define ASVJSON_DISABLE_TOML
#define ASVJSON_DISABLE_CSV
#define ASVJSON_DISABLE_INI
#define ASVJSON_DISABLE_PROTOBUF
#define ASVJSON_DISABLE_BENCODE
#define ASVJSON_DISABLE_PLIST
#define ASVJSON_DISABLE_JSONPATH
#define ASVJSON_DISABLE_SCHEMA
#define ASVJSON_DISABLE_JSONSAX

#include "asvjson/asvjson.hpp"

#include <cstdio>
#include <fstream>
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

  // fromAny() with the disabled formats compiled out: JSON is still detected,
  // and a .xml file is not routed through the XML decoder (its extension branch
  // is gone, so the content auto-detection takes over).
  {
    asvJSON j;
    check(j.fromAny(R"([1,2,3])"), "fromAny JSON");

    {
      std::ofstream out("test_minimal_disabled.xml");
      out << "<root><a>1</a></root>";
    }
    asvJSON r;
    r.readFromFileAny("test_minimal_disabled.xml");
    check(r.getFormatType() != asvJSONInternal::FormatType::XML,
          "XML extension must not be dispatched when ASVJSON_DISABLE_XML is set");
    std::remove("test_minimal_disabled.xml");
  }

  // File dispatchers: .json works, a disabled format's extension falls through
  // to the content auto-detection (here: JSON text in a .yaml file).
  {
    const char* path = "test_minimal_roundtrip.json";
    asvJSON w;
    check(w.parse(R"({"x":7})"), "parse for write");
    check(w.writeToFileAny(path), "writeToFileAny(.json)");
    asvJSON r;
    check(r.readFromFileAny(path), "readFromFileAny(.json)");
    check(r.getInt("x") == 7, "read back value");
    std::remove(path);

    const char* yamlPath = "test_minimal_disabled.yaml";
    check(w.writeToFileAny(yamlPath), "writeToFileAny(.yaml) with YAML disabled");
    std::ifstream in(yamlPath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    check(!content.empty(), "disabled-format write still produces a file");
    check(content[0] == '{', "disabled-format write falls back to JSON");
    std::remove(yamlPath);
  }

  if (failures == 0) std::printf("minimal build smoke: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
