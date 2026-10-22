#pragma once
// JSON Lines (NDJSON) serialization/parsing for asvJSON++
//
// One JSON value per line. The decoder ALWAYS produces an ARRAY-root, even for
// a single line; the encoder writes one line per array element, or a single
// line for a non-array root.
// Empty lines are skipped. Trailing CR / space / tab on each line is trimmed.
// An invalid line aborts the WHOLE parse with "JSON Lines: invalid JSON at
// line N: <the inner parser's diagnostic> near: <first 30 bytes of the
// offending line>" (the prefix is cut at a character boundary, never
// mid-sequence, and it keeps COMPLETE multi-byte characters).

#include "../core.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace asvJSONInternal {

inline std::string jsonlinesPrefix(std::string_view s) {
    // Return up to 30 bytes of `s`, cut at a CHARACTER boundary.
    //
    // The byte right after the prefix must not be a UTF-8 continuation byte
    // (0b10xxxxxx) - otherwise the prefix ends in the middle of a character.
    // The old rule backed up to the last ASCII byte instead, which also threw
    // away COMPLETE characters: a line of CJK characters lost all of them and
    // the message showed an empty prefix, so the user could not see WHICH line
    // broke.
    size_t cut = s.size() < 30 ? s.size() : 30;
    while (cut > 0 && cut < s.size() &&
           (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80)
        cut--;
    // The input may END in the middle of a character (a truncated document):
    // drop an incomplete trailing sequence too, so the prefix is always valid
    // UTF-8.
    while (cut > 0) {
        unsigned char last = static_cast<unsigned char>(s[cut - 1]);
        size_t need = 0;
        if (last < 0x80) break;
        else if ((last & 0xE0) == 0xC0) need = 1;
        else if ((last & 0xF0) == 0xE0) need = 2;
        else if ((last & 0xF8) == 0xF0) need = 3;
        else break;  // not a lead byte - invalid input, leave it as it is
        bool complete = cut > need;
        for (size_t k = 1; complete && k <= need; k++)
            if ((static_cast<unsigned char>(s[cut - 1 - k]) & 0xC0) != 0x80) complete = false;
        if (complete) break;
        cut--;
    }
    return std::string(s.substr(0, cut));
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
		size_t lineNum = 0;
		while (pos < input.size()) {
			size_t end = input.find('\n', pos);
			if (end == std::string_view::npos) end = input.size();
			std::string_view line = input.substr(pos, end - pos);
			lineNum++;
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
				line.remove_suffix(1);
			if (!line.empty()) {
				asvJSON tmp;
				if (!tmp.parse(line)) {
					// The inner parser's own diagnostic ("Expected ':'",
					// "Invalid escape in string", ...) is the useful part - it
					// used to be dropped, so the user only saw "invalid JSON".
					// The line number says WHICH line broke.
					std::string why = tmp.lastError.empty() ? std::string("invalid JSON")
					                                           : tmp.lastError;
					throw asvJSONError("JSON Lines: invalid JSON at line " + std::to_string(lineNum) +
					                   ": " + why + " near: " + jsonlinesPrefix(line));
				}
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
