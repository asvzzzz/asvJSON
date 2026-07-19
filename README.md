# asvJSON++

A C++17 JSON library supporting binary data, DateTime, Base64, BSON, MessagePack, CBOR, JSON Pointer, JSON Merge Patch, XML, YAML, CSV, TOON, TRON, GOON, Protobuf, TOML, JSON Lines (NDJSON), and S-Expression serialization.

**Author:** Sergey Andyk  asvzzz@narod.ru

## License

MIT license - see the `LICENSE` file for details.

## Features

### Core
- JSON types: String, Number, Boolean, Null, Object, Array
- Binary data with Base64 encoding
- Custom Base64 charset for encryption/obfuscation
- DateTime ISO-8601 with milliseconds
- File I/O operations
- Type-safe accessors with `opt*` defaults
- `std::string_view` support for all key and string operations (zero-copy where possible)
- `noexcept` on all accessors (`getInt`, `getDouble`, `getBool`) for performance
- Transparent `unordered_map` lookup - avoids temporary `std::string` allocation on modern compilers (GCC 9, Clang, MSVC)
- `std::from_chars` for fast double parsing on C++23 compilers
- `allowNaNInfinity` flag - parse/serialize NaN and Infinity values

### Comments
- `//`, `/* */`, `#` style comments

### Nested Access
- Dot notation: `json.getNested("user.address.city")`
- Escape dots with backslash: `json.getNested("domain\\.example.com")`

### Serialization Formats
- JSON (compact and pretty print)
- BSON (binary)
- MessagePack (binary, RFC 7049)
- CBOR - `toCBOR()` / `fromCBOR()` - RFC 8949 Concise Binary Object Representation with indefinite-length items, half-precision floats, datetime/extension/regex tags
- YAML - `toYAML()` / `fromYAML()` with block-style sequences/mappings, YAML 1.2 `.nan`/`.inf` literals, automatic key quoting, literal block scalars, tags (`!!int`/`!!float`/`!!bool`/`!!null`/`!!str`/`!!set`/`!!omap`/`!!pairs`), anchors & aliases, multi-document streams, `%TAG`/`%YAML` directives, line-number-anchored errors
- XML - `toXML()` / `fromXML()` - serialization and parsing with attributes (`@`-prefix), text content (`#text`), CDATA, comments, type detection (datetime/binary/objectid/regex/timestamp/extension)
- CSV - `toCSV()` with recursive flattening (`a.b.c`), two-pass union of keys for arrays of objects, `"` escaping per RFC 4180
- TOON - `toTOON()` / `fromTOON()` - token-oriented object notation with inline and tabular array formats, full round-trip serialization and parsing
- TRON - `toTRON()` / `fromTRON()` - token-reduced object notation with class definitions for repeated structures, class instantiation, inheritance, named arguments, round-trip support
- GOON - `toGOON()` / `fromGOON()` - greatly optimized object notation with YAML-like indentation, tabular arrays, inline/inference-based lists, single-char literals (T/F/_/~), dictionary references, round-trip support
- Protobuf - `toProtobuf()` / `fromProtobuf()` - Protocol Buffers binary wire format with schema-driven field mapping, packed fixed-size arrays; plus `toProtobufText()` / `fromProtobufText()` for human-readable text format
- TOML - `toTOML()` / `fromTOML()` - Tom's Obvious Minimal Language with tables, arrays of tables, inline tables/arrays, multi-line strings, hex/octal/binary integers, dotted keys, comments
- JSON Lines (NDJSON) - `toJSONLines()` / `fromJSONLines()` - one JSON value per line, ideal for streaming/ logs/bulk data
- S-Expression - `toSexpr()` / `fromSexpr()` - Lisp-style nested lists `(key "value" (nested 1 2 3))` with heuristic object/array detection, `;` comments, `nil`/`#t`/`#f` literals

### Standards
- JSON Pointer (RFC 6901)
- JSON Merge Patch (RFC 7396)
- JSON Patch (RFC 6902)

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
- Transparent hash/comparator (`std::equal_to<>`, `SafeHash::is_transparent`) for allocation-free `unordered_map` lookup on modern compilers
- Heterogeneous lookup helpers (`map_find`/`map_count`) with GCC < 9 fallback

### Thread Safety
- **Reading:** Fully thread-safe - multiple threads can read the same `asvJSON` instance simultaneously.
- **Writing:** Not thread-safe - a single `asvJSON` instance must not be written to concurrently from multiple threads.
- **Base64 custom charset:** Protected by `std::mutex` - `setBase64Chars()` and all encoding/decoding functions are safe to call concurrently from multiple threads.

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
| `bool parse(const std::string& json)` | Parse JSON from `std::string`. |
| `bool parse(std::string_view json)` | Parse JSON from `string_view` (zero-copy buffer). |
| `void clear()` | Clear all data, reset to empty state. |
| `std::string getLastError() const` | Return last error message. |
| *(field)* `bool allowNaNInfinity` | When `true`, parses/serializes `NaN`, `Infinity`, `-Infinity` (non-standard JSON). Default `false`. |

#### Serialization & File I/O

| Method | Description |
|--------|-------------|
| `std::string serialize(bool pretty = false) const` | Serialize to JSON string. |
| `std::string toXML() const` | Serialize to XML document. |
| `bool fromXML(std::string_view input)` | Parse XML string (elements, attributes, type detection, CDATA). |
| `std::string toYAML() const` | Serialize to YAML document. |
| `bool fromYAML(std::string_view input)` | Parse YAML string (tags, anchors/aliases, multi-doc, directives). |
| `std::string toCSV() const` | Serialize to CSV (flattened, two-pass for arrays of objects). |
| `std::string toTOON() const` | Serialize to TOON (token-oriented object notation). |
| `bool fromTOON(std::string_view input)` | Parse TOON string. |
| `std::string toTRON() const` | Serialize to TRON (token-reduced object notation). |
| `bool fromTRON(std::string_view input)` | Parse TRON string (class definitions, inheritance, named args). |
| `std::string toGOON() const` | Serialize to GOON (greatly optimized object notation). |
| `bool fromGOON(std::string_view input)` | Parse GOON string (indentation-based, tabular arrays, dictionary refs). |
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
| `void putBinary(key, const uint8_t* data, size_t len)` | Add binary data. |
| `void putBinary(key, const std::vector<uint8_t>& data)` | Add binary data. |
| `void putBinChunked(key, const uint8_t* data, size_t size, size_t chunk_size = 76)` | Split binary into Base64 chunks. |
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
| `getDateTimeString(key)` | `std::string` | ISO-8601 string. |
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
| `setBase64Chars(const std::string& chars)` | Set custom Base64 alphabet (64 chars). Thread-safe. |
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
items:
  - 10
  - 20
  - 30
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
- **Heuristic detection**: even-length list with string/symbol keys → object; otherwise → array
- **Nested structures**: `(person (name "Alice" age 25))`
- **Booleans**: `#t` (true), `#f` (false)
- **Null**: `nil`
- **Strings**: `"quoted"` with C-style escapes (`\n`, `\t`, `\"`, `\\`)
- **Comments**: `;` to end of line
- **Full round-trip**: `toSexpr()` then `fromSexpr()` returns the original data

## Changelog

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
