#pragma once
// XML serialization/parsing for asvJSON++
// Namespace prefixes (e.g. xml:lang, xsi:nil) are preserved as part of key strings:
//   - Attributes become "@prefix:name" keys (e.g. "@xml:lang")
//   - Element names preserve the prefix (e.g. "ns:element")
//   - xmlns declarations become "@xmlns" and "@xmlns:prefix" attributes
//
// Semantics notes:
//   - <x/> and <x></x> parse identically; a lone type="..." attribute on an
//     empty element pins the empty typed value (string/binary/array/object/
//     extension), so both forms round-trip.
//   - Mixed content is pinned with an asv:xc="1" marker on output so #content
//     arrays re-parse exactly (leading-text mixtures included). External XML
//     is routed to #content only when any text follows an element: leading
//     text before elements comes back as {"#text", children} (asymmetric by
//     design, see the interleaving analysis in xmlParseElement).
//   - Arrays of objects serialize as <name><item>...</item></name> (single
//     repeated child name), which the collapse rule reverses on a re-parse.
//     A SINGLE-element array carries an explicit asv:arr="1" marker (the
//     collapse rule needs count >= 2, so an unmarked <name><item>...</item></name>
//     re-parses as a plain {item: value} object).
//   - Adjacent STRING items in a #content array have no XML text boundary and
//     would merge into one text run; the encoder separates consecutive
//     strings with an internal empty marker element (<asv:s/>) that the
//     parser drops on a re-parse.
//   - RESERVED internal keys (in mixed #content only): asv:s (separator
//     marker), asv:xc (existence = rich/mixed-content pin; value is ignored -
//     the encoder writes "1", but any value including "" triggers), asv:arr ("1" =
//     single-element array pin). A user key equal to one of these is only
//     affected inside a mixed #content (asv:xc/) array; elsewhere on the
//     document the literal text round-trips as data. The asv:arr marker
//     always wins - <x asv:arr="1"/> and <x asv:arr="1"></x> both parse as [],
//     and <x asv:arr="1" foo="bar">...</x> parses as [] with @foo silently
//     dropped. When both asv:xc and asv:arr are present (not emitted by the
//     encoder), asv:arr wins - the element becomes [] with text dropped.
//     Extracted before the self-closing split; the encoder never emits
//     asv:arr on empty elements, so this is only reachable from hand-written XML.
//   - A user "@type" key is written as the reserved attribute asv:ut, because
//     a bare type="..." is the encoder's own annotation (empty-value pin and
//     #text string pin) and would be consumed by the decoder, turning
//     {@type: "string"} into an empty STRING. asv:ut is therefore reserved
//     everywhere; the decoder maps it back to @type, and a hand-written bare
//     type="..." with a non-annotation value still decodes to a plain @type.
//   - A MULTI-KEY object inside a #content array cannot be represented: the
//     parser restores every element segment as a {childName: value} wrapper,
//     so wrapping it in <item> re-parsed as {"item": {...}} -- a level the
//     input never had. toXML throws for that shape instead of reshaping it
//     (single-key objects, which the parser does produce, round-trip fine).
//   - KNOWN LIMITATION (pre-existing, not a regression): an empty string in a
//     #content array is not round-tripped at either end. A LEADING one
//     (["", "foo"]) has no XML text to serialize and no preceding adjacent-
//     string boundary; a TRAILING one (["foo", ""]) does emit the <asv:s/>
//     separator but is followed by no text, and the empty text run produces no
//     segment on re-parse. Both collapse to the non-empty items. Emitting a
//     marker for the leading case would resurrect exactly the asv:s key
//     collision documented below, and no empty text node is representable in
//     XML, so neither is fixable without changing the on-wire marker scheme.
//   - A non-STRING scalar directly inside a #content array (reachable from a
//     programmatically built tree -- the decoder only ever produces {name:value}
//     wrappers and, since mixed text is character data, raw strings) serializes
//     as an <item>N</item> element and re-parses as the wrapper {"item": N}.
//     That is a stable fixed point, and the <item> element is the only XML form
//     that carries the scalar's number/bool type, but it is a one-way reshaping
//     of the bare scalar on the first encode.
//   - An xsi:nil="true" element decodes as null and the element's OTHER
//     attributes are dropped (a null has no attribute slot in the data model).
//     toXML never writes a nil element with sibling attributes, so this is only
//     reachable from hand-written or foreign XML. The asv:arr="1" array pin is
//     likewise honoured only for an attribute-less element.
//   - RESERVED KEYS: inside a #content array the keys/attributes asv:s, asv:xc
//     and asv:arr are reserved as internal round-trip markers; asv:ut is
//     reserved document-wide as the user @type attribute name. A user object
//     that itself contains {asv:s: ...} / {asv:xc: ...} / {asv:arr: ...} as a
//     DIRECT child is unpinned and its contents round-trip (use that form to
//     keep a literal key); put them inside a normal (non-array-of-strings)
//     object instead of at mixed-content top level, or quote the key so it is
//     not confused with a marker on re-parse.
//   - A stray '<' not starting a valid tag in skipped xsi:nil content scans
//     as ordinary characters (malformed input is tolerated, never hangs).
//   - xmlns declarations are element-scoped: a prefix bound inside one element
//     never leaks to earlier or later siblings.
//   - Multiple root elements (invalid XML, tolerated as an extension) merge
//     last-wins into one object.
//   - CDATA sections and mixed-content text are always character data: their
//     text is never re-interpreted as a number/bool/null. (Leading text before
//     any child element on the non-interleaved path still goes through value
//     detection, matching a plain-text element.) This is what lets external
//     mixed-content documents ("<x><b>z</b>123</x>") round-trip exactly; the
//     encoder's asv:xc pin already routes text to raw strings, and the decoder
//     now matches it for non-pinned external input too.
//   - A NaN/Infinity attribute value throws (it has no XML attribute text
//     form); NaN/Infinity doubles in element content serialize as xsi:nil.

#include "../core.hpp"

namespace asvJSONInternal {

// Decode XML entities: &amp; &lt; &gt; &quot; &apos; &#NN; &#xNN;
inline std::string xmlDecodeEntities(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '&') {
			size_t end = s.find(';', i);
			if (end == std::string_view::npos) { out += s[i]; continue; }
			std::string_view ent = s.substr(i + 1, end - i - 1);
			if (ent == "amp") out += '&';
			else if (ent == "lt") out += '<';
			else if (ent == "gt") out += '>';
			else if (ent == "quot") out += '"';
			else if (ent == "apos") out += '\'';
			else if (!ent.empty() && ent[0] == '#') {
				int base = (ent.size() > 2 && ent[1] == 'x') ? 16 : 10;
				const char* first = ent.data() + (base == 16 ? 2 : 1);
				const char* last = ent.data() + ent.size();
				if (first >= last) {
					// Nothing after "&#"/"&#x": impossible to parse; keep the
					// raw text instead of running from_chars over an inverted
					// range (undefined behavior).
					out += '&'; out.append(ent.data(), ent.size()); out += ';';
					i = end;
					continue;
				}
				unsigned long cp = 0;
				auto [ptr, ec] = std::from_chars(first, last, cp, base);
				if (ec == std::errc() && ptr > first) {
					if (cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
						if (cp <= 0x7F) out += static_cast<char>(cp);
						else if (cp <= 0x7FF) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
						else if (cp <= 0xFFFF) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
						else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					}
					// else: valid number, illegal XML codepoint (surrogate/
					// above U+10FFFF) -- silently dropped (legacy behavior).
				} else {
					// Malformed numeric reference (e.g. &#xG;): keep the raw
					// text instead of losing the entity entirely -- same
					// fallback as an unknown named reference.
					out += '&'; out.append(ent.data(), ent.size()); out += ';';
				}
			} else {
				out += '&'; out.append(ent.data(), ent.size()); out += ';';
			}
			i = end;
		} else {
			out += s[i];
		}
	}
	return out;
}


// Forward declaration (defined alongside the decoder below)
inline bool xmlNeedsStringType(std::string_view s);

// Serialize a REGEX value as raw "pattern|options|" text for type="regex".
// Never emit JSON-style quotes here -- the XML text body is raw content, so
// fmtRegexVal()'s quote wrapping would corrupt the pattern on a toXML ->
// fromXML round trip.
//
// A '|' inside a pattern or an option string is escaped as \u007C and a
// backslash as \\, so the ONLY literal '|' characters in our own output are
// the separators, and a trailing '|' terminates the field unambiguously.
// Without this the parser's rfind('|') split a no-options pattern like "a|b"
// into pattern "a" + options "b" (silent corruption). Escaping the backslash
// first keeps a pattern containing the literal text "\u007C" distinguishable
// from an escaped pipe. Unescaped external XML stays inherently ambiguous.
inline void xmlEscRegexText(std::string_view s, std::string& out) {
	for (char c : s) {
		if (c == '\\') out += "\\\\";
		else if (c == '|') out += "\\u007C";
		else out += c;
	}
}

inline std::string xmlUnescRegexText(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			if (s[i + 1] == '\\') { out += '\\'; i++; continue; }
			if (i + 5 < s.size() && s.compare(i, 6, "\\u007C") == 0) { out += '|'; i += 5; continue; }
		}
		out += s[i];
	}
	return out;
}

inline void fmtRegexXml(std::string_view s, std::string& out) {
	size_t sep = s.rfind('\0');
	std::string_view pat = (sep != std::string_view::npos) ? s.substr(0, sep) : s;
	std::string_view opts = (sep != std::string_view::npos && sep + 1 < s.size())
		? s.substr(sep + 1) : std::string_view();
	xmlEscRegexText(pat, out);
	out += '|';
	if (!opts.empty()) xmlEscRegexText(opts, out);
	out += '|';
}

// Wire name for a USER "@type" attribute. The encoder's own type="..."
// annotation is a reserved attribute name, so a user key @type used to be
// written as a bare type="..." and then be re-read as that annotation: the
// annotation wins and the value is consumed ({"@type":"string"} came back as an
// empty STRING, {@type:"string", other:1} as {#text:"", other:1}). It also
// silently suppressed the type="string" pin on a sibling #text, so
// {@type:"x", #text:"123"} came back with #text as the number 123.
// User @type data now travels under this reserved name instead, leaving the
// bare type=... attribute exclusively for encoder annotations. Reading stays
// backward compatible: a bare type="..." that is not a known annotation word
// still decodes to @type, so documents written before this change are
// unaffected.
inline constexpr std::string_view kXmlUserTypeAttr = "asv:ut";

// Attribute name to write for a user "@"-prefixed object key.
inline std::string_view xmlUserAttrName(const std::string& k) {
	return k == "@type" ? kXmlUserTypeAttr : std::string_view(k).substr(1);
}

// Object key a parsed attribute maps back to. Every attribute becomes an
// @-prefixed key except the reserved asv:ut, which is the user's own @type.
inline std::string xmlAttrToKey(const std::string& name) {
	if (name == kXmlUserTypeAttr) return "@type";
	return "@" + name;
}

// Stringify any scalar attribute value (parser attributes are always STRING,
// but user data can hold INT/DOUBLE/BOOL/NULL @-prefixed keys). Stringifying
// keeps the attribute data instead of emitting an empty value.
inline void xmlAppendAttrValue(std::string& out, const asvJSONValue* v) {
	using T = asvJSONValue::Type;
	if (!v) return;
	switch (v->type) {
		case T::STRING: out += xmlEscapeContent(v->str_data); break;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) out.append(buf, ptr - buf);
			break;
		}
		case T::DOUBLE: {
			if (std::isnan(v->dbl) || std::isinf(v->dbl))
				throw asvJSONError("XML: NaN/Infinity attribute value cannot be serialized");
			std::string t;
			fmtDoubleValDot0(v->dbl, t);
			out += t;
			break;
		}
		case T::BOOL_VAL: out += v->flag ? "true" : "false"; break;
		case T::NULL_VAL: out += "null"; break;
		default:
			// DATETIME/BINARY/OBJECTID/REGEX/TIMESTAMP/EXTENSION and anything
			// container-like have no XML attribute text form; emitting "" would
			// silently lose the value.
			throw asvJSONError("XML: attribute value type cannot be serialized");
	}
}

inline void asvJSONValue::toXML(std::string& out) const {
	toXML(out, "root", 0);
}

inline void asvJSONValue::toXML(std::string& out, const std::string& name, int indent) const {
	using T = asvJSONValue::Type;
	std::string pad(indent * 2, ' ');

	switch (type) {
		case T::NULL_VAL:
			out += pad + "<" + xmlSanitizeElementName(name) + " xsi:nil=\"true\"/>\n";
			break;
		case T::BOOL_VAL:
			out += pad + "<" + xmlSanitizeElementName(name) + ">" + (flag ? "true" : "false") + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += pad + "<" + xmlSanitizeElementName(name) + ">" + std::string(buf, ptr) + "</" + xmlSanitizeElementName(name) + ">\n"; }
			break;
		}
		case T::DOUBLE: {
			if (std::isnan(dbl) || std::isinf(dbl)) { out += pad + "<" + xmlSanitizeElementName(name) + " xsi:nil=\"true\"/>\n"; break; }
			std::string val; fmtDoubleValDot0(dbl, val);
			out += pad + "<" + xmlSanitizeElementName(name) + ">" + xmlEscapeContent(val) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::STRING:
			if (xmlNeedsStringType(str_data))
				out += pad + "<" + xmlSanitizeElementName(name) + " type=\"string\">" + xmlEscapeContent(str_data) + "</" + xmlSanitizeElementName(name) + ">\n";
			else
				out += pad + "<" + xmlSanitizeElementName(name) + ">" + xmlEscapeContent(str_data) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(timestamp, datetime_ms, dt, tz_offset_min);
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"datetime\">" + dt + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::BINARY: {
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"binary\">" + xmlEscapeContent(encodeBase64(bin_data.data(), bin_data.size())) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::OBJECTID: {
			std::string hex; fmtObjectIdHexVal(str_data, hex);
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"objectid\">" + hex + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::REGEX: {
			std::string r; fmtRegexXml(str_data, r);
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"regex\">" + xmlEscapeContent(r) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::TIMESTAMP: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += pad + "<" + xmlSanitizeElementName(name) + " type=\"timestamp\">" + std::string(buf, ptr) + "</" + xmlSanitizeElementName(name) + ">\n"; }
			break;
		}
		case T::EXTENSION: {
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"extension\" exttype=\"" + std::to_string(static_cast<int>(ext_type)) + "\">" + xmlEscapeContent(encodeBase64(bin_data.data(), bin_data.size())) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::OBJECT: {
			if (!obj || obj->empty()) { out += pad + "<" + xmlSanitizeElementName(name) + " type=\"object\"></" + xmlSanitizeElementName(name) + ">\n"; break; }
			// Check for #content array (mixed content from fromXML)
			auto contentIt = obj->find("#content");
			if (contentIt != obj->end() && contentIt->second && contentIt->second->type == ARRAY) {
				// #text and #content cannot coexist - they are alternative
				// representations of the same thing. A programmatic tree with
				// both is malformed; reject it rather than silently dropping
				// one side.
				if (obj->find("#text") != obj->end())
					throw asvJSONError("XML: #text and #content cannot coexist");
				// Pin #content with an asv:xc marker so a re-parse routes back
				// into the #content branch (raw strings, no type detect) and
				// the element lists restore as {name: val} wrappers. Without
				// the marker a leading-text mixture ("foo<b>1</b>") would
				// re-collapse into {"#text","b"} -- the asymmetric parser path
				// -- and lose the array. Always emitted, pure-text included
				// (a pure-text array would otherwise re-parse as a scalar).
				out += pad + "<" + xmlSanitizeElementName(name) + " asv:xc=\"1\"";
				for (const auto& [k, v] : *obj) {
					if (k == "#content") continue;
					if (!k.empty() && k[0] == '@') {
						out += " " + xmlSanitizeElementName(xmlUserAttrName(k)) + "=\"";
						xmlAppendAttrValue(out, v.get());
						out += "\"";
					}
				}
				out += ">";
				for (size_t ti = 0; ti < contentIt->second->arr->size(); ti++) {
					const auto& item = (*contentIt->second->arr)[ti];
					if (item->type == asvJSONValue::STRING) {
						// Adjacent text items have no XML boundary (they merge
						// into one text run on a re-parse), so separate
						// consecutive strings with an internal empty-marker
						// element (<asv:s/>) the parser drops on a re-parse.
						if (ti > 0 && (*contentIt->second->arr)[ti - 1]->type == asvJSONValue::STRING)
							out += "<asv:s/>";
						out += xmlEscapeContent(item->str_data);
					} else if (item->type == asvJSONValue::OBJECT && item->obj && item->obj->size() == 1) {
						const auto& [childName, childVal] = *item->obj->begin();
						std::string safeChildName = xmlSanitizeElementName(childName);
						if (!childVal) {
							out += "<" + safeChildName + "/>";
						} else if (childVal->type == asvJSONValue::NULL_VAL) {
							out += "<" + safeChildName + " xsi:nil=\"true\"/>";
						} else if (childVal->type == asvJSONValue::STRING && childVal->str_data.empty()) {
							out += "<" + safeChildName + "/>";
						} else if (childVal->type == asvJSONValue::STRING) {
							if (xmlNeedsStringType(childVal->str_data))
								out += "<" + safeChildName + " type=\"string\">" + xmlEscapeContent(childVal->str_data) + "</" + safeChildName + ">";
							else
								out += "<" + safeChildName + ">" + xmlEscapeContent(childVal->str_data) + "</" + safeChildName + ">";
						} else if (childVal->type == asvJSONValue::INT) {
							char buf[32];
							auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), childVal->num);
							if (ec == std::errc()) out += "<" + safeChildName + ">" + std::string(buf, ptr) + "</" + safeChildName + ">";
						} else if (childVal->type == asvJSONValue::DOUBLE) {
							if (!std::isnan(childVal->dbl) && !std::isinf(childVal->dbl)) {
								std::string val; fmtDoubleValDot0(childVal->dbl, val);
								out += "<" + safeChildName + ">" + xmlEscapeContent(val) + "</" + safeChildName + ">";
							}
						} else if (childVal->type == asvJSONValue::BOOL_VAL) {
							out += "<" + safeChildName + ">" + (childVal->flag ? "true" : "false") + "</" + safeChildName + ">";
						} else {
							childVal->toXML(out, childName, 0);
							if (!out.empty() && out.back() == '\n') out.pop_back();
						}
} else if (item->type == asvJSONValue::OBJECT) {
						// A multi-key object in #content has no #content item form
						// that decodes back to a bare object: the parser restores every
						// element segment as a {childName: value} wrapper, so wrapping it
						// in <item> came back as {"item":{...}} -- an extra level the input
						// never had. The decoder never produces a multi-key item (only
						// {name: value} wrappers), so this shape is reachable from a
						// programmatically built tree only. Reject it rather than silently
						// re-shaping the data.
						throw asvJSONError("XML: multi-key object in mixed #content is not representable");
					} else if (item->type == asvJSONValue::ARRAY) {
						item->toXML(out, "item", 0);
						if (!out.empty() && out.back() == '\n') out.pop_back();
					} else if (item->type == asvJSONValue::INT || item->type == asvJSONValue::DOUBLE ||
					           item->type == asvJSONValue::BOOL_VAL || item->type == asvJSONValue::NULL_VAL ||
					           item->type == asvJSONValue::DATETIME || item->type == asvJSONValue::BINARY ||
					           item->type == asvJSONValue::OBJECTID || item->type == asvJSONValue::REGEX ||
					           item->type == asvJSONValue::TIMESTAMP || item->type == asvJSONValue::EXTENSION) {
						item->toXML(out, "item", 0);
						if (!out.empty() && out.back() == '\n') out.pop_back();
					} else {
						// CUSTOM_TAG (and any future unhandled type) used to fall
						// through silently; reject instead of dropping the item.
						throw asvJSONError("XML: cannot serialize this type in mixed content");
					}
				}
				out += "</" + xmlSanitizeElementName(name) + ">\n";
				break;
			}
			// If this object carries #text (text content alongside attributes or
			// element children), emit it compactly on one line -- like the
			// #content/mixed-content path -- so no indentation whitespace leaks
			// into the text value on a re-parse.
			auto textIt = obj->find("#text");
			if (textIt != obj->end() && textIt->second &&
			    (textIt->second->type == STRING || textIt->second->type == INT ||
			     textIt->second->type == DOUBLE || textIt->second->type == BOOL_VAL ||
			     textIt->second->type == NULL_VAL)) {
				out += pad + "<" + xmlSanitizeElementName(name);
				for (const auto& [k, v] : *obj) {
					if (k == "#text") continue;
					if (!k.empty() && k[0] == '@') {
						out += " " + xmlSanitizeElementName(xmlUserAttrName(k)) + "=\"";
						xmlAppendAttrValue(out, v.get());
						out += "\"";
					}
				}
				// A #text STRING that looks like a number/bool/null needs a
				// type="string" annotation or a re-parse re-detects its type.
				// A user @type used to suppress this (the annotation would have
				// collided on the same attribute name); it no longer does,
				// since user @type is written as asv:ut.
				if (textIt->second->type == asvJSONValue::STRING &&
				    xmlNeedsStringType(textIt->second->str_data))
					out += " type=\"string\"";
				out += ">";
				auto& tv = textIt->second;
				if (tv->type == asvJSONValue::STRING) out += xmlEscapeContent(tv->str_data);
				else if (tv->type == asvJSONValue::INT) { char buf[32]; auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), tv->num); if (ec == std::errc()) out += std::string(buf, ptr); }
				else if (tv->type == asvJSONValue::DOUBLE) { if (!std::isnan(tv->dbl) && !std::isinf(tv->dbl)) { std::string val; fmtDoubleValDot0(tv->dbl, val); out += val; } }
				else if (tv->type == asvJSONValue::BOOL_VAL) out += tv->flag ? "true" : "false";
				else if (tv->type == asvJSONValue::NULL_VAL) out += "null";
				for (const auto& [k, v] : *obj) {
					if (!k.empty() && k[0] == '@') continue;
					if (k == "#text") continue;
					std::string tmp;
					v->toXML(tmp, k, 0);
					if (!tmp.empty() && tmp.back() == '\n') tmp.pop_back();
					out += tmp;
				}
				out += "</" + xmlSanitizeElementName(name) + ">\n";
				break;
			}
			out += pad + "<" + xmlSanitizeElementName(name);
			for (const auto& [k, v] : *obj) {
				if (!k.empty() && k[0] == '@') {
					out += " " + xmlSanitizeElementName(xmlUserAttrName(k)) + "=\"";
					xmlAppendAttrValue(out, v.get());
					out += "\"";
				}
			}
			out += ">\n";
			for (const auto& [k, v] : *obj) {
				if (!k.empty() && k[0] == '@') continue;
				if (k == "#text") continue;
				v->toXML(out, k, indent + 1);
			}
			out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::ARRAY: {
			if (!arr || arr->empty()) { out += pad + "<" + xmlSanitizeElementName(name) + " type=\"array\"></" + xmlSanitizeElementName(name) + ">\n"; break; }
			// Every array -- objects included -- serializes as one <name>
			// element with repeated <item> children. The parser's collapse
			// rule (single repeated child name) reverses this exactly, so
			// arrays of objects round-trip instead of degrading into multiple
			// sibling <name> elements ({name:[{name:...}]} on a re-parse).
			// A SINGLE-element array has no repeated name to collapse on and
			// would re-parse as a plain {item: value} object, so it carries an
			// explicit asv:arr marker that pins the array shape.
			out += pad + "<" + xmlSanitizeElementName(name);
			if (arr->size() == 1) out += " asv:arr=\"1\"";
			out += ">\n";
			for (const auto& item : *arr) {
				if (item->type == asvJSONValue::STRING && xmlNeedsStringType(item->str_data))
					out += pad + "  <item type=\"string\">" + xmlEscapeContent(item->str_data) + "</item>\n";
				else if (item->type == asvJSONValue::OBJECT || item->type == asvJSONValue::ARRAY) {
					item->toXML(out, "item", indent + 1);
				} else if (item->type == asvJSONValue::DATETIME || item->type == asvJSONValue::BINARY ||
				           item->type == asvJSONValue::OBJECTID || item->type == asvJSONValue::REGEX ||
				           item->type == asvJSONValue::TIMESTAMP || item->type == asvJSONValue::EXTENSION) {
					item->toXML(out, "item", indent + 1);
				} else {
					// CUSTOM_TAG (and any future unhandled type) has no element
					// text form; "<item></item>" would re-parse as an empty STRING
					// and lose the tag, so reject it as the mixed-content path does.
					if (item->type == asvJSONValue::CUSTOM_TAG)
						throw asvJSONError("XML: cannot serialize CUSTOM_TAG value");
					out += pad + "  <item";
					if (item->type == asvJSONValue::NULL_VAL) {
						out += " xsi:nil=\"true\"/>\n";
					} else if (item->type == asvJSONValue::DOUBLE && (std::isnan(item->dbl) || std::isinf(item->dbl))) {
						out += " xsi:nil=\"true\"/>\n";
					} else {
						out += ">";
						if (item->type == asvJSONValue::STRING) out += xmlEscapeContent(item->str_data);
						else if (item->type == asvJSONValue::INT) { char buf[32]; auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), item->num); if (ec == std::errc()) out += std::string(buf, ptr); }
						else if (item->type == asvJSONValue::DOUBLE) { std::string val; fmtDoubleValDot0(item->dbl, val); out += xmlEscapeContent(val); }
						else if (item->type == asvJSONValue::BOOL_VAL) out += item->flag ? "true" : "false";
						out += "</item>\n";
					}
				}
			}
			out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		default:
			// Only CUSTOM_TAG (and any future unhandled type) reaches this
			// default: it has no XML text form, and the old silent "<name/>"
			// re-parsed as an empty STRING, quietly losing the tag. Reject it
			// like the mixed-content path does instead of dropping the value.
			throw asvJSONError("XML: cannot serialize CUSTOM_TAG value");
	}
}

// ---------- XML Parser ----------

inline void xmlSkipSpaces(std::string_view s, size_t& pos) {
	while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
		pos++;
}

inline std::string xmlParseName(std::string_view s, size_t& pos) {
	std::string name;
	while (pos < s.size()) {
		unsigned char c = static_cast<unsigned char>(s[pos]);
		// ASCII alnum plus '-', '_', ':', '.' and any byte >= 0x80 (UTF-8
		// characters). Anything else terminates the name.
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    c == '-' || c == '_' || c == ':' || c == '.' || c >= 0x80)
			name += static_cast<char>(s[pos++]);
		else break;
	}
	return name;
}

inline std::string xmlParseAttrValue(std::string_view s, size_t& pos) {
	char quote = s[pos];
	pos++;
	std::string val;
	while (pos < s.size() && s[pos] != quote) {
		val += s[pos++];
	}
	if (pos >= s.size()) throw asvJSONError("XML: unterminated attribute value");
	pos++;
	return xmlDecodeEntities(val);
}

// Every value-level STRING allocation in the XML decoder goes through this
// wrapper. makeString returns nullptr both on OOM and when the payload exceeds
// MAX_STRING_LEN -- and, unlike the JSON parser, the XML decoder does NOT
// length-check text/attribute values, so a >10 MiB text or attribute used to
// leave a null node in the tree: fromXML() returned true and the next
// serialize()/toXML() dereferenced the nullptr (0xC0000005). Throwing here
// keeps xmlParseElement "non-null or throw", matching plist's checkStringLen.
inline std::unique_ptr<asvJSONValue> xmlMakeString(const char* s, size_t len) {
	auto v = asvJSONValue::makeString(s, len);
	if (!v) throw asvJSONError(len > asvJSONValue::MAX_STRING_LEN ? "XML: string too long" : "XML: out of memory");
	return v;
}

inline std::unique_ptr<asvJSONValue> xmlMakeString(std::string_view sv) {
	return xmlMakeString(sv.data(), sv.size());
}

// Detect value type from XML text content
inline std::unique_ptr<asvJSONValue> xmlDetectValue(std::string_view s) {
	if (s.empty()) return asvJSONValue::makeNull();
	if (s == "true" || s == "TRUE") return asvJSONValue::makeBool(true);
	if (s == "false" || s == "FALSE") return asvJSONValue::makeBool(false);
	if (s == "null" || s == "NULL") return asvJSONValue::makeNull();
	if (!s.empty() && (s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) {
		// JSON numbers never carry leading zeros ("042", "-042"), so those
		// literals must stay STRINGs (otherwise the value silently changes
		// on a re-parse). "0", "-0", "0.5" and "-0.5" are still valid.
		bool leadZero = false;
		if (s.size() > 1 && s[0] == '0' && s[1] >= '0' && s[1] <= '9') leadZero = true;
		else if (s.size() > 2 && s[0] == '-' && s[1] == '0' && s[2] >= '0' && s[2] <= '9') leadZero = true;
		if (!leadZero) {
			long long v;
			auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
			if (ec == std::errc() && ptr == s.data() + s.size()) return asvJSONValue::makeInt(v);
			bool allDigits = (s.size() > 1);
			const char* p0 = (s[0] == '-') ? s.data() + 1 : s.data();
			for (const char* p = p0; p < s.data() + s.size(); ++p)
				if (*p < '0' || *p > '9') { allDigits = false; break; }
			if (allDigits) {
				auto r = xmlMakeString(s.data(), s.size());
				r->raw_number = true;
				return r;
			}
			double d;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
			auto [ptr2, ec2] = std::from_chars(s.data(), s.data() + s.size(), d);
			if (ec2 == std::errc() && ptr2 == s.data() + s.size()) return asvJSONValue::makeDouble(d);
#else
			// Fallback for toolchains without locale-independent from_chars:
			// strtod respects LC_NUMERIC, so a comma-decimal locale would
			// read "3.14" as STRING instead of DOUBLE. Callers in such
			// environments should setlocale(LC_ALL, "C") before parsing.
			std::string tmp(s);
			char* end; errno = 0;
			d = std::strtod(tmp.c_str(), &end);
			if (errno != ERANGE && end == tmp.c_str() + tmp.size()) return asvJSONValue::makeDouble(d);
#endif
		}
	}
	return xmlMakeString(s.data(), s.size());
}

// xmlDetectValue's scalar branches (makeNull/makeBool/makeInt/makeDouble) are
// nothrow factories that return nullptr on OOM; funnel the result so a null
// never lands in the tree -- same "non-null or throw" rule as xmlMakeString.
inline std::unique_ptr<asvJSONValue> xmlNonNull(std::unique_ptr<asvJSONValue> v) {
	if (!v) throw asvJSONError("XML: out of memory");
	return v;
}

// true when a STRING needs a type="string" annotation on output so that a
// re-parse preserves STRING rather than re-detecting a number/bool/null.
// Pure textual mirror of xmlDetectValue's type decision -- deliberately no
// node allocation (this runs once per string element on output).
inline bool xmlNeedsStringType(std::string_view s) {
	if (s.empty()) return true; // xmlDetectValue("") -> null
	if (s == "true" || s == "TRUE" || s == "false" || s == "FALSE" || s == "null" || s == "NULL") return true;
	if (!s.empty() && (s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) {
		// JSON numbers never carry leading zeros -- those stay STRINGs.
		bool leadZero = false;
		if (s.size() > 1 && s[0] == '0' && s[1] >= '0' && s[1] <= '9') leadZero = true;
		else if (s.size() > 2 && s[0] == '-' && s[1] == '0' && s[2] >= '0' && s[2] <= '9') leadZero = true;
		if (!leadZero) {
			long long v;
			auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
			if (ec == std::errc() && ptr == s.data() + s.size()) return true; // INT
			bool allDigits = (s.size() > 1);
			const char* p0 = (s[0] == '-') ? s.data() + 1 : s.data();
			for (const char* p = p0; p < s.data() + s.size(); ++p)
				if (*p < '0' || *p > '9') { allDigits = false; break; }
			if (allDigits) return false; // overflow literal -> STRING (raw_number)
			double d;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
			auto [ptr2, ec2] = std::from_chars(s.data(), s.data() + s.size(), d);
			if (ec2 == std::errc() && ptr2 == s.data() + s.size()) return true; // DOUBLE
#else
			// Same LC_NUMERIC caveat as xmlDetectValue: strtod respects
			// the current locale, so a comma-decimal environment would
			// misclassify "3.14". Callers should setlocale(LC_ALL, "C").
			std::string tmp(s);
			char* end; errno = 0;
			d = std::strtod(tmp.c_str(), &end);
			if (errno != ERANGE && end == tmp.c_str() + tmp.size()) return true; // DOUBLE
#endif
		}
	}
	return false;
}

// Build the value for an element with empty content. <x/> and <x></x> are the
// same XML construct, so both routes share this logic:
//   - the asv:xc marker pins an empty #content array;
//   - a lone type="..." attribute pins an empty typed value (string/binary/
//     array/object/extension); any other attribute, or a type that cannot be
//     represented empty (datetime/objectid/regex/timestamp), keeps the element
//     an attribute-only object ({"@type":...} etc.);
//   - otherwise an empty element is an empty STRING, and an attribute-only
//     element is an object of @-prefixed keys.
inline std::unique_ptr<asvJSONValue> xmlEmptyElement(const std::unordered_map<std::string, std::string>& attrs, bool forceMixed) {
	if (forceMixed) {
		auto xobj = asvJSONValue::makeObject();
		if (!xobj) throw asvJSONError("XML: out of memory");
		auto xarr = asvJSONValue::makeArray();
		if (!xarr) throw asvJSONError("XML: out of memory");
		xobj->obj->emplace("#content", std::move(xarr));
		for (const auto& [k, v] : attrs) xobj->obj->emplace(xmlAttrToKey(k), xmlMakeString(v.data(), v.size()));
		return xobj;
	}
	auto tit = attrs.find("type");
	if (tit != attrs.end()) {
		const std::string& t = tit->second;
		// Only the type attribute (plus optional exttype for extensions) is
		// consumed as a typed empty value. Any other attribute means the
		// element is an attribute-only object, which is preserved.
		bool onlyTyped = true;
		for (const auto& [k, v] : attrs)
			if (k != "type" && !(t == "extension" && k == "exttype")) { onlyTyped = false; break; }
		if (onlyTyped) {
			if (t == "string") return xmlMakeString("", 0);
			if (t == "binary") return xmlNonNull(asvJSONValue::makeBinary(nullptr, 0));
			if (t == "array") return xmlNonNull(asvJSONValue::makeArray());
			if (t == "object") return xmlNonNull(asvJSONValue::makeObject());
			if (t == "extension") {
				int8_t et = 0;
				auto eit = attrs.find("exttype");
				if (eit != attrs.end()) {
					long ev = std::strtol(eit->second.c_str(), nullptr, 10);
					if (ev >= -128 && ev <= 127) et = static_cast<int8_t>(ev);
				}
				return xmlNonNull(asvJSONValue::makeExtension(et, nullptr, 0));
			}
		}
	}
	if (attrs.empty()) return xmlMakeString("", 0);
	auto obj = asvJSONValue::makeObject();
	if (!obj) throw asvJSONError("XML: out of memory");
	for (const auto& [k, v] : attrs) obj->obj->emplace(xmlAttrToKey(k), xmlMakeString(v.data(), v.size()));
	return obj;
}

// Remove XML whitespace (space, tab, CR, LF) from base64 text so PEM-style
// line-wrapped payloads decode instead of silently degrading to an object.
inline std::string xmlStripWs(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s)
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r') out += c;
	return out;
}

// Same character class as xmlParseName: ASCII alnum plus '-', '_', ':', '.'
// and any byte >= 0x80 (UTF-8). The bytes are compared as unsigned, so a
// non-ASCII name character (e.g. a Cyrillic element name) does not terminate
// the scan early -- with a plain char the >= 0x80 test cannot even be written,
// and every range comparison misses.
inline bool xmlNameChar(char c) {
	unsigned char u = static_cast<unsigned char>(c);
	return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
	       u == ':' || u == '-' || u == '_' || u == '.' || u >= 0x80;
}

// Advance pos past an xsi:nil element's body to the matching closing tag.
// A naive "</name" substring search can stop at a nested same-name closing
// tag inside the content, so nesting is counted: every nested <name> opening
// that delimits real content increments depth, and only the depth-0 close is
// consumed. Comments/CDATA/PIs are skipped so their '<' never counts.
// On failure pos is left unchanged and the caller's closing-tag/error path
// diagnoses the unterminated element.
inline void xmlSkipNilBody(std::string_view s, size_t& pos, const std::string& elemName) {
	int depth = 0;
	size_t p = pos;
	while (p < s.size()) {
		if (s[p] == '<') {
			if (p + 1 < s.size() && s[p + 1] == '/') {
				size_t q = p + 2, e = q;
				while (e < s.size() && xmlNameChar(s[e])) e++;
				if (std::string_view(s.data() + q, e - q) == elemName) {
					if (depth == 0) {
						size_t a = e;
						while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) a++;
						if (a < s.size() && s[a] == '>') pos = a + 1;
						return;
					}
					depth--;
				}
				p = e;
				continue;
			}
			if (p + 3 < s.size() && s[p + 1] == '!' && s[p + 2] == '-' && s[p + 3] == '-') {
				size_t e = s.find("-->", p + 4);
				p = (e == std::string_view::npos) ? s.size() : e + 3;
				continue;
			}
			if (p + 8 < s.size() && s[p + 1] == '!' && s.substr(p + 2, 7) == "[CDATA[") {
				size_t e = s.find("]]>", p + 9);
				p = (e == std::string_view::npos) ? s.size() : e + 3;
				continue;
			}
			if (p + 2 < s.size() && s[p + 1] == '?') {
				size_t e = s.find("?>", p + 2);
				p = (e == std::string_view::npos) ? s.size() : e + 2;
				continue;
			}
			size_t q = p + 1, e = q;
			while (e < s.size() && xmlNameChar(s[e])) e++;
			// A '<' with no name chars following (< junk, malformed input) has
			// an empty name that never matches elemName, so p advances past the
			// '<' by exactly one and the remainder scans as ordinary content
			// until the next '<' -- O(1) per stray '<'. A pathological body of
			// repeated MATCHING opening tags that each lack a '>' does rescan to
			// EOF per tag (O(n^2) worst case, no crash).
			if (std::string_view(s.data() + q, e - q) == elemName) {
				// Only a genuinely OPENING tag balances against a later </name>.
				// A self-closing tag -- "<x/>", "<x />" or "<x a=\"1\"/>" -- completes
				// its element instead of opening a nested one, so counting it left
				// the outer </name> unbalanced: <x xsi:nil="true"><x/></x> never found
				// its closing tag, pos did not advance, and the leftover </x> was
				// later parsed as a new element ("empty element name").
				// The tag end has to be found by scanning, not by skipping blanks
				// and expecting '>': that only recognises an attribute-less tag, so
				// <x attr="v"> was taken for self-closing, the inner </x> was
				// matched at depth 0, and the outer </x> was left behind (the same
				// false match also swallowed the unterminated-element diagnostic).
				// Quoted attribute values may legally hold '>' ('<x a=">">') and
				// '/' ('<x a="b/c">'), so the scan skips over quoted runs first and
				// only stops on an unquoted '/'.
				size_t a = e;
				while (a < s.size() && s[a] != '>' && s[a] != '/') {
					if (s[a] == '"' || s[a] == '\'') {
						char quote = s[a++];
						while (a < s.size() && s[a] != quote) a++;
						if (a < s.size()) a++;
					} else {
						a++;
					}
				}
				// Unterminated tag (no '>' at all) is not counted: the body scan
				// then runs to EOF and the caller reports it as unterminated.
				if (a < s.size() && s[a] != '/') depth++;
			}
			p = e;
			continue;
		}
		p++;
	}
}

// Forward declaration
// collapseSameNamed toggles XML's "element whose only children share a name
// collapses to a bare array" rule. XML needs it ({"bin":[a,b]} is encoded as
// <bin><item/>x2</bin>); SVG must NOT collapse, because its encoder names the
// repeated elements after the JSON key ({"defs":{"clipPath":[a,b]}} ->
// <defs><clipPath/>x2</defs>), so the key is recoverable only when the
// children stay grouped under it.
inline std::unique_ptr<asvJSONValue> xmlParseElement(std::string_view s, size_t& pos, std::string& elemName, int depth, std::unordered_map<std::string, std::string>& nsMap, bool collapseSameNamed);

// Segment for interleaved mixed content tracking
struct XmlSegment {
	bool isElement;
	bool isCdata;  // text came from a CDATA section: always character data
	std::string text;      // for text segments
	std::string childName; // for element segments
	std::unique_ptr<asvJSONValue> childVal;
};

// Parse a single element's children into segments (preserving interleaved order)
inline void xmlParseChildrenSegments(std::string_view s, size_t& pos,
	std::vector<XmlSegment>& segments, int depth,
	std::unordered_map<std::string, std::string>& nsMap, bool collapseSameNamed)
{
	while (pos < s.size()) {
		if (s[pos] == '<') {
			if (pos + 1 < s.size() && s[pos + 1] == '/') break;
			if (pos + 1 < s.size() && s[pos + 1] == '!') {
				if (pos + 3 < s.size() && s[pos + 2] == '-' && s[pos + 3] == '-') {
					size_t end = s.find("-->", pos + 4);
					if (end == std::string_view::npos) throw asvJSONError("XML: unterminated comment");
					pos = end + 3; continue;
				}
				if (pos + 8 < s.size() && s.substr(pos + 2, 7) == "[CDATA[") {
					size_t end = s.find("]]>", pos + 9);
					if (end == std::string_view::npos) throw asvJSONError("XML: unterminated CDATA section");
					std::string cdata = std::string(s.substr(pos + 9, end - pos - 9));
					if (!cdata.empty()) {
						// CDATA is character data per the spec -- flagged so the
						// text is never re-interpreted as a number/bool/null.
						segments.push_back({false, true, std::move(cdata), "", nullptr});
					}
					pos = end + 3; continue;
				}
				if (pos + 9 < s.size() && s.substr(pos + 2, 7) == "DOCTYPE") {
					size_t scan = pos + 9;
					int bracketDepth = 0;
					while (scan < s.size()) {
						if (s[scan] == '[') bracketDepth++;
						else if (s[scan] == ']') bracketDepth--;
						else if (s[scan] == '>' && bracketDepth <= 0) break;
						scan++;
					}
					if (scan >= s.size()) throw asvJSONError("XML: unterminated DOCTYPE");
					pos = scan + 1; continue;
				}
				break;
			}
			if (pos + 1 < s.size() && s[pos + 1] == '?') {
				size_t end = s.find("?>", pos + 2);
				if (end == std::string_view::npos) throw asvJSONError("XML: unterminated processing instruction");
				pos = end + 2; continue;
			}
			std::string childName;
			auto childVal = xmlParseElement(s, pos, childName, depth + 1, nsMap, collapseSameNamed);
			if (childVal) segments.push_back({true, false, "", std::move(childName), std::move(childVal)});
		} else {
			size_t start = pos;
			while (pos < s.size() && s[pos] != '<') pos++;
			std::string text = xmlDecodeEntities(s.substr(start, pos - start));
			bool onlySpace = true;
			for (auto c : text) {
				if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { onlySpace = false; break; }
			}
			if (!onlySpace) segments.push_back({false, false, std::move(text), "", nullptr});
		}
	}
}

// Parse a complete XML element: <name attrs...>content</name> or <name attrs.../>
inline std::unique_ptr<asvJSONValue> xmlParseElement(std::string_view s, size_t& pos, std::string& elemName, int depth, std::unordered_map<std::string, std::string>& nsMap, bool collapseSameNamed) {
	if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) throw asvJSONError("XML nesting too deep");
	// Namespace declarations are element-scoped: copy the inherited bindings,
	// accumulate this element's own xmlns declarations into the copy, and hand
	// the copy to children. Deeper elements never leak a binding back out, so
	// a prefix declared inside one sibling is invisible to the next.
	std::unordered_map<std::string, std::string> scopedNs = nsMap;
	if (pos >= s.size() || s[pos] != '<') throw asvJSONError("expected '<'");
	pos++; // skip '<'

	elemName = xmlParseName(s, pos);
	if (elemName.empty()) throw asvJSONError("empty element name");

	// Parse attributes
	std::unordered_map<std::string, std::string> attrs;
	while (pos < s.size()) {
		xmlSkipSpaces(s, pos);
		if (pos >= s.size()) break;
		if (s[pos] == '/' || s[pos] == '>') break;
		std::string attrName = xmlParseName(s, pos);
		if (attrName.empty()) throw asvJSONError("invalid attribute name");
		xmlSkipSpaces(s, pos);
		if (pos < s.size() && s[pos] == '=') {
			pos++;
			xmlSkipSpaces(s, pos);
			if (pos < s.size() && (s[pos] == '"' || s[pos] == '\'')) {
				// Duplicate attributes are forbidden by the XML spec; the
				// first value would otherwise be silently overwritten.
				if (!attrs.emplace(attrName, xmlParseAttrValue(s, pos)).second)
					throw asvJSONError("XML: duplicate attribute \"" + attrName + "\"");
			}
		} else {
			// Bare attribute name without '=' is malformed XML; reject it
			// instead of silently dropping the name.
			throw asvJSONError("XML: attribute \"" + attrName + "\" has no value");
		}
	}

	// Accumulate any xmlns declarations made on THIS element into the scoped
	// map (the copy is discarded when the element returns, so bindings stay
	// within this element's subtree).
	for (const auto& [k, v] : attrs) {
		if (k.size() > 6 && k.substr(0, 6) == "xmlns:") scopedNs[std::string(k.substr(6))] = v;
	}

	// xsi:nil detection: check any prefix:*nil attribute whose prefix
	// maps to http://www.w3.org/2001/XMLSchema-instance.
	// Also accepts literal xsi:nil without xmlns declaration (common shorthand).
	{
		static const std::string xsiUri = "http://www.w3.org/2001/XMLSchema-instance";
		// Find any attribute ending with ":nil" matching XSI namespace
		for (const auto& [k, v] : attrs) {
			if (k.size() < 5 || k.substr(k.size() - 4) != ":nil") continue;
			std::string prefix(k.data(), k.size() - 4);
			// Accept if prefix maps to XSI URI, or if literal "xsi" without xmlns decl
			auto it = scopedNs.find(prefix);
			bool match = (it != scopedNs.end() && it->second == xsiUri);
			if (!match && prefix == "xsi" && !scopedNs.count("xsi")) match = true;
			if (!match) continue;
			if (v == "true" || v == "1") {
				if (pos < s.size() && s[pos] == '/') {
					pos++;
					if (pos < s.size() && s[pos] == '>') pos++;
					else throw asvJSONError("XML: malformed self-closing tag in <" + elemName + ">");
				} else if (pos < s.size() && s[pos] == '>') {
					pos++;
					// Skip the (usually empty) body to the matching closing
					// tag -- depth-aware so nested same-name content is skipped
					// wholesale rather than stopping at its </name>.
					// xmlSkipNilBody leaves pos untouched when no matching close
					// exists, so an unterminated element is reported here instead
					// of being silently accepted as null with the trailing
					// content left for the caller to misparse.
					size_t bodyStart = pos;
					xmlSkipNilBody(s, pos, elemName);
					if (pos == bodyStart)
						throw asvJSONError("unterminated element: <" + elemName + "> (expected </" + elemName + ">)");
				}
				return xmlNonNull(asvJSONValue::makeNull());
			}
		}
	}

	// asv:xc / asv:arr markers (from toXML) pin mixed-content and single-element
	// array shapes respectively. Both must be extracted before the self-closing
	// split so they apply consistently to <x/> and <x></x>. Consumed here;
	// never become @attrs.
	bool forceMixed = false;
	auto xcIt = attrs.find("asv:xc");
	if (xcIt != attrs.end()) { attrs.erase(xcIt); forceMixed = true; }
	bool forceArray = false;
	auto arrIt = attrs.find("asv:arr");
	if (arrIt != attrs.end()) { attrs.erase(arrIt); forceArray = true; }

	// Self-closing tag
	if (pos < s.size() && s[pos] == '/') {
		pos++;
		if (pos < s.size() && s[pos] == '>') pos++;
		else throw asvJSONError("XML: malformed self-closing tag in <" + elemName + ">");
		if (forceArray) return xmlNonNull(asvJSONValue::makeArray());
		return xmlEmptyElement(attrs, forceMixed);
	}

	// Opening tag
	if (pos < s.size() && s[pos] == '>') pos++;
	else throw asvJSONError("expected '>'");

	// Parse children and text content
	std::vector<XmlSegment> segments;
	xmlParseChildrenSegments(s, pos, segments, depth + 1, scopedNs, collapseSameNamed);

	// Closing tag
	xmlSkipSpaces(s, pos);
	if (pos + 1 < s.size() && s[pos] == '<' && s[pos + 1] == '/') {
		pos += 2;
		std::string closeName = xmlParseName(s, pos);
		if (closeName != elemName) throw asvJSONError("mismatched closing tag: " + closeName + " != " + elemName);
		xmlSkipSpaces(s, pos);
		if (pos < s.size() && s[pos] == '>') pos++;
	} else if (pos >= s.size()) {
		throw asvJSONError("unterminated element: <" + elemName + "> (expected </" + elemName + ">)");
	} else {
		throw asvJSONError("XML: expected closing tag for <" + elemName + ">");
	}

	// Analyze segments: count text vs element segments
	size_t textCount = 0, elemCount = 0;
	std::string combinedText;
	bool hasCdata = false;
	bool hasInterleaving = false;
	for (size_t i = 0; i < segments.size(); i++) {
		if (segments[i].isElement) {
			elemCount++;
		} else {
			textCount++;
			combinedText += segments[i].text;
			if (segments[i].isCdata) hasCdata = true;
		}
	}
	// Interleaving = at least one text segment after any child element. The
	// decoder is deliberately asymmetric about leading text: "foo<b/>" routes
	// to the object path below ({#text, children}) rather than #content; only
	// text that follows an element -- or an explicit asv:xc marker -- produces
	// a #content array. The encoder compensates: it emits asv:xc anytime the
	// value has #content, so round-tripped XML always takes the #content side.
	bool seenElem = false, textAfterElem = false;
	for (size_t i = 0; i < segments.size(); i++) {
		if (segments[i].isElement) {
			seenElem = true;
		} else if (seenElem) {
			textAfterElem = true;
			break;
		}
	}
	hasInterleaving = textAfterElem;

	// Build result
	if (segments.empty()) {
		// Empty element content. <x/> and <x></x> mean the same thing, so both
		// routes share xmlEmptyElement: an asv:xc marker restores an empty
		// #content array, a lone type="..." attribute pins an empty typed value
		// (STRING/BINARY/ARRAY/OBJECT/EXTENSION), and an attribute-only element
		// stays an @-prefixed object. (An asv:arr-marked empty is still an
		// empty array -- the marker is only emitted for non-empty arrays.)
		if (forceArray) return xmlNonNull(asvJSONValue::makeArray());
		return xmlEmptyElement(attrs, forceMixed);
	}

	// asv:arr marker (single-element array from toXML): the child elements are
	// the array items, so collect them regardless of the count-based collapse
	// rule. Whitespace-only text segments (encoder indentation) carry no items.
	if (forceArray) {
		auto arr = asvJSONValue::makeArray();
		if (!arr) throw asvJSONError("XML: out of memory");
		for (auto& seg : segments)
			if (seg.isElement) {
				if (!seg.childVal) throw asvJSONError("XML: out of memory");
				arr->arr->push_back(std::move(seg.childVal));
			}
		return arr;
	}

	// No elements, only text - simple case
	if (elemCount == 0) {
		if (forceMixed) {
			// <x asv:xc="1">text</x>: pure-text marker -- restore the original
			// #content array of strings (do not value-detect).
			auto xobj = asvJSONValue::makeObject();
			if (!xobj) throw asvJSONError("XML: out of memory");
			auto xarr = asvJSONValue::makeArray();
			if (!xarr) throw asvJSONError("XML: out of memory");
			for (size_t i = 0; i < segments.size(); i++)
				if (!segments[i].isElement && !segments[i].text.empty())
					xarr->arr->push_back(xmlMakeString(segments[i].text.data(), segments[i].text.size()));
			xobj->obj->emplace("#content", std::move(xarr));
			for (const auto& [k, v] : attrs) xobj->obj->emplace(xmlAttrToKey(k), xmlMakeString(v.data(), v.size()));
			return xobj;
		}
		if (attrs.empty()) {
			// CDATA is character data by definition: its text (possibly mixed
			// with ordinary text) must never be re-detected as a number/bool/
			// null, so any CDATA presence forces a plain STRING.
			if (hasCdata) return xmlMakeString(combinedText.data(), combinedText.size());
			return xmlNonNull(xmlDetectValue(combinedText));
		}
		// Check for special type attribute (from toXML)
		auto tit = attrs.find("type");
		if (tit != attrs.end()) {
			const std::string& t = tit->second;
			if (t == "string") {
				// Force STRING (preserves numeric/bool/null-looking literals,
				// empty text included). With other attributes the element is
				// an object: @attrs plus a #text that must stay STRING (this
				// is what toXML emits for #text-bearing objects).
				if (attrs.size() == 1) return xmlMakeString(combinedText.data(), combinedText.size());
				auto sobj = asvJSONValue::makeObject();
				if (!sobj) throw asvJSONError("XML: out of memory");
				for (const auto& [k, v] : attrs) if (k != "type") sobj->obj->emplace(xmlAttrToKey(k), xmlMakeString(v.data(), v.size()));
				sobj->obj->emplace("#text", xmlMakeString(combinedText.data(), combinedText.size()));
				return sobj;
			}
			if (t == "datetime" && !combinedText.empty()) {
				time_t ts = 0; int ms = 0; int tz = 0;
				if (tryParseDateTime(combinedText, ts, &ms, &tz)) return xmlNonNull(asvJSONValue::makeDateTime(ts, ms, tz));
			}
			if (t == "binary" && !combinedText.empty()) {
				std::string clean = xmlStripWs(combinedText);
				auto decoded = decodeBase64Fast(clean.data(), clean.size());
				if (!decoded.empty()) return xmlNonNull(asvJSONValue::makeBinary(decoded.data(), decoded.size()));
			}
			if (t == "objectid" && !combinedText.empty()) {
				bool hexOk = true;
				for (char c : combinedText)
					if (!std::isxdigit(static_cast<unsigned char>(c))) { hexOk = false; break; }
				if (hexOk && combinedText.size() == 24) {
					std::string oid; oid.reserve(12);
					for (size_t i = 0; i + 1 < combinedText.size(); i += 2) {
						char buf[3] = {combinedText[i], combinedText[i+1], 0};
						oid.push_back(static_cast<char>(std::strtol(buf, nullptr, 16)));
					}
					if (oid.size() == 12) return xmlNonNull(asvJSONValue::makeObjectId(std::string_view(oid.data(), 12)));
				}
			}
			if (t == "regex" && !combinedText.empty()) {
				// The last '|' (when present) is the mandatory terminator
				// emitted by fmtRegexXml; what remains splits into pattern and
				// options at its own last '|'. A body without a terminator is
				// legacy unescaped XML: the historical rfind('|') split is kept
				// so previously written documents still decode as before.
				std::string body = combinedText;
				if (!body.empty() && body.back() == '|') body.pop_back();
				size_t sep = body.rfind('|');
				std::string pat = (sep == std::string::npos) ? body : body.substr(0, sep);
				std::string opt = (sep == std::string::npos) ? std::string() : body.substr(sep + 1);
				pat = xmlUnescRegexText(pat);
				opt = xmlUnescRegexText(opt);
				if (!pat.empty()) {
					auto rn = asvJSONValue::makeRegex(pat.c_str(), opt.empty() ? nullptr : opt.c_str());
					return xmlNonNull(std::move(rn));
				}
			}
			if (t == "timestamp" && !combinedText.empty()) {
				long long v;
				auto [ptr, ec] = std::from_chars(combinedText.data(), combinedText.data() + combinedText.size(), v);
				if (ec == std::errc()) return xmlNonNull(asvJSONValue::makeTimestamp(v));
			}
			if (t == "extension" && !combinedText.empty()) {
				std::string clean = xmlStripWs(combinedText);
				auto decoded = decodeBase64Fast(clean.data(), clean.size());
				if (!decoded.empty()) {
					auto eit = attrs.find("exttype");
					int8_t et = 0;
					if (eit != attrs.end()) {
						long ev = std::strtol(eit->second.c_str(), nullptr, 10);
						if (ev >= -128 && ev <= 127) et = static_cast<int8_t>(ev);
					}
					return xmlNonNull(asvJSONValue::makeExtension(et, decoded.data(), decoded.size()));
				}
			}
		}
		// Has attributes - object with #text + @attr
		auto obj = asvJSONValue::makeObject();
		if (!obj) throw asvJSONError("XML: out of memory");
		for (const auto& [k, v] : attrs) obj->obj->emplace(xmlAttrToKey(k), xmlMakeString(v.data(), v.size()));
		if (!combinedText.empty()) obj->obj->emplace("#text", xmlNonNull(xmlDetectValue(combinedText)));
		return obj;
	}

	// Has child elements (possibly interleaved with text)
	auto obj = asvJSONValue::makeObject();
	if (!obj) throw asvJSONError("XML: out of memory");

	// type="string" pins the element's own text as a STRING. The encoder emits
	// it for a #text that would otherwise re-detect as a number/bool/null, and
	// the child elements must not change that: without honoring it here the
	// annotation re-appeared as a phantom @type and the text was re-detected
	// ({"#text":"123", child:1} -> {@type:"string", #text:123, child:1}).
	// Mirrors the type="string" handling of the text-only branch above. The
	// annotation is consumed (never becomes an @-key) so it is not re-emitted
	// on a second round trip.
	//
	// The two branches cannot simply share one flag: here the annotation is
	// dropped from attrs regardless of what else the element carries (an
	// element with children plus other @attrs still must not grow a phantom
	// @type), whereas the text-only branch only returns a typed value when the
	// attribute set is otherwise empty. Same detection, different bookkeeping.
	bool pinTextString = false;
	{
		auto tit = attrs.find("type");
		if (tit != attrs.end() && tit->second == "string") pinTextString = true;
	}

	// Add attributes. The type="string" annotation is consumed here; a user
	// @type arrives as the reserved asv:ut attribute and maps back to @type.
	for (const auto& [k, v] : attrs) {
		if (pinTextString && k == "type") continue;
		obj->obj->emplace(xmlAttrToKey(k), xmlMakeString(v.data(), v.size()));
	}

	if (hasInterleaving || forceMixed) {
		// Mixed content: preserve interleaved order as #content array
		auto arr = asvJSONValue::makeArray();
		if (!arr) throw asvJSONError("XML: out of memory");
		for (auto& seg : segments) {
			if (seg.isElement) {
				// Internal empty-marker element (<asv:s/>) between adjacent
				// #content strings: dropped so the original string split is
				// restored on a re-parse. Only honored for marker-pinned
				// content; a real <asv:s/> in external XML stays data.
				if (forceMixed && seg.childName == "asv:s" &&
				    seg.childVal && seg.childVal->type == asvJSONValue::STRING &&
				    seg.childVal->str_data.empty())
					continue;
				if (!seg.childVal) throw asvJSONError("XML: out of memory");
				auto wrapper = asvJSONValue::makeObject();
				if (!wrapper) throw asvJSONError("XML: out of memory");
				wrapper->obj->emplace(seg.childName, std::move(seg.childVal));
				arr->arr->push_back(std::move(wrapper));
			} else {
				// Text in mixed content is character data, like CDATA: always
				// a raw STRING, never value-detected. Marker-pinned content
				// must stay raw so {"#content":["123"]} round-trips instead of
				// becoming [123]; the same rule now covers external interleaved
				// text. Detecting it produced a scalar ({"#content":[10,...]})
				// that the encoder had to wrap as <item>10</item>, re-parsing as
				// {"item":10} -- a one-way reshape that broke external
				// mixed-content round-trips. Always-string closes that gap.
				arr->arr->push_back(xmlMakeString(seg.text.data(), seg.text.size()));
			}
		}
		obj->obj->emplace("#content", std::move(arr));
	} else {
		// Non-interleaved: all text first (if any), then all elements
		// Add text content as #text (CDATA content stays raw STRING)
		if (!combinedText.empty()) {
			if (hasCdata || pinTextString) obj->obj->emplace("#text", xmlMakeString(combinedText.data(), combinedText.size()));
			else obj->obj->emplace("#text", xmlNonNull(xmlDetectValue(combinedText)));
		} else if (pinTextString) {
			// type="string" with no text run at all still denotes an empty
			// STRING: this is what toXML emits for {#text:"", child:1}.
			obj->obj->emplace("#text", xmlMakeString("", 0));
		}

		// Count occurrences of each child name (global, not just consecutive)
		std::unordered_map<std::string, size_t> nameCounts;
		for (auto& seg : segments) {
			if (seg.isElement) nameCounts[seg.childName]++;
		}

		// Add children, grouping into arrays for names with count > 1
		for (auto& seg : segments) {
			if (!seg.isElement) continue;
			if (nameCounts[seg.childName] > 1) {
				auto it = obj->obj->find(seg.childName);
				if (it == obj->obj->end()) {
					auto arr = asvJSONValue::makeArray();
					if (!arr) throw asvJSONError("XML: out of memory");
					obj->obj->emplace(seg.childName, std::move(arr));
					it = obj->obj->find(seg.childName);
				}
				if (!seg.childVal) throw asvJSONError("XML: out of memory");
				it->second->arr->push_back(std::move(seg.childVal));
			} else {
				if (!seg.childVal) throw asvJSONError("XML: out of memory");
				obj->obj->emplace(seg.childName, std::move(seg.childVal));
			}
		}
	}

	// Collapse: if element has only children (no attrs, no text, no #content),
	// and all children are same-named, return the array directly. Skipped for
	// SVG (fromSVG passes collapseSameNamed=false) so the JSON key survives.
	if (collapseSameNamed && attrs.empty() && combinedText.empty() && !hasInterleaving) {
		std::unordered_map<std::string, size_t> nameCounts2;
		for (auto& seg : segments) {
			if (seg.isElement) nameCounts2[seg.childName]++;
		}
		for (const auto& [k, v] : nameCounts2) {
			if (v > 1 && nameCounts2.size() == 1) {
				auto it = obj->obj->find(k);
				if (it != obj->obj->end() && it->second->type == asvJSONValue::ARRAY) {
					auto arr = std::move(it->second);
					return arr;
				}
			}
		}
	}

	return obj;
}

// Parse an XML document into a root object. Throws asvJSONError on failure.
// Shared by asvJSON::fromXML (collapseSameNamed=true) and asvJSON::fromSVG
// (collapseSameNamed=false, see the forward declaration above).
inline std::unique_ptr<asvJSONValue> xmlParseDocument(std::string_view input, bool collapseSameNamed) {
	if (input.empty()) throw asvJSONError("empty input");

	// Strip UTF-8 BOM (EF BB BF) if present (mirrors the JSON parser).
	if (input.size() >= 3 && (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
		input = input.substr(3);

	size_t pos = 0;

	// Skip XML declaration <?xml ...?>
	xmlSkipSpaces(input, pos);
	if (pos + 5 <= input.size() && input.substr(pos, 5) == "<?xml") {
		size_t end = input.find("?>", pos + 2);
		if (end == std::string_view::npos) throw asvJSONError("unclosed XML declaration");
		pos = end + 2;
	}

	// Skip comments and DOCTYPE before root element
	auto skipProlog = [&]() -> bool {
		while (pos < input.size()) {
			xmlSkipSpaces(input, pos);
			if (pos + 4 < input.size() && input.substr(pos, 4) == "<!--") {
				size_t end = input.find("-->", pos + 4);
				if (end == std::string_view::npos) throw asvJSONError("unclosed comment");
				pos = end + 3;
				continue;
			}
			if (pos + 9 < input.size() && input.substr(pos, 9) == "<!DOCTYPE") {
				size_t scan = pos + 9;
				int bracketDepth = 0;
				while (scan < input.size()) {
					if (input[scan] == '[') bracketDepth++;
					else if (input[scan] == ']') bracketDepth--;
					else if (input[scan] == '>' && bracketDepth <= 0) break;
					scan++;
				}
				if (scan >= input.size()) throw asvJSONError("unclosed DOCTYPE");
				pos = scan + 1;
				continue;
			}
			break;
		}
		return pos < input.size() && input[pos] == '<';
	};

	if (!skipProlog()) throw asvJSONError("expected '<'");

	// Parse all top-level elements, combining into one object. XML allows a
	// single root only; repeated roots (tolerated as an extension) merge
	// last-wins so the final element for a name is the surviving value.
	auto obj = asvJSONValue::makeObject();
	if (!obj) throw asvJSONError("out of memory");
	std::unordered_map<std::string, std::string> nsMap;
	int count = 0;
	while (pos < input.size()) {
		if (!skipProlog()) break;
		std::string name;
		auto val = xmlParseElement(input, pos, name, 0, nsMap, collapseSameNamed);
		if (!val) break;
		obj->obj->insert_or_assign(std::move(name), std::move(val));
		count++;
	}
	if (count == 0) throw asvJSONError("failed to parse root element");

	return obj;
}

inline bool asvJSON::fromXML(std::string_view input) {
	root = nullptr;
	dirty_ = false;
	formatType = FormatType::Unknown;
	try {
		auto obj = xmlParseDocument(input, /*collapseSameNamed=*/true);
		root = std::move(obj);
		formatType = FormatType::XML;
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	} catch (const std::exception& e) {
		// bad_alloc / length_error etc. must not escape: the public contract is
		// bool + lastError, like the other from* parsers (see fromMessagePack).
		lastError = e.what();
		root = nullptr;
		return false;
	}
}

} // namespace asvJSONInternal
