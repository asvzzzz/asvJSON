#pragma once

#include <cstdint>
#include <cstddef>

namespace asvJSONInternal {

inline bool isValidUTF8(const uint8_t* data, size_t len) noexcept {
	for (size_t i = 0; i < len; i++) {
		if ((data[i] & 0x80) == 0) continue;
		int extra;
		if ((data[i] & 0xE0) == 0xC0) extra = 1;
		else if ((data[i] & 0xF0) == 0xE0) extra = 2;
		else if ((data[i] & 0xF8) == 0xF0) extra = 3;
		else return false;
		if (i + extra >= len) return false;
		for (int j = 1; j <= extra; j++)
			if ((data[i + j] & 0xC0) != 0x80) return false;
		i += extra;
	}
	return true;
}

} // namespace asvJSONInternal
