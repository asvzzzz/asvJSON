# Unified Data Exchange (UDE)

## Overview
UDE is a human-readable, machine-efficient format that combines features of JSON, YAML, INI, TOML and binary formats such as BSON or MessagePack. It aims to be:

- **Self-describing**  -  type information is embedded inline.
- **Extensible**  -  custom tags are supported without breaking compatibility.
- **Compact**  -  binary extensions reduce size for large payloads.
- **Strict yet flexible**  -  a *strict mode* rejects ambiguous constructs while the default mode remains permissive.

---

## 2. File Structure
A UDE file may contain one or more **documents** separated by a document separator line:

```text
---
```

or the legacy comment-style separators (still accepted for backward compatibility):

```text
// UDE document separator
// --- End of Document ---
```

A `...` line closes the current document (a following `---` opens the next one).
If no separator is present, the entire file is treated as a single document.
Multiple documents parse to a JSON array of document values; a leading `---`
does not create an empty first document.

**Note:** Document separation is handled as a pre-parsing step. The EBNF grammar defined in Appendix A is applied to each document chunk individually after the file is split by this separator.

Because a multi-document stream decodes to a single root array, re-encoding it writes that array as one document  -  the `---` separators are not recreated. The data round-trips unchanged (`stream -> array -> document` is stable), but the result is no longer a multi-document stream. Keep the original file if the separators themselves matter. Line numbers in errors are relative to the whole input, not to the individual document.

### 2.1 Header (Optional)
The first non-empty line can be an optional header that identifies the format and may include flags:

```text
# UDE v1.0

```
* `UDE`  -  literal marker.
* `v1.0`  -  version of the UDE specification. The major digit is a breaking change: documents declaring a major version other than `1` are rejected with an error. Minor versions are backward compatible, so older minor versions (e.g. `v1.0`) are accepted by a newer implementation.
* `strict`  -  optional flag that enables strict mode for the whole stream.
* Unknown flags are ignored so that forward-compatible minor versions keep working.

If no header is present, the document consists of a single plain-text line. The parser treats it as a string value. A line is only treated as plain text when it cannot be a structured document at all (no `:` separator and no structured start character); malformed or strict-mode-rejected documents report an error instead of degrading to plain text.

---

## 3. Lexical Elements
| Element | Syntax | Notes |
|---------|--------|-------|
| **Comment** | `# comment...`, `// comment...`, or multi-line `/* ... */` | Begins at first non-whitespace character and continues until end of line for single-line comments, or until the first closing `*/` for block comments. Block comments are **not nestable**  -  the first `*/` always closes the comment. Comments are ignored by the parser. A single-line or block comment may also follow a value on the same line, e.g. `key: value # note` or `key: value /* note */`; such trailing comments are ignored and are not part of the value (see the inline comment note in Appendix A).
| **Key** | Unquoted identifier (`[A-Za-z_][A-Za-z0-9_-]*`), quoted string (`"..."`), dotted key (`a.b.c`), or UNQUOTED_STRING (`[A-Za-z0-9._~+/=-]+`). | In *default* mode keys may be unquoted if they match the regex; in *strict* mode all keys must be quoted or escaped. Trailing commas are allowed, but duplicate keys produce an error only in strict mode.

Examples:
- `name: value`  -  simple key
- `a.b.c: 42`  -  dotted key interpreted as nested objects, equivalent to `a: {b: {c: 42}}`
- `user-name: admin`  -  UNQUOTED_STRING with hyphen

**Dotted keys** (`DOT_KEY ::= IDENTIFIER ('.' IDENTIFIER)+`) are a shorthand for nesting: each dot-separated segment creates one level of object. `a.b.c: 1` is the same object as `a: {b: {c: 1}}`. Because `a.b.c` also matches UNQUOTED_STRING, the lexer MUST prefer DOT_KEY (see the prioritization note in Appendix A) so dotted keys always produce nesting rather than a literal string key. To force a literal key containing dots, quote it: `"a.b.c": 1`.
| **Colon** | `:` | Separates key and value. Whitespace around colon is allowed.
| **String** | Double-quoted (`"..."`) supporting JSON escapes (`\n`, `\t`, `\r`, `\b`, `\f`, `\\`, `\"`, `\/`), byte escapes `\xHH`, and Unicode escapes `\uXXXX` (including surrogate pairs), `\UXXXXXXXX`, `\u{...}`. Single-quoted (`'...'`) strings are fully literal: the only escape is `''` for a literal quote. Both forms may span multiple lines; a backslash immediately before a line break inside a double-quoted string acts as an INI-style line continuation (joins lines and strips leading whitespace). Literal `\r\n` line breaks are normalized to `\n`. Unquoted strings are allowed and may contain only letters, digits, and `_ . ~ + / - =` (see `UNQUOTED_STRING` in Appendix A); they must not contain whitespace or any other punctuation. A leading `=` is discouraged (see the character note in Appendix A). In strict mode unquoted strings containing any non-alphanumeric character must be quoted.
| **Number** | Decimal, hexadecimal (`0x`), octal (`0o`), binary (`0b`). Floating point uses standard C-style notation. | No leading zeros unless `0` itself.
| **Boolean** | `true`, `false` (case-insensitive).
| **Null** | `null`.
| **Array start/end** | `[` and `]`. Elements separated by commas or newlines; a trailing comma is permitted in both modes and is safely ignored by the parser (does not create an extra null element). A single space between elements on one line (`[1 2]`) is also tolerated as an extension beyond the grammar's `SEP ::= WS? ',' WS? | NEWLINE`; the encoder always emits commas.
| **Object start/end** | `{` and `}`. Key/value pairs separated by commas or newlines; duplicate keys are only reported as errors when strict mode is enabled. In default (permissive) mode a duplicate key does **not** raise an error  -  the last occurrence silently overrides earlier values for that key (`insert_or_assign` semantics). A single space between pairs on one line (`{a: 1 b: 2}`) is also tolerated as an extension beyond the grammar's `SEP ::= WS? ',' WS? | NEWLINE`. |
| **Block scalar indicator** | `|` (literal) or `>` (folded). Followed by an optional indentation level and optional chomping indicators (`+`, `-`). If no chomping indicator is supplied, the default is to keep a single trailing newline (standard YAML behavior).
| **Anchor** | `&name`. Creates a reference that can be reused with `*name`.
| **Alias** | `*name`. Replaces the anchor with a deep copy of its value. An alias whose anchor is not defined yet (a forward reference, a typo, or a value that refers to itself) is an `undefined alias *<name>` error - genuine cycles are impossible because an anchor is registered only after its value has fully parsed. |
| **Tag** | `!type` prefix before any value (e.g., `!base64 "..."`). Tags are parsed by custom handlers; unknown tags are preserved as `CUSTOM_TAG` with name and value stored separately (Section 8.1). The parser does not enforce the semantics of a tag  -  validation is delegated to user callbacks.

### 3.1 String Escape Sequences

Inside double-quoted strings (`DQ_STRING`) the following escape sequences are recognized (see `ESC` in Appendix A):

| Escape | Code point / meaning |
|--------|----------------------|
| `\"` | Double quote (U+0022) |
| `\\` | Backslash (U+005C) |
| `\/` | Solidus (U+002F, optional, JSON-style) |
| `\b` | Backspace (U+0008) |
| `\f` | Form feed (U+000C) |
| `\n` | Line feed (U+000A) |
| `\r` | Carriage return (U+000D) |
| `\t` | Horizontal tab (U+0009) |
| `\xHH` | One byte, exactly two hex digits (`HH` in `00` - `FF`) |
| `\uXXXX` | One BMP code point, exactly four hex digits; valid surrogate pairs are joined automatically |
| `\UXXXXXXXX` | One code point in U+0000...U+10FFFF, exactly eight hex digits |
| `\u{...}` | One code point, one or more hex digits inside braces (Rust/JS style) |
| `\<newline>` | Line continuation: joins the current line with the next, stripping leading whitespace (INI style) |

Single-quoted strings (`SQ_STRING`) are fully literal: the only escape is `''`, which represents a literal single quote; no other backslash processing occurs.

A code point above U+10FFFF, an invalid surrogate pair, or a malformed escape (e.g. `\x` without two hex digits, a truncated `\u`/`\U`, or an unknown escape letter) is a parse error reported with the line number, e.g. `UDE: invalid escape sequence in string at line N`.

**Encoded documents** (Section 2) are decoded before parsing. If the byte content cannot represent a code point, the decoder substitutes U+FFFD REPLACEMENT CHARACTER rather than emitting an invalid UTF-8 sequence: an unpaired UTF-16 surrogate (a lone high or low surrogate) and a UTF-32 value above U+10FFFF all become U+FFFD. A surrogate *pair* still joins into the real character. This is the only lossy step in decoding, and it is deliberate  -  a lone surrogate has no UTF-8 encoding, so passing it through would produce bytes no consumer can read.

---


## 4. Data Types
UDE supports the following scalar types:

| Type | Representation | Example |
|------|----------------|---------|
| **Null** | `null` | `key: null` |
| **Boolean** | `true` / `false` | `enabled: true` |
| **Integer** | Decimal, hex (`0xFF`), octal (`0o77`), binary (`0b1011`); `_` may separate digits (`1_000`, `0xFF_FF`) | `count: 42`, `hex: 0x2A` |
| **Float** | Standard decimal or scientific notation; `_` may separate digits; a leading digit before the decimal point is required, so `.5` is a string | `ratio: 3.14e-2` |
| **Non-finite float** | `.nan`, `.inf`, `.infinity`, each optionally negated | `ratio: -.inf` |
| **String** | Quoted or unquoted (if safe) | `name: "John Doe"` |
| **Binary** | Tagged as `!base64 "..."` or `!bin 0xFFEE` | `payload: !base64 "SGVsbG8="` |
| **Array** | `[elem1, elem2]` or multi-line block | `list: [1, 2, 3]` |
| **Object** | `{key: value}` or explicit braces | `person: {name: Alice, age: 30}` |

JSON has no literal for a non-finite value, so one projects to `null` in JSON output; the spellings above are the UDE way of keeping it. A UDE round-trip of `.inf` gives a `DOUBLE` holding infinity, not the integer or string it started as.

A `_` digit separator is only legal *between* two digits, in all bases: `1_000` and `0b1010_1010` are numbers, `1_` and `_1` are plain strings. A number that overflows the destination type is not an error  -  it is kept as its original text in a STRING, so nothing is lost.

---

## 5. Arrays of Objects & Tabular Data
UDE allows arrays to contain objects that share the same set of keys. When an array contains only objects with identical key sets, it is treated as a *tabular* representation and can be printed in CSV-like form for human readability:

```text
people: [
  {name: Alice, age: 30},
  {name: Bob,   age: 25}
]
```
The parser treats this as an array of objects; the library may provide helper functions to access it as a table.

---

## 6. Block Scalars (Multi-line Strings)
Block scalars preserve line breaks and indentation. The syntax is:

```text
key: |
  Line one
  Line two
```
or
```text
key: >-
  Folded text that will become a single line.
```
Chomping indicators:
* `+`  -  keep all trailing newlines, including the line break that ends the last content line.
* `-`  -  strip all trailing newlines.
* none  -  clip: keep exactly one trailing newline. The guarantee is keyed on the block having a body at all, not on that body being non-empty text, so a block of nothing but blank lines is `"\n"` and not `""`. A header with no body whatsoever  -  immediately followed by end of file, or by a line that is not more indented than the header  -  is the empty string.

The encoder picks the indicator from the value it is writing: a value with no trailing newline becomes `|-`, a value with exactly one becomes `|` (the default), and a value with two or more becomes `|+`.

An optional explicit indentation indicator (`|2`, `>-3`) overrides the auto-detected base indentation. The digit comes **before** the chomping indicator and is exactly one digit, so `|2-` is valid and `|+2` is not -  a reversed order is a header error, not a silently accepted spelling.

The digit is **absolute**, not relative to the header: it names the column at
which the body starts, counted from the beginning of the line. So `key: |2`
takes a body indented by exactly 2 spaces, and `    key: |2` still takes a body
indented by exactly 2  -  the four extra spaces of the header line are not
added to it, and a body indented 6 spaces keeps the remaining 4 as value
prefix. Note that a conformant YAML reader counts the indicator *relative to
the parent node*, so it would read that last document as `data` where UDE reads
`    data`. The two agree only when the header sits at column 0, which is the
common case and the only one the encoder produces: the encoder never emits a
digit at all (it lets the base indent be auto-detected), so no UDE round-trip
depends on this reading. Prefer the implicit form when the value has to be
portable between UDE and YAML.

A `0` indicator (`key: |0`) is degenerate: with base indentation 0 the parser
strips **no** indentation from the body, and a body line at or above the
header's indentation terminates the scalar and is re-parsed as the next
document entry. `key: |0` followed by `next: 1` is therefore an empty `key`
and a separate `next`; a deeper body line keeps its full leading whitespace
(`key: |0` over `"  hello"` yields a value of two leading spaces + `hello`).
The parser accepts it (it is legal, just avoids stripping), but prefer any
positive indicator or the implicit form when the value has to be portable
between UDE and YAML.

`|` and `>` fold identically as far as chomping is concerned, so for the same body text they yield the same trailing breaks; they differ only in how the body's interior line breaks are treated (`>` turns a lone break between two non-empty lines into a space and keeps a run of k blank lines as k breaks).

Only whitespace, a comment, or the end of the line may follow the indicators, and all three comment forms are accepted there -  `key: | # note`, `key: > // note` and `key: | /* note */` are all valid. Accepting only two of them made `/* */` the single place in the grammar where a block comment was a syntax error. A block comment opened on the header line must close on that same line: one that would need to cross the line break is rejected with `UDE: unterminated comment after block scalar header at line N` rather than being allowed to swallow the body. Anything else after the indicators is `UDE: unexpected content after block scalar header at line N`.

Block scalar termination: the base indentation is the number of leading spaces
on the first non-empty line of the block. Every subsequent line must have at
least that many leading spaces; extra spaces are part of the value. A line with
fewer leading spaces terminates the scalar. Empty lines are **part of the
content** (paragraph breaks) and never terminate the scalar by themselves; they
are preserved, and the default (clip) chomping removes exactly one trailing
newline.

The base indentation is not free: the first non-empty line of the body must be
indented **deeper than the line carrying the header**, which is what keeps a
scalar from reaching out of its own value. `key: |\nnext: 1` is therefore the
empty string for `key` plus a separate `next: 1` key, not the text
`"next: 1\n"`. Taking the first non-empty line as the base unconditionally
would set the base to column 0, let a following top-level entry pass the
"dedent" test, and swallow it as content, silently losing a key. The floor is
the header line's own indentation, not column 0, so an indented key keeps an
indented body. An explicit indentation indicator is bound by the same floor.
Blank lines may still precede the body: they are content, not indentation.

The floor counts **spaces** on the header line. A header indented with a tab
has no structural indentation at all in UDE (Section 12: tabs are never
structural), so its floor is column 0 and any indented body is accepted. This is
not a hole in the tab rule: that rule governs the body's indentation margin,
which is checked separately and does reject a tab there
(`UDE: tab used as indentation in block scalar at line N`).

Tabs (Section 12): a tab is not allowed in the indentation margin of a block
scalar  -  the margin must consist of spaces only (YAML rule). A tab after the
margin is ordinary content. A **whitespace-only** body line is the one exception:
the margin is measured with spaces and tabs together on such a line, so enforcing
the spaces-only rule would reject a tab that is not acting as indentation at all.
A whitespace-only line is a blank line -  a paragraph break whose whitespace is
discarded anyway  -  and there is nothing for the margin rule to protect, so
`key: |` over a body containing `" \t "` is accepted. A tab used as real
indentation is still rejected.

---

## 7. Anchors and Aliases
Anchors create a named reference that can be reused later:

```text
default: &def {x: 1, y: 2}
point1: *def
point2: {x: 3, y: 4}
```
Aliases (`*name`) are replaced with a deep copy of the anchored value during parsing. An alias that has no matching anchor  -  whether it is a forward reference (referenced before the `&anchor` is defined) or a typo  -  is a hard error in **all** modes: the parser raises `UDE: undefined alias *<name>` and aborts. Anchor definition is therefore not deferred; the anchor must be present in the document. Because an anchor is registered only after its value has fully parsed, a value can never refer to itself or to an anchor defined later in the document, so genuine cycles are impossible: `a: &x *y` together with `b: &y *x` is caught as an `undefined alias`. Resolving an alias is a single deep copy of the anchored value (bounded by the clone node budget in Section 12) and never recurses into further anchor resolution, so there is no alias-chain depth limit.

### 7.1 Merge Keys

UDE supports the YAML-style merge key `<<`. A mapping may contain `<<: *anchor` (or `<<: [*a, *b]`) to inline the anchored object's members into the current object. Only the unquoted form `<<` merges. The anchored value must be an object or a sequence of objects; existing keys in the target object take precedence over merged keys, and for a sequence of objects later entries win. A non-object anchor or a sequence containing a non-object is a parse error (`UDE: merge key requires an object or sequence of objects`).

---

## 8. Tags and Extensions
Tags allow custom data types to be represented in a type-safe way:

| Tag | Syntax | Meaning |
|-----|--------|---------|
| `!int` | `!int <number>` | Coerces/validates the value as an integer; a non-integral numeric is rejected (`UDE: !int requires an integral value`). |
| `!float` | `!float <number>` | Coerces/validates the value as a floating-point number. |
| `!string` | `!string <value>` | Coerces the value to a string (e.g. `!string 42`  ->  `"42"`). |
| `!bool` | `!bool <true\|false>` | Coerces/validates a boolean value. |
| `!null` | `!null` | Produces an explicit null value. |
| `!base64` | `!base64 "<data>"` | Decoded binary data from base64 string. |
| `!bin` | `!bin 0x<hex>` | Binary data represented as hex literal. |
| `!datetime` | `!datetime "2026-07-30T14:45:00Z"` | ISO-8601 timestamp. |
| `!ext` | `!ext "<type>:<data>"` | Extension type with arbitrary payload. Type and data are combined into a single quoted value, because a tag takes exactly one VALUE (`TAGGED_VALUE ::= TAG WS? ANCHOR? WS? VALUE`); a two-token form like `!ext image/png "..."` would not parse. `<type>` is a signed byte and must be within `[-128, 127]`; anything else is rejected with `UDE: extension type out of range [-128, 127]`. |
| `!regex` | `!regex "__REGEX__<pattern>\|<opts>\|"` | Regular expression with optional flags. The canonical body is described below; the historical `!regex "<pattern>\|<opts>"` form is still accepted. |
| `!schema` | `!schema <string\|object>` | Schema reference or inline schema; stored as a custom tag with name `schema` (see 8.1). |

These **explicit type-cast tags** (`!int`, `!float`, `!string`, `!bool`, `!null`) force a specific runtime type on the following value and validate it; a value of the wrong type is a parse error. They are the UDE equivalent of YAML's explicit type tags.

**`!null` needs no value.** A tag's value may not live on a later line (`WS` is `[ \t]*` and does not include a line break), so `!null` is recognised before any whitespace/comment skipping that could cross the line end. A bare `!null` is valid at the end of a line, at the end of the input, and directly before a flow delimiter, which is what makes `[!null, 1]` and `{y: !null, z: 1}` work:

```text
a: !null          # end of line
[!null, 1]        # before ','
{y: !null}        # before '}'
```

Every other tag still requires a value; a bare `!int` is rejected with `UDE: !int requires a value`. The anchored spelling `!null &a` is accepted the same way, as a deliberate extension beyond Appendix A, whose `VALUE` is mandatory: a bare `!null` already carries no value, so this is the consistent reading, and an anchor with nothing to point at would otherwise be unrepresentable. Every other tag still requires one after an anchor (`!int &a` is rejected).

`!float` additionally rejects a numeric literal that no `double` can hold exactly (an out-of-`int64` integer, or more significant digits than a double preserves) with `UDE: !float requires a literal representable as a double`. `!int` rejects the same input with `!int requires an integer literal`, so `!float` is not laxer than the integer path. A DOUBLE that is NaN or Inf, and a `DATETIME` the platform cannot convert, both serialize as `null` (see [Non-JSON types](#non-json-types)) and come back as a null value / the string `"null"` rather than as a DOUBLE / DATETIME -  unavoidable, since UDE has no encoding for either.

#### `!regex` body forms

The pattern/options separator has to be located in the **raw** token text, before escape processing, otherwise a `|` written as `\u007C` inside a pattern is indistinguishable from the separator. The canonical body therefore escapes every literal pipe as `\u007C` and ends with a mandatory terminator:

```text
!regex "__REGEX__^(a\u007Cb)$|i|"     # pattern ^(a|b)$, options i
```

The `__REGEX__` marker is what makes the canonical form unambiguous. A body **without** the marker is the historical form and keeps its old meaning  -  the decoded text split at the **last** `|`  -  so an existing `!regex "a|b|c"` still means pattern `a|b` with options `c` instead of being read as canonical and silently truncated. A pattern that needs a literal pipe is emitted as `\u007C`, and the last column below is literally what the encoder writes; the bare `|` characters in it are the separator and the terminator, not part of a field:

| Pattern | Options | Emitted body |
|---------|---------|--------------|
| `^(a\|b)$` | (none) | `__REGEX__^(a\u007Cb)$\|\|` |
| `a\|` | (none) | `__REGEX__a\u007C\|\|` |
| `a` | `i` | `__REGEX__a\|i\|` |

The canonical split takes the **first** raw `|` as the separator and the **last**
as the mandatory terminator, which is exactly why a literal pipe has to be
written as an escape: it is what guarantees the first raw `|` is the separator
and never part of a field.

**Quoting style.** The canonical marker is honoured in either quoting style:
`!regex '__REGEX__abc|i|'` reads the same as its double-quoted twin, and the
fields are then decoded with the rules of the style actually written (only a
double-quoted body knows `\u007C`, `\n` and the rest; a single-quoted one knows
only the `''` doubling). The consequence to keep in mind: because a
single-quoted body has no `\u007C` escape, **a literal pipe in a pattern is not
representable in single quotes**. Such a body falls back to the historical
last-`|` rule and reads as options, e.g. `!regex 'a|b'` is pattern `a`, options
`b`  -  the same reading an unmarked double-quoted body gets. Use the
double-quoted canonical form for a pattern that contains a pipe. The encoder
always writes the double-quoted form.

> **Note on `!!value`:** UDE does **not** support the YAML `!!value` / `!!yaml` core-schema tags. UDE tags are always single-token `!name` forms or verbatim `!<uri>` forms (Section 8.1). The absence of `!!value` is intentional and is **not** a specification discrepancy.

The library provides handlers for the standard tags. **Unknown tags** follow the *save* policy (the default of the three possible behaviors *error / save / ignore*): the tag and its value are preserved without data loss.

### 8.1 Custom Tags (canonical form)
An unknown tag stores its **name separately from its value**:

```text
!<name> <value>
```

* The name is a plain identifier, an optional namespace form `!ns:tag` / `!a.b`, or a verbatim URI in angle brackets `!<tag:example.com,2026:type>`.
* Every `.`/`:`-separated component of a non-verbatim name must start with an identifier character, so `!x`, `!_x`, `!ns:tag` and `!a.b` are valid while `!1x`, `!-x`, `!.a` and `!a.` are rejected with `UDE: invalid tag name`. A name the grammar cannot spell has no `!name` form, which is exactly what the verbatim form is for: the encoder writes such a name as `!<name>` so it still round-trips. The verbatim body may be empty (`!<>`), which is how an empty tag name is represented; `!<` cannot be confused with a simple name.
* The value is any UDE value (string, number, object, array, ...), not flattened text  -  `!point {x: 1, y: 2}` keeps the object intact.
* **The angle brackets mean "this name is a literal custom-tag name", not "a synonym for the simple form".** That is what lets a custom tag whose name collides with a built-in be expressed at all:

```text
!regex "^(a|b)$"      # the built-in REGEX
!<regex> "^(a|b)$"    # a CUSTOM_TAG whose name happens to be "regex"
```

The decoder dispatches on the tag name, so a custom tag named after a built-in is unreachable through `!name`; the encoder therefore emits the verbatim form for such a name, and the verbatim form suppresses the dispatch. `!<null>` with no value is consequently rejected like any other tag (`UDE: !<null> requires a value`)  -  only the simple `!null` is value-less. `CUSTOM_TAG` is a type in the JSON convention, so a document that wants a genuine `null` there uses `{"name":"null","value":null}`.
* In JSON (Extended JSON convention) a custom tag is represented as:

```json
{"$customTag":{"name":"custom","value":"some value"}}
```

* `!schema <string|object>` is stored the same way (tag name `schema`, value kept structured) so schema references and inline schemas round-trip.

Unknown tags are processed by user-defined callbacks via the `CUSTOM_TAG` value type.

---

## 9. Strict Mode
A parser can be instantiated in **strict mode** where the following rules apply:

* Duplicate keys in an object are errors.
* All keys must be quoted (`"..."`); unquoted keys are rejected (Section 3).
* Unquoted values that contain special characters are rejected.
* Mixed type arrays (e.g., `[1, "two"]`) are allowed; schema validation may reject them if desired.
* Anchors must be defined before they are referenced (an undefined alias is rejected in all modes, not only strict  -  see Section 7).

In strict mode the **serializer** also never emits block (folded) scalars:
multiline strings are written as quoted, JSON-escaped strings, because a block
scalar's body is unquoted content and strict documents must remain fully
quotable by construction. (The strict *parser* still accepts `|` / `>` values
for compatibility with documents produced by other tools.)

Strict mode is useful when data integrity is critical; the default permissive mode aims for maximum compatibility with existing YAML/INI files.

---

## 10. Example Document
```text
# UDE v1.0

name: "Example Document"
created_at: !datetime "2026-07-30T14:45:00Z"
enabled: true
tags: [sample, demo]
config: { timeout: 30, retries: 5 }
messages: |
  Line one.
  Line two.
  Line three.
payload: !base64 "SGVsbG8gd29ybGQ="
people: [
  {name: Alice, age: 30},
  {name: Bob,   age: 25}
]
---
```
---

## 11. Compatibility Notes
* **JSON**  ->  valid UDE (no comments or tags).
* **YAML**  ->  Simple YAML files are often valid UDE. Complex YAML features (implicit typing like ~ for null, or complex keys) require manual adaptation.
* **INI**  ->  converted to flat objects; nested sections become dot-notation keys.
* **BSON/MessagePack**  ->  represented via `!bin` or `!base64` tags.

---

## 12. Limits and Resources
The parser enforces documented resource limits:

| Limit | Value | Notes |
|-------|-------|-------|
| Anchor resolution | single deep copy | An alias is resolved by copying the anchored value (bounded by the clone node budget); it never recurses into further alias resolution, so there is no alias-chain depth limit (Section 7). |
| Number of anchors | 1,000,000 | `too many anchors` error once the per-document count is reached. |
| Object/array nesting | core `MAX_NESTING_DEPTH` | Shared with the JSON parser, including the counting convention: the root container is level 1 and scalars do not count, so the same tree the JSON parser accepts is one `toUDE()` serializes. A tree built past the limit by hand makes `toUDE()` throw `UDE: maximum nesting depth exceeded` rather than emit a truncated document. |
| String length | 10 MiB | Shared core limit (`MAX_STRING_LEN`). The limit is checked on the **decoded** value, so it applies to every string form: double-quoted, single-quoted, block scalar, and the quoted body of `!string` / `!regex` / `!base64`. Exceeding it is reported as `UDE: string too long`. The check lives in the string factory rather than at each call site, so it also covers the values `!string` builds by conversion (a number or a boolean rendered as text) even though those are orders of magnitude below the limit. |
| Object/array size | 1,000,000 entries | Shared core limit. |

Tabs are **not** structural indentation anywhere in UDE (YAML rule). They are
accepted as ordinary whitespace separators between tokens; inside a block
scalar a tab in the indentation margin is an error (Section 6), while tabs
after the margin are content.

---

## 13. Error Handling

UDE parsing is fail-fast: the parser reports a problem by throwing a C++ exception of type `asvJSONError`. The message always begins with the prefix `UDE: ` and, where the position is known, ends with ` at line <N>` giving the 1-based line number in the source document. Column information is **not** tracked, and there is no numeric error-code enumeration  -  the message text itself is the diagnostic.

The following conditions raise an error (non-exhaustive; messages are verbatim):

* **Syntax / structure**  -  `UDE: expected ':' after key ...`, `UDE: expected key at line N`, `UDE: expected value at line N`, `UDE: unterminated object/array/string/single-quoted string/tag URI/block comment at line N`, `UDE: unexpected top-level collection at line N`, `UDE: trailing content after block scalar`, `UDE: trailing content after top-level collection`.
* **Comments**  -  `UDE: unterminated block comment at line N` is raised wherever an unclosed `/*` appears, including inside the lookahead that decides whether a tag is followed by a value. Answering "the line ends here" for an unclosed comment would turn the real fault into a misleading `UDE: !null requires a value` (or a silent plain `null` for a bare `!null`).
* **Block scalar**  -  `UDE: tab used as indentation in block scalar at line N` (a tab in the indentation margin; spaces only are allowed).
* **Numbers / escapes**  -  `UDE: invalid \x escape in string`, `UDE: invalid \u/\U escape in string`, `UDE: code point out of range in string`, `UDE: unterminated \u{...} escape in string`, `UDE: invalid \u{...} escape in string`, `UDE: invalid code point in string`, `UDE: invalid escape sequence \<c> in string`, `UDE: invalid escape at end of string`. These are raised by the escape decoder, which names the escape it rejected; the parser that called it appends ` at line <N>` and re-throws it as an `asvJSONError`, so the decoder's text reaches the user intact. A length failure (`UDE: string too long`, Section 12) is raised by the string factory, deliberately outside that wrapper, so it is never relabelled as a bad escape.
* **Anchors / aliases**  -  `UDE: undefined alias *<name>`, `UDE: too many anchors`, `UDE: failed to store anchor &<name>`, `UDE: merge key requires an object or sequence of objects`.
* **Duplicate keys**  -  `UDE: duplicate key '<key>' in strict mode` (default mode keeps the last value silently).
* **Strict mode**  -  `UDE: strict mode requires all keys to be quoted (key: ...)`, `UDE: strict mode requires quoting value: <tok>`, `UDE: missing value after ':' at line N`.
* **Tags**  -  e.g. `UDE: !int requires an integral value`, `UDE: !base64 requires a string value`, `UDE: !datetime requires a string value`, `UDE: !ext requires "type:data"`, `UDE: extension type out of range [-128, 127]`, `UDE: invalid base64 data`, `UDE: invalid regex`, `UDE: invalid tag name '!<name>' at line N`, `UDE: <tag> requires a value` (a tag written without a value other than `!null`), `UDE: unterminated tag URI at line N`.
* **Resource limits**  -  `UDE: array too large`, `UDE: binary data too large`, `UDE: extension data too large`, `UDE: string too long`, `UDE: too many documents` (see Sec. 12).
* **Header / version**  -  `UDE: malformed version header`, `UDE: unsupported UDE version vX.Y` (a major version other than `1` is rejected).

There is no error-recovery mode: the first error aborts parsing. Callers that need partial results must catch the exception; the partially built DOM is not returned.

---

## 14. Future Work
* Standardized tag registry for third-party extensions.
* Schema validation (optional JSONSchema-like validator).
* Streaming parser for large documents.
* External URI references (`!uri` tag) for out-of-line binary payloads.
* Formal, machine-checkable error codes (currently diagnostics are free-form strings).
* Configurable pretty-print options (indent width, tab indentation, key sorting, line wrapping).

---

## Appendix A  -  Formal Grammar
The following EBNF describes the lexical and syntactic rules of UDE.  It is intended to be unambiguous and suitable as a reference for implementation writers.

### Tokens

# Token priority for values (highest to lowest)
# BOOLEAN > NULL > NUMBER > ALIAS > ANCHOR > TAG > STRING > IDENTIFIER > UNQUOTED_STRING
```
COMMENT           ::= "#"[^\n]* | "//"[^\n]*
BLOCK_COMMENT     ::= "/*" (NOT_STAR | "*" NOT_SLASH)* "*/"

# Where
#   NOT_STAR  = any character except '*'
#   NOT_SLASH = any character except '/'
COLON             ::= ':'
# String semantics
# DQ_STRING: JSON escapes plus \xHH (byte), \uXXXX (with surrogate pairs),
# \UXXXXXXXX, \u{...} (code point), and line continuation '\' immediately before
# a NEWLINE (joins lines, stripping leading whitespace). Literal \r\n inside a
# multiline string is normalized to \n.
# SQ_STRING: fully literal; the only escape is "''" (a literal quote). No
# backslash processing is applied.
STRING            ::= DQ_STRING | SQ_STRING
DQ_STRING         ::= '"' (ESC | ~["\\])* '"'
SQ_STRING         ::= "'" (SQ_ESC | ~['])* "'"
SQ_ESC            ::= "''"
ESC               ::= '\\' ["\\/bfnrt] | '\\x' HEXDIG HEXDIG | '\\u' HEXDIG{4} | '\\U' HEXDIG{8} | '\\u{' HEXDIG+ '}' | '\\' NEWLINE
HEXDIG            ::= [0-9a-fA-F]
NUMBER            ::= SIGNED_INT | SIGNED_FLOAT | HEX | OCTAL | BINARY | NON_FINITE
SIGNED_INT        ::= '-'? INT
SIGNED_FLOAT      ::= '-'? FLOAT
INT               ::= DIGITS | DIGITS '_' DIGITS
FLOAT             ::= DIGITS ("." [0-9]*)? ([eE][+-]?[0-9]+)?
DIGITS            ::= '0' | [1-9][0-9]* ( '_' [0-9]+ )*
# Note: A leading digit before the decimal point is required. Forms like '.5' or
# '-.5' are not valid FLOAT literals; they are lexed as UNQUOTED_STRING instead.
# This is a deliberate restriction to avoid ambiguity with dotted keys.
# Digit separators: '_' is allowed between digits and is removed from the value,
# so "1_000" is 1000. A '_' must sit between two digits; "1_" and "_1" are not
# numbers. The same rule applies inside HEX, OCTAL and BINARY.
NON_FINITE        ::= '-'? ( '.nan' | '.inf' | '.infinity' )
# Note: JSON has no literal for these, so a DOUBLE holding a non-finite value
# projects to null in JSON output. UDE therefore gives them a first-class
# spelling, and a non-finite DOUBLE survives a UDE round-trip (its type is still
# DOUBLE afterwards). ".nan" without the dot is an ordinary unquoted string.
HEX               ::= '-'? "0x" DIGITS
OCTAL             ::= '-'? "0o" DIGITS
BINARY            ::= '-'? "0b" DIGITS
# Note: The optional leading '-' is part of the HEX/OCTAL/BINARY token itself.
# In a longest-match lexer, '-0xFF' is lexed as a single HEX token (length 5),
# not as SIGNED_INT '-' followed by an invalid '0xFF'.
IDENTIFIER        ::= [A-Za-z_][A-Za-z0-9_-]*
UNQUOTED_STRING   ::= [A-Za-z0-9._~+/=-]+
# Character note: '-' and '_' are always allowed in UNQUOTED_STRING (and in
# IDENTIFIER). '=' is permitted in the token but is discouraged as the first
# character of a key to avoid INI-style key=value ambiguity; a key starting
# with '=' should be quoted. This reconciles the token definition with the
# statement in Section 3 that unquoted strings contain "no special characters".

TAG               ::= '!' ( IDENTIFIER (':' IDENTIFIER | '.' IDENTIFIER)* | '<' TAG_URI '>' )
TAG_URI           ::= [^>]+  // verbatim tag URI, e.g. tag:example.com,2026:type
ANCHOR            ::= '&' IDENTIFIER
ALIAS             ::= '*' IDENTIFIER
WHITESPACE        ::= [ \t]+  // spaces and tabs
NEWLINE           ::= '\r'? '\n'
WS                ::= WHITESPACE*
SPACES            ::= ' '+  // spaces only; tabs are NOT allowed in the indentation margin of a block scalar
VERSION_STRING    ::= [0-9]+ '.' [0-9]+  // e.g., 1.0
```### Grammar
```
DOCUMENT            ::= (HEADER NEWLINE)? (DOC_LINE NEWLINE)* DOC_LINE? | PLAIN_TEXT
PLAIN_TEXT          ::= ~[\n]*
# Priority note: the structured alternative is attempted first. Because its
# trailing DOC_LINE is optional, it can match an *empty* document. Therefore a
# document whose remaining content is a single non-empty line that fails to
# parse as DOC_LINE (e.g. "hello world" with no colon) MUST fall through to
# PLAIN_TEXT rather than being accepted as an empty document with dangling text.
DOC_LINE          ::= WS? (LINE | BLOCK_COMMENT | COMMENT)
HEADER            ::= '# UDE v' VERSION_STRING
LINE              ::= WS (KEY COLON VALUE | BLOCK_SCALAR | COLLECTION)
COLLECTION        ::= OBJECT | ARRAY
OBJECT            ::= '{' WS (PAIRS)? WS '}'
ARRAY             ::= '[' WS (VALUES)? WS ']'
SEP               ::= WS? ',' WS? | NEWLINE

# Separator usage note
# Mixing commas and newlines in the same array or object is allowed syntactically
# but discouraged, as it can lead to inconsistent formatting across parsers.

VALUES            ::= VALUE (SEP VALUE)* SEP?
PAIRS             ::= PAIR (SEP PAIR)* SEP?
PAIR              ::= KEY WS COLON WS VALUE (WS? COMMENT)?
# Inline comment note: a single-line (# or //) or block (/* */) comment may
# also follow the VALUE on the same line, e.g. `key: value # note` or
# `key: value /* note */`. Such comments are ignored by the parser and are
# NOT part of VALUE. They appear only outside quoted strings; a '#' inside a
# double- or single-quoted string is literal text. (The parser skips them via
# skipWsAndComments() after the value.)
KEY   ::= IDENTIFIER | STRING | DOT_KEY | UNQUOTED_STRING

# Prioritization note
# When parsing a key that could match both DOT_KEY and UNQUOTED_STRING (e.g., "a.b.c"), the lexer/parser must attempt to match DOT_KEY first.  This guarantees that dotted keys are interpreted as nested structures rather than simple strings, ensuring deterministic behaviour across implementations.

DOT_KEY           ::= IDENTIFIER ('.' IDENTIFIER)+
VALUE             ::= STRING | NUMBER | BOOLEAN | NULL | UNQUOTED_STRING | ALIAS | TAGGED_VALUE | ANCHORED_VALUE | COLLECTION | BLOCK_SCALAR
TAGGED_VALUE      ::= TAG WS? ANCHOR? WS? VALUE
ANCHORED_VALUE    ::= ANCHOR WS? VALUE

BLOCK_SCALAR     ::= SCALAR_INDICATOR INDENT_DIGIT? CHOMP_INDICATOR? WS? NEWLINE INDENTED_TEXT

# Note: An optional SINGLE decimal digit may precede the chomp indicator,
# mirroring YAML syntax (e.g., '|2-' or '>3+'). The indicator is exactly one
# digit in 0-9; multi-digit or signed values are NOT permitted.

# Folded Scalar Semantics
# Note: In a folded scalar a line break is turned into a space only when exactly
# one break separates two non-empty lines; chomping then applies to the folded
# result. This mirrors YAML behaviour.
# When the scalar indicator is '>' (folded), the following rules apply:
# * A single NEWLINE between two non-empty lines is replaced by a space.
# * Empty lines are part of the content: a run of k blank lines between
#   non-empty lines yields k NEWLINEs, exactly as in YAML block folding (so one
#   blank line separates paragraphs and two keep a blank line between them).
# * The last non-empty line's own terminating NEWLINE is part of the content,
#   which is what lets '+' chomping preserve it and clip chomping keep exactly
#   one. Collapsing a blank-line run, or dropping that final break, would make
#   '>' disagree with '|' for the same text.
# * The resulting string may be further processed by the tag handlers if present.
# These semantics are identical to YAML's folded scalar rules and allow
# multi-line values to be stored as a single logical line in UDE.
SCALAR_INDICATOR  ::= '|' | '>'
CHOMP_INDICATOR   ::= '+' | '-'
INDENT_DIGIT      ::= [0-9]
INDENTED_TEXT     ::= (SPACES* TEXT_LINE NEWLINE)* SPACES* TEXT_LINE

# Indentation handling
# Each line in a block scalar must have at least the base indentation
# The first non-empty line determines the base indent; subsequent lines with
# less indentation terminate the scalar. Empty lines are part of the content
# (paragraph breaks) and never terminate the scalar by themselves
# (Section 6); a trailing blank line supplies the single line break that clip
# chomping keeps.
# Floor: the first non-empty line must be indented deeper than the header line
# itself, so a body cannot reach out of its own value ('key: |' followed by
# 'next: 1' is an empty scalar plus a separate key). An explicit
# INDENT_DIGIT is bound by the same floor. Blank lines may precede the body.
# Clip: the trailing break is guaranteed whenever the block had a body at all,
# including a body of only blank lines; only a header with no body yields "".
# Example:
#   key: |
#     alpha
#       beta
#     gamma
#   next: 1
# base indent = 4 (two leading spaces on "alpha"). "  beta" contributes
# 2 extra spaces ("  beta" -> "beta" with 2 leading spaces in value);
# "gamma" contributes none; "next: 1" has indent 2 < 4 and terminates
# the scalar.
TEXT_LINE         ::= ~[\n]*

/* Block Scalar Semantics */
/* The base indentation is the number of leading spaces on the first line of the block scalar. All subsequent lines must have at least this many spaces; any excess spaces are part of the value.
   Empty lines are part of the content (paragraph breaks) and never terminate the scalar. The block scalar ends when a line with fewer leading spaces than the base indentation appears, or at EOF.
   Tabs are not allowed in the indentation margin; the margin must be spaces only. */
BOOLEAN           ::= [tT][rR][uU][eE] | [fF][aA][lL][sS][eE]
NULL              ::= [nN][uU][lL][lL]
```
### Security constraints
* Alias resolution is a single deep copy of the anchored value (bounded by the clone node budget); it cannot recurse, so there is no alias-chain depth limit.
* Genuine cycles are impossible: an anchor is registered only after its value has fully parsed, so a value can never refer to itself or to an anchor defined later - `a: &x *y` together with `b: &y *x` is reported as `UDE: undefined alias *<name>`, never recursed.

---

## 15. License
UDE is released under the MIT license. The specification text itself is public domain.

---

End of UDE Specification.