#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <ctime>
#include <cstring>

namespace asvJSONInternal {

/**
 * @brief Parse ISO 8601 datetime string
 * @param sv Input string view
 * @param out Output timestamp
 * @param ms_out Optional milliseconds output
 * @return true if parsed successfully
 */
inline bool tryParseDateTime(std::string_view sv, time_t& out, int* ms_out) {
	if (sv.size() < 20) return false;
	if (sv[4] != '-' || sv[7] != '-' || sv[10] != 'T') return false;

	// Parse components with digit validation
	auto parse2 = [](const char* p, int& v) -> bool {
		if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
		v = (p[0] - '0') * 10 + (p[1] - '0');
		return true;
	};

	int year, month, day, hour, min, sec = 0;
	char sign = '+';
	int tzH = 0, tzM = 0;

	if (!parse2(sv.data(), year)) return false;
	year = year * 100 + (sv[2] - '0') * 10 + (sv[3] - '0');
	if (!parse2(sv.data() + 5, month) || month < 1 || month > 12) return false;
	if (!parse2(sv.data() + 8, day) || day < 1 || day > 31) return false;
	if (!parse2(sv.data() + 11, hour) || hour > 23) return false;
	if (sv[13] != ':') return false;
	if (!parse2(sv.data() + 14, min) || min > 59) return false;

	size_t pos = 16;

	// Seconds are optional (some implementations omit them)
	if (pos < sv.size() && sv[pos] == ':') {
		pos++;
		if (pos + 1 < sv.size() && sv[pos] >= '0' && sv[pos] <= '9' && sv[pos + 1] >= '0' && sv[pos + 1] <= '9') {
			sec = (sv[pos] - '0') * 10 + (sv[pos + 1] - '0');
			pos += 2;
			if (sec > 60) return false; // allow leap second :60
		} else {
			return false;
		}
	}

	// Optional milliseconds
	int ms = 0;
	if (pos < sv.size() && sv[pos] == '.') {
		pos++;
		int mult = 100;
		while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9' && mult > 0) {
			ms += (sv[pos] - '0') * mult;
			mult /= 10;
			pos++;
		}
	}

	if (ms_out) *ms_out = ms;

	if (pos >= sv.size()) return false;

	// Timezone
	if (sv[pos] == 'Z') {
		pos++;
	} else if (sv[pos] == '+' || sv[pos] == '-') {
		sign = sv[pos]; pos++;
		if (pos + 1 >= sv.size()) return false;
		int tz_h, tz_m = 0;
		if (!parse2(sv.data() + pos, tz_h)) return false;
		pos += 2;
		if (pos < sv.size() && sv[pos] == ':') pos++;
		if (pos + 1 < sv.size() && sv[pos] >= '0' && sv[pos] <= '9' && sv[pos + 1] >= '0' && sv[pos + 1] <= '9') {
			tz_m = (sv[pos] - '0') * 10 + (sv[pos + 1] - '0');
			pos += 2;
		}
		tzH = tz_h;
		tzM = tz_m;
	} else {
		return false;
	}

	if (pos != sv.size()) return false;

	// Normalize month/day for timegm
	std::tm tm = {};
	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;
	tm.tm_isdst = 0;
	out = asvjson_timegm(&tm);

	if (sign == '+') out -= tzH * 3600 + tzM * 60;
	else if (sign == '-') out += tzH * 3600 + tzM * 60;

	return true;
}

/**
 * @brief Append UTF-8 codepoint to string
 * @param out Output string
 * @param cp Unicode codepoint
 */
inline void appendUtf8Codepoint(std::string& out, unsigned int cp) {
	if (cp < 0x80) {
		out += static_cast<char>(cp);
	} else if (cp < 0x800) {
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x110000) {
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

/**
 * @brief Unescape JSON string with full Unicode support
 * @param raw Raw JSON string (without outer quotes)
 * @param allowNull Whether to allow embedded null characters
 * @return Unescaped string
 */
inline std::string unescapeJsonString(std::string_view raw, bool allowNull) {
	std::string out;
	out.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); i++) {
		if (raw[i] != '\\') { out += raw[i]; continue; }
		if (i + 1 >= raw.size()) break;
		switch (raw[++i]) {
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u': {
				if (i + 4 >= raw.size()) break;
				auto hex = [&](size_t off) -> int {
					char c = raw[i + off];
					if (c >= '0' && c <= '9') return c - '0';
					if (c >= 'a' && c <= 'f') return c - 'a' + 10;
					if (c >= 'A' && c <= 'F') return c - 'A' + 10;
					return -1;
				};
				int h1 = hex(1), h2 = hex(2), h3 = hex(3), h4 = hex(4);
				if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) throw std::runtime_error("Invalid unicode escape in string");
				unsigned int cp = (static_cast<unsigned int>(h1) << 12) | (static_cast<unsigned int>(h2) << 8) | (static_cast<unsigned int>(h3) << 4) | static_cast<unsigned int>(h4);
				i += 4;

				// Handle surrogate pairs
				if (cp >= 0xD800 && cp <= 0xDBFF) {
					if (i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
						int h5 = hex(3), h6 = hex(4), h7 = hex(5), h8 = hex(6);
						if (h5 >= 0 && h6 >= 0 && h7 >= 0 && h8 >= 0) {
							int lo = (h5 << 12) | (h6 << 8) | (h7 << 4) | h8;
							if (lo >= 0xDC00 && lo <= 0xDFFF) {
								cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
								i += 6;
							}
						}
					}
				}
				if (cp >= 0xD800 && cp <= 0xDFFF) throw std::runtime_error("Lone surrogate in string");
				appendUtf8Codepoint(out, cp);
				break;
			}
			default: out += '\\'; out += raw[i]; break;
		}
	}
	return out;
}

} // namespace asvJSONInternal
