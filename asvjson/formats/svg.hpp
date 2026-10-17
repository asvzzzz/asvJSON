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
//     (e.g. {"@x": "10", "y": "20"} -> <rect x="10" y="20"/>)
//   - The special key "#text" becomes element text content
//   - Scalar children (INT/DOUBLE/BOOL_VAL) are also emitted as text content
//     (e.g. {"text": 42} -> <text>42</text>)
//   - Arrays of objects create multiple child elements with the same parent tag
//   - Numbers in attributes are emitted as bare numeric values
//   - Booleans become "true"/"false" strings
//   - Null values are skipped (no attribute emitted)
//   - Void (self-closing) elements: rect, circle, ellipse, line, path, polyline,
//     polygon — children are silently ignored if present. Note: `image` and `use`
//     are container elements per SVG spec and are NOT self-closing.
//
// Known limitations:
//   - Mixed content ordering: "#text" nodes are always emitted BEFORE child
//     elements, not interleaved. Pure-text and pure-element parents are fine;
//     true mixed-order round-trip is not supported.
//   - Non-OBJECT root: scalar/null/ARRAY roots are replaced by an empty
//     <svg xmlns="..."></svg> — data is silently lost. Use an object root.
//   - Multi-key root objects are wrapped in <svg> (guarantees single XML root).
//   - Empty-string root key ("") produces element "<element>" (fallback name);
//     @-only root objects produce invalid SVG with "@<attrname>" as element tag
//     (e.g. {"@x":"1"} -> <@x>...</@x>). Neither is valid SVG markup.
//   - Non-serializable children of elements (DATETIME, BINARY, OBJECTID, REGEX,
//     TIMESTAMP, EXTENSION, CUSTOM_TAG) are silently dropped — the element emits
//     empty tags <tag></tag>. OBJECT and ARRAY children are recursively serialized.
//   - Scalar root values are wrapped in <root>...</root> (free toSVG(const asvJSONValue*)).
//
// Decoder: since SVG is XML, fromSVG reuses fromXML directly.

#include <string>
#include <string_view>
#include <cmath>
#include "../detail/escape.hpp"

namespace asvJSONInternal {

class asvJSONValue;

inline void appendSvgAttr(std::string& out, std::string_view name, const asvJSONValue* v);
inline void svgSerializeElement(std::string& out, std::string_view tag, const asvJSONValue* v, int indent);
inline void asvJSONValue_toSVG(const asvJSONValue* v, std::string& out);

// Point 8: check v == nullptr at the start.
// Point 2: compute value in a local buffer first, then write name=value atomically.
// Point 3: escape attribute values (&, ", <, >).
// Point 5: support ARRAY values — iterate elements and join with spaces.
inline void appendSvgAttr(std::string& out, std::string_view name, const asvJSONValue* v) {
	if (name.empty() || name[0] != '@') return;
	if (!v) return; // Point 8

	std::string key = std::string(name).substr(1); // strip '@'
	if (key.empty()) return;

	// Compute value in a local buffer first (Point 2 — atomic write)
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

	// True void (self-closing) SVG elements only. Note: `image` and `use` are
	// container elements per SVG spec and must NOT be in this list.
	static const char* selfClosing[] = {
		"rect", "circle", "ellipse", "line", "path", "polyline", "polygon", nullptr
	};
	bool isSelfClosing = false;
	for (const char** p = selfClosing; *p; ++p) {
		if (*p == tag) { isSelfClosing = true; break; }
	}

	out += std::string((size_t)indent, ' ');
	out += '<';
	out += tag;

	if (v && v->type == asvJSONValue::OBJECT && v->obj) {
		// Emit attributes first (@ prefixed keys)
		for (const auto& [k, val] : *(v->obj)) {
			appendSvgAttr(out, k, val.get());
		}

		// Self-closing: always close, ignore children
		if (isSelfClosing) {
			out += "/>\n";
			return;
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
			for (char c : textContent) {
				switch (c) {
					case '&': out += "&amp;"; break;
					case '<': out += "&lt;"; break;
					case '>': out += "&gt;"; break;
					default:  out += c;
				}
			}
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
		out += tag;
		out += '>';
		out += '\n';
	} else if (v && v->type == asvJSONValue::ARRAY && v->arr) {
		if (isSelfClosing) {
			// Void element with array children: ignore children, emit self-closing
			out += "/>\n";
			return;
		}
		// Arrays: close opening tag, emit children from each object element, then close
		out += ">\n";
		for (const auto& elem : *(v->arr)) {
			if (!elem) continue;
			if (elem->type == asvJSONValue::OBJECT && elem->obj) {
				for (const auto& [k, val] : *(elem->obj)) {
					svgSerializeElement(out, k, val.get(), indent + 2);
				}
			}
		}
		out += std::string((size_t)indent, ' ');
		out += "</";
		out += tag;
		out += '>';
		out += '\n';
	} else if (v && v->type == asvJSONValue::STRING) {
		if (isSelfClosing) {
			out += "/>\n";
			return;
		}
		out += '>';
		for (char c : v->str_data) {
			switch (c) {
				case '&': out += "&amp;"; break;
				case '<': out += "&lt;"; break;
				case '>': out += "&gt;"; break;
				default:  out += c;
			}
		}
		out += "</";
		out += tag;
		out += '>';
		out += '\n';
	} else if (v && (v->type == asvJSONValue::INT || v->type == asvJSONValue::DOUBLE || v->type == asvJSONValue::BOOL_VAL)) {
		// Scalar child values are emitted as text content (like #text)
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
		out += tag;
		out += '>';
		out += '\n';
	} else if (isSelfClosing) {
		out += "/>\n";
	} else {
		out += '>';
		out += "</";
		out += tag;
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

// AsvJSONValue method implementations — must be inside the namespace.
inline void asvJSONValue::toSVG(std::string& out) const {
	asvJSONInternal::asvJSONValue_toSVG(this, out);
}

inline void asvJSONValue::toSVG(std::string& out, const std::string& name, int indent) const {
	svgSerializeElement(out, name.empty() ? "element" : name, this, indent);
}

} // namespace asvJSONInternal

namespace asvJSONInternal {

// SVG is XML-based: fromSVG delegates to the existing XML parser after BOM strip.
// Point 9: remove redundant catch(asvJSONError) { throw; }
inline bool asvJSON::fromSVG(std::string_view input) {
	root = nullptr;
	dirty_ = false;
	formatType = FormatType::Unknown;
	try {
		std::string data(input);
		if (data.size() >= 3 &&
			static_cast<uint8_t>(data[0]) == 0xEF &&
			static_cast<uint8_t>(data[1]) == 0xBB &&
			static_cast<uint8_t>(data[2]) == 0xBF) {
			data.erase(0, 3); // strip UTF-8 BOM
		}
		bool ok = fromXML(std::string_view(data));
		if (ok) formatType = FormatType::SVG;
		return ok;
	} catch (const std::exception& e) {
		lastError = e.what();
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
