#pragma once

#include <string>
#include <string_view>

namespace asvJSONInternal {

// ---------- XML helpers ----------
inline std::string xmlEscapeContent(std::string_view s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (auto c : s) {
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default: out += c;
		}
	}
	return out;
}

inline std::string xmlSanitizeElementName(std::string_view key) {
	std::string name;
	name.reserve(key.size());
	for (auto c : key) {
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') name += '_';
		else if (c == '"' || c == '\'' || c == '<' || c == '>' || c == '&') name += '_';
		else name += c;
	}
	if (name.empty() || (name[0] >= '0' && name[0] <= '9') || name[0] == '-')
		name = "item" + name;
	return name;
}

// ---------- YAML helpers ----------
inline bool yamlNeedsQuotes(std::string_view s) {
	if (s.empty()) return true;
	if (s[0] == ' ' || s[0] == '\t') return true;
	static const char* keywords[] = {"null", "NULL", "Null", "true", "TRUE", "True", "false", "FALSE", "False",
		"yes", "YES", "Yes", "no", "NO", "No", "on", "ON", "On", "off", "OFF", "Off",
		"y", "Y", "n", "N"};
	for (auto kw : keywords) if (s == kw) return true;
	for (auto c : s) {
		if (c == ':' || c == '#' || c == '{' || c == '}' || c == '[' || c == ']' ||
			c == ',' || c == '&' || c == '*' || c == '!' || c == '|' || c == '>' ||
			c == '\'' || c == '"' || c == '%' || c == '@' || c == '`')
			return true;
	}
	if (s[0] == '-' && s.size() > 1) {
		bool allNum = true;
		for (size_t i = 1; i < s.size(); i++) if (s[i] < '0' || s[i] > '9') { allNum = false; break; }
		if (allNum) return true;
	}
	for (auto c : s) if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return true;
	return false;
}

inline std::string yamlQuote(std::string_view s) {
	std::string out;
	out = "'";
	for (auto c : s) {
		if (c == '\'') out += "''";
		else out += c;
	}
	out += "'";
	return out;
}

inline std::string yamlDQuote(std::string_view s) {
	std::string out = "\"";
	for (auto c : s) {
		switch (c) {
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			case '\\': out += "\\\\"; break;
			case '"': out += "\\\""; break;
			case '\0': out += "\\0"; break;
			default: out += c;
		}
	}
	out += '"';
	return out;
}

inline std::string yamlQuoteKey(std::string_view s) {
	if (yamlNeedsQuotes(s)) return yamlQuote(s);
	return std::string(s);
}

} // namespace asvJSONInternal
