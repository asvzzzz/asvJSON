#pragma once
// XML serialization/parsing for asvJSON++

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

} // namespace asvJSONInternal
using namespace asvJSONInternal;

inline void asvJSONValue::toXML(std::string& out) const {
	toXML(out, "root", 0);
}

inline void asvJSONValue::toXML(std::string& out, const std::string& name, int indent) const {
	using T = asvJSONValue::Type;
	std::string pad(indent * 2, ' ');

	switch (type) {
		case T::NULL_VAL:
			out += pad + "<" + xmlSanitizeElementName(name) + "/>\n";
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
			out += pad + "<" + xmlSanitizeElementName(name) + ">\n";
			for (const auto& [k, v] : *obj) v->toXML(out, k, indent + 1);
			out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::ARRAY: {
			if (!arr) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			out += pad + "<" + xmlSanitizeElementName(name) + ">\n";
			for (const auto& item : *arr) item->toXML(out, "item", indent + 1);
			out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		default:
			out += pad + "<" + xmlSanitizeElementName(name) + "/>\n";
			break;
	}
}

// ---------- XML Parser ----------

namespace asvJSONInternal {

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

// Parse a single element's children into (name, value) pairs, plus text content
static void xmlParseChildren(std::string_view s, size_t& pos,
	std::vector<std::pair<std::string, std::unique_ptr<asvJSONValue>>>& children,
	std::string& textContent, int depth)
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
					textContent += s.substr(pos + 9, end - pos - 9);
					pos = end + 3; continue;
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
			if (childVal) children.emplace_back(std::move(childName), std::move(childVal));
		} else {
			size_t start = pos;
			while (pos < s.size() && s[pos] != '<') pos++;
			std::string text = xmlDecodeEntities(s.substr(start, pos - start));
			bool onlySpace = true;
			for (auto c : text) {
				if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { onlySpace = false; break; }
			}
			if (!onlySpace) textContent += text;
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

	// Self-closing tag
	if (pos < s.size() && s[pos] == '/') {
		pos++;
		if (pos < s.size() && s[pos] == '>') { pos++; }
		if (attrs.empty()) return asvJSONValue::makeNull();
		auto obj = asvJSONValue::makeObject();
		for (const auto& [k, v] : attrs) obj->obj->emplace("@" + k, asvJSONValue::makeString(v.data(), v.size()));
		return obj;
	}

	// Opening tag
	if (pos < s.size() && s[pos] == '>') pos++;
	else throw asvJSONError("expected '>'");

	// Parse children and text content
	std::vector<std::pair<std::string, std::unique_ptr<asvJSONValue>>> children;
	std::string textContent;
	xmlParseChildren(s, pos, children, textContent, depth + 1);

	// Closing tag
	xmlSkipSpaces(s, pos);
	if (pos + 1 < s.size() && s[pos] == '<' && s[pos + 1] == '/') {
		pos += 2;
		std::string closeName = xmlParseName(s, pos);
		if (closeName != elemName) throw asvJSONError("mismatched closing tag: " + closeName + " != " + elemName);
		xmlSkipSpaces(s, pos);
		if (pos < s.size() && s[pos] == '>') pos++;
	}

	// Build result
	if (children.empty()) {
		// No child elements - just text content
		if (attrs.empty()) return xmlDetectValue(textContent);
		// Check for special type attribute (from toXML)
		if (!textContent.empty()) {
			auto tit = attrs.find("type");
			if (tit != attrs.end()) {
				const std::string& t = tit->second;
				if (t == "datetime") {
					time_t ts = 0; int ms = 0;
					if (tryParseDateTime(textContent, ts, &ms)) return asvJSONValue::makeDateTime(ts, ms);
				}
				if (t == "binary") {
					auto decoded = decodeBase64Fast(textContent.data(), textContent.size());
					if (!decoded.empty()) return asvJSONValue::makeBinary(decoded.data(), decoded.size());
				}
				if (t == "objectid") {
					if (textContent.size() == 24) {
						std::string oid; oid.reserve(12);
						for (size_t i = 0; i + 1 < textContent.size(); i += 2) {
							char buf[3] = {textContent[i], textContent[i+1], 0};
							oid.push_back(static_cast<char>(std::strtol(buf, nullptr, 16)));
						}
						if (oid.size() == 12) return asvJSONValue::makeObjectId(std::string_view(oid.data(), 12));
					}
				}
				if (t == "regex") {
					size_t sep = textContent.find('|');
					if (sep != std::string::npos) {
						std::string pat = textContent.substr(0, sep);
						std::string opt = textContent.substr(sep + 1);
						return asvJSONValue::makeRegex(pat.c_str(), opt.c_str());
					}
					return asvJSONValue::makeRegex(textContent.c_str(), "");
				}
				if (t == "timestamp") {
					long long v;
					auto [ptr, ec] = std::from_chars(textContent.data(), textContent.data() + textContent.size(), v);
					if (ec == std::errc()) return asvJSONValue::makeTimestamp(v);
				}
				if (t == "extension") {
					auto decoded = decodeBase64Fast(textContent.data(), textContent.size());
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
		}
		// Has attributes - object with #text + @attr
		auto obj = asvJSONValue::makeObject();
		for (const auto& [k, v] : attrs) obj->obj->emplace("@" + k, asvJSONValue::makeString(v.data(), v.size()));
		if (!textContent.empty()) obj->obj->emplace("#text", xmlDetectValue(textContent));
		if (obj->obj->empty()) return asvJSONValue::makeNull();
		return obj;
	}

	// Has child elements - build object
	auto obj = asvJSONValue::makeObject();

	// Add attributes
	for (const auto& [k, v] : attrs) obj->obj->emplace("@" + k, asvJSONValue::makeString(v.data(), v.size()));

	// Add text content as #text
	if (!textContent.empty()) obj->obj->emplace("#text", xmlDetectValue(textContent));

	// Count occurrences of each child name (global, not just consecutive)
	std::unordered_map<std::string, size_t> nameCounts;
	for (const auto& child : children) nameCounts[child.first]++;

	// Add children, grouping into arrays for names with count > 1
	for (auto& child : children) {
		if (nameCounts[child.first] > 1) {
			auto it = obj->obj->find(child.first);
			if (it == obj->obj->end()) {
				auto arr = asvJSONValue::makeArray();
				obj->obj->emplace(child.first, std::move(arr));
				it = obj->obj->find(child.first);
			}
			it->second->arr->push_back(std::move(child.second));
		} else {
			obj->obj->emplace(child.first, std::move(child.second));
		}
	}

	return obj;
}

} // namespace asvJSONInternal

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

		// Skip comments and whitespace before root element
		while (pos < input.size()) {
			xmlSkipSpaces(input, pos);
			if (pos + 4 < input.size() && input.substr(pos, 4) == "<!--") {
				size_t end = input.find("-->", pos + 4);
				if (end == std::string_view::npos) throw asvJSONError("unclosed comment");
				pos = end + 3;
				continue;
			}
			break;
		}

		if (pos >= input.size() || input[pos] != '<') throw asvJSONError("expected '<'");

		std::string rootName;
		auto val = xmlParseElement(input, pos, rootName, 0);
		if (!val) throw asvJSONError("failed to parse root element");

		auto obj = asvJSONValue::makeObject();
		obj->obj->emplace(std::move(rootName), std::move(val));
		root = std::move(obj);
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	}
}
