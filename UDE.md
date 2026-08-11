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
// UDE document separator
// --- End of Document ---
```
If no separator is present, the entire file is treated as a single document.

**Note:** Document separation is handled as a pre‑parsing step. The EBNF grammar defined in Appendix A is applied to each document chunk individually after the file is split by this separator.
### 2.1 Header (Optional)
The first non‑empty line can be an optional header that identifies the format and may include flags:

```text
# UDE v1.0

```
* `UDE` – literal marker.
* `v1.0` – version of the UDE specification.
If no header is present, the document consists of a single plain‑text line. The parser treats it as a string value. A line is only treated as plain text when it cannot be a structured document at all (no `:` separator and no structured start character); malformed or strict‑mode‑rejected documents report an error instead of degrading to plain text.

---

## 3. Lexical Elements
| Element | Syntax | Notes |
|---------|--------|-------|
| **Comment** | `# comment…`, `// comment…`, or multi‑line `/* … */` | Begins at first non‑whitespace character and continues until end of line for single‑line comments, or until closing `*/` for block comments. Comments are ignored by the parser.
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
| **Object start/end** | `{` and `}`. Key/value pairs separated by commas or newlines; duplicate keys are only reported as errors when strict mode is enabled.
| **Block scalar indicator** | `|` (literal) or `>` (folded). Followed by an optional indentation level and optional chomping indicators (`+`, `-`). If no chomping indicator is supplied, the default is to keep a single trailing newline (standard YAML behavior).
| **Anchor** | `&name`. Creates a reference that can be reused with `*name`.
| **Alias** | `*name`. Replaces the anchor with its value; aliasing an alias is disallowed and will raise a cycle‑detected error. Maximum anchor depth is 100 levels.
| **Tag** | `!type` prefix before any value (e.g., `!base64 "…"`). Tags are parsed by custom handlers; unknown tags are preserved as raw strings. The parser does not enforce the semantics of a tag – validation is delegated to user callbacks.

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

Block scalar termination (Appendix A): the base indentation is the number of
leading spaces on the first non‑empty line of the block. Every subsequent line
must have at least that many leading spaces; extra spaces are part of the value.
A line with fewer leading spaces terminates the scalar. An empty line (zero
characters after the break) is treated as a line with zero indentation and
*terminates* the block scalar whenever the base indentation is greater than
zero — it contributes a single trailing line break (subject to chomping) and
any content after it must start a new document construct. A leading empty line
before the base indentation is known is preserved. Because of this rule, a
single trailing newline is encodable (the terminator supplies it), but
paragraph breaks via consecutive empty lines are not; the serializer therefore
writes values containing blank interior or trailing lines as quoted strings.

---

## 7. Anchors and Aliases
Anchors create a named reference that can be reused later:

```text
default: &def {x: 1, y: 2}
point1: *def
point2: {x: 3, y: 4}
```
Aliases (`*name`) are replaced with the anchored value during parsing.

---

## 8. Tags and Extensions
Tags allow custom data types to be represented in a type‑safe way:

| Tag | Syntax | Meaning |
|-----|--------|---------|
| `!base64` | `!base64 "<data>"` | Decoded binary data from base64 string. |
| `!bin` | `!bin 0x<hex>` | Binary data represented as hex literal. |
| `!datetime` | `!datetime "2026-07-30T14:45:00Z"` | ISO‑8601 timestamp. |
| `!ext` | `!ext "<type>:<data>"` | Extension type with arbitrary payload. Type and data are combined into a single quoted value, because a tag takes exactly one VALUE (`TAGGED_VALUE ::= TAG WS? ANCHOR? WS? VALUE`); a two‑token form like `!ext image/png "..."` would not parse. |

The library provides handlers for the standard tags; unknown tags are stored as raw strings and can be processed by user‑defined callbacks.

---

## 9. Strict Mode
A parser can be instantiated in **strict mode** where the following rules apply:

* Duplicate keys in an object are errors.
* All keys must be quoted (`"..."`); unquoted keys are rejected (Section 3).
* Unquoted values that contain special characters are rejected.
* Mixed type arrays (e.g., `[1, "two"]`) are allowed; schema validation may reject them if desired.
* Anchors must be defined before they are referenced.

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
// UDE document separator
// --- End of Document ---
```
---

## 11. Compatibility Notes
* **JSON** → valid UDE (no comments or tags).
* **YAML** → Simple YAML files are often valid UDE. Complex YAML features (implicit typing like ~ for null, or complex keys) require manual adaptation.
* **INI** → converted to flat objects; nested sections become dot‑notation keys.
* **BSON/MessagePack** → represented via `!bin` or `!base64` tags.

---

## 12. Future Work
* Standardized tag registry for third‑party extensions.
* Schema validation (optional JSONSchema‑like validator).
* Streaming parser for large documents.

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
FLOAT             ::= [0-9]+("."[0-9]*)?(E[+-]?[0-9]+)?
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

TAG               ::= '!' IDENTIFIER
ANCHOR            ::= '&' IDENTIFIER
ALIAS             ::= '*' IDENTIFIER
WHITESPACE        ::= [ \t]+  // spaces and tabs
NEWLINE           ::= '\r'? '\n'
WS                ::= WHITESPACE*
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
DOC_LINE          ::= LINE | BLOCK_COMMENT | COMMENT
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
PAIR              ::= KEY WS COLON WS VALUE
KEY   ::= IDENTIFIER | STRING | DOT_KEY | UNQUOTED_STRING

# Prioritization note
# When parsing a key that could match both DOT_KEY and UNQUOTED_STRING (e.g., "a.b.c"), the lexer/parser must attempt to match DOT_KEY first.  This guarantees that dotted keys are interpreted as nested structures rather than simple strings, ensuring deterministic behaviour across implementations.

DOT_KEY           ::= IDENTIFIER ('.' IDENTIFIER)+
VALUE             ::= STRING | NUMBER | BOOLEAN | NULL | UNQUOTED_STRING | ALIAS | TAGGED_VALUE | ANCHORED_VALUE | COLLECTION | BLOCK_SCALAR
TAGGED_VALUE      ::= TAG WS? ANCHOR? WS? VALUE
ANCHORED_VALUE    ::= ANCHOR WS? VALUE

BLOCK_SCALAR     ::= SCALAR_INDICATOR INT? CHOMP_INDICATOR? WS? NEWLINE INDENTED_TEXT

# Note: An optional integer may precede the chomp indicator, mirroring YAML syntax (e.g., '|2-' or '>3+').

# Folded Scalar Semantics
# Note: In a folded scalar, first the text is folded (NEWLINE → space), then chomping indicators are applied. This mirrors YAML behaviour.
# When the scalar indicator is '>' (folded), the following rules apply:
# * A single NEWLINE after a line of text is replaced by a space.
# * An empty line terminates the scalar (Appendix A), so it cannot form a
#   paragraph break; only the fold-to-space applies to the collected text.
# * The resulting string may be further processed by the tag handlers if present.
# These semantics are identical to YAML's folded scalar rules and allow
# multi‑line values to be stored as a single logical line in UDE.
SCALAR_INDICATOR  ::= '|' | '>'
CHOMP_INDICATOR   ::= '+' | '-'
INDENTED_TEXT     ::= (WHITESPACE* TEXT_LINE NEWLINE)* WHITESPACE* TEXT_LINE

# Indentation handling
# Each line in a block scalar must have at least the base indentation
# The first line determines the base indent; subsequent lines with less
# indentation terminate the scalar. Empty lines are treated as a line
# with zero indentation and terminate the scalar when the base indentation
# is greater than zero (Appendix A); the terminating empty line supplies a
# single trailing line break subject to chomping.
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
   Empty lines (zero characters after NEWLINE) are treated as a line with zero indentation and terminate the block scalar if the base indentation is greater than zero.
   The block scalar ends either when a line with fewer leading spaces than the base indentation appears, or at EOF. */
BOOLEAN           ::= [tT][rR][uU][eE] | [fF][aA][lL][sS][eE]
NULL              ::= [nN][uU][lL][lL]
```
### Security constraints
* Anchor resolution depth must not exceed 100 levels.
* Cyclic references are forbidden; the parser must detect and report an error.
* The parser MUST track anchors visited during resolution (e.g., a visited/visiting set of anchor names) so that a cycle such as `a: &x *y` / `b: &y *x` is rejected instead of recursing indefinitely.

---

## 13. License
UDE is released under the MIT license. The specification text itself is public domain.

---

End of UDE Specification.