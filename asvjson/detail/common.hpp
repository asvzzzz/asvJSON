#pragma once

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

struct FormatFrame {
	char type = 'O';
	bool hasVal = false;
	int align = 0;
	bool first = true;
	bool isRoot = false;
	std::string prefix;
};

static std::vector<std::string> splitLines(std::string_view input) {
	std::vector<std::string> lines;
	if (input.empty()) return lines;
	size_t start = 0;
	for (size_t i = 0; i <= input.size(); i++) {
		if (i == input.size() || input[i] == '\n') {
			if (i > start) {
				if (input[i - 1] == '\r') lines.push_back(std::string(input.substr(start, i - start - 1)));
				else lines.push_back(std::string(input.substr(start, i - start)));
			}
			start = i + 1;
		}
	}
	return lines;
}

static int countIndent(const std::string& s) {
	int n = 0;
	for (auto c : s) {
		if (c == ' ') n++;
		else if (c == '\t') n += 2;
		else break;
	}
	return n;
}

static std::string_view stripIndent(const std::string& s) {
	size_t pos = 0;
	while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
	return std::string_view(s).substr(pos);
}

static void closeFrames(std::vector<FormatFrame>& stack, std::string& out, int indent) {
	while (!stack.empty()) {
		auto& top = stack.back();
		if (top.isRoot) break;
		if (indent > static_cast<int>(top.align)) break;
		out += (top.type == 'A') ? ']' : '}';
		stack.pop_back();
	}
}

static void addComma(std::vector<FormatFrame>& stack, std::string& out) {
	if (!stack.empty() && !stack.back().first && stack.back().hasVal) {
		out += ',';
		if (!stack.back().prefix.empty()) out += ' ';
	}
}

} // namespace asvJSONInternal
