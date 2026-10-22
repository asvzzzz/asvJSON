#pragma once
// SVG (Scalable Vector Graphics) serialization/parsing for asvJSON++
//
// SVG is XML-based, so fromSVG delegates to the existing XML parser after BOM
// stripping. The encoder produces valid SVG 1.1 markup from JSON trees.
//
// Encoder conventions:
//   - Root object keys become SVG element names (svg, rect, circle, ellipse,
//     line, polyline, polygon, path, text, g, defs, clipPath, etc.)
//   - Object properties starting with '@' become SVG attributes
//     (e.g. {"@x": "10", "@y": "20"} -> <rect x="10" y="20"/>; keys without
//     '@' are child elements, not attributes)
//   - The special key "#text" becomes element text content
//   - Scalar children (INT/DOUBLE/BOOL_VAL) are also emitted as text content
//     (e.g. {"text": 42} -> <text>42</text>)
//   - Arrays of objects create multiple child elements with the same parent tag
//   - Numbers in attributes are emitted as bare numeric values
//   - Booleans become "true"/"false" strings
//   - Null values are skipped (no attribute emitted)
//   - Void (self-closing) elements: rect, circle, ellipse, line, path, polyline,
//     polygon - children are silently ignored if present. Note: `image` and `use`
//     are container elements per SVG spec and are NOT self-closing.
//
// Known limitations:
//   - Mixed content ordering: "#text" nodes are always emitted BEFORE child
//     elements, not interleaved. Pure-text and pure-element parents are fine;
//     true mixed-order round-trip is not supported.
//   - Single- or empty-element arrays lose their array-ness on round-trip: the
//     encoder emits one <key> element (indistinguishable from an object value)
//     and the SVG side has no asv:arr-style marker to recover it (only the top-level
//     XML encoder emits one). Multi-element arrays round-trip via the repeated
//     sibling elements plus the no-collapse decode (see Decoder below).
//     An empty array decodes back as an empty STRING ("") rather than null.
//   - Non-OBJECT root: scalar/null/ARRAY roots are replaced by an empty
//     <svg xmlns="..."></svg> - data is silently lost. Use an object root.
//   - Multi-key root objects are wrapped in <svg> (guarantees single XML root).
//   - Empty-string root key ("") produces element "<element>" (fallback name).
//   - Element names are sanitized with xmlSanitizeElementName (the same rule the
//     XML encoder uses): a key holding characters the XML name grammar forbids
//     ("k[0]", "a b", "&a", "@x") maps them to '_', and a name starting with a
//     digit or '-' gets an "item" prefix. Valid SVG names (rect, circle,
//     clipPath, ...) are untouched, so this only changes keys that used to
//     produce markup no XML parser accepts (fromSVG itself rejected it).
//   - Non-serializable children of elements (DATETIME, BINARY, OBJECTID, REGEX,
//     TIMESTAMP, EXTENSION, CUSTOM_TAG): the value is lost, the element is emitted
//     as an empty <tag></tag>. OBJECT and ARRAY children are recursively serialized.
//   - Non-OBJECT root values (scalars, ARRAY, and special types) are wrapped in
//     <root>...</root> by free toSVG(const asvJSONValue*); scalar content is
//     preserved but ARRAY/special-type contents are lost - documented limitation.
//   - Empty #text ("") on elements that round-trip as self-closing is silently
//     dropped: {"g":{"@class":"x","#text":""}} -> <g class="x"/> -> {"g":{"@class":"x"}}.
//     Semantically empty text == absent, but the shape changes from {#text:""} to absent.
//   - svgSerializeElement has no depth guard - deeply nested programmatic trees can
//     overflow the stack. Consistent with other encoders (toXML/toUBJSON/etc.).
//   - A C0 control byte other than tab, LF and CR has NO legal XML 1.0 spelling
//     (the Char production excludes it and a character reference may not name it
//     either), so toSVG() REPORTS it with an asvJSONError instead of writing it
//     raw - the markup would otherwise be one that no conforming parser accepts.
//
// Decoder: since SVG is XML, fromSVG reuses the shared XML document parser
// (xmlParseDocument) but with collapseSameNamed=false. XML needs the collapse
// rule ({"bin":[a,b]} is written as <bin><item/>x2</bin>, so an element whose
// only children share a name must fold back to a bare array); SVG must not use
// it, because this encoder names the repeated elements after the JSON key
// ({"defs":{"clipPath":[a,b]}} -> <defs><clipPath/>x2</defs>), so keeping the
// children grouped under their name preserves the key. Without that flag the
// round-trip dropped the key ({"defs":[{...},{...}]}).

#include <string>
#include <string_view>
#include <cmath>
#include "../detail/escape.hpp"

namespace asvJSONInternal {

struct asvJSONValue;

inline void appendSvgAttr(std::string& out, std::string_view name, const asvJSONValue* v);
inline void svgSerializeElement(std::string& out, std::string_view tag, const asvJSONValue* v, int indent);
inline void asvJSONValue_toSVG(const asvJSONValue* v, std::string& out);

// Point 8: check v == nullptr at the start.
// Point 2: compute value in a local buffer first, then write name=value atomically.
// Point 3: escape attribute values (&, ", <, >).
// Point 5: support ARRAY values - iterate elements and join with spaces.
inline void appendSvgAttr(std::string& out, std::string_view name, const asvJSONValue* v) {
	if (name.empty() || name[0] != '@') return;
	if (!v) return; // Point 8

	std::string key = std::string(name).substr(1); // strip '@'
	if (key.empty()) return;

	// Compute value in a local buffer first (Point 2 - atomic write)
	bool hasValue = false;
	std::string valBuf;
	switch (v->type) {
		case asvJSONValue::STRING:
			valBuf = xmlEscapeContent(v->str_data); // Point 3: escape
			hasValue = true;
			break;
		case asvJSONValue::INT:
			valBuf = std::to_string(v->num);
			hasValue = true;
			break;
		case asvJSONValue::DOUBLE: {
			if (std::isnan(v->dbl) || std::isinf(v->dbl))
				return; // skip non-finite attributes
			fmtDoubleValDot0(v->dbl, valBuf);
			valBuf = xmlEscapeContent(valBuf); // Point 3
			hasValue = true;
			break;
		}
		case asvJSONValue::BOOL_VAL:
			valBuf = v->flag ? "true" : "false";
			hasValue = true;
			break;
		case asvJSONValue::NULL_VAL:
			return; // skip null attributes
		case asvJSONValue::ARRAY: {
			// Point 5: iterate array elements, join with spaces
			if (!v->arr) break; // safety: OOM can leave arr null even for ARRAY type
			bool first = true;
			for (const auto& elem : *(v->arr)) {
				if (!elem) continue;
				std::string item;
				switch (elem->type) {
					case asvJSONValue::STRING:   item = xmlEscapeContent(elem->str_data); break;
					case asvJSONValue::INT:      item = std::to_string(elem->num); break;
					case asvJSONValue::DOUBLE:
						if (std::isnan(elem->dbl) || std::isinf(elem->dbl)) continue; // skip non-finite array items
						fmtDoubleValDot0(elem->dbl, item);
						item = xmlEscapeContent(item);
						break;
					case asvJSONValue::BOOL_VAL: item = elem->flag ? "true" : "false"; break;
					default: continue;
				}
				if (!first) valBuf += ' ';
				valBuf += item;
				first = false;
			}
			hasValue = true; // emit even if array is empty -> key=""
			break;
		}
		default:
			return; // objects/other not valid as single attributes
	}
	if (!hasValue) return;

	out += ' ';
	out += key;
	out += '=';
	out += '"';
	out += valBuf;
	out += '"';
}

// Point 1: only true void elements are self-closing. Container tags removed.
// Condition changed from `isSelfClosing || !hasContent` to just `!hasContent`.
inline void svgSerializeElement(std::string& out, std::string_view tag, const asvJSONValue* v, int indent) {
	if (tag.empty()) return;

	// The element name comes from a JSON key, which may hold characters the
	// XML name grammar forbids ("k[0]", "a b", "&a"). Emitting them verbatim
	// produced markup no XML parser accepts - not even fromSVG itself ("invalid
	// attribute name"). Sanitize it the same way the XML encoder does: a valid
	// SVG name (rect, circle, clipPath, ...) is untouched, an invalid character
	// maps to '_' and a leading digit or '-' gets an "item" prefix.
	std::string safeTag = xmlSanitizeElementName(tag);

	// True void (self-closing) SVG elements only. Note: `image` and `use` are
	// container elements per SVG spec and must NOT be in this list.
	static const char* selfClosing[] = {
		"rect", "circle", "ellipse", "line", "path", "polyline", "polygon", nullptr
	};
	bool isSelfClosing = false;
	for (const char** p = selfClosing; *p; ++p) {
		if (*p == safeTag) { isSelfClosing = true; break; }
	}

	if (v && v->type == asvJSONValue::OBJECT && v->obj) {
		out += std::string((size_t)indent, ' ');
		out += '<';
		out += safeTag;
		// Emit attributes first (@ prefixed keys)
		for (const auto& [k, val] : *(v->obj)) {
			appendSvgAttr(out, k, val.get());
		}

		// First pass: collect text content and check for element children
		std::string textContent;
		bool hasElementChildren = false;
		for (const auto& [k, val] : *(v->obj)) {
			if (!k.empty() && k[0] == '@') continue; // skip attributes
			if (k == "#text") {
				if (val) {
					switch (val->type) {
						case asvJSONValue::STRING:    textContent += val->str_data; break;
						case asvJSONValue::INT:       textContent += std::to_string(val->num); break;
						case asvJSONValue::DOUBLE:
							if (std::isnan(val->dbl) || std::isinf(val->dbl)) break; // skip non-finite text
							fmtDoubleValDot0(val->dbl, textContent);
							break;
						case asvJSONValue::BOOL_VAL:  textContent += val->flag ? "true" : "false"; break;
						default: break;
					}
				}
				continue;
			}
			hasElementChildren = true;
		}

		// Self-closing: void elements, or empty containers with no text/children.
		// Attributes don't require a body - <g class="foo"/> is valid XML/SVG.
		bool isEmptyLeaf = !isSelfClosing && !hasElementChildren && textContent.empty();

		if (isSelfClosing || isEmptyLeaf) {
			out += "/>\n";
			return;
		}

		// Close opening tag: with newline if element children exist, inline otherwise
		if (hasElementChildren) {
			out += ">\n";
		} else {
			out += '>';
		}

		// Emit text content: inline if no element children, indented line before them otherwise
		if (!textContent.empty()) {
			if (hasElementChildren) {
				for (int i = 0; i < indent + 2; ++i) out += ' ';
			}
			xmlEscapeTextInto(out, textContent);
			if (hasElementChildren) {
				out += '\n';
			}
		}

		// Second pass: emit child elements
		for (const auto& [k, val] : *(v->obj)) {
			if (!k.empty() && k[0] == '@') continue; // skip attributes
			if (k == "#text") continue; // already emitted above

			// Recurse for child elements
			svgSerializeElement(out, k, val.get(), indent + 2);
		}

		// Close tag (indented only when there are element children)
		if (hasElementChildren) {
			for (int i = 0; i < indent; ++i) out += ' ';
		}
		out += "</";
		out += safeTag;
		out += '>';
		out += '\n';
	} else if (v && v->type == asvJSONValue::ARRAY && v->arr) {
		if (isSelfClosing) {
			// Void element with array children: ignore children, emit self-closing
			out += std::string((size_t)indent, ' ');
			out += '<';
			out += safeTag;
			out += "/>\n";
			return;
		}
		// Empty array -> emit empty element so the key is not silently dropped.
		if (v->arr->empty()) {
			out += std::string((size_t)indent, ' ');
			out += '<';
			out += safeTag;
			out += "/>\n";
			return;
		}
		// Each array element represents a sibling with the same tag name.
		// Delegate to svgSerializeElement so @-attributes and #text are handled
		// correctly (the OBJECT branch knows how to skip @/emit as attrs,
		// and emit #text as element content). Pass indent unchanged - each
		// element writes its own opening/closing tag at this level.
		for (const auto& elem : *(v->arr)) {
			if (!elem) continue;
			svgSerializeElement(out, safeTag, elem.get(), indent);
		}
	} else if (v && v->type == asvJSONValue::STRING) {
		out += std::string((size_t)indent, ' ');
		out += '<';
		out += safeTag;
		if (isSelfClosing) {
			out += "/>\n";
			return;
		}
		out += '>';
		xmlEscapeTextInto(out, v->str_data);
		out += "</";
		out += safeTag;
		out += '>';
		out += '\n';
	} else if (v && (v->type == asvJSONValue::INT || v->type == asvJSONValue::DOUBLE || v->type == asvJSONValue::BOOL_VAL)) {
		// Scalar child values are emitted as text content (like #text)
		out += std::string((size_t)indent, ' ');
		out += '<';
		out += safeTag;
		if (isSelfClosing) {
			out += "/>\n";
			return;
		}
		out += '>';
		switch (v->type) {
			case asvJSONValue::INT:       out += std::to_string(v->num); break;
			case asvJSONValue::DOUBLE:
				if (std::isnan(v->dbl) || std::isinf(v->dbl)) break; // skip non-finite scalar child
				fmtDoubleValDot0(v->dbl, out);
				break;
			case asvJSONValue::BOOL_VAL:  out += v->flag ? "true" : "false"; break;
			default: break;
		}
		out += "</";
		out += safeTag;
		out += '>';
		out += '\n';
	} else if (isSelfClosing) {
		out += std::string((size_t)indent, ' ');
		out += '<';
		out += safeTag;
		out += "/>\n";
	} else {
		out += std::string((size_t)indent, ' ');
		out += '<';
		out += safeTag;
		out += '>';
		out += "</";
		out += safeTag;
		out += '>';
		out += '\n';
	}
}

// Point 7: escape text content in the fallback branch
inline void asvJSONValue_toSVG(const asvJSONValue* v, std::string& out) {
	if (!v) return;
	if (v->type == asvJSONValue::OBJECT && v->obj) {
		for (const auto& [k, val] : *(v->obj)) {
			svgSerializeElement(out, k, val.get(), 0);
		}
	} else {
		out += "<root>";
		switch (v->type) {
			case asvJSONValue::STRING: {
				for (char c : v->str_data) {
					switch (c) {
						case '&': out += "&amp;"; break;
						case '<': out += "&lt;"; break;
						case '>': out += "&gt;"; break;
						default:  out += c;
					}
				}
				break;
			}
			case asvJSONValue::INT:    out += std::to_string(v->num); break;
			case asvJSONValue::DOUBLE:
				if (std::isnan(v->dbl) || std::isinf(v->dbl)) break; // skip non-finite scalar root
				fmtDoubleValDot0(v->dbl, out);
				break;
			case asvJSONValue::BOOL_VAL: out += v->flag ? "true" : "false"; break;
			default: break;
		}
		out += "</root>";
	}
}

// AsvJSONValue method implementations - must be inside the namespace.
inline void asvJSONValue::toSVG(std::string& out) const {
	asvJSONInternal::asvJSONValue_toSVG(this, out);
}

inline void asvJSONValue::toSVG(std::string& out, const std::string& name, int indent) const {
	svgSerializeElement(out, name.empty() ? "element" : name, this, indent);
}

} // namespace asvJSONInternal

namespace asvJSONInternal {

// SVG is XML-based: fromSVG delegates to the shared XML document parser after
// BOM strip. collapseSameNamed=false: the SVG encoder names repeated elements
// after the JSON key ({"defs":{"clipPath":[a,b]}} -> <defs><clipPath/>x2</defs>),
// so XML's same-named-only collapse would drop the key on the way back.
inline bool asvJSON::fromSVG(std::string_view input) {
	root = nullptr;
	dirty_ = false;
	formatType = FormatType::Unknown;
	try {
		auto obj = xmlParseDocument(input, /*collapseSameNamed=*/false);
		root = std::move(obj);
		formatType = FormatType::SVG;
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	} catch (const std::exception& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	}
}

// Free function delegates to the member so both entry points produce identical output.
inline std::string toSVG(const asvJSON& doc) {
	return doc.toSVG();
}

inline std::string toSVG(const asvJSONValue* v) {
	std::string out;
	if (v) v->toSVG(out);
	return out;
}

} // namespace asvJSONInternal
