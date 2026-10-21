#pragma once
// JSON Lines (NDJSON) serialization/parsing for asvJSON++
//
// One JSON value per line. The decoder ALWAYS produces an ARRAY-root, even for
// a single line; the encoder writes one line per array element, or a single
// line for a non-array root.
// Empty lines are skipped. Trailing CR / space / tab on each line is trimmed.
// An invalid line aborts the WHOLE parse with "JSON Lines: invalid JSON near:
// <first 30 bytes of the offending line>" (UTF-8 safe: the prefix is cut at a
// character boundary, never mid-sequence).

#include "../core.hpp"

namespace asvJSONInternal {

inline std::string jsonlinesPrefix(std::string_view s) {
    // Return up to 30 bytes of `s`, but never end in the middle of a UTF-8
    // multi-byte sequence. If byte 30 lands on a lead or continuation byte,
    // back up until an ASCII byte (or empty string).
    if (s.size() <= 30) return std::string(s);
    std::string out(s.data(), 30);
    // Strip any trailing non-ASCII bytes (lead or continuation) so the result
    // is guaranteed to be a valid UTF-8 prefix ending at a character boundary.
    while (!out.empty() && static_cast<uint8_t>(out.back()) >= 0x80)
        out.pop_back();
    return out;
}

inline std::string asvJSON::toJSONLines() const {
	std::string out;
	if (!root) return out;
	if (root->type == asvJSONValue::ARRAY && root->arr) {
		for (const auto& elem : *root->arr) {
			elem->serialize(out, allowNaNInfinity);
			out += '\n';
		}
	} else {
		root->serialize(out, allowNaNInfinity);
		out += '\n';
	}
	return out;
}

inline bool asvJSON::fromJSONLines(std::string_view input) {
	root = nullptr;
	dirty_ = false;
	formatType = FormatType::Unknown;
	try {
		auto arr = asvJSONValue::makeArray();
		if (!arr) { lastError = "out of memory"; return false; }
		size_t pos = 0;
		while (pos < input.size()) {
			size_t end = input.find('\n', pos);
			if (end == std::string_view::npos) end = input.size();
			std::string_view line = input.substr(pos, end - pos);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
				line.remove_suffix(1);
			if (!line.empty()) {
				asvJSON tmp;
				if (!tmp.parse(line))
					throw asvJSONError("JSON Lines: invalid JSON near: " + jsonlinesPrefix(line));
				arr->arr->push_back(tmp.releaseRoot());
			}
			pos = end + 1;
		}
		root = std::move(arr);
		formatType = FormatType::JSONLines;
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	} catch (const std::exception& e) { // bad_alloc / length_error etc. must not escape: the public contract is bool + lastError, like the other from* parsers.
		lastError = e.what();
		root = nullptr;
		return false;
	}
}

} // namespace asvJSONInternal
