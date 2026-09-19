# asvJSON++

A C++17 JSON library supporting binary data, DateTime, Base64, BSON, MessagePack, CBOR, JSON Pointer, JSON Merge Patch, XML, YAML, CSV, TOON, TRON, GOON, UDE, Protobuf, TOML, JSON Lines (NDJSON), S-Expressions, JSON Schema validation, JSONPath (RFC 9535), JSON SAX/streaming parsing, and C++ struct/enum code generation.

**Author:** Sergey Andyk  asvzzz@narod.ru

## Online Converter

Try it online: **https://jx8.ru/**  -  browser-based converter supporting all 18 formats, with file upload/download.

## License

MIT license - see the `LICENSE` file for details.

## Features

### Core
- JSON types: String, Number, Boolean, Null, Object, Array
- Binary data with Base64 encoding
- Custom Base64 charset for encryption/obfuscation
- DateTime ISO-8601 with milliseconds
- File I/O operations
- Auto-format file I/O: `readFromFileAny()` / `writeToFileAny()` with heuristic format detection
- Type-safe accessors with `opt*` defaults
- `std::string_view` support for all key and string operations (zero-copy where possible)
- `noexcept` on all accessors (`getInt`, `getDouble`, `getBool`) for performance
- Object keys keep their insertion order (round-trip friendly): parse -> serialize preserves key order in JSON, YAML, INI and all formats; opt-in `ASVJSON_USE_ORDERED_MAP` switches to sorted `std::map`
- `std::from_chars` for fast double parsing on C++23 compilers
- `allowNaNInfinity` flag - parse/serialize NaN and Infinity values
- `operator[]` for chained navigation (`json["a"]["b"]["c"]`)
- Iterator API: `begin()`/`end()` for objects and arrays, `values()` range helper
- `emplace(key, val)` and `push(val)` for in-place value construction
- `operator==` / `operator!=` for deep structural equality
- `merge(other)` for RFC 7396 deep merge with null-deletion
- `diff(other)` for generating JSON Patch arrays
- Format detection: `getFormatType()` and `fromAny()` with heuristic content detection

### Comments
- `//`, `/* */`, `#` style comments

### Nested Access
- Dot notation: `json.getNested("user.address.city")`
- Escape dots with backslash: `json.getNested("domain\\.example.com")`

### Serialization Formats
- JSON (compact and pretty print)
- BSON (binary)  -  MongoDB document format; duplicate keys follow last-wins; string elements must be NUL-terminated; unsupported types (e.g. `CUSTOM_TAG`) are rejected with an error
- MessagePack (binary, RFC 7049)
- CBOR - `toCBOR()` / `fromCBOR()` - RFC 8949 Concise Binary Object Representation with indefinite-length items, half-precision floats, datetime/extension/regex tags
- YAML - `toYAML()` / `fromYAML()` with block-style sequences/mappings, YAML 1.2 `.nan`/`.inf` literals, automatic key quoting, literal block scalars, tags (`!!int`/`!!float`/`!!bool`/`!!null`/`!!str`/`!!set`/`!!omap`/`!!pairs`/`!!value`/`!!yaml`), block-style `!!set` (`- key` / `? key` items), explicit tags on block nodes (`!!map`/`!!seq`/`!!binary`/custom at root, mapping values, sequence items, explicit keys), complex (flow-collection) mapping keys stringified to JSON string keys, user-defined tags preserved as `CUSTOM_TAG` (with an optional `customTagTransform` hook to dispatch them to custom types at parse time), configurable serializer (`YamlWriteOptions`: flow/block/auto style, indent width, quoting control), anchors & aliases, multi-document streams, `%TAG`/`%YAML` directives, line-number-anchored errors
- XML - `toXML()` / `fromXML()` - serialization and parsing with attributes (`@`-prefix), text content (`#text`), CDATA, comments, type detection (datetime/binary/objectid/regex/timestamp/extension); malformed input (unterminated comments/CDATAs/attribute values) and duplicate attributes are rejected, and attribute/`#content` types that cannot be represented throw instead of being silently altered
- CSV - `toCSV()` with recursive flattening (`a.b.c`), two-pass union of keys for arrays of objects, `"` escaping per RFC 4180
- TOON - `toTOON()` / `fromTOON()` - token-oriented object notation with inline and tabular array formats, full round-trip serialization and parsing
- TRON - `toTRON()` / `fromTRON()` - token-reduced object notation with class definitions for repeated structures, class instantiation, inheritance, named arguments, round-trip support
- GOON - `toGOON()` / `fromGOON()` - greatly optimized object notation with YAML-like indentation, tabular arrays, inline/inference-based lists, single-char literals (T/F/_/~), dictionary references, round-trip support
- Protobuf - `toProtobuf()` / `fromProtobuf()` - Protocol Buffers binary wire format with schema-driven field mapping, packed fixed-size arrays; plus `toProtobufText()` / `fromProtobufText()` for human-readable text format
- TOML - `toTOML()` / `fromTOML()` - Tom's Obvious Minimal Language with tables, arrays of tables, inline tables/arrays, multi-line strings, hex/octal/binary integers, dotted keys, comments
- JSON Lines (NDJSON) - `toJSONLines()` / `fromJSONLines()` - one JSON value per line, ideal for streaming/ logs/bulk data
- S-Expression - `toSexpr()` / `fromSexpr()` - Lisp-style nested lists `(key "value" (nested 1 2 3))` with heuristic object/array detection, `;` comments, `nil`/`#t`/`#f` literals
- JSON5 - `toJSON5()` / `fromJSON5()` - JSON superset with unquoted keys, single-quoted strings, trailing commas, hex/octal/binary numbers, leading decimal, plus sign, comments, NaN/Infinity literals, Unicode identifiers, `\u{...}` code point escapes, extended whitespace (Zs), ES5 escape sequences (`\v`, `\0`, `\xHH`), control character validation, and MongoDB Extended JSON for special types
- INI - `toINI()` / `fromINI()` - classic initialization file format with sections, key-value pairs, `;`/`#` comments, quoted values, escape sequences, line continuation, and dot-notation section nesting
- Dotenv - `toDotenv()` / `fromDotenv()` - flat `KEY=VALUE` environment files with an optional `export` prefix, `#` comments (line and inline), single-quoted literal values, double-quoted values with C-style escapes (may span lines), and INI-style bare-value type inference
- UBJSON - `toUBJSON()` / `fromUBJSON()` - Universal Binary JSON (ubjson.org), big-endian markers for int8/uint8/int16/int32/int64, float32/float64, strings (`S`), null/bool, homogeneous typed containers (counted and optimized `$`-type with explicit counts), and the standard binary blob convention (`[$U#...` decodes to BINARY); object keys are written as bare length-prefixed strings; no magic header so `fromAny()` does not auto-detect it (use `fromUBJSON()` or the `.ubj` extension)
- Bencode - `toBencode()` / `fromBencode()` - the BitTorrent encoding (integers, byte strings, lists, sorted-key dictionaries); byte strings decode to STRING when valid UTF-8 and to BINARY otherwise; types with no bencode representation map lossily on encode (BOOL -> 0/1, NULL -> empty string, DOUBLE -> decimal string, DATETIME/TIMESTAMP -> seconds)
- JavaProp - `toJavaProp()` / `fromJavaProp()` - the classic Java `.properties` files: `#`/`!` comment lines, `=`/`:`/whitespace separators, `\` line continuation, `\uXXXX` escapes (incl. surrogate pairs), and INI-style bare-value type inference
- Plist - `toPlist()` / `fromPlist()` - the classic Apple XML property list (`<plist>`/`<dict>`/`<array>` wrapper, `<string>`/`<integer>`/`<real>`/`<true/>`/`<false/>`/`<date>`/`<data>` values, entities, comments)
- UDE - `toUDE()` / `fromUDE()` - Unified Data Exchange with a `# UDE` header, multiple documents (`// --- End of Document ---`), `#`/`//`/`/* */` comments, anchors/aliases, block scalars `|`/`>`, and type tags (`!int`, `!float`, `!string`, `!bool`, `!null`, `!schema`, `!base64`, `!bin`, `!datetime`, `!ext`, `!regex`); explicit tags preserve type info (a `!float` integral value round-trips as a float), digit separators (`1_000_000`, `0xFF_FF`), YAML-style non-finite floats (`.nan`, `.inf`, `-.inf`), big integers stored as exact raw literals, and `!datetime` with timezone offsets; strict mode rejects duplicate keys and special characters in bare tokens. Full specification: [UDE.md](UDE.md)
- RON - `toRON()` / `fromRON()` - Rusty Object Notation (the `ron` crate format) with structs, tuples, maps, named types, `Some`/`None` optionals, enum unit variants and variants with data (`Type::Variant(data)`), lists, bare idents, radix/separable/suffixed numbers (`0xFF`, `1_000`, `42u32`), floats (`.5`, `5.`, `5.e3`, `inf`, `-inf`, `NaN`), quoted strings with escapes and line continuation, raw strings (`r"..."`/`r#"..."#`), byte strings/chars (`b"..."`, `b'c'`), chars (`'a'`), ranges (`3..5`, `..=7`, `3..`, `..`, `..=`), `//`/nested `/* */` comments, `#![enable(...)]` headers, and trailing commas. `None` maps to `null`, `Some(x)` unfolds to `x`, empty `()` maps to an empty object; unit variants and bare idents become strings; ranges round-trip as `{"$range":{"start":...,"end":...,"inclusive":...}}` objects; special types (binary/objectid/regex/timestamp/extension/datetime/custom tags/YAML sets) round-trip through their Extended JSON marker objects
- C++ codegen - `toCpp()` / `fromCpp()` - generate C++17 `struct`/`enum` declarations from a parsed value tree, and parse C++ declarations back into a value tree: nested structs per object, `std::array<T,N>` (or `std::vector<T>`) for homogeneous arrays, `std::vector<std::variant<...>>` for mixed arrays, and enums inferred from identifier-string arrays (object-map enums via `enumPaths`/`enumNames`); a C-style output mode emits `typedef struct` / C arrays / `enum`; comments carry over as `//` / `/* */`

### Standards
- JSON Pointer (RFC 6901)
- JSON Merge Patch (RFC 7396)
- JSON Patch (RFC 6902)
- JSON Schema validation (draft-07)
- JSONPath (RFC 9535)

### JSON Schema Validation

```cpp
asvJSON doc;
doc.parse(R"({"name":"Alice","age":30})");

asvJSON schema;
schema.parse(R"({
  "type": "object",
  "required": ["name"],
  "properties": {
    "name": {"type": "string"},
    "age": {"type": "integer", "minimum": 0}
  }
})");

std::string error;
bool valid = doc.validate(schema, error); // true
```

Supports: `type`, `const`, `enum`, `allOf`/`anyOf`/`oneOf`/`not`, `if`/`then`/`else`, `$ref` (local), `minLength`/`maxLength`/`pattern`, `minimum`/`maximum`/`exclusiveMinimum`/`exclusiveMaximum`/`multipleOf`, `minItems`/`maxItems`/`uniqueItems`/`items`/`contains`, `required`/`properties`/`patternProperties`/`additionalProperties`/`minProperties`/`maxProperties`/`propertyNames`/`dependentRequired`/`dependencies`.

Disable with `#define ASVJSON_DISABLE_SCHEMA`.

### JSONPath (RFC 9535)

```cpp
asvJSON doc;
doc.parse(R"({"users":[{"name":"Alice","age":30},{"name":"Bob","age":25}]})");

auto results = doc.query("$.users[?(@.age >= 30)].name");
// results[0] points to "Alice"

const asvJSONValue* first = doc.queryFirst("$.users[0].name");
// first points to "Alice"

bool valid = asvJSON::isValidPath("$.users[*].name"); // true
```

Supports: `$`, `.member`, `['member']`, `[index]`, `[start:end:step]` slices, `..` recursive descent, `[*]` wildcard, `?(filter)` with `<`, `>`, `<=`, `>=`, `==`, `!=`, `&&`, `||`, `!` operators, and functions: `length()`, `count()`, `min()`, `max()`, `sum()`, `avg()`, `match()`, `search()`, `value()`.

`query()` is inherently lenient (RFC 9535): a selector it cannot evaluate (e.g. an unterminated bracket, arguments passed to a function, or a filter expression containing unsupported constructs) never throws  -  it contributes no matches, so a malformed path yields an empty result rather than a partial one. Function arguments are only allowed on the parenthesized right-hand side of filter operators (e.g. `[?(@.price ..)]`).

Disable with `#define ASVJSON_DISABLE_JSONPATH`.

### JSON SAX / Streaming Parser

```cpp
// Full-input SAX parsing
struct Handler : public JsonSaxHandler {
  void onKeyValue(const std::string& key, const asvJSONValue* val) override { ... }
  void onArrayBegin() override { ... }
  // ... other callbacks
};

asvJSON doc;
Handler h;
doc.parseSax(jsonString, h);

// Chunked streaming (for large files)
JsonStreamParser stream;
stream.setHandler(h);
while (auto chunk = readNextChunk()) {
  if (!stream.feed(chunk)) break;
}
stream.finish();
```

Disable with `#define ASVJSON_DISABLE_SAX`.

### Special Data Types
- Binary data (automatic Base64 in JSON, raw in BSON/MessagePack)
- DateTime (milliseconds precision)
- ObjectId (12-byte identifier) - serialized as MessagePack ext type 1
- Regex (pattern + options) - serialized as MessagePack ext type 2
- Timestamp (int64) - serialized as MessagePack ext type 3
- Extension types (MessagePack ext, configurable extType)

### C++17 Features
- Move semantics
- `std::string_view` for keys and strings
- Structured bindings
- Insertion-ordered object map (default backing store) - object keys keep their insertion order; heterogeneous `string_view`/`const char*`/`string` lookups via `SafeHash`/`std::equal_to<>`, `std::map` available via `ASVJSON_USE_ORDERED_MAP`
- Heterogeneous lookup helpers (`map_find`/`map_count`) with GCC < 9 fallback

### Thread Safety
- **Reading:** Fully thread-safe - multiple threads can read the same `asvJSON` instance simultaneously.
- **Writing:** Not thread-safe - a single `asvJSON` instance must not be written to concurrently from multiple threads, and reads must not overlap a write.
- **Distinct instances:** Separate `asvJSON` instances can be used concurrently from different threads; format parsers use `thread_local` state (e.g. YAML tag maps), so no global mutable state is shared.
- **Base64 custom charset:** `encodeBase64()` and `decodeBase64Fast()` are thread-safe (they snapshot the charset under a lock). `setBase64Chars()` is **not** thread-safe  -  call it before starting concurrent encoding/decoding.

## Limits

- MAX_NESTING_DEPTH: 48
- MAX_STRING_LEN: 10 MB
- MAX_ARRAY_SIZE: 1 000 000
- MAX_OBJECT_SIZE: 1 000 000

## Quick Start

```cpp
#include "asvJSON++.hpp"

int main() {
    asvJSON json;
    json.parse("{}");

    json.putString("name", "John");
    json.putInt("age", 30);
    json.putBool("enabled", true);
    json.putDouble("balance", 1234.56);

    std::string compact = json.serialize();
    std::string pretty = json.serialize(true);

    json.parse(compact);
    std::string name = json.getString("name");

    // Access nested values
    json.parse(R"({"user":{"address":{"city":"New York"}}})");
    std::string city = json.getString("user.address.city");

    // JSON Pointer
    auto* val = json.getByPointer("/user/address/city");
    std::string_view sv = val->getStringView();

    // Convert to other formats
    std::string xml = json.toXML();
    std::string yaml = json.toYAML();
    std::string csv = json.toCSV();
    std::string tron = json.toTRON();
    std::string sexpr = json.toSexpr();

    return 0;
}
```

## API Reference

### asvJSON Class

#### Construction & Parsing

| Method | Description |
|--------|-------------|
| `asvJSON()` | Default constructor. |
| `asvJSON(const asvJSON& other)` | Copy constructor (deep copy). |
| `asvJSON(asvJSON&& other) noexcept` | Move constructor. |
| `~asvJSON()` | Destructor. |
| `asvJSON& operator=(const asvJSON& other)` | Copy assignment. |
| `asvJSON& operator=(asvJSON&& other) noexcept` | Move assignment. |
| `bool parse(std::string_view json)` | Parse JSON. Accepts `std::string`, string literals, `const char*`, etc. (all convert to `std::string_view`). A UTF-8 BOM (`EF BB BF`) at the start is stripped automatically. |
| `void clear()` | Clear all data, reset to empty state. |
| `std::string getLastError() const` | Return last error message. |
| *(field)* `bool allowNaNInfinity` | When `true`, parses/serializes `NaN`, `Infinity`, `-Infinity` (non-standard JSON). Default `false`. |
| *(field)* `bool validateUTF8` | When `true`, `parse()` rejects input that is not well-formed UTF-8 (checked with `isValidUTF8`) and returns `false` with `lastError = "Invalid UTF-8 in input"`. A leading BOM is stripped before validation. Default `false`. |

#### Serialization & File I/O

| Method | Description |
|--------|-------------|
| `std::string serialize(bool pretty = false) const` | Serialize to JSON string. |
| `size_t byteSize() const` | Size in bytes of the serialized JSON document. **WARNING:** performs a full serialization (allocating the whole output string) just to count bytes. For very large documents, serialize once with `serialize()` and use the returned string's `.size()`. |
| `std::string toXML() const` | Serialize to XML document. |
| `bool fromXML(std::string_view input)` | Parse XML string (elements, attributes, type detection, CDATA). |
| `std::string toYAML() const` | Serialize to YAML document (block style). |
| `std::string toYAML(const YamlWriteOptions& opts) const` | Serialize to YAML with custom style/indent/quoting. |
| `bool fromYAML(std::string_view input)` | Parse YAML string (tags, anchors/aliases, multi-doc, directives). |
| `std::string toCSV() const` | Serialize to CSV (flattened, two-pass for arrays of objects). |
| `std::string toTOON() const` | Serialize to TOON (token-oriented object notation). |
| `bool fromTOON(std::string_view input)` | Parse TOON string. |
| `std::string toTRON() const` | Serialize to TRON (token-reduced object notation). |
| `bool fromTRON(std::string_view input)` | Parse TRON string (class definitions, inheritance, named args). |
| `std::string toGOON() const` | Serialize to GOON (greatly optimized object notation). |
| `bool fromGOON(std::string_view input)` | Parse GOON string (indentation-based, tabular arrays, dictionary refs). |
| `std::string toUDE(bool strict = false) const` | Serialize to UDE (Unified Data Exchange). |
| `bool fromUDE(std::string_view input, bool strict = false)` | Parse UDE string (header, multi-doc, anchors, tags, block scalars). |
| `std::vector&lt;uint8_t&gt; toProtobuf(const std::string&amp; schema) const` | Serialize to Protocol Buffers binary wire format (schema-driven). |
| `bool fromProtobuf(const uint8_t* data, size_t size, const std::string&amp; schema)` | Parse Protocol Buffers binary wire format. |
| `std::string toProtobufText() const` | Serialize to Protobuf text format (human-readable). |
| `bool fromProtobufText(const std::string&amp; text)` | Parse Protobuf text format. |
| `std::string toTOML() const` | Serialize to TOML (Tom's Obvious Minimal Language). |
| `bool fromTOML(std::string_view input)` | Parse TOML string (tables, arrays, inline tables, multi-line strings). |
| `std::string toJSONLines() const` | Serialize to JSON Lines (one JSON value per line). |
| `bool fromJSONLines(std::string_view input)` | Parse JSON Lines text into an array of values. |
| `std::string toSexpr() const` | Serialize to S-Expression (Lisp-style nested lists). |
| `bool fromSexpr(std::string_view input)` | Parse S-Expression string (heuristic object/array detection, `;` comments). |
| `std::string toRON(bool pretty = true) const` | Serialize to RON (Rusty Object Notation; struct/tuple/map/list syntax). |
| `bool fromRON(std::string_view input)` | Parse RON string (structs, tuples, maps, enums, options, ranges, comments). |
| `std::string toCpp(const CppWriteOptions& opts = {}) const` | Generate C++ `struct`/`enum` declarations from the value tree (modern C++17 or C style). |
| `bool fromCpp(std::string_view input, const CppReadOptions& opts = {})` | Parse C++ `struct`/`enum` declarations into the value tree. |
| `std::string toDotenv() const` | Serialize to Dotenv (`.env`) - flat `KEY=VALUE` lines; nested objects/arrays are written as quoted JSON text. |
| `bool fromDotenv(std::string_view input)` | Parse a Dotenv (`.env`) string (`export` prefix, quoted values, inline comments, type inference). |
| `std::string toBencode() const` | Serialize to Bencode (BitTorrent encoding; dictionary keys sorted as byte strings). |
| `bool fromBencode(std::string_view input)` | Parse a Bencode string (integers, byte strings, lists, dictionaries). |
| `std::string toJavaProp() const` | Serialize to Java `.properties` (bug-compatible with the JDK: unescaped trailing whitespace is dropped, leading whitespace escaped). |
| `bool fromJavaProp(std::string_view input)` | Parse a Java `.properties` string (`#`/`!` comments, `=`/`:`/whitespace separators, `\` continuation, `\uXXXX` escapes, type inference). |
| `std::string toPlist() const` | Serialize to an Apple XML property list (`<plist version="1.0">`, `<dict>`/`<array>`/`<string>`/`<integer>`/`<real>`/`<true/>`/`<false/>`/`<date>`/`<data>`). |
| `bool fromPlist(std::string_view input)` | Parse an Apple XML property list (declaration/DOCTYPE/comments accepted, entities and numeric refs unescaped, whitespace-wrapped `<data>` base64 decoded). |
| `std::vector<uint8_t> toUBJSON() const` | Serialize to UBJSON (big-endian; no magic header, so `fromAny()` will not auto-detect it). |
| `bool fromUBJSON(const void* data, size_t size)` | Parse UBJSON from raw bytes. |
| `bool fromUBJSON(const std::string& data)` | Parse UBJSON from `std::string`. |
| `bool writeToFile(const std::string& filename, bool pretty = false) const` | Write JSON to file. |
| `bool readFromFile(const std::string& filename)` | Read and parse JSON from file. |

#### Putting Values (Object / Root)

All `put*` methods create the root as an object if it doesn't exist.  
Keys can be `const char*`, `std::string`, or `std::string_view`.

| Method | Description |
|--------|-------------|
| `void putString(key, value)` | Add string. |
| `void putInt(key, int64_t value)` | Add 64-bit integer. |
| `void putDouble(key, double value)` | Add double. |
| `void putFloat32(key, float value)` | Add float (stored as double, serialised as float32 in MessagePack). |
| `void putBool(key, bool value)` | Add boolean. |
| `void putNull(key)` | Add JSON null. |
| `void putDateTime(key, time_t value)` | Add datetime (seconds since epoch). |

> **Note on DateTime serialization:** timestamps that `gmtime` cannot convert to UTC (e.g. before 1970, or beyond the platform's supported range) render as the literal text `"null"` (i.e. serialize as the JSON string `"null"`), and `getDateTimeString()` returns `""` for them. Years 1970+ are fully supported.
| `void putBinary(key, const uint8_t* data, size_t len)` | Add binary data. |
| `void putBinary(key, const std::vector<uint8_t>& data)` | Add binary data. |
| `bool putBinChunked(key, const uint8_t* data, size_t size, size_t chunk_size = 76)` | Split binary into Base64 chunks. Returns true on success, false if memory allocation fails. |
| `void putObjectId(key, std::string_view oid)` | Add ObjectId (12-byte binary). |
| `void putTimestamp(key, int64_t ts)` | Add timestamp (MessagePack-specific). |
| `void putRegex(key, const char* pattern, const char* options)` | Add regular expression. |
| `void putExtension(key, int8_t extType, const uint8_t* data, size_t len)` | Add MessagePack extension type. |

#### Getting Values (Object / Nested)

| Method | Return type | Description |
|--------|-------------|-------------|
| `getString(key)` | `std::string` | String value. |
| `getStringView(key)` | `std::string_view` | Zero-copy string view. |
| `getInt(key)` | `int64_t` | Integer value. |
| `getDouble(key)` | `double` | Double value. |
| `getBool(key)` | `bool` | Boolean value. |
| `getDateTime(key)` | `time_t` | Datetime (seconds). |
| `getDateTimeMs(key)` | `int` | Milliseconds part of datetime. |
| `getDateTimeString(key)` | `std::string` | ISO-8601 string. Returns `""` if the timestamp cannot be converted to UTC (e.g. before 1970). |
| `getBinary(key)` | `std::vector<uint8_t>` | Binary data. |
| `getBinChunked(key)` | `std::vector<uint8_t>` | Reconstruct binary from chunks. |
| `getObjectId(key)` | `std::string` | ObjectId as 12-byte string. |
| `getObjectIdView(key)` | `std::string_view` | Zero-copy ObjectId view. |
| `getTimestamp(key)` | `int64_t` | Timestamp value. |
| `getRegex(key)` | `std::pair<std::string,std::string>` | Regex pattern and options. |
| `getRegex(key, pattern, options)` | `bool` | Regex via out-parameters (returns false if not found). |
| `getExtension(key)` | `std::pair<int8_t, std::vector<uint8_t>>` | Extension type and data. |

#### Optional Access (with default)

| Method | Description |
|--------|-------------|
| `optString(key, defaultValue = "")` | Return string or default. |
| `optInt(key, defaultValue = 0)` | Return `int64_t` or default. |
| `optDouble(key, defaultValue = 0.0)` | Return double or default. |
| `optBool(key, defaultValue = false)` | Return bool or default. |
| `optDateTime(key, defaultValue = 0)` | Return `time_t` or default. |
| `optDateTimeTM(key, defaultValue = std::tm{})` | Return `std::tm` or default. |

#### Type Checks

| Method | Description |
|--------|-------------|
| `isNull(key)` | Check if value is JSON null. |
| `isString(key)` | Check if value is a string. |
| `isInt(key)` | Check if value is an integer. |
| `isDouble(key)` | Check if value is a double. |
| `isBool(key)` | Check if value is a boolean. |
| `isObject(key)` | Check if value is an object. |
| `isArray(key)` | Check if value is an array. |
| `isBinary(key)` | Check if value is binary data. |
| `isDateTime(key)` | Check if value is a datetime. |
| `isObjectId(key)` | Check if value is an ObjectId. |
| `isTimestamp(key)` | Check if value is a timestamp. |
| `isRegex(key)` | Check if value is a regex. |
| `isExtension(key, specificType = 0)` | Check if value is an extension type (optionally filter by extType). |

#### Object Query Methods

| Method | Description |
|--------|-------------|
| `bool hasKey(std::string_view key) const` | Check if key exists in the root object. |
| `void remove(std::string_view key)` | Remove a key from the root object. |
| `size_t size() const` | Number of keys/elements in the root. |
| `std::vector<std::string> getKeys() const` | Get all key names from the root object. |

#### Array Operations

Add elements to an array nested inside the root object.

| Method | Description |
|--------|-------------|
| `void arrayAddString(key, const char* value)` | Append a string to the array. |
| `void arrayAddInt(key, int64_t value)` | Append a 64-bit integer. |
| `void arrayAddDouble(key, double value)` | Append a double. |
| `void arrayAddBool(key, bool value)` | Append a boolean. |
| `void arrayAddNull(key)` | Append a null. |
| `void arrayAddDateTime(key, time_t value)` | Append a datetime. |

#### Raw Node Access

Methods that return `asvJSONValue*` pointers for direct traversal.

| Method | Return | Description |
|--------|--------|-------------|
| `get(std::string_view key)` | `asvJSONValue*` | Flat key lookup (no dot-path). |
| `getNested(std::string_view path)` | `asvJSONValue*` | Dot-notation lookup (`"user.address.city"`). |
| `getConst(std::string_view key)` | `const asvJSONValue*` | Const-qualified key lookup. |
| `getConst(size_t idx)` | `const asvJSONValue*` | Const array element by index. |
| `getObject()` | `asvJSONValue*` | Get-or-create root as object. |
| `getRoot()` | `asvJSONValue*` | Direct root access. |
| `getRootArray()` | `asvJSONValue*` | Get root if it is an array (non-const). |
| `getRootArray() const` | `const asvJSONValue*` | Get root if it is an array (const). |
| `getArray(std::string_view key)` | `asvJSONValue*` | Get array child by key. |

#### Nested Special Type Accessors

Convenience methods combining dot-path lookup with type extraction.

| Method | Description |
|--------|-------------|
| `getNestedObjectId(path)` | ObjectId via dot-path. |
| `getNestedTimestamp(path)` | Timestamp via dot-path. |
| `getNestedRegex(path)` | Regex (pattern, options) via dot-path. |

#### JSON Pointer (RFC 6901)

| Method | Description |
|--------|-------------|
| `getByPointer(std::string_view pointer)` | Lookup value by JSON Pointer (`/user/address/city`). |
| `setByPointer(std::string_view pointer, asvJSONValue* value)` | Set value at pointer path. |
| `removeByPointer(std::string_view pointer)` | Remove value at pointer path. |

#### Merge / Patch

| Method | Description |
|--------|-------------|
| `void merge(const asvJSON& other)` | Shallow merge of object keys. |
| `bool applyPatch(const asvJSON& patch)` | JSON Patch (RFC 6902) - array of operations. |
| `asvJSON applyMergePatch(const asvJSON& patch) const` | JSON Merge Patch (RFC 7396) - returns new document. |

#### Binary Format Conversion

| Method | Description |
|--------|-------------|
| `std::vector<uint8_t> toMessagePack() const` | Serialise to MessagePack. |
| `bool fromMessagePack(const uint8_t* data, size_t size)` | Parse MessagePack from raw bytes. |
| `bool fromMessagePack(const std::string& data)` | Parse MessagePack from `std::string`. |
| `std::vector<uint8_t> toCBOR() const` | Serialise to CBOR (RFC 8949). |
| `bool fromCBOR(const uint8_t* data, size_t size)` | Parse CBOR from raw bytes. |
| `bool fromCBOR(const std::string& data)` | Parse CBOR from `std::string`. |
| `std::vector<uint8_t> toBSON() const` | Serialise to BSON. |
| `bool fromBSON(const uint8_t* data, size_t size)` | Parse BSON from raw bytes. |
| `bool fromBSON(const std::string& data)` | Parse BSON from `std::string`. |
| `std::vector<uint8_t> toUBJSON() const` | Serialise to UBJSON (Universal Binary JSON). |
| `bool fromUBJSON(const uint8_t* data, size_t size)` | Parse UBJSON from raw bytes. |
| `bool fromUBJSON(const std::string& data)` | Parse UBJSON from `std::string`. |
| `std::string toJavaProp() const` | Serialise to Java `.properties`. |
| `bool fromJavaProp(std::string_view input)` | Parse Java `.properties`. |
| `std::string toPlist() const` | Serialise to an Apple XML property list. |
| `bool fromPlist(std::string_view input)` | Parse an Apple XML property list. |
| `static std::vector<uint8_t> messagePackFromString(const std::string& json)` | JSON string -> MessagePack. |
| `static std::string stringFromMessagePack(const uint8_t* data, size_t len)` | MessagePack -> JSON string. |
| `static std::vector<uint8_t> cborFromString(const std::string& json)` | JSON string -> CBOR. |
| `static std::vector<uint8_t> protobufFromString(const std::string& json)` | JSON string -> Protobuf binary (using numeric field names as IDs). |
| `static std::string stringFromProtobuf(const uint8_t* data, size_t len)` | Protobuf binary -> JSON string. |

### asvJSONValue Class

#### Static Factory Methods

Factory methods return `std::unique_ptr<asvJSONValue>`. Automatic lifetime management.

| Method | Description |
|--------|-------------|
| `makeString(const char* s, size_t len)` | Create a string value. |
| `makeStringOwned(char* s, size_t len)` | Create string from owned buffer (takes ownership). |
| `makeStringView(std::string_view sv)` | Create string from `string_view`. |
| `makeInt(int64_t n)` | Create a 64-bit integer. |
| `makeDouble(double d)` | Create a double. |
| `makeBool(bool b)` | Create a boolean. |
| `makeNull()` | Create a null value. |
| `makeObject()` | Create an empty object. |
| `makeArray()` | Create an empty array. |
| `makeDateTime(time_t ts, int ms = 0)` | Create a datetime. |
| `makeBinary(const uint8_t* data, size_t len)` | Create binary data. |
| `makeObjectId(std::string_view oid)` | Create a 12-byte ObjectId. |
| `makeTimestamp(int64_t ts)` | Create a timestamp. |
| `makeRegex(const char* pattern, const char* options)` | Create a regular expression. |
| `makeExtension(int8_t extType, const uint8_t* data, size_t len)` | Create an extension type. |

#### Instance Methods

| Method | Return | Description |
|--------|--------|-------------|
| `get(std::string_view key)` | `asvJSONValue*` | Child lookup by key. |
| `get(size_t idx)` | `asvJSONValue*` | Array element by index. |
| `getConst(std::string_view key)` | `const asvJSONValue*` | Const key lookup. |
| `getConst(size_t idx)` | `const asvJSONValue*` | Const array index access. |
| `size()` | `size_t` | Number of children. |
| `hasKey(std::string_view key)` | `bool` | Check key existence. |
| `getStringView()` | `std::string_view` | Zero-copy string content. |
| `getString()` | `const char*` | Raw C-string pointer. |
| `getStringLen()` | `size_t` | String length. |
| `getInt()` | `int64_t` | Integer value. |
| `getDouble()` | `double` | Double value. |
| `getBool()` | `bool` | Boolean value. |
| `getDateTime()` | `time_t` | Datetime timestamp. |
| `getDateTimeMs()` | `int` | Milliseconds portion. |
| `getBinary()` | `std::vector<uint8_t>` | Binary data. |
| `static typeToString(Type t)` | `std::string_view` | Convert type enum to human-readable name. |

### Free Functions

| Function | Description |
|----------|-------------|
| `cloneValue(const asvJSONValue* v)` | Deep-clone any value (returns new allocation). |
| `valuesEqual(const asvJSONValue* a, const asvJSONValue* b)` | Deep value comparison. |
| `setBase64Chars(const std::string& chars)` | Set custom Base64 alphabet (64 chars). **Not thread-safe**  -  call before concurrent use. |
| `getBase64Chars()` | Get current Base64 alphabet. Thread-safe. |
| `base64_encode(const uint8_t* data, size_t len)` | Encode to Base64 string. |
| `base64_decode_fast(const char* str, size_t len, bool* error)` | Decode Base64 to bytes. |
| `isValidUTF8(const uint8_t* data, size_t len)` | Validate UTF-8 byte sequence. |
| `xmlEscapeContent(std::string_view s)` | Escape XML special characters (`&`, `<`, `>`, `"`). |
| `xmlSanitizeElementName(std::string_view key)` | Make a valid XML element name (replace invalid chars with `_`). |
| `yamlNeedsQuotes(std::string_view s)` | Check if a YAML scalar needs quoting. |
| `yamlQuote(std::string_view s)` | Quote a YAML scalar. |
| `yamlQuoteKey(std::string_view s)` | Quote a YAML key if needed. |
| `csvEscape(std::string_view s)` | Escape a CSV field (RFC 4180). |

## Serialization Examples

### XML

Input:
```json
{"name":"John & Jane","active":true,"items":[1,2,3],"meta":{"nested":true}}
```

Output:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<root>
  <active>true</active>
  <items>
    <item>1</item>
    <item>2</item>
    <item>3</item>
  </items>
  <meta>
    <nested>true</nested>
  </meta>
  <name>John &amp; Jane</name>
</root>
```

### YAML

`toYAML()` serializes to indentation-based YAML with block-style sequences, literal block scalars, and tagged values.

`fromYAML()` parses YAML back into JSON, supporting the same features plus single/double-quoted strings, comments, and document markers.

`toYAML()` also accepts a `YamlWriteOptions` struct for fine-grained control:
```cpp
YamlWriteOptions o;
o.style = YamlWriteOptions::Style::Flow; // Block (default) | Auto | Flow
o.indentWidth = 4;                       // spaces per indentation level
o.flowMaxLen = 120;                      // Auto flow/block fallback threshold
o.alwaysQuoteStrings = true;             // quote every string scalar
o.alwaysQuoteKeys = true;                // quote every mapping key
o.doubleQuotes = true;                   // prefer "..." over '...'
std::string yaml = json.toYAML(o);
```
- `Style::Block`  -  pure indentation-based output (matches `toYAML(true)`, the default).
- `Style::Auto`  -  flow style with automatic block fallback for values longer than `flowMaxLen` (matches `toYAML(false)`).
- `Style::Flow`  -  flow style everywhere with no length limit; multiline strings are inlined as double-quoted scalars.

Input:
```json
{"name":"John","items":[1,2,3],"meta":{"nested":true}}
```

Output (`toYAML`):
```yaml
---
name: John
items:
  - 1
  - 2
  - 3
meta:
  nested: true
```

Round-trip (`fromYAML`):
```cpp
asvJSON json;
json.fromYAML("name: John\nitems:\n  - 1\n  - 2\n  - 3");
std::string name = json.getString("name"); // "John"
```

### CBOR (RFC 8949)

CBOR (Concise Binary Object Representation) is a binary JSON superset standardized as RFC 8949, designed for small code size and compact messages.

```cpp
asvJSON json;
json.parse("{\"name\":\"Test\",\"count\":42,\"active\":true}");
auto cbor = json.toCBOR();
// cbor.size() < json.serialize().size() - compact binary format

asvJSON json2;
json2.fromCBOR(cbor.data(), cbor.size());
```

CBOR support includes:
- **All major types**: unsigned/negative integers, byte strings, text strings, arrays, maps, simple values
- **Indefinite-length** arrays, maps, and strings
- **Floats**: half-precision (16-bit), single-precision (32-bit), double-precision (64-bit)
- **Tags**: datetime (tag 1), regular expression (tag 35), extension types (tag 257)
- **Static helper**: `asvJSON::cborFromString()` - JSON string -> CBOR bytes

### CSV

Input:
```json
{"name":"John","age":30,"meta":{"city":"NYC"}}
```

Output:
```
age,meta.city,name
30,NYC,John
```

Input (array of objects):
```json
[{"x":1,"z":3},{"y":2}]
```

Output (two-pass, union keys, sorted alphabetically):
```
x,y,z
1,,3
,2,
```

### TOON

Input:
```json
{"name":"John","age":30,"active":true,"items":[10,20,30],"address":{"city":"NYC"}}
```

Output:
```toon
name: John
age: 30
active: true
items[3]: 10,20,30
address:
  city: NYC
```

### TRON

TRON (Token Reduced Object Notation) is a JSON superset that reduces token count by defining reusable class schemas for repeated object structures.

Input:
```json
[{"name":"Alice","age":30},{"name":"Bob","age":25}]
```

Output:
```tron
class A: name,age
[A(Alice,30),A(Bob,25)]
```

TRON features:
- **Class definitions** hoist repeated property sets into a header
- **Class instantiation** uses positional or named arguments: `A(1,2)` or `A(x=1,y=2)`
- **Inheritance**: `class B(A): z` creates class B with A's properties plus `z`
- **Comments**: `#` to end of line
- Trailing commas allowed in arrays, objects, and class instances
- Objects with 1 property or 1 occurrence are serialized as plain JSON `{"key":val}`
- Full round-trip: `toTRON()` then `fromTRON()` returns the original data
- **Strict tokenizer**: unterminated quoted strings and unrecognized characters are rejected (`fromTRON()` returns `false`) instead of being silently skipped

### GOON

GOON (Greatly Optimized Object Notation) is a JSON superset with YAML-like indentation, designed for human-friendly data entry.

Input:
```json
{"name":"John","age":30,"active":true,"items":[10,20,30],"address":{"city":"NYC"}}
```

Output:
```goon
name: John
age: 30
active: T
items[]:
  - 10
  - 20
  - 30
address:
  city: NYC
```

Tabular arrays (array of objects with same keys) use a compact header:
```goon
[2]{x,y}:
  1,2
  3,4
```

GOON features:
- **Indentation-based** nesting (2-space indentation)
- **Single-char literals**: `T`=true, `F`=false, `_`=null, `~`=empty string
- **Inline lists**: `[N]: val1,val2,val3` or `[]:` with `- value` items on subsequent lines
- **Tabular arrays**: `name[N]{col1,col2}:` header with comma-separated rows
- **Dictionary references**: `$: $1="value"` on first line, `$1` resolves later
- **Comments**: `#` to end of line
- `#` prefix forces quoting to avoid comment ambiguity
- Full round-trip: `toGOON()` then `fromGOON()` returns the original data

### Protobuf

Protobuf (Protocol Buffers) is a binary serialization format developed by Google. The library supports both the binary wire format and a human-readable text format.

```cpp
asvJSON json;
json.putString("name", "Alice");
json.putInt("age", 30);
json.putBool("active", true);

// Schema-driven binary encoding
std::string schema = R"({
  "name":{"id":1,"type":"string"},
  "age":{"id":2,"type":"int32"},
  "active":{"id":3,"type":"bool"}
})";

auto buf = json.toProtobuf(schema);
// buf.size() < json.serialize().size() - compact binary format

asvJSON json2;
json2.fromProtobuf(buf.data(), buf.size(), schema);

// Human-readable text format
std::string text = json.toProtobufText();
// name: "Alice"
// age: 30
// active: true

asvJSON json3;
json3.fromProtobufText(text);
```

Protobuf features:
- **Schema-driven**: field numbers and types defined in a JSON schema object
- **Varint encoding**: compact integer representation
- **Packed fixed-size arrays**: `fixed32`, `fixed64`, `float`, `double` packed as contiguous blocks
- **Static helpers**: `protobufFromString()` / `stringFromProtobuf()` for JSON string <-> binary conversion
- **Text format**: human-readable `key: value` representation with nested messages and arrays

### TOML

TOML (Tom's Obvious Minimal Language) is a configuration file format designed for readability. The library supports full round-trip serialization and parsing.

```cpp
asvJSON json;
json.parse(R"({"name":"John","age":30,"active":true,"items":[10,20,30],"address":{"city":"NYC"}})");

// Encode to TOML
std::string toml = json.toTOML();
// toml:
// name = "John"
// age = 30
// items = [10, 20, 30]
// active = true
// [address]
// city = "NYC"

// Decode from TOML
asvJSON j2;
j2.fromTOML(toml);
// j2.getString("name") == "John"
// j2.getInt("age") == 30
```

TOML features:
- **Tables and nested tables**: `[table]` / `[table.subtable]` headers
- **Arrays of tables**: `[[array]]` headers for array-of-object structures
- **Inline tables**: `{key = value, ...}` compact syntax with dotted keys (`{x.y = 1, x.z = 2}`)
- **Inline arrays**: `[1, 2, 3]` with typed values
- **Multi-line strings**: `"""..."""` basic and `'''...'''` literal (leading newline trimmed per spec)
- **Numeric types**: integers (decimal, hex `0x`, octal `0o`, binary `0b`), floats, booleans
- **Dotted keys**: `a.b.c = 42` creates nested structure
- **Quoted keys**: `"key with spaces"` via JSON-style basic strings
- **Literal strings**: `'C:\Windows'` for raw unescaped strings
- **Comments**: `#` to end of line, both full-line and inline
- **Null handling**: TOML has no null type; null values are skipped during encoding
- **Full round-trip**: `toTOML()` then `fromTOML()` returns the original data

### JSON Lines (NDJSON)

JSON Lines (also called NDJSON) is a text format where each line is a valid JSON value. It is well-suited for streaming data, log files, and bulk database operations.

```cpp
asvJSON json;
json.parse(R"([{"name":"Alice","age":30},{"name":"Bob","age":25}])");

// Encode to JSON Lines
std::string jl = json.toJSONLines();
// jl:
// {"name":"Alice","age":30}
// {"name":"Bob","age":25}

// Decode from JSON Lines
asvJSON j2;
j2.fromJSONLines(jl);
// j2.getRoot()->get(0)->get("name") == "Alice"
```

Features:
- **Array root**: each element becomes one line
- **Non-array root**: serialized as a single line
- **Empty lines**: silently skipped during parsing
- **Windows line endings**: `\r\n` handled transparently
- **Round-trip**: `toJSONLines()` then `fromJSONLines()` returns the original array
- **Error handling**: invalid JSON on any line rejects the entire input

### S-Expression

S-Expression (S-expression) is a Lisp-style nested list format: `(key "value" (nested 1 2 3))`. It is useful for metaprogramming, AST representation, and configuration.

```cpp
asvJSON json;
json.parse(R"({"name":"John","age":30,"active":true,"address":{"city":"NYC"}})");

// Encode to S-Expression
std::string sexpr = json.toSexpr();
// sexpr: (name "John" age 30 active #t address (city "NYC"))

// Decode from S-Expression
asvJSON j2;
j2.fromSexpr(sexpr);
// j2.getString("name") == "John"
// j2.getInt("age") == 30
```

S-Expression features:
- **Objects**: alternating key-value pairs in a list, e.g., `(name "John" age 30)`
- **Arrays**: plain list of values, e.g., `(1 2 3)`
- **Heuristic detection**: even-length list with string/symbol keys  ->  object; otherwise  ->  array
- **Nested structures**: `(person (name "Alice" age 25))`
- **Booleans**: `#t` (true), `#f` (false)
- **Null**: `nil`
- **Strings**: `"quoted"` with C-style escapes (`\n`, `\t`, `\"`, `\\`)
- **Comments**: `;` to end of line
- **Number handling**: tokens shaped like numbers but invalid as JSON (leading `+`, leading zeros, `1.e5`) decode as strings rather than failing the whole document
- **Non-finite doubles**: `NaN`/`Infinity` cannot be serialized  -  `toSexpr()` throws instead of emitting `nil`
- **Full round-trip**: `toSexpr()` then `fromSexpr()` returns the original data

### RON  -  Rusty Object Notation

`toRON(bool pretty = true)` / `fromRON()`  -  the format used by Rust's `ron` crate.

```
asvJSON j;
j.parse(R"({"name":"John","age":30,"active":true,"address":{"city":"NYC","zip":10001}})");
std::string ron = j.toRON();
// (
//   name: "John",
//   age: 30,
//   active: true,
//   address: (
//     city: "NYC",
//     zip: 10001,
//   ),
// )
```

Decoding:

```cpp
asvJSON j;
j.fromRON(R"((name: "Alice", mode: Auto, coord: (10, 20), level: 3..=5, maybe: Some("hidden")))");
// j.getString("name") == "Alice"
// j.getString("mode") == "Auto"          (bare ident / enum unit variant)
// j.getRoot()->get("coord")->get(0)->getInt() == 10   (tuple -> array)
// level round-trips as {"$range":{"start":3,"end":5,"inclusive":true}}
// j.getString("maybe") == "hidden"       (Some(x) unfolds)
```

RON mapping rules:
- `None`  ->  `null`; `Some(x)`  ->  `x`; empty `()`  ->  empty object; `[]`/`{}` stay empty collections
- Bare idents and enum unit variants (`Red`, `Running`)  ->  strings; named structs/tuples and qualified variants keep their name as a key: `GameConfig(...)`  ->  `{"GameConfig": ...}`, `Type::Variant(data)`  ->  `{"Type::Variant": ...}`
- Encoding `{"GameConfig": {"width": 800}}` emits the named form `GameConfig(width:800)` (a single-key object whose value is an object or non-empty array and whose key is a valid RON type name), so the name survives RON  ->  JSON  ->  RON round-trips; empty-object values emit `GameConfig()`; scalar, empty-array, range and YAML-set values keep the plain struct/map form
- Keys that are valid identifiers emit as `(key: value)` structs; other keys force `{ "key": value }` map syntax
- Numbers: radix (`0xFF`, `0o17`, `0b1010`), digit separators (`1_000`), type suffixes (`42u32`, `3.14f64`), `.5`/`5.`/`5.e3` normalization, `inf`/`-inf`/`NaN`
- Ranges round-trip as `{"$range":{"start":...,"end":...,"inclusive":...}}` marker objects
- Comments `//` and nested `/* */`, `#![enable(...)]` headers, trailing commas
- Comments round-trip: `fromRON()` attaches comments to their nodes and `toRON()` re-emits them in place (leading above a key/item, same-line trailing after a value, closings before `)`/`}`/`]`, and a trailing footer after the document). The attached comments also survive conversion into the other comment-capable text formats: `toJSON5()` keeps them verbatim (`//`/`/* */` are native), `toYAML()` translates them to `#` lines, and `toUDE()` keeps them verbatim (`//`/`/* */` are native UDE comments). `serialize()`/JSON output intentionally drops them (JSON has no comment syntax)
- Special types (binary/objectid/regex/timestamp/extension/datetime/custom tags/YAML sets) round-trip via Extended JSON markers

### C++ Struct/Enum Generation  -  `toCpp()` / `fromCpp()`

`toCpp(const CppWriteOptions& = {})` infers C++ type declarations from a parsed value tree. `fromCpp(input, const CppReadOptions& = {})` parses C++ `struct`/`enum` declarations back into a value tree, so `fromCpp(toCpp(x))` recovers the original for the common cases.

```
asvJSON j;
j.fromRON(R"(
GameConfig(
  window_size: (800, 600),
  window_title: "PAC-MAN",
  fullscreen: false,
  key_bindings: { up: "Up", down: "Down" },
)
)");

CppWriteOptions o;
o.enumPaths.push_back("key_bindings");
o.enumNames.push_back({"key_bindings", "Direction"});
std::cout << j.toCpp(o);
// enum class Direction { Up, Down, };
// struct KeyBindings { Direction up = Direction::Up; Direction down = Direction::Down; };
// struct GameConfig {
//     std::array<int, 2> window_size = {800, 600};
//     std::string window_title = "PAC-MAN";
//     bool fullscreen = false;
//     KeyBindings key_bindings = {};
// };
```

**Inference rules:**

| Input shape | Generated type |
|---|---|
| OBJECT | `struct`, one member per key |
| ARRAY of one type | `std::array<T, N>` (or `std::vector<T>` when `useStdArray = false`) |
| ARRAY of objects | `std::array<Struct, N>` / `std::vector<Struct>` (union of keys; struct named from the field) |
| ARRAY of mixed types | `std::vector<std::variant<...>>` |
| empty ARRAY | `std::vector<std::monostate>` placeholder (edit the type) |
| ARRAY of identifier strings | an `enum` (disable with `emitEnums = false`) |
| INT / DOUBLE / BOOL | `int` / `double` / `bool` |
| STRING | `std::string` (`stringType`) |
| DATETIME / BINARY | `std::time_t` / `std::vector<std::uint8_t>` |
| NULL | `std::nullptr_t` |

**Root handling:** a single-key root `{ "Name": { ... } }` is unwrapped into `struct Name` (`unwrapRootSingleKey`, default on); otherwise the root struct is named by `rootName` (default `Document`). A root array/scalar emits `using Name = <type>;`.

**Options (`CppWriteOptions`):** `style` (`ModernCpp` default, or `C`), `emitIncludes`, `emitDefaults`, `emitComments`, `emitEnums`, `emitEnumMaps` (treat any object map of identifier strings as an enum; default `false`), `enumPaths` / `enumNames` (force and name enums for matching paths/field names), `useEnumClass`, `useStdArray`, `unwrapRootSingleKey`, `forwardDeclare`, `indentWidth`, `rootName`, and per-type overrides `intType` / `stringType` / `binaryType` / `datetimeType` / `nullType`.

C style emits `typedef struct ... ;`, C arrays (`int window_size[2];`), plain `enum` types (`enum Direction`), and no member initializers (C has none); the include preamble uses `<stdbool.h>`, `<stdint.h>` and `<time.h>`. Disable the whole feature with `ASVJSON_DISABLE_CPP`.

> **Note:** RON enum unit variants parse to plain strings (`Up` is indistinguishable from `"Up"`), so object-map enums must be opted into explicitly via `enumPaths`; identifier-string arrays are auto-detected.

**Parsing C++ back  -  `fromCpp()`:** the decoder lexes declarations, resolves named types, and materializes a value tree from the default member initializers (initializers are the data source, so C-style output, which has none, yields type defaults).

```
asvJSON j;
j.fromCpp(R"(
struct Document {
    std::string name = "PAC-MAN";
    std::array<int, 2> window_size = {800, 600};
    bool fullscreen = false;
};
)");
std::cout << j.serialize();  // {"name":"PAC-MAN","window_size":[800,600],"fullscreen":false}
```

Supported constructs: `struct`/`class` definitions (public members; methods, access specifiers, bitfields, templates and bases skipped), C `typedef struct { ... } Name;`, nested `struct`s with a trailing declarator, `enum` / `enum class` (with optional underlying type and explicit values), `using Name = Type;` and `typedef` aliases, and forward references. Types: scalars (`int`, `double`, `bool`, `std::string`, `const char*`), `std::array<T,N>` and C `T[N]` arrays, `std::vector`/`std::list`/`std::deque`/`std::set` (dynamic), `std::variant<...>`, `std::optional<T>`, `std::map`/`std::unordered_map`, and `std::pair`. `//` and `/* */` comments are preserved on nodes when `includeComments` is set. A fixed `std::array<Enum,N>` left uninitialized with `N` equal to the enum's member count is filled with the member names in order (recovering enum arrays).

Object-like `#define NAME replacement` macros are expanded across the declaration before parsing, so constants (including multi-line `\`-continued ones and chains) resolve in member initializers, array lengths (`T name[N]`, `std::array<T,N>`) and enum contexts; the replacement may be a number, string/char literal, `true`/`false`/`nullptr`, a parenthesized or unary-signed literal, or another macro. Function-like macros (`#define F(x) ...`) are left untouched. Scalar initializers also accept surrounding parentheses and a leading unary sign (`int n = (-1);`); the `toCpp` form `(-9223372036854775807LL - 1)` for `INT64_MIN` parses back to the boundary value.

`std::unique_ptr<T>` / `std::shared_ptr<T>` member types melt to their pointee, so generated headers round-trip. Note that a self-referential struct (`struct Node { std::unique_ptr<Node> next; };`) materializes its initializer tree up to the depth guard (`MAX_NESTING_DEPTH`) before erroring - expected for recursive structures with unbounded data.

**Root handling:** when an alias (`using`/`typedef`) exists, the last one in source order becomes the root; otherwise the root is the single struct not referenced by any other struct. A root named `rootName` (default `Document`) is unwrapped to its members; any other single struct is wrapped as `{ "Name": { ... } }`, and multiple roots are wrapped the same way.

**Options (`CppReadOptions`):** `unwrapRootStruct` (unwrap a single root struct, default `true`), `materializeDefaults` (use member initializers, default `true`), `includeComments` (attach parsed comments, default `true`), `strict` (throw instead of skipping unparsable members, default `false`), and `rootName` (default `"Document"`). Disable the whole feature with `ASVJSON_DISABLE_CPP`.

> **Note  -  lossy by design:** code generation cannot encode every JSON shape, so some round-trips lose data: single-key (`{Name:{...}}`) and alias roots lose the wrapper key; arrays of heterogeneous objects, top-level arrays/scalars, and `std::variant`/`std::map` values recover only type and length (fixed arrays repeat the first element's values); C-style output carries no initializers. `std::variant` alternatives that collapse to the same type (e.g. several integer types) resolve to the first match.

### JSON5  -  Extended JSON superset

`toJSON5()` / `fromJSON5()`  -  JSON5 with MongoDB Extended JSON for special types.

**JSON5 features:** unquoted keys (Unicode identifiers), single-quoted strings, trailing commas, hex (`0x`), octal (`0o`), binary (`0b`) numbers, leading decimal (`.5`), plus sign (`+42`), `//`/`/* */`/`#` comments, `NaN`/`Infinity`/`-Infinity`/`-NaN` literals, `\u{...}` code point escapes (surrogate pairs for supplementary), ES5 escape sequences (`\v`, `\0`, `\xHH`), extended whitespace (Zs Unicode spaces, BOM), control character validation outside strings, locale-independent identifier parsing.

```
{name:"John",age:30,active:true,address:{city:"NYC",zip:10001}}
```

**Extended JSON** (MongoDB Canonical format) for special data types:

| Type | Serialized form |
|---|---|
| OBJECTID | `{"$oid":"507f1f77bcf86cd799439011"}` |
| REGEX | `{"$regex":"^test$","$options":"gi"}` |
| TIMESTAMP | `{"$timestamp":{"t":1712345678}}` |
| BINARY | `{"$binary":{"base64":"3q0=","subType":"00"}}` |
| EXTENSION | `{"$binary":{"base64":"3q0=","subType":"2a"}}` |
| DATETIME | `"2026-07-20T00:21:57Z"` (ISO 8601) |

### INI  -  Classic initialization file format

`toINI()` / `fromINI()`  -  the classic initialization file format with dot-notation section nesting for hierarchical data.

```
; Sample configuration
name = John
age = 30

[address]
city = NYC
zip = 10001

[network.server]
ip = 10.0.0.1

[network.client]
retries = 3
```

**Features:** sections `[name]` with dot-notation nesting (`[parent.child]`), `;`/`#` comments, quoted `"values"` with escape sequences (`\n`, `\t`, `\\`, `\;`, `\#`, `\=`), line continuation (`\` at end of line), global keys (before any section), `=`/`:`/space delimiters, duplicate key override, array of objects support.

On decoding, all values are stored as strings. On encoding, types are formatted appropriately (numbers bare, booleans `true`/`false`, objects as nested sections, arrays of objects as duplicate sections).

### Dotenv  -  environment configuration files

`toDotenv()` / `fromDotenv()`  -  flat `KEY=VALUE` files used for environment variables.

```
# Application settings
export APP_NAME="My App"
PORT=8080
DEBUG=true # verbose
EMPTY=null
```

**Features:** optional `export ` prefix, `#` line comments and inline comments, single-quoted literal values, double-quoted values with C-style escapes (`\n`, `\r`, `\t`, `\\`, `\"`, `\'`, `\$`) that may span multiple lines, INI-style bare-value type inference (null/bool/int/double/datetime, with leading-zero/hex/overflow literals kept as exact strings), duplicate keys last-wins, and comment preservation on YAML-style round-trips (leading/trailing/footing). `toDotenv()` requires an object root; nested objects/arrays have no dotenv syntax and are emitted as their compact JSON text inside a quoted string (lossless text, but they decode back as strings). Auto-detection (`fromAny`, `.env`/`.env` files) is wired in and only attempted for flat `=`-containing input with no `[section]` headers.

### Bencode  -  BitTorrent encoding

`toBencode()` / `fromBencode()`  -  the encoding used by BitTorrent `.torrent` files.

```
d7:formatsi22e4:name9:asvJSON++4:tagsl4:json3:c++ee
```

**Format:** integers `i42e`, byte strings `4:spam`, lists `l...e`, dictionaries `d...e`. **Decoding:** integers become INT; byte strings become STRING when the bytes are valid UTF-8 and BINARY otherwise; lists become ARRAY; dictionaries become OBJECT. **Encoding:** INT/STRING/BINARY/ARRAY/OBJECT encode natively and dictionaries are emitted with keys in ascending byte order (as the spec requires). Value types with no bencode representation map to the closest form (documented lossy): BOOL -> integer 0/1, NULL -> empty byte string, DOUBLE -> its decimal text as a byte string, DATETIME/TIMESTAMP -> integer seconds, OBJECTID/REGEX -> byte string, EXTENSION -> raw bytes, CUSTOM_TAG -> its payload value. So JSON5 -> bencode round-trips work for arbitrary data (e.g. `{ratio: 1.5, active: true}` -> `d6:activei1e5:ratio3:1.5e`). Auto-detection (`fromAny`, `.torrent`/`.bencode` files) tries bencode first for input starting with a `d<digit>:` dictionary signature (before the YAML-like GOON/TOON parsers that would otherwise claim short colon-heavy input) and, in the binary path, only for that same signature; a full consuming parse is always required.

### JavaProp  -  Java `.properties` files

`toJavaProp()` / `fromJavaProp()`  -  the classic Java `.properties` files used for application configuration.

```
# application settings
! generated by build 42
app.name=My App
app.port: 8080
debug=true
path=C:\\test\\file
```

**Features:** `#`/`!` comment lines (preserved on round-trips as leading/footing via the shared `Comments` metadata), `=`/`:`/first-whitespace separators, `\` line continuation (and `\\` kept literal), escape sequences `\\` `\t` `\n` `\r` `\f` `\uXXXX` (with surrogate pairs combined into 4-byte UTF-8 and lone surrogates replaced by U+FFFD), and INI-style bare-value type inference (null/bool/int/double/datetime, with leading-zero/hex/overflow literals kept as exact strings); duplicate keys last-wins; no quoting, and `#` mid-value is data (never an inline comment). The encoder is bug-compatible with the JDK: unescaped trailing whitespace in a value is dropped, leading whitespace escaped, keys escape `\ = : # ! space` plus control characters, and nested objects/arrays are emitted as compact JSON text (documented lossy). `toJavaProp()` requires an object root. Auto-detection (`fromAny`, `.properties`/`.props` files) is wired in after the dotenv gate and lures content with an `!` comment line, a `\u` escape, an `=` separator, or an unspaced `:` separator (whitespace-separated pairs are deliberately not distinctive enough).

### Plist  -  Apple Property List (XML)

`toPlist()` / `fromPlist()`  -  the classic XML `.plist` files used by Apple configuration (`Info.plist`, `CFPreferences`, Xcode settings).

**Format:** an XML document rooted at `<plist version="1.0">` wrapping a single value: `<dict>` of alternating `<key>`/value pairs, `<array>`, or a scalar. Value elements map onto the JSON model - `<string>`  ->  STRING, `<integer>`  ->  INT, `<real>`  ->  DOUBLE, `<true/>`/`<false/>`  ->  BOOL, `<date>`  ->  DATETIME (ISO-8601 with timezone preserved), `<data>`  ->  BINARY (base64). **Decoding** accepts an optional `<?xml ...?>` declaration, `<!DOCTYPE ...>` (including bracketed internal subsets), and `<!-- -->` comments between elements; entities `&amp;` `&lt;` `&gt;` `&quot;` `&apos;` plus numeric references `&#NN;`/`&#xHH;` are unescaped; a UTF-8 BOM is stripped; and the `<plist>` wrapper is optional (a bare `<array>`/`<dict>`/scalar document also parses). Malformed documents are rejected with a `plist:` error, never silently mis-parsed (strict string truncation, invalid numbers/dates/base64, missing keys, unknown elements, trailing content). **Encoding** emits the XML declaration + DOCTYPE + `<plist>` wrapper with 2-space indentation. plist has no null type: NULL is written as an empty `<string/>` and reads back as an empty string (documented lossy). Special types degrade to their underlying representation: REGEX  ->  `<string>` (pattern only), TIMESTAMP  ->  `<integer>` (seconds), OBJECTID/EXTENSION  ->  `<data>`, CUSTOM_TAG  ->  its payload value (tag name dropped). Auto-detection (`fromAny`) runs before the generic XML parser when the document declares a `<plist` root; `.plist` files dispatch through `readFromFileAny`/`writeToFileAny`.

### UBJSON  -  Universal Binary JSON

`toUBJSON()` / `fromUBJSON()`  -  the compact binary encoding defined at ubjson.org; a direct binary sibling of JSON.

```
{ i 0x01 a  i 0x2A }        // {"a":42}
S i 0x03 f o o              // "foo"
[ $ U # i 0x03 <bytes...>   // binary blob (decodes to BINARY)
```

**Format:** big-endian markers `Z` (null), `N` (noop), `T`/`F` (bool), `i`/`U`/`I`/`l`/`L` (int8/uint8/int16/int32/int64), `d`/`D` (float32/float64), `H` (high-precision number, kept as an exact STRING to avoid precision loss), `C` (1-char string), `S` (string), `[` (array) and `{` (object). **Encoding:** numbers use the smallest fitting integer type; strings are written as `S <len-type><len><bytes>`; object keys are bare length-prefixed strings (no `S` marker); containers are emitted in the simple unoptimized `[...]`/`{...}` form, and BINARY is written with the spec's binary-data convention `[$U#<len><bytes>`. **Decoding:** all containers are accepted - unoptimized, counted (`[#<n>`), and optimized/typed (`[$<type>#<n>`); a typed uint8 array `[$U#...` decodes to BINARY; `$` without a `#` count is rejected; `N` noops are skipped inside containers. DATETIME encodes as float64 seconds, TIMESTAMP as int64 seconds, REGEX as its string, and OBJECTID/EXTENSION payloads as binary blobs (intentional - see the JSON representation for lossless forms). UBJSON has no magic leading bytes, so `fromAny()` does not auto-detect it - call `fromUBJSON()` directly or use the `.ubj`/`.ubjson` file extensions.

## Changelog

### 1.25.8 (2026-09-19)

Addresses two S-Expression serializer fixes (`asvjson/formats/sexpr.hpp`):

- **Fix (Sexpr, NaN/Infinity serialization contract):** the DOUBLE case previously threw `asvJSONError("Cannot serialize NaN or Infinity")`, which broke parity with every other `to*()` serializer in the library - none of them throw out of a valid tree. It now emits the same `"nil"` placeholder used for null values (and consistent with how sibling formats handle non-finite doubles), so serialization never fails on this input; decoding behavior is unchanged (`nil` still reads back as null).
- **Fix (Sexpr, CUSTOM_TAG encoder paren imbalance):** the S-expression emitted a spurious extra opening parenthesis before `"value"` - `("\x01$customTag" ("\x01name" "foo" (" \x01value" 42))` - producing unbalanced parens that only happened to parse back correctly by accident of where the reader's matching logic landed. The stray `(` is removed, so the inner object group is a clean flat two-key form: `("\x01$customTag" ("\x01name" "foo" "\x01value" 42))`.
- **Test:** new coverage in `testAuditMedLow` - NaN and +Infinity now assert `"nil"` appears in the serialized S-expression (replacing a previous assertion that serialization must throw), plus round-trip checks for valued (`{"$customTag":{"name":"foo","value":42}}`) and null-valued custom tags through `$customTag` Extended JSON -> `toSexpr()` -> `fromSexpr()`, verifying the tag name, value type, and payload survive intact.

### 1.25.7 (2026-09-19)

Addresses review + edge-case findings in the incremental JSON SAX/streaming parser (`JsonStreamParser`, `asvjson/formats/jsonsax.hpp`):

- **Fix (SAX, error-path contract):** documented that `onError` is a nothrow notification point - it fires both inside the parsers' internal try/catch and on early-return validation paths that bypass it; handlers must not throw from it.
- **Fix (SAX, per-container size limits):** object/array element counters now reset at each fresh container open (`cntObject_[depth_]`/`cntArray_[depth_] = 0`) so `MAX_OBJECT_SIZE`/`MAX_ARRAY_SIZE` are enforced per container rather than leaking across sibling containers on the same nesting level.
- **Fix (SAX, datetime heuristic removed):** string values - both top-level and object members - are now always reported unescaped via `onStringValue`, even when the raw text happens to look like an ISO timestamp; SAX has no dedicated datetime event, so callbacks receive decoded bytes only (the DOM parser's separate DATETIME path is unaffected).
- **Fix (SAX, int64 overflow divergence documented):** a number too large for `int64_t` degrades to a double in the SAX stream while the DOM parser keeps it as an exact raw-number STRING - both are valid representations; this asymmetry is now explicitly documented at the call site.
- **Fix (SAX, empty feed chunk no-op):** `feed("")` is now guaranteed safe and can never be mistaken for "data after complete value" (previously reachable via a 0-byte read on an already-complete stream).
- **Fix (SAX, UTF-8 cut off at EOF):** multibyte validation (`utf8Need_`) moved ahead of accepting the completed top-level value, so input ending mid-sequence inside a trailing comment is rejected even when state had reached `TAIL`/comment mode.
- **Fix (SAX, dangling `/` at EOF / dead code path):** introduced an explicit `State::TAIL` for "complete top-level value produced"; a lone `/` waiting on its second character at EOF is now correctly reported as data-after-value instead of being silently accepted by a self-contradictory old branch; comment-only and empty input are both rejected with clear, DOM-matching error messages.
- **Fix (SAX, number token DoS guard):** added `MAX_NUMBER_LEN = 4096` bound on the accumulated cross-chunk number token so a pathological stream cannot grow it without limit.
- **Fix (SAX, NaN/Infinity gating made conditional):** `isValueStart()` now consults `allowNaNInfinity`, matching the DOM parser's opt-in behavior for non-standard JSON literals (`NAN`/`INF`).
- **Test:** new regression cases in `testJsonStream`: empty input rejected, comment-only input rejected, dangling `/` at EOF rejected (and accepted once a full trailing comment arrives), incomplete UTF-8 inside a trailing comment rejected.

### 1.25.6 (2026-09-19)

Addresses the reviewer pass in `err.txt` (JSON Schema draft-07 validator):

- **Fix (schema, const/enum/uniqueItems with extended types):** `valuesEqual` previously compared only the type and scalar fields, so `DATETIME`, `BINARY`, `OBJECTID`, `REGEX`, `TIMESTAMP`, `EXTENSION` and `CUSTOM_TAG` never matched `const`/`enum` (and duplicate detection under `uniqueItems`) - any document failed a matching `const` (R2.1). `valuesEqual` now compares the extended payloads: DATETIME compares `timestamp + datetime_ms + tz_offset_min`, BINARY compares `bin_data` bytes, OBJECTID/REGEX compare their bytes, TIMESTAMP compares the integer, EXTENSION compares `ext_type + bin_data`, CUSTOM_TAG compares `str_data + custom_value` (recursively).
- **Fix (schema, non-string `$ref`):** a `$ref` whose value is not a string was silently ignored, letting the rest of the schema validate as if the keyword were absent (e.g. `{"$ref":42}` behaved like `{}`). It now throws `Invalid schema: '$ref' must be a string` (R2.2).
- **Perf (schema):** `resolveRef` results and `validateKeywordShapes` shape checks are cached (`refCache_`, `shapeCache_` - keyed by JSON-pointer string), so repeated/cyclic references no longer re-walk the tree on every visit; `contains` no longer re-creates its sub-validator per item (`moveSubVal` + fresh `path_`/`error_` per iteration); `matchedByPattern_` switched from `std::set` to `std::unordered_set` (R3.1-R3.4).
- **Fix (schema, cycle guard):** the old `refDepth_` counter with a magic `48` limit was replaced by a real `RefStack` - an unordered set of `(value*, schema*)` pairs (`std::unordered_set` of a `{value*, schema*}` struct). Legitimate recursion (self-`$ref` down a deep document) is bounded by the data's own depth via the pair keys, so a 63-level document no longer trips the old off-by-one `47`/`48` boundary; a true `(value, schema)` cycle is detected on its first repeat instead. Forks (anyOf/oneOf/not/if) copy the stack, so sibling branches can still re-enter the same node legitimately.
- **Perf/Fix (schema, misc):** `resolveRef` uses a non-null default node instead of falling through a `failed` path for missing pointers; the standalone `valuesEqual` catches `std::bad_alloc` on compile; `getCompiled` guards against `$defs` (2019 alias for `definitions`, now a valid `$ref` target); number-out-of-range messages print the exact `int64`/double value instead of a truncated one.
- **Test:** new `testJSONSchemaReviewFixes` + the nested-ref 63-level guard test; suite is now 358 tests, all passing.
- **Version bump:** 1.25.5  ->  1.25.6

### 1.25.5 (2026-09-18)

- **Fix (web, Bencode file upload):** the web tool failed on real `.torrent` files with `"bencode: dictionary key must be a byte string"`. The frontend read ALL uploaded files as UTF-8 text via `FileReader.readAsText`, corrupting binary byte strings (invalid UTF-8  ->  U+FFFD) before they reached the parser. Bencode is now a binary-capable input format: `loadFile` reads it (and MessagePack/BSON/CBOR/Protobuf/UBJSON) as an `ArrayBuffer` and uploads base64 with the `[Binary data - base64 encoded]` preamble. The server (`server.cpp`) and CLI (`convert.cpp`) now feed the decoded bytes to `fromBencode` when the request is binary, and text when it is not.
- **Fix (web, Bencode text input):** Bencode stays a text-first format - the sample and pasted bencode text (no binary preamble) still take the text path, so round-trips like `Bencode -> Bencode` remain byte-identical.
- **Version bump:** 1.25.4  ->  1.25.5

### 1.25.4 (2026-09-18)

- **Fix (cpp, toCpp regression):** the C-mode empty-array placeholder `/* TODO: type */` would collapse to a bare name after comment stripping (`field;` - invalid C). It now emits `void* /* TODO: type */`, keeping the declaration compilable while marking the gap explicitly.
- **Version bump:** 1.25.3  ->  1.25.4

### 1.25.3 (2026-09-18)

- **Fix (cpp, fromCpp):** multiple declarators per member declaration now all parse (`int x, y;`, `std::string p, q = "hi";`, `double m = 1.5, n;`, `struct Inner { ... } a, b;`). Previously only the first declarator produced a field. A trailing empty segment (`int a, ;`) is skipped safely: `parseMember` always advances past the terminating `;`, so the body loop can never stall.
- **Fix (cpp, fromCpp):** the emitted `INT64_MIN` form `(-9223372036854775807LL - 1)` now round-trips - `cppRdScalarLiteral` recognizes the `NUMBER - NUMBER` pattern (unsigned arithmetic, two's-complement safe) in addition to the boundary literal `-9223372036854775808`.
- **Fix (cpp preprocessor, fromCpp):** `#if` evaluates real boolean expressions now - paren-less `defined X`, `defined(X)`, `!`, `&&`, `||` and parentheses; `#if defined(FOO) && 0` is correctly false and `#if 0 || defined(FOO)` true (previously only the first `defined(...)` was honored).
- **Fix (cpp, toCpp):** the empty-array placeholder is now `std::vector<std::monostate>` in C++ (with `#include <variant>`), and `void* /* TODO: type */` in C mode - compile-valid in both, clearer than the old `std::vector<std::nullptr_t>` / plain `void*`. The C spelling keeps a real type (`void*`) so the declaration stays syntactically valid after comment stripping.
- **Version bump:** 1.25.2  ->  1.25.3

### 1.25.2 (2026-09-18)
- **Fix (cpp, fromCpp):** qualified and multi-word types classify correctly: east-const (`int const a = 5`), and `signed char` / `unsigned long long` no longer lose their integer kind (they were mis-typed as strings when `signed`/`unsigned` was stripped as a leading qualifier). `std::unique_ptr<T>` / `std::shared_ptr<T>` members now melt to their pointee so `toCpp()` output round-trips.
- **Fix (cpp, fromCpp):** integer overflows and partially-prefixed literals (e.g. `18446744073709551615ull`, `08`) previously became silent 0; they now fall back to the literal's spelling as a string.
- **Fix (cpp, fromCpp lexer):** unterminated string/block-comment literals now fail with a line-anchored `cpp:` error instead of silently consuming the input. `\xNN` and `\uNNNN` escapes (incl. surrogate pairs, via U+FFFD for lone surrogates) decode in string literals.
- **Fix (cpp, fromCpp preprocessor):** conditional compilation stacks now implemented - `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif` (with pragmatic `defined(X)`/`0`/`1` evaluation) gate `#define`/`#undef` and token emission; `#undef` removes a macro; `#define X` (no replacement) registers for `#ifdef`. Hidden `#define` bodies no longer leak.
- **Fix (cpp, toCpp):** mixed arrays of objects no longer emit duplicate struct definitions - repeated `inferType` visits of the same node reuse the registered struct (`std::unordered_map<const asvJSONValue*, CppType>` cache + `CppGeneratorBusyGuard`). Also: `INT64_MIN` emits `(-9223372036854775807LL - 1)` instead of a compile-error literal; computed `NaN`/`±Infinity` emit `(0.0 / 0.0)` / `(1.0 / 0.0)` / `(-1.0 / 0.0)` instead of silent `0.0`; C style forward-declarations use `typedef struct X X;`; `uniqueName` throws rather than silently degrading after heavy collisions.
- **Fix (cpp, internals):** `findStruct` is now O(1) via an index map; the busy-guard set and per-object type cache reset per `run()`; enum signatures use length-prefixed components (no `\x1F` collision).
- **Version bump:** 1.25.1  ->  1.25.2

### 1.25.1 (2026-09-18)

- **Web fix:** removed the duplicate UBJSON entries from the "Text Formats" groups in the converter frontend selectors. UBJSON is a binary format and is (and always was) handled through base64 both ways, so it now only appears under "Binary Formats".
- **Version bump:** 1.25.0  ->  1.25.1

### 1.25.0 (2026-09-18)

- **Bencode lossy mappings (core):** `toBencode()` no longer throws on value types bencode cannot represent; BOOL -> integer 0/1, NULL -> empty byte string, DOUBLE -> decimal text byte string, DATETIME/TIMESTAMP -> integer seconds, OBJECTID/REGEX -> byte string, EXTENSION -> raw bytes, CUSTOM_TAG -> payload value (documented lossy). JSON5 (and any other format that can produce booleans/null/doubles) now converts to bencode instead of failing with `bencode: unsupported value type`.
- **Version bump:** 1.24.1  ->  1.25.0

### 1.24.1 (2026-09-18)

- **Fix (core):** added `asvJSON::get(size_t)` overloads (const and non-const) so index access on a root array works directly. Previously a call like `j.get(0)` bound to `get(std::string_view)` via the null-pointer-constant conversion `0 -> const char*` and then `strlen(nullptr)`, crashing at runtime; now the integral conversion to `size_t` wins overload resolution and returns the element (or `nullptr` when the root is not an array or the index is out of range).
- **Version bump:** 1.24.0  ->  1.24.1

### 1.24.0 (2026-09-18)

- **New format - Plist (Apple XML Property List):** added `asvJSON::toPlist()` / `asvJSON::fromPlist(std::string_view)` (and `asvJSONValue::toPlist`) - the classic XML `.plist` files. Encoder emits an XML declaration, DOCTYPE and `<plist version="1.0">` wrapper (2-space indent) with `<dict>`/`<key>`, `<array>`, `<string>`, `<integer>`, `<real>` (`.0` appended to whole doubles), `<true/>`/`<false/>`, `<date>` (ISO-8601), and `<data>` (base64) elements. Decoder skips BOM/`<?xml?>`/`<!DOCTYPE>` (bracket-aware)/comments, unescapes entities and `&#NN;`/`&#xHH;` refs, tolerates whitespace-wrapped `<data>` base64, accepts a bare value root without the `<plist>` wrapper, and rejects malformed input strictly (`plist:` errors: non-numeric/overflowed `<integer>`/`<real>`, bad `<date>`, invalid base64, unterminated/missing/unknown elements, trailing content). NULL is lossy (`<string/>`); REGEX/TIMESTAMP/OBJECTID/EXTENSION/CUSTOM_TAG degrade to their underlying representation. Auto-detection (`fromAny`) is gated before the generic XML parser on a `<plist` root; wired into `readFromFileAny`/`writeToFileAny` (`.plist`), the web converter UI, and the examples. Added `testFromPlist`, `testToPlist`, `testPlistRoundTrip`, `testPlistFromAny`.
- **Version bump:** 1.23.0  ->  1.24.0

### 1.23.0 (2026-09-18)

- **New format - JavaProp (Java `.properties`):** added `asvJSON::toJavaProp()` / `asvJSON::fromJavaProp(std::string_view)` (and `asvJSONValue::toJavaProp`) - the classic Java `.properties` files. Decoder handles `#`/`!` comment lines (with comment preservation for round-trips), `=`/`:`/first-whitespace separators, `\` line continuation (with `\\` kept literal), escapes `\\` `\t` `\n` `\r` `\f` `\uXXXX` (surrogate pairs combined, lone surrogates -> U+FFFD), unicode-escape-based keys, nullable empty values, and INI-style bare-value type inference (null/bool/int/double/datetime); duplicate keys last-wins; `#` mid-value stays data. Encoder mirrors JDK `store()` behavior (escaping `\ = : # ! space \t \n \r \f` in keys and escaping leading whitespace plus control characters in values, dropping unescaped trailing value whitespace), requires an object root, and emits nested objects/arrays as compact JSON text. Wired into `fromAny` (after the dotenv gate: `!` comment lines, `\u` escapes, `=` separators, or unspaced `:` separators), `readFromFileAny`/`writeToFileAny` (`.properties`/`.props`), the web converter UI, and the examples. Added `testFromJavaProp`, `testToJavaProp`, `testJavaPropRoundTrip`, `testJavaPropFromAny`.
- **Version bump:** 1.22.0  ->  1.23.0

### 1.22.0 (2026-09-18)

- **New format - UBJSON:** added `asvJSON::toUBJSON()` / `asvJSON::fromUBJSON(const void*, size_t)` / `asvJSON::fromUBJSON(const std::string&)` (and `asvJSONValue::toUBJSON`) - Universal Binary JSON (ubjson.org). Encoder emits big-endian `Z`/`N`/`T`/`F`/`i`/`U`/`I`/`l`/`L`/`d`/`D`/`C`/`S` markers, chooses the smallest fitting integer type, writes strings as `S <len><bytes>`, object keys as bare length-prefixed strings, simple unoptimized containers, and BINARY via the `[$U#<len><bytes>` binary-data convention. Decoder accepts unoptimized, counted (`[#<n>`) and optimized/typed (`[$<type>#<n>`, `$` without a count rejected) containers, skips `N` noops, decodes `[$U#` to BINARY, keeps `H` (high-precision) values as exact STRINGs, makes `C` a 1-char string, and enforces depth/size/truncation limits. No `fromAny` auto-detection (UBJSON shares JSON's `{`/`[` lead bytes); wired into `readFromFileAny`/`writeToFileAny` (`.ubj`/`.ubjson`), the web converter UI (binary/base64, like CBOR/MessagePack), and the examples. Added `testFromUBJSON`, `testToUBJSON`, `testUBJSONRoundTrip`.
- **Version bump:** 1.21.0  ->  1.22.0

### 1.21.0 (2026-09-18)

- **New format - Bencode:** added `asvJSON::toBencode()` / `asvJSON::fromBencode(std::string_view)` (and `asvJSONValue::toBencode`) - the BitTorrent encoding. Decoder supports integers (`i42e`), byte strings (`4:spam`), lists (`l...e`) and dictionaries (`d...e`), rejecting leading zeros, negative zero, truncation, trailing content and int64 overflow; byte strings that are not valid UTF-8 decode to BINARY. Encoder emits INT/STRING/BINARY/ARRAY/OBJECT, sorts dictionary keys as byte strings, and throws for types bencode cannot represent (bool/null/double). Wired into `fromAny` (text and gated binary detection), `readFromFileAny`/`writeToFileAny` (`.torrent`/`.bencode`/`.benc`), the web converter UI, and the examples. Added `testFromBencode`, `testToBencode`, `testBencodeRoundTrip`.
- **Version bump:** 1.20.0  ->  1.21.0

### 1.20.0 (2026-09-18)

- **New format - Dotenv (`.env`):** added `asvJSON::toDotenv()` / `asvJSON::fromDotenv(std::string_view)` (and `asvJSONValue::toDotenv`) - flat `KEY=VALUE` environment files. Decoder handles an optional `export ` prefix, `#` line/inline comments, single-quoted literal values, double-quoted values with C-style escapes that may span multiple lines, bare-value type inference (null/bool/int/double/datetime; leading-zero, hex and overflowing numeric literals stay exact strings), duplicate-key last-wins, and comment preservation (leading/trailing/footing via the shared `Comments` metadata). Encoder requires an object root and writes nested objects/arrays as compact quoted JSON text. Wired into `fromAny`, `readFromFileAny`/`writeToFileAny` (`.env`/`.dotenv`), the web converter UI, and the examples. Added `testFromDotenv`, `testToDotenv`, `testDotenvRoundTrip`, `testDotenvComments`.
- **Version bump:** 1.19.1  ->  1.20.0

### 1.19.1 (2026-09-18)

- **Fix - `fromCpp()` ignored `#define` constants:** object-like `#define NAME replacement` macros are now captured by the lexer (including multi-line `\`-continued definitions) and expanded across the token stream before parsing, so constants resolve in member initializers and array lengths (`T name[N]`, `std::array<T,N>`) instead of being emitted as identifier strings. Replacements may be numbers, string/char literals, `true`/`false`/`nullptr`, parenthesized or unary-signed literals, or other macros; function-like macros are left untouched. Scalar initializers now also accept surrounding parentheses and a leading unary sign. Added `testFromCppDefineMacros`.
- **Version bump:** 1.19.0  ->  1.19.1

### 1.19.0 (2026-09-17)

- **New - C++ declaration decoder:** added `asvJSON::fromCpp(std::string_view, const CppReadOptions& = {})`, the inverse of `toCpp()`. It lexes C++ declarations (methods, access specifiers, bitfields, templates and bases skipped), resolves named types, and materializes a value tree from default member initializers. Supports `struct`/`class`, C `typedef struct`, nested structs with a trailing declarator, `enum`/`enum class` (underlying type and explicit values), `using`/`typedef` aliases, forward references, `std::array<T,N>` / C `T[N]` / `std::vector` / `std::variant` / `std::optional` / `std::map` / `std::pair`, and comment preservation. A fixed `std::array<Enum,N>` with `N` equal to the member count recovers an enum array. Root resolution uses the last alias, or the single unreferenced struct; a root named `Document` (`rootName`) is unwrapped, others are wrapped as `{ "Name": {...} }`. `CppReadOptions` controls `unwrapRootStruct` / `materializeDefaults` / `includeComments` / `strict` / `rootName`. Wired into `readFromFileAny`/`writeToFileAny` (`.hpp`/`.h`) and the web converter (CPP input).
- **Version bump:** 1.18.0  ->  1.19.0

### 1.18.0 (2026-09-17)

- **New - C++ struct/enum generation:** added `asvJSON::toCpp(const CppWriteOptions& = {})` (and `asvJSONValue::toCpp`) - an encode-only code generator that infers C++17 `struct`/`enum` declarations from a parsed value tree. Objects become structs (nested, union-of-keys for heterogeneous object arrays); homogeneous arrays become `std::array<T,N>` (or `std::vector<T>`), mixed arrays `std::vector<std::variant<...>>`, empty arrays a `std::vector<std::nullptr_t>` placeholder; identifier-string arrays are auto-detected as enums; object-map enums are opted into via `enumPaths`/`enumNames` (RON unit variants are plain strings, so they cannot be auto-detected); a single-key root `{Name:{...}}` is unwrapped into `struct Name` (`unwrapRootSingleKey`). `CppWriteOptions` controls style (`ModernCpp` default / `C` - `typedef struct`, C arrays, plain `enum`, no member initializers), includes, defaults, comments, forward declarations, `useEnumClass`, `useStdArray`, indent width, root name and per-type overrides. Comments carried on nodes (`//` / `/* */`) are emitted as leading/trailing lines. Disable with `ASVJSON_DISABLE_CPP`.
- **Unified comment model:** all comment-capable formats (RON, JSON5, YAML, UDE, INI, ...) now share the same `Comment`/`Comments` metadata (leading / trailing / closing / footing) on `asvJSONValue`, so comments bridge uniformly between formats. A leading block comment on a later entry is never emitted inline after the previous entry's separator in minified RON/JSON5 output (which would re-bind it as that entry's trailing comment), so `toRON(false)` / `toJSON5(false)` round-trips are idempotent. Added regression tests for both.
- **Version bump:** 1.17.0  ->  1.18.0

### 1.17.0 (2026-09-16)

- **New - RON support:** Added `toRON(bool pretty = true) const` and `fromRON(std::string_view)` plus `asvJSONValue::toRON()` for the RON (Rusty Object Notation) format. The decoder converts RON to an intermediate JSON document and parses with the existing JSON parser; the encoder walks the tree directly. Supported syntax: structs `(a: 1)`, maps `{ "k": v }`, lists, tuples, named types (`Name(...)` kept as `{"Name": ...}` and re-emitted as `Name(...)` from JSON), `Some(x)`  ->  unfolds, `None`  ->  `null`, enum unit variants and variants with data (`Red`, `Type::Variant(data)`)  ->  strings/objects, bare idents  ->  strings, radix/separable/suffixed numbers (`0xFF`, `0o17`, `0b1010`, `1_000`, `42u32`, `-7i8`, `3.14f64`), floats (`.5`, `5.`, `5.e3`, `inf`/`-inf`/`NaN`), quoted strings with escapes (`\n`, `\t`, `\xHH`, `\u{...}`) and line continuation, raw strings (`r"..."`, `r#"..."#`), byte strings/chars (`b"..."`, `br#"..."#`, `b'c'`), chars (`'a'`), ranges (`3..5`, `..=7.3`, `3..`, `..`, `..=`) round-tripping as `{"$range":{"start":...,"end":...,"inclusive":...}}`, `//`/nested `/* */` comments, `#![enable(...)]` headers, and trailing commas. Special types (BINARY/EXTENSION/OBJECTID/REGEX/TIMESTAMP/DATETIME/CUSTOM_TAG/YAML sets) round-trip through their Extended JSON marker objects. `.ron` is recognized by `readFromFileAny`/`writeToFileAny` and `fromAny` (heuristics prefer RON for parenthesized input containing `:`/`,`). Disable with `ASVJSON_DISABLE_RON`.
- **New - RON comment preservation:** `fromRON()` attaches `//` and `/* */` comments to their tree nodes (leading above a key/item, same-line trailing after values, closings before `)`/`}`/`]`, plus a footer for trailing document comments), and `toRON()` re-emits them in place for idempotent RON  ->  RON round-trips. A same-line `//` after a separator (`800, // optional struct name`) binds to the value that just ended, and the encoders place the separating comma BEFORE such a trailing comment so the next entry starts fresh on its own line (RON, JSON5 and UDE all do this; no stray leading `,`). The same metadata feeds the other comment-capable text serializers: JSON5 emits it verbatim, YAML translates it to `#` lines, and UDE keeps it verbatim, so RON  ->  JSON5 / YAML / UDE conversions preserve every comment. The reverse direction also works: YAML comments (from `yamlCollectComments`) are bridged into RON comment metadata before RON / JSON5 / UDE encoding, so YAML  ->  RON / JSON5 / UDE conversions carry `# ...` comments over as `// ...`. JSON output intentionally drops comments (JSON has no comment syntax). `cloneValueImpl` and `operator==` ignore comment metadata.
- **Version bump:** 1.16.1  ->  1.17.0
- **Web - removed JSONPath `path` request field:** the optional `path` query mode (which evaluated an RFC 9535 JSONPath and returned matches as a JSON array) was never used by the web UI, so the backend handling in `web/server.cpp` (`doConvert`) and `web/convert.cpp` is removed. `asvJSON::query()` and the JSONPath library remain fully available for library users.

### 1.16.1 (2026-09-11)

- **Fix - CBOR negative-integer encoding:** Compute `-1 - n` in `uint64` to avoid signed-overflow UB at the `INT64_MIN` boundary.
- **Fix - GOON NaN/Infinity round-trip:** Non-finite doubles serialize as `NaN`/`-NaN`/`Infinity`/`-Infinity` instead of collapsing to `null`/`_` across plain values, table cells and list items.
- **Fix - TOON table rows:** Row field-count mismatch now throws instead of producing a malformed row.
- **Fix - JSON5 `\u{...}`:** Codepoint escapes longer than 6 hex digits are rejected before the accumulation can overflow.
- **Fix - MsgPack ext datetime:** Seconds are range-checked against `time_t` after signed reinterpretation so negative timestamps round-trip and out-of-range seconds are rejected.
- **Fix - XML:** Element-name sanitization uses a strict whitelist; empty arrays/objects emit `type` markers; typed scalars are preserved in mixed content and primitive arrays.
- **Fix - CSV:** JSON number-shape gate keeps malformed numbers as strings; `0x` hex literals stay text; NaN/Inf cells emit `null`; integral doubles append `.0`; flattened-column collisions and oversized cells throw.
- **Fix - INI:** Two-pass emission keeps bare lines ahead of `[[...]]` tables; empty containers round-trip as `{}`/`[]`; full double precision; out-of-range integers/doubles and trailing quoted garbage throw; leading-bracket keys and mixed/nested arrays rejected.
- **Fix - fmtDoubleVal:** `%.16g` output is verified via `strtod` round-trip and escalates to `%.17g` when it would lose the last digit, fixing 17-digit doubles on weak toolchains.
- **Fix - TRON parser:** Tokenizer rejects unterminated quoted strings and unrecognized characters instead of silently skipping them (`fromTRON()` returns `false`).
- **Fix - S-Expression parser:** Tokens that parse as numbers but are not valid JSON (leading `+`, leading zeros, `1.e5`) decode as strings instead of failing the whole document; `toSexpr()` throws on `NaN`/`Infinity` doubles instead of emitting `nil`.
- **Fix - BSON:** Duplicate keys now follow last-wins (the first value used to win); string elements whose declared length does not end in a NUL byte are rejected; serializing unsupported types (e.g. `CUSTOM_TAG`) throws instead of writing BSON null.
- **Fix - XML parser/serializer:** Unterminated comments, CDATA sections, DOCTYPE/PI constructs and attribute values are rejected; duplicate attributes throw; attribute values whose type cannot be represented (datetime, binary, regex, compound) throw instead of being silently emptied; a `CUSTOM_TAG` in mixed `#content` is rejected instead of dropped.
- **Fix - JSONPath:** `query()` stays lenient per RFC 9535 and returns empty for constructs it cannot evaluate (unterminated bracket segments, arguments passed to path-segment functions, filters with unsupported descent) instead of partially evaluating them.
- **Fix - DateTime parsing:** ISO-8601 strings with more than 3 fractional-second digits parse successfully (extra digits are consumed, milliseconds truncated) instead of failing; timezone offsets are range-validated (hours  <=  23, minutes  <=  59), so `+99:99` no longer produces a bogus DATETIME.
- **Web - JSONPath query mode:** The online converter accepts an optional `path` request field that evaluates an RFC 9535 JSONPath against any parsed input and returns the matches as a JSON array.
- **Version bump:** 1.16.0  ->  1.16.1

### 1.16.0 (2026-08-30)

- **New - Custom tag dispatch hook:** Added `asvJSON::customTagTransform`, an optional user callback invoked at parse time whenever a custom-tag annotation (`{"$customTag":{"name":...,"value":...}}`) is recognized. It applies uniformly across YAML, GOON, UDE, and plain JSON (all funnel through the JSON object parser). The callback receives the resolved tag name and the tag's value and returns a replacement `std::unique_ptr<asvJSONValue>` (or `nullptr` to keep the default `CUSTOM_TAG` node). This closes the last remaining YAML/GOON known-gap item.
- **Version bump:** 1.15.0 -> 1.16.0

### 1.15.0 (2026-08-26)

- **New - JSON Schema validation:** Added `validate()` method and `SchemaValidator` class supporting draft-07 keywords: `type`, `const`, `enum`, `allOf`/`anyOf`/`oneOf`/`not`, `if`/`then`/`else`, `$ref` (local with JSON Pointer decoding), string (`minLength`/`maxLength`/`pattern`), number (`minimum`/`maximum`/`exclusiveMinimum`/`exclusiveMaximum`/`multipleOf`), array (`minItems`/`maxItems`/`uniqueItems`/`items`/`contains`), object (`required`/`properties`/`patternProperties`/`additionalProperties`/`minProperties`/`maxProperties`/`propertyNames`/`dependentRequired`/`dependencies`). Disable with `ASVJSON_DISABLE_SCHEMA`.
- **New - JSONPath (RFC 9535):** Added `query()`, `queryFirst()`, `isValidPath()` methods. Supports `$`, `.member`, `['member']`, `[index]`, `[start:end:step]` slices, `..member` recursive descent, `[*]` wildcard, `?(filter)` expressions with comparison/logic operators, and functions (`length`, `count`, `min`, `max`, `sum`, `avg`, `match`, `search`, `value`). Disable with `ASVJSON_DISABLE_JSONPATH`.
- **New - JSON SAX/Streaming parser:** Added `parseSax()` method with `JsonSaxHandler` (callback-based), `JsonSaxParser` (full-input), `JsonStreamParser` (chunked), and `JsonSaxToDom` adapter. Supports comments. Disable with `ASVJSON_DISABLE_SAX`.
- **New - Auto-format file I/O:** Added `readFromFileAny(filename)` and `writeToFileAny(filename, pretty)` with heuristic format detection from content/file extension, supporting `.gz`/`.bz2`/`.xz` suffix stripping.
- **New - Format detection:** Added `FormatType` enum, `getFormatType()` accessor, and `fromAny(string_view)` method with heuristic content-based format detection across all 19 formats.
- **New - Pretty-print for YAML/TOML/INI:** `toYAML(pretty)`, `toTOML(pretty)`, `toINI(pretty)` now accept a `bool pretty` parameter for indented output.
- **New - `operator[]` chaining:** Added `JsonProxy` for chained navigation (`json["a"]["b"]["c"]`) on both objects and arrays.
- **New - Iterator API:** Added `begin()`/`end()` on `asvJSONValue` (iterates object keys or array elements), `asvJSON`, and `JsonProxy`. Added `values()` range helper for arrays.
- **New - Emplace/push/equality/merge/diff:** Added `emplace(key, val)` and `push(val)` on `asvJSONValue` for constructing values in-place; `operator==`/`operator!=` for deep structural equality; `merge(other)` for RFC 7396 deep merge with null-deletion; `diff(other)` returning JSON Patch array.
- **Fix - `toBSON()` consistency:** Changed return type from `std::string` to `std::vector<uint8_t>` to match `toCBOR()`/`toMessagePack()`.
- **Fix - UB across formats (strtod/strtoll on string_view):** Replaced all `std::strtod(s.data(), ...)` and `std::strtoll(val.data(), ...)` calls on `string_view` parameters with local `std::string` copies in `yaml.hpp`, `xml.hpp`, `csv.hpp`, `jsonsax.hpp` (both parsers).
- **Fix - JSONPath thread_local aliasing:** Replaced `static thread_local asvJSONValue` with owned `unique_ptr` via `hold()` method and `PathResult::owned` to fix incorrect filter comparisons when both sides are literals.
- **Fix - JSONPath OOB slice:** Added bounds checking in `jpSlice` for `step > 0` (previously could read past array end for large slice bounds).
- **Fix - Schema regex pre-compilation:** `patternProperties` regexes now compiled once before the object property loop instead of O(N*M) times.
- **Fix - Schema $ref JSON Pointer decoding:** `$ref` path segments now decode `~0`  ->  `~` and `~1`  ->  `/` per RFC 6901.
- **Fix - Unicode cleanup:** Replaced em dashes and non-ASCII characters with ASCII equivalents in source comments.
- **Version bump:** 1.14.0  ->  1.15.0

### 1.14.0 (2026-08-20)

- **UDE format additions:** Custom tags with canonical name/value split, namespaces (`!ns:tag`) and verbatim (`!<uri>`), Extended JSON representation, YAML-style `---`/`...` document separators, versioned header, merge keys (`<<: *default`), digit separators (`1_000_000`), lossless big-integer literals, `!datetime` with timezone offsets, non-finite float literals (`.nan`/`/.inf`), type-coercion tags (`!int`/`!float`/`!string`/`!bool`/`!null`), `!schema`, bare `!null`, folded block scalar (`>`), single-quoted strings, full string escapes.
- **UDE spec conformance:** Documented resource limits (alias depth 100, anchors 1M), YAML-style tab prohibition in block scalar indentation.
- **Parser hardening:** Fixed OOB, UB and malformed input across YAML, S-Expression, TOML, XML, Base64, BSON, CSV, JSON5. Integer overflow detection, binary parser bounds checks, `cloneValue` node count limit (100M), YAML tag map RAII safety, `splitLines` phantom EOF removal.
- **Version bump:** 1.13.0  ->  1.14.0

### 1.13.0 (2026-08-04)

- **New format - UDE:** Added `toUDE()` / `fromUDE()`  -  Unified Data Exchange with a `# UDE` header, multiple documents (`// --- End of Document ---`), `#`/`//`/`/* */` comments, anchors/aliases, block scalars `|`/`>`, and type tags (`!base64`, `!bin`, `!datetime`, `!ext`, `!regex`); strict mode rejects duplicate keys and special characters in bare tokens.
- **UDE spec conformance:** default chomping now clips (keeps one trailing newline) instead of stripping; block scalars handle a blank first line, and empty lines are preserved as paragraph breaks (they never terminate the scalar by themselves, Section 6) with a single trailing line break captured; the serializer falls back to quoted strings whenever a value cannot round-trip as a block scalar; quoted dotted keys (`"a.b.c"`) are literal while unquoted ones nest; document separators match exactly (no accidental splits on comments); anchors store deep copies (no dangling pointers) with cycle detection; strict mode requires quoted keys; a leading `+` is no longer accepted as a numeric sign.
- **Version bump:** 1.12.0 -> 1.13.0

### 1.12.0 (2026-07-20)

- **New format - INI:** Added `toINI()` / `fromINI()`  -  classic initialization file format with sections, key-value pairs, `;`/`#` comments, quoted values, escape sequences, line continuation, and dot-notation section nesting for hierarchical data (e.g. `[network.server]`  ->  nested JSON `{"network":{"server":{...}}}`).
- **Version bump:** 1.11.1 -> 1.12.0

### 1.11.1 (2026-07-20)

- **JSON5 spec conformance:** Full compliance with JSON5 specification (https://spec.json5.org/). Added `\u{...}` code point escapes (BMP  ->  `\uHHHH`, supplementary  ->  surrogate pairs), ES5 escape sequences (`\v`  ->  `\u000B`, `\0`  ->  `\u0000`, `\xHH`  ->  `\u00HH`), invalid escape validation (octal, malformed `\x`/`\u`). Control characters outside strings now correctly rejected; raw control chars in strings escaped. Extended whitespace (U+2000 - U+200A, U+205F, U+3000 Zs spaces; U+FEFF BOM) in all whitespace-skip positions. Unicode identifier keys with locale-independent `isIdStart`/`isIdCont`. `-NaN` literal preserved as `-NaN`. `\'` in double-quoted strings and `\"` in single-quoted strings handled correctly.
- **Version bump:** 1.11.0 -> 1.11.1

### 1.11.0 (2026-07-20)

- **New format - JSON5:** Added `toJSON5()` / `fromJSON5()`  -  JSON superset with unquoted keys, single-quoted strings, trailing commas, hex/octal/binary numbers, plus sign, leading decimal, comments. Uses MongoDB Extended JSON (`$oid`, `$regex`+`$options`, `$timestamp`, `$binary` with subType) for special data types (OBJECTID, REGEX, TIMESTAMP, BINARY, EXTENSION), replacing legacy `__OID__`/`__REGEX__`/`__BASE64__`/`__EXT__` prefix convention.
- **Extended JSON parser:** JSON parser now detects `$oid`, `$regex`+`$options`, `$timestamp`, `$binary` objects and converts them to native special types. Legacy custom prefix strings (`__BASE64__`, `__OID__`, `__REGEX__`, `__EXT__`) are no longer recognized.
- **YAML decoder updated:** YAML `!objectid`/`!regex`/`!!binary`/`!ext` tags now emit Extended JSON format instead of legacy prefixes, ensuring full round-trip fidelity.
- **NaN/Infinity in JSON5:** `NaN`, `Infinity`, `-Infinity` literals supported (enabled automatically in `fromJSON5`).
- **Version bump:** 1.10.0 -> 1.11.0

### 1.10.0 (2026-07-20)

- **New format - S-Expression:** Added `toSexpr()` / `fromSexpr()` - Lisp-style nested list format with heuristic object/array detection, `;` comments, `nil`/`#t`/`#f` literals, C-style escape sequences, and full round-trip support.
- **Version bump:** 1.9.0 -> 1.10.0

### 1.9.0 (2026-07-19)

- **New format - JSON Lines (NDJSON):** Added `toJSONLines()` / `fromJSONLines()` - one JSON value per line, for streaming/logs/bulk data.
- **Version bump:** 1.8.1 -> 1.9.0

### 1.8.1 (2026-07-19)

- **Bugfix - TOML inline table dotted keys:** Fixed loss of data when inline table keys share a dotted prefix (e.g., `{x.y = 1, x.z = 2}` now produces `{"x":{"y":1,"z":2}}` instead of duplicate-key JSON).
- **Bugfix - TOML inline table type conflict:** Added error on scalar-to-table redefinition (e.g., `{a = 1, a.b = 2}` now throws instead of segfaulting).
- **Bugfix - TOML depth guard:** Fixed variable shadowing that disabled the `MAX_TOML_DEPTH` recursion guard in inline table and inline array parsers.

### 1.8.0 (2026-07-19)

- **New format - TOML:** Added `toTOML()` / `fromTOML()` - Tom's Obvious Minimal Language with tables, arrays of tables, inline tables/arrays, multi-line strings (basic and literal), hex/octal/binary integers, dotted keys, quoted keys, literal strings, comments. Direct tree-building decoder for robustness.
- **Version bump:** 1.7.0 -> 1.8.0

### 1.7.0 (2026-07-19)

- **New format - Protobuf:** Added `toProtobuf()` / `fromProtobuf()` - Protocol Buffers binary wire format with schema-driven field mapping, varint encoding, packed fixed-size arrays. Text format via `toProtobufText()` / `fromProtobufText()`. Static helpers `protobufFromString()` / `stringFromProtobuf()`.
- **New - XML decoder:** Added `fromXML()` - full XML parser supporting elements, attributes (`@`-prefix), text content (`#text`), CDATA, comments, processing instructions, type detection (`type="datetime|binary|objectid|regex|timestamp|extension"` attributes), child grouping into arrays for repeated names, self-closing tags.
- **YAML decoder rewritten:** Major improvements:
  - Tag support: `!!int` (hex/octal/binary), `!!float` (`.nan`/`.inf`), `!!bool`/`!!null`/`!!str`/`!!timestamp`, `!!set` (flow), `!!omap`, `!!pairs`
  - Flow anchors & aliases (`&anchor`/`*alias` inside `{}`/`[]`)
  - `%TAG` / `%YAML` directive parsing with tag handle resolution
  - Line-number-anchored error diagnostics for parse errors
  - NaN/Infinity encoding fix: `.nan`, `.inf`, `-.inf` (was emitting `~`)
- **New tests:** 655+ lines covering Protobuf, XML, YAML (tags, anchors/aliases, multi-doc, directives, edge cases).
- **Version bump:** 1.6.0 -> 1.7.0

### 1.6.0 (2026-07-18)

- **New format - CBOR:** Added `toCBOR()` / `fromCBOR()` - RFC 8949 Concise Binary Object Representation with indefinite-length items, half-precision floats, datetime/extension/regex tags. Static helper `cborFromString()`.
- **Modular structure:** Refactored monolithic header into `asvjson/` module directory  -  `core.hpp`, `detail/*.hpp`, `formats/*.hpp`. The top-level `asvJSON++.hpp` remains as a backward-compat wrapper.
- **Bugfix  -  Base64 custom charset:** `decodeBase64Fast` now uses `getDecodeTable()`, fixing round-trip when a custom charset is set via `setBase64Chars()`.
- **Bugfix  -  Namespace pollution:** Global symbols (`FormatFrame`, `splitLines`, `countIndent`, `stripIndent`, `closeFrames`, `addComma`) moved into `asvJSONInternal` namespace.
- **Bugfix  -  Strict literal parsing:** Reject non-standard identifiers after `true`/`false`/`null`/`NaN`/`Infinity` (e.g., `trueX`, `false123`).
- **Fix  -  XML output:** Added `<?xml version="1.0" encoding="UTF-8"?>` declaration and `type="..."` attributes for special types.
- **Fix  -  YAML output:** Added `---` document separator, proper tags (`!!binary`, `!objectid`, `!regex`, `!ext`), and multiline `|` block scalar for strings with newlines.
- **Fix  -  TOON/GOON frame management:** Corrected `addComma` `!first` check, root frame `hasVal`, and sub-frame push with `first=true`.
- **Fix  -  BSON test:** Corrected element type byte order (type before key, per BSON spec).

### 1.4.0 (2026-07-16)

- **New format  -  CSV parser:** Added `fromCSV()`  -  RFC 4180 CSV parser with typed value detection and multi-line quoted field support.
- **Error handling unification:** All parsers (JSON, MessagePack, BSON, TOON, TRON, GOON) now throw `asvJSONError` uniformly, with top-level try/catch in public methods.
- **TRON key order fix:** Sorted keys in `tronDiscoverSchemas` for deterministic output across MSVC and GCC.
- **Boundary-check refactor:** Extracted `checkStringLen`, `checkArraySize`, `checkObjectSize`, `checkNestingDepth` helpers to eliminate 32 duplicated limit checks.

### 1.3.0 (2026-07-16)

- **New format  -  GOON:** Greatly Optimized Object Notation  -  indentation-based nesting, tabular arrays, single-char literals (`T`/`F`/`_`/`~`), dictionary references, RLE (`*N`) for tabular rows, column reference (`^`) support.
- **New format  -  TRON:** Token-Reduced Object Notation  -  class definitions for repeated structures, class instantiation, inheritance, named arguments, full round-trip.
- **New format  -  TOON:** Token-Oriented Object Notation with inline and tabular array formats, full round-trip.
- **NaN/Infinity support:** `allowNaNInfinity` flag enables parsing and serialization of non-standard JSON values.
- **MSVC fixes:** Resolved multiple-definition linker errors (`inline` on out-of-line methods), signedness mismatch, removed non-const `getRootArray()`.

### 1.0.0  -  1.2.0

Initial releases with JSON, BSON, MessagePack, XML, YAML, CSV, JSON Pointer, JSON Patch, JSON Merge Patch support.

### Important Lifetime Notes

- `std::string_view` returned by `getStringView()` or `getObjectIdView()` points to internal buffers. It becomes invalid after the next call to `parse()` or after the `asvJSON` object is destroyed. **Do not store these views long-term.**
- Raw pointers returned by `asvJSON::get()` and `asvJSONValue::get()` are owned by the library. **Do not delete them.**
- `setByPointer()` **takes ownership** of the passed `asvJSONValue*`. Do not delete it after a successful call; the library will destroy it automatically.
- `put*` methods copy the input values (strings are duplicated). No extra memory management is required from the caller.
