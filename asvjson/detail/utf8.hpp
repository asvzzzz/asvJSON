#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace asvJSONInternal {

// Renders a byte for an error message: an ASCII character as itself, a byte
// >= 0x80 (or a NUL) as a hex escape ("0xE4"). A single raw non-ASCII byte
// would be an invalid UTF-8 sequence in the message (the lead byte of a
// multi-byte character alone), and a raw NUL would truncate it for a
// C-string consumer.
inline std::string utf8SafeByteName(char c) {
	unsigned char uc = static_cast<unsigned char>(c);
	if (uc >= 0x80 || uc == 0) {
		static const char kHex[] = "0123456789ABCDEF";
		std::string s = "0x";
		s += kHex[(uc >> 4) & 0xF];
		s += kHex[uc & 0xF];
		return s;
	}
	return std::string(1, c);
}

inline bool isValidUTF8(const uint8_t* data, size_t len) noexcept {
	if (!data) return len == 0;
	for (size_t i = 0; i < len; i++) {
		uint8_t b = data[i];
		if (b < 0x80) continue;
		if (b >= 0xC2 && b <= 0xDF) {
			// 2-byte sequence: C2..DF (rejects overlong C0/C1)
			if (i + 1 >= len) return false;
			if ((data[i + 1] & 0xC0) != 0x80) return false;
			i += 1;
		} else if (b >= 0xE0 && b <= 0xEF) {
			// 3-byte sequence: E0..EF (E0 rejects overlong, ED rejects surrogates)
			if (i + 2 >= len) return false;
			uint8_t b1 = data[i + 1], b2 = data[i + 2];
			if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
			if (b == 0xE0 && b1 < 0xA0) return false;       // overlong
			if (b == 0xED && b1 >= 0xA0) return false;       // U+D800..DFFF surrogates
			i += 2;
		} else if (b >= 0xF0 && b <= 0xF4) {
			// 4-byte sequence: F0..F4 (F0 rejects overlong, F4 caps at U+10FFFF)
			if (i + 3 >= len) return false;
			uint8_t b1 = data[i + 1], b2 = data[i + 2], b3 = data[i + 3];
			if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
			if (b == 0xF0 && b1 < 0x90) return false;       // overlong
			if (b == 0xF4 && b1 > 0x8F) return false;       // > U+10FFFF
			i += 3;
		} else {
			return false; // 0x80..0xC1 (continuation/overlong leads), 0xF5..0xFF
		}
	}
	return true;
}

} // namespace asvJSONInternal
