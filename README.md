# asvJSON++

A C++17 JSON library supporting binary data, DateTime, Base64, BSON, MessagePack, CBOR, JSON Pointer, JSON Merge Patch, XML, YAML, CSV, TOON, TRON, GOON, ADE, Protobuf, TOML, JSON Lines (NDJSON), S-Expressions, SVG, JSON Schema validation, JSONPath (RFC 9535), JSON SAX/streaming parsing, and C++ struct/enum code generation.

**Author:** Sergey Andyk  asvzzz@narod.ru

## Online Converter

Try it online: **https://jx8.ru/**  -  browser-based converter supporting all 26 formats, with file upload/download.

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
- XML - `toXML()` / `fromXML()` - serialization and parsing with attributes (`@`-prefix), text content (`#text`), CDATA, comments, type detection (datetime/binary/objectid/regex/timestamp/extension); malformed input (unterminated comments/CDATAs/attribute values) and duplicate attributes are rejected, and attribute/`#content` types that cannot be represented throw instead of being silently altered. A C0 control byte (other than tab/LF/CR) has no legal XML 1.0 spelling, so the encoder reports it with an `asvJSONError` instead of emitting markup that no conforming parser accepts (the decoder stays lenient and reads such a byte back)
- CSV - `toCSV()` with recursive flattening (`a.b.c`) and first-appearance column order, two-pass union of keys for arrays of objects, `"` escaping per RFC 4180, formula-injection guard on bare cells
- TOON - `toTOON()` / `fromTOON()` - token-oriented object notation with inline and tabular array formats, full round-trip serialization and parsing
- TRON - `toTRON()` / `fromTRON()` - token-reduced object notation with class definitions for repeated structures, class instantiation, inheritance, named arguments, round-trip support
- GOON - `toGOON()` / `fromGOON()` - greatly optimized object notation with YAML-like indentation, tabular arrays, inline/inference-based lists, single-char literals (T/F/_/~), dictionary references, round-trip support
- Protobuf - `toProtobuf()` / `fromProtobuf()` - Protocol Buffers binary wire format with schema-driven field mapping, packed fixed-size arrays; plus `toProtobufText()` / `fromProtobufText()` for human-readable text format
- TOML - `toTOML()` / `fromTOML()` - Tom's Obvious Minimal Language with tables, arrays of tables, inline tables/arrays, multi-line strings, hex/octal/binary integers, dotted keys, comments
- JSON Lines (NDJSON) - `toJSONLines()` / `fromJSONLines()` - one JSON value per line, ideal for streaming/ logs/bulk data
- S-Expression - `toSexpr()` / `fromSexpr()` - Lisp-style nested lists `(key "value" (nested 1 2 3))` with heuristic object/array detection, `;` comments, `nil`/`#t`/`#f` literals
- JSON5 - `toJSON5()` / `fromJSON5()` - JSON superset with unquoted keys, single-quoted strings, trailing commas, hex/octal/binary numbers, leading decimal, plus sign, comments, NaN/Infinity literals, Unicode identifiers, `\u{...}` code point escapes, extended whitespace (Zs), ES5 escape sequences (`\v`, `\0`, `\xHH`), control character validation, and MongoDB Extended JSON for special types
- INI - `toINI()` / `fromINI()` - classic initialization file format with sections, key-value pairs, `;`/`#` comments, quoted values, escape sequences, line continuation, and dot-notation section nesting
- Dotenv - `toDotenv()` / `fromDotenv()` - flat `KEY=VALUE` environment files with an optional `export` prefix, `#` comments (line and inline), single-quoted literal values, double-quoted values with C-style escapes (may span lines), unknown escapes preserved verbatim, and bare-value type inference (numbers/bools/null like INI, ISO-8601 dates as DATETIME, which is written back as bare ISO text and round-trips losslessly); nested objects/arrays and BINARY/EXTENSION have no dotenv syntax and are written as their JSON/base64 text (quoted only when a bare value would re-read as a different type), decoding back as STRING
- UBJSON - `toUBJSON()` / `fromUBJSON()` - Universal Binary JSON (ubjson.org), big-endian markers for int8/uint8/int16/int32/int64, float32/float64, strings (`S`), null/bool, homogeneous typed containers (counted and optimized `$`-type with explicit counts), and the standard binary blob convention (`[$U#...` decodes to BINARY); object keys are written as bare length-prefixed strings; no magic header so `fromAny()` does not auto-detect it (use `fromUBJSON()` or the `.ubj` extension)
- Bencode - `toBencode()` / `fromBencode()` - the BitTorrent encoding (integers, byte strings, lists, sorted-key dictionaries); byte strings decode to STRING when valid UTF-8 and to BINARY otherwise; types with no bencode representation map lossily on encode (BOOL -> 0/1, NULL -> empty string, DOUBLE -> decimal string, DATETIME/TIMESTAMP -> seconds)
- JavaProp - `toJavaProp()` / `fromJavaProp()` - the classic Java `.properties` files: `#`/`!` comment lines, `=`/`:`/whitespace separators, `\` line continuation, `\uXXXX` escapes (incl. surrogate pairs), and INI-style bare-value type inference
- Plist - `toPlist()` / `fromPlist()` - the classic Apple XML property list (`<plist>`/`<dict>`/`<array>` wrapper, `<string>`/`<integer>`/`<real>`/`<true/>`/`<false/>`/`<date>`/`<data>` values, entities, comments); a C0 control byte (other than tab/LF/CR) has no legal XML 1.0 spelling and is reported with an `asvJSONError` instead of being written raw
- ADE - `toADE()` / `fromADE()` - Advanced Data Exchange with a `# ADE` header, multiple documents (`// --- End of Document ---`), `#`/`//`/`/* */` comments, anchors/aliases, block scalars `|`/`>`, and type tags (`!int`, `!float`, `!string`, `!bool`, `!null`, `!schema`, `!base64`, `!bin`, `!datetime`, `!ext`, `!regex`); explicit tags preserve type info (a `!float` integral value round-trips as a float), digit separators (`1_000_000`, `0xFF_FF`), YAML-style non-finite floats (`.nan`, `.inf`, `-.inf`), big integers stored as exact raw literals, and `!datetime` with timezone offsets; strict mode rejects duplicate keys and special characters in bare tokens. Full specification: [ADE.md](ADE.md)
- RON - `toRON()` / `fromRON()` - Rusty Object Notation (the `ron` crate format) with structs, tuples, maps, named types, `Some`/`None` optionals, enum unit variants and variants with data (`Type::Variant(data)`), lists, bare idents, radix/separable/suffixed numbers (`0xFF`, `1_000`, `42u32`), floats (`.5`, `5.`, `5.e3`, `inf`, `-inf`, `NaN`), quoted strings with escapes and line continuation, raw strings (`r"..."`/`r#"..."#`), byte strings/chars (`b"..."`, `b'c'`), chars (`'a'`), ranges (`3..5`, `..=7`, `3..`, `..`, `..=`), `//`/nested `/* */` comments, `#![enable(...)]` headers, and trailing commas. `None` maps to `null`, `Some(x)` unfolds to `x`, empty `()` maps to an empty object; unit variants and bare idents become strings; ranges round-trip as `{"$range":{"start":...,"end":...,"inclusive":...}}` objects; special types (binary/objectid/regex/timestamp/extension/datetime/custom tags/YAML sets) round-trip through their Extended JSON marker objects
- C++ codegen - `toCpp()` / `fromCpp()` - generate C++17 `struct`/`enum` declarations from a parsed value tree, and parse C++ declarations back into a value tree: nested structs per object, `std::array<T,N>` (or `std::vector<T>`) for homogeneous arrays, `std::vector<std::variant<...>>` for mixed arrays, and enums inferred from identifier-string arrays (object-map enums via `enumPaths`/`enumNames`); a C-style output mode emits `typedef struct` / C arrays / `enum`; comments carry over as `//` / `/* */`
- SVG - `toSVG()` / `fromSVG()` - Scalable Vector Graphics, XML-based vector graphics format; requires XML support (`ASVJSON_DISABLE_XML` is not compatible with SVG — attempting to build with XML disabled but SVG enabled emits a compile-time error). Produces well-formed `<svg>` documents with proper namespacing (object keys become elements, `@`-prefixed keys become attributes, `#text` becomes text content, and arrays become repeated same-named sibling elements); a C0 control byte (other than tab/LF/CR) has no legal XML 1.0 spelling and is reported with an `asvJSONError` instead of being written raw; decodes through the shared XML parser with the same-named-children collapse disabled, so a multi-element array key such as `{"defs":{"clipPath":[...]}}` survives a JSON  ->  SVG  ->  JSON round-trip (plain XML folds such children into a bare array and would drop the key)

#### Format Specifications

| Category | Format | Specification |
| --- | --- | --- |
| Industrial standards | JSON | [json.org](https://www.json.org/), [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259) |
| Industrial standards | BSON | [bsonspec.org](https://bsonspec.org/) |
| Industrial standards | MessagePack | [msgpack.org](https://msgpack.org/) |
| Industrial standards | CBOR | [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949) |
| Industrial standards | XML | [W3C XML](https://www.w3.org/TR/xml/) |
| Industrial standards | YAML | [YAML 1.2 spec](https://yaml.org/spec/1.2.2/) |
| Industrial standards | CSV | [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180) |
| Industrial standards | TOML | [toml.io](https://toml.io/en/) |
| Industrial standards | Protobuf | [protobuf.dev](https://protobuf.dev/) |
| Binary formats | UBJSON | [ubjson.org](https://ubjson.org/) |
| Binary formats | Bencode | [Wikipedia: Bencode](https://en.wikipedia.org/wiki/Bencode) |
| Binary formats | Plist | [Apple NSKeyedArchiver / .plist](https://developer.apple.com/documentation/foundation/nskeyedarchiver) |
| Human-readable / LLM-friendly | JSON5 | [spec.json5.org](https://spec.json5.org/) |
| Human-readable / LLM-friendly | INI | [Wikipedia: Configuration file](https://en.wikipedia.org/wiki/Configuration_file) |
| Human-readable / LLM-friendly | Dotenv | [dotenvx.com](https://dotenvx.com/) |
| Human-readable / LLM-friendly | JavaProp | [Java Properties](https://docs.oracle.com/javase/8/docs/api/java/util/Properties.html) |
| Human-readable / LLM-friendly | RON | [ron-rs/ron](https://github.com/ron-rs/ron) |
| Human-readable / LLM-friendly | S-Expression | [Wikipedia: S-expression](https://en.wikipedia.org/wiki/S-expression) |
| Human-readable / LLM-friendly | JSON Lines (NDJSON) | [ndjson.org](https://ndjson.org/) |
| Human-readable / LLM-friendly | ADE | [github.com/asvzzz/ADE](https://github.com/asvzzz/ADE) |
| Config / env formats | TOON | [github.com/toon-format/spec](https://github.com/toon-format/spec) |
| Config / env formats | TRON | [tron-format.github.io](https://tron-format.github.io/) |
| Config / env formats | GOON | [GOON-format/SPEC.md](https://github.com/GOON-format/goon/blob/main/SPEC.md) |
| Platform / copyright formats | C++ codegen | Internal (asvJSON++) |
| Graphics / markup formats | SVG | [W3C SVG](https://www.w3.org/TR/SVG2/) |

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

`format` is accepted but never asserted -- draft-07 makes it a pure annotation. `$ref` is local only (RFC 6901 JSON Pointer into the root schema).

Behavior worth knowing:

- **Custom types and `type`.** Each custom type is matched by the JSON shape it serializes to, so `{"type":"string"}` accepts a `DATETIME` and `{"type":"object"}` accepts an `OBJECTID`, `REGEX`, `CUSTOM_TAG`, `EXTENSION` or `TIMESTAMP` (all of which emit extended-JSON objects). Both spellings are accepted where both are plausible -- `OBJECTID`/`REGEX`/`CUSTOM_TAG` pass as `string` or `object`, `TIMESTAMP` as `integer`/`number`/`object`, `BINARY` as `string` or `object` -- so no schema that passed before stops passing. Plain JSON parses those extended forms back as ordinary objects, which is why the distinction only shows on decoded or programmatically built nodes.
- **Malformed schemas fail loudly.** A keyword value of the wrong shape is rejected (`enum` not an array, a non-numeric `minimum`, a non-string `pattern`/`format`, a `type`/`required` entry that is not a string). This includes inside `anyOf`/`oneOf`/`not`/`if`/`contains`: those keywords read a branch failure as "the instance did not match", so a broken branch is now reported instead of being counted as a mismatch that flips the verdict. Only subschemas the instance actually reaches are checked -- an unused `properties` entry is not.
- **Degenerate keyword shapes are rejected**, in the spirit of the empty-`enum` rule: `enum: []` and `type: []` (both match nothing), an empty `allOf`/`anyOf`/`oneOf` (an empty `allOf` matches everything and an empty `anyOf`/`oneOf` matches nothing, so both are degenerate), duplicate `type`/`required` entries (draft-07 says these arrays SHOULD be unique), and the draft-04 boolean `exclusiveMinimum`/`exclusiveMaximum` without the `minimum`/`maximum` it modifies. An empty `required` stays a legal no-op.
- **Never throws.** `validate()` converts an escaping `std::exception` -- an allocation failure, or `std::regex_error` from a `pattern` whose *match* runs out of stack (the pattern itself compiled fine) -- into `false` with a `Schema validation failed: ...` message.

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

Supports: `$`, `.member`, `.'quoted member'`, `['member']`, `[index]`, `[start:end:step]` slices, `..` recursive descent, `[*]` wildcard, `?(filter)` with `<`, `>`, `<=`, `>=`, `==`, `!=`, `&&`, `||`, `!` operators, and functions: `length()`, `count()`, `min()`, `max()`, `sum()`, `avg()`, `match()`, `search()`, `value()`.

The member-name shorthand also accepts a quoted name, as RFC 9535 2.5.2.1 allows: `$.'a b'` and `$.''` select a member exactly like `$['a b']` and `$['']` do (including the descendant form `$..'a b'` and the same shorthand inside a filter path).

`query()` is inherently lenient (RFC 9535): a selector it cannot evaluate (e.g. an unterminated bracket, arguments passed to a function, or a filter expression containing unsupported constructs) never throws  -  it contributes no matches, so a malformed path yields an empty result rather than a partial one. Function arguments are only allowed on the parenthesized right-hand side of filter operators (e.g. `[?(@.price ..)]`).

Disable with `#define ASVJSON_DISABLE_JSONPATH`.

### JSON SAX / Streaming Parser

```cpp
// Full-input SAX parsing
struct Handler : public JsonSaxHandler {
  std::string error;  // handlers keep the message they care about
  void onKey(std::string_view key) override { ... }
  void onStringValue(std::string_view value) override { ... }
  void onStartArray() override { ... }
  void onError(std::string_view message) override { error = message; }
  // ... other callbacks
};

asvJSON doc;
Handler h;
JsonSaxParser parser;
if (!parser.parse(jsonString, h)) {
  std::cerr << parser.getLastError();  // same text the handler received
}

// Chunked streaming (for large files)
JsonStreamParser stream(h);       // the handler is bound in the constructor
while (auto chunk = readNextChunk()) {
  if (!stream.feed(chunk)) break;
}
stream.finish();
```

Disable with `#define ASVJSON_DISABLE_SAX`.

**Error reporting.** `JsonSaxParser::parse()` and `JsonStreamParser::feed()`/`finish()` never throw for malformed input; they report through `onError` and return `false`. `onError` must not throw -- it is a notification point, not control flow. The stream parser routes every error through a helper that catches anything the callback throws, so a misbehaving handler still gets `false` back; the single-pass parser delivers errors at one catch point plus a few unwrapped early-return paths, where a throwing handler escapes `parse()`.

`-NaN` keeps its sign in both SAX parsers and in the DOM parser, so a handler that re-serializes the value (or inspects `std::signbit`) sees what the input said. It makes no difference to comparisons -- `NaN != NaN` either way.

An input with no value in it -- empty, whitespace-only, comment-only, or nothing but a UTF-8 BOM -- is rejected as `empty input` rather than as an unexpected character. Inside a container, an input that ends where a value was expected is `Unexpected end of input`.

### Special Data Types
- Binary data (automatic Base64 in JSON, raw in BSON/MessagePack)
- DateTime (milliseconds precision)
- ObjectId (12-byte identifier) - serialized as MessagePack ext type 1
- Regex (pattern + options) - serialized as MessagePack ext type 2
- Timestamp (int64) - serialized as MessagePack ext type 3
- Custom tags (name + annotated value) - serialized as MessagePack ext type 4 (`[name-string, value]`), mirroring the CBOR tag-258 convention
- Extension types (MessagePack ext, configurable extType) - the type bytes `1`/`2`/`3`/`4`/`-1` are **reserved** for the special types above: the encoder writes an `EXTENSION` node verbatim, so a hand-built `EXTENSION(1, 12 bytes)` decodes back as ObjectId, and such a node has no round-trip guarantee (use any other type byte). A well-known ext body that cannot form its special value (ObjectId != 12 bytes, Timestamp != 8 bytes, an empty Regex body or empty pattern) decodes to a plain `EXTENSION` keeping the type byte and the raw body, never to null. A MessagePack `uint64` above `INT64_MAX` is rejected, not preserved (asvJSON `INT` is signed 64-bit, and a `raw_number` string would re-encode as a string). The parser enforces the [Limits](#limits) above on untrusted input; the encoder does not re-check them.

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
| `std::string toADE(bool strict = false) const` | Serialize to ADE (Advanced Data Exchange). |
| `bool fromADE(std::string_view input, bool strict = false)` | Parse ADE string (header, multi-doc, anchors, tags, block scalars). |
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
| `std::string toJavaProp() const` | Serialize to Java `.properties` (bug-compatible with the JDK: unescaped trailing whitespace is dropped on load, leading whitespace escaped, and a value's final space is escaped so it survives). |
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

YAML parsing details:
- **Multi-line plain scalars** - block-context plain scalars fold across continuation lines (one line break becomes a space; each blank line becomes `\n`), both after a key (`k: line1` + deeper `line2`) and as deeper values of an empty key (`k:` + deeper lines), including sequence items; a continuation that looks like a nested key/flow value ends the fold.
- **Block scalar whitespace** - tabs at/after the content indent are data (never indentation); a whitespace-only line is blank at any depth; excess leading spaces past the content indent are preserved; a tab used as indentation (before the content indent) is still rejected.
- **`---` markers carry content** - a `---` marker may hold the first document's content on its own line (`--- a: 1`); `---abc:` is ordinary content, not a marker.
- **Signed non-finite floats** - `+.inf`, `-.inf`, `+.nan`, `+Infinity`, `-Infinity` parse as doubles; unrecognized spellings (`inf`, `+Inf`) stay strings.
- **Block-form `!!omap` / `!!pairs`** - accepted in block style; applying the tag to a non-sequence block value is an error (was a silent empty array).
- **Self-referential anchors** - `&x [*x]` and aliases pointing at an anchor that recursively contains itself are reported as an error (guard against infinite pre-scan expansion), while plain aliases keep working.
- **Escapes** - `\uXXXX` combines surrogate pairs into the astral code point; an unpaired surrogate becomes U+FFFD (it has no UTF-8 form), and an unknown escape such as `\uZZZZ` is kept verbatim.
- **REGEX pipes** - the `!regex "<pattern>|<options>"` tag separates the pattern from the options with a `|`; a literal `|` inside the pattern is written as `\u007C` (the same convention TRON and XML use), so the separator stays unambiguous and a pattern like `a|b` round-trips exactly. Documents written before that convention keep their old reading (the last `|` of the decoded text is the separator).
- **Comments** - a comment may follow a single-line flow collection (`{a: 1} # c`, also as a mapping value or a `!!set` value), and comment paths are length-prefixed, so a key holding any byte (e.g. U+001F) keeps its comment. At most one trailing comment is re-emitted, inline, and only when it is a single-line one; a multi-line block trailing comment has no inline form and is dropped. Errors inside an anchored or embedded block report a line number relative to that block.
- **Multi-line strings with `#`** - a `'#'`-leading line is content in a block scalar (the `'#'` only starts a comment below the content indent), so such strings use `|` instead of being quoted.
- **Root scalars** - a document that is a plain or quoted scalar, a multi-line folded plain scalar, or a root block scalar (`|`/`>`) decodes as that scalar. The encoder emits a root scalar bare (no `'': value` wrapper in either style) and quotes the lone `-`/`?` indicators and the `---`/`...` markers so string data cannot turn into a sequence or document boundary.

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

Comments are emitted only in `Block` style; `Auto` and `Flow` output is comment-free.

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
- **Tags**: datetime (tag 1), regular expression (tag 35), extension types (tag 257), custom tags (tag 258)
- **Static helper**: `asvJSON::cborFromString()` - JSON string -> CBOR bytes

Documented round-trip limitations:
- **`DATETIME` is tag 1 + float64** (seconds with a `.ms` fraction), so it inherits
  float64's exact-integer range. `|timestamp| <= 2^53` is exact; beyond that the
  seconds are rounded to the nearest representable double (up to ±512 s at the far
  end of `time_t`) and `datetime_ms` is dropped entirely. Use `TIMESTAMP` (tag 1 +
  integer) for instants that need full `int64` second precision - that is also why
  the two node types are kept distinct on the wire: a hand-written tag 1 + integer
  decodes as `TIMESTAMP`, not as `DATETIME`.
- **`tz_offset_min` is not encoded** (tag 1 is an absolute UTC instant) and comes
  back as the default on decode.
- **A `raw_number` string becomes a quoted string.** A number literal no
  `int64`/`double` can hold (e.g. `123456789012345678901234567890`) is kept by the
  JSON parser as a `STRING` with `raw_number` set, so JSON re-emits it unquoted.
  CBOR has no bignum that would carry the literal text, so it is written as a plain
  text string: the flag is lost and the value comes back as a `STRING`, quoted
  again in `toJSON()`. Same trade-off as MessagePack's out-of-range `uint64`.
- **Text strings must be valid UTF-8** (RFC 8949 §3.1), so `toCBOR()` throws
  `"CBOR invalid UTF-8 in text string"` rather than emitting a document its own
  decoder would reject. This also covers a `REGEX` pattern and a `CUSTOM_TAG`
  name, whose factories copy bytes verbatim and do not validate - other
  serializers in the library emit such values as-is, CBOR does not. Use `BINARY`
  (byte string, deliberately unchecked) for arbitrary byte payloads.
- **Nesting** is capped at `MAX_NESTING_DEPTH` (48) levels and a document claiming
  a deeper structure is rejected rather than risking the stack. Counting nested
  *containers*, CBOR allows 49 where the JSON parser allows 48: the CBOR guard
  (`depth > 48`, root at depth 0) applies to every item including scalars, so one
  extra level fits only when the innermost container is empty. CBOR is the more
  permissive of the two; both limits are hard caps.

### CSV

Input:
```json
{"name":"John","age":30,"meta":{"city":"NYC"}}
```

Output:
```
name,age,meta.city
John,30,NYC
```

Nested objects are flattened to dotted column names (`meta.city`). Column order is
first-appearance order of each flattened key in document order, not alphabetical.

Input (array of objects):
```json
[{"x":1,"z":3},{"y":2}]
```

Output (two-pass, union keys, first-appearance order):
```
x,z,y
1,3,
,2,
```

Note that `fromCSV()` always returns an ARRAY of row objects, so an OBJECT root
round-trips as a one-element array, and a nested object comes back as a flat
dotted key (`{"a":{"b":1}}` -> `"a.b\n1\n"` -> `[{"a.b":1}]`).

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

Decoder note, for hand-written input only (the encoder emits none of these): a list
item's value is `content.substr(2)`, and `content` comes from `stripIndent`, which
ltrims **only** -  the trailing whitespace of the line survives. So a bare `- ` is
two characters, matches the `- ` guard, and yields one **empty-string** element
(`[""]`), not the object item that a bare `-` with no trailing space opens (`[{}]`).
What follows the hyphen-space is the element: `-  ` (two spaces) is a one-space
string, and `-<TAB>` is neither form -  it misses both guards and is read as the
root scalar string `"-<TAB>"`.

Decoder robustness: a UTF-8 BOM at the start is stripped (it used to become part
of the first key's name). A pure all-digit declared array length over 1e9 is
rejected with an `array too large` error in both the named `key[N]` form and the
list-item `- [N]` form, instead of degrading into a mangled `key:value` line or a
bogus string element; a non-digit inside the brackets (`v[2000000000x]`) still
keeps the old degrade. A line whose only `:` sits inside a balanced quoted token
has no unquoted separator and is reported as an `unrecognized line` -  a
hand-written quoted scalar like `"a:b"` must appear as a value (e.g. `k: "a:b"`)
or as the whole document. (An *unterminated* quoted token, e.g. the escaped-quote
key `"a\"` of `"a\": 1`, still falls back to the raw `:` so it keeps parsing.)

Special types ride the Extended JSON markers the core JSON parser recognizes, so BINARY, EXTENSION, OBJECTID, REGEX, TIMESTAMP, DATETIME and CUSTOM_TAG round-trip through `toTOON()`/`fromTOON()` (a CUSTOM_TAG keeps both its name and its payload via `{"$customTag":{"name":...,"value":...}}`).

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
- **Regex values** serialize as `"__REGEX__<pattern>|<options>|"` (a mandatory trailing `|`; literal `|` inside the pattern/options is escaped as `\u007C`), so patterns with alternation round-trip correctly

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
- Quote/escape-aware key scanning: quoted keys that *end* with a backslash (`"a:b\\"`) or contain `[`/`]` round-trip correctly instead of being dropped
- Strict decoding: an unrecognized line throws (returns `false` from `fromGOON`) rather than being silently discarded; a `- item` outside an array frame is an error
- RLE repeat counts are range-checked with overflow detection (an over-limit `*N` throws instead of invoking `atoi` UB)
- The dictionary is capped at 256 entries, so a huge document cannot grow an unbounded `$:` header
- Empty roots (`{}`/`[]`) serialize as a single line and parse back
- A leading `+` is not part of a number, so a hand-written `+5` (or `+Infinity`) is a STRING, matching what the encoder writes; a sign inside an exponent (`1e+5`) is still a number
- `DATETIME` keeps its UTC offset in both encoder paths (e.g. `2023-11-15T03:13:20.250+05:00`)
- An empty BINARY/EXTENSION keeps its type: the marker is still emitted (`__BASE64__`, `__EXT__<type>_`, an empty payload) instead of degrading to `""`
- `CUSTOM_TAG` (an unknown tag from YAML/ADE/JSON) rides the `{"$customTag":{"name":...,"value":...}}` marker the JSON parser recognizes, so the tag name and payload both round-trip - including as a tabular cell, whose row splitter is quote- and brace-aware
- A cell that cannot be expressed in the flat `val1,val2` row syntax makes that whole table fall back to list format instead of corrupting the row. A cell is rejected when it carries a bare comma (e.g. the REGEX pattern `a,b`), a bare `*` (it would start an RLE repeat count — a STRING is immune because `goonNeedsQuotes` quotes it, a bare `__REGEX__` marker is not), a raw newline, or unbalanced `{`/`[`/`}`/`]` or an unterminated quote (e.g. `a[b`, where the decoder's brace-aware splitter used to run past the cell and merge columns, turning `a[b,1` into one cell plus a null)
- A depth-0 `:` in a cell (e.g. the REGEX pattern `a:b`) is not a rejection: the table stays tabular and its rows are indented one level deeper, which keeps it unambiguous
- A `__REGEX__` payload is backslash-escaped so a pattern can never end its cell, row or line: `\`, `|`, `:`, `\n` and `\r` are escaped (a pattern may legally contain a real newline, and a raw one used to split one cell into two bogus rows). Because `\` is escaped first, a literal backslash-n stays distinguishable from a real newline. Hand-written documents using the older unescaped form still decode
- A root value may be a complete single-line flow collection (`{"a":1}` **or** `[1,2,3]`), a special-marker token, or a bare scalar; none of them is read as a block mapping. The marker case matters because a REGEX pattern may contain an unquoted `:` (`__REGEX__a:b` used to decode as `{"__REGEX__a":"b"}`, losing the type)
- A bare `__TS__` with no numeric payload stays the string `"__TS__"` instead of splicing into the invalid JSON `{"$timestamp":{"t":}}` and failing with a foreign `Unexpected: }` error
- The prototype keys `__proto__`, `constructor` and `prototype` are accepted as ordinary keys: the spec asks implementations to reject them to prevent prototype-chain pollution, which cannot occur here (keys live in a map, never in an object prototype)

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

Schema-driven typing is strict: a field accepts only the value type it names, plus the widenings that lose nothing. An `int32`/`int64`/`sint*` field takes an INT, a BOOL as 0/1, or a DOUBLE that is exactly an in-range integer (no fractional part, no NaN/inf, within bounds); a `float`/`double`/`fixed*` field takes any numeric value; a `bool` field takes a BOOL or a numeric 0/1. A STRING, NULL, BINARY, EXTENSION, OBJECT or ARRAY in a numeric field -- and the reverse -- throws `asvJSONError` naming the field, instead of silently encoding 0 or an empty field. `enum` is carried as its integer ordinal (symbolic names have no mapping, as the schema carries no name table) and decodes back as an INT.

Documented limitations:
- **Repeated scalar**: a repeated schema field given a scalar is encoded as a single element and decodes as a one-element array.
- **Nested arrays**: not representable in the schema-less wire format (the name map records one type code per field, not per element). `toProtobuf()` throws rather than dropping the sub-array.
- **Untyped LEN payloads**: a length-delimited value of unknown type decodes as a sub-message or a BINARY, never a STRING.
- **`EXTENSION` type byte**: preserved through the schema-less round-trip via the name map, for a scalar field and for an array whose elements are all extensions of the same type. A mixed-type extension array decodes as BINARY.

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
- **Null handling**: TOML has no null type; null values are skipped during encoding (a NULL scalar root emits an empty document)
- **Special types degrade to text**: TOML has no binary/regex/objectid/timestamp/extension/custom-tag types, so `toTOML()` writes them as text (base64, the regex pattern, the 24-hex object id, the epoch seconds, the Extended-JSON tag text) and `fromTOML()` reads them back as strings (an int for a timestamp) - the type identity is lost, but the payload is kept (an earlier version dropped such members silently, so the key vanished from the document)
- **Quoted "null" strings**: a DATETIME whose instant `gmtimeSafe` cannot convert (e.g. pre-1970) is emitted as the quoted string `"null"`, so the line stays valid TOML and re-reads as a STRING
- **Line continuations in basic strings**: a `\` at end of line folds the line break and the next line's indentation (a TOML 0.5 rule kept as a lenient extension for single-line strings)
- **Weak date-time validation**: only the month (1-12) and day (1-31) ranges are checked; `2024-01-01T99:99:99Z` decodes to the STRING `"2024-01-01T99:99:99Z"`, while a malformed date prefix (`2024-13-01`) fails the numeric parse
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
- **Error handling**: invalid JSON on any line rejects the entire input; the message names the line number, repeats the inner parser's diagnostic (`Expected ':'`, `Unexpected: }`) and quotes the first 30 bytes of the offending line (cut at a character boundary, so multi-byte text stays readable)

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
- Comments round-trip: `fromRON()` attaches comments to their nodes and `toRON()` re-emits them in place (leading above a key/item, same-line trailing after a value, closings before `)`/`}`/`]`, and a trailing footer after the document). The attached comments also survive conversion into the other comment-capable text formats: `toJSON5()` keeps them verbatim (`//`/`/* */` are native), `toYAML()` translates them to `#` lines, and `toADE()` keeps them verbatim (`//`/`/* */` are native ADE comments). `serialize()`/JSON output intentionally drops them (JSON has no comment syntax)
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

C style emits `typedef struct ... ;`, C arrays (`int window_size[2];`), plain `enum` types (`enum Direction`), and no member initializers (C has none); the include preamble uses `<stdbool.h>`, `<stdint.h>` and `<time.h>`. A nested array keeps every dimension (`int m[2][3];` is two rows of three). Disable the whole feature with `ASVJSON_DISABLE_CPP`.

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

Supported constructs: `struct`/`class`/`union` definitions (public members; methods, access specifiers, bitfields, templates, bases and the `final` class-head specifier skipped), C `typedef struct { ... } Name;`, nested `struct`s with a trailing declarator, `enum` / `enum class` (with optional underlying type and explicit values), an anonymous enum with a declarator (`enum { A, B } var;` keeps the member `var`), `using Name = Type;` and `typedef` aliases (including `typedef enum E { ... } Alias;`, whose trailing name is registered as the alias), and forward references. Member attributes (`[[nodiscard]] int y;`, `int x [[deprecated]] = 5;`) are skipped on either side of the declarator name and of the initializer, so an attribute is never mistaken for a field name or an array suffix. Types: scalars (`int`, `double`, `bool`, `std::string`, `const char*`), `std::array<T,N>` and C `T[N]` arrays, `std::vector`/`std::list`/`std::deque`/`std::set` (dynamic), `std::variant<...>`, `std::optional<T>`, `std::map`/`std::unordered_map`, and `std::pair` (materialized as a fixed 2-element array, both members used as the element's type alternatives so `p{1,"a"}` keeps both values). `//` and `/* */` comments are preserved on nodes when `includeComments` is set - including the comments of the enumerators, which are attached to the variant values where the enum type is used. A fixed `std::array<Enum,N>` left uninitialized with `N` equal to the enum's member count is filled with the member names in order (recovering enum arrays).

**Initializers:** `= ...` and brace-init (`Point p{1,2};`, `int arr[3]{1,2,3};`) are both decoded from the member's default initializer, which is the data source. A constructor-style brace group fills the members positionally, so `Point p{1,2}` recovers `{"x":1,"y":2}`. An initializer that is not a literal the reader can evaluate - a binary/unary operator expression (`1 | 2`, `1 << 4`), a call (`sizeof(int)`), or a concatenation (`"a" + "b"`) - yields the type's default value (`0`, `false`, `""`), and a bitfield (`int x : 3;`) decodes to its field default because a JSON tree has no bit width. Lenient mode therefore prefers a wrong-looking default over failing; `strict` mode rejects all of these instead. A truncated struct body (EOF before the closing brace) is always an error, never a silently dropped struct.

Object-like `#define NAME replacement` macros are expanded across the declaration before parsing, so constants (including multi-line `\`-continued ones and chains) resolve in member initializers, array lengths (`T name[N]`, `std::array<T,N>`) and enum contexts; the replacement may be a number, string/char literal, `true`/`false`/`nullptr`, a parenthesized or unary-signed literal, or another macro. A comment is never part of a replacement list, and it is **removed** (not treated as the end of the replacement) before the body is tokenized: `#define MAX /* cap */ 100` and `#define MAX 100 // note` both yield `100`, and the removal is quote-aware, so a `//` or `/* */` sequence inside a string or character literal of the replacement stays data. An unterminated block comment at the end of a replacement is dropped to the end of that text as well. Function-like macros (`#define F(x) ...`) are left untouched. Scalar initializers also accept surrounding parentheses and a leading unary sign (`int n = (-1);`); the `toCpp` form `(-9223372036854775807LL - 1)` for `INT64_MIN` parses back to the boundary value. String literals decode the C++ escape set: `\\`, `\"`, `\'`, `\n`, `\t`, `\r`, `\0`, `\a`, `\b`, `\f`, `\v`, `\xNN`, octal (`\0`-`\377`, up to three digits) and `\uNNNN` / `\UXXXXXXXX` (supplementary code points encode as 4-byte UTF-8, lone surrogates as U+FFFD). A `\U` code point above `U+10FFFF` or in the surrogate range has no UTF-8 form and is rejected; an unknown escape stays verbatim (`"\8"` reads as `"8"`).

A file-scope instance declared with a trailing declarator is materialized under its variable name, both when the root is wrapped and when it is unwrapped (`struct Document { int x = 1; } extra;` -> `{"x":1,"extra":{"x":1}}`) - the unwrap path used to drop it, on the reasoning that a single root means every file-scope declarator's type is referenced by the root's fields, which does not hold for a declarator of the root type itself.

`std::unique_ptr<T>` / `std::shared_ptr<T>` member types melt to their pointee, so generated headers round-trip. Note that a self-referential struct (`struct Node { std::unique_ptr<Node> next; };`) materializes its initializer tree up to the depth guard (`MAX_NESTING_DEPTH`) before erroring - expected for recursive structures with unbounded data.

**Root handling:** when an alias (`using`/`typedef`) exists, the last one in source order becomes the root; otherwise the root is the single struct not referenced by any other struct. A root named `rootName` (default `Document`) is unwrapped to its members; any other single struct is wrapped as `{ "Name": { ... } }`, and multiple roots are wrapped the same way.

**Options (`CppReadOptions`):** `unwrapRootStruct` (unwrap a single root struct, default `true`), `materializeDefaults` (use member initializers, default `true`), `includeComments` (attach parsed comments, default `true`), `strict` (reject what cannot be represented instead of defaulting it: unparsable members, bit-field widths, and initializer expressions that are not literals; default `false`), and `rootName` (default `"Document"`). Disable the whole feature with `ASVJSON_DISABLE_CPP`.

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

All six types survive a JSON5 round-trip, DATETIME included: the core parser's date detection restores the type from the ISO-8601 string, milliseconds and UTC offset included (`1712345678` ms `123` at `+05:30` re-reads identically). BINARY and EXTENSION are told apart by `subType` (`00` vs. the extension type), and OBJECTID stores its 12 raw bytes as 24 hex digits.

`//`, `/* */` and `#` comments round-trip: `fromJSON5()` binds each comment to the node it documents and `toJSON5()` re-emits it in place -- leading above a key/item, same-line trailing after a value, closings before the `}`/`]`. Nodes are addressed by length-prefixed path segments, so a key containing any byte (U+001F, NUL, high bytes) keeps its comments. Two placement rules worth knowing: a second `//`-trailing comment on one value cannot share the value's line, so it is written below it and re-reads as a leading comment of the next member (or the container's closing comment) -- the text survives, the placement normalizes; and a raw control byte inside a string is rejected, so a NUL key must be written with the escape `"a\u0000b"`.

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

**Features:** sections `[name]` with dot-notation nesting (`[parent.child]`), `;`/`#` comments, quoted `"values"` with escape sequences (`\n`, `\t`, `\r`, `\0`, `\a`, `\b`, `\'`, `\\`, `\;`, `\#`, `\=`, `\:`), line continuation (`\` at end of line), global keys (before any section), `=`/`:`/space delimiters, duplicate key override, array of objects support. The escape table is the exact inverse of the decode table: every escape `toINI()` writes, `fromINI()` reads back, and `\xNN` is understood on decode (as it already was in section paths), so a literal `\x41` must be written `\\x41`. A value holding a space, a quote, a delimiter, a comment introducer, a backslash or a control byte that has an escape (`\0`, `\a`, `\b`) is always emitted quoted, so the escapes are always reachable. Keys are escaped on both sides (flat `key = value` and section-path segments): `[A-Za-z0-9_.-]` literal, `.`/`\` backslash-escaped, everything else `\xNN` (`\x5B` for `[`) -- so a key starting with `[` round-trips through its hex escape (a bare `[k] = v` line would be read as a section header; the encoder never writes one). An EMPTY key or empty section-path segment has no INI spelling and makes the encoder throw, both as a flat key and as a section header, so `{"":...}` is an error rather than silently collapsed output.

On decoding, values are typed: quoted values are always strings; bare values may be null/bool/int/double (leading-zero, hex and overflow literals are kept as exact strings); and a bare full ISO-8601 timestamp (`1970-01-01T05:00:00.123+05:00`) is typed DATETIME and round-trips losslessly (seconds, milliseconds and tz offset included). A plain string that merely looks like a full ISO date is quoted on encode so it stays a string. On encoding, types are formatted appropriately (numbers bare, booleans `true`/`false`, objects as nested sections, arrays of objects as duplicate sections). The special value types degrade to text like the other text formats: DATETIME (bare ISO-8601, the only lossless one), TIMESTAMP (bare epoch seconds, re-reads as INT), BINARY/EXTENSION (quoted BASE64 of the payload), OBJECTID (quoted 24-hex of the bytes), REGEX (quoted pattern only, options dropped), CUSTOM_TAG (quoted `{"$customTag":{"name":...,"value":...}}` JSON convention text) -- the payload survives as text and the type identity is lost on re-parse.

### Dotenv  -  environment configuration files

`toDotenv()` / `fromDotenv()`  -  flat `KEY=VALUE` files used for environment variables.

```
# Application settings
export APP_NAME="My App"
PORT=8080
DEBUG=true # verbose
EMPTY=null
```

**Features:** optional `export ` prefix, `#` line comments and inline comments (a `#` is only a comment when preceded by space/tab *within the value*; the whitespace right after `=` is trimmed first, so a leading `#` is data however it is written -- `KEY=#text` and `KEY= #text` both keep the text `#text`), single-quoted literal values, double-quoted values with C-style escapes (`\n`, `\r`, `\t`, `\0`, `\a`, `\b`, `\f`, `\v`, `\\`, `\"`, `\'`, plus `\xHH` for control bytes with no short form, e.g. `\x01`/`\x7F`; unknown escapes like `\$` and `\q` are preserved verbatim) that may span multiple lines, bare-value type inference (null/bool/int/double, plus full ISO-8601 timestamps like `2024-01-01T12:00:00Z` inferred as DATETIME -- INI infers the same full-ISO form now, and in both formats bare dates like `2024-01-01` without a time part stay strings, and DATETIME re-encodes as a quoted string that decodes back as STRING; leading-zero/hex/overflow literals kept as exact strings, while `{}`/`[]` are plain strings, again unlike INI), duplicate keys last-wins, and comment preservation on YAML-style round-trips (leading/trailing/footing). `toDotenv()` requires an object root; nested objects/arrays have no dotenv syntax and are emitted as their compact JSON text inside a quoted string (lossless text, but they decode back as strings). Keys are written verbatim -- dotenv has no quoted-key form -- so a key containing a control byte (or space, `#`, `=`, or a quote) is rejected by both the parser and `toDotenv()`; values escape every such byte as `\xHH`, so no control byte ever lands raw in the file. Auto-detection (`fromAny`, `.env`/`.env` files) is wired in and only attempted for flat `=`-containing input with no `[section]` headers.

### Bencode  -  BitTorrent encoding

`toBencode()` / `fromBencode()`  -  the encoding used by BitTorrent `.torrent` files.

```
d7:formatsi22e4:name9:asvJSON++4:tagsl4:json3:c++ee
```

**Format:** integers `i42e`, byte strings `4:spam`, lists `l...e`, dictionaries `d...e`. **Decoding:** integers become INT; byte strings become STRING when the bytes are valid UTF-8 and BINARY otherwise; lists become ARRAY; dictionaries become OBJECT. **Encoding:** INT/STRING/BINARY/ARRAY/OBJECT encode natively and dictionaries are emitted with keys in ascending byte order (as the spec requires). Value types with no bencode representation map to the closest form (documented lossy): BOOL -> integer 0/1, NULL -> empty byte string, DOUBLE -> its decimal text as a byte string, DATETIME/TIMESTAMP -> integer seconds, OBJECTID/REGEX -> byte string, EXTENSION -> raw bytes, CUSTOM_TAG -> its payload value. So JSON5 -> bencode round-trips work for arbitrary data (e.g. `{ratio: 1.5, active: true}` -> `d6:activei1e5:ratio3:1.5e`). Auto-detection (`fromAny`, `.torrent`/`.bencode` files) tries bencode first for input starting with a `d<digits>:` dictionary signature, i.e. it scans the whole leading key length so a first key of 10 bytes or more (`d13:announce-list...`) is recognised too (before the YAML-like GOON/TOON parsers that would otherwise claim short colon-heavy input, and before JavaProp, which would otherwise mis-detect it) and, in the binary path, only for that same signature; a full consuming parse is always required. On input the parser does not require dictionary keys to be sorted and keeps the last of any duplicate keys; the encoder always writes sorted output. A REGEX encodes as a byte string of its **pattern only**  -  the options after the NUL separator are dropped, mirroring the plist encoder, so it survives only as a plain STRING (`.torrent`/`.bencode` has no regex type). And a byte string is bencode's only empty form: NULL, an empty STRING and an empty BINARY all encode as `0:` and decode back as the empty STRING.

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

**Features:** `#`/`!` comment lines (preserved on round-trips as leading/footing via the shared `Comments` metadata), `=`/`:`/first-whitespace separators, `\` line continuation (and `\\` kept literal), escape sequences `\\` `\t` `\n` `\r` `\f` `\uXXXX` (with surrogate pairs combined into 4-byte UTF-8 and lone surrogates replaced by U+FFFD), and INI-style bare-value type inference (null/bool/int/double/datetime, with leading-zero/hex/overflow literals kept as exact strings); duplicate keys last-wins; no quoting, and `#` mid-value is data (never an inline comment). Comment *markup* normalizes on a round-trip: a `!`-marker line or a space-less `#text` loads as text and is re-emitted as `# text` (hash plus one space), and a comment block the encoder emits under a value re-reads as the next key's leading block (or the root's footing after the last value). Empty keys (`.properties` allows `=value`) are rejected on both sides with "javaprop: empty key". The encoder is bug-compatible with the JDK: unescaped trailing whitespace in a value is dropped on load, leading whitespace escaped, keys escape `\ = : # ! space` plus control characters. Because of that loader rule the encoder also escapes a space in *final* position (`"a "` -> `a\ `, so it round-trips), while interior spaces stay literal; unescaped trailing whitespace in hand-written files is still dropped, exactly as the JDK does. Nested objects/arrays, TIMESTAMP, OBJECTID, REGEX and CUSTOM_TAG are emitted as their extended-JSON text (`{"$oid":...}`, `{"$timestamp":...}`, `{"$regex":...}`/`{"$options":...}`, `{"$customTag":...}`) and BINARY/EXTENSION as base64 of their bytes; all are documented lossy (they decode back as a plain STRING -- the JSON text, or the base64 text). The output is UTF-8 encoded (non-Latin characters are written raw), not ISO-8859-1: it round-trips through this decoder fine because the loader accepts UTF-8 too, but a JDK `store`-produced file expects ISO-8859-1 and will misread bytes >= 0x80 (the JDK writer escapes non-Latin characters as `\uXXXX`). A comment whose text contains `\n` is emitted as one `#` line per physical line, so it re-reads as several separate Comment entries rather than one multi-line comment; the single-comment-to-multiple-comments loss mirrors what INI/dotenv do with trailing comment text. Bare-value type inference accepts a leading sign (`+5` -> INT 5, `+3.14` -> DOUBLE) — the same lenient convention shared by INI/dotenv/CSV (the plain JSON grammar rejects a leading `+`, and the JDK does not type at all, but this library's bare-value inference treats it as an int/double). NaN is written as the bare token `nan` with no sign bit preserved (`-NaN` -> `nan` on encode, which `strtod` reads back as positive NaN); only `-Infinity` keeps its negative sign. This is a library-wide convention (the same `nan`/`inf`/`-inf` spelling is used in INI/dotenv/plist too), and the guard against stamping `.0` on top (`nan.0`) stays valid for both the strtod fallback and the `std::from_chars` fast path — `from_chars` stops at the first non-digit after `nan`, so it also rejects `nan.0`. A finite integral double gets a ".0" appended so it re-loads as DOUBLE, not INT. `toJavaProp()` requires an object root. Auto-detection (`fromAny`, `.properties`/`.props` files) is wired in after the dotenv gate and lures content with an `!` comment line, a `\u` escape, an `=` or an unspaced `:` before the first whitespace on a line (a separator that only appears *after* whitespace, or a whitespace-separated pair, is deliberately not distinctive enough).

### Plist  -  Apple Property List (XML)

`toPlist()` / `fromPlist()`  -  the classic XML `.plist` files used by Apple configuration (`Info.plist`, `CFPreferences`, Xcode settings).

**Format:** an XML document rooted at `<plist version="1.0">` wrapping a single value: `<dict>` of alternating `<key>`/value pairs, `<array>`, or a scalar. Value elements map onto the JSON model - `<string>`  ->  STRING, `<integer>`  ->  INT, `<real>`  ->  DOUBLE, `<true/>`/`<false/>`  ->  BOOL, `<date>`  ->  DATETIME (ISO-8601 with timezone preserved), `<data>`  ->  BINARY (base64). **Decoding** accepts an optional `<?xml ...?>` declaration, `<!DOCTYPE ...>` (including bracketed internal subsets), and `<!-- -->` comments between elements; entities `&amp;` `&lt;` `&gt;` `&quot;` `&apos;` plus numeric references `&#NN;`/`&#xHH;` are unescaped; a UTF-8 BOM is stripped; and the `<plist>` wrapper is optional (a bare `<array>`/`<dict>`/scalar document also parses). Whitespace is tolerated before the `>` of any tag, so `</dict >` parses like `</dict>`; `<!-- -->` comments inside a text node are dropped rather than kept as literal text; a numeric reference in the surrogate range (`&#xD800;`..`&#xDFFF;`) is not a valid Unicode scalar value and becomes U+FFFD (as in ADE); and `<data>` base64 may omit the trailing `=` padding; a `<![CDATA[...]]>` section inside a text element is read as its raw content (entities inside the section stay literal, per XML 1.0); and tag attributes (like `<plist version="1.0">`) are accepted and ignored. Malformed documents are rejected with a `plist:` error, never silently mis-parsed (strict string truncation, invalid numbers/dates/base64, missing keys, unknown elements, trailing content). **Encoding** emits the XML declaration + DOCTYPE + `<plist>` wrapper with 2-space indentation. NaN/Infinity write as bare `<real>nan</real>`/`<real>inf</real>` and read back via `strtod` (a non-standard extension Apple readers will not understand, mirroring the JSON layer's `allowNaNInfinity` convention). plist has no null type: NULL is written as an empty `<string/>` and reads back as an empty string (documented lossy). Special types degrade to their underlying representation: REGEX  ->  `<string>` (the full pattern, `|` alternation included; the options are dropped, as plist has no regex type), TIMESTAMP  ->  `<integer>` (seconds), OBJECTID/EXTENSION  ->  `<data>`, CUSTOM_TAG  ->  its payload value (tag name dropped). Auto-detection (`fromAny`) runs before the generic XML parser when the first element of the document (after an optional BOM, `<?xml ...?>`, comments and `<!DOCTYPE>`) is a `<plist>` root - a bare `<plist` substring, an element named `<plistx>` or the word inside a comment no longer claims the document; `.plist` files dispatch through `readFromFileAny`/`writeToFileAny`.

### UBJSON  -  Universal Binary JSON

`toUBJSON()` / `fromUBJSON()`  -  the compact binary encoding defined at ubjson.org; a direct binary sibling of JSON.

```
{ i 0x01 a  i 0x2A }        // {"a":42}
S i 0x03 f o o              // "foo"
[ $ U # i 0x03 <bytes...>   // binary blob (decodes to BINARY)
```

**Format:** big-endian markers `Z` (null), `N` (noop), `T`/`F` (bool), `i`/`U`/`I`/`l`/`L` (int8/uint8/int16/int32/int64), `d`/`D` (float32/float64), `H` (high-precision number, kept as an exact STRING to avoid precision loss), `C` (1-char string), `S` (string), `[` (array) and `{` (object). **Encoding:** numbers use the smallest fitting integer type; strings are written as `S <len-type><len><bytes>`; object keys are bare length-prefixed strings (no `S` marker); containers are emitted in the simple unoptimized `[...]`/`{...}` form, and BINARY is written with the spec's binary-data convention `[$U#<len><bytes>`. **Decoding:** all containers are accepted - unoptimized, counted (`[#<n>`), and optimized/typed (`[$<type>#<n>`); a typed uint8 array `[$U#...` decodes to BINARY; `$` without a `#` count is rejected (both `[$` and `${`); `N` noops are skipped only inside untyped containers (where every element carries its own marker) - typed/optimized containers never skip `0x4E` bytes because elements there are bare payload (a `0x4E` in a typed int8 array decodes as value 78), `[$N#<n>` (a no-op typed array, empty body per spec) decodes as an **empty** array while `{$N#<n>` fixes every object value to null, and a container-typed element must open with the marker its `$<type>` header declares (a mismatch is a `typed array/object element marker mismatch` error, not a silent re-interpretation). DATETIME encodes as float64 seconds, TIMESTAMP as int64 seconds, REGEX as its string. OBJECTID and EXTENSION payloads encode as binary blobs (`[$U#...`) — this is intentional type-loss because UBJSON has no native type for them; the JSON representation (`{"$oid":"..."}`, `{"$binary":{"base64":"..."}}`) round-trips losslessly through any format that preserves objects, but a raw UBJSON round-trip converts OBJECTID/EXTENSION to BINARY and back to the base64 string. For full type preservation use JSON or a format with explicit special-type support. UBJSON has no magic leading bytes, so `fromAny()` does not auto-detect it - call `fromUBJSON()` directly or use the `.ubj`/`.ubjson` file extensions.

### SVG  -  Scalable Vector Graphics

`toSVG()` / `fromSVG()`  -  SVG 1.1 vector graphics, the XML-based markup format. Encoding produces a well-formed `<svg>` document; decoding reuses the shared XML parser.

Input:
```json
{"svg":{"@xmlns":"http://www.w3.org/2000/svg","@viewBox":"0 0 200 150","rect":{"@x":"10","@y":"10","@width":"80","@height":"60","@fill":"red"},"circle":{"@cx":"130","@cy":"40","@r":"30","@stroke":"blue","@fill":"none"},"text":{"@x":"10","@y":"120","#text":"Hello SVG!"}}}
```

Output:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 150">
  <rect x="10" y="10" width="80" height="60" fill="red"/>
  <circle cx="130" cy="40" r="30" stroke="blue" fill="none"/>
  <text x="10" y="120">Hello SVG!</text>
</svg>
```

**Mapping:** object keys become element names, `@`-prefixed keys become attributes, the `#text` key (or a bare scalar child) becomes the element's text content, and an array value becomes repeated same-named sibling elements (`{"defs":{"clipPath":[a,b]}}`  ->  two `<clipPath>` elements inside `<defs>`). Void elements (`rect`, `circle`, `ellipse`, `line`, `path`, `polyline`, `polygon`) self-close and ignore children. A single-key root uses the key as the root tag directly; a multi-key root is wrapped in `<svg xmlns="http://www.w3.org/2000/svg">`, and a root-level `@name` key becomes an ordinary child element of that wrapper (`{"@x":1,"y":2}`  ->  `<svg ...><_x>1</_x><y>2</y></svg>`) - attributes come from `@` keys of an object that *is* an element (`{"svg":{"@x":1}}`  ->  `<svg x="1"/>`). A key that is not a legal XML element name is sanitized (`k[0]`  ->  `k_0_`, `a b`  ->  `a_b`, `@x`  ->  `_x`; an empty key or one starting with a digit or `-` gets an `item` prefix, e.g. `1st`  ->  `item1st`) because the XML name grammar has no `[`, `]`, space or `&`; such keys are therefore not reproduced verbatim on decode. A C0 control byte other than tab/LF/CR has no legal XML 1.0 spelling and raises `asvJSONError` on every path (element content, `#text`, attribute values and the scalar-root `<root>` fallback). An array attribute is emitted as its space-joined items; a non-finite item is skipped and an array where nothing survives (only NaN/Inf, or empty) still yields `attr=""`, while a *scalar* NaN/Inf attribute is omitted entirely. The `.svg` extension dispatches through `readFromFileAny()` / `writeToFileAny()`.

**Decoding:** `fromSVG()` delegates to the shared XML parser but with the same-named-children collapse disabled. Plain XML folds an element whose only children share a name back into a bare array (`<bin><item/>` x2  ->  `{"bin":[a,b]}`), because the *XML* encoder names array items `item`; the *SVG* encoder instead names the repeated elements after the JSON key, so keeping the children grouped under that name preserves the key and `{"defs":{"clipPath":[...]}}` survives a JSON  ->  SVG  ->  JSON round-trip. Known losses: a **single-element** array still loses its array-ness (one element is indistinguishable from an object value, and SVG emits no `asv:arr` marker), so it round-trips as a plain object; an **empty** array decodes as the string `""` (not null) because an empty `<tag/>` carries no type information - this differs from XML's `null` convention for empty elements, and is pinned in tests. Mixed text/element ordering is not preserved (`#text` is emitted before child elements), and non-serializable children (DATETIME, BINARY, OBJECTID, REGEX, TIMESTAMP, EXTENSION, CUSTOM_TAG) are dropped - the element is emitted but its value is lost (as `<tag/>`, the single empty-content spelling shared with an empty string, an empty container and null).

## Changelog

### 1.26.7 (2026-09-20)

TOML audit fixes (`asvjson/formats/toml.hpp`) from the latest err.txt review. New regression assertions merged into the existing `testFromTOML` / `testToTOML` cases, suite stays at **375**:

- **Fix (TOML decode, silent element loss):** `flushVal` in the inline-array parser called `parseTomlValue` without `strictTail`, so `[1 2]` (two bare tokens with no comma/newline) dropped the `2` and produced `[1]` -- `[1 2, 3]` became `[1, 3]`. `flushVal` now passes `strictTail = true`, matching the inline-table path; such input is a parse error instead of a silent loss.
- **Fix (TOML decode, array-table guard collision):** `arrayTablePaths` tracked only the last dotted-path segment, so `[[a.x]]` registered the leaf name `x` and accidentally allowed `[[b.x]]` to append onto a statically-defined `b.x = []` (violating the TOML rule against appending to a statically-defined array). The guard now keyes on the full dotted path (`arrayTablePaths.insert/count(fullPath)`).
- **Fix (TOML decode, multiline-string leading newline):** the block-style string openers trimmed `\r`/`\n` twice, so `"""` followed by a blank source line (content `\n\nhello` inside the delimiter) dropped both newlines instead of the spec-mandated single line break. Both `"""` and `'''` openers now trim exactly one line break (`\r\n` counts as one; a second `\n` stays as content).
- **Docs (TOML, file header):** added a "Known limitations" block documenting the silent NULL drop (`{"a": null, "b": 1}` -> `b = 1`, `[1, null, 2]` -> `[1, 2]`) and the BINARY/DATETIME round-trip drift (both serialize to base64/date-time *strings* and decode as STRING -- not re-tagged -- including RFC 3339 `2024-01-01T00:00:00Z`).
- **Chore (TOML encode, dead code):** folded the two identical branches in the compact `tomlEmitVal` path into one body.
- **Chore (TOML decode, dead flag):** removed the never-set `inMlString` flag whose `if (!inMlString)` wrapper was always true (multiline-string continuations are assembled separately and never reach `stripComment`).
- **Chore (TOML, key hardening):** `parseTomlKey` now throws on an unterminated quoted key instead of silently returning the accumulated prefix.
- **Chore (TOML, Unicode):** deleted the local `appendCodepoint` and routed `consumeUnicodeEscape` through the shared `appendUtf8Codepoint` (same UTF-8 encoder as protobuf/ADE/plist). The `>= 0x110000` out-of-range case remains unreachable from TOML because `consumeUnicodeEscape` already bounds-checks before encoding.
- **Chore (TOML, number tail):** removed the redundant `!isDelim(pos)` pre-check before `checkTailLen(pos)` in the number parser; with `strictTail` now forced at every `parseTomlValue` call site, `checkTailLen` alone rejects all trailing content.
- **Version bump:** 1.26.6 -> 1.26.7

### 1.26.6 (2026-09-20)

BSON audit fixes (`asvjson/formats/bson.hpp`) from the latest err.txt review. Four new regression tests, suite now at **375** (up from 371):

- **Fix (BSON decode, subtype 0x03 poisoning):** the "binary old"/UUID subdocument type was decoded by trusting an inner length (`oldLen`) untethered from the enclosing `blen`. When they diverged, `pos` was left in the middle of the block and the *next* element was read from arbitrary bytes inside the payload (smuggling arbitrary values past the strict terminator check). The decoder now tracks `blockEnd = pos + blen`, honors `oldLen` only when it exactly matches `blen - 4` (falling back to the `blen - 4` payload otherwise), and **always** sets `pos = blockEnd`.
- **Fix (BSON encode, silent truncation):** BINARY/EXTENSION/STRING length fields are written as 32-bit but `bin_data.size()` / `str_data.size()` were cast without a range check, silently truncating a length above `UINT32_MAX` (and overflowing on the string's `+1`). All three branches now throw `BSON ... too large` past the limit, matching the existing OBJECT/ARRAY guards.
- **Fix (BSON decode, nested bound):** embedded documents were parsed with the *whole buffer* as their limit instead of the parent's `docEnd`, so a forged child size could be validated only against `dataLen`. Nested parses now receive the parent's `docEnd` as their limit.
- **Fix (BSON decode, strict bounds):** element-bound checks were written as `pos + N > docEnd + 1`; the `+1` let a fixed-size value read the document terminator byte as data. All fixed-size branches (`0x01` double, `0x02` string, `0x07` objectid, `0x08` bool, `0x09` datetime, `0x10` int32, `0x11` timestamp, `0x12` int64, `0x13` decimal128) now check against `docEnd` itself.
- **Fix (BSON encode, embedded NUL):** BSON strings may only contain the trailing `0x00` terminator; keys were already guarded, but a value `"a\0b"` serialized as formally invalid BSON. `toBSON()` now throws on an embedded NUL in a STRING value.
- **Docs (BSON, file header):** documented the last-wins duplicate-key behavior, the subtype-0x03 realignment rule, EXTENSION subtype collisions (`0x00`/`0x03`/`0x05` decode as BINARY/UUID-ish), `0x06` Undefined normalizing to null, regex options stored verbatim (no `ilmsux` validation), and the STRING embedded-NUL rejection.
- **Version bump:** 1.26.5 -> 1.26.6

### 1.26.5 (2026-09-20)

Protobuf audit fixes, round 3 (`asvjson/formats/protobuf.hpp`) from the latest err.txt review. Same regression tests extended (`testProtobufAuditFixesRound2`, suite stays at **371**):

- **Fix (Protobuf wire, reserved-field desync):** the `protoDecodeMessageBody` skip of the schema-less name map (field 19000) was the one call site still ignoring the `protoSkipWireValue` return value. A truncated fixed-size value (e.g. wire type 1 with only 4 of 8 payload bytes) left `pos` unadvanced and would drift the next tag read into the middle of the value. It now `break`s out of the body decode (safe empty root) instead -- matching the already-correct check in the array-root scan.
- **Fix (Protobuf text, hex with no digits):** `0x` at end of input consumed the prefix and then returned `nullptr`, silently dropping the token (and, at the root, aborting the message). It now returns the bare `0x` literal as a raw-number string, keeping the execution point advanced -- symmetric with the trailing-junk handling of `123abc`.
- **Feature (Protobuf text, float suffix):** protobuf text-format float literals may end in `f`/`F` (`1.5f`, `-2F`); these were previously caught by the trailing-junk branch and kept as raw-number strings. The suffix is now consumed and the value parsed as the DOUBLE it names -- but only when it is the whole tail of the token. A trailing alphanumeric run (`123field`, `1e5x`, `1.5fxyz`) still falls through to the junk branch and survives losslessly as a raw-number string; it is never split into a DOUBLE plus an orphaned identifier.
- **Chore (Protobuf text, dead code):** the top-of-loop `if (!first && text[pos] == ',')` comma-eating branch in `protoTextParseArrayBody` was unreachable -- the bottom of the loop always consumes a comma when present. Removed the branch **and the `first` flag declaration**, which had been left behind as an unused local (`-Wunused-but-set-variable`).
- **Chore (Protobuf text, redundancy):** dropped the `std::isalpha` test beside `std::isalnum` in the trailing-junk checks (`std::isalpha` is a subset of `std::isalnum` for ASCII).
- **Docs (Protobuf, file header):** documented that `toProtobuf()` encodes only OBJECT/ARRAY roots (a scalar root silently yields an empty buffer, rejected by `fromProtobuf`), and that an empty OBJECT root (`{}`) is inherently unrepresentable on the wire (zero bytes == empty input) while empty ARRAY roots round-trip via the 19001 marker. Nested `{}` fields are preserved.
- **Version bump:** 1.26.4 -> 1.26.5

### 1.26.4 (2026-09-20)

Protobuf audit fixes, round 2 (`asvjson/formats/protobuf.hpp`) from the updated err.txt review. One new regression test (`testProtobufAuditFixesRound2`), bringing the suite to **371** (up from 370):

- **Fix (Protobuf text, scientific notation):** the numeric branch of `protoTextParseScalar` scanned its token with `protoTextParseIdent`, which greedily consumes `e`/`E` as identifier characters. `1e5` / `1E5` / `2E+3` were therefore swallowed whole (never treated as exponents) and returned as raw-number strings, while `1e+10` / `1e-5` stranded the parser on the sign and aborted the entire message. The token is now scanned character-by-character (`mantissa ['.' frac] ['e'/'E' sign? digits]`), so all scientific forms parse as DOUBLES and the message survives.
- **Fix (Protobuf wire, empty nested object):** a schema-less LEN payload that decoded to an empty sub-message was rejected by the heuristic (`obj->obj->empty()`) and fell through to the binary/string branch, turning `{"a":{}}` into `{"a":""}`. A zero-length payload that decodes to an empty object is now accepted as a message (non-empty payloads that produce no fields still fall back to BINARY, so NUL-prefixed byte strings are unaffected).
- **Fix (Protobuf wire, array-of-objects root):** `protoEncodeRoot` concatenated the elements of a top-level array into one TLV stream, merging their fields (last-wins for scalars) and letting each element's schema-less name map overwrite the others' -- data loss. The encoder now wraps the array in a synthetic message (marker field **19001**, wire type 0, value 1, then repeated field 1 with one sub-message per element); the decoder detects the marker and unwraps the field-1 submessages back into an ARRAY root. Empty arrays, mixed-key elements, and empty-object elements all round-trip losslessly. Schema-driven array roots use the same wrapper. The reserved range 19000-19999 is now used for both the name map and the array-root marker and is documented as unsafe for user data.
- **Fix (`fromProtobuf(const std::string&)`, dead code):** the "unpadded base64" acceptance branch was structurally unreachable -- an unpadded input has length % 4 != 0 and never enters the branch, and when padding is present the canonical padded re-encode already matched. Removed the dead `unpad` comparison; only the canonical (padded, exact-round-trip) check remains.
- **Fix (Protobuf wire, reserved-field desync):** the schema-less name map (field 19000) was skipped only on wire type 2; a malformed input using any other wire type left the value bytes unread, and the next loop iteration read the tag from the middle of them. A new `protoSkipWireValue` helper now advances past varint/fixed32/fixed64/length-delimited values of any width; it also keeps the top-level array-root scan synchronized on garbage input.
- **Fix (Protobuf text, lone sign):** a bare `-` (or a field value of just `-`) was coerced to integer 0; it now parses as an error (value skipped), and the same holds for `+`.
- **Chore (Protobuf text, local-string lifetime):** the overflow branches that returned a raw-number literal moved the local token into the value via `makeString(std::move(...))` instead of taking a `string_view` of an about-to-be-destroyed local (safe today, but fragile). Confirmed `makeStringView` copies (`core.hpp:586`), so this was prophylactic.
- **Chore (Protobuf text, name-map build):** `nm += '"' + std::to_string(...)` rewritten as `"\"" + std::to_string(...)` for readability.
- **Docs (Protobuf, file header):** documented the array-root wrapper convention and the reserved 19000-19999 range, plus the BINARY/EXTENSION -> STRING loss in the text format.
- **Version bump:** 1.26.3 -> 1.26.4

### 1.26.3 (2026-09-20)

Protobuf audit fixes (`asvjson/formats/protobuf.hpp`) from the err.txt review, covering both the wire format and the text format. One new regression test, bringing the suite to **370** (up from 369):

- **Fix (Protobuf wire, heuristic crash):** a schema-less LEN payload with an unknown/empty type was speculatively parsed as a sub-message with no exception fence; a blob that merely *starts* like a message (e.g. a byte string whose first byte looks like a valid tag followed by an unterminated varint) threw `asvJSONError` from deep inside the parse and aborted the whole decode. The speculative parse is now wrapped so any trouble falls back to the string/binary branch -- raw binary content decodes as BINARY instead of failing the entire message.
- **Fix (Protobuf wire, varint 10th byte):** `protoReadVarint` accepted a 10th byte (shift == 63) whose bits above the low bit were silently shifted out of the 64-bit value, tolerating non-canonical overlong varints; a 10th byte carrying any bit beyond bit 0 is now rejected with `malformed varint: 10th byte overflow`.
- **Fix (`fromProtobuf`, exception containment):** the `(const void*, size_t, schemaJson)` parser now catches `std::exception` (previously only `asvJSONError`), so allocation failures no longer escape the bool + `lastError` contract -- consistent with `fromProtobufText` and the other from*() parsers.
- **Fix (Protobuf wire, mixed arrays):** the schema-less type code for an array was taken from its first element (`[1,"a",true]` -> "I", decoded as packed int64 garbage). The encoder now emits the unknown code "A" unless every element shares one type, so mixed arrays decode per-element heuristically instead of silently corrupting.
- **Fix (Protobuf wire, field-number bound):** tag parsing accepted field numbers up to `INT_MAX`; the check is now the spec limit `2^29 - 1` (`protoMaxFieldNum`), preventing forged truncation of out-of-range field numbers.
- **Fix (Protobuf wire, schema-driven encode):** a string/bytes/message field whose JSON value has a different type silently encoded an empty field (data loss); `protoEncodeFieldValue` now throws `asvJSONError` naming the offending field type.
- **Fix (Protobuf text, hex/octal integers):** the scalar parser only handled decimal; it now accepts C-style `0x...` hex and leading-zero octal literals (with the same exact-literal `raw_number` preservation on overflow as decimal), while `0.5` / `0e5` still parse as doubles.
- **Fix (Protobuf text, lone surrogates):** an unpaired `\uD800..\uDBFF` (or a lone `\uDC00..\uDFFF`) escape emitted invalid UTF-8 instead of being rejected; it is now replaced with U+FFFD, matching `core.hpp::unescapeJsonString` (a following low surrogate that does not form a valid pair is reparsed on its own).
- **Fix (`fromProtobuf(const std::string&)`, base64 auto-detect):** a genuine binary protobuf whose bytes happen to all fall inside the base64 alphabet could be misread as base64 text. The string must now be the canonical base64 of its own decode (re-encode round-trip, ignoring `=` padding) before the convenience base64 path is taken; anything else falls back to the raw binary interpretation.
- **Docs (Protobuf, file header):** documented the known/limitations behaviors the audit flagged -- null values are not serialized by the text format, protobuf groups (wire types 3/4) are not supported, mixed-element arrays are not representable in schema-less mode, field numbers are capped at 2^29-1, and the schema-less sub-message heuristic falls back to BINARY rather than aborting.
- **Test:** new `testProtobufAuditFixes`: heuristic-fallback to BINARY on a `{0x08,0xFF}` payload, 10th-byte varint rejection (and a legal max 10-byte varint, INT -1), homogeneous array regression + mixed-array no-corruption, 2^29 field-number rejection (empty root), string/message type-mismatch throws, hex/octal text parsing (incl. overflow literal), lone-surrogate U+FFFD + valid surrogate-pair combination, and the base64 canonical-re-encode behavior (`"QQQ="` decodes as the raw message, a real base64 string still round-trips).
- **Version bump:** 1.26.2 -> 1.26.3

### 1.26.2 (2026-09-20)

MessagePack audit fixes (`asvjson/formats/msgpack.hpp`) from the err.txt review, plus `fromMessagePack` exception containment (`asvjson/core.hpp`). Three new regression tests, bringing the suite to **369** (up from 366):

- **Fix (MsgPack, CUSTOM_TAG round-trip):** `CUSTOM_TAG` previously fell into `default:` and encoded as `0xC0` (nil), silently destroying the annotation and its value on decode. It now encodes as the asvJSON-specific ext type **4** whose body is a msgpack-encoded name string followed by the msgpack-encoded value (an explicit nil when `custom_value` is absent), mirroring the CBOR tag-258 `[name, value]` layout; the name uses the smallest str form (fixstr/str8/str16/str32). The decoder validates before consuming: a body that is not a name string + exactly one complete value falls back to a plain EXTENSION holding the raw bytes instead of a partial decode, and the recursion is scoped to the ext body so sibling bytes are never consumed. The fixext branches (0xD4-0xD8) also dispatch `et == 4` to this decoder, because `writeMsgPackExt` sizes the wire form by body length and a custom-tag body can be exactly 2/4/8/16 bytes (e.g. empty name + nil/value) -- without that a previously-encoded short-body custom tag decoded back as a plain EXTENSION. Each branch validates the decode inside the fixed-size window with the same raw-EXTENSION fallback.
- **Fix (MsgPack, duplicate map keys):** fixmap/map16/map32 used `emplace`, silently keeping the FIRST occurrence and dropping later duplicates' values; all three now use `insert_or_assign` so last-wins, matching the JSON parser's convention (and the other binary formats).
- **Fix (MsgPack, 32-bit wraparound-safe bounds):** `pos + len > dataLen` / `pos + slen > dataLen` compared after a potential unsigned wraparound pass; rewritten as always-safe `len > dataLen - pos` in the ext8/16/32, bin8/16/32, fixstr, and str8/16/32 branches (the small fixed-size `pos + 2/4/8/16` length-header checks are unaffected).
- **Fix (MsgPack, null-alloc guards):** every hop through a `make*` factory that can return `nullptr` on allocation failure now throws instead of returning null -- `makeObjectId`/`makeTimestamp`/`makeDateTime`/`makeRegex`/`makeExtension` in ext and fixext1/2/4/8/16 branches, and map16/map32 values.
- **Fix (`fromMessagePack`, exception containment):** added a `catch (const std::exception&)` clause alongside the asvJSONError handler so allocation failures (the custom-tag decode uses `std::make_unique`) / other standard exceptions no longer escape the public API, consistent with `fromCBOR` and the other from*() parsers.
- **Docs (MsgPack, file header):** documented the asvJSON extension-type conventions referenced by the audit -- ext 1 = ObjectId (12 raw bytes), ext 2 = Regex (`pattern\0options`), ext 3 = Timestamp (asvJSON-specific int64 fixext8, distinct from the standard -1 timestamp), ext 4 = Custom Tag, ext -1 = the standard MessagePack timestamp forms. Also annotated that the 12-byte timestamp-96 datetime is only handled in the 0xC7 branch because MessagePack only emits that form as ext8, and unified the 0xC9 timestamps accumulation to unsigned `uint64_t` like its 0xC7/0xC8/0xD7 siblings.
- **Test:** new `testMessagePackCustomTagRoundTrip` (value + null value + >31-char name via str8, plus malformed ext-4 bodies decoding to a raw EXTENSION, never a partial CUSTOM_TAG), `testMessagePackDuplicateKeysLastWins` (all three map encodings), and `testMessagePackCustomTagShortBody` (custom-tag bodies of exactly 2/4/8/16 bytes landing on the fixext2/4/8/16 decoder paths, plus an ext8 sanity length).
- **Version bump:** 1.26.1 -> 1.26.2

### 1.26.1 (2026-09-20)

CSV audit fixes (`asvjson/formats/csv.hpp`) from the err.txt review, plus one new regression test (suite now at **366**, up from 365):

- **Perf (CSV, cell typing):** `toCSV` no longer builds a throwaway heap-allocated `asvJSONValue` (via `csvDetectType`) for every cell. Re-detection is consulted only for text-ish types (STRING / REGEX / BINARY / EXTENSION / OBJECTID) whose type actually needs quoting; INT/BOOL/DOUBLE/NULL/DATETIME/ARRAY/OBJECT cells always carry their fixed text and are never quoted, so a large numeric document avoids one allocation per cell.
- **Docs (CSV, file header):** documented the three deliberately non-obvious behaviors the audit flagged -- (a) the formula-injection guard applies to **bare** cells only (a cell that must be quoted anyway - contains `,`/`"`/newline - is emitted as a plain quoted literal without the `'`), (b) a hand-written bare `'=`/`'+`/`'-`/`'@` cell loses its apostrophe on decode (required so toCSV->fromCSV round-trips losslessly; silent for hand-authored input), and (c) a root OBJECT whose single key holds an ARRAY flattens that key to a column of JSON text and decodes back as an ARRAY of `{key:"..."}` rows; scalar roots emit a headerless line that does not round-trip.
- **Docs (CSV, comment placement):** the `@brief` doc comments now sit on their own functions -- `csvNumberShapeOk` got a matching `@brief JSON number shape gate for CSV cells` (the old "Detect type" text was misplaced there), and `csvDetectType` keeps `@brief Detect JSON value type from CSV cell string`; the `to_chars` branch of `csvDetectType` gained an explicit note that `result_out_of_range` (double overflow `1e400` / total underflow `1e-400`) falls through to a STRING exactly like the strtod branch below it.
- **Change (CSV, flatten column order):** both flattening branches sorted the flattened-key union lexicographically via `std::map` -- the OBJECT-root path iterated its `flat` map directly, and `writeArrayOfObjects` gathered header keys the same way, so `{"b":1,"a":2}` emitted `a,b\n2,1` while the parsed document's real order was `b,a`. Both now collect first-appearance-order columns into a vector (dedup / collision detection via `std::unordered_set`), so header order follows ObjectMapT insertion order -- consistent with the JSON/YAML/INI round-trip paths (see err.txt csv.hpp "writeArrayOfObjects -- std::map (сортировка) вместо ObjectMapT").
- **Test:** new `testCsvAuditCells` in the CSV runner: `.5` stays STRING (JSON has no leading-dot numbers, never re-forms as DOUBLE), `0`/`-0`/`+0` all decode to INT 0, and INT64_MIN (`-9223372036854775808`) survives a toCSV/fromCSV round trip as an exact INT.
- **Version bump:** 1.26.0 -> 1.26.1

### 1.26.0 (2026-09-20)

CBOR audit fixes (`asvjson/formats/cbor.hpp`) plus exception-containment hardening of `fromCBOR` (`asvjson/core.hpp`). Seven new regression tests, bringing the suite to **365** (up from 358):

- **Fix (CBOR, CUSTOM_TAG round-trip):** `CUSTOM_TAG` previously fell through to undefined on encode and was unrecognized on decode. It now encodes as CBOR tag **258** + array `[name(string), value]`, mirroring EXTENSION's existing tag-**257** layout; the name is required/always emitted, and a null `custom_value` emits an explicit CBOR null element (`0xF6`). The decoder validates (size-2 array with a string first item) before consuming so malformed input falls back to the untouched plain array instead of partial consumption -- matching JSON/YAML/Sexpr `$customTag` handling for annotations surviving round-trips.
- **Fix (CBOR, byte-string/text length overflow):** `len64 > SIZE_MAX || pos + len64 > dataLen` was vulnerable to unsigned wraparound in the intermediate addition when `len64` is huge; replaced with a single always-safe comparison against remaining buffer size (`dataLen - pos`).
- **Fix (CBOR, duplicate map keys):** previously used `emplace`, silently keeping first occurrence and dropping later duplicates' values; now uses `insert_or_assign` so last-wins, matching the JSON parser's convention.
- **Fix (CBOR, tag 1 time_t boundary + millisecond carry):** upper-bound check compared against a double-rounded value of INT64_MAX that was itself one past any legal time_t, making the cast below it UB; now computes an exact representable bound strictly greater than every valid time_t and rejects at/above it. Rounding the fractional part up to exactly 1000 ms now carries into the next second (with an overflow check before the increment) instead of being silently clamped -- so a fractional-seconds value like `...999` no longer loses one second.
- **Fix (CBOR, reserved simple values):** expanded the rejected "additional information" range from 28-30 to 28-31 to cover the full reserved range per RFC 8949 s3.2; also replaced an unqualified `pow` with `std::pow`.
- **Fix (`fromCBOR`, exception containment):** added a `catch (const std::exception&)` clause alongside the existing asvJSONError handler so allocation failures / other standard exceptions no longer escape the public API, consistent with every other from*() parser's "return bool + set lastError" contract.
- **Docs:** clarified in-code comments that OBJECTID's null-fallback branch is defensive (normal input can't reach it) and that DATETIME tag 1 intentionally does not encode tz_offset_min since an epoch instant has no recoverable local offset (expected data loss, matching other epoch-based formats).
- **Test:** new regression blocks under a "CBOR v1.26.0 Audit Fixes" section in test.cpp: custom-tag round-trip (value present + null value), excessive-length byte-string/text rejection, duplicate-key last-wins in maps, tag 1 time_t upper-bound boundary and ms-carry rounding both directions, plus a check that non-asvJSONError standard exceptions thrown deep inside parsing are caught by fromCBOR rather than escaping.
- **Version bump:** 1.25.9 -> 1.26.0

### 1.25.9 (2026-09-19)

Addresses three must-fix bugs plus documentation gaps from a security/correctness review of the RFC 9535 JSONPath implementation (`asvjson/formats/jsonpath.hpp`):

- **Fix (JSONPath, bracket-list partial results):** `jpExpandBracket` accumulated each valid list item's result into its output vector as it went; when a later item in the same `[a,b,c]` selector was invalid (bad number/escape/slice token, or an unrecognized token type), several early-exit paths used a bare `return`, leaving earlier items' already-appended results behind and leaking them out of the whole query. Every error exit inside that loop now rolls back to its pre-loop size before returning, so `$[0,!]` / `$[0,<out-of-range>]` on `[10,20,30]` correctly select nothing instead of silently returning `[10]`.
- **Fix (JSONPath, `length()`/`count()` on a missing node):** `jpGetPropCount(nullptr)` returned 0 for a null ("Nothing") input, so `@.missing.length() == 0`, or selecting `.x.missing.length()` directly, could wrongly match/select as if the length were genuinely known to be zero. Per RFC 9535 a missing node is "Nothing", not an empty collection: it now returns -1 (sentinel) and both `jpApplyFunc` call sites already mapped negative results to "no value" (`nullptr`), so such filters no longer match and direct selections of the length yield nothing.
- **Fix (JSONPath, filter-depth guard exception safety):** the manual `++filterDepth_ / --filterDepth_` pair around a nested parenthesized filter's recursive parse could leak an increment if that recursion exited non-locally; it is now held by a small RAII scope-guard struct (`FilterDepthGuard`) so the depth counter always unwinds exactly once regardless of how the guarded call exits.
- **Docs (JSONPath):** documented in the file header, alongside the existing RFC-compliance notes: single-value (not nodelist) filter semantics for `count()`/`length()` comparisons and loose membership behavior of things like `@.a[*] == 5`; that `match()`/`search()` use ECMAScript `std::regex`, a superset of I-Regexp, with an explicit ReDoS warning for untrusted patterns; and that `query()`/`queryFirst()`, while lenient about malformed path segments (they select nothing rather than throwing), do not suppress out-of-memory exceptions from internal allocations on very large/deep trees.
- **Test:** new regression blocks in `testJSONPathExtras`: an invalid selector appearing AFTER a comma no longer leaks the results of earlier valid items in the same bracket list; and `length()`/`count()` of a missing node now select nothing (never matching a bogus `== 0`) both as a filter operand and as a direct selection.
- **Version bump:** 1.25.8 -> 1.25.9

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
- **Unified comment model:** all comment-capable formats (RON, JSON5, YAML, ADE, INI, ...) now share the same `Comment`/`Comments` metadata (leading / trailing / closing / footing) on `asvJSONValue`, so comments bridge uniformly between formats. A leading block comment on a later entry is never emitted inline after the previous entry's separator in minified RON/JSON5 output (which would re-bind it as that entry's trailing comment), so `toRON(false)` / `toJSON5(false)` round-trips are idempotent. Added regression tests for both.
- **Version bump:** 1.17.0  ->  1.18.0

### 1.17.0 (2026-09-16)

- **New - RON support:** Added `toRON(bool pretty = true) const` and `fromRON(std::string_view)` plus `asvJSONValue::toRON()` for the RON (Rusty Object Notation) format. The decoder converts RON to an intermediate JSON document and parses with the existing JSON parser; the encoder walks the tree directly. Supported syntax: structs `(a: 1)`, maps `{ "k": v }`, lists, tuples, named types (`Name(...)` kept as `{"Name": ...}` and re-emitted as `Name(...)` from JSON), `Some(x)`  ->  unfolds, `None`  ->  `null`, enum unit variants and variants with data (`Red`, `Type::Variant(data)`)  ->  strings/objects, bare idents  ->  strings, radix/separable/suffixed numbers (`0xFF`, `0o17`, `0b1010`, `1_000`, `42u32`, `-7i8`, `3.14f64`), floats (`.5`, `5.`, `5.e3`, `inf`/`-inf`/`NaN`), quoted strings with escapes (`\n`, `\t`, `\xHH`, `\u{...}`) and line continuation, raw strings (`r"..."`, `r#"..."#`), byte strings/chars (`b"..."`, `br#"..."#`, `b'c'`), chars (`'a'`), ranges (`3..5`, `..=7.3`, `3..`, `..`, `..=`) round-tripping as `{"$range":{"start":...,"end":...,"inclusive":...}}`, `//`/nested `/* */` comments, `#![enable(...)]` headers, and trailing commas. Special types (BINARY/EXTENSION/OBJECTID/REGEX/TIMESTAMP/DATETIME/CUSTOM_TAG/YAML sets) round-trip through their Extended JSON marker objects. `.ron` is recognized by `readFromFileAny`/`writeToFileAny` and `fromAny` (heuristics prefer RON for parenthesized input containing `:`/`,`). Disable with `ASVJSON_DISABLE_RON`.
- **New - RON comment preservation:** `fromRON()` attaches `//` and `/* */` comments to their tree nodes (leading above a key/item, same-line trailing after values, closings before `)`/`}`/`]`, plus a footer for trailing document comments), and `toRON()` re-emits them in place for idempotent RON  ->  RON round-trips. A same-line `//` after a separator (`800, // optional struct name`) binds to the value that just ended, and the encoders place the separating comma BEFORE such a trailing comment so the next entry starts fresh on its own line (RON, JSON5 and ADE all do this; no stray leading `,`). The same metadata feeds the other comment-capable text serializers: JSON5 emits it verbatim, YAML translates it to `#` lines, and ADE keeps it verbatim, so RON  ->  JSON5 / YAML / ADE conversions preserve every comment. The reverse direction also works: YAML comments (from `yamlCollectComments`) are bridged into RON comment metadata before RON / JSON5 / ADE encoding, so YAML  ->  RON / JSON5 / ADE conversions carry `# ...` comments over as `// ...`. JSON output intentionally drops comments (JSON has no comment syntax). `cloneValueImpl` and `operator==` ignore comment metadata.
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

- **New - Custom tag dispatch hook:** Added `asvJSON::customTagTransform`, an optional user callback invoked at parse time whenever a custom-tag annotation (`{"$customTag":{"name":...,"value":...}}`) is recognized. It applies uniformly across YAML, GOON, ADE, and plain JSON (all funnel through the JSON object parser). The callback receives the resolved tag name and the tag's value and returns a replacement `std::unique_ptr<asvJSONValue>` (or `nullptr` to keep the default `CUSTOM_TAG` node). This closes the last remaining YAML/GOON known-gap item.
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

- **ADE format additions:** Custom tags with canonical name/value split, namespaces (`!ns:tag`) and verbatim (`!<uri>`), Extended JSON representation, YAML-style `---`/`...` document separators, versioned header, merge keys (`<<: *default`), digit separators (`1_000_000`), lossless big-integer literals, `!datetime` with timezone offsets, non-finite float literals (`.nan`/`/.inf`), type-coercion tags (`!int`/`!float`/`!string`/`!bool`/`!null`), `!schema`, bare `!null`, folded block scalar (`>`), single-quoted strings, full string escapes.
- **ADE spec conformance:** Documented resource limits (alias depth 100, anchors 1M), YAML-style tab prohibition in block scalar indentation.
- **Parser hardening:** Fixed OOB, UB and malformed input across YAML, S-Expression, TOML, XML, Base64, BSON, CSV, JSON5. Integer overflow detection, binary parser bounds checks, `cloneValue` node count limit (100M), YAML tag map RAII safety, `splitLines` phantom EOF removal.
- **Version bump:** 1.13.0  ->  1.14.0

### 1.13.0 (2026-08-04)

- **New format - ADE:** Added `toADE()` / `fromADE()`  -  Advanced Data Exchange with a `# ADE` header, multiple documents (`// --- End of Document ---`), `#`/`//`/`/* */` comments, anchors/aliases, block scalars `|`/`>`, and type tags (`!base64`, `!bin`, `!datetime`, `!ext`, `!regex`); strict mode rejects duplicate keys and special characters in bare tokens.
- **ADE spec conformance:** default chomping now clips (keeps one trailing newline) instead of stripping; block scalars handle a blank first line, and empty lines are preserved as paragraph breaks (they never terminate the scalar by themselves, Section 6) with a single trailing line break captured; the serializer falls back to quoted strings whenever a value cannot round-trip as a block scalar; quoted dotted keys (`"a.b.c"`) are literal while unquoted ones nest; document separators match exactly (no accidental splits on comments); anchors store deep copies (no dangling pointers) and forward/self references are rejected as undefined aliases; strict mode requires quoted keys; a leading `+` is no longer accepted as a numeric sign.
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
