# Unified Data Exchange (UDE)

## Overview
UDE is a human‑readable, machine‑efficient format that combines features of JSON, YAML, INI, TOML and binary formats such as BSON or MessagePack. It aims to be:

- **Self‑describing** – type information is embedded inline.
- **Extensible** – custom tags are supported without breaking compatibility.
- **Compact** – binary extensions reduce size for large payloads.
- **Strict yet flexible** – a *strict mode* rejects ambiguous constructs while the default mode remains permissive.

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

**Note:** Document separation is handled as a pre‑parsing step. The EBNF grammar defined in Appendix A is applied to each document chunk individually after the file is split by this separator.

### 2.1 Header (Optional)
The first non‑empty line can be an optional header that identifies the format and may include flags:

```text
# UDE v1.0

```
* `UDE` – literal marker.
* `v1.0` – version of the UDE specification. The major digit is a breaking change: documents declaring a major version other than `1` are rejected with an error. Minor versions are backward compatible, so older minor versions (e.g. `v1.0`) are accepted by a newer implementation.
* `strict` – optional flag that enables strict mode for the whole stream.
* Unknown flags are ignored so that forward‑compatible minor versions keep working.

If no header is present, the document consists of a single plain‑text line. The parser treats it as a string value. A line is only treated as plain text when it cannot be a structured document at all (no `:` separator and no structured start character); malformed or strict‑mode‑rejected documents report an error instead of degrading to plain text.

---

## 3. Lexical Elements
| Element | Syntax | Notes |
|---------|--------|-------|
| **Comment** | `# comment…`, `// comment…`, or multi‑line `/* … */` | Begins at first non‑whitespace character and continues until end of line for single‑line comments, or until the first closing `*/` for block comments. Block comments are **not nestable** — the first `*/` always closes the comment. Comments are ignored by the parser. A single‑line or block comment may also follow a value on the same line, e.g. `key: value # note` or `key: value /* note */`; such trailing comments are ignored and are not part of the value (see the inline comment note in Appendix A).
| **Key** | Unquoted identifier (`[A-Za-z_][A-Za-z0-9_-]*`), quoted string (`"..."`), dotted key (`a.b.c`), or UNQUOTED_STRING (`[A-Za-z0-9._~+/=-]+`). | In *default* mode keys may be unquoted if they match the regex; in *strict* mode all keys must be quoted or escaped. Trailing commas are allowed, but duplicate keys produce an error only in strict mode.

Examples:
- `name: value` – simple key
- `a.b.c: 42` – dotted key interpreted as nested objects, equivalent to `a: {b: {c: 42}}`
- `user-name: admin` – UNQUOTED_STRING with hyphen

**Dotted keys** (`DOT_KEY ::= IDENTIFIER ('.' IDENTIFIER)+`) are a shorthand for nesting: each dot‑separated segment creates one level of object. `a.b.c: 1` is the same object as `a: {b: {c: 1}}`. Because `a.b.c` also matches UNQUOTED_STRING, the lexer MUST prefer DOT_KEY (see the prioritization note in Appendix A) so dotted keys always produce nesting rather than a literal string key. To force a literal key containing dots, quote it: `"a.b.c": 1`.
| **Colon** | `:` | Separates key and value. Whitespace around colon is allowed.
| **String** | Double-quoted (`"..."`) supporting JSON escapes (`\n`, `\t`, `\r`, `\b`, `\f`, `\\`, `\"`, `\/`), byte escapes `\xHH`, and Unicode escapes `\uXXXX` (including surrogate pairs), `\UXXXXXXXX`, `\u{...}`. Single-quoted (`'...'`) strings are fully literal: the only escape is `''` for a literal quote. Both forms may span multiple lines; a backslash immediately before a line break inside a double-quoted string acts as an INI-style line continuation (joins lines and strips leading whitespace). Literal `\r\n` line breaks are normalized to `\n`. Unquoted strings are allowed and may contain only letters, digits, and `_ . ~ + / - =` (see `UNQUOTED_STRING` in Appendix A); they must not contain whitespace or any other punctuation. A leading `=` is discouraged (see the character note in Appendix A). In strict mode unquoted strings containing any non‑alphanumeric character must be quoted.
| **Number** | Decimal, hexadecimal (`0x`), octal (`0o`), binary (`0b`). Floating point uses standard C‑style notation. | No leading zeros unless `0` itself.
| **Boolean** | `true`, `false` (case‑insensitive).
| **Null** | `null`.
| **Array start/end** | `[` and `]`. Elements separated by commas or newlines; a trailing comma is permitted in both modes and is safely ignored by the parser (does not create an extra null element).
| **Object start/end** | `{` and `}`. Key/value pairs separated by commas or newlines; duplicate keys are only reported as errors when strict mode is enabled. In default (permissive) mode a duplicate key does **not** raise an error — the last occurrence silently overrides earlier values for that key (`insert_or_assign` semantics).
| **Block scalar indicator** | `|` (literal) or `>` (folded). Followed by an optional indentation level and optional chomping indicators (`+`, `-`). If no chomping indicator is supplied, the default is to keep a single trailing newline (standard YAML behavior).
| **Anchor** | `&name`. Creates a reference that can be reused with `*name`.
| **Alias** | `*name`. Replaces the anchor with its value; aliasing an alias is disallowed and will raise a cycle‑detected error. Maximum anchor depth is 100 levels.
| **Tag** | `!type` prefix before any value (e.g., `!base64 "…"`). Tags are parsed by custom handlers; unknown tags are preserved as `CUSTOM_TAG` with name and value stored separately (Section 8.1). The parser does not enforce the semantics of a tag – validation is delegated to user callbacks.

### 3.1 String Escape Sequences

Inside double‑quoted strings (`DQ_STRING`) the following escape sequences are recognized (see `ESC` in Appendix A):

| Escape | Code point / meaning |
|--------|----------------------|
| `\"` | Double quote (U+0022) |
| `\\` | Backslash (U+005C) |
| `\/` | Solidus (U+002F, optional, JSON‑style) |
| `\b` | Backspace (U+0008) |
| `\f` | Form feed (U+000C) |
| `\n` | Line feed (U+000A) |
| `\r` | Carriage return (U+000D) |
| `\t` | Horizontal tab (U+0009) |
| `\xHH` | One byte, exactly two hex digits (`HH` in `00`–`FF`) |
| `\uXXXX` | One BMP code point, exactly four hex digits; valid surrogate pairs are joined automatically |
| `\UXXXXXXXX` | One code point in U+0000…U+10FFFF, exactly eight hex digits |
| `\u{…}` | One code point, one or more hex digits inside braces (Rust/JS style) |
| `\<newline>` | Line continuation: joins the current line with the next, stripping leading whitespace (INI style) |

Single‑quoted strings (`SQ_STRING`) are fully literal: the only escape is `''`, which represents a literal single quote; no other backslash processing occurs.

A code point above U+10FFFF, an invalid surrogate pair, or a malformed escape (e.g. `\x` without two hex digits, a truncated `\u`/`\U`, or an unknown escape letter) is a parse error reported with the line number, e.g. `UDE: invalid escape sequence in string at line N`.

---


## 4. Data Types
UDE supports the following scalar types:

| Type | Representation | Example |
|------|----------------|---------|
| **Null** | `null` | `key: null` |
| **Boolean** | `true` / `false` | `enabled: true` |
| **Integer** | Decimal, hex (`0xFF`), octal (`0o77`), binary (`0b1011`) | `count: 42`, `hex: 0x2A` |
| **Float** | Standard decimal or scientific notation | `ratio: 3.14e-2` |
| **String** | Quoted or unquoted (if safe) | `name: "John Doe"` |
| **Binary** | Tagged as `!base64 "…"` or `!bin 0xFFEE` | `payload: !base64 "SGVsbG8="` |
| **Array** | `[elem1, elem2]` or multi‑line block | `list: [1, 2, 3]` |
| **Object** | `{key: value}` or explicit braces | `person: {name: Alice, age: 30}` |

---

## 5. Arrays of Objects & Tabular Data
UDE allows arrays to contain objects that share the same set of keys. When an array contains only objects with identical key sets, it is treated as a *tabular* representation and can be printed in CSV‑like form for human readability:

```text
people: [
  {name: Alice, age: 30},
  {name: Bob,   age: 25}
]
```
The parser treats this as an array of objects; the library may provide helper functions to access it as a table.

---

## 6. Block Scalars (Multi‑line Strings)
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
* `+` – keep all trailing newlines.
* `-` – strip all trailing newlines.
No indicator defaults to keeping one newline (YAML default).

An optional explicit indentation indicator `|2`, `|0` (a digit right after the
`|`/`>`) overrides the auto‑detected base indentation.

Block scalar termination: the base indentation is the number of leading spaces
on the first non‑empty line of the block. Every subsequent line must have at
least that many leading spaces; extra spaces are part of the value. A line with
fewer leading spaces terminates the scalar. Empty lines are **part of the
content** (paragraph breaks) and never terminate the scalar by themselves; they
are preserved, and the default (clip) chomping removes exactly one trailing
newline.

Tabs (Section 12): a tab is not allowed in the indentation margin of a block
scalar — the margin must consist of spaces only (YAML rule). A tab after the
margin is ordinary content.

---

## 7. Anchors and Aliases
Anchors create a named reference that can be reused later:

```text
default: &def {x: 1, y: 2}
point1: *def
point2: {x: 3, y: 4}
```
Aliases (`*name`) are replaced with the anchored value during parsing. An alias that has no matching anchor — whether it is a forward reference (referenced before the `&anchor` is defined) or a typo — is a hard error in **all** modes: the parser raises `UDE: undefined alias *<name>` and aborts. Anchor definition is therefore not deferred; the anchor must be present in the document. Cycles (`*a` resolving to a value that contains `*a`) are likewise rejected with a cycle‑detected error; the maximum resolution depth is 100 levels.

### 7.1 Merge Keys

UDE supports the YAML‑style merge key `<<`. A mapping may contain `<<: *anchor` (or `<<: [*a, *b]`) to inline the anchored object's members into the current object. Only the unquoted form `<<` merges. The anchored value must be an object or a sequence of objects; existing keys in the target object take precedence over merged keys, and for a sequence of objects later entries win. A non‑object anchor or a sequence containing a non‑object is a parse error (`UDE: merge key requires an object or sequence of objects`).

---

## 8. Tags and Extensions
Tags allow custom data types to be represented in a type‑safe way:

| Tag | Syntax | Meaning |
|-----|--------|---------|
| `!base64` | `!base64 "<data>"` | Decoded binary data from base64 string. |
| `!bin` | `!bin 0x<hex>` | Binary data represented as hex literal. |
| `!datetime` | `!datetime "2026-07-30T14:45:00Z"` | ISO‑8601 timestamp. |
| `!ext` | `!ext "<type>:<data>"` | Extension type with arbitrary payload. Type and data are combined into a single quoted value, because a tag takes exactly one VALUE (`TAGGED_VALUE ::= TAG WS? ANCHOR? WS? VALUE`); a two‑token form like `!ext image/png "..."` would not parse. |

The library provides handlers for the standard tags. **Unknown tags** follow the *save* policy (the default of the three possible behaviors *error / save / ignore*): the tag and its value are preserved without data loss.

### 8.1 Custom Tags (canonical form)
An unknown tag stores its **name separately from its value**:

```text
!<name> <value>
```

* The name is a plain identifier, an optional namespace form `!ns:tag`, or a verbatim URI in angle brackets `!<tag:example.com,2026:type>`.
* The value is any UDE value (string, number, object, array, ...), not flattened text — `!point {x: 1, y: 2}` keeps the object intact.
* In JSON (Extended JSON convention) a custom tag is represented as:

```json
{"$customTag":{"name":"custom","value":"some value"}}
```

* `!schema <string|object>` is stored the same way (tag name `schema`, value kept structured) so schema references and inline schemas round-trip.

Unknown tags are processed by user‑defined callbacks via the `CUSTOM_TAG` value type.

---

## 9. Strict Mode
A parser can be instantiated in **strict mode** where the following rules apply:

* Duplicate keys in an object are errors.
* All keys must be quoted (`"..."`); unquoted keys are rejected (Section 3).
* Unquoted values that contain special characters are rejected.
* Mixed type arrays (e.g., `[1, "two"]`) are allowed; schema validation may reject them if desired.
* Anchors must be defined before they are referenced (an undefined alias is rejected in all modes, not only strict — see Section 7).

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
* **JSON** → valid UDE (no comments or tags).
* **YAML** → Simple YAML files are often valid UDE. Complex YAML features (implicit typing like ~ for null, or complex keys) require manual adaptation.
* **INI** → converted to flat objects; nested sections become dot‑notation keys.
* **BSON/MessagePack** → represented via `!bin` or `!base64` tags.

---

## 12. Limits and Resources
The parser enforces documented resource limits:

| Limit | Value | Notes |
|-------|-------|-------|
| Anchor resolution depth | 100 | Alias chains resolving through more than 100 levels are rejected (Section 7). |
| Number of anchors | 1,000,000 | `too many anchors` error once the per‑document count is reached. |
| Object/array nesting | core `MAX_NESTING_DEPTH` | Shared with the JSON parser. |
| String length | 10 MiB | Shared core limit. |
| Object/array size | 1,000,000 entries | Shared core limit. |

Tabs are **not** structural indentation anywhere in UDE (YAML rule). They are
accepted as ordinary whitespace separators between tokens; inside a block
scalar a tab in the indentation margin is an error (Section 6), while tabs
after the margin are content.

---

## 13. Error Handling

UDE parsing is fail‑fast: the parser reports a problem by throwing a C++ exception of type `asvJSONError`. The message always begins with the prefix `UDE: ` and, where the position is known, ends with ` at line <N>` giving the 1‑based line number in the source document. Column information is **not** tracked, and there is no numeric error‑code enumeration — the message text itself is the diagnostic.

The following conditions raise an error (non‑exhaustive; messages are verbatim):

* **Syntax / structure** — `UDE: expected ':' after key …`, `UDE: expected key at line N`, `UDE: expected value at line N`, `UDE: unterminated object/array/string/single‑quoted string/tag URI/block comment at line N`, `UDE: unexpected top‑level collection at line N`, `UDE: trailing content after block scalar`, `UDE: trailing content after top‑level collection`.
* **Block scalar** — `UDE: tab used as indentation in block scalar at line N` (a tab in the indentation margin; spaces only are allowed).
* **Numbers / escapes** — `UDE: invalid \x escape in string`, `UDE: invalid \u/\U escape in string`, `UDE: code point out of range in string`, `UDE: unterminated \u{...} escape in string`, `UDE: invalid escape sequence in string at line N`.
* **Anchors / aliases** — `UDE: undefined alias *<name>`, `UDE: cyclic reference detected for alias *<name>`, `UDE: anchor resolution depth exceeded` (depth > 100), `UDE: too many anchors`, `UDE: failed to store anchor &<name>`, `UDE: merge key requires an object or sequence of objects`.
* **Duplicate keys** — `UDE: duplicate key '<key>' in strict mode` (default mode keeps the last value silently).
* **Strict mode** — `UDE: strict mode requires all keys to be quoted (key: …)`, `UDE: strict mode requires quoting value: <tok>`, `UDE: missing value after ':' at line N`.
* **Tags** — e.g. `UDE: !int requires an integral value`, `UDE: !base64 requires a string value`, `UDE: !datetime requires a string value`, `UDE: !ext requires "type:data"`, `UDE: invalid base64 data`, `UDE: invalid regex`, `UDE: empty tag URI at line N`.
* **Resource limits** — `UDE: array too large`, `UDE: binary data too large`, `UDE: extension data too large`, `UDE: too many documents` (see §12).
* **Header / version** — `UDE: malformed version header`, `UDE: unsupported UDE version vX.Y` (a major version other than `1` is rejected).

There is no error‑recovery mode: the first error aborts parsing. Callers that need partial results must catch the exception; the partially built DOM is not returned.

---

## 14. Future Work
* Standardized tag registry for third‑party extensions.
* Schema validation (optional JSONSchema‑like validator).
* Streaming parser for large documents.
* External URI references (`!uri` tag) for out‑of‑line binary payloads.
* Formal, machine‑checkable error codes (currently diagnostics are free‑form strings).
* Configurable pretty‑print options (indent width, tab indentation, key sorting, line wrapping).

---

## Appendix A – Formal Grammar
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
NUMBER            ::= SIGNED_INT | SIGNED_FLOAT | HEX | OCTAL | BINARY
SIGNED_INT        ::= '-'? INT
SIGNED_FLOAT      ::= '-'? FLOAT
INT               ::= '0' | [1-9][0-9]*
FLOAT             ::= [0-9]+("." [0-9]*)? ([eE][+-]?[0-9]+)?
# Note: A leading digit before the decimal point is required. Forms like '.5' or
# '-.5' are not valid FLOAT literals; they are lexed as UNQUOTED_STRING instead.
# This is a deliberate restriction to avoid ambiguity with dotted keys.
HEX               ::= '-'? "0x" [0-9a-fA-F]+
OCTAL             ::= '-'? "0o" [0-7]+
BINARY            ::= '-'? "0b" [01]+
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
# Note: In a folded scalar, first the text is folded (NEWLINE → space), then chomping indicators are applied. This mirrors YAML behaviour.
# When the scalar indicator is '>' (folded), the following rules apply:
# * A single NEWLINE between two non-empty lines is replaced by a space.
# * Empty lines are part of the content: a run of one or more blank lines
#   between non-empty lines is preserved as a single paragraph break
#   (one NEWLINE), exactly as in YAML block folding.
# * The resulting string may be further processed by the tag handlers if present.
# These semantics are identical to YAML's folded scalar rules and allow
# multi‑line values to be stored as a single logical line in UDE.
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
* Anchor resolution depth must not exceed 100 levels.
* Cyclic references are forbidden; the parser must detect and report an error.
* The parser MUST track anchors visited during resolution (e.g., a visited/visiting set of anchor names) so that a cycle such as `a: &x *y` / `b: &y *x` is rejected instead of recursing indefinitely.

---

## 15. License
UDE is released under the MIT license. The specification text itself is public domain.

---

End of UDE Specification.