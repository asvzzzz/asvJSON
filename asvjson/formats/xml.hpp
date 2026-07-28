#pragma once
// XML serialization/parsing for asvJSON++
// Namespace prefixes (e.g. xml:lang, xsi:nil) are preserved as part of key strings:
//   - Attributes become "@prefix:name" keys (e.g. "@xml:lang")
//   - Element names preserve the prefix (e.g. "ns:element")
//   - xmlns declarations become "@xmlns" and "@xmlns:prefix" attributes

#include "../core.hpp"

namespace asvJSONInternal {

// Decode XML entities: &amp; &lt; &gt; &quot; &apos; &#NN; &#xNN;
static std::string xmlDecodeEntities(std::string_view s) {
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
				if (ent.size() > 1 && ent[1] == 'x') {
					char* ep = nullptr;
					long cp = std::strtol(ent.data() + 2, &ep, 16);
					if (ep > ent.data() + 2 && cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
						if (cp <= 0x7F) out += static_cast<char>(cp);
						else if (cp <= 0x7FF) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
						else if (cp <= 0xFFFF) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
						else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					}
				} else {
					char* ep = nullptr;
					long cp = std::strtol(ent.data() + 1, &ep, 10);
					if (ep > ent.data() + 1 && cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
						if (cp <= 0x7F) out += static_cast<char>(cp);
						else if (cp <= 0x7FF) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
						else if (cp <= 0xFFFF) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
						else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					}
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
			if (std::isnan(dbl) || std::isinf(dbl)) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			std::string val; fmtDoubleVal(dbl, val);
			out += pad + "<" + xmlSanitizeElementName(name) + ">" + xmlEscapeContent(val) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::STRING:
			out += pad + "<" + xmlSanitizeElementName(name) + ">" + xmlEscapeContent(str_data) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(timestamp, datetime_ms, dt);
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
			std::string r; fmtRegexVal(str_data, r);
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
			if (!obj) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			// Check for #content array (mixed content from fromXML)
			auto contentIt = obj->find("#content");
			if (contentIt != obj->end() && contentIt->second && contentIt->second->type == ARRAY) {
				out += pad + "<" + xmlSanitizeElementName(name);
				for (const auto& [k, v] : *obj) {
					if (k == "#content") continue;
					if (!k.empty() && k[0] == '@') out += " " + k.substr(1) + "=\"" + xmlEscapeContent(v->type == asvJSONValue::STRING ? v->str_data : "") + "\"";
				}
				out += ">";
				for (const auto& item : *(contentIt->second->arr)) {
					if (item->type == asvJSONValue::STRING) {
						out += xmlEscapeContent(item->str_data);
					} else if (item->type == asvJSONValue::OBJECT && item->obj && item->obj->size() == 1) {
						const auto& [childName, childVal] = *item->obj->begin();
						std::string safeChildName = xmlSanitizeElementName(childName);
						if (!childVal || childVal->type == asvJSONValue::NULL_VAL) {
							out += "<" + safeChildName + "/>";
						} else if (childVal->type == asvJSONValue::STRING && childVal->str_data.empty()) {
							out += "<" + safeChildName + "/>";
						} else if (childVal->type == asvJSONValue::STRING) {
							out += "<" + safeChildName + ">" + xmlEscapeContent(childVal->str_data) + "</" + safeChildName + ">";
						} else if (childVal->type == asvJSONValue::INT) {
							char buf[32];
							auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), childVal->num);
							if (ec == std::errc()) out += "<" + safeChildName + ">" + std::string(buf, ptr) + "</" + safeChildName + ">";
						} else if (childVal->type == asvJSONValue::DOUBLE) {
							std::string val; fmtDoubleVal(childVal->dbl, val);
							out += "<" + safeChildName + ">" + xmlEscapeContent(val) + "</" + safeChildName + ">";
						} else if (childVal->type == asvJSONValue::BOOL_VAL) {
							out += "<" + safeChildName + ">" + (childVal->flag ? "true" : "false") + "</" + safeChildName + ">";
						} else {
							size_t before = out.size();
							childVal->toXML(out, childName, 0);
							if (!out.empty() && out.back() == '\n') out.pop_back();
						}
					} else if (item->type == asvJSONValue::OBJECT) {
						size_t before = out.size();
						item->toXML(out, "item", 0);
						if (!out.empty() && out.back() == '\n') out.pop_back();
					} else if (item->type == asvJSONValue::ARRAY) {
						size_t before = out.size();
						item->toXML(out, "item", 0);
						if (!out.empty() && out.back() == '\n') out.pop_back();
					} else if (item->type == asvJSONValue::INT) {
						char buf[32];
						auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), item->num);
						if (ec == std::errc()) out += std::string(buf, ptr);
					} else if (item->type == asvJSONValue::DOUBLE) {
						std::string val; fmtDoubleVal(item->dbl, val); out += val;
					} else if (item->type == asvJSONValue::BOOL_VAL) {
						out += item->flag ? "true" : "false";
					} else if (item->type == asvJSONValue::NULL_VAL) {
						// null in mixed content -> skip
					}
				}
				out += "</" + xmlSanitizeElementName(name) + ">\n";
				break;
			}
			out += pad + "<" + xmlSanitizeElementName(name);
			for (const auto& [k, v] : *obj) {
				if (!k.empty() && k[0] == '@') out += " " + k.substr(1) + "=\"" + xmlEscapeContent(v->type == asvJSONValue::STRING ? v->str_data : "") + "\"";
			}
			out += ">\n";
			for (const auto& [k, v] : *obj) {
				if (!k.empty() && k[0] == '@') continue;
				v->toXML(out, k, indent + 1);
			}
			out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::ARRAY: {
			if (!arr) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			if (arr->empty()) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			bool allObjects = true;
			for (const auto& item : *arr) {
				if (item->type != asvJSONValue::OBJECT) { allObjects = false; break; }
			}
			if (allObjects) {
				for (const auto& item : *arr) {
					item->toXML(out, name, indent);
				}
			} else {
				out += pad + "<" + xmlSanitizeElementName(name) + ">\n";
				for (const auto& item : *arr) {
					out += pad + "  <item";
					if (item->type == asvJSONValue::NULL_VAL) {
						out += "/>\n";
					} else {
						out += ">";
						if (item->type == asvJSONValue::STRING) out += xmlEscapeContent(item->str_data);
						else if (item->type == asvJSONValue::INT) { char buf[32]; auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), item->num); if (ec == std::errc()) out += std::string(buf, ptr); }
						else if (item->type == asvJSONValue::DOUBLE) { std::string val; fmtDoubleVal(item->dbl, val); out += xmlEscapeContent(val); }
						else if (item->type == asvJSONValue::BOOL_VAL) out += item->flag ? "true" : "false";
						out += "</item>\n";
					}
				}
				out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			}
			break;
		}
		default:
			out += pad + "<" + xmlSanitizeElementName(name) + "/>\n";
			break;
	}
}

// ---------- XML Parser ----------

static void xmlSkipSpaces(std::string_view s, size_t& pos) {
	while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
		pos++;
}

static std::string xmlParseName(std::string_view s, size_t& pos) {
	std::string name;
	while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '-' || s[pos] == '_' || s[pos] == ':' || s[pos] == '.'))
		name += s[pos++];
	return name;
}

static std::string xmlParseAttrValue(std::string_view s, size_t& pos) {
	char quote = s[pos];
	pos++;
	std::string val;
	while (pos < s.size() && s[pos] != quote) {
		val += s[pos++];
	}
	if (pos < s.size()) pos++;
	return xmlDecodeEntities(val);
}

// Detect value type from XML text content
static std::unique_ptr<asvJSONValue> xmlDetectValue(std::string_view s) {
	if (s.empty()) return asvJSONValue::makeNull();
	if (s == "true" || s == "TRUE") return asvJSONValue::makeBool(true);
	if (s == "false" || s == "FALSE") return asvJSONValue::makeBool(false);
	if (s == "null" || s == "NULL") return asvJSONValue::makeNull();
	if (!s.empty() && (s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) {
		long long v;
		auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
		if (ec == std::errc() && ptr == s.data() + s.size()) return asvJSONValue::makeInt(v);
		double d;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
		auto [ptr2, ec2] = std::from_chars(s.data(), s.data() + s.size(), d);
		if (ec2 == std::errc() && ptr2 == s.data() + s.size()) return asvJSONValue::makeDouble(d);
#else
		char* end; errno = 0;
		d = std::strtod(s.data(), &end);
		if (errno != ERANGE && end == s.data() + s.size()) return asvJSONValue::makeDouble(d);
#endif
	}
	return asvJSONValue::makeString(s.data(), s.size());
}

// Forward declaration
static std::unique_ptr<asvJSONValue> xmlParseElement(std::string_view s, size_t& pos, std::string& elemName, int depth);

// Segment for interleaved mixed content tracking
struct XmlSegment {
	bool isElement;
	std::string text;      // for text segments
	std::string childName; // for element segments
	std::unique_ptr<asvJSONValue> childVal;
};

// Parse a single element's children into segments (preserving interleaved order)
static void xmlParseChildrenSegments(std::string_view s, size_t& pos,
	std::vector<XmlSegment>& segments, int depth)
{
	while (pos < s.size()) {
		xmlSkipSpaces(s, pos);
		if (pos >= s.size()) break;
		if (s[pos] == '<') {
			if (pos + 1 < s.size() && s[pos + 1] == '/') break;
			if (pos + 1 < s.size() && s[pos + 1] == '!') {
				if (pos + 3 < s.size() && s[pos + 2] == '-' && s[pos + 3] == '-') {
					size_t end = s.find("-->", pos + 4);
					if (end == std::string_view::npos) { pos = s.size(); break; }
					pos = end + 3; continue;
				}
				if (pos + 8 < s.size() && s.substr(pos + 2, 7) == "[CDATA[") {
					size_t end = s.find("]]>", pos + 9);
					if (end == std::string_view::npos) { pos = s.size(); break; }
					std::string cdata = std::string(s.substr(pos + 9, end - pos - 9));
					if (!cdata.empty()) {
						segments.push_back({false, std::move(cdata), "", nullptr});
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
					if (scan >= s.size()) { pos = s.size(); break; }
					pos = scan + 1; continue;
				}
				break;
			}
			if (pos + 1 < s.size() && s[pos + 1] == '?') {
				size_t end = s.find("?>", pos + 2);
				if (end == std::string_view::npos) { pos = s.size(); break; }
				pos = end + 2; continue;
			}
			std::string childName;
			auto childVal = xmlParseElement(s, pos, childName, depth + 1);
			if (childVal) segments.push_back({true, "", std::move(childName), std::move(childVal)});
		} else {
			size_t start = pos;
			while (pos < s.size() && s[pos] != '<') pos++;
			std::string text = xmlDecodeEntities(s.substr(start, pos - start));
			bool onlySpace = true;
			for (auto c : text) {
				if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { onlySpace = false; break; }
			}
			if (!onlySpace) segments.push_back({false, std::move(text), "", nullptr});
		}
	}
}

// Parse a complete XML element: <name attrs...>content</name> or <name attrs.../>
static std::unique_ptr<asvJSONValue> xmlParseElement(std::string_view s, size_t& pos, std::string& elemName, int depth) {
	if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) throw asvJSONError("XML nesting too deep");
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
				attrs[attrName] = xmlParseAttrValue(s, pos);
			}
		}
	}

	// xsi:nil="true" - treat element as JSON null
	{
		auto nilIt = attrs.find("xsi:nil");
		if (nilIt != attrs.end() && (nilIt->second == "true" || nilIt->second == "1")) {
			if (pos < s.size() && s[pos] == '/') { pos++; if (pos < s.size() && s[pos] == '>') pos++; }
			else if (pos < s.size() && s[pos] == '>') {
				pos++;
				// Skip to matching closing tag, tolerant of whitespace before >
				std::string closeTag = "</" + elemName;
				size_t closePos = s.find(closeTag, pos);
				if (closePos != std::string_view::npos) {
					size_t afterName = closePos + closeTag.size();
					while (afterName < s.size() && (s[afterName] == ' ' || s[afterName] == '\t' || s[afterName] == '\n' || s[afterName] == '\r'))
						afterName++;
					if (afterName < s.size() && s[afterName] == '>')
						pos = afterName + 1;
				}
			}
			return asvJSONValue::makeNull();
		}
	}

	// Self-closing tag
	if (pos < s.size() && s[pos] == '/') {
		pos++;
		if (pos < s.size() && s[pos] == '>') { pos++; }
		if (attrs.empty()) return asvJSONValue::makeString("", 0);
		auto obj = asvJSONValue::makeObject();
		for (const auto& [k, v] : attrs) obj->obj->emplace("@" + k, asvJSONValue::makeString(v.data(), v.size()));
		return obj;
	}

	// Opening tag
	if (pos < s.size() && s[pos] == '>') pos++;
	else throw asvJSONError("expected '>'");

	// Parse children and text content
	std::vector<XmlSegment> segments;
	xmlParseChildrenSegments(s, pos, segments, depth + 1);

	// Closing tag
	xmlSkipSpaces(s, pos);
	if (pos + 1 < s.size() && s[pos] == '<' && s[pos + 1] == '/') {
		pos += 2;
		std::string closeName = xmlParseName(s, pos);
		if (closeName != elemName) throw asvJSONError("mismatched closing tag: " + closeName + " != " + elemName);
		xmlSkipSpaces(s, pos);
		if (pos < s.size() && s[pos] == '>') pos++;
	}

	// Analyze segments: count text vs element segments
	size_t textCount = 0, elemCount = 0;
	std::string combinedText;
	bool hasInterleaving = false;
	for (size_t i = 0; i < segments.size(); i++) {
		if (segments[i].isElement) {
			elemCount++;
		} else {
			textCount++;
			combinedText += segments[i].text;
			// Check if elements appear both before and after this text
		}
	}
	// Interleaving = text appears both before and after child elements
	// Simple case: text-before-elements then elements is NOT interleaved
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
		// No content - just attributes or plain
		if (attrs.empty()) return xmlDetectValue(combinedText);
		auto obj = asvJSONValue::makeObject();
		for (const auto& [k, v] : attrs) obj->obj->emplace("@" + k, asvJSONValue::makeString(v.data(), v.size()));
		if (obj->obj->empty()) return asvJSONValue::makeString("", 0);
		return obj;
	}

	// No elements, only text - simple case
	if (elemCount == 0) {
		if (attrs.empty()) return xmlDetectValue(combinedText);
		// Check for special type attribute (from toXML)
		auto tit = attrs.find("type");
		if (tit != attrs.end() && !combinedText.empty()) {
			const std::string& t = tit->second;
			if (t == "datetime") {
				time_t ts = 0; int ms = 0;
				if (tryParseDateTime(combinedText, ts, &ms)) return asvJSONValue::makeDateTime(ts, ms);
			}
			if (t == "binary") {
				auto decoded = decodeBase64Fast(combinedText.data(), combinedText.size());
				if (!decoded.empty()) return asvJSONValue::makeBinary(decoded.data(), decoded.size());
			}
			if (t == "objectid") {
				if (combinedText.size() == 24) {
					std::string oid; oid.reserve(12);
					for (size_t i = 0; i + 1 < combinedText.size(); i += 2) {
						char buf[3] = {combinedText[i], combinedText[i+1], 0};
						oid.push_back(static_cast<char>(std::strtol(buf, nullptr, 16)));
					}
					if (oid.size() == 12) return asvJSONValue::makeObjectId(std::string_view(oid.data(), 12));
				}
			}
			if (t == "regex") {
				size_t sep = combinedText.rfind('|');
				if (sep != std::string::npos) {
					std::string pat = combinedText.substr(0, sep);
					std::string opt = combinedText.substr(sep + 1);
					return asvJSONValue::makeRegex(pat.c_str(), opt.c_str());
				}
				return asvJSONValue::makeRegex(combinedText.c_str(), "");
			}
			if (t == "timestamp") {
				long long v;
				auto [ptr, ec] = std::from_chars(combinedText.data(), combinedText.data() + combinedText.size(), v);
				if (ec == std::errc()) return asvJSONValue::makeTimestamp(v);
			}
			if (t == "extension") {
				auto decoded = decodeBase64Fast(combinedText.data(), combinedText.size());
				if (!decoded.empty()) {
					auto eit = attrs.find("exttype");
					int8_t et = 0;
					if (eit != attrs.end()) {
						long ev = std::strtol(eit->second.c_str(), nullptr, 10);
						if (ev >= -128 && ev <= 127) et = static_cast<int8_t>(ev);
					}
					return asvJSONValue::makeExtension(et, decoded.data(), decoded.size());
				}
			}
		}
		// Has attributes - object with #text + @attr
		auto obj = asvJSONValue::makeObject();
		for (const auto& [k, v] : attrs) obj->obj->emplace("@" + k, asvJSONValue::makeString(v.data(), v.size()));
		if (!combinedText.empty()) obj->obj->emplace("#text", xmlDetectValue(combinedText));
		if (obj->obj->empty()) return asvJSONValue::makeString("", 0);
		return obj;
	}

	// Has child elements (possibly interleaved with text)
	auto obj = asvJSONValue::makeObject();

	// Add attributes
	for (const auto& [k, v] : attrs) obj->obj->emplace("@" + k, asvJSONValue::makeString(v.data(), v.size()));

	if (hasInterleaving) {
		// Mixed content: preserve interleaved order as #content array
		auto arr = asvJSONValue::makeArray();
		for (auto& seg : segments) {
			if (seg.isElement) {
				auto wrapper = asvJSONValue::makeObject();
				wrapper->obj->emplace(seg.childName, std::move(seg.childVal));
				arr->arr->push_back(std::move(wrapper));
			} else {
				arr->arr->push_back(xmlDetectValue(seg.text));
			}
		}
		obj->obj->emplace("#content", std::move(arr));
	} else {
		// Non-interleaved: all text first (if any), then all elements
		// Add text content as #text
		if (!combinedText.empty()) obj->obj->emplace("#text", xmlDetectValue(combinedText));

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
					obj->obj->emplace(seg.childName, std::move(arr));
					it = obj->obj->find(seg.childName);
				}
				it->second->arr->push_back(std::move(seg.childVal));
			} else {
				obj->obj->emplace(seg.childName, std::move(seg.childVal));
			}
		}
	}

	// Collapse: if element has only children (no attrs, no text, no #content),
	// and all children are same-named, return the array directly.
	if (attrs.empty() && combinedText.empty() && !hasInterleaving) {
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

inline bool asvJSON::fromXML(std::string_view input) {
	try {
		if (input.empty()) throw asvJSONError("empty input");

		size_t pos = 0;

		// Skip XML declaration <?xml ...?>
		xmlSkipSpaces(input, pos);
		if (pos + 5 < input.size() && input.substr(pos, 5) == "<?xml") {
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

		// Parse all top-level elements, combining into one object
		auto obj = asvJSONValue::makeObject();
		int count = 0;
		while (pos < input.size()) {
			if (!skipProlog()) break;
			std::string name;
			auto val = xmlParseElement(input, pos, name, 0);
			if (!val) break;
			obj->obj->emplace(std::move(name), std::move(val));
			count++;
		}
		if (count == 0) throw asvJSONError("failed to parse root element");

		root = std::move(obj);
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	}
}

} // namespace asvJSONInternal
