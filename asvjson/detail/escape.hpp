#pragma once

#include <string>
#include <string_view>
#include <charconv>
#include <cstdlib>
#include <clocale>
#if defined(_WIN32)
#include <locale.h>
#endif

namespace asvJSONInternal {

// ---------- locale-independent strtod ----------
// Every grammar in this library spells decimals with a DOT (JSON's, and the
// "bare value" type-inference of INI/dotenv/CSV/Java Properties/ADE/TOML/
// YAML/... which all mirror it). std::strtod respects LC_NUMERIC: under a
// comma-decimal locale (de_DE, ru_RU, fr_FR, ...) strtod("1.5") stops at the
// dot, so the SAME document typed "1.5" as DOUBLE in the C locale degraded to
// STRING elsewhere - locale-dependent tree drift (probe sw69, wave 72: 39
// drifts on INI/ADE/dotenv/... whose inference calls strtod directly instead
// of the locale-safe from_chars fast path).
// The fix is a drop-in strtod bound to the classic ("C") locale: identical
// grammar (inf/nan/hex-float spellings included), no global setlocale (which
// would race with the library's per-thread parse guarantees), no behavior
// change while the process is already in the C locale.
inline double asvStrtod(const char* s, char** end) {
#if defined(_WIN32)
	// UCRT: _create_locale once; _strtod_l is thread-safe for READONLY locale
	// handles (the CRT docs allow concurrent use of an _locale_t).
	static const _locale_t classic = _create_locale(LC_ALL, "C");
	if (classic) return _strtod_l(s, end, classic);
	return std::strtod(s, end);
#elif defined(__GLIBC__) || defined(__APPLE__) || defined(__BIONIC__)
	static const locale_t classic = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	if (classic) return strtod_l(s, end, classic);
	return std::strtod(s, end);
#else
	// No per-call locale API: fall back to plain strtod. (Toolchains where
	// from_chars(double) exists keep taking the locale-safe path above it -
	// this only covers the fallback branches.)
	return std::strtod(s, end);
#endif
}

// ---------- XML helpers ----------
// XML 1.0's Char production is #x9 | #xA | #xD | [#x20-#xD7FF] | ... : every
// other C0 control byte is excluded, and a character reference may not name
// one either ("the replacement text of an entity must match the production
// Char"). Such a byte therefore has NO legal XML spelling, not even as
// "&#1;". Writing it raw produced a document that no conforming parser
// accepts (and the library's own reader happened to accept it back), so it is
// reported instead - the same "an unencodable value is an error, never a
// non-round-trippable output" rule the dotenv encoder follows.
inline bool xmlCharAllowed(unsigned char c) {
	return c >= 0x20 || c == '\t' || c == '\n' || c == '\r';
}

// Key/name length limit for the XML-family encoders. This header is included
// from core.hpp BEFORE struct asvJSONValue, so asvJSONValue::MAX_STRING_LEN is
// not yet visible here; core.hpp static_asserts that the two constants stay
// equal.
inline constexpr size_t XML_MAX_NAME_LEN = 10u * 1024u * 1024u;

inline std::string xmlInvalidCharMessage(unsigned char c) {
	static const char* HEX = "0123456789ABCDEF";
	std::string msg = "XML: control character U+00";
	msg += HEX[(c >> 4) & 0xF];
	msg += HEX[c & 0xF];
	msg += " cannot be serialized (XML 1.0 has no legal spelling for it)";
	return msg;
}

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
			default:
				if (!xmlCharAllowed(static_cast<unsigned char>(c)))
					throw asvJSONError(xmlInvalidCharMessage(static_cast<unsigned char>(c)));
				out += c;
		}
	}
	return out;
}

// Escape XML character data (the &, <, > set - the quotes stay literal in text
// content). Same control-character rule as xmlEscapeContent.
inline void xmlEscapeTextInto(std::string& out, std::string_view s) {
	for (char c : s) {
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			default:
				if (!xmlCharAllowed(static_cast<unsigned char>(c)))
					throw asvJSONError(xmlInvalidCharMessage(static_cast<unsigned char>(c)));
				out += c;
		}
	}
}

inline std::string xmlSanitizeElementName(std::string_view key) {
	// The name is written as an element (or attribute) name and comes back as
	// an object key; fromXML/fromSVG reject a name past MAX_STRING_LEN, so the
	// encoder must reject the same key (an unencodable key is an error, never
	// non-round-trippable output). This constant mirrors asvJSONValue::
	// MAX_STRING_LEN -- core.hpp static_asserts the two stay equal.
	if (key.size() > XML_MAX_NAME_LEN)
		throw asvJSONError("XML: name too long");
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
		asvStrtod(tmp.c_str(), &end);
		if (end == tmp.c_str() + tmp.size()) return true;
	}
	return false;
#endif
}

inline bool yamlNeedsQuotes(std::string_view s) {
	if (s.empty()) return true;
	if (s[0] == ' ' || s[0] == '\t') return true;
	// Control characters (and DEL) are not YAML-printable: a bare scalar with
	// one would be emitted raw and rejected on reparse (block keys survived
	// only because the block path re-escapes them while building the
	// intermediate JSON; flow keys and values did not). Force the quoted form,
	// which yamlDQuote escapes as \xNN.
	for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return true;
	static const char* keywords[] = {"null", "NULL", "Null", "true", "TRUE", "True", "false", "FALSE", "False",
		"yes", "YES", "Yes", "no", "NO", "No", "on", "ON", "On", "off", "OFF", "Off",
		"y", "Y", "n", "N"};
	for (auto kw : keywords) if (s == kw) return true;
	// Strings that would be read back as numbers or special YAML scalars must
	// be quoted so they round-trip as strings ("001", "1.2", ".inf", "~", ...).
	if (yamlLooksNumeric(s)) return true;
	// Document markers must be quoted: a bare "---" / "..." at the start of a
	// line is a stream boundary, not a string (a root STRING with that text
	// decoded as an empty document / null).
	if (s == "---" || s == "...") return true;
	// A lone dash / question mark is a sequence / explicit-key indicator at
	// the start of a line, so it must be quoted (a root STRING "-" was emitted
	// bare and decoded as an empty sequence; "?" as an empty key).
	if (s == "-" || s == "?") return true;
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
			default: {
				// Other C0 controls and DEL are not YAML-printable: emit them
				// as \xNN (yamlUnescapeDouble decodes \xNN) instead of writing
				// a raw byte that the intermediate JSON step rejects.
				unsigned char uc = static_cast<unsigned char>(c);
				if (uc < 0x20 || uc == 0x7F) {
					static const char hex[] = "0123456789ABCDEF";
					out += "\\x";
					out += hex[uc >> 4];
					out += hex[uc & 0x0F];
				} else {
					out += c;
				}
				break;
			}
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
