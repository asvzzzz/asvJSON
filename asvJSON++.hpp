#pragma once
// asvJSON++ - C++17 JSON library
// 
// Configuration:
//   - Define ASVJSON_USE_ORDERED_MAP before including header for:
//     * Deterministic key order in serialized output (std::map)
//     * Transparent lookup with std::string_view, const char*, std::string
//   - Default (undefined): std::unordered_map for O(1) lookups
//     * string_view keys are converted to std::string for lookup
// 
// 
// Features:
//   - JSON parsing/serialization with comments support
//   - BSON binary format parsing/serialization
//   - MessagePack binary format parsing/serialization
//   - XML/YAML/CSV serialization
//   - JSON Pointer (RFC 6901)
//   - JSON Patch (RFC 6902) and JSON Merge Patch (RFC 7396)
//   - Base64 binary data encoding
//   - DateTime with milliseconds
//   - Nested key access (e.g., "user.address.city")
// 
// Architecture:
//   - Header-only library, C++17 standard
//   - unique_ptr for internal containers (obj/arr)
//   - Factory methods return raw pointers - caller manages deletion
//   - Big-endian byte order for MessagePack float/double (MessagePack spec)
// 
// Example:
//   asvJSON json;
//   json.parse("{\"name\": \"John\"}");
//   std::cout << json.getString("name") << std::endl;
//   // Output: John
//
// Important: string_view lifetime
//   - getStringView() returns string_view valid only until next parse() or destruction
//   - This is zero-copy optimization - data points to internal buffer
//   - Do NOT store returned string_view for long-term use
//
// Security features:
//   - OOB protection in string parsing
//   - DateTime digit validation
//   - BSON array index limit (10M)
//   - Duplicate key handling
//   - Move constructor nulling
#ifndef ASVJSON_H
#define ASVJSON_H

#if __cplusplus < 201703L
#error "asvJSON++ requires C++17 or later"
#endif

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <utility>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <cctype>
#include <ctime>
#include <cmath>
#include <limits>
#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <charconv>

#ifdef _WIN32
inline void asvjson_gmtime(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); gmtime_s(tm, &tt); }
inline void asvjson_localtime(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); localtime_s(tm, &tt); }
#define asvjson_timegm _mkgmtime
#else
inline void asvjson_gmtime(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); gmtime_r(&tt, tm); }
inline void asvjson_localtime(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); localtime_r(&tt, tm); }
#define asvjson_timegm timegm
#endif

// Object map type - default uses unordered_map for O(1) lookups
// Define ASVJSON_USE_ORDERED_MAP before including header for std::map (deterministic output)
namespace asvJSONInternal {
struct StringViewLess {
	using is_transparent = void;
	bool operator()(std::string_view a, std::string_view b) const noexcept { return a < b; }
	bool operator()(const std::string& a, const std::string& b) const noexcept { return a < b; }
	bool operator()(const std::string& a, std::string_view b) const noexcept { return std::string_view(a) < b; }
	bool operator()(std::string_view a, const std::string& b) const noexcept { return a < std::string_view(b); }
	bool operator()(const char* a, std::string_view b) const noexcept { return std::string_view(a) < b; }
	bool operator()(std::string_view a, const char* b) const noexcept { return a < std::string_view(b); }
	bool operator()(const std::string& a, const char* b) const noexcept { return std::string_view(a) < b; }
	bool operator()(const char* a, const std::string& b) const noexcept { return std::string_view(a) < b; }
	bool operator()(const char* a, const char* b) const noexcept { return std::string_view(a) < std::string_view(b); }
};
struct SafeHash {
	using is_transparent = void;
	size_t operator()(std::string_view sv) const noexcept {
		size_t h = 14695981018646697469ull;
		for (char c : sv) {
			h ^= static_cast<unsigned char>(c);
			h *= 1099511628211ull;
		}
		return h;
	}
	size_t operator()(const std::string& s) const noexcept { return operator()(std::string_view(s)); }
	size_t operator()(const char* s) const noexcept { return s ? operator()(std::string_view(s)) : 0; }
};
} // namespace asvJSONInternal

// Heterogeneous lookup helpers for unordered_map.
// map_find/map_count pass string_view directly on modern compilers (no allocation).
// On GCC < 9 (no P0919R3), a temporary string is needed for the find call.
#ifdef ASVJSON_USE_ORDERED_MAP
inline constexpr std::string_view asvjson_key(std::string_view sv) { return sv; }
inline const std::string& asvjson_key(const std::string& s) { return s; }
inline constexpr const char* asvjson_key(const char* s) { return s; }
inline std::string_view asvjson_key(const char* data, size_t len) { return std::string_view(data, len); }
template<typename Map, typename Key>
inline auto map_find(Map& m, const Key& k) { return m.find(k); }
template<typename Map, typename Key>
inline auto map_count(Map& m, const Key& k) { return m.count(k); }
#else
#if defined(__cpp_lib_generic_unordered_lookup) && __cpp_lib_generic_unordered_lookup >= 201811L
template<typename Map>
inline auto map_find(Map& m, std::string_view k) { return m.find(k); }
template<typename Map>
inline auto map_count(Map& m, std::string_view k) { return m.count(k); }
#else
template<typename Map>
inline auto map_find(Map& m, std::string_view k) { return m.find(std::string(k)); }
template<typename Map>
inline auto map_count(Map& m, std::string_view k) { return m.count(std::string(k)); }
#endif
#endif

class asvJSONError : public std::runtime_error {
public:
	explicit asvJSONError(const std::string& msg) : std::runtime_error(msg) {}
	explicit asvJSONError(const char* msg) : std::runtime_error(msg) {}
};

struct asvJSONValue {
	static constexpr size_t MAX_NESTING_DEPTH = 48;
	static constexpr size_t MAX_STRING_LEN = 10 * 1024 * 1024;
	static constexpr size_t MAX_ARRAY_SIZE = 1000000;
	static constexpr size_t MAX_OBJECT_SIZE = 1000000;

	enum Type { NULL_VAL, STRING, OBJECT, ARRAY, INT, BOOL_VAL, DOUBLE, DATETIME, BINARY, OBJECTID, REGEX, TIMESTAMP, EXTENSION };
	Type type = NULL_VAL;

	int8_t ext_type = 0;

#ifdef ASVJSON_USE_ORDERED_MAP
	using ObjectMap = std::map<std::string, std::unique_ptr<asvJSONValue>, asvJSONInternal::StringViewLess>;
#else
    using ObjectMap = std::unordered_map<std::string, std::unique_ptr<asvJSONValue>, asvJSONInternal::SafeHash, std::equal_to<>>;
#endif
	std::unique_ptr<ObjectMap> obj;
	std::unique_ptr<std::vector<std::unique_ptr<asvJSONValue>>> arr;

	std::string str_data;
	std::vector<uint8_t> bin_data;
	int64_t num = 0;
	bool flag = false;
	bool is_float32 = false;
	double dbl = 0;
	int64_t timestamp = 0;
	int datetime_ms = 0;

	asvJSONValue() : type(NULL_VAL), ext_type(0), obj(), arr(), num(0), flag(false), is_float32(false), dbl(0), timestamp(0), datetime_ms(0) {}
	~asvJSONValue() { destroy(); }

	asvJSONValue(const asvJSONValue&) = delete;
	asvJSONValue& operator=(const asvJSONValue&) = delete;

	asvJSONValue(asvJSONValue&& other) noexcept : type(NULL_VAL) {
		type = other.type;
		str_data = std::move(other.str_data);
		obj = std::move(other.obj);
		arr = std::move(other.arr);
		num = other.num;
		flag = other.flag;
		dbl = other.dbl;
		timestamp = other.timestamp;
		bin_data = std::move(other.bin_data);
		datetime_ms = other.datetime_ms;
		is_float32 = other.is_float32;
		ext_type = other.ext_type;

		other.type = NULL_VAL;
		other.datetime_ms = 0;
		other.is_float32 = false;
		other.ext_type = 0;
	}

	asvJSONValue& operator=(asvJSONValue&& other) noexcept {
		if (this != &other) {
			destroy();
			type = other.type;
			str_data = std::move(other.str_data);
			obj = std::move(other.obj);
			arr = std::move(other.arr);
			num = other.num;
			flag = other.flag;
			dbl = other.dbl;
			timestamp = other.timestamp;
			bin_data = std::move(other.bin_data);
			datetime_ms = other.datetime_ms;
			is_float32 = other.is_float32;
			ext_type = other.ext_type;

			other.type = NULL_VAL;
			other.datetime_ms = 0;
			other.is_float32 = false;
			other.ext_type = 0;
		}
		return *this;
	}

	void destroy() {
		str_data.clear();
		bin_data.clear();
		if (type == OBJECT) {
			obj.reset();
		} else if (type == ARRAY) {
			arr.reset();
		}
		type = NULL_VAL;
		datetime_ms = 0;
	}

	/**
	 * @brief Create a string value (copies data)
	 * @param s Pointer to string data
	 * @param len Length of string
	 * @return New string value or nullptr on error
	 */
	[[nodiscard]] static asvJSONValue* makeString(const char* s, size_t len) {
		if (len > MAX_STRING_LEN) return nullptr;
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = STRING;
		try { v->str_data.assign(s, len); } catch (...) { delete v; return nullptr; }
		return v;
	}

	/**
	 * @brief Create a string value taking ownership of allocated memory
	 * @param s Allocated string (caller retains ownership until this call)
	 * @param len Length of string
	 * @return New string value or nullptr on error
	 */
	[[nodiscard]] static asvJSONValue* makeStringOwned(char* s, size_t len) {
		if (!s || len > MAX_STRING_LEN) { delete[] s; return nullptr; }
		std::unique_ptr<char[]> guard(s);
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = STRING;
		try { v->str_data.assign(guard.get(), len); } catch (...) { delete v; return nullptr; }
		return v;
	}

	[[nodiscard]] static asvJSONValue* makeStringView(std::string_view sv) {
		return makeString(sv.data(), sv.size());
	}

	/**
	 * @brief Create an empty object value
	 * @return New object value or nullptr on allocation failure
	 */
	[[nodiscard]] static asvJSONValue* makeObject() {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = OBJECT;
		try {
			v->obj.reset(new ObjectMap());
		} catch (...) {
			delete v;
			return nullptr;
		}
		return v;
	}

	/**
	 * @brief Create an empty array value
	 * @return New array value or nullptr on allocation failure
	 */
	[[nodiscard]] static asvJSONValue* makeArray() {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = ARRAY;
		try {
			v->arr.reset(new std::vector<std::unique_ptr<asvJSONValue>>());
		} catch (...) {
			delete v;
			return nullptr;
		}
		return v;
	}

	/**
	 * @brief Create an integer value
	 * @param n Integer value
	 * @return New integer value or nullptr on allocation failure
	 */
	[[nodiscard]] static asvJSONValue* makeInt(int64_t n) {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = INT;
		v->num = n;
		v->is_float32 = false;
		v->ext_type = 0;
		return v;
	}

	/**
	 * @brief Create a boolean value
	 * @param b Boolean value
	 * @return New boolean value or nullptr on allocation failure
	 */
	[[nodiscard]] static asvJSONValue* makeBool(bool b) {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = BOOL_VAL;
		v->flag = b;
		v->is_float32 = false;
		v->ext_type = 0;
		return v;
	}

	/**
	 * @brief Create a double-precision floating point value
	 * @param d Double value
	 * @return New double value or nullptr on allocation failure
	 */
	[[nodiscard]] static asvJSONValue* makeDouble(double d) {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = DOUBLE;
		v->dbl = d;
		v->is_float32 = false;
		return v;
	}

	/**
	 * @brief Create a null value
	 * @return New null value or nullptr on allocation failure
	 */
	[[nodiscard]] static asvJSONValue* makeNull() {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = NULL_VAL;
		return v;
	}

	/**
	 * @brief Create a datetime value with optional milliseconds
	 * @param ts Unix timestamp (seconds since epoch)
	 * @param ms Milliseconds (0-999, default: 0)
	 * @return New datetime value or nullptr on allocation failure
	 */
	[[nodiscard]] static asvJSONValue* makeDateTime(time_t ts, int ms = 0) {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = DATETIME;
		v->timestamp = ts + (ms / 1000);
		v->datetime_ms = static_cast<int>(ms % 1000);
		if (v->datetime_ms < 0) {
			v->datetime_ms += 1000;
			v->timestamp--;
		}
		return v;
	}

	/**
	 * @brief Create a binary data value
	 * @param data Pointer to binary data (copied)
	 * @param len Length of binary data
	 * @return New binary value or nullptr if too long
	 */
	[[nodiscard]] static asvJSONValue* makeBinary(const uint8_t* data, size_t len) {
		if (len > MAX_STRING_LEN || (len > 0 && data == nullptr)) return nullptr;
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = BINARY;
		try {
			if (len > 0) v->bin_data.assign(data, data + len);
		} catch (...) { delete v; return nullptr; }
		return v;
	}

	/**
	 * @brief Create an extension type value (MessagePack fixext/EXT)
	 * @param extType Extension type identifier (e.g., 0xD3 for timestamp)
	 * @param data Pointer to extension data (copied)
	 * @param len Length of extension data
	 * @return New extension value or nullptr on error
	 */
	[[nodiscard]] static asvJSONValue* makeExtension(int8_t extType, const uint8_t* data, size_t len) {
		if (!data && len > 0) return nullptr;
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = EXTENSION;
		v->ext_type = extType;
		try {
			if (len > 0) v->bin_data.assign(data, data + len);
		} catch (...) { delete v; return nullptr; }
		return v;
	}

	/**
	 * @brief Create an ObjectId value (12-byte BSON ObjectId)
	 * @param oid Pointer to 12-byte ObjectId data (copied)
	 * @return New ObjectId value or nullptr on error
	 */
	[[nodiscard]] static asvJSONValue* makeObjectId(std::string_view oid) {
		if (oid.size() != 12) return nullptr;
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = OBJECTID;
		try { v->str_data.assign(oid.data(), oid.size()); } catch (...) { delete v; return nullptr; }
		return v;
	}

	[[nodiscard]] static asvJSONValue* makeTimestamp(int64_t ts) {
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = TIMESTAMP;
		v->num = ts;
		return v;
	}

	/**
	 * @brief Create a regex value (BSON regex type)
	 * @param pattern Regex pattern string
	 * @param options Regex options (e.g., "i" for case-insensitive)
	 * @return New regex value or nullptr on error
	 */
	[[nodiscard]] static asvJSONValue* makeRegex(const char* pattern, const char* options) {
		if (!pattern) return nullptr;
		size_t patLen = strlen(pattern);
		if (patLen == 0) return nullptr;
		size_t optLen = options ? strlen(options) : 0;
		if (patLen > MAX_STRING_LEN || optLen > MAX_STRING_LEN || patLen + optLen > MAX_STRING_LEN - 2) return nullptr;
		auto* v = new(std::nothrow) asvJSONValue();
		if (!v) return nullptr;
		v->type = REGEX;
		try {
			v->str_data.reserve(patLen + optLen + 1);
			v->str_data.append(pattern, patLen);
			v->str_data.push_back('|');
			if (optLen > 0) v->str_data.append(options, optLen);
		} catch (...) { delete v; return nullptr; }
		return v;
	}

	// Using string_view for zero-copy key lookup
	/**
	 * @brief Get child value by key (flat lookup, no dot-path support)
	 * @param key Object key (exact match, dots are literal)
	 * @return Pointer to value or nullptr if not found
	 */
	[[nodiscard]] asvJSONValue* get(std::string_view key) const {
		if (key.empty() || type != OBJECT || !obj) return nullptr;
		auto it = map_find(*obj, key);
		return (it != obj->end()) ? it->second.get() : nullptr;
	}

	[[nodiscard]] asvJSONValue* get(size_t idx) const {
		if (type != ARRAY || !arr || idx >= arr->size()) return nullptr;
		return (*arr)[idx].get();
	}

	const asvJSONValue* getConst(std::string_view key) const {
		if (key.empty() || type != OBJECT || !obj) return nullptr;
		auto it = map_find(*obj, key);
		return (it != obj->end()) ? it->second.get() : nullptr;
	}

	const asvJSONValue* getConst(size_t idx) const {
		if (type != ARRAY || !arr || idx >= arr->size()) return nullptr;
		return (*arr)[idx].get();
	}

	size_t size() const {
		if (type == ARRAY && arr) return arr->size();
		if (type == OBJECT && obj) return obj->size();
		return 0;
	}

	/**
	 * @brief Check if object contains a key
	 * @param key Key to check
	 * @return true if key exists in object
	 */
	[[nodiscard]] bool hasKey(std::string_view key) const {
		return type == OBJECT && obj && map_count(*obj, key) > 0;
	}


	static constexpr std::string_view typeToString(Type t) noexcept {
		switch (t) {
			case NULL_VAL: return "null";
			case STRING: return "string";
			case OBJECT: return "object";
			case ARRAY: return "array";
			case INT: return "int";
			case BOOL_VAL: return "bool";
			case DOUBLE: return "double";
			case DATETIME: return "datetime";
			case BINARY: return "binary";
			case OBJECTID: return "objectid";
			case REGEX: return "regex";
			case TIMESTAMP: return "timestamp";
			case EXTENSION: return "extension";
			default: return "unknown";
		}
	}

	// Return string_view for zero-copy access
	/**
	 * @brief Get string as string_view (zero-copy, valid until value is modified)
	 * @return string_view of string or empty view if not string type
	 */
	[[nodiscard]] std::string_view getStringView() const noexcept {
		return type == STRING ? std::string_view(str_data) : std::string_view();
	}

	/**
	 * @brief Get raw string pointer (valid only for STRING type)
	 * @return C-string pointer or empty string if not string type
	 */
	const char* getString() const noexcept { return type == STRING ? str_data.c_str() : ""; }
	/**
	 * @brief Get string length
	 * @return Length of string or 0 if not string type
	 */
	size_t getStringLen() const noexcept { return type == STRING ? str_data.size() : 0; }
	/**
	 * @brief Get integer value
	 * @return Integer value or 0 if not int type
	 */
	[[nodiscard]] int64_t getInt() const noexcept { return type == INT ? num : 0; }
	/**
	 * @brief Get double value
	 * @return Double value or 0.0 if not double type
	 */
	[[nodiscard]] double getDouble() const noexcept { return type == DOUBLE ? dbl : 0.0; }
	/**
	 * @brief Get boolean value
	 * @return Boolean value or false if not bool type
	 */
	[[nodiscard]] bool getBool() const noexcept { return type == BOOL_VAL ? flag : false; }
	/**
	 * @brief Get datetime timestamp
	 * @return Unix timestamp or 0 if not datetime type
	 */
	time_t getDateTime() const noexcept { return type == DATETIME ? timestamp : 0; }
	/**
	 * @brief Get datetime milliseconds
	 * @return Milliseconds (0-999) or 0 if not datetime type
	 */
	[[nodiscard]] int getDateTimeMs() const noexcept { return type == DATETIME ? datetime_ms : 0; }
	/**
	 * @brief Get binary data as vector
	 * @return Copy of binary data or empty vector if not binary type
	 */
	std::vector<uint8_t> getBinary() const {
		if ((type != BINARY && type != EXTENSION) || bin_data.empty()) return {};
		return bin_data;
	}

	/**
	 * @brief Serialize value to JSON string (compact)
	 * @param out Output string to append to
	 */
	void serialize(std::string& out, bool allowNaNInfinity = false) const;
	/**
	 * @brief Serialize value to JSON string (pretty printed)
	 * @param out Output string to append to
	 * @param indent Current indentation level
	 */
	void serializePretty(std::string& out, int indent = 0, bool allowNaNInfinity = false) const;
	/**
	 * @brief Serialize value to MessagePack binary format
	 * @param out Output vector to append to
	 */
	void toMessagePack(std::vector<uint8_t>& out) const;
	/**
	 * @brief Serialize value to BSON binary format
	 * @param out Output vector to append to
	 */
	void toBSON(std::vector<uint8_t>& out) const;
	/**
	 * @brief Serialize value to XML string
	 * @param out Output string to append to
	 */
	void toXML(std::string& out) const;
	/**
	 * @brief Serialize value to XML with element name and indentation
	 * @param out Output string to append to
	 * @param name XML element name
	 * @param indent Current indentation level
	 */
	void toXML(std::string& out, const std::string& name, int indent) const;
	/**
	 * @brief Serialize value to YAML string
	 * @param out Output string to append to
	 */
	void toYAML(std::string& out) const;
	/**
	 * @brief Serialize value to YAML with indentation and context
	 * @param out Output string to append to
	 * @param indent Current indentation level
	 * @param key Object key (empty if not an object value)
	 * @param isArrayItem true if this is an array element
	 */
	void toYAML(std::string& out, int indent, const std::string& key, bool isArrayItem) const;
	/**
	 * @brief Serialize value to CSV string
	 * @param out Output string to append to
	 */
	void toCSV(std::string& out) const;
};

/**
 * @brief Clone a JSON value (deep copy)
 * @param v Value to clone (can be nullptr)
 * @return New cloned value or nullptr
 */
inline asvJSONValue* cloneValue(const asvJSONValue* v);

// C++17 inline base64 charset
inline constexpr char ASVJSON_BASE64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline std::string asvjson_custom_base64_chars;
/**
 * @brief Get global mutex for base64 charset access
 * @return Reference to static mutex
 */
inline std::mutex& asvjson_base64_mutex() {
	static std::mutex mtx;
	return mtx;
}

/**
 * @brief Version counter for custom charset changes
 * @return Reference to global version counter
 */
inline std::atomic<int>& asvjson_base64_version() {
	static std::atomic<int> version{0};
	return version;
}

/**
 * @brief Get decode table, building per-thread on first access and when charset changes
 * @return Pointer to 256-entry decode table
 */
inline const int8_t* asvjson_get_decode_table() {
	thread_local int8_t table[256];
	thread_local int local_version = -1;
	int global_version = asvjson_base64_version();
	if (local_version != global_version) {
		std::memset(table, -1, sizeof(table));
		std::string charset_copy;
		{
			std::lock_guard<std::mutex> lock(asvjson_base64_mutex());
			charset_copy = asvjson_custom_base64_chars;
		}
		const char* charset = charset_copy.empty() ? ASVJSON_BASE64_CHARS : charset_copy.c_str();
		for (int i = 0; i < 64; i++) {
			table[static_cast<unsigned char>(charset[i])] = static_cast<int8_t>(i);
		}
		local_version = global_version;
	}
	return table;
}

/** @brief Force rebuild of the decode table (call after changing charset) */
inline void asvjson_rebuild_decode_table() {
	++asvjson_base64_version();
}

/**
 * @brief Set custom base64 charset
 * @param chars 64-character string
 */
inline void setBase64Chars(const std::string& chars) {
	if (chars.length() >= 64) {
		std::lock_guard<std::mutex> lock(asvjson_base64_mutex());
		asvjson_custom_base64_chars = chars.length() > 64 ? chars.substr(0, 64) : chars;
		asvjson_rebuild_decode_table();
	}
}

/**
 * @brief Get current base64 charset
 * @return Charset string (64 chars)
 */
inline const char* getBase64Chars() {
	asvjson_get_decode_table();
	std::lock_guard<std::mutex> lock(asvjson_base64_mutex());
	return asvjson_custom_base64_chars.empty() ? ASVJSON_BASE64_CHARS : asvjson_custom_base64_chars.c_str();
}

/**
 * @brief Check if data is valid UTF-8
 * @param data Pointer to data
 * @param len Length of data
 * @return true if valid UTF-8
 */
inline bool isValidUTF8(const uint8_t* data, size_t len) noexcept {
	if (!data || len == 0) return true;
	const uint8_t* end = data + len;
	const uint8_t* p = data;
	while (p < end) {
		uint8_t c = *p;
		if (c <= 0x7F) { ++p; continue; }
		if (c < 0xC2) return false; // 0x80-0xC1: invalid
		if (c < 0xE0) { // 2-byte
			if (p + 1 >= end || (p[1] & 0xC0) != 0x80) return false;
			p += 2;
		} else if (c < 0xF0) { // 3-byte
			if (p + 2 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
			uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
			p += 3;
		} else if (c < 0xF5) { // 4-byte
			if (p + 3 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
			uint32_t cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
			if (cp < 0x10000 || cp > 0x10FFFF) return false;
			p += 4;
		} else {
			return false; // 0xF5-0xFF: invalid
		}
	}
	return true;
}

/**
 * @brief Decode base64 char to value (standard RFC 4648 table)
 * @param c Character to decode
 * @return Value 0-63 or -1
 */
[[maybe_unused]] inline int base64_decode_value_char(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

/**
 * @brief Decode base64 char to value (custom charset)
 * @param c Character to decode
 * @return Value 0-63 or -1
 */
inline int base64_decode_value_custom(char c) {
	return asvjson_get_decode_table()[static_cast<unsigned char>(c)];
}

/**
 * @brief Encode bytes to base64 string
 * @param input Pointer to data
 * @param len Length of data
 * @return Base64-encoded string
 */
inline std::string base64_encode(const uint8_t* input, size_t len) {
	if (!input || len > SIZE_MAX / 3) return "";
	const char* chars = getBase64Chars();
	std::string result;
	result.reserve((len + 2) / 3 * 4);
	for (size_t i = 0; i < len; i += 3) {
		int b0 = input[i];
		int b1 = i + 1 < len ? input[i + 1] : 0;
		int b2 = i + 2 < len ? input[i + 2] : 0;
		result.push_back(chars[(b0 >> 2)]);
		result.push_back(chars[((b0 & 0x3) << 4) | (b1 >> 4)]);
		result.push_back(i + 1 < len ? chars[((b1 & 0xF) << 2) | (b2 >> 6)] : '=');
		result.push_back(i + 2 < len ? chars[b2 & 0x3F] : '=');
	}
	return result;
}

/**
 * @brief Decode base64 string to bytes (fast)
 * @param str Base64 string
 * @param len Length of string
 * @param error Error flag output
 * @return Decoded bytes or empty
 */
inline std::vector<uint8_t> base64_decode_fast(const char* str, size_t len, bool* error = nullptr) {
	if (error) *error = false;
	if (!str || len == 0) return {};
	asvjson_get_decode_table();
	size_t realLen = len;
	while (realLen > 0 && str[realLen - 1] == '=') {
		realLen--;
	}
	if (realLen < 2 || realLen > (SIZE_MAX / 4) * 3) {
		if (error) *error = true;
		return {};
	}
	std::vector<uint8_t> result;
	result.reserve(realLen / 4 * 3 + 3);
	int values[4] = {-1, -1, -1, -1};
	size_t i = 0;
	while (i < realLen) {
		values[0] = base64_decode_value_custom(str[i]);
		if (values[0] < 0) { if (error) *error = true; return {}; }
		i++;
		if (i >= realLen || str[i] == '=') {
			values[1] = -1;
		} else {
			values[1] = base64_decode_value_custom(str[i]);
			i++;
		}
		if (i >= realLen || str[i] == '=') {
			values[2] = -1;
		} else {
			values[2] = base64_decode_value_custom(str[i]);
			i++;
		}
		if (i >= realLen || str[i] == '=') {
			values[3] = -1;
		} else {
			values[3] = base64_decode_value_custom(str[i]);
			i++;
		}
		result.push_back((values[0] << 2) | ((values[1] >= 0) ? (values[1] >> 4) : 0));
		if (values[2] >= 0 && values[1] >= 0) {
			result.push_back(((values[1] & 0xF) << 4) | (values[2] >> 2));
		}
		if (values[3] >= 0 && values[2] >= 0) {
			result.push_back(((values[2] & 0x3) << 6) | values[3]);
		}
	}
	return result;
}

/**
 * @brief Escape XML special characters in a string
 * @param s Input string view
 * @return Escaped string safe for XML text content
 */
inline std::string xmlEscapeContent(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default:
				if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
					char buf[16];
					snprintf(buf, sizeof(buf), "&#x%02x;", c);
					out += buf;
				} else {
					out.push_back(s[i]);
				}
		}
	}
	return out;
}

/**
 * @brief Sanitize a string to be a valid XML element name
 * @param key Input key string
 * @return Valid XML element name (invalid chars replaced with '_')
 */
inline std::string xmlSanitizeElementName(std::string_view key) {
	if (key.empty()) return "_";
	std::string out;
	out.reserve(key.size() + 1);
	auto isNameStart = [](unsigned char c) -> bool {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':';
	};
	auto isNameChar = [](unsigned char c) -> bool {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
	};
	if (!isNameStart(static_cast<unsigned char>(key[0]))) out.push_back('_');
	for (size_t i = 0; i < key.size(); i++) {
		unsigned char c = static_cast<unsigned char>(key[i]);
		out.push_back(isNameChar(c) ? key[i] : '_');
	}
	return out;
}

// ---------- YAML helpers ----------
/**
 * @brief Check if a YAML string value needs quoting
 * @param s Input string view
 * @return true if quotes are required
 */
inline bool yamlNeedsQuotes(std::string_view s) {
	if (s.empty()) return true;
	// YAML booleans, nulls, special floats - compare case-insensitively
	{
		std::string lc(s.size(), '\0');
		for (size_t i = 0; i < s.size(); i++)
			lc[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
		if (lc == "null" || lc == "~" || lc == "true" || lc == "false" ||
			lc == "yes" || lc == "no" || lc == "on" || lc == "off" ||
			lc == ".nan" || lc == ".inf" || lc == "-.inf") return true;
	}
	// Numeric
	bool digits = false;
	size_t i = 0;
	if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
	for (; i < s.size(); i++) {
		if (s[i] >= '0' && s[i] <= '9') { digits = true; continue; }
		if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') continue;
		if ((s[i] == '-' || s[i] == '+') && i > 0 && (s[i-1] == 'e' || s[i-1] == 'E')) continue;
		digits = false; break;
	}
	if (digits) return true;
	// Control chars
	for (auto c : s) if (static_cast<unsigned char>(c) < 0x20) return true;
	// Special leading chars
	char f = s[0];
	if (f == ' ' || f == '\t' || f == '&' || f == '*' || f == '!' || f == '|' || f == '>' ||
		f == '\'' || f == '"' || f == '%' || f == '@' || f == '`' || f == '[' || f == ']' ||
		f == '{' || f == '}' || f == ',' || f == '#' || f == '~' || f == '?' || f == ':') return true;
	if ((f == '-' && (s.size() == 1 || s[1] == ' ')) || (f == ':' && s.size() == 1)) return true;
	// Patterns inside
	for (size_t j = 0; j + 1 < s.size(); j++)
		if ((s[j] == ':' && s[j+1] == ' ') || (s[j] == '#' && (j == 0 || s[j-1] == ' '))) return true;
	if (s.back() == ':' || s.back() == ' ') return true;
	return false;
}

/**
 * @brief Quote a YAML string value
 * @param s Input string view
 * @return Properly quoted YAML string
 */
inline std::string yamlQuote(std::string_view s) {
	if (s.empty()) return "\"\"";
	// Multiline - literal block scalar
	auto nl = s.find('\n');
	if (nl != std::string_view::npos) {
		std::string out = "|";
		if (s.back() == '\n') out += "-";   // strip trailing newline
		out += "\n";
		std::string pad = "  ";
		out += pad;
		for (size_t idx = 0; idx < s.size(); idx++) {
			out += s[idx];
			if (s[idx] == '\n' && idx + 1 < s.size() && s[idx+1] != '\n') out += pad;
		}
		return out;
	}
	// Single-quote preferred
	if (s.find('\'') == std::string_view::npos) {
		return "'" + std::string(s) + "'";
	}
	// Double-quote fallback
	std::string out = "\"";
	for (auto c : s) {
		switch (c) {
			case '"': out += "\\\""; break;  case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;  case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += c;
		}
	}
	out += '"';
	return out;
}

/**
 * @brief Quote a YAML key if needed
 * @param s Input key string
 * @return Quoted key or original string
 */
inline std::string yamlQuoteKey(std::string_view s) {
	return yamlNeedsQuotes(s) ? yamlQuote(s) : std::string(s);
}

/**
 * @brief Convert hex character to its numeric value
 * @param c Hex character (0-9, a-f, A-F)
 * @return Value 0-15 or -1 on invalid
 */
inline int hexDigitValue(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/**
 * @brief Append a JSON-escaped string to output
 * @param out Output string to append to
 * @param s String view to escape
 */
inline void appendJsonEscaped(std::string& out, std::string_view s) {
	for (size_t i = 0; i < s.size(); i++) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c == '"') { out += '\\'; out.push_back('"'); }
		else if (c == '\\') { out += "\\\\"; }
		else if (c == '\n') out += "\\n";
		else if (c == '\r') out += "\\r";
		else if (c == '\t') out += "\\t";
		else if (c == '\b') out += "\\b";
		else if (c == '\f') out += "\\f";
		else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
		else out.push_back(c);
	}
}

inline void asvJSONValue::serialize(std::string& out, bool allowNaNInfinity) const {
	switch (type) {
		case NULL_VAL: out += "null"; break;
		case STRING: {
			out.reserve(out.size() + str_data.size() * 6 + 2);
			out.push_back('"');
			appendJsonEscaped(out, str_data);
			out.push_back('"');
			break;
		}
		case INT: out += std::to_string(num); break;
		case BOOL_VAL: out += flag ? "true" : "false"; break;
		case DOUBLE: {
			if (std::isnan(dbl) || std::isinf(dbl)) {
				if (!allowNaNInfinity) { out += "null"; break; }
				if (std::isnan(dbl)) { out += "NaN"; break; }
				if (dbl > 0) { out += "Infinity"; break; }
				else { out += "-Infinity"; break; }
			}
			if (dbl == std::floor(dbl) && dbl >= std::numeric_limits<int64_t>::min() && dbl <= std::numeric_limits<int64_t>::max()) {
				out += std::to_string(static_cast<int64_t>(dbl));
			} else {
			char buf[32];
			int n = snprintf(buf, sizeof(buf), "%.17g", dbl);
			if (n > 0 && static_cast<size_t>(n) < sizeof(buf)) {
				out += buf;
			} else if (n > 0) {
				std::string fallback(static_cast<size_t>(n) + 1, '\0');
				snprintf(&fallback[0], fallback.size(), "%.17g", dbl);
				out += fallback.c_str();
			} else {
				out += "null";
			}
			}
			break;
		}
		case DATETIME: {
			out.push_back('"');
			char buf[40];
			std::tm tm;
			asvjson_gmtime(&tm, &timestamp);
			if (datetime_ms > 0) {
				std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:", &tm);
				out += buf;
				out += std::to_string(tm.tm_sec);
				out.push_back('.');
				char msbuf[16];
				snprintf(msbuf, sizeof(msbuf), "%03d", datetime_ms);
				out += msbuf;
				out.push_back('Z');
			} else {
				std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
				out += buf;
			}
			out.push_back('"');
			break;
		}
		case BINARY: {
			if (bin_data.empty()) { out += "null"; break; }
			out += "\"__BASE64__";
			out += base64_encode(bin_data.data(), bin_data.size());
			out.push_back('"');
			break;
		}
		case OBJECT: {
			if (obj->size() < SIZE_MAX / 32) out.reserve(out.size() + obj->size() * 32);
			out.push_back('{');
			bool first = true;
			for (const auto& [key, val] : *obj) {
				if (!first) out.push_back(',');
				out.push_back('"');
				appendJsonEscaped(out, key);
				out += "\":";
				val->serialize(out, allowNaNInfinity);
				first = false;
			}
			out.push_back('}');
			break;
		}
		case ARRAY: {
			if (arr->size() < SIZE_MAX / 16) out.reserve(out.size() + arr->size() * 16);
			out.push_back('[');
			bool first = true;
			for (const auto& v : *arr) {
				if (!first) out.push_back(',');
				v->serialize(out, allowNaNInfinity);
				first = false;
			}
			out.push_back(']');
			break;
		}
		default: out += "null"; break;
	}
}

inline void asvJSONValue::serializePretty(std::string& out, int indent, bool allowNaNInfinity) const {
	size_t est = 256;
	if (type == OBJECT && obj && obj->size() < SIZE_MAX / 80) est = obj->size() * 80;
	else if (type == ARRAY && arr && arr->size() < SIZE_MAX / 32) est = arr->size() * 32;
	out.reserve(out.size() + est);
	std::string prefix(indent * 4, ' ');
	std::string indentNext((indent + 1) * 4, ' ');
	switch (type) {
		case NULL_VAL: out += "null"; break;
		case STRING: {
			out.push_back('"');
			appendJsonEscaped(out, str_data);
			out.push_back('"');
			break;
		}
		case INT: out += std::to_string(num); break;
		case BOOL_VAL: out += flag ? "true" : "false"; break;
		case DOUBLE: {
			if (std::isnan(dbl) || std::isinf(dbl)) {
				if (!allowNaNInfinity) { out += "null"; break; }
				if (std::isnan(dbl)) { out += "NaN"; break; }
				if (dbl > 0) { out += "Infinity"; break; }
				else { out += "-Infinity"; break; }
			}
			if (dbl == std::floor(dbl) && dbl >= std::numeric_limits<int64_t>::min() && dbl <= std::numeric_limits<int64_t>::max()) {
				out += std::to_string(static_cast<int64_t>(dbl));
				break;
			}
			char buf[64];
			int n = snprintf(buf, sizeof(buf), "%.17g", dbl);
			if (n > 0 && static_cast<size_t>(n) < sizeof(buf)) {
				out += buf;
			} else if (n > 0) {
				std::string fallback(static_cast<size_t>(n) + 1, '\0');
				snprintf(&fallback[0], fallback.size(), "%.17g", dbl);
				out += fallback.c_str();
			} else {
				out += "null";
			}
			break;
		}
		case DATETIME: {
			out.push_back('"');
			char buf[40];
			std::tm tm;
			asvjson_gmtime(&tm, &timestamp);
			if (datetime_ms > 0) {
				std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:", &tm);
				out += buf;
				out += std::to_string(tm.tm_sec);
				out.push_back('.');
				char msbuf[16];
				snprintf(msbuf, sizeof(msbuf), "%03d", datetime_ms);
				out += msbuf;
				out.push_back('Z');
			} else {
				std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
				out += buf;
			}
			out.push_back('"');
			break;
		}
		case BINARY: {
			if (bin_data.empty()) { out += "null"; break; }
			out += "\"__BASE64__";
			out += base64_encode(bin_data.data(), bin_data.size());
			out.push_back('"');
			break;
		}
		case OBJECT: {
			out.push_back('{');
			if (!obj->empty()) {
				out.push_back('\n');
				bool first = true;
				for (const auto& [key, val] : *obj) {
					if (!first) { out.push_back(','); out.push_back('\n'); }
						out += indentNext;
						out.push_back('"');
					appendJsonEscaped(out, key);
						out += "\": ";
						val->serializePretty(out, indent + 1, allowNaNInfinity);
						first = false;
				}
				out.push_back('\n');
				out += prefix;
			}
			out.push_back('}');
			break;
		}
		case ARRAY: {
			out.push_back('[');
			if (!arr->empty()) {
				out.push_back('\n');
				bool first = true;
				for (const auto& v : *arr) {
					if (!first) { out.push_back(','); out.push_back('\n'); }
					out += indentNext;
					v->serializePretty(out, indent + 1, allowNaNInfinity);
					first = false;
				}
				out.push_back('\n');
				out += prefix;
			}
			out.push_back(']');
			break;
		}
		default: out += "null"; break;
	}
}

class asvJSON {
public:
	/** @brief Error message from the last failed operation */
	mutable std::string lastError;
	/** @brief Allow NaN and Infinity values in JSON numbers (default: false) */
	bool allowNaNInfinity = false;

private:
	asvJSONValue* root = nullptr;
	std::string jsonBuf;
	std::string_view json;
	size_t pos = 0;
	int parseDepth = 0;

	/** Skip whitespace and comments (//, /*, #) */
	inline void skip() {
		while (pos < json.size()) {
			char ch = json[pos];
			if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { pos++; continue; }
			if (ch == '/' && pos + 1 < json.size()) {
				if (json[pos + 1] == '/') {
					pos += 2;
					while (pos < json.size() && json[pos] != '\n' && json[pos] != '\r') pos++;
				} else if (json[pos + 1] == '*') {
					pos += 2;
					while (pos + 1 < json.size() && !(json[pos] == '*' && json[pos + 1] == '/')) pos++;
					if (pos + 1 >= json.size()) throw asvJSONError("Unclosed multiline comment");
					pos += 2;
				} else break;
			} else if (ch == '#') {
				while (pos < json.size() && json[pos] != '\n' && json[pos] != '\r') pos++;
			} else break;
		}
	}

	/** @return Current character, or 0 at end */
	inline char cur() const { return pos < json.size() ? json[pos] : 0; }
	/** Advance to next character */
	inline void next() { pos++; }

	std::string_view parseStringRaw() {
		if (cur() != '"') throw asvJSONError("Expected string");
		size_t start = ++pos;
		while (pos < json.size()) {
			char c = json[pos];
			if (c == '"') break;
			if (c == '\\') {
				if (pos + 1 >= json.size()) throw asvJSONError("Unclosed escape");
				pos += 2;
			} else {
				++pos;
			}
		}
		if (pos >= json.size() || json[pos] != '"') throw asvJSONError("Unclosed string");
		size_t len = pos - start;
		if (len > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("String too long");
		next(); // skip closing quote
		return json.substr(start, len);
	}

	std::string parseStringKey() {
		std::string_view raw = parseStringRaw();
		if (raw.size() > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("Object key too long");
		if (raw.find('\\') == std::string_view::npos) {
			for (size_t i = 0; i < raw.size(); i++) {
				if (static_cast<unsigned char>(raw[i]) < 0x20) throw asvJSONError("Control character in object key");
			}
			if (!isValidUTF8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size())) throw asvJSONError("Invalid UTF-8 in object key");
			return std::string(raw);
		}
		std::string unescaped;
		unescaped.reserve(raw.size());
		for (size_t i = 0; i < raw.size();) {
			if (raw[i] == '\\' && i + 1 < raw.size()) {
				i++;
				if (raw[i] == 'u' && i + 4 < raw.size()) {
					unsigned int cp = 0;
					bool valid = true;
			for (int j = 0; j < 4; j++) {
					cp <<= 4;
					int v = hexDigitValue(raw[i + 1 + j]);
					if (v < 0) { valid = false; break; }
					cp += static_cast<unsigned int>(v);
				}
					if (!valid) throw asvJSONError("Invalid Unicode escape in object key");
					if (cp > 0x10FFFF) throw asvJSONError("Unicode code point out of range");
					if (cp >= 0xD800 && cp <= 0xDBFF) {
						if (i + 10 < raw.size() && raw[i + 5] == '\\' && raw[i + 6] == 'u') {
							unsigned int low = 0;
							bool low_valid = true;
							for (int j = 0; j < 4; j++) {
								low <<= 4;
								int v = hexDigitValue(raw[i + 7 + j]);
								if (v < 0) { low_valid = false; break; }
								low += static_cast<unsigned int>(v);
							}
							if (low_valid && low >= 0xDC00 && low <= 0xDFFF) {
								cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
								if (cp < 0x80) unescaped.push_back(static_cast<char>(cp));
								else if (cp < 0x800) {
									unescaped.push_back(static_cast<char>(0xC0 | (cp >> 6)));
									unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
								} else if (cp < 0x10000) {
									unescaped.push_back(static_cast<char>(0xE0 | (cp >> 12)));
									unescaped.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
									unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
								} else {
									unescaped.push_back(static_cast<char>(0xF0 | (cp >> 18)));
									unescaped.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
									unescaped.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
									unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
								}
								i += 11;
								continue;
							}
						}
						throw asvJSONError("Invalid lone surrogate in object key");
					}
					if (cp >= 0xDC00 && cp <= 0xDFFF) throw asvJSONError("Invalid lone surrogate in object key");
					if (cp < 0x80) unescaped.push_back(static_cast<char>(cp));
					else if (cp < 0x800) {
						unescaped.push_back(static_cast<char>(0xC0 | (cp >> 6)));
						unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
					} else if (cp < 0x10000) {
						unescaped.push_back(static_cast<char>(0xE0 | (cp >> 12)));
						unescaped.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
						unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
					} else {
						unescaped.push_back(static_cast<char>(0xF0 | (cp >> 18)));
						unescaped.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
						unescaped.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
						unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
					}
					i += 5;
					continue;
				} else {
					switch (raw[i]) {
						case 'n': unescaped.push_back('\n'); break;
						case 'r': unescaped.push_back('\r'); break;
						case 't': unescaped.push_back('\t'); break;
						case 'b': unescaped.push_back('\b'); break;
						case 'f': unescaped.push_back('\f'); break;
						case '"': unescaped.push_back('"'); break;
						case '\\': unescaped.push_back('\\'); break;
						case '/': unescaped.push_back('/'); break;
						default: unescaped.push_back(raw[i]); break;
					}
				}
				i++;
			} else {
				if (static_cast<unsigned char>(raw[i]) < 0x20) throw asvJSONError("Control character in object key");
				unescaped.push_back(raw[i]);
				i++;
			}
		}
		if (!isValidUTF8(reinterpret_cast<const uint8_t*>(unescaped.data()), unescaped.size())) throw asvJSONError("Invalid UTF-8 in object key");
		return unescaped;
	}

	/** Try to parse ISO 8601 datetime string */
	bool tryParseDateTime(std::string_view sv, time_t& out, int* ms_out) {
		if (sv.size() < 20) return false;

		auto isDigit = [](char c) { return c >= '0' && c <= '9'; };

		if (!isDigit(sv[0]) || !isDigit(sv[1]) || !isDigit(sv[2]) || !isDigit(sv[3]) ||
			sv[4] != '-' || !isDigit(sv[5]) || !isDigit(sv[6]) || sv[7] != '-' ||
			!isDigit(sv[8]) || !isDigit(sv[9]) || sv[10] != 'T' ||
			!isDigit(sv[11]) || !isDigit(sv[12]) || sv[13] != ':' ||
			!isDigit(sv[14]) || !isDigit(sv[15]) || sv[16] != ':' ||
			!isDigit(sv[17]) || !isDigit(sv[18])) return false;

		std::tm tm = {};
		int year = (sv[0] - '0') * 1000 + (sv[1] - '0') * 100 + (sv[2] - '0') * 10 + (sv[3] - '0');
		int month = (sv[5] - '0') * 10 + (sv[6] - '0');
		int day = (sv[8] - '0') * 10 + (sv[9] - '0');
		int hour = (sv[11] - '0') * 10 + (sv[12] - '0');
		int minute = (sv[14] - '0') * 10 + (sv[15] - '0');
		int second = (sv[17] - '0') * 10 + (sv[18] - '0');

		if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return false;
		static constexpr int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		int maxDay = daysInMonth[month - 1];
		if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) maxDay = 29;
		if (day > maxDay) return false;

		tm.tm_year = year - 1900;
		tm.tm_mon = month - 1;
		tm.tm_mday = day;
		tm.tm_hour = hour;
		tm.tm_min = minute;
		tm.tm_sec = second;
		tm.tm_isdst = 0;

		int ms = 0;
		size_t tz_pos = 19;
		if (tz_pos < sv.size() && sv[tz_pos] == '.') {
			size_t p = tz_pos + 1;
			size_t digits = 0;
			while (p < sv.size() && sv[p] >= '0' && sv[p] <= '9' && digits < 3) {
				ms = ms * 10 + (sv[p] - '0');
				p++;
				digits++;
			}
			if (digits < 3) ms *= (digits == 1 ? 100 : 10);
			if (digits == 3 && p < sv.size() && sv[p] >= '0' && sv[p] <= '9') return false;
			while (p < sv.size() && sv[p] >= '0' && sv[p] <= '9') p++;
			tz_pos = p;
		}

		int tz_offset = 0;
		if (tz_pos < sv.size()) {
			if (sv[tz_pos] == 'Z') {
				tz_offset = 0;
				tz_pos++;
			} else if (sv[tz_pos] == '+' || sv[tz_pos] == '-') {
				int sign = (sv[tz_pos] == '+') ? 1 : -1;
				size_t tz_start = tz_pos + 1;
				if (tz_start + 4 <= sv.size() && sv[tz_start + 2] == ':') {
					int h = (sv[tz_start] - '0') * 10 + (sv[tz_start + 1] - '0');
					int m = (sv[tz_start + 3] - '0') * 10 + (sv[tz_start + 4] - '0');
					tz_offset = sign * (h * 60 + m);
					tz_pos = tz_start + 5;
				} else if (tz_start + 3 <= sv.size()) {
					int h = (sv[tz_start] - '0') * 10 + (sv[tz_start + 1] - '0');
					int m = (sv[tz_start + 2] - '0') * 10 + (sv[tz_start + 3] - '0');
					tz_offset = sign * (h * 60 + m);
					tz_pos = tz_start + 4;
				}
			}
		}

		time_t t = asvjson_timegm(&tm);
		if (t == -1) return false;
		t -= tz_offset * 60;
		out = t;
		if (ms_out) *ms_out = ms;
		return tz_pos == sv.size();
	}

	asvJSONValue* parseValue() {
		skip();
		char c = cur();
		if (c == '{') return parseObject();
		if (c == '[') return parseArray();
		if (c == '"') return parseStringOrSpecial();
		if (c == 't') {
			next();
			if (cur() != 'r') throw asvJSONError("Invalid true");
			next();
			if (cur() != 'u') throw asvJSONError("Invalid true");
			next();
			if (cur() != 'e') throw asvJSONError("Invalid true");
			next();
			return asvJSONValue::makeBool(true);
		}
		if (c == 'f') {
			next();
			if (cur() != 'a') throw asvJSONError("Invalid false");
			next();
			if (cur() != 'l') throw asvJSONError("Invalid false");
			next();
			if (cur() != 's') throw asvJSONError("Invalid false");
			next();
			if (cur() != 'e') throw asvJSONError("Invalid false");
			next();
			return asvJSONValue::makeBool(false);
		}
		if (c == 'n') {
			next();
			if (cur() != 'u') throw asvJSONError("Invalid null");
			next();
			if (cur() != 'l') throw asvJSONError("Invalid null");
			next();
			if (cur() != 'l') throw asvJSONError("Invalid null");
			next();
			return asvJSONValue::makeNull();
		}
		if (allowNaNInfinity && c == 'N') {
			if (json.compare(pos, 3, "NaN") == 0) { pos += 3; return asvJSONValue::makeDouble(NAN); }
		} else if (allowNaNInfinity && c == 'I') {
			if (json.compare(pos, 8, "Infinity") == 0) { pos += 8; return asvJSONValue::makeDouble(INFINITY); }
		} else if (allowNaNInfinity && c == '-' && pos + 1 < json.size() && json[pos + 1] == 'I') {
			if (json.compare(pos, 9, "-Infinity") == 0) { pos += 9; return asvJSONValue::makeDouble(-INFINITY); }
		}
		if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
		throw asvJSONError(std::string("Unexpected: ") + c);
	}

	asvJSONValue* parseStringOrSpecial() {
		std::string_view raw = parseStringRaw();

		// Check Base64 first (must precede date parse to avoid false positives)
		if (raw.size() > 10 && raw.compare(0, 10, "__BASE64__") == 0) {
			auto data = base64_decode_fast(raw.data() + 10, raw.size() - 10);
			auto* v = asvJSONValue::makeBinary(data.data(), data.size());
			if (!v) throw asvJSONError("Failed to allocate binary");
			return v;
		}

		if (raw.size() >= 20 && raw[4] == '-' && raw[7] == '-' && raw[10] == 'T') {
			time_t ts;
			int ms = 0;
			if (tryParseDateTime(raw, ts, &ms)) {
				return asvJSONValue::makeDateTime(ts, ms);
			}
		}

		// Check for escape without copying
		if (raw.find('\\') == std::string_view::npos) {
			for (size_t i = 0; i < raw.size(); i++) {
				if (static_cast<unsigned char>(raw[i]) < 0x20) throw asvJSONError("Control character in string");
			}
			if (!isValidUTF8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size())) throw asvJSONError("Invalid UTF-8 in string");
			auto* v = asvJSONValue::makeString(raw.data(), raw.size());
			if (!v) throw asvJSONError("Failed to allocate string");
			return v;
		}

		// Process escapes
		std::string unescaped;
		unescaped.reserve(raw.size());
		for (size_t i = 0; i < raw.size(); ) {
			if (raw[i] == '\\' && i + 1 < raw.size()) {
				i++;
				if (raw[i] == 'u' && i + 4 < raw.size()) {
					unsigned int cp = 0;
					bool valid = true;
			for (int j = 0; j < 4; j++) {
					cp <<= 4;
					int v = hexDigitValue(raw[i + 1 + j]);
					if (v < 0) { valid = false; break; }
					cp += static_cast<unsigned int>(v);
				}
					if (valid && cp <= 0x10FFFF) {
						if (cp >= 0xD800 && cp <= 0xDBFF) {
							if (i + 10 < raw.size() && raw[i+5] == '\\' && raw[i+6] == 'u') {
								unsigned int low = 0;
								bool low_valid = true;
								for (int j = 0; j < 4; j++) {
									low <<= 4;
									int v = hexDigitValue(raw[i + 7 + j]);
									if (v < 0) { low_valid = false; break; }
									low += static_cast<unsigned int>(v);
								}
								if (low_valid && low >= 0xDC00 && low <= 0xDFFF) {
									cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
									unescaped.push_back(static_cast<char>(0xF0 | (cp >> 18)));
									unescaped.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
									unescaped.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
									unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
									i += 11;
									continue;
								}
							}
							throw asvJSONError("Invalid lone surrogate in string");
						}
						if (cp >= 0xDC00 && cp <= 0xDFFF) throw asvJSONError("Invalid lone surrogate in string");
						if (cp < 0x80) unescaped.push_back(static_cast<char>(cp));
						else if (cp < 0x800) { unescaped.push_back(static_cast<char>(0xC0 | (cp >> 6))); unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
						else if (cp < 0x10000) { unescaped.push_back(static_cast<char>(0xE0 | (cp >> 12))); unescaped.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
						else { unescaped.push_back(static_cast<char>(0xF0 | (cp >> 18))); unescaped.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); unescaped.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); unescaped.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
						i += 5;
						continue;
					} else throw asvJSONError("Invalid Unicode escape sequence");
				} else {
					switch (raw[i]) {
						case 'n': unescaped.push_back('\n'); break;
						case 'r': unescaped.push_back('\r'); break;
						case 't': unescaped.push_back('\t'); break;
						case 'b': unescaped.push_back('\b'); break;
						case 'f': unescaped.push_back('\f'); break;
						case '"': unescaped.push_back('"'); break;
						case '\\': unescaped.push_back('\\'); break;
						default: unescaped.push_back(raw[i]);
					}
				}
				i++;
			} else {
				if (static_cast<unsigned char>(raw[i]) < 0x20) throw asvJSONError("Control character in string");
				unescaped.push_back(raw[i]); i++;
			}
		}
		if (!isValidUTF8(reinterpret_cast<const uint8_t*>(unescaped.data()), unescaped.size())) {
			throw asvJSONError("Invalid UTF-8 sequence");
		}
		auto* v = asvJSONValue::makeString(unescaped.c_str(), unescaped.size());
		if (!v) throw asvJSONError("Failed to allocate string");
		return v;
	}

	asvJSONValue* parseObject() {
		if (++parseDepth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) {
			--parseDepth;
			throw asvJSONError("Maximum nesting depth exceeded");
		}
		auto* obj = asvJSONValue::makeObject();
		if (!obj) { --parseDepth; throw asvJSONError("Failed to allocate object"); }
		next(); // skip '{'
		size_t objSize = 0;
		try {
			while (true) {
				skip();
				if (cur() == '}') { next(); break; }
				if (objSize >= asvJSONValue::MAX_OBJECT_SIZE) { --parseDepth; throw asvJSONError("Object too large"); }
				std::string key = parseStringKey();
				skip();
				if (cur() != ':') throw asvJSONError("Expected ':'");
				next();
				asvJSONValue* val = parseValue();
				if (!val) { --parseDepth; throw asvJSONError("Failed to parse object value"); }
			auto it = map_find(*(obj->obj), key);
			if (it != obj->obj->end()) {
				it->second.reset(val);
			} else {
				try {
					obj->obj->emplace(std::move(key), std::unique_ptr<asvJSONValue>(val));
				} catch (...) {
					delete val;
					throw;
				}
				objSize++;
			}
			skip();
				if (cur() == '}') { next(); break; }
				if (cur() == ',') next();
				else throw asvJSONError("Expected ',' or '}'");
			}
		} catch (...) { delete obj; --parseDepth; throw; }
		--parseDepth;
		return obj;
	}

	asvJSONValue* parseArray() {
		if (++parseDepth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) {
			--parseDepth;
			throw asvJSONError("Maximum nesting depth exceeded");
		}
		auto* arr = asvJSONValue::makeArray();
		if (!arr) { --parseDepth; throw asvJSONError("Failed to allocate array"); }
		arr->arr->reserve(16);
		next(); // skip '['
		while (true) {
			skip();
			if (cur() == ']') { next(); break; }
			if (arr->arr->size() >= asvJSONValue::MAX_ARRAY_SIZE) { delete arr; --parseDepth; throw asvJSONError("Array too large"); }
			auto* val = parseValue();
			if (!val) { delete arr; throw asvJSONError("Failed to parse array element"); }
			arr->arr->push_back(std::unique_ptr<asvJSONValue>(val));
			skip();
			if (cur() == ']') { next(); break; }
			if (cur() == ',') next();
			else throw asvJSONError("Expected ',' or ']'");
		}
		--parseDepth;
		return arr;
	}

	asvJSONValue* parseNumber() {
		size_t start = pos;
		auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)); };
		if (cur() == '-') next();
		size_t intStart = pos;
		while (isDigit(cur())) next();
		size_t intLen = pos - intStart;
		if (intLen > 1 && json[intStart] == '0') throw asvJSONError("Invalid number: leading zero not allowed");
		bool isDouble = false;
		if (cur() == '.') { isDouble = true; next(); if (!isDigit(cur())) throw asvJSONError("Invalid number: digit required after decimal point"); while (isDigit(cur())) next(); }
		if (cur() == 'e' || cur() == 'E') { isDouble = true; next(); if (cur() == '+' || cur() == '-') next(); if (!isDigit(cur())) throw asvJSONError("Invalid number: digit required after exponent"); while (isDigit(cur())) next(); }
		size_t numLen = pos - start;
		if (numLen == 0) throw asvJSONError("Invalid number");
		const char* buf = json.data() + start;
		if (isDouble) {
			double d;
		#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || __GNUC__ >= 11)
			auto [ptr, ec] = std::from_chars(buf, buf + numLen, d);
			if (ec != std::errc() || ptr != buf + numLen) throw asvJSONError("Invalid number");
		#else
			std::string numStr(buf, numLen);
			char* endptr;
			errno = 0;
			d = std::strtod(numStr.c_str(), &endptr);
			if (errno == ERANGE) throw asvJSONError("Invalid number: out of range");
			if (endptr != numStr.c_str() + numLen) throw asvJSONError("Invalid number");
		#endif
			if (!allowNaNInfinity && (std::isnan(d) || std::isinf(d))) throw asvJSONError("Invalid number: NaN or Infinity not allowed");
			return asvJSONValue::makeDouble(d);
		} else {
			long long l;
			auto [ptr, ec] = std::from_chars(buf, buf + numLen, l);
			if (ec != std::errc() || ptr != buf + numLen) throw asvJSONError("Invalid number");
			return asvJSONValue::makeInt(l);
		}
	}

public:
	/** Default constructor - creates an empty (null) document */
	asvJSON() = default;
	/** Destructor */
	~asvJSON() { delete root; }
	/**
	 * @brief Copy constructor - deep clones the document
	 * @param other Document to copy
	 */
	asvJSON(const asvJSON& other) {
		root = cloneValue(other.root);
	}
	/**
	 * @brief Copy assignment - deep clones the document
	 * @param other Document to copy
	 * @return Reference to this
	 */
	asvJSON& operator=(const asvJSON& other) {
		if (this != &other) {
			delete root;
			root = cloneValue(other.root);
		}
		return *this;
	}
	/**
	 * @brief Move constructor - transfers ownership
	 * @param other Document to move from
	 */
	asvJSON(asvJSON&& other) noexcept
		: lastError(std::move(other.lastError)),
		  allowNaNInfinity(other.allowNaNInfinity),
		  root(other.root),
		  jsonBuf(std::move(other.jsonBuf)),
		  json(jsonBuf),
		  pos(other.pos),
		  parseDepth(other.parseDepth) {
		other.root = nullptr;
		other.pos = 0;
		other.parseDepth = 0;
		other.json = std::string_view();
	}
	/**
	 * @brief Move assignment - transfers ownership
	 * @param other Document to move from
	 * @return Reference to this
	 */
	asvJSON& operator=(asvJSON&& other) noexcept {
		if (this != &other) {
			delete root;
			root = other.root;
			jsonBuf = std::move(other.jsonBuf);
			json = jsonBuf;
			pos = other.pos;
			parseDepth = other.parseDepth;
			lastError = std::move(other.lastError);
			allowNaNInfinity = other.allowNaNInfinity;
			other.root = nullptr;
			other.pos = 0;
			other.parseDepth = 0;
			other.json = std::string_view();
		}
		return *this;
	}

	/**
	 * @brief Parse a JSON string from std::string
	 * @param s JSON string to parse
	 * @return true if parsing succeeded, false on error (see lastError)
	 */
	bool parse(const std::string& s) {
		delete root;
		root = nullptr;
		jsonBuf = s;
		json = jsonBuf;
		pos = 0;
		try {
			root = parseValue();
			skip();
			if (pos != json.size()) { delete root; root = nullptr; lastError = "Trailing chars"; return false; }
			return root != nullptr;
		} catch (const asvJSONError& e) {
			lastError = e.what();
			delete root;
			root = nullptr;
			return false;
		}
	}

	/**
	 * @brief Parse a JSON string from string_view (zero-copy for temporary strings)
	 * @param s JSON string to parse
	 * @return true if parsing succeeded, false on error (see lastError)
	 */
	bool parse(std::string_view s) {
		delete root;
		root = nullptr;
		jsonBuf.assign(s.data(), s.size());
		json = jsonBuf;
		pos = 0;
		try {
			root = parseValue();
			skip();
			if (pos != json.size()) { delete root; root = nullptr; lastError = "Trailing chars"; return false; }
			return root != nullptr;
		} catch (const asvJSONError& e) {
			lastError = e.what();
			delete root;
			root = nullptr;
			return false;
		}
	}

	/**
	 * @brief Serialize to JSON string
	 * @param pretty Enable pretty printing with indentation
	 * @return JSON string representation
	 */
	std::string serialize(bool pretty = false) const {
		std::string out;
		out.reserve(512);
		if (root) {
			if (pretty) root->serializePretty(out, 0, allowNaNInfinity);
			else root->serialize(out, allowNaNInfinity);
		} else out = "null";
		return out;
	}

	/**
	 * @brief Write JSON to file
	 * @param filename Path to output file
	 * @param pretty Enable pretty printing
	 * @return true on success, false on error (see lastError)
	 */
	bool writeToFile(const std::string& filename, bool pretty = false) const {
		std::ofstream out(filename);
		if (!out.is_open()) {
			lastError = "Failed to open file: " + filename;
			return false;
		}
		out << serialize(pretty);
		if (!out.good()) {
			lastError = "Failed to write file: " + filename;
			out.close();
			return false;
		}
		out.close();
		return true;
	}

	/**
	 * @brief Read JSON from file
	 * @param filename Path to input file
	 * @return true on success, false on error (see lastError)
	 */
	bool readFromFile(const std::string& filename) {
		std::ifstream in(filename);
		if (!in.is_open()) {
			lastError = "Failed to open file: " + filename;
			return false;
		}
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();
		return parse(content);
	}

	// Using string_view for zero-copy get operations
	/**
	 * @brief Get string value by key (supports nested paths like "user.address.city")
	 * @param key Key path (dot notation for nested objects)
	 * @return String value or empty string if not found/wrong type
	 */
	[[nodiscard]] std::string getString(std::string_view key) const {
		if (!root) return "";
		auto* v = getNested(key);
		if (!v) return "";
		if (v->type == asvJSONValue::STRING) return std::string(v->str_data.data(), v->str_data.size());
		if (v->type == asvJSONValue::DATETIME) {
			char buf[32];
			std::tm tm;
			asvjson_gmtime(&tm, &v->timestamp);
			std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
			return std::string(buf);
		}
		return "";
	}

	/**
	 * @brief Get string value as string_view (zero-copy)
	 * @param key Key path
	 * @return string_view or empty if not found/not string type
	 */
	[[nodiscard]] std::string_view getStringView(std::string_view key) const noexcept {
		if (!root) return {};
		auto* v = getNested(key);
		if (!v || v->type != asvJSONValue::STRING) return {};
		return std::string_view(v->str_data.data(), v->str_data.size());
	}

	/**
	 * @brief Get integer value by key
	 * @param key Key path (supports nested paths)
	 * @return Integer value or 0 if not found/wrong type
	 */
	[[nodiscard]] int64_t getInt(std::string_view key) const noexcept {
		if (!root) return 0;
		auto* v = getNested(key);
		return v && v->type == asvJSONValue::INT ? v->num : 0;
	}

	/**
	 * @brief Get double value by key (converts int to double if needed)
	 * @param key Key path
	 * @return Double value or 0.0 if not found/wrong type
	 */
	[[nodiscard]] double getDouble(std::string_view key) const noexcept {
		if (!root) return 0.0;
		auto* v = getNested(key);
		if (!v) return 0.0;
		if (v->type == asvJSONValue::DOUBLE) return v->dbl;
		if (v->type == asvJSONValue::INT) return static_cast<double>(v->num);
		return 0.0;
	}

	/**
	 * @brief Get boolean value by key (flat lookup, use getNested for dot-paths)
	 * @param key Object key
	 * @return Boolean value or false if not found/wrong type
	 */
	[[nodiscard]] bool getBool(std::string_view key) const noexcept {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::BOOL_VAL ? v->flag : false;
	}

	/**
	 * @brief Get datetime timestamp by key
	 * @param key Key path
	 * @return Unix timestamp or 0 if not found/wrong type
	 */
	[[nodiscard]] time_t getDateTime(std::string_view key) const {
		if (!root) return 0;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::DATETIME ? v->timestamp : 0;
	}

	/**
	 * @brief Get binary data by key
	 * @param key Key path
	 * @return Binary data vector or empty vector if not found/wrong type
	 */
	[[nodiscard]] std::vector<uint8_t> getBinary(std::string_view key) const {
		if (!root) return {};
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::BINARY ? v->getBinary() : std::vector<uint8_t>();
	}

	// Put methods - const char* versions (primary, no allocation)
	/**
	 * @brief Internal helper for put* methods
	 */
	template<typename F>
	void setValue(std::string_view key, F&& factory) {
		if (!root || root->type != asvJSONValue::OBJECT) { delete root; root = asvJSONValue::makeObject(); }
		if (!root) return;
		auto* v = factory();
		if (!v) return;
		auto it = map_find(*root->obj, key);
		if (it != root->obj->end()) { it->second.reset(v); }
		else { root->obj->emplace(std::string(key), std::unique_ptr<asvJSONValue>(v)); }
	}

	/**
	 * @brief Store string value from string_view
	 * @param key Object key
	 * @param value String value (copied)
	 */
	void putString(std::string_view key, std::string_view value) {
		setValue(key, [value]{ return asvJSONValue::makeString(value.data(), value.size()); });
	}

	/**
	 * @brief Store integer value
	 * @param key Object key
	 * @param value Integer value
	 */
	void putInt(std::string_view key, int64_t value) {
		setValue(key, [value]{ return asvJSONValue::makeInt(value); });
	}

	/**
	 * @brief Store double value
	 * @param key Object key
	 * @param value Double value
	 */
	void putDouble(std::string_view key, double value) {
		setValue(key, [value]{ return asvJSONValue::makeDouble(value); });
	}

	/**
	 * @brief Store float value (as double with is_float32 flag for round-trip)
	 * @param key Object key
	 * @param value Float value
	 */
	void putFloat32(std::string_view key, float value) {
		setValue(key, [value]{ auto* v = asvJSONValue::makeDouble(static_cast<double>(value)); if (v) v->is_float32 = true; return v; });
	}

	/**
	 * @brief Store boolean value
	 * @param key Object key
	 * @param value Boolean value
	 */
	void putBool(std::string_view key, bool value) {
		setValue(key, [value]{ return asvJSONValue::makeBool(value); });
	}

	/**
	 * @brief Store datetime value
	 * @param key Object key
	 * @param value Unix timestamp
	 */
	void putDateTime(std::string_view key, time_t value) {
		setValue(key, [value]{ return asvJSONValue::makeDateTime(value); });
	}

	/**
	 * @brief Store null value
	 * @param key Object key
	 */
	void putNull(std::string_view key) {
		setValue(key, []{ return asvJSONValue::makeNull(); });
	}

	/**
	 * @brief Store binary data (base64 encoded in JSON)
	 * @param key Key to store binary data
	 * @param data Pointer to binary data
	 * @param len Length of binary data
	 */
	void putBinary(std::string_view key, const uint8_t* data, size_t len) {
		setValue(key, [data, len]{ return asvJSONValue::makeBinary(data, len); });
	}
	/**
	 * @brief Store binary data from vector
	 * @param key Key to store binary data
	 * @param data Vector containing binary data
	 */
	void putBinary(std::string_view key, const std::vector<uint8_t>& data) {
		putBinary(key, data.data(), data.size());
	}

	/**
	 * @brief Store binary data as base64-encoded chunks in an array
	 * @param key Key to store the chunked binary data
	 * @param data Pointer to binary data
	 * @param size Size of binary data in bytes
	 * @param chunk_size Size of each base64 chunk in bytes (default: 76 per RFC 2045)
	 */
	void putBinChunked(std::string_view key, const uint8_t* data, size_t size, size_t chunk_size = 76) {
		if (!root || root->type != asvJSONValue::OBJECT) { delete root; root = asvJSONValue::makeObject(); }
		if (!root) return;
		auto* arr = asvJSONValue::makeArray();
		if (!arr) return;
		size_t bytes_per_chunk = (chunk_size / 4) * 3;
		for (size_t i = 0; i < size; i += bytes_per_chunk) {
			size_t chunk = std::min(bytes_per_chunk, size - i);
			std::string encoded = base64_encode(data + i, chunk);
			auto* v = asvJSONValue::makeString(encoded.c_str(), encoded.length());
			if (!v) { delete arr; return; }
			arr->arr->emplace_back(std::unique_ptr<asvJSONValue>(v));
		}
		root->obj->emplace(std::string(key), std::unique_ptr<asvJSONValue>(arr));
	}

	/**
	 * @brief Retrieve binary data stored as base64-encoded chunks
	 * @param key Key containing chunked binary data
	 * @return Decoded binary data, empty vector if not found or wrong type
	 */
	[[nodiscard]] std::vector<uint8_t> getBinChunked(std::string_view key) const {
		auto* arr = getArray(key);
		if (!arr) return {};
		std::vector<uint8_t> result;
		for (size_t i = 0; i < arr->size(); i++) {
			auto* chunk = arr->get(i);
			if (chunk && chunk->type == asvJSONValue::STRING) {
				bool decode_error = false;
				auto decoded = base64_decode_fast(chunk->str_data.data(), chunk->str_data.size(), &decode_error);
				if (decode_error) return {};
				result.insert(result.end(), decoded.begin(), decoded.end());
			}
		}
		return result;
	}

	/**
	 * @brief Store Object Id value (12 bytes)
	 * @param key Key
	 * @param oid Object Id data (must be exactly 12 bytes)
	 */
	void putObjectId(std::string_view key, std::string_view oid) {
		setValue(key, [oid]{ return asvJSONValue::makeObjectId(oid); });
	}

	/**
	 * @brief Store timestamp value
	 * @param key Key
	 * @param ts Timestamp value
	 */
	void putTimestamp(std::string_view key, int64_t ts) {
		setValue(key, [ts]{ return asvJSONValue::makeTimestamp(ts); });
	}
	/**
	 * @brief Store regex value
	 * @param key Key
	 * @param pattern Regex pattern
	 * @param options Regex options
	 */
	void putRegex(std::string_view key, const char* pattern, const char* options) {
		setValue(key, [pattern, options]{ return asvJSONValue::makeRegex(pattern, options); });
	}

	/**
	 * @brief Store extension type value
	 * @param key Key
	 * @param extType Extension type identifier
	 * @param data Pointer to extension data
	 * @param len Length of extension data
	 */
	void putExtension(std::string_view key, int8_t extType, const uint8_t* data, size_t len) {
		setValue(key, [extType, data, len]{ return asvJSONValue::makeExtension(extType, data, len); });
	}

	/**
	 * @brief Get extension type data by key
	 * @param key Key path
	 * @return Pair of (extension type, binary data) or (0, empty) if not found
	 */
	[[nodiscard]] std::pair<int8_t, std::vector<uint8_t>> getExtension(std::string_view key) const {
		auto* v = get(key);
		if (v && v->type == asvJSONValue::EXTENSION) {
			return std::make_pair(v->ext_type, v->getBinary());
		}
		return std::make_pair<int8_t, std::vector<uint8_t>>(0, std::vector<uint8_t>());
	}
	/**
	 * @brief Check if value is extension type
	 * @param key Key path
	 * @param specificType Optional: check specific extension type
	 * @return true if value is extension (optionally of specific type)
	 */
	[[nodiscard]] bool isExtension(std::string_view key, int8_t specificType = 0) const {
		auto* v = get(key);
		if (!v || v->type != asvJSONValue::EXTENSION) return false;
		return specificType == 0 || v->ext_type == specificType;
	}

	/**
	 * @brief Get ObjectId value by key
	 * @param key Key path
	 * @return ObjectId as hex string or empty string if not found/wrong type
	 */
	[[nodiscard]] std::string getObjectId(std::string_view key) const {
		auto* v = get(key);
		return (v && v->type == asvJSONValue::OBJECTID) ? std::string(v->str_data.data(), v->str_data.size()) : std::string();
	}
	/**
	 * @brief Get ObjectId as string_view (zero-copy)
	 * @param key Key path
	 * @return string_view or empty if not ObjectId
	 */
	[[nodiscard]] std::string_view getObjectIdView(std::string_view key) const {
		auto* v = get(key);
		return (v && v->type == asvJSONValue::OBJECTID) ? std::string_view(v->str_data.data(), v->str_data.size()) : std::string_view();
	}

	/**
	 * @brief Get timestamp value by key
	 * @param key Key path
	 * @return Timestamp value or 0 if not found/wrong type
	 */
	[[nodiscard]] int64_t getTimestamp(std::string_view key) const {
		auto* v = get(key);
		return (v && v->type == asvJSONValue::TIMESTAMP) ? v->num : 0;
	}

	/**
	 * @brief Get regex pattern and options by key
	 * @param key Key path
	 * @return Pair of (pattern, options) or (empty, empty) if not found/wrong type
	 */
	[[nodiscard]] std::pair<std::string, std::string> getRegex(std::string_view key) const {
		auto* v = get(key);
		if (v && v->type == asvJSONValue::REGEX && v->str_data.size() > 0) {
			const char* sep = static_cast<const char*>(memchr(v->str_data.data(), '|', v->str_data.size()));
			if (sep) {
				std::string pattern(v->str_data.data(), sep - v->str_data.data());
				std::string options(sep + 1, v->str_data.data() + v->str_data.size() - sep - 1);
				return std::make_pair(pattern, options);
			}
			return std::make_pair(std::string(v->str_data.data(), v->str_data.size()), std::string());
		}
		return std::make_pair(std::string(), std::string());
	}
	/**
	 * @brief Get regex value by key (output parameters variant)
	 * @param key Object key
	 * @param pattern [out] Regex pattern
	 * @param options [out] Regex options
	 * @return true if regex was found and extracted
	 */
	bool getRegex(std::string_view key, std::string& pattern, std::string& options) const {
		pattern.clear();
		options.clear();
		auto* v = get(key);
		if (v && v->type == asvJSONValue::REGEX && v->str_data.size() > 0) {
			const char* sep = static_cast<const char*>(memchr(v->str_data.data(), '|', v->str_data.size()));
			if (sep) {
				pattern.assign(v->str_data.data(), sep - v->str_data.data());
				options.assign(sep + 1, v->str_data.data() + v->str_data.size() - sep - 1);
			} else {
				pattern.assign(v->str_data.data(), v->str_data.size());
			}
		}
		return !pattern.empty();
	}

	/**
	 * @brief Check if value is ObjectId type
	 * @param key Key path
	 * @return true if value is ObjectId type
	 */
	[[nodiscard]] bool isObjectId(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::OBJECTID;
	}

	/**
	 * @brief Check if value is timestamp type
	 * @param key Key path
	 * @return true if value is timestamp type
	 */
	[[nodiscard]] bool isTimestamp(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::TIMESTAMP;
	}

	/**
	 * @brief Check if value is regex type
	 * @param key Key path
	 * @return true if value is regex type
	 */
	[[nodiscard]] bool isRegex(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::REGEX;
	}

	/**
	 * @brief Check if value is binary type
	 * @param key Key path
	 * @return true if value is binary type
	 */
	[[nodiscard]] bool isBinary(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::BINARY;
	}

	/**
	 * @brief Check if value is datetime type
	 * @param key Key path
	 * @return true if value is datetime type
	 */
	[[nodiscard]] bool isDateTime(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::DATETIME;
	}

	/**
	 * @brief Get ObjectId by nested path
	 * @param path Dot-separated path
	 * @return ObjectId as string or empty
	 */
	[[nodiscard]] std::string getNestedObjectId(std::string_view path) const {
		auto* v = getNested(path);
		return (v && v->type == asvJSONValue::OBJECTID) ? std::string(v->str_data.data(), v->str_data.size()) : std::string();
	}
	/**
	 * @brief Get timestamp by nested path
	 * @param path Dot-separated path
	 * @return Timestamp value or 0
	 */
	[[nodiscard]] int64_t getNestedTimestamp(std::string_view path) const {
		auto* v = getNested(path);
		return (v && v->type == asvJSONValue::TIMESTAMP) ? v->num : 0;
	}
	/**
	 * @brief Get regex by nested path
	 * @param path Dot-separated path
	 * @return Pair of (pattern, options)
	 */
	[[nodiscard]] std::pair<std::string, std::string> getNestedRegex(std::string_view path) const {
		auto* v = getNested(path);
		if (v && v->type == asvJSONValue::REGEX && v->str_data.size() > 0) {
			const char* sep = static_cast<const char*>(memchr(v->str_data.data(), '|', v->str_data.size()));
			if (sep) {
				std::string pattern(v->str_data.data(), sep - v->str_data.data());
				std::string options(sep + 1, v->str_data.data() + v->str_data.size() - sep - 1);
				return std::make_pair(pattern, options);
			}
			return std::make_pair(std::string(v->str_data.data(), v->str_data.size()), std::string());
		}
		return std::make_pair(std::string(), std::string());
	}

	/**
	 * @brief Get value by key
	 * @param key Key path
	 * @return Pointer to value or nullptr if not found
	 */
	[[nodiscard]] const asvJSONValue* get(std::string_view key) const {
		if (!root) return nullptr;
		if (key.empty()) return root;
		if (root->type != asvJSONValue::OBJECT) return nullptr;
		return root->get(key);
	}

	[[nodiscard]] asvJSONValue* get(std::string_view key) {
		return const_cast<asvJSONValue*>(static_cast<const asvJSON*>(this)->get(key));
	}

	/**
	 * @brief Get value by nested path (supports dot notation with escaping)
	 * @param path Path like "user.address.city" (use \\. for literal dots)
	 * @return Pointer to value or nullptr if path not found
	 */
	[[nodiscard]] const asvJSONValue* getNested(std::string_view path) const {
		if (!root) return nullptr;
		const asvJSONValue* current = root;
		size_t start = 0;
		while (start < path.size()) {
			size_t dot = std::string_view::npos;
			size_t i = start;
			while (i < path.size()) {
				if (path[i] == '\\' && i + 1 < path.size() && path[i + 1] == '.') {
					i += 2;
				} else if (path[i] == '.') {
					dot = i;
					break;
				} else {
					i++;
				}
			}
			std::string key;
			for (size_t j = start; j < (dot == std::string_view::npos ? path.size() : dot); j++) {
				if (path[j] == '\\' && j + 1 < path.size() && path[j + 1] == '.') {
					key += '.';
					j++;
				} else {
					key += path[j];
				}
			}
			if (current->type != asvJSONValue::OBJECT) return nullptr;
			auto it = current->obj->find(key);
			if (it == current->obj->end()) return nullptr;
			if (!it->second) return nullptr;
			current = it->second.get();
			if (dot == std::string_view::npos) break;
			start = dot + 1;
		}
		return current;
	}

	[[nodiscard]] asvJSONValue* getNested(std::string_view path) {
		return const_cast<asvJSONValue*>(static_cast<const asvJSON*>(this)->getNested(path));
	}

	/**
	 * @brief Check if object contains a key
	 * @param key Key to check
	 * @return true if key exists
	 */
	[[nodiscard]] bool hasKey(std::string_view key) const noexcept {
		if (!root || root->type != asvJSONValue::OBJECT) return false;
		return map_count(*root->obj, key) > 0;
	}

	/**
	 * @brief Remove a key from object
	 * @param key Key to remove
	 */
	inline void remove(std::string_view key) {
		if (!root || root->type != asvJSONValue::OBJECT) return;
		auto it = map_find(*root->obj, key);
		if (it != root->obj->end()) root->obj->erase(it);
	}

	/**
	 * @brief Clear document (delete root, set to null)
	 */
	void clear() { delete root; root = nullptr; }

	/**
	 * @brief Get number of keys (for objects) or elements (for arrays)
	 * @return Size or 0 if not object/array
	 */
	[[nodiscard]] size_t size() const { return root ? root->size() : 0; }

	/**
	 * @brief Get all keys in object
	 * @return Vector of key strings
	 */
	[[nodiscard]] std::vector<std::string> getKeys() const {
		if (!root || root->type != asvJSONValue::OBJECT) return {};
		std::vector<std::string> keys;
		keys.reserve(root->obj->size());
		for (const auto& [key, _] : *root->obj) keys.push_back(key);
		return keys;
	}

	/**
	 * @brief Get or create root object (creates empty object if root is not object)
	 * @return Pointer to root object
	 */
	[[nodiscard]] asvJSONValue* getObject() {
		if (!root || root->type != asvJSONValue::OBJECT) {
			delete root;
			root = asvJSONValue::makeObject();
		}
		return root;
	}

	/**
	 * @brief Get const value by key path (zero-copy)
	 * @param key Key path
	 * @return Const pointer to value or nullptr
	 */
	[[nodiscard]] const asvJSONValue* getConst(std::string_view key) const {
		if (!root) return nullptr;
		if (key.empty()) return root;
		if (root->type != asvJSONValue::OBJECT) return nullptr;
		return root->getConst(key);
	}

	/**
	 * @brief Get array element by index
	 * @param idx Element index
	 * @return Const pointer to value or nullptr
	 */
	[[nodiscard]] const asvJSONValue* getConst(size_t idx) const {
		if (!root) return nullptr;
		if (root->type != asvJSONValue::ARRAY) return nullptr;
		if (idx >= root->arr->size()) return nullptr;
		return (*root->arr)[idx].get();
	}

	/**
	 * @brief Get array value by key
	 * @param key Key path
	 * @return Pointer to array or nullptr if not found/wrong type
	 */
	[[nodiscard]] const asvJSONValue* getArray(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::ARRAY ? v : nullptr;
	}

	[[nodiscard]] asvJSONValue* getArray(std::string_view key) {
		return const_cast<asvJSONValue*>(static_cast<const asvJSON*>(this)->getArray(key));
	}

	/**
	 * @brief Get root array (if root is array)
	 * @return Pointer to root array or nullptr
	 */
	[[nodiscard]] asvJSONValue* getRootArray() const {
		return root && root->type == asvJSONValue::ARRAY ? root : nullptr;
	}

	[[nodiscard]] asvJSONValue* getRoot() { return root; }
	[[nodiscard]] const asvJSONValue* getRoot() const { return root; }

	/**
	 * @brief Get string value or default
	 * @param key Key path
	 * @param defaultValue Default value if not found
	 * @return String value or default
	 */
	[[nodiscard]] std::string optString(std::string_view key, const char* defaultValue = "") const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::STRING ? std::string(v->str_data.data(), v->str_data.size()) : defaultValue;
	}

	/**
	 * @brief Get integer value or default
	 * @param key Key path
	 * @param defaultValue Default value if not found
	 * @return Integer value or default
	 */
	[[nodiscard]] int64_t optInt(std::string_view key, int64_t defaultValue = 0) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::INT ? v->num : defaultValue;
	}

	/**
	 * @brief Get double value or default
	 * @param key Key path
	 * @param defaultValue Default value if not found
	 * @return Double value or default
	 */
	[[nodiscard]] double optDouble(std::string_view key, double defaultValue = 0.0) const {
		auto* v = get(key);
		if (!v) return defaultValue;
		if (v->type == asvJSONValue::DOUBLE) return v->dbl;
		if (v->type == asvJSONValue::INT) return static_cast<double>(v->num);
		return defaultValue;
	}

	/**
	 * @brief Get boolean value or default
	 * @param key Key path
	 * @param defaultValue Default value if not found
	 * @return Boolean value or default
	 */
	[[nodiscard]] bool optBool(std::string_view key, bool defaultValue = false) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::BOOL_VAL ? v->flag : defaultValue;
	}

	/**
	 * @brief Check if value is null
	 * @param key Key path
	 * @return true if value is null type
	 */
	[[nodiscard]] bool isNull(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::NULL_VAL;
	}

	[[nodiscard]] bool isString(std::string_view key) const { auto* v = get(key); return v && v->type == asvJSONValue::STRING; }
	[[nodiscard]] bool isInt(std::string_view key) const { auto* v = get(key); return v && v->type == asvJSONValue::INT; }
	[[nodiscard]] bool isDouble(std::string_view key) const { auto* v = get(key); return v && v->type == asvJSONValue::DOUBLE; }
	[[nodiscard]] bool isBool(std::string_view key) const { auto* v = get(key); return v && v->type == asvJSONValue::BOOL_VAL; }
	[[nodiscard]] bool isObject(std::string_view key) const { auto* v = get(key); return v && v->type == asvJSONValue::OBJECT; }
	[[nodiscard]] bool isArray(std::string_view key) const { auto* v = get(key); return v && v->type == asvJSONValue::ARRAY; }

	/**
	 * @brief Get datetime as ISO 8601 string
	 * @param key Key path
	 * @return DateTime string or empty
	 */
	[[nodiscard]] std::string getDateTimeString(std::string_view key) const {
		auto* v = get(key);
		if (v && v->type == asvJSONValue::DATETIME) {
			char buf[40];
			std::tm tm;
			asvjson_gmtime(&tm, &v->timestamp);
			if (v->datetime_ms > 0) {
				std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:", &tm);
				std::string result = buf;
				result += std::to_string(tm.tm_sec);
				result += '.';
				char msbuf[16];
				snprintf(msbuf, sizeof(msbuf), "%03d", v->datetime_ms);
				result += msbuf;
				result += "Z";
				return result;
			}
			std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
			return buf;
		}
		return std::string();
	}

	/**
	 * @brief Get datetime milliseconds
	 * @param key Key path
	 * @return Milliseconds (0-999) or 0
	 */
	[[nodiscard]] int getDateTimeMs(std::string_view key) const noexcept {
		auto* v = get(key);
		return v && v->type == asvJSONValue::DATETIME ? v->getDateTimeMs() : 0;
	}

	/**
	 * @brief Get datetime timestamp or default
	 * @param key Key path
	 * @param defaultValue Default timestamp
	 * @return Timestamp or default
	 */
	[[nodiscard]] time_t optDateTime(std::string_view key, time_t defaultValue = 0) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::DATETIME ? v->getDateTime() : defaultValue;
	}

	/**
	 * @brief Get datetime as tm struct or default
	 * @param key Key path
	 * @param defaultValue Default tm struct
	 * @return tm struct or default
	 */
	[[nodiscard]] std::tm optDateTimeTM(std::string_view key, const std::tm& defaultValue = std::tm{}) const {
		auto* v = get(key);
		if (!v || v->type != asvJSONValue::DATETIME) return defaultValue;
		std::tm tm = {};
		int64_t ts = v->getDateTime();
		asvjson_localtime(&tm, &ts);
		return tm;
	}

	/**
	 * @brief Internal helper for arrayAdd* methods
	 */
	template<typename F>
	void arrayAddValue(std::string_view key, F&& factory) {
		if (!root || root->type != asvJSONValue::OBJECT) { delete root; root = asvJSONValue::makeObject(); }
		if (!root) return;
		auto it = map_find(*root->obj, key);
		asvJSONValue* arr;
		if (it == root->obj->end()) {
			arr = asvJSONValue::makeArray();
			if (!arr) return;
			root->obj->emplace(std::string(key), std::unique_ptr<asvJSONValue>(arr));
		} else {
			arr = it->second.get();
			if (!arr || arr->type != asvJSONValue::ARRAY) {
				arr = asvJSONValue::makeArray();
				if (!arr) { it->second.reset(); return; }
				it->second.reset(arr);
			}
		}
		if (!arr || !arr->arr) return;
		auto* v = factory();
		if (!v) return;
		arr->arr->push_back(std::unique_ptr<asvJSONValue>(v));
	}

	/**
	 * @brief Add string to array by key (creates array if needed)
	 * @param key Key path
	 * @param value String value
	 */
	void arrayAddString(std::string_view key, const char* value) {
		if (!value) return;
		arrayAddValue(key, [value]{ return asvJSONValue::makeString(value, strlen(value)); });
	}
	/**
	 * @brief Add integer to array by key (creates array if needed)
	 * @param key Key path
	 * @param value Integer value
	 */
	void arrayAddInt(std::string_view key, int64_t value) {
		arrayAddValue(key, [value]{ return asvJSONValue::makeInt(value); });
	}
	/**
	 * @brief Add double to array by key (creates array if needed)
	 * @param key Key path
	 * @param value Double value
	 */
	void arrayAddDouble(std::string_view key, double value) {
		arrayAddValue(key, [value]{ return asvJSONValue::makeDouble(value); });
	}

	/**
	 * @brief Add boolean to array by key (creates array if needed)
	 * @param key Key path
	 * @param value Boolean value
	 */
	void arrayAddBool(std::string_view key, bool value) {
		arrayAddValue(key, [value]{ return asvJSONValue::makeBool(value); });
	}

	/**
	 * @brief Add null to array by key (creates array if needed)
	 * @param key Key path
	 */
	void arrayAddNull(std::string_view key) {
		arrayAddValue(key, []{ return asvJSONValue::makeNull(); });
	}

	/**
	 * @brief Add datetime to array by key (creates array if needed)
	 * @param key Key path
	 * @param value Timestamp value
	 */
	void arrayAddDateTime(std::string_view key, time_t value) {
		arrayAddValue(key, [value]{ return asvJSONValue::makeDateTime(value); });
	}

	/**
	 * @brief Get last error message
	 * @return Error string
	 */
	[[nodiscard]] std::string getLastError() const { return lastError; }

	/**
	 * @brief Convert type enum to string
	 * @param type Value type
	 * @return Type name string
	 */
	[[nodiscard]] static std::string typeToString(asvJSONValue::Type type) {
		return std::string(asvJSONValue::typeToString(type));
	}

	std::vector<uint8_t> toMessagePack() const;
	/**
	 * @brief Parse MessagePack binary format
	 * @param data Pointer to MessagePack data
	 * @param size Size of data in bytes
	 * @return true on success, false on error (see lastError)
	 */
	bool fromMessagePack(const uint8_t* data, size_t size);
	/**
	 * @brief Serialize to BSON binary format
	 * @return BSON-encoded bytes
	 */
	std::vector<uint8_t> toBSON() const;
	/**
	 * @brief Serialize to XML string
	 * @return XML document
	 */
	std::string toXML() const;
	/**
	 * @brief Serialize to YAML string
	 * @return YAML document
	 */
	std::string toYAML() const;
	/**
	 * @brief Serialize to CSV string
	 * @return CSV document
	 */
	std::string toCSV() const;
	/**
	 * @brief Parse BSON binary format
	 * @param data Pointer to BSON data
	 * @param size Size of data in bytes
	 * @return true on success, false on error (see lastError)
	 */
	bool fromBSON(const uint8_t* data, size_t size);
	/**
	 * @brief Get value by JSON Pointer (RFC 6901)
	 * @param pointer JSON Pointer string
	 * @return Pointer to value or nullptr
	 */
	// Helper: check if a string can be interpreted as a non-negative integer (for JSON Pointer array indices)
	static bool isArrayIndex(const std::string& s) {
		if (s.empty()) return false;
		for (char c : s) {
			if (c < '0' || c > '9') return false;
		}
		// Check it's not too large (within size_t range)
		errno = 0;
		char* end;
		(void)strtoul(s.c_str(), &end, 10);
		return (errno != ERANGE && *end == '\0' && end == s.c_str() + s.length());
	}

	/**
	 * @brief Get value by JSON Pointer (const)
	 * @param pointer JSON Pointer string (RFC 6901)
	 * @return Const pointer to value, or nullptr
	 */
	const asvJSONValue* getByPointer(std::string_view pointer) const;
	/**
	 * @brief Get value by JSON Pointer (mutable)
	 * @param pointer JSON Pointer string (RFC 6901)
	 * @return Pointer to value, or nullptr
	 */
	asvJSONValue* getByPointer(std::string_view pointer);

	/**
	 * @brief Set value by JSON Pointer (creates ancestors)
	 * @param pointer JSON Pointer string
	 * @param value Value to set (takes ownership)
	 * @return true on success
	 */
	bool setByPointer(std::string_view pointer, asvJSONValue* value);

	/**
	 * @brief Remove value by JSON Pointer
	 * @param pointer JSON Pointer string
	 * @return true on success
	 */
	bool removeByPointer(std::string_view pointer);
	/**
	 * @brief Merge another object's key-value pairs into this object (in-place)
	 * @param other Object to merge from (only OBJECT type is processed)
	 * @note Values from 'other' override existing values; null values remove keys
	 */
	void merge(const asvJSON& other);
	/**
	 * @brief Apply JSON Patch (RFC 6902) in-place
	 * @param patch JSON Patch document
	 * @return true on success, false on error (see lastError)
	 * @note Supports: add, remove, replace, move, copy, test, operation
	 */
	bool applyPatch(const asvJSON& patch);
	/**
	 * @brief Apply JSON Merge Patch (RFC 7396) and return new document
	 * @param patch Merge patch document
	 * @return New JSON document with patch applied
	 */
	asvJSON applyMergePatch(const asvJSON& patch) const;

	/**
	 * @brief Convert JSON string to MessagePack bytes
	 * @param json JSON string
	 * @return MessagePack-encoded bytes
	 */
	static std::vector<uint8_t> messagePackFromString(const std::string& json) {
		asvJSON j;
		if (!j.parse(json)) return std::vector<uint8_t>();
		return j.toMessagePack();
	}
	/**
	 * @brief Parse MessagePack bytes and return JSON string
	 * @param data Pointer to MessagePack data
	 * @param len Length of data
	 * @return JSON string representation
	 */
	static std::string stringFromMessagePack(const uint8_t* data, size_t len) {
		asvJSON j;
		if (!j.fromMessagePack(data, len)) return std::string();
		return j.serialize();
	}
	/**
	 * @brief Parse MessagePack from string
	 * @param data MessagePack string
	 * @return true on success
	 */
	inline bool fromMessagePack(const std::string& data) {
		return data.empty() ? false : fromMessagePack(reinterpret_cast<const uint8_t*>(data.data()), data.size());
	}
	/**
	 * @brief Parse BSON from string
	 * @param data BSON string
	 * @return true on success
	 */
	inline bool fromBSON(const std::string& data) {
		return data.empty() ? false : fromBSON(reinterpret_cast<const uint8_t*>(data.data()), data.size());
	}
};

inline std::vector<uint8_t> asvJSON::toMessagePack() const {
	std::vector<uint8_t> out;
	if (!root) return out;
	root->toMessagePack(out);
	return out;
}

/**
 * @brief Read 64-bit little-endian integer
 * @param data Pointer to data
 * @return Integer value
 */
inline uint64_t readLE64(const uint8_t* data) {
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) v |= (static_cast<uint64_t>(data[i]) << (i * 8));
	return v;
}

/**
 * @brief Read 64-bit little-endian double
 * @param data Pointer to data
 * @return Double value
 */
inline double readLE64_double(const uint8_t* data) {
	uint64_t u = readLE64(data);
	double d;
	memcpy(&d, &u, sizeof(d));
	return d;
}

/**
 * @brief Parse MessagePack to asvJSONValue
 * @param data Pointer to MessagePack data
 * @param pos Current position (updated)
 * @param dataLen Total data length
 * @param depth Current nesting depth
 * @return Parsed value or nullptr
 */
inline asvJSONValue* parseMessagePack(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth = 0) {
	if (pos >= dataLen) return nullptr;
	if (depth > asvJSONValue::MAX_NESTING_DEPTH) return nullptr;
	uint8_t type = data[pos++];

	if (type == 0xC0) return asvJSONValue::makeNull();
	if (type == 0xC2) return asvJSONValue::makeBool(false);
	if (type == 0xC3) return asvJSONValue::makeBool(true);
	if (type <= 0x7F) return asvJSONValue::makeInt(type);
	if (type >= 0xE0) return asvJSONValue::makeInt(static_cast<int8_t>(type));
	if (type >= 0xA0 && type <= 0xBF) {
		size_t strLen = type & 0x1F;
		if (pos + strLen > dataLen) return nullptr;
		auto* v = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), strLen);
		pos += strLen;
		return v;
	}

	if (type == 0xCA) {
		if (pos + 4 > dataLen) return nullptr;
		uint32_t bits = (static_cast<uint32_t>(data[pos]) << 24) |
						(static_cast<uint32_t>(data[pos + 1]) << 16) |
						(static_cast<uint32_t>(data[pos + 2]) << 8) |
						static_cast<uint32_t>(data[pos + 3]);
		pos += 4;
		float f;
		std::memcpy(&f, &bits, sizeof(f));
		auto* v = asvJSONValue::makeDouble(static_cast<double>(f));
		v->is_float32 = true;
		return v;
	}
	if (type == 0xCB) {
		if (pos + 8 > dataLen) return nullptr;
		uint64_t bits = 0;
		for (int i = 7; i >= 0; i--) bits = (bits << 8) | static_cast<uint64_t>(data[pos++]);
		double d;
		std::memcpy(&d, &bits, sizeof(d));
		return asvJSONValue::makeDouble(d);
	}

	if (type >= 0x90 && type <= 0x9F) {
		size_t count = type & 0x0F;
		if (pos + count > dataLen) return nullptr;
		auto* arr = asvJSONValue::makeArray();
		if (!arr) return nullptr;
		for (size_t i = 0; i < count; i++) {
			auto* v = parseMessagePack(data, pos, dataLen, depth + 1);
			if (!v) { delete arr; return nullptr; }
			arr->arr->push_back(std::unique_ptr<asvJSONValue>(v));
		}
		return arr;
	}

	if (type == 0xDC) {
		if (pos + 2 > dataLen) return nullptr;
		size_t arrLen = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
		pos += 2;
		if (arrLen > asvJSONValue::MAX_ARRAY_SIZE) return nullptr;
		auto* arr = asvJSONValue::makeArray();
		if (!arr) return nullptr;
		for (uint16_t i = 0; i < arrLen; i++) {
			auto* v = parseMessagePack(data, pos, dataLen, depth + 1);
			if (!v) { delete arr; return nullptr; }
			arr->arr->push_back(std::unique_ptr<asvJSONValue>(v));
		}
		return arr;
	}

	if (type == 0xDD) {
		if (pos + 4 > dataLen) return nullptr;
		uint32_t arrLen = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
		pos += 4;
		if (arrLen > asvJSONValue::MAX_ARRAY_SIZE) return nullptr;
		auto* arr = asvJSONValue::makeArray();
		if (!arr) return nullptr;
		for (uint32_t i = 0; i < arrLen; i++) {
			auto* v = parseMessagePack(data, pos, dataLen, depth + 1);
			if (!v) { delete arr; return nullptr; }
			arr->arr->push_back(std::unique_ptr<asvJSONValue>(v));
		}
		return arr;
	}

	if (type == 0xDE) {
		if (pos + 2 > dataLen) return nullptr;
		size_t mapLen = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
		pos += 2;
		if (mapLen > asvJSONValue::MAX_OBJECT_SIZE) return nullptr;
		auto* obj = asvJSONValue::makeObject();
		if (!obj) return nullptr;
		for (uint16_t i = 0; i < mapLen; i++) {
			std::unique_ptr<asvJSONValue> key(parseMessagePack(data, pos, dataLen, depth + 1));
			if (!key || key->type != asvJSONValue::STRING) { delete obj; return nullptr; }
			std::unique_ptr<asvJSONValue> val(parseMessagePack(data, pos, dataLen, depth + 1));
			if (!val) { delete obj; return nullptr; }
			obj->obj->emplace(std::string(key->str_data.data(), key->str_data.size()), std::move(val));
		}
		return obj;
	}

	if (type == 0xD9) {
		if (pos >= dataLen) return nullptr;
		size_t strLen = data[pos++];
		if (strLen > asvJSONValue::MAX_STRING_LEN || pos + strLen > dataLen) return nullptr;
		auto* v = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), strLen);
		pos += strLen;
		return v;
	}

	if (type == 0xDA) {
		if (pos + 2 > dataLen) return nullptr;
		size_t strLen = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
		pos += 2;
		if (strLen > asvJSONValue::MAX_STRING_LEN || pos + strLen > dataLen) return nullptr;
		auto* v = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), strLen);
		pos += strLen;
		return v;
	}

	if (type == 0xDB) {
		if (pos + 4 > dataLen) return nullptr;
		size_t strLen = (static_cast<size_t>(data[pos]) << 24) | (static_cast<size_t>(data[pos + 1]) << 16) | (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
		pos += 4;
		if (strLen > asvJSONValue::MAX_STRING_LEN || pos + strLen > dataLen) return nullptr;
		auto* v = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), strLen);
		pos += strLen;
		return v;
	}

	if (type == 0xD0) {
		if (pos >= dataLen) return nullptr;
		int8_t n = static_cast<int8_t>(data[pos++]);
		return asvJSONValue::makeInt(n);
	}

	if (type == 0xD1) {
		if (pos + 2 > dataLen) return nullptr;
		int16_t n = (data[pos] << 8) | data[pos + 1];
		pos += 2;
		return asvJSONValue::makeInt(n);
	}

	if (type == 0xD2) {
		if (pos + 4 > dataLen) return nullptr;
		int32_t n = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
		pos += 4;
		return asvJSONValue::makeInt(n);
	}

	if (type == 0xD3) {
		if (pos + 8 > dataLen) return nullptr;
		uint64_t n = 0;
		for (int i = 7; i >= 0; i--) n = (n << 8) | static_cast<uint64_t>(data[pos++]);
		return asvJSONValue::makeInt(static_cast<int64_t>(n));
	}

	if (type == 0xCC) {
		if (pos >= dataLen) return nullptr;
		uint8_t n = data[pos++];
		return asvJSONValue::makeInt(n);
	}

	if (type == 0xCD) {
		if (pos + 2 > dataLen) return nullptr;
		uint16_t n = (data[pos] << 8) | data[pos + 1];
		pos += 2;
		return asvJSONValue::makeInt(n);
	}

	if (type == 0xCE) {
		if (pos + 4 > dataLen) return nullptr;
		uint32_t n = (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
		pos += 4;
		return asvJSONValue::makeInt(n);
	}

	if (type == 0xCF) {
		if (pos + 8 > dataLen) return nullptr;
		uint64_t n = 0;
		for (int i = 7; i >= 0; i--) n = (n << 8) | data[pos++];
		if (n > static_cast<uint64_t>(INT64_MAX)) {
			return asvJSONValue::makeDouble(static_cast<double>(n));
		}
		return asvJSONValue::makeInt(static_cast<int64_t>(n));
	}

	if (type >= 0x80 && type <= 0x8F) {
		auto* obj = asvJSONValue::makeObject();
		if (!obj) return nullptr;
		int count = type & 0x0F;
		for (int i = 0; i < count; i++) {
			std::unique_ptr<asvJSONValue> key(parseMessagePack(data, pos, dataLen, depth + 1));
			if (!key || key->type != asvJSONValue::STRING) { delete obj; return nullptr; }
			std::unique_ptr<asvJSONValue> val(parseMessagePack(data, pos, dataLen, depth + 1));
			if (!val) { delete obj; return nullptr; }
			obj->obj->emplace(std::string(key->str_data.data(), key->str_data.size()), std::move(val));
		}
		return obj;
	}

	if (type == 0xDF) {
		if (pos + 4 > dataLen) return nullptr;
		size_t objLen = (static_cast<size_t>(data[pos]) << 24) | (static_cast<size_t>(data[pos + 1]) << 16) | (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
		pos += 4;
		if (objLen > asvJSONValue::MAX_OBJECT_SIZE) return nullptr;
		auto* obj = asvJSONValue::makeObject();
		if (!obj) return nullptr;
		for (size_t i = 0; i < objLen; i++) {
			std::unique_ptr<asvJSONValue> key(parseMessagePack(data, pos, dataLen, depth + 1));
			if (!key || key->type != asvJSONValue::STRING) { delete obj; return nullptr; }
			std::unique_ptr<asvJSONValue> val(parseMessagePack(data, pos, dataLen, depth + 1));
			if (!val) { delete obj; return nullptr; }
			obj->obj->emplace(std::string(key->str_data.data(), key->str_data.size()), std::move(val));
		}
		return obj;
	}

	if (type == 0xD6) {
		if (pos + 5 > dataLen) return nullptr;
		uint8_t extType = data[pos++];
		if (extType != 0xFF) {
			auto* v = asvJSONValue::makeExtension(static_cast<int8_t>(extType), data + pos, 4);
			if (!v) return nullptr;
			pos += 4;
			return v;
		}
		uint32_t sec = 0;
		for (int i = 3; i >= 0; i--) sec = (sec << 8) | data[pos++];
		return asvJSONValue::makeDateTime(sec, 0);
	}

	if (type == 0xD7) {
		if (pos + 9 > dataLen) return nullptr;
		uint8_t extType = data[pos++];
		if (extType == 3) {
			// Timestamp (custom roundtrip)
			int64_t ts = 0;
			for (int i = 7; i >= 0; i--) ts = (ts << 8) | static_cast<int64_t>(data[pos++]);
			return asvJSONValue::makeTimestamp(ts);
		}
		if (extType != 0xFF) {
			auto* v = asvJSONValue::makeExtension(static_cast<int8_t>(extType), data + pos, 8);
			if (!v) return nullptr;
			pos += 8;
			return v;
		}
		uint64_t data64 = 0;
		for (int i = 7; i >= 0; i--) data64 = (data64 << 8) | data[pos++];
		uint64_t sec = data64 & 0x3FFFFFFFFLL;
		uint32_t ns = static_cast<uint32_t>((data64 >> 34) & 0x3FFFFFFF);
		if (ns >= 1000000000) return nullptr;
		return asvJSONValue::makeDateTime(sec, ns / 1000000);
	}

	if (type == 0xD8) {
		if (pos + 17 > dataLen) return nullptr;
		uint8_t extType = data[pos++];
		if (extType == 1) {
			auto* v = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
			if (!v) return nullptr;
			pos += 16;
			return v;
		}
		if (extType != 0xFF) {
			auto* v = asvJSONValue::makeExtension(static_cast<int8_t>(extType), data + pos, 16);
			if (!v) return nullptr;
			pos += 16;
			return v;
		}
		uint64_t sec = 0;
		for (int i = 7; i >= 0; i--) sec = (sec << 8) | data[pos++];
		uint32_t ns = 0;
		for (int i = 3; i >= 0; i--) ns = (ns << 8) | data[pos++];
		if (ns >= 1000000000) return nullptr;
		return asvJSONValue::makeDateTime(sec, ns / 1000000);
	}

	if (type == 0xC7) {
		if (pos + 2 > dataLen) return nullptr;
		uint8_t len = data[pos++];
		int8_t extType = static_cast<int8_t>(data[pos++]);
		if (len == 12 && extType == -1) {
			if (pos + 12 > dataLen) return nullptr;
			uint32_t ns = 0;
			for (int i = 3; i >= 0; i--) ns = (ns << 8) | data[pos++];
			uint64_t sec = 0;
			for (int i = 7; i >= 0; i--) sec = (sec << 8) | data[pos++];
			if (ns >= 1000000000) return nullptr;
			return asvJSONValue::makeDateTime(sec, ns / 1000000);
		}
		if (extType == 1) {
			// ObjectId
			if (len != 12 || pos + 12 > dataLen) return nullptr;
			auto* v = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
			if (!v) return nullptr;
			pos += 12;
			return v;
		}
		if (extType == 2) {
			// Regex: stored as pattern|options
			if (pos + len > dataLen) return nullptr;
			std::string s(reinterpret_cast<const char*>(data + pos), len);
			pos += len;
			size_t sep = s.find('|');
			const char* optPtr = nullptr;
			if (sep != std::string::npos) {
				s[sep] = '\0';
				if (sep + 1 < s.size()) optPtr = s.c_str() + sep + 1;
			}
			auto* v = asvJSONValue::makeRegex(sep != std::string::npos ? s.c_str() : s.c_str(), optPtr);
			if (!v) return nullptr;
			return v;
		}
		if (extType == 3) {
			// Timestamp: stored as 8-byte int64
			if (len < 8 || pos + 8 > dataLen) return nullptr;
			int64_t ts = 0;
			for (int i = 7; i >= 0; i--) ts = (ts << 8) | static_cast<int64_t>(data[pos++]);
			auto* v = asvJSONValue::makeTimestamp(ts);
			if (!v) return nullptr;
			return v;
		}
		if (pos + len > dataLen) return nullptr;
		auto* v = asvJSONValue::makeExtension(extType, data + pos, len);
		if (!v) return nullptr;
		pos += len;
		return v;
	}

	if (type == 0xD4) {
			if (pos + 2 > dataLen) return nullptr;
			int8_t extType = static_cast<int8_t>(data[pos++]);
			auto* v = asvJSONValue::makeExtension(extType, data + pos, 1);
			if (!v) return nullptr;
			pos++;
			return v;
		}

		if (type == 0xD5) {
			if (pos + 3 > dataLen) return nullptr;
			int8_t extType = static_cast<int8_t>(data[pos++]);
			auto* v = asvJSONValue::makeExtension(extType, data + pos, 2);
			if (!v) return nullptr;
			pos += 2;
			return v;
		}

	if (type == 0xC4) {
		if (pos >= dataLen) return nullptr;
		uint8_t binLen = data[pos++];
		if (pos + binLen > dataLen) return nullptr;
		auto* v = asvJSONValue::makeBinary(data + pos, binLen);
		pos += binLen;
		return v;
	}

	if (type == 0xC5) {
		if (pos + 2 > dataLen) return nullptr;
		size_t binLen = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
		pos += 2;
		if (binLen > asvJSONValue::MAX_STRING_LEN || pos + binLen > dataLen) return nullptr;
		auto* v = asvJSONValue::makeBinary(data + pos, binLen);
		pos += binLen;
		return v;
	}

	if (type == 0xC6) {
		if (pos + 4 > dataLen) return nullptr;
		size_t binLen = (static_cast<size_t>(data[pos]) << 24) | (static_cast<size_t>(data[pos + 1]) << 16) | (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
		pos += 4;
		if (binLen > asvJSONValue::MAX_STRING_LEN || pos + binLen > dataLen) return nullptr;
		auto* v = asvJSONValue::makeBinary(data + pos, binLen);
		pos += binLen;
		return v;
	}

	if (type == 0xC8) {
		if (pos + 2 > dataLen) return nullptr;
		uint16_t len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
		pos += 2;
		if (pos + 1 + len > dataLen) return nullptr;
		int8_t extType = static_cast<int8_t>(data[pos++]);
		if (extType == 1) {
			// ObjectId
			if (len != 12 || pos + 12 > dataLen) return nullptr;
			auto* v = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
			if (!v) return nullptr;
			pos += 12;
			return v;
		}
		if (extType == 2) {
			if (pos + len > dataLen) return nullptr;
			std::string s(reinterpret_cast<const char*>(data + pos), len);
			pos += len;
			size_t sep = s.find('|');
			const char* optPtr = nullptr;
			if (sep != std::string::npos) {
				s[sep] = '\0';
				if (sep + 1 < s.size()) optPtr = s.c_str() + sep + 1;
			}
			auto* v = asvJSONValue::makeRegex(sep != std::string::npos ? s.c_str() : s.c_str(), optPtr);
			if (!v) return nullptr;
			return v;
		}
		if (pos + len > dataLen) return nullptr;
		auto* v = asvJSONValue::makeExtension(extType, data + pos, len);
		if (!v) return nullptr;
		pos += len;
		return v;
	}
	if (type == 0xC9) {
		if (pos + 4 > dataLen) return nullptr;
		uint32_t len = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
		pos += 4;
		if (len > static_cast<uint32_t>(asvJSONValue::MAX_STRING_LEN) || pos + 1 + len > dataLen) return nullptr;
		int8_t extType = static_cast<int8_t>(data[pos++]);
		if (extType == 1) {
			if (len != 12 || pos + 12 > dataLen) return nullptr;
			auto* v = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
			if (!v) return nullptr;
			pos += 12;
			return v;
		}
		if (extType == 2) {
			if (pos + len > dataLen) return nullptr;
			std::string s(reinterpret_cast<const char*>(data + pos), len);
			pos += len;
			size_t sep = s.find('|');
			const char* optPtr = nullptr;
			if (sep != std::string::npos) {
				s[sep] = '\0';
				if (sep + 1 < s.size()) optPtr = s.c_str() + sep + 1;
			}
			auto* v = asvJSONValue::makeRegex(sep != std::string::npos ? s.c_str() : s.c_str(), optPtr);
			if (!v) return nullptr;
			return v;
		}
		if (pos + len > dataLen) return nullptr;
		auto* v = asvJSONValue::makeExtension(extType, data + pos, len);
		if (!v) return nullptr;
		pos += len;
		return v;
	}

	return nullptr;
}

inline bool asvJSON::fromMessagePack(const uint8_t* data, size_t size) {
	delete root;
	root = nullptr;
	if (!data || size == 0) return false;
	size_t pos = 0;
	root = parseMessagePack(data, pos, size);
	if (!root || pos != size) {
		delete root;
		root = nullptr;
		lastError = pos != size ? "Trailing bytes" : "Parse failed";
		return false;
	}
	return true;
}

inline std::vector<uint8_t> asvJSON::toBSON() const {
	std::vector<uint8_t> out;
	if (!root) return out;
	root->toBSON(out);
	return out;
}

inline std::string asvJSON::toXML() const {
	std::string out;
	if (!root) { out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root/>\n"; return out; }
	out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	if (root->type == asvJSONValue::OBJECT) {
		out += "<root>\n";
		for (const auto& [key, val] : *root->obj) {
			val->toXML(out, xmlSanitizeElementName(key), 1);
		}
		out += "</root>\n";
	} else if (root->type == asvJSONValue::ARRAY) {
		out += "<root>\n";
		for (const auto& v : *root->arr) {
			v->toXML(out, "item", 1);
		}
		out += "</root>\n";
	} else {
		root->toXML(out, "root", 0);
	}
	return out;
}

inline std::string asvJSON::toYAML() const {
	std::string out;
	if (!root) { out += "null\n"; return out; }
	out += "---\n";
	if (root->type == asvJSONValue::OBJECT) {
		for (const auto& [k, v] : *root->obj)
			v->toYAML(out, 0, k, false);
	} else if (root->type == asvJSONValue::ARRAY) {
		for (const auto& v : *root->arr)
			v->toYAML(out, 0, "", true);
	} else {
		root->toYAML(out, 0, "", false);
	}
	return out;
}

// ---------- CSV helpers ----------
/**
 * @brief Escape a string for CSV output
 * @param s Input string view
 * @return CSV-escaped string
 */
inline std::string csvEscape(std::string_view s) {
	if (!s.empty() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@')) {
		return "'" + std::string(s);
	}
	bool need = false;
	for (auto c : s) if (c == ',' || c == '"' || c == '\n' || c == '\r') { need = true; break; }
	if (!need) return std::string(s);
	std::string out; out += '"';
	for (auto c : s) { if (c == '"') out += "\"\""; else out += c; }
	out += '"';
	return out;
}

inline std::string asvJSON::toCSV() const {
	std::string out;
	if (root) root->toCSV(out);
	return out;
}

inline void asvJSONValue::toCSV(std::string& out) const {
	auto cellValue = [](const asvJSONValue* v) -> std::string {
		if (!v) return {};
		using T = asvJSONValue::Type;
		switch (v->type) {
			case T::NULL_VAL: return {};
			case T::BOOL_VAL: return v->flag ? "true" : "false";
			case T::INT: return std::to_string(v->num);
			case T::DOUBLE: {
				if (std::isnan(v->dbl) || std::isinf(v->dbl)) return {};
				char buf[32]; int n = snprintf(buf, sizeof(buf), "%.17g", v->dbl);
				return (n > 0) ? std::string(buf, static_cast<size_t>(n)) : std::string{};
			}
			case T::STRING: return v->str_data;
			case T::DATETIME: {
				char buf[40]; std::tm tm; asvjson_gmtime(&tm, &v->timestamp);
				char ms[16] = ""; if (v->datetime_ms > 0) snprintf(ms, sizeof(ms), ".%03d", v->datetime_ms);
				if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm) == 0) return {};
				return std::string(buf) + ms + "Z";
			}
			case T::BINARY: return base64_encode(v->bin_data.data(), v->bin_data.size());
			case T::OBJECTID: {
				std::string hex;
				for (size_t i = 0; i < v->str_data.size() && i < 12; i++) {
					char hb[4]; snprintf(hb, sizeof(hb), "%02x", static_cast<unsigned char>(v->str_data[i])); hex += hb;
				}
				return hex;
			}
			case T::REGEX: {
				size_t sep = v->str_data.find('|');
				return (sep != std::string_view::npos) ? std::string(v->str_data.data(), sep) : v->str_data;
			}
			case T::TIMESTAMP: return std::to_string(v->num);
			case T::EXTENSION: return base64_encode(v->bin_data.data(), v->bin_data.size());
			case T::ARRAY:
			case T::OBJECT: {
				std::string s;
				v->serialize(s, false);
				return s;
			}
			default: return {};
		}
	};
	using T = asvJSONValue::Type;

	// Shared: write array of objects as CSV rows
	auto writeArrayOfObjects = [&](const std::vector<std::unique_ptr<asvJSONValue>>& items) {
		if (items.empty()) return;
		std::map<std::string, bool> keyMap;
		std::unordered_set<const asvJSONValue*> visited;
		auto collect = [&](auto&& self, const asvJSONValue* v, const std::string& prefix,
						   std::unordered_set<const asvJSONValue*>& vis) -> void {
			if (!v) return;
			if (vis.count(v)) return;
			vis.insert(v);
			if (v->type == T::OBJECT && v->obj) {
				for (const auto& [k, val] : *v->obj)
					self(self, val.get(), prefix.empty() ? k : prefix + "." + k, vis);
			} else { keyMap[prefix.empty() ? "value" : prefix] = true; }
		};
		for (const auto& item : items) collect(collect, item.get(), "", visited);
		std::vector<std::string> keys; keys.reserve(keyMap.size());
		for (const auto& [k, _] : keyMap) keys.push_back(k);
		for (size_t i = 0; i < keys.size(); i++) { if (i > 0) out += ','; out += csvEscape(keys[i]); }
		out += '\n';
		for (const auto& item : items) {
			std::map<std::string, std::string> flat;
			std::unordered_set<const asvJSONValue*> fillVisited;
			auto fill = [&](auto&& self, const asvJSONValue* v, const std::string& prefix,
							std::unordered_set<const asvJSONValue*>& vis) -> void {
				if (!v) return;
				if (vis.count(v)) return;
				vis.insert(v);
				if (v->type == T::OBJECT && v->obj) {
					for (const auto& [k, val] : *v->obj)
						self(self, val.get(), prefix.empty() ? k : prefix + "." + k, vis);
				} else { flat[prefix.empty() ? "value" : prefix] = cellValue(v); }
			};
			fill(fill, item.get(), "", fillVisited);
			for (size_t i = 0; i < keys.size(); i++) {
				if (i > 0) out += ',';
				auto it = flat.find(keys[i]);
				out += csvEscape(it != flat.end() ? it->second : "");
			}
			out += '\n';
		}
	};

	switch (type) {
		case T::OBJECT: {
			if (!obj || obj->empty()) return;
			// If single key maps to an array of objects - unwrap
			if (obj->size() == 1) {
				const auto& [onlyKey, onlyVal] = *obj->begin();
				if (onlyVal->type == T::ARRAY && onlyVal->arr && !onlyVal->arr->empty()) {
					bool allObjs = true;
					for (const auto& v : *onlyVal->arr)
						if (v->type != T::OBJECT) { allObjs = false; break; }
					if (allObjs) { writeArrayOfObjects(*onlyVal->arr); break; }
				}
			}
			// Otherwise: flat single row
			std::map<std::string, std::string> flat;
			std::unordered_set<const asvJSONValue*> flattenVisited;
			auto flatten = [&](auto&& self, const asvJSONValue* v, const std::string& prefix,
							   std::unordered_set<const asvJSONValue*>& vis) -> void {
				if (!v) return;
				if (vis.count(v)) return;
				vis.insert(v);
				if (v->type == T::OBJECT && v->obj) {
					for (const auto& [k, val] : *v->obj)
						self(self, val.get(), prefix.empty() ? k : prefix + "." + k, vis);
				} else { flat[prefix.empty() ? "value" : prefix] = cellValue(v); }
			};
			flatten(flatten, this, "", flattenVisited);
			bool first = true;
			for (const auto& [k, _] : flat) { if (!first) out += ','; out += csvEscape(k); first = false; }
			out += '\n';
			first = true;
			for (const auto& [_, v] : flat) { if (!first) out += ','; out += csvEscape(v); first = false; }
			out += '\n';
			break;
		}
		case T::ARRAY: {
			if (!arr || arr->empty()) return;
			bool allObjects = true;
			for (const auto& v : *arr)
				if (v->type != T::OBJECT) { allObjects = false; break; }
			if (allObjects) {
				writeArrayOfObjects(*arr);
			} else {
				out += "value\n";
				for (const auto& v : *arr) { out += csvEscape(cellValue(v.get())); out += '\n'; }
			}
			break;
		}
		default:
			out += csvEscape(cellValue(this)); out += '\n';
			break;
	}
}

inline asvJSONValue* parseBSON(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth);

/**
 * @brief Parse BSON binary format
 * @param data Pointer to BSON data
 * @param size Size of data in bytes
 * @return true on success, false on error (see lastError)
 */
inline bool asvJSON::fromBSON(const uint8_t* data, size_t size) {
	delete root;
	root = nullptr;
	if (!data || size < 5) return false;

	size_t pos = 0;
	root = parseBSON(data, pos, size, 0);
	if (!root || pos != size) {
		delete root;
		root = nullptr;
		lastError = pos != size ? "Trailing bytes" : "Parse failed";
		return false;
	}
	return true;
}

/**
 * @brief Read 32-bit little-endian integer
 * @param data Pointer to data
 * @return Integer value
 */
inline uint32_t readLE32(const uint8_t* data) {
	return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

/**
 * @brief Parse BSON value
 * @param data Pointer to BSON data
 * @param pos Current position (updated)
 * @param dataLen Total data length
 * @param depth Current nesting depth
 * @return Parsed value or nullptr
 */
inline asvJSONValue* parseBSON(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth) {
	if (pos + 4 > dataLen) return nullptr;
	if (depth > asvJSONValue::MAX_NESTING_DEPTH) return nullptr;
	int32_t docLen = readLE32(data + pos);
	pos += 4;
	if (docLen < 5 || static_cast<size_t>(docLen) > dataLen - (pos - 4)) return nullptr;
	size_t docEnd = pos + docLen - 4;
	asvJSONValue* obj = asvJSONValue::makeObject();
	while (pos < docEnd) {
		std::string key;
		while (pos < dataLen && data[pos] != 0) {
			if (key.length() > asvJSONValue::MAX_STRING_LEN) { delete obj; return nullptr; }
			key += static_cast<char>(data[pos++]);
		}
		pos++;
		if (key.empty()) break;
		if (pos >= dataLen) { delete obj; return nullptr; }
		uint8_t type = data[pos++];
		if (type == 0) break;
		asvJSONValue* val = nullptr;
		switch (type) {
			case 0x01: {
				if (pos + 8 > dataLen) { delete obj; return nullptr; }
				double d = readLE64_double(data + pos);
				pos += 8;
				val = asvJSONValue::makeDouble(d);
				break;
			}
			case 0x02: {
				if (pos + 4 > dataLen) { delete obj; return nullptr; }
				int32_t strLen = readLE32(data + pos);
				pos += 4;
				if (strLen <= 0 || strLen > static_cast<int32_t>(asvJSONValue::MAX_STRING_LEN)) { delete obj; return nullptr; }
				if (static_cast<size_t>(strLen) > dataLen - pos) { delete obj; return nullptr; }
				if (data[pos + strLen - 1] != 0) { delete obj; return nullptr; }
				val = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(strLen - 1));
				if (!val) { delete obj; return nullptr; }
				pos += strLen;
				break;
			}
			case 0x03: {
				val = parseBSON(data, pos, dataLen, depth + 1);
				break;
			}
			case 0x04: {
				val = parseBSON(data, pos, dataLen, depth + 1);
				if (!val) { delete obj; return nullptr; }
				if (val->type == asvJSONValue::OBJECT) {
					if (val->obj->empty()) {
						delete val;
						val = asvJSONValue::makeArray();
						break;
					}
					size_t count = val->obj->size();
					bool sequential = true;
					char idxBuf[32];
					for (size_t i = 0; i < count; i++) {
						auto [ptr, ec] = std::to_chars(idxBuf, idxBuf + sizeof(idxBuf), i);
						if (ec != std::errc()) { sequential = false; break; }
						auto it = map_find(*val->obj, std::string_view(idxBuf, static_cast<size_t>(ptr - idxBuf)));
						if (it == val->obj->end()) { sequential = false; break; }
					}
					if (sequential) {
						try {
							asvJSONValue* arr = asvJSONValue::makeArray();
							if (!arr) { delete val; delete obj; return nullptr; }
							arr->arr->resize(count);
							for (size_t i = 0; i < count; i++) {
								auto [ptr, ec] = std::to_chars(idxBuf, idxBuf + sizeof(idxBuf), i);
								if (ec != std::errc()) { delete val; delete obj; return nullptr; }
								auto it = map_find(*val->obj, std::string_view(idxBuf, static_cast<size_t>(ptr - idxBuf)));
								(*arr->arr)[i] = std::move(it->second);
							}
							delete val;
							val = arr;
						} catch (...) {
							delete val; delete obj;
							return nullptr;
						}
					}
				}
				break;
			}
			case 0x05: {
				if (pos + 5 > dataLen) { delete obj; return nullptr; }
				int32_t binLen = readLE32(data + pos);
				pos += 4;
				uint8_t subtype = data[pos++];
				if (binLen < 0 || binLen > static_cast<int32_t>(asvJSONValue::MAX_STRING_LEN)) { delete obj; return nullptr; }
				if (static_cast<size_t>(binLen) > dataLen - pos) { delete obj; return nullptr; }
				if (subtype == 0x80) {
					val = asvJSONValue::makeExtension(subtype, data + pos, binLen);
				} else {
					val = asvJSONValue::makeBinary(data + pos, binLen);
				}
				if (!val) { delete obj; return nullptr; }
				pos += binLen;
				break;
			}
			case 0x08: {
				val = asvJSONValue::makeBool(data[pos++] != 0);
				break;
			}
			case 0x09: {
				if (pos + 8 > dataLen) { delete obj; return nullptr; }
				int64_t ms = static_cast<int64_t>(readLE64(data + pos));
				pos += 8;
				time_t ts = static_cast<time_t>(ms / 1000);
				int ms_part = static_cast<int>(ms % 1000);
				val = asvJSONValue::makeDateTime(ts, ms_part);
				break;
			}
			case 0x10: {
				if (pos + 4 > dataLen) { delete obj; return nullptr; }
				int32_t n = readLE32(data + pos);
				pos += 4;
				val = asvJSONValue::makeInt(n);
				break;
			}
			case 0x12: {
				if (pos + 8 > dataLen) { delete obj; return nullptr; }
				int64_t n = static_cast<int64_t>(readLE64(data + pos));
				pos += 8;
				val = asvJSONValue::makeInt(n);
				break;
			}
			case 0x0A: {
				val = asvJSONValue::makeNull();
				break;
			}
			case 0x07: {
				if (pos + 12 > dataLen) { delete obj; return nullptr; }
				val = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
				pos += 12;
				break;
			}
			case 0x11: {
				if (pos + 8 > dataLen) { delete obj; return nullptr; }
				// BSON Timestamp: 4 bytes increment (LE) + 4 bytes seconds (LE)
				uint32_t increment = readLE32(data + pos);
				pos += 4;
				uint32_t seconds = readLE32(data + pos);
				pos += 4;
				int64_t ts = (static_cast<int64_t>(seconds) << 32) | increment;
				val = asvJSONValue::makeTimestamp(ts);
				break;
			}
			case 0x0B: {
				if (pos + 2 > dataLen) { delete obj; return nullptr; }
				std::string pattern;
				while (pos < dataLen && data[pos] != 0) {
					if (pattern.length() > asvJSONValue::MAX_STRING_LEN) { delete obj; return nullptr; }
					pattern += static_cast<char>(data[pos++]);
				}
				if (pos >= dataLen) { delete obj; return nullptr; }
				pos++;
				std::string options;
				while (pos < dataLen && data[pos] != 0) {
					if (options.length() > asvJSONValue::MAX_STRING_LEN) { delete obj; return nullptr; }
					options += static_cast<char>(data[pos++]);
				}
				if (pos >= dataLen) { delete obj; return nullptr; }
				pos++;
				const char* optPtr = options.empty() ? nullptr : options.c_str();
				val = asvJSONValue::makeRegex(pattern.c_str(), optPtr);
				if (!val) { delete obj; return nullptr; }
				break;
			}
			default: {
				delete obj;
				return nullptr;
			}
		}
		std::unique_ptr<asvJSONValue> guard(val);
		if (!guard) continue;
		(*obj->obj)[key] = std::move(guard);
	}
	if (pos < dataLen && data[pos] == 0) pos++;
	return obj;
}

/**
 * @brief Decode a single JSON Pointer key segment
 * @param sv View updated to after the segment and trailing '/'
 * @return Decoded key string, or empty on invalid escape
 */
inline std::string decodeJSONPointerKey(std::string_view& sv) {
	std::string key;
	while (!sv.empty() && sv[0] != '/') {
		if (sv[0] == '~') {
			if (sv.size() >= 2 && sv[1] == '0') { key += '~'; sv.remove_prefix(2); continue; }
			if (sv.size() >= 2 && sv[1] == '1') { key += '/'; sv.remove_prefix(2); continue; }
			sv.remove_prefix(sv.size() >= 2 ? 2 : 1);
			return {}; // invalid escape
		}
		key += sv[0];
		sv.remove_prefix(1);
	}
	if (!sv.empty() && sv[0] == '/') sv.remove_prefix(1);
	return key;
}

inline const asvJSONValue* asvJSON::getByPointer(std::string_view pointer) const {
	if (pointer.data() == nullptr || !root) return nullptr;
	if (pointer.empty()) return root;
	if (pointer[0] != '/') return nullptr;

	pointer.remove_prefix(1);
	const asvJSONValue* current = root;

	while (true) {
		std::string key = decodeJSONPointerKey(pointer);
		if (current->type == asvJSONValue::ARRAY) {
			errno = 0;
			char* end;
			unsigned long idx = strtoul(key.c_str(), &end, 10);
			if (errno == ERANGE || *end != 0 || end != key.c_str() + key.length() || idx >= current->arr->size()) return nullptr;
			current = (*current->arr)[idx].get();
			if (!current) return nullptr;
		} else if (current->type == asvJSONValue::OBJECT) {
			auto it = map_find(*current->obj, key);
			if (it == current->obj->end() || !it->second) return nullptr;
			current = it->second.get();
		} else {
			return nullptr;
		}
		if (pointer.empty()) break;
	}

	return current;
}

inline asvJSONValue* asvJSON::getByPointer(std::string_view pointer) {
	return const_cast<asvJSONValue*>(static_cast<const asvJSON*>(this)->getByPointer(pointer));
}

inline bool asvJSON::setByPointer(std::string_view pointer, asvJSONValue* value) {
	if (pointer.data() == nullptr || !value || pointer.empty() || pointer[0] != '/') { delete value; return false; }
	try {
		if (!root || (root->type != asvJSONValue::OBJECT && root->type != asvJSONValue::ARRAY)) {
			delete root;
			root = asvJSONValue::makeObject();
			if (!root) { delete value; return false; }
		}

		pointer.remove_prefix(1);
		std::vector<std::string> keys;
		while (true) {
			keys.push_back(decodeJSONPointerKey(pointer));
			if (pointer.empty()) break;
		}
		if (keys.empty()) { delete value; return false; }

		asvJSONValue* current = root;
		for (size_t i = 0; i + 1 < keys.size(); i++) {
			std::string& key = keys[i];

			if (current->type == asvJSONValue::ARRAY) {
				if (key == "-") {
					auto* n = asvJSONValue::makeNull();
					if (!n) { delete value; return false; }
					current->arr->push_back(std::unique_ptr<asvJSONValue>(n));
					current = current->arr->back().get();
				} else {
					errno = 0;
					char* end;
					unsigned long rawIdx = strtoul(key.c_str(), &end, 10);
					if (errno == ERANGE || *end != 0 || end != key.c_str() + key.length()) { delete value; return false; }
					size_t idx = static_cast<size_t>(rawIdx);
					if (idx > current->arr->max_size()) { delete value; return false; }
					if (idx >= current->arr->size()) {
						current->arr->resize(idx + 1);
					}
					if (!(*current->arr)[idx]) {
						(*current->arr)[idx] = std::unique_ptr<asvJSONValue>(asvJSONValue::makeNull());
					}
					current = (*current->arr)[idx].get();
				}
			} else if (current->type == asvJSONValue::OBJECT) {
				auto it = map_find(*current->obj, key);
				if (it == current->obj->end()) {
					auto* newObj = asvJSONValue::makeObject();
					if (!newObj) { delete value; return false; }
					try {
						current->obj->emplace(key, std::unique_ptr<asvJSONValue>(newObj));
					} catch (...) {
						delete newObj;
						delete value;
						return false;
					}
					current = newObj;
				} else {
					current = it->second.get();
					if (!current) {
						current = asvJSONValue::makeObject();
						if (!current) { delete value; return false; }
						it->second.reset(current);
					} else if (current->type != asvJSONValue::OBJECT && current->type != asvJSONValue::ARRAY) {
						if (i + 2 < keys.size() && isArrayIndex(keys[i + 1])) {
							it->second.reset();
							current = asvJSONValue::makeArray();
							if (!current) { delete value; return false; }
							it->second.reset(current);
						} else {
							it->second.reset();
							current = asvJSONValue::makeObject();
							if (!current) { delete value; return false; }
							it->second.reset(current);
						}
					}
				}
			} else {
				delete value; return false;
			}
		}

		std::string& lastKey = keys.back();

		if (current->type == asvJSONValue::ARRAY) {
			size_t idx;
			if (lastKey == "-") {
				idx = current->arr->size();
			} else {
				errno = 0;
				char* end;
				idx = static_cast<size_t>(strtoul(lastKey.c_str(), &end, 10));
				if (errno == ERANGE || *end != 0) { delete value; return false; }
			}
			if (idx >= asvJSONValue::MAX_ARRAY_SIZE) { delete value; return false; }
			if (idx >= current->arr->size()) {
				current->arr->resize(idx + 1);
			}
			(*current->arr)[idx].reset(value);
		} else if (current->type == asvJSONValue::OBJECT) {
			auto it = current->obj->find(lastKey);
			if (it != current->obj->end()) { it->second.reset(value); }
			else { current->obj->emplace(lastKey, std::unique_ptr<asvJSONValue>(value)); }
		} else {
			delete value; return false;
		}
		return true;
	} catch (const std::bad_alloc&) {
		delete value;
		return false;
	} catch (...) {
		delete value;
		return false;
	}
}

inline bool asvJSON::removeByPointer(std::string_view pointer) {
	if (pointer.data() == nullptr || !root) return false;
	if (pointer == "/") {
		delete root;
		root = nullptr;
		return true;
	}
	asvJSONValue* target = getByPointer(pointer);
	if (!target) return false;
	std::string path(pointer.data(), pointer.size());
	size_t pos = path.rfind('/');
	if (pos == std::string::npos) return false;
	std::string key = path.substr(pos + 1);

	std::string decodedKey;
	for (size_t i = 0; i < key.size(); ++i) {
		if (key[i] == '~' && i + 1 < key.size()) {
			if (key[i + 1] == '0') { decodedKey += '~'; i += 1; }
			else if (key[i + 1] == '1') { decodedKey += '/'; i += 1; }
			else { return false; }
		} else {
			decodedKey += key[i];
		}
	}
	key = decodedKey;

		if (pos == 0) {
		if (root->type == asvJSONValue::ARRAY) {
			errno = 0;
			char* end;
			unsigned long rawIdx = strtoul(key.c_str(), &end, 10);
			if (errno == ERANGE || *end != 0 || end != key.c_str() + key.length()) return false;
			size_t idx = static_cast<size_t>(rawIdx);
			if (idx >= root->arr->size()) return false;
			root->arr->erase(root->arr->begin() + static_cast<ptrdiff_t>(idx));
		} else if (root->type == asvJSONValue::OBJECT) {
			auto it = map_find(*root->obj, key);
			if (it == root->obj->end()) return false;
			root->obj->erase(it);
		}
		return true;
	}

	asvJSONValue* parent = getByPointer(path.substr(0, pos));
	if (!parent) return false;
	if (parent->type == asvJSONValue::ARRAY) {
		errno = 0;
		char* end;
		unsigned long rawIdx = strtoul(key.c_str(), &end, 10);
		if (errno == ERANGE || *end != 0 || end != key.c_str() + key.length()) return false;
		size_t idx = static_cast<size_t>(rawIdx);
		if (idx >= parent->arr->size()) return false;
		parent->arr->erase(parent->arr->begin() + idx);
	} else if (parent->type == asvJSONValue::OBJECT) {
		auto it = map_find(*parent->obj, key);
		if (it == parent->obj->end()) return false;
		parent->obj->erase(it);
	} else {
		return false;
	}
	return true;
}

inline asvJSONValue* cloneValue(const asvJSONValue* v) {
	if (!v) return nullptr;
	switch (v->type) {
		case asvJSONValue::NULL_VAL: return asvJSONValue::makeNull();
		case asvJSONValue::BOOL_VAL: return asvJSONValue::makeBool(v->flag);
		case asvJSONValue::INT: return asvJSONValue::makeInt(v->num);
		case asvJSONValue::DOUBLE: {
			auto* result = asvJSONValue::makeDouble(v->dbl);
			if (result) result->is_float32 = v->is_float32;
			return result;
		}
		case asvJSONValue::STRING: return asvJSONValue::makeString(v->str_data.data(), v->str_data.size());
		case asvJSONValue::DATETIME: return asvJSONValue::makeDateTime(v->timestamp, v->datetime_ms);
		case asvJSONValue::BINARY: return asvJSONValue::makeBinary(v->bin_data.data(), v->bin_data.size());
		case asvJSONValue::OBJECTID: return asvJSONValue::makeObjectId(std::string_view(v->str_data.data(), v->str_data.size()));
		case asvJSONValue::TIMESTAMP: return asvJSONValue::makeTimestamp(v->num);
		case asvJSONValue::REGEX: {
			if (v->str_data.empty()) {
				auto* result = new(std::nothrow) asvJSONValue();
				if (!result) return nullptr;
				result->type = asvJSONValue::REGEX;
				return result;
			}
			std::string s(v->str_data.data(), v->str_data.size());
			size_t sep = s.find('|');
			if (sep == std::string::npos) {
				// No separator: treat whole string as pattern, no options
				auto* result = asvJSONValue::makeRegex(s.c_str(), nullptr);
				return result ? result : asvJSONValue::makeNull();
			}
			if (sep == 0) return asvJSONValue::makeNull(); // separator at start is invalid
			const char* optPtr = nullptr;
			if (sep + 1 < s.length()) {
				optPtr = s.c_str() + sep + 1;
			}
			auto* result = asvJSONValue::makeRegex(s.substr(0, sep).c_str(), optPtr);
			if (!result) return asvJSONValue::makeNull();
			return result;
		}
		case asvJSONValue::EXTENSION: {
			return asvJSONValue::makeExtension(v->ext_type, v->bin_data.data(), v->bin_data.size());
		}
		case asvJSONValue::ARRAY: {
			auto* arr = asvJSONValue::makeArray();
			if (!arr) return nullptr;
			for (auto& item : *v->arr) {
				auto* cloned = cloneValue(item.get());
				if (!cloned) { delete arr; return nullptr; }
				arr->arr->emplace_back(std::unique_ptr<asvJSONValue>(cloned));
			}
			return arr;
		}
		case asvJSONValue::OBJECT: {
			auto* obj = asvJSONValue::makeObject();
			if (!obj) return nullptr;
			for (const auto& [kv_key, kv_val] : *v->obj) {
				auto* cloned = cloneValue(kv_val.get());
				if (!cloned) { delete obj; return nullptr; }
				obj->obj->emplace(kv_key, std::unique_ptr<asvJSONValue>(cloned));
			}
			return obj;
		}
		default: return nullptr;
	}
}

/**
 * @brief Apply JSON Merge Patch recursively
 * @param target Target value to patch (modified in-place)
 * @param patch Patch value to apply
 * @return Patched value (usually target)
 */
inline asvJSONValue* mergePatchRecursive(asvJSONValue* target, const asvJSONValue* patch) {
	if (!target || !patch) return target;
	if (target->type == asvJSONValue::OBJECT && patch->type == asvJSONValue::OBJECT) {
		for (const auto& [kv_key, kv_val] : *patch->obj) {
			auto it = target->obj->find(kv_key);
			if (it != target->obj->end()) {
				if (kv_val->type == asvJSONValue::NULL_VAL) {
					target->obj->erase(it);
				} else if (it->second && it->second->type == asvJSONValue::OBJECT && kv_val->type == asvJSONValue::OBJECT) {
					asvJSONValue* merged = mergePatchRecursive(it->second.get(), kv_val.get());
					if (merged != it->second.get()) {
						it->second.reset(merged);
					}
				} else {
					asvJSONValue* cloned = cloneValue(kv_val.get());
					if (cloned) it->second.reset(cloned);
					else it->second.reset(asvJSONValue::makeNull());
				}
			} else {
				if (kv_val->type != asvJSONValue::NULL_VAL) {
					asvJSONValue* cloned = cloneValue(kv_val.get());
					if (cloned) target->obj->emplace(kv_key, std::unique_ptr<asvJSONValue>(cloned));
				}
			}
		}
		return target;
	}
	return cloneValue(patch);
}

inline void asvJSON::merge(const asvJSON& other) {
	if (!other.root) return;
	if (!root) { root = cloneValue(other.root); return; }
	asvJSONValue* newRoot = mergePatchRecursive(root, other.root);
	if (newRoot != root) {
		delete root;
		root = newRoot;
	}
}

inline asvJSON asvJSON::applyMergePatch(const asvJSON& patch) const {
	asvJSON result;
	if (!root) return result;
	if (root->type == asvJSONValue::OBJECT && patch.root && patch.root->type == asvJSONValue::OBJECT) {
		result.root = mergePatchRecursive(cloneValue(root), patch.root);
	} else {
		result.root = cloneValue(patch.root ? patch.root : root);
	}
	return result;
}

/**
 * @brief Deep equality comparison of two asvJSONValue trees
 * @param a First value (can be nullptr)
 * @param b Second value (can be nullptr)
 * @return true if values are deeply equal
 */
inline bool valuesEqual(const asvJSONValue* a, const asvJSONValue* b) {
	if (!a || !b) return false;
	if (a->type != b->type) {
		if (a->type == asvJSONValue::INT && b->type == asvJSONValue::DOUBLE)
			return static_cast<double>(a->num) == b->dbl;
		if (a->type == asvJSONValue::DOUBLE && b->type == asvJSONValue::INT)
			return a->dbl == static_cast<double>(b->num);
		return false;
	}
	switch (a->type) {
		case asvJSONValue::NULL_VAL: return true;
		case asvJSONValue::BOOL_VAL: return a->flag == b->flag;
		case asvJSONValue::INT: return a->num == b->num;
		case asvJSONValue::DOUBLE: return a->dbl == b->dbl;
		case asvJSONValue::STRING: return a->str_data.size() == b->str_data.size() && (a->str_data.size() == 0 || std::memcmp(a->str_data.data(), b->str_data.data(), a->str_data.size()) == 0);
		case asvJSONValue::DATETIME: return a->timestamp == b->timestamp && a->datetime_ms == b->datetime_ms;
		case asvJSONValue::BINARY: return a->bin_data.size() == b->bin_data.size() && (a->bin_data.size() == 0 || std::memcmp(a->bin_data.data(), b->bin_data.data(), a->bin_data.size()) == 0);
		case asvJSONValue::EXTENSION: return a->ext_type == b->ext_type && a->bin_data.size() == b->bin_data.size() && (a->bin_data.size() == 0 || std::memcmp(a->bin_data.data(), b->bin_data.data(), a->bin_data.size()) == 0);
		case asvJSONValue::ARRAY: {
			if (a->arr->size() != b->arr->size()) return false;
			for (size_t i = 0; i < a->arr->size(); i++) {
				if (!valuesEqual((*a->arr)[i].get(), (*b->arr)[i].get())) return false;
			}
			return true;
		}
		case asvJSONValue::OBJECT: {
			if (a->obj->size() != b->obj->size()) return false;
			for (const auto& [kv_key, kv_val] : *a->obj) {
				auto it = b->obj->find(kv_key);
				if (it == b->obj->end() || !valuesEqual(kv_val.get(), it->second.get())) return false;
			}
			return true;
		}
		case asvJSONValue::OBJECTID: return a->str_data.size() == 12 && b->str_data.size() == 12 && std::memcmp(a->str_data.data(), b->str_data.data(), 12) == 0;
		case asvJSONValue::REGEX: return a->str_data.size() == b->str_data.size() && (a->str_data.size() == 0 || std::memcmp(a->str_data.data(), b->str_data.data(), a->str_data.size()) == 0);
		case asvJSONValue::TIMESTAMP: return a->num == b->num;
		default: return false;
	}
}

inline bool asvJSON::applyPatch(const asvJSON& patch) {
	if (!patch.root || patch.root->type != asvJSONValue::ARRAY) return false;
	for (size_t i = 0; i < patch.root->arr->size(); i++) {
		auto* op = (*patch.root->arr)[i].get();
		if (!op || op->type != asvJSONValue::OBJECT) continue;
		auto* opVal = op->get("op");
		if (!opVal || opVal->type != asvJSONValue::STRING) continue;
		std::string_view opStr(opVal->str_data.data(), opVal->str_data.size());
		auto* pathVal = op->get("path");
		if (!pathVal || pathVal->type != asvJSONValue::STRING) continue;
		std::string pathStr(pathVal->str_data.data(), pathVal->str_data.size());

		if (opStr == "remove") {
			if (!removeByPointer(pathStr)) return false;
		} else if (opStr == "replace") {
			auto* val = op->get("value");
			if (!val) return false;
			auto cloned = std::unique_ptr<asvJSONValue>(cloneValue(val));
			if (!cloned) return false;
			if (!setByPointer(pathStr, cloned.release())) { return false; }
		} else if (opStr == "add") {
			auto* val = op->get("value");
			if (!val) return false;
			auto cloned = std::unique_ptr<asvJSONValue>(cloneValue(val));
			if (!cloned) return false;
			if (!pathStr.empty() && pathStr.back() == '-') {
				std::string parentPath = pathStr.substr(0, pathStr.length() - 1);
				asvJSONValue* parent = getByPointer(parentPath);
				if (parent && parent->type == asvJSONValue::ARRAY) {
					if (parent->arr->size() >= asvJSONValue::MAX_ARRAY_SIZE) return false;
					parent->arr->emplace_back(std::move(cloned));
				} else { return false; }
			} else {
				if (!setByPointer(pathStr, cloned.release())) { return false; }
			}
		} else if (opStr == "copy") {
			auto* fromVal = op->get("from");
			if (!fromVal || fromVal->type != asvJSONValue::STRING) return false;
			std::string fromPath(fromVal->str_data.data(), fromVal->str_data.size());
			asvJSONValue* from = getByPointer(fromPath);
			if (!from) return false;
			auto cloned = std::unique_ptr<asvJSONValue>(cloneValue(from));
			if (!cloned) return false;
			if (!setByPointer(pathStr, cloned.release())) { return false; }
		} else if (opStr == "move") {
			auto* fromVal = op->get("from");
			if (!fromVal || fromVal->type != asvJSONValue::STRING) return false;
			std::string fromPath(fromVal->str_data.data(), fromVal->str_data.size());
			if (fromPath == pathStr) continue;
			asvJSONValue* from = getByPointer(fromPath);
			if (!from) return false;
			auto cloned = std::unique_ptr<asvJSONValue>(cloneValue(from));
			if (!cloned) return false;
			std::unique_ptr<asvJSONValue> backup(cloneValue(from));
			if (!backup) return false;
			if (!removeByPointer(fromPath)) { return false; }
			if (!setByPointer(pathStr, cloned.release())) {
				setByPointer(fromPath, backup.release());
				return false;
			}
		} else if (opStr == "test") {
			auto* val = op->get("value");
			if (val) {
				asvJSONValue* current = getByPointer(pathStr);
				if (!current || !valuesEqual(current, val)) return false;
			}
		}
	}

	return true;
}

inline void asvJSONValue::toMessagePack(std::vector<uint8_t>& out) const {
	switch (type) {
		case NULL_VAL: out.push_back(0xC0); break;
		case BOOL_VAL: out.push_back(flag ? 0xC3 : 0xC2); break;
		case INT: {
			int64_t n = num;
			if (n >= 0 && n <= 127) {
				out.push_back(static_cast<uint8_t>(n));
			} else if (n >= -32 && n <= -1) {
				out.push_back(static_cast<uint8_t>(0xE0 + static_cast<int8_t>(n)));
			} else if (n >= -128 && n <= 127) {
				out.push_back(0xD0);
				out.push_back(static_cast<uint8_t>(n));
			} else if (n >= -32768 && n <= 32767) {
				out.push_back(0xD1);
				out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(n & 0xFF));
			} else if (n >= -2147483648LL && n <= 2147483647LL) {
				out.push_back(0xD2);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
			} else {
				out.push_back(0xD3);
				for (int i = 7; i >= 0; i--) out.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
			}
			break;
		}
		case DOUBLE: {
			if (is_float32) {
				uint32_t u;
				float f = static_cast<float>(dbl);
				memcpy(&u, &f, sizeof(u));
				out.push_back(0xCA);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
			} else {
				uint64_t u;
				memcpy(&u, &dbl, sizeof(u));
				out.push_back(0xCB);
				for (int i = 7; i >= 0; i--) out.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
			}
			break;
		}
		case STRING: {
			if (str_data.size() <= 31) {
				out.push_back(0xA0 | static_cast<uint8_t>(str_data.size()));
			} else if (str_data.size() <= 255) {
				out.push_back(0xD9);
				out.push_back(static_cast<uint8_t>(str_data.size()));
			} else if (str_data.size() <= 65535) {
				out.push_back(0xDA);
				out.push_back(static_cast<uint8_t>((str_data.size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(str_data.size() & 0xFF));
			} else {
				out.push_back(0xDB);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((str_data.size() >> (i * 8)) & 0xFF));
			}
			out.insert(out.end(), str_data.data(), str_data.data() + str_data.size());
			break;
		}
		case OBJECT: {
			if (!obj) break;
			if (obj->size() <= 15) {
				out.push_back(0x80 | static_cast<uint8_t>(obj->size()));
			} else if (obj->size() <= 65535) {
				out.push_back(0xDE);
				out.push_back(static_cast<uint8_t>((obj->size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(obj->size() & 0xFF));
			} else {
				out.push_back(0xDF);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((obj->size() >> (i * 8)) & 0xFF));
			}
			for (const auto& [key, val] : *obj) {
				size_t key_len = key.length();
				if (key_len > MAX_STRING_LEN) throw asvJSONError("MessagePack key too long");
				if (key_len <= 31) {
					out.push_back(0xA0 | static_cast<uint8_t>(key_len));
				} else if (key_len <= 255) {
					out.push_back(0xD9);
					out.push_back(static_cast<uint8_t>(key_len));
				} else if (key_len <= 65535) {
					out.push_back(0xDA);
					out.push_back(static_cast<uint8_t>((key_len >> 8) & 0xFF));
					out.push_back(static_cast<uint8_t>(key_len & 0xFF));
				} else {
					out.push_back(0xDB);
					for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((key_len >> (i * 8)) & 0xFF));
				}
				out.insert(out.end(), key.begin(), key.begin() + key_len);
				if (val) val->toMessagePack(out);
				else { out.push_back(0xC0); }
			}
			break;
		}
		case ARRAY: {
			if (!arr) break;
			if (arr->size() <= 15) {
				out.push_back(0x90 | static_cast<uint8_t>(arr->size()));
			} else if (arr->size() <= 65535) {
				out.push_back(0xDC);
				out.push_back(static_cast<uint8_t>((arr->size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(arr->size() & 0xFF));
			} else {
				out.push_back(0xDD);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((arr->size() >> (i * 8)) & 0xFF));
			}
			for (const auto& v : *arr) v->toMessagePack(out);
			break;
		}
		case DATETIME: {
			uint32_t ns = static_cast<uint32_t>(datetime_ms) * 1000000;
			if (ns > 999999999) ns = 999999999;
			if (timestamp >= 0 && timestamp < 0x100000000LL && ns == 0) {
				out.push_back(0xD6);
				out.push_back(0xFF);
				out.push_back(static_cast<uint8_t>((timestamp >> 24) & 0xFF));
				out.push_back(static_cast<uint8_t>((timestamp >> 16) & 0xFF));
				out.push_back(static_cast<uint8_t>((timestamp >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(timestamp & 0xFF));
			} else if (timestamp >= 0 && timestamp < (1LL << 34)) {
				out.push_back(0xD7);
				out.push_back(0xFF);
				uint64_t packed = (static_cast<uint64_t>(ns) << 34) | static_cast<uint64_t>(timestamp);
				for (int i = 7; i >= 0; i--) {
					out.push_back(static_cast<uint8_t>((packed >> (i * 8)) & 0xFF));
				}
			} else {
				out.push_back(0xC7);
				out.push_back(12);
				out.push_back(0xFF);
				out.push_back(static_cast<uint8_t>((ns >> 24) & 0xFF));
				out.push_back(static_cast<uint8_t>((ns >> 16) & 0xFF));
				out.push_back(static_cast<uint8_t>((ns >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(ns & 0xFF));
				for (int i = 7; i >= 0; i--) {
					out.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
				}
			}
			break;
		}
		case BINARY: {
			if (bin_data.size() <= 255) {
				out.push_back(0xC4);
				out.push_back(static_cast<uint8_t>(bin_data.size()));
			} else if (bin_data.size() <= 65535) {
				out.push_back(0xC5);
				out.push_back(static_cast<uint8_t>((bin_data.size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(bin_data.size() & 0xFF));
			} else {
				out.push_back(0xC6);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((bin_data.size() >> (i * 8)) & 0xFF));
			}
			out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			break;
		}
		case EXTENSION: {
			if (bin_data.empty()) { out.push_back(0xC0); break; }
			if (bin_data.size() == 1) {
				out.push_back(0xD4);
				out.push_back(ext_type);
				out.push_back(bin_data.data()[0]);
			} else if (bin_data.size() == 2) {
				out.push_back(0xD5);
				out.push_back(ext_type);
				out.push_back(bin_data.data()[0]);
				out.push_back(bin_data.data()[1]);
			} else if (bin_data.size() == 4) {
				out.push_back(0xD6);
				out.push_back(ext_type);
				out.push_back(bin_data.data()[0]);
				out.push_back(bin_data.data()[1]);
				out.push_back(bin_data.data()[2]);
				out.push_back(bin_data.data()[3]);
			} else if (bin_data.size() == 8) {
				out.push_back(0xD7);
				out.push_back(ext_type);
				out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			} else if (bin_data.size() == 16) {
				out.push_back(0xD8);
				out.push_back(ext_type);
				out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			} else if (bin_data.size() <= 255) {
				out.push_back(0xC7);
				out.push_back(static_cast<uint8_t>(bin_data.size()));
				out.push_back(ext_type);
				out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			} else if (bin_data.size() <= 65535) {
				out.push_back(0xC8);
				out.push_back(static_cast<uint8_t>((bin_data.size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(bin_data.size() & 0xFF));
				out.push_back(ext_type);
				out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			} else {
				out.push_back(0xC9);
				out.push_back(static_cast<uint8_t>((bin_data.size() >> 24) & 0xFF));
				out.push_back(static_cast<uint8_t>((bin_data.size() >> 16) & 0xFF));
				out.push_back(static_cast<uint8_t>((bin_data.size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(bin_data.size() & 0xFF));
				out.push_back(ext_type);
				out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			}
			break;
		}
		case OBJECTID: {
			if (str_data.size() != 12) { out.push_back(0xC0); break; }
			// Store as ext8 (0xC7) with type 1 for roundtrip preservation
			if (str_data.size() <= 255) {
				out.push_back(0xC7);
				out.push_back(static_cast<uint8_t>(str_data.size()));
			} else if (str_data.size() <= 65535) {
				out.push_back(0xC8);
				out.push_back(static_cast<uint8_t>((str_data.size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(str_data.size() & 0xFF));
			} else {
				out.push_back(0xC9);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((str_data.size() >> (i * 8)) & 0xFF));
			}
			out.push_back(1); // extType=1 for ObjectId
			out.insert(out.end(), reinterpret_cast<const uint8_t*>(str_data.data()), reinterpret_cast<const uint8_t*>(str_data.data()) + str_data.size());
			break;
		}
		case REGEX: {
			if (str_data.empty()) { out.push_back(0xC0); break; }
			// Store as ext with type 2 for roundtrip preservation
			if (str_data.size() <= 255) {
				out.push_back(0xC7);
				out.push_back(static_cast<uint8_t>(str_data.size()));
			} else if (str_data.size() <= 65535) {
				out.push_back(0xC8);
				out.push_back(static_cast<uint8_t>((str_data.size() >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(str_data.size() & 0xFF));
			} else {
				out.push_back(0xC9);
				for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>((str_data.size() >> (i * 8)) & 0xFF));
			}
			out.push_back(2); // extType=2 for Regex
			out.insert(out.end(), reinterpret_cast<const uint8_t*>(str_data.data()), reinterpret_cast<const uint8_t*>(str_data.data()) + str_data.size());
			break;
		}
		case TIMESTAMP: {
			// Store as ext8 (0xC7) with type 3 for roundtrip preservation
			out.push_back(0xD7);
			out.push_back(3); // extType=3 for Timestamp
			uint64_t n = static_cast<uint64_t>(num);
			for (int i = 7; i >= 0; i--) out.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
			break;
		}
		default: out.push_back(0xC0); break;
	}
}

/**
 * @brief Write 32-bit little-endian integer
 * @param out Output vector
 * @param v Value to write
 */
inline void writeLE32(std::vector<uint8_t>& out, uint32_t v) {
	out.push_back(static_cast<uint8_t>(v & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

/**
 * @brief Write 64-bit little-endian integer
 * @param out Output vector
 * @param v Value to write
 */
inline void writeLE64(std::vector<uint8_t>& out, uint64_t v) {
	for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

inline void asvJSONValue::toBSON(std::vector<uint8_t>& out) const {
	switch (type) {
		case NULL_VAL: out.push_back(0x0A); break;
		case BOOL_VAL: out.push_back(0x08); out.push_back(flag ? 0x01 : 0x00); break;
		case INT: {
			out.push_back(0x12);
			writeLE64(out, static_cast<uint64_t>(num));
			break;
		}
		case DOUBLE: {
			out.push_back(0x01);
			uint64_t bits;
			memcpy(&bits, &dbl, sizeof(bits));
			writeLE64(out, bits);
			break;
		}
		case STRING: {
			out.push_back(0x02);
			uint32_t len = static_cast<uint32_t>(str_data.size() + 1);
			writeLE32(out, len);
			if (str_data.size() > 0) out.insert(out.end(), str_data.data(), str_data.data() + str_data.size());
			out.push_back('\0');
			break;
		}
		case DATETIME: {
			out.push_back(0x09);
			int64_t ms = 0;
			if (timestamp > 0 && timestamp > (INT64_MAX - datetime_ms) / 1000) {
				ms = INT64_MAX;
			} else if (timestamp < 0 && timestamp < (datetime_ms < 0 ? (INT64_MIN - datetime_ms) / 1000 : INT64_MIN / 1000)) {
				ms = INT64_MIN;
			} else {
				ms = timestamp * 1000LL + datetime_ms;
			}
			writeLE64(out, static_cast<uint64_t>(ms));
			break;
		}
		case BINARY: {
			out.push_back(0x05);
			writeLE32(out, static_cast<uint32_t>(bin_data.size()));
			out.push_back(0x00);
			if (bin_data.size() > 0) out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			break;
		}
		case ARRAY: {
			if (!arr) break;
			size_t docStart = out.size();
			out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0);
			for (size_t i = 0; i < arr->size(); i++) {
				char idxBuf[32];
				auto [ptr, ec] = std::to_chars(idxBuf, idxBuf + sizeof(idxBuf), i);
				if (ec != std::errc()) throw asvJSONError("Failed to format array index");
				out.insert(out.end(), idxBuf, ptr);
				out.push_back('\0');
				(*arr)[i]->toBSON(out);
			}
			out.push_back('\0');
			{
				size_t rawDocLen = out.size() - docStart;
				if (rawDocLen > 0xFFFFFFFFULL) throw asvJSONError("BSON document too large");
				uint32_t docLen = static_cast<uint32_t>(rawDocLen);
				out[docStart + 0] = static_cast<uint8_t>(docLen & 0xFF);
				out[docStart + 1] = static_cast<uint8_t>((docLen >> 8) & 0xFF);
				out[docStart + 2] = static_cast<uint8_t>((docLen >> 16) & 0xFF);
				out[docStart + 3] = static_cast<uint8_t>((docLen >> 24) & 0xFF);
			}
			break;
		}
		case OBJECT: {
			if (!obj) break;
			size_t docStart = out.size();
			out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0);
			for (const auto& [key, val] : *obj) {
				out.insert(out.end(), key.begin(), key.end());
				out.push_back('\0');
				if (val) val->toBSON(out);
				else out.push_back(0x0A);
			}
			out.push_back('\0');
			{
				size_t rawDocLen = out.size() - docStart;
				if (rawDocLen > 0xFFFFFFFFULL) throw asvJSONError("BSON document too large");
				uint32_t docLen = static_cast<uint32_t>(rawDocLen);
				out[docStart + 0] = static_cast<uint8_t>(docLen & 0xFF);
				out[docStart + 1] = static_cast<uint8_t>((docLen >> 8) & 0xFF);
				out[docStart + 2] = static_cast<uint8_t>((docLen >> 16) & 0xFF);
				out[docStart + 3] = static_cast<uint8_t>((docLen >> 24) & 0xFF);
			}
			break;
		}
		case OBJECTID: {
			if (str_data.size() != 12) throw asvJSONError("Invalid ObjectId length");
			out.push_back(0x07);
			out.insert(out.end(), reinterpret_cast<const uint8_t*>(str_data.data()), reinterpret_cast<const uint8_t*>(str_data.data()) + 12);
			break;
		}
		case TIMESTAMP: {
			out.push_back(0x11);
			// BSON Timestamp: 4 bytes increment (LE) + 4 bytes seconds (LE)
			writeLE32(out, static_cast<uint32_t>(num & 0xFFFFFFFF));
			writeLE32(out, static_cast<uint32_t>((static_cast<uint64_t>(num) >> 32) & 0xFFFFFFFF));
			break;
		}
		case REGEX: {
			out.push_back(0x0B);
			if (str_data.data() && str_data.size() > 0) {
				const char* sep = static_cast<const char*>(memchr(str_data.data(), '|', str_data.size()));
				if (sep) {
					out.insert(out.end(), str_data.data(), sep);
					out.push_back('\0');
					out.insert(out.end(), sep + 1, str_data.data() + str_data.size());
					out.push_back('\0');
			} else {
				out.insert(out.end(), str_data.data(), str_data.data() + str_data.size());
				out.push_back('\0');
				out.push_back('\0');
			}
			} else {
				out.push_back('\0');
				out.push_back('\0');
			}
			break;
		}
		case EXTENSION: {
			out.push_back(0x05);
			writeLE32(out, static_cast<uint32_t>(bin_data.size()));
			out.push_back(0x80);
			out.insert(out.end(), bin_data.data(), bin_data.data() + bin_data.size());
			break;
		}
		default:
			// BSON: write as generic binary ext for unhandled types
			out.push_back(0x05);
			writeLE32(out, 0);
			out.push_back(0x00);
			break;
	}
}

inline void asvJSONValue::toXML(std::string& out) const {
	out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	toXML(out, "root", 0);
}

inline void asvJSONValue::toXML(std::string& out, const std::string& name, int indent) const {
	std::string prefix(static_cast<size_t>(indent) * 2, ' ');
	switch (type) {
		case NULL_VAL:
			out += prefix + "<" + name + "/>\n";
			break;
		case BOOL_VAL:
			out += prefix + "<" + name + ">" + (flag ? "true" : "false") + "</" + name + ">\n";
			break;
		case INT:
			out += prefix + "<" + name + ">" + std::to_string(num) + "</" + name + ">\n";
			break;
		case DOUBLE: {
			if (std::isnan(dbl)) {
				out += prefix + "<" + name + "/>\n";
			} else if (std::isinf(dbl)) {
				out += prefix + "<" + name + ">" + std::string(dbl > 0 ? "Infinity" : "-Infinity") + "</" + name + ">\n";
			} else {
				char buf[32];
				int n = snprintf(buf, sizeof(buf), "%.17g", dbl);
				if (n > 0) out += prefix + "<" + name + ">" + std::string(buf, static_cast<size_t>(n)) + "</" + name + ">\n";
				else out += prefix + "<" + name + "/>\n";
			}
			break;
		}
		case STRING:
			out += prefix + "<" + name + ">" + xmlEscapeContent(str_data) + "</" + name + ">\n";
			break;
		case DATETIME: {
			char buf[40];
			std::tm tm;
			asvjson_gmtime(&tm, &timestamp);
			char msbuf[16] = "";
			if (datetime_ms > 0) snprintf(msbuf, sizeof(msbuf), ".%03d", datetime_ms);
			if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm) == 0)
				throw asvJSONError("Failed to format datetime");
			out += prefix + "<" + name + " type=\"datetime\">" + buf + msbuf + "Z</" + name + ">\n";
			break;
		}
		case BINARY: {
			auto encoded = base64_encode(bin_data.data(), bin_data.size());
			out += prefix + "<" + name + " type=\"binary\">" + encoded + "</" + name + ">\n";
			break;
		}
		case OBJECTID: {
			std::string hex;
			for (size_t i = 0; i < str_data.size() && i < 12; i++) {
				char hb[4];
				snprintf(hb, sizeof(hb), "%02x", static_cast<unsigned char>(str_data[i]));
				hex += hb;
			}
			out += prefix + "<" + name + " type=\"objectid\">" + hex + "</" + name + ">\n";
			break;
		}
		case REGEX: {
			size_t sep = str_data.find('|');
			std::string_view pattern = (sep != std::string_view::npos) ? std::string_view(str_data.data(), sep) : std::string_view(str_data);
			std::string_view options = (sep != std::string_view::npos) ? std::string_view(str_data.data() + sep + 1, str_data.size() - sep - 1) : std::string_view();
			out += prefix + "<" + name + " type=\"regex\">";
			out += "<pattern>" + xmlEscapeContent(pattern) + "</pattern>";
			out += "<options>" + xmlEscapeContent(options) + "</options>";
			out += "</" + name + ">\n";
			break;
		}
		case TIMESTAMP:
			out += prefix + "<" + name + " type=\"timestamp\">" + std::to_string(num) + "</" + name + ">\n";
			break;
		case EXTENSION: {
			auto encoded = base64_encode(bin_data.data(), bin_data.size());
			out += prefix + "<" + name + " type=\"extension\" extType=\"" + std::to_string(ext_type) + "\">" + encoded + "</" + name + ">\n";
			break;
		}
		case OBJECT: {
			out += prefix + "<" + name + ">\n";
			if (obj) {
				for (const auto& [key, val] : *obj) {
					val->toXML(out, xmlSanitizeElementName(key), indent + 1);
				}
			}
			out += prefix + "</" + name + ">\n";
			break;
		}
		case ARRAY: {
			out += prefix + "<" + name + ">\n";
			if (arr) {
				for (const auto& v : *arr) {
					v->toXML(out, "item", indent + 1);
				}
			}
			out += prefix + "</" + name + ">\n";
			break;
		}
	}
}

inline void asvJSONValue::toYAML(std::string& out) const {
	out += "---\n";
	toYAML(out, 0, "", false);
}

inline void asvJSONValue::toYAML(std::string& out, int indent, const std::string& key, bool isArrayItem) const {
	auto pad = [&]() { out.append(static_cast<size_t>(indent) * 2, ' '); };
	auto startLine = [&]() { pad(); if (isArrayItem) out += "- "; else if (!key.empty()) { out += yamlQuoteKey(key); out += ": "; } };
	auto startBlock = [&]() { pad(); if (isArrayItem) out += '-'; else if (!key.empty()) { out += yamlQuoteKey(key); out += ':'; } out += '\n'; };
	switch (type) {
		case asvJSONValue::NULL_VAL:
			startLine(); out += "~\n"; break;
		case asvJSONValue::BOOL_VAL:
			startLine(); out += (flag ? "true" : "false"); out += '\n'; break;
		case asvJSONValue::INT:
			startLine(); out += std::to_string(num); out += '\n'; break;
		case asvJSONValue::DOUBLE: {
			if (std::isnan(dbl)) { startLine(); out += ".nan\n"; break; }
			if (std::isinf(dbl)) { startLine(); out += (dbl > 0 ? ".inf" : "-.inf"); out += '\n'; break; }
			char buf[32]; int n = snprintf(buf, sizeof(buf), "%.17g", dbl);
			startLine(); out.append(buf, static_cast<size_t>(n > 0 ? n : 0)); out += '\n';
			break;
		}
		case asvJSONValue::STRING: {
			startLine();
			auto& s = str_data;
			if (s.find('\n') != std::string_view::npos) {
				out += "|\n";
				out.append(static_cast<size_t>(indent) * 2 + 2, ' ');
				for (size_t i = 0; i < s.size(); i++) {
					out += s[i];
					if (s[i] == '\n' && i + 1 < s.size() && s[i+1] != '\n')
						{ out.append(static_cast<size_t>(indent) * 2 + 2, ' '); }
				}
			} else {
				out += yamlNeedsQuotes(s) ? yamlQuote(s) : s;
			}
			out += '\n';
			break;
		}
		case asvJSONValue::DATETIME: {
			char buf[40]; std::tm tm; asvjson_gmtime(&tm, &timestamp);
			char ms[16] = ""; if (datetime_ms > 0) snprintf(ms, sizeof(ms), ".%03d", datetime_ms);
			if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm) == 0)
				throw asvJSONError("Failed to format datetime");
			startLine(); out += buf; out += ms; out += "Z\n";
			break;
		}
		case asvJSONValue::BINARY: {
			auto enc = base64_encode(bin_data.data(), bin_data.size());
			startLine(); out += "!!binary \""; out += enc; out += "\"\n";
			break;
		}
		case asvJSONValue::OBJECTID: {
			std::string hex;
			for (size_t i = 0; i < str_data.size() && i < 12; i++) {
				char hb[4]; snprintf(hb, sizeof(hb), "%02x", static_cast<unsigned char>(str_data[i])); hex += hb;
			}
			startLine(); out += "!objectid "; out += yamlQuote(hex); out += '\n';
			break;
		}
		case asvJSONValue::REGEX: {
			size_t sep = str_data.find('|');
			std::string_view pat = (sep != std::string_view::npos) ? std::string_view(str_data.data(), sep) : std::string_view(str_data);
			std::string_view opt = (sep != std::string_view::npos) ? std::string_view(str_data.data() + sep + 1, str_data.size() - sep - 1) : std::string_view();
			startLine(); out += "!regex "; out += yamlQuote(pat); out += ' '; out += yamlQuote(opt); out += '\n';
			break;
		}
		case asvJSONValue::TIMESTAMP:
			startLine(); out += std::to_string(num); out += '\n'; break;
		case asvJSONValue::EXTENSION: {
			auto enc = base64_encode(bin_data.data(), bin_data.size());
			startLine(); out += "!ext " + std::to_string(ext_type) + ' '; out += yamlQuote(enc); out += '\n';
			break;
		}
		case asvJSONValue::OBJECT:
			if (!obj || obj->empty()) { startLine(); out += "{}\n"; break; }
			if (isArrayItem || !key.empty()) startBlock();
			for (const auto& [k, v] : *obj)
				v->toYAML(out, indent + (isArrayItem || !key.empty() ? 1 : 0), k, false);
			break;
		case asvJSONValue::ARRAY:
			if (!arr || arr->empty()) { startLine(); out += "[]\n"; break; }
			if (isArrayItem || !key.empty()) startBlock();
			for (const auto& v : *arr)
				v->toYAML(out, indent + (isArrayItem || !key.empty() ? 1 : 0), "", true);
			break;
	}
}

#endif
