#pragma once

#include <string>
#include <string_view>
#include <charconv>
#include <cstdlib>

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
	for (unsigned char c : key) {
		// The XML name grammar (and this parser's xmlParseName) accept only
		// alnum, '-', '_', ':', '.' plus UTF-8 characters (bytes >= 0x80).
		// Everything else -- spaces, markup chars, '#', '@', '$', '!', '~',
		// backslashes, punctuation, control bytes -- maps to '_' so a user
		// key never produces an un-parseable <a$b>/<a\x01b> element.
		bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		          c == '-' || c == '_' || c == ':' || c == '.' || c >= 0x80;
		if (ok) name += static_cast<char>(c);
		else name += '_';
	}
	if (name.empty() || (name[0] >= '0' && name[0] <= '9') || name[0] == '-')
		name = "item" + name;
	return name;
}

// ---------- YAML helpers ----------
// True when `s` would be interpreted as a JSON number token if emitted bare
// (e.g. "123", "1.5", "001", "1e4", "+7", "-0.5"). Numeric-looking strings
// must be quoted in YAML so a YAML->JSON round trip does not change their
// type or emit an invalid JSON number ("001" -> "leading zero not allowed").
inline bool yamlLooksNumeric(std::string_view s) {
	std::string_view t = s;
	if (t.size() > 1 && (t[0] == '+' || t[0] == '-')) t.remove_prefix(1);
	if (t.empty()) return false;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
	double d = 0.0;
	auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), d);
	return ec == std::errc() && ptr == t.data() + t.size();
#else
	if (t.size() > 0) {
		std::string tmp(t);
		char* end = nullptr;
		std::strtod(tmp.c_str(), &end);
		if (end == tmp.c_str() + tmp.size()) return true;
	}
	return false;
#endif
}

inline bool yamlNeedsQuotes(std::string_view s) {
	if (s.empty()) return true;
	if (s[0] == ' ' || s[0] == '\t') return true;
	static const char* keywords[] = {"null", "NULL", "Null", "true", "TRUE", "True", "false", "FALSE", "False",
		"yes", "YES", "Yes", "no", "NO", "No", "on", "ON", "On", "off", "OFF", "Off",
		"y", "Y", "n", "N"};
	for (auto kw : keywords) if (s == kw) return true;
	// Strings that would be read back as numbers or special YAML scalars must
	// be quoted so they round-trip as strings ("001", "1.2", ".inf", "~", ...).
	if (yamlLooksNumeric(s)) return true;
	if (s == "~" || s == ".nan" || s == ".NaN" || s == ".NAN" ||
		s == ".inf" || s == ".Inf" || s == ".INF" || s == "+.inf" || s == "-.inf" ||
		s == "Infinity" || s == "+Infinity" || s == "-Infinity" || s == "NaN")
		return true;
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
	if (yamlNeedsQuotes(s)) {
		// Single quotes cannot represent control characters or line breaks
		// safely (a literal '\n' would fold to a space on reparse, corrupting
		// the key), so fall back to double quotes which escape them.
		for (unsigned char c : s)
			if (c < 0x20 || c == 0x7F) return yamlDQuote(s);
		return yamlQuote(s);
	}
	return std::string(s);
}

} // namespace asvJSONInternal
