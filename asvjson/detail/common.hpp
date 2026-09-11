#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <limits>

// Shared internal utilities for asvJSON++
// StringViewLess, SafeHash, keyForLookup, mapFind, mapCount
// FormatFrame, splitLines, countIndent, stripIndent, closeFrames, addComma

namespace asvJSONInternal {

struct StringViewLess {
	using is_transparent = void;
	bool operator()(std::string_view a, std::string_view b) const {
		return a < b;
	}
};

struct SafeHash {
	using is_transparent = void;
	using transparent_key_equal = std::equal_to<>;
	size_t operator()(std::string_view s) const noexcept {
		size_t h = 2166136261u;
		for (auto c : s) {
			h ^= static_cast<size_t>(static_cast<unsigned char>(c));
			h *= 16777619u;
		}
		return h;
	}
};

} // namespace asvJSONInternal

namespace asvJSONInternal {

#ifdef ASVJSON_USE_ORDERED_MAP
inline constexpr std::string_view keyForLookup(std::string_view sv) { return sv; }
inline const std::string& keyForLookup(const std::string& s) { return s; }
inline constexpr const char* keyForLookup(const char* s) { return s; }
inline std::string_view keyForLookup(const char* data, size_t len) { return std::string_view(data, len); }

template<typename Map, typename Key>
inline auto mapFind(Map& m, const Key& k) { return m.find(k); }
template<typename Map, typename Key>
inline auto mapCount(Map& m, const Key& k) { return m.count(k); }
#else
#if defined(__cpp_lib_generic_unordered_lookup) && __cpp_lib_generic_unordered_lookup >= 201811L
template<typename Map>
inline auto mapFind(Map& m, std::string_view k) { return m.find(k); }
template<typename Map>
inline auto mapCount(Map& m, std::string_view k) { return m.count(k); }
#else
template<typename Map>
inline auto mapFind(Map& m, std::string_view k) { return m.find(std::string(k)); }
template<typename Map>
inline auto mapCount(Map& m, std::string_view k) { return m.count(std::string(k)); }
#endif
#endif

} // namespace asvJSONInternal

namespace asvJSONInternal {

// YAML serialization options (passed to toYAML()).
// Defaults reproduce the classic toYAML()/toYAML(true) block-style output.
struct YamlWriteOptions {
	enum class Style : int { Block = 0, Auto = 1, Flow = 2 };
	// Block: pure indentation-based output (matches toYAML(true)).
	// Auto:  flow-style with automatic block fallback when a value exceeds
	//        flowMaxLen or cannot be represented inline (matches toYAML(false)).
	// Flow:  flow-style everywhere, no length-based fallback; multiline
	//        strings are inlined as double-quoted scalars.
	Style style = Style::Block;
	int indentWidth = 2;     // spaces per indentation level
	int flowMaxLen = 80;     // Auto fallback threshold (characters)
	bool alwaysQuoteStrings = false; // quote every string scalar
	bool alwaysQuoteKeys = false;    // quote every mapping key
	bool doubleQuotes = false;       // prefer "..." over '...' when quoting
};

// GOON serialization options.
struct GoonWriteOptions {
	enum class Mode : int { LLM = 0, Compact = 1, Balanced = 2 };
	// LLM: verbose, including all comments and extra whitespace for readability.
	// Compact: minimal whitespace and no comments (matches toGOON(true)).
	// Balanced: hybrid mode with indentation and standard spacing.
	Mode mode = Mode::LLM;
	int indentWidth = 2;     // spaces per indentation level
	bool alwaysQuoteStrings = false; // quote every string scalar
	bool alwaysQuoteKeys = false;    // quote every mapping key
};

struct FormatFrame {
	char type = 'O';
	bool hasVal = false;
	int align = 0;
	bool first = true;
	bool isRoot = false;
	std::string prefix;
	int childIndent = -1; // indentation of the first entry; -1 = unset
};

inline std::vector<std::string> splitLines(std::string_view input) {
	std::vector<std::string> lines;
	if (input.empty()) return lines;
	size_t start = 0;
	for (size_t i = 0; i < input.size(); i++) {
		if (input[i] == '\n') {
			if (i > start && input[i - 1] == '\r')
				lines.push_back(std::string(input.substr(start, i - start - 1)));
			else
				lines.push_back(std::string(input.substr(start, i - start)));
			start = i + 1;
		}
	}
	// A trailing '\n' terminates the final line; it does NOT open an empty
	// phantom line at EOF. (Earlier versions emitted that phantom, which forced
	// every consumer to compensate -- e.g. block-scalar chomp counted a spurious
	// trailing '\n'.)
	if (start < input.size()) lines.push_back(std::string(input.substr(start)));
	return lines;
}

inline int countIndent(const std::string& s) {
	size_t n = 0;
	for (auto c : s) {
		if (c == ' ') n++;
		else if (c == '\t') n += 2;
		else break;
	}
	return static_cast<int>(n < static_cast<size_t>(std::numeric_limits<int>::max()) ? n : static_cast<size_t>(std::numeric_limits<int>::max()));
}

inline std::string_view stripIndent(const std::string& s) {
	size_t pos = 0;
	while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
	return std::string_view(s).substr(pos);
}

inline void closeFrames(std::vector<FormatFrame>& stack, std::string& out, int indent) {
	while (!stack.empty()) {
		auto& top = stack.back();
		if (top.isRoot) break;
		if (indent > static_cast<int>(top.align)) break;
		out += (top.type == 'A') ? ']' : '}';
		stack.pop_back();
	}
}

inline void addComma(std::vector<FormatFrame>& stack, std::string& out) {
	if (!stack.empty() && !stack.back().first && stack.back().hasVal) {
		out += ',';
		if (!stack.back().prefix.empty()) out += ' ';
	}
}

} // namespace asvJSONInternal
