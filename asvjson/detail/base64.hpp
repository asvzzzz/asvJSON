#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <atomic>

namespace asvJSONInternal {

inline constexpr char ASVJSON_BASE64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline std::string customBase64Chars;

/**
 * @brief Get global mutex for base64 charset access
 * @return Reference to static mutex
 */
inline std::mutex& base64Mutex() {
	static std::mutex mtx;
	return mtx;
}

/**
 * @brief Get base64 version counter for thread-safe charset changes
 * @return Reference to version counter
 */
inline std::atomic<int>& base64Version() {
	static std::atomic<int> ver{0};
	return ver;
}

/**
 * @brief Get raw mutable decode table (no locking)
 */
inline int8_t* getDecodeTableRaw() {
	static int8_t decodeTable[256];
	return decodeTable;
}

/**
 * @brief Rebuild base64 decode table after charset change
 */
inline void rebuildDecodeTable() {
	int8_t* table = getDecodeTableRaw();
	const char* chars = customBase64Chars.empty() ? ASVJSON_BASE64_CHARS : customBase64Chars.c_str();
	memset(table, -1, 256);
	for (int i = 0; i < 64; i++) table[static_cast<unsigned char>(chars[i])] = static_cast<int8_t>(i);
}

/**
 * @brief Get base64 decode table (built on first call)
 * @return Pointer to 256-byte decode table
 */
inline const int8_t* getDecodeTable() {
	if (base64Version().load(std::memory_order_acquire) > 0) return getDecodeTableRaw();
	static std::atomic<bool> initialized{false};
	if (!initialized.load(std::memory_order_acquire)) {
		std::lock_guard<std::mutex> lock(base64Mutex());
		if (!initialized.load(std::memory_order_relaxed)) {
			rebuildDecodeTable();
			initialized.store(true, std::memory_order_release);
		}
	}
	return getDecodeTableRaw();
}

/**
 * @brief Set custom base64 character set
 * @param chars 64-character string defining the custom charset
 * @note This function is NOT thread-safe - set before any encoding/decoding
 */
inline void setBase64Chars(const std::string& chars) {
	if (chars.size() != 64) return;
	std::lock_guard<std::mutex> lock(base64Mutex());
	customBase64Chars = chars;
	rebuildDecodeTable();
	base64Version().fetch_add(1, std::memory_order_release);
}

/**
 * @brief Get current base64 character set
 * @return Pointer to 64-char C-string
 */
inline const char* getBase64Chars() {
	return customBase64Chars.empty() ? ASVJSON_BASE64_CHARS : customBase64Chars.c_str();
}

inline int decodeBase64Value(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

/**
 * @brief Encode data to base64
 * @param input Data to encode
 * @param len Length of data
 * @return Base64-encoded string
 */
inline std::string encodeBase64(const uint8_t* input, size_t len) {
	static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve(((len + 2) / 3) * 4 + 8);
	const char* table = chars;
	{
		std::lock_guard<std::mutex> lock(base64Mutex());
		if (!customBase64Chars.empty()) table = customBase64Chars.c_str();
	}
	for (size_t i = 0; i < len; i += 3) {
		unsigned int v = static_cast<unsigned char>(input[i]) << 16;
		if (i + 1 < len) v |= static_cast<unsigned char>(input[i + 1]) << 8;
		if (i + 2 < len) v |= static_cast<unsigned char>(input[i + 2]);
		out += table[(v >> 18) & 0x3F];
		out += table[(v >> 12) & 0x3F];
		out += (i + 1 < len) ? table[(v >> 6) & 0x3F] : '=';
		out += (i + 2 < len) ? table[v & 0x3F] : '=';
	}
	return out;
}

/**
 * @brief Fast base64 decode
 * @param str Base64 string
 * @param len Length of string
 * @param error Optional error flag
 * @return Decoded binary data
 */
inline std::vector<uint8_t> decodeBase64Fast(const char* str, size_t len, bool* error = nullptr) {
	if (error) *error = false;
	if (len == 0) return {};
	if (len % 4 != 0) { if (error) *error = true; return {}; }
	const int8_t* table = getDecodeTable();
	size_t padding = 0;
	if (len >= 1 && str[len - 1] == '=') padding++;
	if (len >= 2 && str[len - 2] == '=') padding++;
	size_t outLen = (len / 4) * 3 - padding;
	std::vector<uint8_t> out(outLen);
	for (size_t i = 0, j = 0; i < len; i += 4) {
		int vals[4];
		for (int k = 0; k < 4; k++) {
			vals[k] = (i + k < len) ? table[static_cast<unsigned char>(str[i + k])] : 0;
			if (vals[k] < 0) {
				if (str[i + k] == '=') { vals[k] = 0; continue; }
				if (error) *error = true;
				return {};
			}
		}
		unsigned int v = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6) | vals[3];
		if (j < outLen) out[j++] = static_cast<uint8_t>((v >> 16) & 0xFF);
		if (j < outLen) out[j++] = static_cast<uint8_t>((v >> 8) & 0xFF);
		if (j < outLen) out[j++] = static_cast<uint8_t>(v & 0xFF);
	}
	return out;
}

inline int decodeBase64Char(char c) {
	return getDecodeTable()[static_cast<unsigned char>(c)];
}

} // namespace asvJSONInternal
