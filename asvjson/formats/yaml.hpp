#pragma once
// YAML serialization/parsing for asvJSON++

#include "../core.hpp"

// ==================== YAML Encoder ====================

inline void asvJSONValue::toYAML(std::string& out) const {
	out += "---\n";
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
			if (std::isnan(dbl) || std::isinf(dbl)) { out += prefix() + "~\n"; break; }
			std::string val; fmtDoubleVal(dbl, val);
			out += prefix() + val + "\n";
			break;
		}
		case T::STRING: {
			if (str_data.find('\n') != std::string_view::npos) {
				out += prefix() + "|\n";
				for (size_t i = 0; i < str_data.size(); ) {
					size_t eol = str_data.find('\n', i);
					if (eol == std::string_view::npos) eol = str_data.size();
					out += std::string((indent + 1) * 2, ' ') + std::string(str_data.data() + i, eol - i) + "\n";
					i = eol + 1;
				}
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
			out += prefix() + "\n";
			for (const auto& [k, v] : *obj) v->toYAML(out, indent + 1, k, false);
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

// ==================== YAML Decoder (YAML → JSON → parse) ====================

namespace asvJSONInternal {

// Unescape YAML double-quoted string (\n, \t, \\, \", \xNN, \uNNNN)
static std::string yamlUnescapeDouble(std::string_view s) {
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

// Unescape YAML single-quoted string ('' → ')
static std::string yamlUnescapeSingle(std::string_view s) {
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
static std::string_view yamlStripComment(std::string_view s) {
	bool inSQ = false, inDQ = false;
	for (size_t i = 0; i < s.size(); i++) {
		if (!inDQ && !inSQ && s[i] == '#') return s.substr(0, i);
		if (s[i] == '"' && !inSQ) inDQ = !inDQ;
		if (s[i] == '\'' && !inDQ) inSQ = !inSQ;
	}
	return s;
}

// Convert YAML scalar to JSON (auto-detect type)
static std::string yamlScalarToJson(std::string_view s) {
	if (s.empty() || s == "~" || s == "null" || s == "NULL" || s == "Null") return "null";
	if (s == "true" || s == "TRUE" || s == "True" || s == "yes" || s == "YES" || s == "Yes" || s == "on" || s == "ON" || s == "On")
		return "true";
	if (s == "false" || s == "FALSE" || s == "False" || s == "no" || s == "NO" || s == "No" || s == "off" || s == "OFF" || s == "Off")
		return "false";
	if (s == "NaN" || s == ".NaN") return "NaN";
	if (s == "Infinity" || s == ".Inf" || s == ".inf") return "Infinity";
	if (s == "-Infinity" || s == "-.Inf" || s == "-.inf") return "-Infinity";
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

static std::string yamlParseInlineValue(std::string_view s);

// Convert unescaped regex "pattern|opts" to internal pattern|opts
static std::string yamlRegexToInternal(std::string_view s) {
	return std::string(s);
}

// Parse tagged YAML value (!!binary, !objectid, !regex, !ext) and return JSON string
static std::string yamlParseTaggedValue(std::string_view tagBody) {
	if (tagBody.size() >= 9 && tagBody.substr(0, 9) == "!!binary ")
		return "\"__BASE64__" + std::string(tagBody.substr(9)) + "\"";
	if (tagBody.size() >= 10 && tagBody.substr(0, 10) == "!objectid ")
		return "\"__OID__" + std::string(tagBody.substr(10)) + "\"";
	if (tagBody.size() >= 7 && tagBody.substr(0, 7) == "!regex ") {
		std::string resolved = yamlParseInlineValue(tagBody.substr(7));
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		std::string internal = yamlRegexToInternal(raw);
		std::string out = "\"__REGEX__";
		for (auto c : internal) {
			if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c;
		}
		out += "\"";
		return out;
	}
	if (tagBody.size() >= 5 && tagBody.substr(0, 5) == "!ext ") {
		std::string remaining = std::string(tagBody.substr(5));
		size_t sp = remaining.find(' ');
		if (sp == std::string::npos) return yamlParseInlineValue(tagBody);
		int extType = 0;
		std::from_chars(remaining.data(), remaining.data() + sp, extType);
		std::string resolved = yamlParseInlineValue(std::string_view(remaining).substr(sp + 1));
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		return "\"__EXT__" + std::to_string(extType) + "__" + raw + "\"";
	}
	return yamlParseInlineValue(tagBody);
}

// Parse a complete YAML inline value and return JSON string
static std::string yamlParseInlineValue(std::string_view s) {
	while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.remove_prefix(1);
	if (s.empty()) return "null";
	if (s.size() >= 9 && s.substr(0, 9) == "!!binary ") return yamlParseTaggedValue(s);
	if (s.size() >= 10 && s.substr(0, 10) == "!objectid ") return yamlParseTaggedValue(s);
	if (s.size() >= 7 && s.substr(0, 7) == "!regex ") return yamlParseTaggedValue(s);
	if (s.size() >= 5 && s.substr(0, 5) == "!ext ") return yamlParseTaggedValue(s);
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
		if (s[0] == '{' || s[0] == '[') return std::string(s);
	}
	return yamlScalarToJson(s);
}

// JSON-escape a string for use as JSON object key
static std::string yamlEscKey(std::string_view s) {
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

} // namespace asvJSONInternal
using namespace asvJSONInternal;

static std::string yamlToJson(std::string_view input);
static std::string yamlParseDoc(std::string_view input);

static bool yamlIsAnchorChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

// Check if & is inside quotes at the given position in s
// Handles escaped quotes: \" inside "", '' inside ''
static bool yamlAmpInQuote(std::string_view s, size_t ampPos) {
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

// Pre-scan lines for &anchor definitions, building name→JSON map.
// For block anchors, extracts child lines and recursively calls yamlToJson.
// depth: recursion guard to detect cycles (max 64).
// Returns true if any anchors were found.
static bool yamlBuildAnchorMap(std::vector<std::string>& lines,
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

		size_t ns = amp + 1;
		if (ns >= content.size() || !yamlIsAnchorChar(content[ns])) continue;
		size_t ne = ns;
		while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
		std::string name(content.substr(ns, ne - ns));
		if (anchors.count(name)) continue;

		found = true;
		std::string_view rest = content.substr(ne);
		while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);

		if (!rest.empty() && rest[0] != '#') {
			// Scalar anchor — value follows on same line
			anchors[name] = yamlParseInlineValue(rest);
		} else {
			// Block anchor — collect child lines
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
static std::string yamlStripAnchors(std::string_view s, std::string* outAnchor) {
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

// Resolve *alias with cycle detection
static std::string yamlResolveAlias(std::string_view aliasName,
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
	std::string val = it->second;
	resolving.erase(name);
	return val;
}

// Parse a single YAML document (no multi-doc handling)
static std::string yamlParseDoc(std::string_view input) {
	auto lines = splitLines(input);
	if (lines.empty()) return "{}";

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
		int indent = countIndent(lines[li]);
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
					size_t ns = i + 1;
					size_t ne = ns;
					while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
					i = ne;
					while (i < content.size() && (content[i] == ' ' || content[i] == '\t')) i++;
					continue;
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
		if (content[0] == '|' || content[0] == '>') {
			bool isFolded = (content[0] == '>');
			closeFrames(stack, out, indent);

			std::string text;
			int contentIndent = -1;
			size_t ci = li + 1;
			while (ci < lines.size()) {
				int si = countIndent(lines[ci]);
				std::string_view sc = stripIndent(lines[ci]);
				if (sc.empty()) { if (contentIndent > 0) text += '\n'; ci++; continue; }
				if (sc[0] == '#') { ci++; continue; }
				if (si > indent) {
					if (contentIndent < 0) contentIndent = si;
					if (si >= contentIndent) {
						if (!text.empty()) text += '\n';
						text += sc;
						ci++;
						continue;
					}
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

			while (!text.empty() && text.back() == '\n') text.pop_back();

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
					std::string text;
					int contentIndent = -1;
					size_t ci = li + 1;
					while (ci < lines.size()) {
						int si = countIndent(lines[ci]);
						std::string_view sc = stripIndent(lines[ci]);
						if (sc.empty()) { if (contentIndent > 0) text += '\n'; ci++; continue; }
						if (sc[0] == '#') { ci++; continue; }
						if (si > indent) {
							if (contentIndent < 0) contentIndent = si;
							if (si >= contentIndent) {
								if (!text.empty()) text += '\n';
								text += sc;
								ci++;
								continue;
							}
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
					while (!text.empty() && text.back() == '\n') text.pop_back();
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
				} else if (valPart == "{}" || valPart == "[]") {
					out += std::string(valPart);
				} else if (valPart[0] == '{' || valPart[0] == '[') {
					out += std::string(valPart);
				} else if (valPart.size() >= 9 && valPart.substr(0, 9) == "!!binary ") {
					out += yamlParseTaggedValue(valPart);
				} else if (valPart.size() >= 10 && valPart.substr(0, 10) == "!objectid ") {
					out += yamlParseTaggedValue(valPart);
				} else if (valPart.size() >= 7 && valPart.substr(0, 7) == "!regex ") {
					out += yamlParseTaggedValue(valPart);
				} else if (valPart.size() >= 5 && valPart.substr(0, 5) == "!ext ") {
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
				size_t scan = li + 1;
				while (scan < lines.size()) {
					std::string_view sc = stripIndent(lines[scan]);
					if (sc.empty() || sc[0] == '#') { scan++; continue; }
					int scIndent = countIndent(lines[scan]);
					if (scIndent < indent) break;
					if (scIndent == indent) {
						if (sc.size() >= 2 && sc[0] == '-' && sc[1] == ' ') { isArrayValue = true; break; }
						break;
					}
					if (sc.size() >= 2 && sc[0] == '-' && sc[1] == ' ') { isArrayValue = true; break; }
					if (sc.find(':') != std::string_view::npos) break;
					break;
				}

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
	}

	// Close all remaining frames
	while (!stack.empty()) {
		out += (stack.back().type == 'A') ? ']' : '}';
		stack.pop_back();
	}

	if (out.empty()) return "{}";
	return out;
}

// Main YAML → JSON entry point — handles multi-document streams
static std::string yamlToJson(std::string_view input) {
	auto lines = splitLines(input);
	if (lines.empty()) return "{}";

	// Find document boundaries: "---" at indent 0
	std::vector<std::pair<size_t, size_t>> docs; // [firstLine, pastEnd)
	size_t docStart = 0;
	bool inDoc = false;

	for (size_t i = 0; i < lines.size(); i++) {
		std::string_view stripped = stripIndent(lines[i]);
		if (stripped.empty() || stripped[0] == '#') continue;

		if (stripped == "---" && countIndent(lines[i]) == 0) {
			if (inDoc) {
				docs.back().second = i; // end at separator
				inDoc = false;
			}
			docStart = i + 1;
			continue;
		}

		if (!inDoc) {
			inDoc = true;
			docs.push_back({docStart, lines.size()}); // tentative end
		}
	}

	// Filter out empty documents
	std::vector<std::pair<size_t, size_t>> validDocs;
	for (auto& d : docs) {
		bool hasContent = false;
		for (size_t i = d.first; i < d.second && !hasContent; i++) {
			std::string_view c = stripIndent(lines[i]);
			if (!c.empty() && c[0] != '#' && c != "---" && c != "...") hasContent = true;
		}
		if (hasContent) validDocs.push_back(d);
	}

	if (validDocs.empty()) return "{}";

	auto buildDocInput = [&](size_t start, size_t end) -> std::string {
		std::string sub;
		for (size_t i = start; i < end; i++) {
			std::string_view c = stripIndent(lines[i]);
			if (c == "..." && countIndent(lines[i]) == 0) continue;
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
		return parse(std::string_view(json));
	} catch (const asvJSONError& e) {
		lastError = e.what();
		return false;
	}
}
