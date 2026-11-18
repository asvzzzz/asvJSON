#pragma once
// JSON Lines (NDJSON) serialization/parsing for asvJSON++
//
// One JSON value per line. The decoder ALWAYS produces an ARRAY-root, even for
// a single line; the encoder writes one line per array element, or a single
// line for a non-array root.
// Empty lines are skipped. Trailing CR / space / tab on each line is trimmed.
// allowNaNInfinity is honored by BOTH sides: the encoder writes the JSON5-style
// literals and the per-line parser accepts them (the flag is copied to the
// per-line asvJSON instance).
// An invalid line aborts the WHOLE parse with "JSON Lines: invalid JSON at
// line N: <the inner parser's diagnostic> near: <first 30 bytes of the
// offending line>" (the prefix is cut at a character boundary, never
// mid-sequence, and it keeps COMPLETE multi-byte characters).

#include "../core.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace asvJSONInternal {

// Input size limit — symmetric with other from* parsers.
inline constexpr size_t MAX_JSONLINES_INPUT = 32u * 1024u * 1024u;

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
    // find the start of the last character and drop it unless it is complete,
    // so the prefix is always valid UTF-8. The last byte may be a LEAD byte
    // ("hi" + E4) or a CONTINUATION of a character that started earlier and was
    // cut ("hi" + E4 BD) - the second case needs the walk back to the lead.
    while (cut > 0) {
        unsigned char last = static_cast<unsigned char>(s[cut - 1]);
        if (last < 0x80) break;  // ASCII - a character boundary
        size_t start = cut - 1;
        while (start > 0 && (static_cast<uint8_t>(s[start]) & 0xC0) == 0x80)
            start--;  // back to the lead byte
        unsigned char lead = static_cast<unsigned char>(s[start]);
        size_t need = 0;
        if (lead < 0x80) need = 1;
        else if ((lead & 0xE0) == 0xC0) need = 2;
        else if ((lead & 0xF0) == 0xE0) need = 3;
        else if ((lead & 0xF8) == 0xF0) need = 4;
        else {
            // A continuation byte without a lead: drop it and re-check.
            cut = start;
            continue;
        }
        if (start + need == cut) break;  // the last character is complete
        cut = start;                     // truncated - drop the whole character
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
	lastError.clear();
	root = nullptr;
	dirty_ = false;
	formatType = FormatType::Unknown;
	if (input.size() > MAX_JSONLINES_INPUT) { lastError = "JSON Lines input exceeds 32 MB limit"; return false; }
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
				// The encoder writes NaN/Infinity when the flag is set, so the
				// per-line parse must honor it too: enc -> dec of the same
				// document has to agree on the flag (the fromYAML/fromGOON
				// save/restore pattern - here tmp is fresh, so a plain copy).
				tmp.allowNaNInfinity = allowNaNInfinity;
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
				auto parsed = tmp.releaseRoot();
				if (!parsed) throw asvJSONError("JSON Lines: out of memory");
				if (!asvJSONValue::checkArraySize(arr->arr->size() + 1))
					throw asvJSONError("JSON Lines: too many lines");
				arr->arr->push_back(std::move(parsed));
			}
			pos = end + 1;
		}
		root = std::move(arr);
		formatType = FormatType::JSONLines;
		return true;
	} catch (const asvJSONError& e) {
		dirty_ = false;
		lastError = e.what();
		root = nullptr;
		return false;
	} catch (const std::exception& e) { // bad_alloc / length_error etc. must not escape: the public contract is bool + lastError, like the other from* parsers.
		dirty_ = false;
		lastError = e.what();
		root = nullptr;
		return false;
	}
}

} // namespace asvJSONInternal
