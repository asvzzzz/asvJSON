#pragma once
// YAML serialization/parsing for asvJSON++

#include "../core.hpp"

namespace asvJSONInternal {

inline constexpr size_t MAX_YAML_ANCHORS = 100000;

inline void asvJSONValue::toYAML(std::string& out) const {
	toYAML(out, 0, "", false);
}

inline void asvJSONValue::toYAML(std::string& out, int indent, const std::string& key, bool isArrayItem) const {
	using T = asvJSONValue::Type;
	auto prefix = [&]() -> std::string {
		if (isArrayItem && key.empty()) return std::string(indent * 2, ' ') + "- ";
		if (!key.empty()) return std::string(indent * 2, ' ') + yamlQuoteKey(key) + ": ";
		return "";
	};

	switch (type) {
		case T::NULL_VAL:
			out += prefix() + "~\n";
			break;
		case T::BOOL_VAL:
			out += prefix() + (flag ? "true" : "false") + "\n";
			break;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += prefix() + std::string(buf, ptr) + "\n"; }
			break;
		}
		case T::DOUBLE: {
			if (std::isnan(dbl)) { out += prefix() + ".nan\n"; break; }
			if (std::isinf(dbl)) { out += prefix() + (dbl > 0 ? ".inf\n" : "-.inf\n"); break; }
			std::string val; fmtDoubleVal(dbl, val);
			out += prefix() + val + "\n";
			break;
		}
		case T::STRING: {
			// Multi-line strings are emitted as literal block scalars with an
			// explicit chomp indicator so round-trips preserve trailing line breaks.
			if (str_data.find('\n') != std::string_view::npos && !str_data.empty() && str_data[0] != '\n') {
				size_t trailing = 0;
				while (trailing < str_data.size() && str_data[str_data.size() - 1 - trailing] == '\n') trailing++;
				std::string ind;
				if (trailing == 0) ind = "|-";
				else if (trailing == 1) ind = "|";
				else ind = "|+";
				out += prefix() + ind + "\n";
				for (size_t i = 0; i < str_data.size(); ) {
					size_t eol = str_data.find('\n', i);
					if (eol == std::string_view::npos) eol = str_data.size();
					out += std::string((indent + 1) * 2, ' ') + std::string(str_data.data() + i, eol - i) + "\n";
					i = eol + 1;
				}
			} else if (str_data.find('\n') != std::string_view::npos) {
				// Leading (or only) blank lines cannot be represented by a literal
				// block -- fall back to a double-quoted string with escapes.
				out += prefix() + yamlDQuote(str_data) + "\n";
			} else if (yamlNeedsQuotes(str_data)) {
				out += prefix() + yamlQuote(str_data) + "\n";
			} else {
				out += prefix() + str_data + "\n";
			}
			break;
		}
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(timestamp, datetime_ms, dt);
			out += prefix() + dt + "\n";
			break;
		}
		case T::BINARY:
			out += prefix() + "!!binary " + encodeBase64(bin_data.data(), bin_data.size()) + "\n";
			break;
		case T::OBJECTID: {
			std::string hex; fmtObjectIdHexVal(str_data, hex);
			out += prefix() + "!objectid " + hex + "\n";
			break;
		}
		case T::REGEX: {
			std::string r; fmtRegexVal(str_data, r);
			out += prefix() + "!regex " + r + "\n";
			break;
		}
		case T::TIMESTAMP: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += prefix() + std::string(buf, ptr) + "\n"; }
			break;
		}
		case T::EXTENSION:
			out += prefix() + "!ext " + std::to_string(ext_type) + " " + yamlQuote(encodeBase64(bin_data.data(), bin_data.size())) + "\n";
			break;
		case T::OBJECT: {
			if (!obj || obj->empty()) { out += prefix() + "{}\n"; break; }
			if (key.empty() && !isArrayItem) {
				for (const auto& [k, v] : *obj) v->toYAML(out, indent, k, false);
			} else {
				out += prefix() + "\n";
				for (const auto& [k, v] : *obj) v->toYAML(out, indent + 1, k, false);
			}
			break;
		}
		case T::ARRAY: {
			if (!arr || arr->empty()) { out += prefix() + "[]\n"; break; }
			if (!isArrayItem && key.empty()) {
				out += "---\n";
				for (const auto& item : *arr) item->toYAML(out, 0, "", true);
				break;
			}
			out += prefix() + "\n";
			for (const auto& item : *arr) item->toYAML(out, indent, "", true);
			break;
		}
		default:
			out += prefix() + "~\n";
			break;
	}
}

// ==================== YAML Decoder (YAML -> JSON -> parse) ====================


// Unescape YAML double-quoted string (\n, \t, \\, \", \xNN, \uNNNN)
inline std::string yamlUnescapeDouble(std::string_view s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			switch (s[++i]) {
				case 'n': out += '\n'; break;
				case 't': out += '\t'; break;
				case 'r': out += '\r'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case '\\': out += '\\'; break;
				case '"': out += '"'; break;
				case '/': out += '/'; break;
				case '0': out += '\0'; break;
				case 'x': {
					if (i + 2 > s.size()) break;
					auto hex = [&](size_t off) -> int {
						char c = s[i + off];
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						return 0;
					};
					unsigned char val = static_cast<unsigned char>((hex(1) << 4) | hex(2));
					out += static_cast<char>(val);
					i += 2;
					break;
				}
				case 'u': {
					if (i + 4 > s.size()) break;
					auto hex = [&](size_t off) -> int {
						char c = s[i + off];
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						return -1;
					};
					unsigned int cp = 0;
					for (int j = 0; j < 4; j++) { int h = hex(1 + j); if (h < 0) break; cp = (cp << 4) | static_cast<unsigned int>(h); }
					if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 5] == '\\' && s[i + 6] == 'u') {
						unsigned int lo = 0;
						for (int j = 0; j < 4; j++) { int h = hex(6 + j); if (h < 0) break; lo = (lo << 4) | static_cast<unsigned int>(h); }
						if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
						else { i += 4; }
					} else { i += 4; }
					if (cp < 0x80) { out += static_cast<char>(cp); }
					else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					break;
				}
				default: out += s[i]; break;
			}
		} else {
			out += s[i];
		}
	}
	return out;
}

// Unescape YAML single-quoted string ('' -> ')
inline std::string yamlUnescapeSingle(std::string_view s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') {
			out += '\''; i++;
		} else {
			out += s[i];
		}
	}
	return out;
}

// Remove trailing comment from a line (respecting quotes)
inline std::string_view yamlStripComment(std::string_view s) {
	bool inSQ = false, inDQ = false;
	for (size_t i = 0; i < s.size(); i++) {
		if (!inDQ && !inSQ && s[i] == '#') return s.substr(0, i);
		if (s[i] == '"' && !inSQ) inDQ = !inDQ;
		if (s[i] == '\'' && !inDQ) inSQ = !inSQ;
	}
	return s;
}

// Convert YAML scalar to JSON (auto-detect type)
inline std::string yamlScalarToJson(std::string_view s) {
	if (s.empty() || s == "~" || s == "null" || s == "NULL" || s == "Null") return "null";
	if (s == "true" || s == "TRUE" || s == "True" || s == "yes" || s == "YES" || s == "Yes" || s == "on" || s == "ON" || s == "On")
		return "true";
	if (s == "false" || s == "FALSE" || s == "False" || s == "no" || s == "NO" || s == "No" || s == "off" || s == "OFF" || s == "Off")
		return "false";
	if (s == "NaN" || s == ".NaN" || s == ".nan" || s == ".NAN") return "NaN";
	if (s == "Infinity" || s == ".Inf" || s == ".inf" || s == ".INF") return "Infinity";
	if (s == "-Infinity" || s == "-.Inf" || s == "-.inf" || s == "-.INF") return "-Infinity";
	char* end = nullptr;
	std::strtod(s.data(), &end);
	if (end == s.data() + s.size() && s.size() > 0) return std::string(s);
	std::string out = "\"";
	for (auto c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
			         else out += c;
		}
	}
	out += "\"";
	return out;
}

inline std::string yamlParseInlineValue(std::string_view s);

// Convert unescaped regex "pattern|opts" to internal pattern|opts
inline std::string yamlRegexToInternal(std::string_view s) {
	return std::string(s);
}

// Force a YAML value to be treated as a JSON string (for !!str tag)
inline std::string yamlForceStringVal(std::string_view val) {
	while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.remove_prefix(1);
	if (val.empty()) return "\"\"";
	// If already a properly quoted JSON string from yamlParseInlineValue, return as-is
	if (val.size() >= 2 && val[0] == '"' && val.back() == '"') return std::string(val);
	// Parse YAML quotes if present
	if (val.size() >= 2) {
		if (val[0] == '"') {
			size_t end = 1;
			while (end < val.size()) {
				if (val[end] == '\\') end += 2;
				else if (val[end] == '"') break;
				else end++;
			}
			if (end < val.size()) {
				std::string unesc = yamlUnescapeDouble(val.substr(1, end - 1));
				std::string out = "\"";
				for (auto c : unesc) {
					switch (c) {
						case '"': out += "\\\""; break;
						case '\\': out += "\\\\"; break;
						case '\n': out += "\\n"; break;
						case '\t': out += "\\t"; break;
						case '\r': out += "\\r"; break;
						default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
						         else out += c;
					}
				}
				out += "\"";
				return out;
			}
		} else if (val[0] == '\'') {
			size_t end = 1;
			for (; end < val.size(); end++) {
				if (val[end] == '\'' && end + 1 < val.size() && val[end + 1] == '\'') end++;
				else if (val[end] == '\'') break;
			}
			if (end < val.size()) {
				std::string unesc = yamlUnescapeSingle(val.substr(1, end - 1));
				std::string out = "\"";
				for (auto c : unesc) {
					if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c;
				}
				out += "\"";
				return out;
			}
		}
	}
	// Plain text: wrap in JSON string with escaping
	std::string out = "\"";
	for (auto c : val) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
			         else out += c;
		}
	}
	out += "\"";
	return out;
}

// Convert JSON array string ["a","b","c"] -> JSON object {"a":null,"b":null,"c":null} for !!set
inline std::string yamlArrayToSetObject(std::string_view arr) {
	if (arr.size() < 2 || arr[0] != '[') return std::string(arr);
	std::string out = "{";
	size_t i = 1;
	bool first = true;
	while (i < arr.size() && arr[i] != ']') {
		while (i < arr.size() && (arr[i] == ',' || arr[i] == ' ' || arr[i] == '\n' || arr[i] == '\t')) i++;
		if (i >= arr.size() || arr[i] == ']') break;
		if (!first) out += ",";
		first = false;
		size_t start = i;
		if (arr[i] == '"') {
			i++;
			while (i < arr.size() && !(arr[i] == '"' && arr[i - 1] != '\\')) { if (arr[i] == '\\') i++; i++; }
			if (i < arr.size()) i++;
		} else if (arr[i] == '{') {
			int d = 1; i++;
			while (i < arr.size() && d > 0) {
				if (arr[i] == '{') d++; else if (arr[i] == '}') d--;
				else if (arr[i] == '"') { i++; while (i < arr.size() && !(arr[i] == '"' && arr[i - 1] != '\\')) { if (arr[i] == '\\') i++; i++; } }
				i++;
			}
		} else if (arr[i] == '[') {
			int d = 1; i++;
			while (i < arr.size() && d > 0) {
				if (arr[i] == '[') d++; else if (arr[i] == ']') d--;
				else if (arr[i] == '"') { i++; while (i < arr.size() && !(arr[i] == '"' && arr[i - 1] != '\\')) { if (arr[i] == '\\') i++; i++; } }
				i++;
			}
		} else {
			while (i < arr.size() && arr[i] != ',' && arr[i] != ']' && arr[i] != ' ' && arr[i] != '\n' && arr[i] != '\t') i++;
		}
		if (start < i && arr[start] == '"') out.append(arr.data() + start, i - start);
		else { out += '"'; out.append(arr.data() + start, i - start); out += '"'; }
		out += ":null";
	}
	out += "}";
	return out;
}

// Tag directives (%TAG handle prefix) for the current document being parsed
static thread_local std::unordered_map<std::string, std::string> yamlDocTagMap;

// Resolve a tag shorthand using %TAG directives (longest handle match wins)
inline std::string yamlResolveTag(std::string_view rawTag) {
	std::string s(rawTag);
	std::string bestHandle;
	std::string bestPrefix;
	for (const auto& [handle, prefix] : yamlDocTagMap) {
		if (s.size() >= handle.size() && s.substr(0, handle.size()) == handle) {
			if (handle.size() > bestHandle.size()) { bestHandle = handle; bestPrefix = prefix; }
		}
	}
	if (!bestHandle.empty()) return bestPrefix + s.substr(bestHandle.size());
	return s;
}

// Forward declarations for flow parser (used by tagged value handler)
inline std::string yamlParseFlowValue(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving);

// Parse tagged YAML value -- handles all !!word / !word tags
inline std::string yamlParseTaggedValue(std::string_view s) {
	// Extract tag name
	size_t tagEnd = 0;
	if (s.size() >= 2 && s[0] == '!') {
		if (s[1] == '!') {
			tagEnd = 2;
			while (tagEnd < s.size() && s[tagEnd] != ' ') tagEnd++;
		} else if (s[1] == '<') {
			tagEnd = s.find('>', 2);
			if (tagEnd != std::string_view::npos) tagEnd++; else tagEnd = s.size();
		} else {
			tagEnd = 1;
			while (tagEnd < s.size() && s[tagEnd] != ' ') tagEnd++;
		}
	}
	if (tagEnd == 0) return yamlParseInlineValue(s);

	std::string tagStr = yamlResolveTag(s.substr(0, tagEnd));
	std::string_view tag = tagStr;
	std::string_view val;
	while (tagEnd < s.size() && s[tagEnd] == ' ') tagEnd++;
	if (tagEnd < s.size()) val = s.substr(tagEnd);

	// Standard YAML tags
	if (tag == "!!str") return yamlForceStringVal(val);
	if (tag == "!!int") {
		if (val.empty()) return "0";
		// Handle YAML Core Schema integer prefixes: 0x (hex), 0o (octal), 0b (binary)
		if (val.size() >= 3 && val[0] == '0') {
			if (val[1] == 'x' || val[1] == 'X') {
				char* e = nullptr;
				errno = 0;
				long long n = std::strtoll(val.data() + 2, &e, 16);
				if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
				if (e == val.data() + val.size()) return std::to_string(n);
			} else if (val[1] == 'o' || val[1] == 'O') {
				char* e = nullptr;
				errno = 0;
				long long n = std::strtoll(val.data() + 2, &e, 8);
				if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
				if (e == val.data() + val.size()) return std::to_string(n);
			} else if ((val[0] == '0' && (val[1] == 'b' || val[1] == 'B')) ||
			           ((val[0] == '-' || val[0] == '+') && val.size() >= 4 && val[1] == '0' && (val[2] == 'b' || val[2] == 'B'))) {
				size_t start = (val[0] == '0') ? 2 : 3;
				int sign = 1;
				if (val[0] == '-') sign = -1;
				long long n = 0;
				bool valid = true;
				for (size_t bi = start; bi < val.size(); bi++) {
					if (val[bi] == '0') n *= 2;
					else if (val[bi] == '1') n = n * 2 + 1;
					else { valid = false; break; }
				}
				if (valid && (n > 0 || val.find_first_not_of("01", start) == std::string::npos))
					return std::to_string(sign * n);
			}
		}
		// Plain decimal integer (including negative)
		{
			std::string valStr(val);
			char* e = nullptr;
			errno = 0;
			long long n = std::strtoll(valStr.c_str(), &e, 10);
			if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
			if (e == valStr.c_str() + valStr.size()) return std::to_string(n);
		}
		std::string parsed = yamlParseInlineValue(val);
		if (parsed.size() >= 2 && parsed[0] == '"' && parsed.back() == '"') {
			std::string inner = unescapeJsonString(parsed.substr(1, parsed.size() - 2), true);
			char* e = nullptr;
			errno = 0;
			long long n = std::strtoll(inner.c_str(), &e, 0);
			if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
			if (e == inner.c_str() + inner.size()) return std::to_string(n);
			throw asvJSONError("YAML: invalid !!int value");
		}
		// parsed must be a valid JSON number, not bool/null/string
		if (!parsed.empty() && (parsed[0] == '-' || (parsed[0] >= '0' && parsed[0] <= '9'))) {
			char* e = nullptr;
			errno = 0;
			long long n = std::strtoll(parsed.c_str(), &e, 10);
			if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
			if (e == parsed.c_str() + parsed.size()) return std::to_string(n);
		}
		throw asvJSONError("YAML: invalid !!int value");
	}
	if (tag == "!!float") {
		if (val.empty()) return "0.0";
		// YAML Core Schema special float values
		if (val == ".inf" || val == ".Inf" || val == ".INF" || val == "Infinity") return "Infinity";
		if (val == "-.inf" || val == "-.Inf" || val == "-.INF" || val == "-Infinity") return "-Infinity";
		if (val == ".nan" || val == ".NaN" || val == ".NAN" || val == "NaN") return "NaN";
		// Try direct float parsing on raw val
		{
			std::string valStr(val);
			char* e = nullptr;
			errno = 0;
			double d = std::strtod(valStr.c_str(), &e);
			if (errno == ERANGE) throw asvJSONError("YAML: !!float overflow");
			if (e == valStr.c_str() + valStr.size() && valStr.size() > 0) {
				char buf[64];
				auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
				if (ec == std::errc()) return std::string(buf, ptr);
			}
		}
		std::string parsed = yamlParseInlineValue(val);
		if (parsed.size() >= 2 && parsed[0] == '"' && parsed.back() == '"') {
			std::string inner = unescapeJsonString(parsed.substr(1, parsed.size() - 2), true);
			char* e = nullptr;
			errno = 0;
			double d = std::strtod(inner.c_str(), &e);
			if (errno == ERANGE) throw asvJSONError("YAML: !!float overflow");
			if (e == inner.c_str() + inner.size()) {
				char buf[64];
				auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
				if (ec == std::errc()) return std::string(buf, ptr);
			}
			throw asvJSONError("YAML: invalid !!float value");
		}
		// parsed must be a valid JSON number
		if (!parsed.empty() && (parsed[0] == '-' || (parsed[0] >= '0' && parsed[0] <= '9'))) {
			char* e = nullptr;
			errno = 0;
			double d = std::strtod(parsed.c_str(), &e);
			if (errno == ERANGE) throw asvJSONError("YAML: !!float overflow");
			if (e == parsed.c_str() + parsed.size()) {
				char buf[64];
				auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
				if (ec == std::errc()) return std::string(buf, ptr);
			}
		}
		throw asvJSONError("YAML: invalid !!float value");
	}
	if (tag == "!!bool") {
		if (val.empty()) return "false";
		std::string lower; lower.reserve(val.size());
		for (auto c : val) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (lower == "true" || lower == "yes" || lower == "on") return "true";
		return "false";
	}
	if (tag == "!!null") return "null";
	if (tag == "!!timestamp") {
		if (!val.empty()) return yamlParseInlineValue(val);
		return "\"\"";
	}

	// Special type tags (Extended JSON)
	if (tag == "!!binary") {
		return "{\"$binary\":{\"base64\":\"" + std::string(val) + "\",\"subType\":\"00\"}}";
	}
	if (tag == "!objectid") {
		return "{\"$oid\":\"" + std::string(val) + "\"}";
	}
	if (tag == "!regex") {
		std::string resolved = yamlParseInlineValue(val);
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		std::string internal = yamlRegexToInternal(raw);
		size_t sep = internal.rfind('|');
		std::string pattern = (sep != std::string::npos) ? internal.substr(0, sep) : internal;
		std::string opts = (sep != std::string::npos) ? internal.substr(sep + 1) : "";
		std::string out = "{\"$regex\":\"";
		for (auto c : pattern) {
			if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c;
		}
		out += "\"";
		if (!opts.empty()) {
			out += ",\"$options\":\"";
			for (auto c : opts) {
				if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c;
			}
			out += "\"";
		}
		out += "}";
		return out;
	}
	if (tag == "!ext") {
		std::string remaining = std::string(val);
		size_t sp = remaining.find(' ');
		if (sp == std::string::npos) return yamlParseInlineValue(val);
		int extType = 0;
		std::from_chars(remaining.data(), remaining.data() + sp, extType);
		std::string resolved = yamlParseInlineValue(std::string_view(remaining).substr(sp + 1));
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		char hex[3];
		snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(extType));
		return "{\"$binary\":{\"base64\":\"" + raw + "\",\"subType\":\"" + hex + "\"}}";
	}

	// !!set -- convert to object-with-null-values for flow notation: [a,b,c] -> {"a":null,"b":null}
	if (tag == "!!set") {
		if (val.empty()) return "{}";
		if (val[0] == '[') {
			std::string tmp(val);
			size_t fp = 0;
			std::string flowJson = yamlParseFlowValue(tmp, fp, nullptr, nullptr);
			return yamlArrayToSetObject(flowJson);
		}
		if (val[0] == '{') {
			std::string tmp(val);
			size_t fp = 0;
			return yamlParseFlowValue(tmp, fp, nullptr, nullptr);
		}
		return std::string(val);
	}
	// !!omap -- ensure array of single-key objects; for flow [{a:1},{b:2}] already correct
	// !!pairs -- ensure array of pairs; for flow [[a,1],[b,2]] already correct
	if (tag == "!!omap" || tag == "!!pairs") {
		if (val.empty()) return "[]";
		if (val[0] == '[' || val[0] == '{') {
			std::string tmp(val);
			size_t fp = 0;
			return yamlParseFlowValue(tmp, fp, nullptr, nullptr);
		}
		return "[]";
	}
	// !!map, !!seq -- pass through value (handled by main loop for block, here for flow)
	if (tag == "!!map" || tag == "!!seq") {
		if (!val.empty()) return std::string(val);
		return (tag == "!!seq") ? "[]" : "{}";
	}

	// Unknown tag -- parse value normally
	if (!val.empty()) return yamlParseInlineValue(val);
	return "null";
}

inline bool yamlIsAnchorChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

// Check if position p is inside flow brackets ({...} or [...]) in string s
inline bool yamlInFlowBrackets(std::string_view s, size_t p) {
	int depth = 0;
	bool inSQuote = false, inDQuote = false;
	for (size_t i = 0; i < p && i < s.size(); i++) {
		if (inSQuote) {
			if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') i++;
			else if (s[i] == '\'') inSQuote = false;
		} else if (inDQuote) {
			if (s[i] == '\\') i++;
			else if (s[i] == '"') inDQuote = false;
		} else {
			if (s[i] == '{' || s[i] == '[') depth++;
			else if (s[i] == '}' || s[i] == ']') depth--;
			else if (s[i] == '\'') inSQuote = true;
			else if (s[i] == '"') inDQuote = true;
		}
	}
	return depth > 0;
}

// Check if & is inside quotes at the given position in s
// Handles escaped quotes: \" inside "", '' inside ''
inline bool yamlAmpInQuote(std::string_view s, size_t ampPos) {
	bool inSQuote = false, inDQuote = false;
	for (size_t i = 0; i < ampPos && i < s.size(); i++) {
		if (inSQuote) {
			if (s[i] == '\'') {
				if (i + 1 < s.size() && s[i + 1] == '\'') i++;
				else inSQuote = false;
			}
		} else if (inDQuote) {
			if (s[i] == '\\') i++;
			else if (s[i] == '"') inDQuote = false;
		} else {
			if (s[i] == '\'') inSQuote = true;
			else if (s[i] == '"') inDQuote = true;
		}
	}
	return inSQuote || inDQuote;
}

// Resolve *alias with cycle detection
inline std::string yamlResolveAlias(std::string_view aliasName,
                                    std::unordered_map<std::string, std::string>& anchors,
                                    std::unordered_set<std::string>& resolving) {
	std::string name(aliasName);
	if (resolving.count(name)) {
		throw asvJSONError("YAML: cyclic alias '" + name + "'");
	}
	auto it = anchors.find(name);
	if (it == anchors.end()) {
		throw asvJSONError("YAML: undefined alias '" + name + "'");
	}
	resolving.insert(name);
	std::string result = it->second;
	resolving.erase(name);
	return result;
}

// Forward declarations for flow parsers (anchored versions support &/* inside flow)
inline std::string yamlParseFlowValue(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors = nullptr,
    std::unordered_set<std::string>* resolving = nullptr);
inline std::string yamlParseFlowMap(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors = nullptr,
    std::unordered_set<std::string>* resolving = nullptr);
inline std::string yamlParseFlowSeq(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors = nullptr,
    std::unordered_set<std::string>* resolving = nullptr);

// Parse a complete YAML inline value and return JSON string
inline std::string yamlParseInlineValue(std::string_view s) {
	while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.remove_prefix(1);
	if (s.empty()) return "null";
	if (s[0] == '!' && s.size() > 1) return yamlParseTaggedValue(s);
	if (s.size() >= 2) {
		if (s[0] == '"') {
			std::string inner;
			size_t i = 1;
			for (; i < s.size(); i++) {
				if (s[i] == '\\' && i + 1 < s.size()) { inner += s[i]; inner += s[i + 1]; i++; continue; }
				if (s[i] == '"') break;
				inner += s[i];
			}
			if (i < s.size()) {
				std::string unesc = yamlUnescapeDouble(inner);
				std::string out = "\"";
				for (auto c : unesc) {
					switch (c) {
						case '"': out += "\\\""; break;
						case '\\': out += "\\\\"; break;
						case '\n': out += "\\n"; break;
						case '\t': out += "\\t"; break;
						case '\r': out += "\\r"; break;
						default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
						         else out += c;
					}
				}
				out += "\"";
				return out;
			}
		} else if (s[0] == '\'') {
			size_t end = 1;
			for (; end < s.size(); end++) {
				if (s[end] == '\'' && end + 1 < s.size() && s[end + 1] == '\'') end++;
				else if (s[end] == '\'') break;
			}
			if (end < s.size()) {
				std::string inner(s.substr(1, end - 1));
				std::string unesc = yamlUnescapeSingle(inner);
				std::string out = "\"";
				for (auto c : unesc) {
					if (c == '"') out += "\\\"";
					else if (c == '\\') out += "\\\\";
					else out += c;
				}
				out += "\"";
				return out;
			}
		}
		if (s[0] == '{' || s[0] == '[') {
			std::string tmp(s);
			size_t fp = 0;
			return yamlParseFlowValue(tmp, fp);
		}
	}
	return yamlScalarToJson(s);
}

// JSON-escape a string for use as JSON object key
inline std::string yamlEscKey(std::string_view s) {
	std::string out;
	for (auto c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			default: out += c;
		}
	}
	return out;
}

// Forward declarations needed by flow parsers
inline std::string yamlParseTaggedValue(std::string_view s);
inline std::string yamlParseInlineValue(std::string_view s);

// Gather complete flow collection text across multiple lines, stripping comments
inline std::string yamlGatherFlow(const std::vector<std::string>& lines, std::string_view firstPart, size_t& li) {
	char openCh = firstPart[0];
	char closeCh = (openCh == '{') ? '}' : ']';
	std::string result(firstPart);
	int depth = 0;
	bool inSq = false, inDq = false;
	// Count initial depth from firstPart
	for (size_t i = 0; i < firstPart.size(); i++) {
		char c = firstPart[i];
		if (inSq) { if (c == '\'' && i + 1 < firstPart.size() && firstPart[i + 1] == '\'') i++; else if (c == '\'') inSq = false; }
		else if (inDq) { if (c == '\\') i++; else if (c == '"') inDq = false; }
		else {
			if (c == '\'') inSq = true; else if (c == '"') inDq = true;
			else if (c == '#') break;
			else if (c == openCh) depth++; else if (c == closeCh) depth--;
		}
	}
	if (depth <= 0) return result;
	// Read more lines until depth closes
	while (li + 1 < lines.size() && depth > 0) {
		li++;
		std::string line(stripIndent(lines[li]));
		std::string clean;
		inSq = false; inDq = false;
		for (size_t i = 0; i < line.size(); i++) {
			char c = line[i];
			if (inSq) {
				if (c == '\'' && i + 1 < line.size() && line[i + 1] == '\'') { clean += c; i++; clean += line[i]; }
				else { clean += c; if (c == '\'') inSq = false; }
			} else if (inDq) {
				clean += c;
				if (c == '\\' && i + 1 < line.size()) { i++; clean += line[i]; }
				else if (c == '"') inDq = false;
			} else if (c == '#') break;
			else {
				if (c == '\'') inSq = true; else if (c == '"') inDq = true;
				if (c == openCh) depth++; else if (c == closeCh) depth--;
				clean += c;
			}
		}
		if (!result.empty() && !clean.empty()) result += ' ';
		result += clean;
	}
	return result;
}

inline std::string yamlParseFlowValue(const std::string& s, size_t& pos);

inline std::string yamlParseFlowMap(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving) {
	pos++; // skip {
	std::string out = "{";
	bool first = true;
	while (pos < s.size()) {
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
		if (pos >= s.size() || s[pos] == '}') break;
		if (s[pos] == ',') { pos++; continue; }
		if (!first) out += ",";
		first = false;
		std::string key = yamlParseFlowValue(s, pos, anchors, resolving);
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
		if (pos < s.size() && s[pos] == ':') {
			pos++;
			while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
			out += key + ":" + yamlParseFlowValue(s, pos, anchors, resolving);
		} else {
			out += key + ":null";
		}
	}
	if (pos < s.size() && s[pos] == '}') pos++;
	out += "}";
	return out;
}

inline std::string yamlParseFlowSeq(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving) {
	pos++; // skip [
	std::string out = "[";
	bool first = true;
	while (pos < s.size()) {
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
		if (pos >= s.size() || s[pos] == ']') break;
		if (s[pos] == ',') { pos++; continue; }
		if (!first) out += ",";
		first = false;
		out += yamlParseFlowValue(s, pos, anchors, resolving);
	}
	if (pos < s.size() && s[pos] == ']') pos++;
	out += "]";
	return out;
}

inline std::string yamlParseFlowValue(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving) {
	while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
	if (pos >= s.size()) return "null";
	// Handle &anchor inside flow
	if (anchors && s[pos] == '&' && pos + 1 < s.size() && yamlIsAnchorChar(s[pos + 1])) {
		size_t ns = pos + 1;
		size_t ne = ns;
		while (ne < s.size() && yamlIsAnchorChar(s[ne])) ne++;
		std::string name(s, ns, ne - ns);
		pos = ne;
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
		std::string val = yamlParseFlowValue(s, pos, anchors, resolving);
		if (anchors->size() >= MAX_YAML_ANCHORS) throw asvJSONError("YAML: too many anchors");
		(*anchors)[name] = val;
		return val;
	}
	// Handle *alias inside flow
	if (anchors && resolving && s[pos] == '*' && pos + 1 < s.size() && yamlIsAnchorChar(s[pos + 1])) {
		size_t ns = pos + 1;
		size_t ne = ns;
		while (ne < s.size() && yamlIsAnchorChar(s[ne])) ne++;
		std::string name(s, ns, ne - ns);
		pos = ne;
		return yamlResolveAlias(name, *anchors, *resolving);
	}
	if (s[pos] == '{') return yamlParseFlowMap(s, pos, anchors, resolving);
	if (s[pos] == '[') return yamlParseFlowSeq(s, pos, anchors, resolving);
	if (s[pos] == '"') {
		size_t start = pos; pos++;
		while (pos < s.size()) { if (s[pos] == '\\') pos += 2; else if (s[pos] == '"') break; else pos++; }
		if (pos < s.size()) pos++;
		return yamlParseInlineValue(std::string_view(s.data() + start, pos - start));
	}
	if (s[pos] == '\'') {
		size_t start = pos; pos++;
		while (pos < s.size()) { if (s[pos] == '\'' && pos + 1 < s.size() && s[pos + 1] == '\'') pos += 2; else if (s[pos] == '\'') break; else pos++; }
		if (pos < s.size()) pos++;
		return yamlParseInlineValue(std::string_view(s.data() + start, pos - start));
	}
	if (s[pos] == '!' && pos + 1 < s.size()) {
		size_t tagStart = pos; pos++;
		while (pos < s.size() && s[pos] != ' ' && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != '\n' && s[pos] != '\t') pos++;
		std::string tagStr(s, tagStart, pos - tagStart);
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
		size_t valStart = pos;
		if (pos < s.size() && s[pos] == '"') {
			pos++;
			while (pos < s.size()) { if (s[pos] == '\\') pos += 2; else if (s[pos] == '"') break; else pos++; }
			if (pos < s.size()) pos++;
		} else if (pos < s.size() && s[pos] == '\'') {
			pos++;
			while (pos < s.size()) { if (s[pos] == '\'' && pos + 1 < s.size() && s[pos + 1] == '\'') pos += 2; else if (s[pos] == '\'') break; else pos++; }
			if (pos < s.size()) pos++;
		} else {
			while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != '\n') pos++;
		}
		std::string combined = tagStr + " " + std::string(s, valStart, pos - valStart);
		while (!combined.empty() && (combined.back() == ' ' || combined.back() == '\t')) combined.pop_back();
		return yamlParseTaggedValue(combined);
	}
	// Plain scalar -- stop at , ] } \n or : followed by space/end/closer
	size_t start = pos;
	while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != '\n') {
		if (s[pos] == ':' && (pos + 1 >= s.size() || s[pos + 1] == ' ' || s[pos + 1] == '\t' || s[pos + 1] == '\n' || s[pos + 1] == '}' || s[pos + 1] == ']' || s[pos + 1] == ',')) break;
		pos++;
	}
	std::string_view sv(s.data() + start, pos - start);
	while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
	if (sv.empty()) return "null";
	return yamlParseInlineValue(sv);
}

inline std::string yamlToJson(std::string_view input);
inline std::string yamlParseDoc(std::string_view input);

// Pre-scan lines for &anchor definitions, building name->JSON map.
// For block anchors, extracts child lines and recursively calls yamlToJson.
// depth: recursion guard to detect cycles (max 64).
// Returns true if any anchors were found.
inline bool yamlBuildAnchorMap(std::vector<std::string>& lines,
                               std::unordered_map<std::string, std::string>& anchors,
                               int depth = 0) {
	if (depth > 64) throw asvJSONError("YAML: maximum anchor recursion depth exceeded");
	bool found = false;
	for (size_t li = 0; li < lines.size(); li++) {
		int indent = countIndent(lines[li]);
		std::string_view content = stripIndent(lines[li]);
		if (content.empty() || content[0] == '#') continue;

		size_t amp = content.find('&');
		if (amp == std::string_view::npos) continue;
		if (yamlAmpInQuote(content, amp)) continue;
		if (yamlInFlowBrackets(content, amp)) continue; // flow anchors handled during parse

		size_t ns = amp + 1;
		if (ns >= content.size() || !yamlIsAnchorChar(content[ns])) continue;
		size_t ne = ns;
		while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
		std::string name(content.substr(ns, ne - ns));
		if (anchors.count(name)) continue;

		if (anchors.size() >= MAX_YAML_ANCHORS) throw asvJSONError("YAML: too many anchors");

		found = true;
		std::string_view rest = content.substr(ne);
		while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);

		if (!rest.empty() && rest[0] != '#') {
			// Scalar anchor -- value follows on same line
			anchors[name] = yamlParseInlineValue(rest);
		} else {
			// Block anchor -- collect child lines
			std::string childYaml;
			size_t ci = li + 1;
			while (ci < lines.size()) {
				int si = countIndent(lines[ci]);
				std::string_view sc = stripIndent(lines[ci]);
				if (sc.empty()) { ci++; continue; }
				if (sc[0] == '#') { ci++; continue; }
				if (si > indent) {
					if (!childYaml.empty()) childYaml += '\n';
					childYaml += lines[ci];
					ci++;
				} else {
					break;
				}
			}
			if (!childYaml.empty()) {
				// Normalize indentation: find minimum indent and strip it
				auto chLines = splitLines(childYaml);
				size_t minIndent = SIZE_MAX;
				for (auto& cl : chLines) {
					std::string_view cs = stripIndent(cl);
					if (cs.empty() || cs[0] == '#') continue;
					size_t ci2 = static_cast<size_t>(countIndent(cl));
					if (ci2 < minIndent) minIndent = ci2;
				}
				if (minIndent > 0 && minIndent != SIZE_MAX) {
					std::string normalized;
					for (auto& cl : chLines) {
						if (!normalized.empty()) normalized += '\n';
						std::string_view cs = stripIndent(cl);
						if (cs.empty() || cs[0] == '#') {
							// Keep empty/comment lines as-is
							normalized += std::string(cl);
						} else {
							normalized += cl.substr(minIndent);
						}
					}
					childYaml = normalized;
				}
				anchors[name] = yamlParseDoc(childYaml);
			} else {
				anchors[name] = "null";
			}
		}
	}
	return found;
}

// Strip &anchor tags from a content string, returning cleaned version
inline std::string yamlStripAnchors(std::string_view s, std::string* outAnchor) {
	std::string result;
	result.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		if (s[i] == '&') {
			if (i + 1 < s.size() && yamlIsAnchorChar(s[i + 1])) {
				size_t start = i + 1;
				size_t end = start;
				while (end < s.size() && yamlIsAnchorChar(s[end])) end++;
				if (outAnchor) *outAnchor = std::string(s.substr(start, end - start));
				i = end;
				while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
				continue;
			}
		}
		result += s[i++];
	}
	while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) result.pop_back();
	return result;
}

// Collect a YAML block scalar (| or >). `header` is the block header text
// (starting with the style char, may include an indent indicator [1-9] and a
// chomp indicator +|-). `li` is the line index of the header line. Gathers
// content lines, applies folding for '>', then chomps trailing line breaks:
// clip (default) keeps one, '-' strips all, '+' keeps all.
// parentIndent < 0 disables the "content must be deeper than parent" check.
inline std::string yamlParseBlockScalar(const std::string& header, const std::vector<std::string>& lines, size_t& li, bool isFolded, int parentIndent) {
	char chomp = 0; // 0 = clip, '+' = keep, '-' = strip
	for (size_t h = 1; h < header.size(); h++) {
		char ch = header[h];
		if (ch == ' ' || ch == '\t') continue;
		if (ch >= '1' && ch <= '9') continue; // explicit indent indicator (indent still auto-detected)
		if (ch == '+' || ch == '-') { chomp = ch; continue; }
		break; // comment or trailing text
	}

	std::string text;
	int contentIndent = -1;
	size_t ci = li + 1;
	// splitLines appends a phantom empty line at EOF when input ends with '\n'.
	// That line carries no real content, so it must be excluded from block-scalar
	// collection (otherwise keep-chomp '+' would preserve a spurious trailing '\n').
	size_t last = lines.size();
	if (last > 0 && lines[last - 1].empty()) last--;
	while (ci < last) {
		int si = countIndent(lines[ci]);
		yamlRejectTabIndent(lines[ci], static_cast<int>(ci) + 1);
		std::string_view sc = stripIndent(lines[ci]);
		if (sc.empty()) { if (contentIndent > 0) text += '\n'; ci++; continue; }
		if (sc[0] == '#') { ci++; continue; }
		if (parentIndent >= 0 && si <= parentIndent) break;
		if (contentIndent < 0) contentIndent = si;
		if (si >= contentIndent) {
			text += sc; text += '\n';
			ci++;
			continue;
		}
		break;
	}
	if (ci > li + 1) li = ci - 1;

	if (isFolded) {
		std::string folded;
		bool prevNewline = false;
		for (size_t i = 0; i < text.size(); i++) {
			if (text[i] == '\n') {
				if (prevNewline) { folded += '\n'; }
				prevNewline = true;
			} else {
				if (prevNewline && !folded.empty() && folded.back() != '\n') folded += ' ';
				prevNewline = false;
				folded += text[i];
			}
		}
		text = folded;
	}

	size_t trailing = 0;
	while (trailing < text.size() && text[text.size() - 1 - trailing] == '\n') trailing++;
	if (chomp == '-') {
		text.erase(text.size() - trailing);
	} else if (chomp == 0) {
		text.erase(text.size() - trailing);
		if (!text.empty()) text += '\n';
	}
	return text;
}

// YAML forbids using tabs for indentation (Section 10). A tab appearing in the
// leading whitespace of a line is a structural error; throw to match UDE behavior.
inline void yamlRejectTabIndent(const std::string& line, int lineNum) {
	for (char c : line) {
		if (c == ' ') continue;
		if (c == '\t') throw asvJSONError("YAML: tab used as indentation at line " + std::to_string(lineNum));
		break;
	}
}

// Parse a single YAML document (no multi-doc handling)
inline std::string yamlParseDoc(std::string_view input) {
	auto lines = splitLines(input);
	if (lines.empty()) return "{}";

	// Scan for directives (%YAML, %TAG) at the beginning of the document
	// Directives are at indent 0 and precede any content
	yamlDocTagMap.clear();
	size_t dirEnd = 0;
	while (dirEnd < lines.size()) {
		std::string_view c = stripIndent(lines[dirEnd]);
		if (c.empty() || c[0] == '#') { dirEnd++; continue; }
		if (countIndent(lines[dirEnd]) > 0 || c[0] != '%') break;
		if (c.size() >= 6 && c.substr(0, 6) == "%TAG ") {
			// %TAG handle prefix
			std::string_view rest = c.substr(5);
			size_t pos = 0;
			while (pos < rest.size() && rest[pos] == ' ') pos++;
			if (pos < rest.size() && rest[pos] == '!') {
				size_t hStart = pos; pos++;
				if (pos < rest.size() && rest[pos] == '!') pos++;
				else if (pos < rest.size() && rest[pos] != ' ') {
					while (pos < rest.size() && rest[pos] != ' ' && rest[pos] != '!') pos++;
					if (pos < rest.size() && rest[pos] == '!') pos++;
				}
				std::string_view handle = rest.substr(hStart, pos - hStart);
				while (pos < rest.size() && rest[pos] == ' ') pos++;
				std::string_view pfx = rest.substr(pos);
				size_t hash = pfx.find('#');
				if (hash != std::string_view::npos) pfx = pfx.substr(0, hash);
				while (!pfx.empty() && (pfx.back() == ' ' || pfx.back() == '\t')) pfx.remove_suffix(1);
				if (!pfx.empty()) yamlDocTagMap[std::string(handle)] = std::string(pfx);
			}
		}
		// %YAML -- silently skip any version
		dirEnd++;
	}
	// Strip directive lines so they don't reach the main parser
	if (dirEnd > 0) lines.erase(lines.begin(), lines.begin() + (std::ptrdiff_t)dirEnd);

	// Pre-scan for anchors (each document has independent anchor namespace)
	std::unordered_map<std::string, std::string> anchors;
	std::unordered_set<std::string> resolving;
	yamlBuildAnchorMap(lines, anchors);

	std::string out;
	std::vector<FormatFrame> stack;
	static constexpr size_t MAX_YAML_DEPTH = 64;

	// Find first meaningful line to establish root indent
	size_t firstLine = 0;
	while (firstLine < lines.size()) {
		std::string_view c = stripIndent(lines[firstLine]);
		if (c.empty() || c[0] == '#') firstLine++;
		else break;
	}
	if (firstLine >= lines.size()) return "{}";

	{
		std::string_view fc = stripIndent(lines[firstLine]);
		if (fc == "{}") return "{}";
		if (fc == "[]") return "[]";
	}

	bool rootIsArr = false;
	{
		std::string_view fc = stripIndent(lines[firstLine]);
		if (fc.size() >= 2 && fc[0] == '-' && fc[1] == ' ') rootIsArr = true;
	}

	int rootIndent = countIndent(lines[firstLine]);

	// Pre-scan to determine root type
	if (rootIsArr) {
		out += '[';
		FormatFrame f; f.type = 'A'; f.align = rootIndent; f.first = true; f.hasVal = true; f.isRoot = true;
		if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
		stack.push_back(f);
	} else {
		out += '{';
		FormatFrame f; f.type = 'O'; f.align = rootIndent; f.first = true; f.hasVal = true; f.isRoot = true;
		if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
		stack.push_back(f);
	}

	size_t maxLine = lines.size();
	for (size_t li = firstLine; li < maxLine; li++) {
		int yamlLineNum = static_cast<int>(li) + 1;
		try {
		int indent = countIndent(lines[li]);
		yamlRejectTabIndent(lines[li], yamlLineNum);
		std::string content = std::string(stripIndent(lines[li]));
		if (content.empty() || content[0] == '#') continue;

		// Strip trailing comment (respecting quotes)
		content = std::string(yamlStripComment(content));
		while (!content.empty() && (content.back() == ' ' || content.back() == '\t')) content.pop_back();
		if (content.empty()) continue;

		// Strip &anchor tags (they were already captured by pre-scan)
		// Must check yamlAmpInQuote to avoid removing & inside quoted strings
		{
			std::string cleaned;
			size_t i = 0;
			while (i < content.size()) {
				if (content[i] == '&' && i + 1 < content.size() && yamlIsAnchorChar(content[i + 1]) && !yamlAmpInQuote(content, i)) {
					if (!yamlInFlowBrackets(content, i)) {
						size_t ns = i + 1;
						size_t ne = ns;
						while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
						i = ne;
						while (i < content.size() && (content[i] == ' ' || content[i] == '\t')) i++;
						continue;
					}
				}
				cleaned += content[i++];
			}
			while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '\t')) cleaned.pop_back();
			content = cleaned;
		}
		if (content.empty()) continue;

		// Handle flow containers {} and [] at any level
		if (content == "{}") {
			closeFrames(stack, out, indent);
			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;
			out += "{}";
			continue;
		}
		if (content == "[]") {
			closeFrames(stack, out, indent);
			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;
			out += "[]";
			continue;
		}

		if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
			// --- Array item ---
			while (!stack.empty()) {
				auto& top = stack.back();
				if (top.isRoot && indent <= top.align) break;
				if (indent >= static_cast<int>(top.align)) break;
				out += (top.type == 'A') ? ']' : '}';
				stack.pop_back();
			}

			if (stack.empty() || stack.back().type != 'A') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
				out += '[';
				FormatFrame f; f.type = 'A'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
				stack.push_back(f);
			}

			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;

			std::string rest = content.substr(2);
			while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

			if (rest.empty()) {
				out += "null";
			} else if (rest.size() > 1 && rest[0] == '*') {
				out += yamlResolveAlias(rest.substr(1), anchors, resolving);
			} else {
				out += yamlParseInlineValue(rest);
			}
			continue;
		}

		// --- Block scalar "|" (literal) or ">" (folded) ---
		// Parse the header (style char, optional indent indicator [1-9], optional
		// chomp indicator +|-), collect content lines, fold if needed, then apply
		// chomping: clip (default) keeps one trailing line break, '-' strips all,
		// '+' keeps all (YAML 1.2 spec).
		if (content[0] == '|' || content[0] == '>') {
			bool isFolded = (content[0] == '>');
			closeFrames(stack, out, indent);

			std::string text = yamlParseBlockScalar(content, lines, li, isFolded, -1);

			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;

			std::string escaped;
			for (auto c : text) {
				switch (c) {
					case '"': escaped += "\\\""; break;
					case '\\': escaped += "\\\\"; break;
					case '\n': escaped += "\\n"; break;
					case '\t': escaped += "\\t"; break;
					default: escaped += c;
				}
			}
			out += "\"" + escaped + "\"";
			continue;
		}

		// --- Key-value pair ---
		size_t colonPos = content.find(':');
		if (colonPos != std::string::npos) {
			closeFrames(stack, out, indent);

			std::string keyPart = content.substr(0, colonPos);
			while (!keyPart.empty() && (keyPart.back() == ' ' || keyPart.back() == '\t')) keyPart.pop_back();

			std::string valPart;
			if (colonPos + 1 < content.size()) valPart = content.substr(colonPos + 1);
			while (!valPart.empty() && (valPart[0] == ' ' || valPart[0] == '\t')) valPart.erase(0, 1);

			std::string rawKey;
			if (keyPart.size() >= 2 && keyPart[0] == '"' && keyPart.back() == '"') {
				rawKey = yamlUnescapeDouble(keyPart.substr(1, keyPart.size() - 2));
			} else if (keyPart.size() >= 2 && keyPart[0] == '\'' && keyPart.back() == '\'') {
				rawKey = yamlUnescapeSingle(keyPart.substr(1, keyPart.size() - 2));
			} else {
				rawKey = std::string(keyPart);
			}

			if (stack.empty() || stack.back().type != 'O') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
				out += '{';
				FormatFrame f; f.type = 'O'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
				stack.push_back(f);
			}

			std::string jsonKey = yamlEscKey(rawKey);

			if (!valPart.empty() && valPart[0] != '#') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				out += "\"" + jsonKey + "\":";

				if (valPart[0] == '|' || valPart[0] == '>') {
					bool isFolded = (valPart[0] == '>');
					std::string text = yamlParseBlockScalar(valPart, lines, li, isFolded, indent);
					std::string escaped;
					for (auto c : text) {
						switch (c) {
							case '"': escaped += "\\\""; break;
							case '\\': escaped += "\\\\"; break;
							case '\n': escaped += "\\n"; break;
							case '\t': escaped += "\\t"; break;
							default: escaped += c;
						}
					}
					out += "\"" + escaped + "\"";
				} else if (valPart[0] == '{' || valPart[0] == '[') {
					std::string flowText = yamlGatherFlow(lines, valPart, li);
					size_t fp = 0;
					out += yamlParseFlowValue(flowText, fp, &anchors, &resolving);
				} else if (valPart[0] == '!' && valPart.size() > 1) {
					out += yamlParseTaggedValue(valPart);
				} else if (valPart.size() > 1 && valPart[0] == '*') {
					out += yamlResolveAlias(std::string_view(valPart).substr(1), anchors, resolving);
				} else {
					out += yamlParseInlineValue(valPart);
				}
			} else {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				out += "\"" + jsonKey + "\":";

				bool isArrayValue = false;
				bool isFlowValue = false;
				size_t scan = li + 1;
				while (scan < lines.size()) {
					std::string_view sc = stripIndent(lines[scan]);
					if (sc.empty() || sc[0] == '#') { scan++; continue; }
					int scIndent = countIndent(lines[scan]);
					if (scIndent < indent) break;
					if (sc.size() >= 1 && (sc[0] == '{' || sc[0] == '[')) { isFlowValue = true; break; }
					if (scIndent == indent) {
						if (sc.size() >= 2 && sc[0] == '-' && sc[1] == ' ') { isArrayValue = true; break; }
						break;
					}
					if (sc.size() >= 2 && sc[0] == '-' && sc[1] == ' ') { isArrayValue = true; break; }
					if (sc.find(':') != std::string_view::npos) break;
					break;
				}

				if (isFlowValue) {
					// Output flow value directly as value for this key
					std::string flowText = yamlGatherFlow(lines, std::string(stripIndent(lines[scan])), scan);
					size_t fp = 0;
					out += yamlParseFlowValue(flowText, fp, &anchors, &resolving);
					li = scan; // skip flow lines
					stack.back().hasVal = true;
				} else {
					if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
					if (isArrayValue) {
						out += '[';
						FormatFrame f; f.type = 'A'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
						stack.push_back(f);
					} else {
						out += '{';
						FormatFrame f; f.type = 'O'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
						stack.push_back(f);
					}
				}
			}
			continue;
		}

		// --- Bare value ---
		closeFrames(stack, out, indent);
		addComma(stack, out);
		if (!stack.empty()) stack.back().first = false;
		if (content.size() > 1 && content[0] == '*') {
			out += yamlResolveAlias(std::string_view(content).substr(1), anchors, resolving);
		} else {
			out += yamlParseInlineValue(content);
		}
		} catch (const asvJSONError& e) {
			throw asvJSONError(std::string(e.what()) + " (line " + std::to_string(yamlLineNum) + ")");
		}
	}

	// Close all remaining frames
	while (!stack.empty()) {
		out += (stack.back().type == 'A') ? ']' : '}';
		stack.pop_back();
	}

	if (out.empty()) return "{}";
	return out;
}

// Main YAML -> JSON entry point -- handles multi-document streams
inline std::string yamlToJson(std::string_view input) {
	auto lines = splitLines(input);
	if (lines.empty()) return "{}";

	// Find document boundaries: "---" at indent 0
	std::vector<std::pair<size_t, size_t>> docs; // [firstLine, pastEnd)
	size_t docStart = 0;
	bool inDoc = false;
	size_t pendingDirStart = SIZE_MAX; // first %-directive line before next doc

	for (size_t i = 0; i < lines.size(); i++) {
		std::string_view stripped = stripIndent(lines[i]);
		if (stripped.empty() || stripped[0] == '#') continue;

		if (stripped == "---" && countIndent(lines[i]) == 0) {
			if (inDoc) {
				docs.back().second = i; // end at separator
				inDoc = false;
			}
			docStart = i + 1;
			if (pendingDirStart != SIZE_MAX) {
				docStart = pendingDirStart; // include directives before ---
				pendingDirStart = SIZE_MAX;
			}
			continue;
		}

		// %-directives at indent 0 are not content -- they belong to the next doc
		if (stripped[0] == '%' && countIndent(lines[i]) == 0) {
			if (pendingDirStart == SIZE_MAX) pendingDirStart = i;
			continue;
		}

		if (!inDoc) {
			inDoc = true;
			docs.push_back({docStart, lines.size()}); // tentative end
		}
	}

	// Filter out documents with no real content (directives-only, etc.)
	std::vector<std::pair<size_t, size_t>> validDocs;
	for (auto& d : docs) {
		bool hasContent = false;
		for (size_t i = d.first; i < d.second && !hasContent; i++) {
			std::string_view c = stripIndent(lines[i]);
			if (!c.empty() && c[0] != '#' && c[0] != '%' && c != "---" && c != "...") hasContent = true;
		}
		if (hasContent) validDocs.push_back(d);
	}

	if (validDocs.empty()) return "{}";

	auto buildDocInput = [&](size_t start, size_t end) -> std::string {
		std::string sub;
		for (size_t i = start; i < end; i++) {
			std::string_view c = stripIndent(lines[i]);
			if ((c == "---" || c == "...") && countIndent(lines[i]) == 0) continue;
			if (!sub.empty()) sub += '\n';
			sub += lines[i];
		}
		return sub;
	};

	if (validDocs.size() == 1) {
		return yamlParseDoc(buildDocInput(validDocs[0].first, validDocs[0].second));
	}

	std::string result = "[";
	for (size_t d = 0; d < validDocs.size(); d++) {
		if (d > 0) result += ",";
		result += yamlParseDoc(buildDocInput(validDocs[d].first, validDocs[d].second));
	}
	result += "]";
	return result;
}

inline bool asvJSON::fromYAML(std::string_view input) {
	try {
		if (input.empty()) throw asvJSONError("empty input");
		std::string json = yamlToJson(input);
		allowNaNInfinity = true;
		return parse(std::string_view(json));
	} catch (const asvJSONError& e) {
		lastError = e.what();
		return false;
	}
}

} // namespace asvJSONInternal
