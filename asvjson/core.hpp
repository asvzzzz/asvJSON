#pragma once
// asvJSON++ v1.6.0 - C++17 JSON library - Core Module
// 
// Configuration:
//   - Define ASVJSON_USE_ORDERED_MAP before including header for:
//     * Deterministic key order in serialized output (std::map)
//     * Transparent lookup with std::string_view, const char*, std::string
//   - Default (undefined): std::unordered_map for O(1) lookups
// 
// Features:
//   - JSON parsing/serialization with comments support
//   - JSON Pointer (RFC 6901)
//   - JSON Patch (RFC 6902) and JSON Merge Patch (RFC 7396)
//   - Base64 binary data encoding
//   - DateTime with milliseconds
//   - Nested key access (e.g., "user.address.city")
// 
// Architecture:
//   - Header-only library, C++17 standard
//   - unique_ptr for internal containers (obj/arr) and factory methods
//   - Factory methods return std::unique_ptr - automatic lifetime management
//
// Include individual format headers for MessagePack, BSON, TOON, TRON, GOON, XML, YAML, CSV, TOML.
//
// Important: string_view lifetime
//   - getStringView() returns string_view valid only until next parse() or destruction
//   - Do NOT store returned string_view for long-term use
//
// Security features:
//   - OOB protection in string parsing
//   - DateTime digit validation
//   - BSON array index limit (10M)
//   - Duplicate key handling
//   - Move constructor nulling
#ifndef ASVJSON_CORE_H
#define ASVJSON_CORE_H

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

#include "detail/common.hpp"
#ifdef _WIN32
namespace asvJSONInternal {
inline bool gmtimeSafe(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); return gmtime_s(tm, &tt) == 0; }
inline void localtimeSafe(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); localtime_s(tm, &tt); }
} // namespace asvJSONInternal
#define asvjson_timegm _mkgmtime
#else
namespace asvJSONInternal {
inline bool gmtimeSafe(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); return gmtime_r(&tt, tm) != nullptr; }
inline void localtimeSafe(std::tm* tm, const int64_t* t) { time_t tt = static_cast<time_t>(*t); localtime_r(&tt, tm); }
} // namespace asvJSONInternal
#define asvjson_timegm timegm
#endif

#include "detail/utf8.hpp"
#include "detail/escape.hpp"
#include "detail/base64.hpp"
#include "detail/datetime.hpp"
namespace asvJSONInternal {

// Object map type - default uses unordered_map for O(1) lookups
// Define ASVJSON_USE_ORDERED_MAP before including header for std::map (deterministic output)

// asvJSONError class
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

	static bool checkStringLen(size_t len) noexcept { return len <= MAX_STRING_LEN; }
	static bool checkArraySize(size_t n) noexcept { return n <= MAX_ARRAY_SIZE; }
	static bool checkObjectSize(size_t n) noexcept { return n <= MAX_OBJECT_SIZE; }
	static bool checkNestingDepth(int depth) noexcept { return depth <= static_cast<int>(MAX_NESTING_DEPTH); }

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
	bool raw_number = false;  // true when value is a string holding an exact numeric literal

	asvJSONValue() : type(NULL_VAL), ext_type(0), obj(), arr(), num(0), flag(false), is_float32(false), dbl(0), timestamp(0), datetime_ms(0), raw_number(false) {}
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
		raw_number = other.raw_number;
		other.type = NULL_VAL;
		other.datetime_ms = 0;
		other.is_float32 = false;
		other.ext_type = 0;
		other.raw_number = false;
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
			raw_number = other.raw_number;
			other.type = NULL_VAL;
			other.datetime_ms = 0;
			other.is_float32 = false;
			other.ext_type = 0;
			other.raw_number = false;
		}
		return *this;
	}

	void destroy() {
		str_data.clear();
		bin_data.clear();
		if (type == OBJECT) { obj.reset(); }
		else if (type == ARRAY) { arr.reset(); }
		type = NULL_VAL;
		datetime_ms = 0;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeString(const char* s, size_t len) {
		if (!asvJSONValue::checkStringLen(len)) return nullptr;
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = STRING;
		try { v->str_data.assign(s, len); } catch (...) { return nullptr; }
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeStringView(std::string_view sv) {
		return makeString(sv.data(), sv.size());
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeObject() {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = OBJECT;
		try { v->obj.reset(new ObjectMap()); } catch (...) { return nullptr; }
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeArray() {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = ARRAY;
		try { v->arr.reset(new std::vector<std::unique_ptr<asvJSONValue>>()); } catch (...) { return nullptr; }
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeInt(int64_t n) {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = INT;
		v->num = n;
		v->is_float32 = false;
		v->ext_type = 0;
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeBool(bool b) {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = BOOL_VAL;
		v->flag = b;
		v->is_float32 = false;
		v->ext_type = 0;
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeDouble(double d) {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = DOUBLE;
		v->dbl = d;
		v->is_float32 = false;
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeNull() {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = NULL_VAL;
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeDateTime(time_t ts, int ms = 0) {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = DATETIME;
		v->timestamp = ts + (ms / 1000);
		v->datetime_ms = static_cast<int>(ms % 1000);
		if (v->datetime_ms < 0) { v->datetime_ms += 1000; v->timestamp--; }
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeBinary(const uint8_t* data, size_t len) {
		if (!asvJSONValue::checkStringLen(len) || (len > 0 && data == nullptr)) return nullptr;
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = BINARY;
		try { if (len > 0) v->bin_data.assign(data, data + len); } catch (...) { return nullptr; }
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeExtension(int8_t extType, const uint8_t* data, size_t len) {
		if (!data && len > 0) return nullptr;
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = EXTENSION;
		v->ext_type = extType;
		try { if (len > 0) v->bin_data.assign(data, data + len); } catch (...) { return nullptr; }
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeObjectId(std::string_view oid) {
		if (oid.size() != 12) return nullptr;
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = OBJECTID;
		try { v->str_data.assign(oid.data(), oid.size()); } catch (...) { return nullptr; }
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeTimestamp(int64_t ts) {
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = TIMESTAMP;
		v->num = ts;
		return v;
	}

	[[nodiscard]] static std::unique_ptr<asvJSONValue> makeRegex(const char* pattern, const char* options) {
		if (!pattern) return nullptr;
		size_t patLen = strlen(pattern);
		if (patLen == 0) return nullptr;
		size_t optLen = options ? strlen(options) : 0;
		if (!asvJSONValue::checkStringLen(patLen) || !asvJSONValue::checkStringLen(optLen) || patLen + optLen > MAX_STRING_LEN - 2) return nullptr;
		auto v = std::unique_ptr<asvJSONValue>(new(std::nothrow) asvJSONValue());
		if (!v) return nullptr;
		v->type = REGEX;
		try {
			v->str_data.reserve(patLen + optLen + 1);
			v->str_data.append(pattern, patLen);
			v->str_data.push_back('\0');
			if (optLen > 0) v->str_data.append(options, optLen);
		} catch (...) { return nullptr; }
		return v;
	}

	[[nodiscard]] asvJSONValue* get(std::string_view key) const {
		if (key.empty() || type != OBJECT || !obj) return nullptr;
		auto it = mapFind(*obj, key);
		return (it != obj->end()) ? it->second.get() : nullptr;
	}

	[[nodiscard]] asvJSONValue* get(size_t idx) const {
		if (type != ARRAY || !arr || idx >= arr->size()) return nullptr;
		return (*arr)[idx].get();
	}

	const asvJSONValue* getConst(std::string_view key) const {
		if (key.empty() || type != OBJECT || !obj) return nullptr;
		auto it = mapFind(*obj, key);
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

	[[nodiscard]] bool hasKey(std::string_view key) const {
		return type == OBJECT && obj && mapCount(*obj, key) > 0;
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

	[[nodiscard]] std::string_view getStringView() const noexcept {
		return type == STRING ? std::string_view(str_data) : std::string_view();
	}

	const char* getString() const noexcept { return type == STRING ? str_data.c_str() : ""; }
	size_t getStringLen() const noexcept { return type == STRING ? str_data.size() : 0; }
	[[nodiscard]] int64_t getInt() const noexcept { return type == INT ? num : 0; }
	[[nodiscard]] double getDouble() const noexcept { return type == DOUBLE ? dbl : 0.0; }
	[[nodiscard]] bool getBool() const noexcept { return type == BOOL_VAL ? flag : false; }
	time_t getDateTime() const noexcept { return type == DATETIME ? timestamp : 0; }
	[[nodiscard]] int getDateTimeMs() const noexcept { return type == DATETIME ? datetime_ms : 0; }

	std::vector<uint8_t> getBinary() const {
		if ((type != BINARY && type != EXTENSION) || bin_data.empty()) return {};
		return bin_data;
	}

	void serialize(std::string& out, bool allowNaNInfinity = false) const;
	void serializePretty(std::string& out, int indent = 0, bool allowNaNInfinity = false) const;
	void toMessagePack(std::vector<uint8_t>& out) const;
	void toCBOR(std::vector<uint8_t>& out) const;
	void toBSON(std::vector<uint8_t>& out) const;
	void toXML(std::string& out) const;
	void toXML(std::string& out, const std::string& name, int indent) const;
	void toYAML(std::string& out) const;
	void toYAML(std::string& out, int indent, const std::string& key, bool isArrayItem) const;
	void toCSV(std::string& out) const;
	void toTOML(std::string& out) const;
	void toINI(std::string& out) const;
};

// JSON formatting helpers

inline int hexDigitValue(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

inline void appendJsonEscaped(std::string& out, std::string_view s) {
	for (auto c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
					out += buf;
				} else {
					out += c;
				}
				break;
		}
	}
}

void appendJsonToken(std::string& out, const asvJSONValue* v, bool allowNaNInfinity);
void parseMsgPackToken(const asvJSONValue* v, std::string& out, bool allowNaNInfinity);

inline void asvJSONValue::serialize(std::string& out, bool allowNaNInfinity) const {
	switch (type) {
		case NULL_VAL:
		case STRING:
		case INT:
		case BOOL_VAL:
		case DOUBLE:
		case DATETIME:
		case BINARY:
			appendJsonToken(out, this, allowNaNInfinity);
			break;
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
		case NULL_VAL:
		case STRING:
		case INT:
		case BOOL_VAL:
		case DOUBLE:
		case DATETIME:
		case BINARY:
			appendJsonToken(out, this, allowNaNInfinity);
			break;
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

inline std::unique_ptr<asvJSONValue> cloneValue(const asvJSONValue* v);


class asvJSON {
public:
	mutable std::string lastError;
	bool allowNaNInfinity = false;
public:
	size_t asvJSON::size() const {
		return root ? root->size() : 0;
	}
private:
	std::unique_ptr<asvJSONValue> root;
	std::string jsonBuf;
	std::string_view json;
	size_t pos = 0;
	int parseDepth = 0;

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

	inline char cur() const { return pos < json.size() ? json[pos] : 0; }
	inline void next() { pos++; }
	inline bool isIdentChar() const {
		char c = cur();
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}

	std::string_view parseStringRaw() {
		if (cur() != '"') throw asvJSONError("Expected string");
		size_t start = ++pos;
		while (pos < json.size()) {
			char c = json[pos];
			if (c == '"') break;
			if (c == '\\') {
				if (pos + 1 >= json.size()) throw asvJSONError("Unclosed escape");
				pos += 2;
			} else if (static_cast<unsigned char>(c) < 0x20) {
				throw asvJSONError("Control character in string");
			} else {
				++pos;
			}
		}
		if (pos >= json.size() || json[pos] != '"') throw asvJSONError("Unclosed string");
		size_t len = pos - start;
		if (!asvJSONValue::checkStringLen(len)) throw asvJSONError("String too long");
		next();
		return json.substr(start, len);
	}

	std::string parseStringKey() {
		std::string_view raw = parseStringRaw();
		if (!asvJSONValue::checkStringLen(raw.size())) throw asvJSONError("Object key too long");
		try { return unescapeJsonString(raw, true); }
		catch (const std::runtime_error&) { throw asvJSONError("Invalid escape in string"); }
	}

	std::unique_ptr<asvJSONValue> parseValue() {
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
			if (isIdentChar()) throw asvJSONError("Invalid true literal");
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
			if (isIdentChar()) throw asvJSONError("Invalid false literal");
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
			if (isIdentChar()) throw asvJSONError("Invalid null literal");
			return asvJSONValue::makeNull();
		}
		if (allowNaNInfinity && c == 'N') {
			if (json.compare(pos, 3, "NaN") == 0) { pos += 3; if (pos < json.size() && isIdentChar()) throw asvJSONError("Invalid NaN literal"); return asvJSONValue::makeDouble(NAN); }
		} else if (allowNaNInfinity && c == 'I') {
			if (json.compare(pos, 8, "Infinity") == 0) { pos += 8; if (pos < json.size() && isIdentChar()) throw asvJSONError("Invalid Infinity literal"); return asvJSONValue::makeDouble(INFINITY); }
		} else if (allowNaNInfinity && c == '-' && pos + 1 < json.size() && json[pos + 1] == 'I') {
			if (json.compare(pos, 9, "-Infinity") == 0) { pos += 9; if (pos < json.size() && isIdentChar()) throw asvJSONError("Invalid -Infinity literal"); return asvJSONValue::makeDouble(-INFINITY); }
		} else if (allowNaNInfinity && c == '-' && pos + 1 < json.size() && json[pos + 1] == 'N') {
			if (json.compare(pos, 4, "-NaN") == 0) { pos += 4; if (pos < json.size() && isIdentChar()) throw asvJSONError("Invalid -NaN literal"); return asvJSONValue::makeDouble(-NAN); }
		}
		if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
		throw asvJSONError(std::string("Unexpected: ") + c);
	}

	std::unique_ptr<asvJSONValue> parseStringOrSpecial() {
		std::string_view raw = parseStringRaw();
		if (raw.size() >= 20 && raw[4] == '-' && raw[7] == '-' && raw[10] == 'T') {
			time_t ts;
			int ms = 0;
			if (tryParseDateTime(raw, ts, &ms)) {
				return asvJSONValue::makeDateTime(ts, ms);
			}
		}
		std::string unescaped;
		try { unescaped = unescapeJsonString(raw, true); }
		catch (const std::runtime_error&) { throw asvJSONError("Invalid escape in string"); }
		return asvJSONValue::makeString(unescaped.c_str(), unescaped.size());
	}

	std::unique_ptr<asvJSONValue> parseObject() {
		if (!asvJSONValue::checkNestingDepth(++parseDepth)) { --parseDepth; throw asvJSONError("Maximum nesting depth exceeded"); }
		auto obj = asvJSONValue::makeObject();
		if (!obj) { --parseDepth; throw asvJSONError("Failed to allocate object"); }
		next();
		size_t objSize = 0;
		try {
			while (true) {
				skip();
				if (cur() == '}') { next(); break; }
				if (!asvJSONValue::checkObjectSize(objSize + 1)) { --parseDepth; throw asvJSONError("Object too large"); }
				std::string key = parseStringKey();
				skip();
				if (cur() != ':') throw asvJSONError("Expected ':'");
				next();
				auto val = parseValue();
				if (!val) { --parseDepth; throw asvJSONError("Failed to parse object value"); }
				auto it = mapFind(*(obj->obj), key);
				if (it != obj->obj->end()) { it->second = std::move(val); }
				else { obj->obj->emplace(std::move(key), std::move(val)); objSize++; }
				skip();
				if (cur() == '}') { next(); break; }
				if (cur() == ',') next();
				else throw asvJSONError("Expected ',' or '}'");
			}
		} catch (...) { --parseDepth; throw; }
		--parseDepth;
		// Special object detection (MongoDB Extended JSON)
		if (obj->obj->size() >= 1) {
			{
				auto oidIt = obj->obj->find("$oid");
				if (oidIt != obj->obj->end() && oidIt->second->type == asvJSONValue::STRING && oidIt->second->str_data.size() == 24 && obj->obj->size() == 1) {
					uint8_t bytes[12];
					for (int i = 0; i < 12; i++) {
						char buf[3] = {oidIt->second->str_data[i*2], oidIt->second->str_data[i*2+1], 0};
						bytes[i] = static_cast<uint8_t>(std::strtol(buf, nullptr, 16));
					}
					return asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<char*>(bytes), 12));
				}
			}
			{
				auto tsIt = obj->obj->find("$timestamp");
				if (tsIt != obj->obj->end() && tsIt->second->type == asvJSONValue::OBJECT && tsIt->second->obj && obj->obj->size() == 1) {
					auto tIt = tsIt->second->obj->find("t");
					if (tIt != tsIt->second->obj->end() && tIt->second->type == asvJSONValue::INT) {
						return asvJSONValue::makeTimestamp(tIt->second->num);
					}
				}
			}
			{
				auto binIt = obj->obj->find("$binary");
				if (binIt != obj->obj->end() && binIt->second->type == asvJSONValue::OBJECT && binIt->second->obj && obj->obj->size() == 1) {
					auto base64It = binIt->second->obj->find("base64");
					auto subIt = binIt->second->obj->find("subType");
					if (base64It != binIt->second->obj->end() && base64It->second->type == asvJSONValue::STRING) {
						auto data = decodeBase64Fast(base64It->second->str_data.data(), base64It->second->str_data.size());
						if (!data.empty()) {
							int subType = 0;
							if (subIt != binIt->second->obj->end() && subIt->second->type == asvJSONValue::STRING) {
								subType = static_cast<int>(std::strtol(subIt->second->str_data.c_str(), nullptr, 16));
							}
							if (subType == 0) {
								return asvJSONValue::makeBinary(data.data(), data.size());
							} else {
								return asvJSONValue::makeExtension(static_cast<int8_t>(subType), data.data(), data.size());
							}
						}
					}
				}
			}
			{
				auto regexIt = obj->obj->find("$regex");
				if (regexIt != obj->obj->end() && regexIt->second->type == asvJSONValue::STRING) {
					std::string pattern = regexIt->second->str_data;
					std::string opts;
					auto optsIt = obj->obj->find("$options");
					if (optsIt != obj->obj->end() && optsIt->second->type == asvJSONValue::STRING) {
						opts = optsIt->second->str_data;
					}
					return asvJSONValue::makeRegex(pattern.c_str(), opts.empty() ? nullptr : opts.c_str());
				}
			}
		}
		return obj;
	}

	std::unique_ptr<asvJSONValue> parseArray() {
		if (!asvJSONValue::checkNestingDepth(++parseDepth)) { --parseDepth; throw asvJSONError("Maximum nesting depth exceeded"); }
		auto arr = asvJSONValue::makeArray();
		if (!arr) { --parseDepth; throw asvJSONError("Failed to allocate array"); }
		arr->arr->reserve(16);
		next();
		while (true) {
			skip();
			if (cur() == ']') { next(); break; }
			if (!asvJSONValue::checkArraySize(arr->arr->size() + 1)) { --parseDepth; throw asvJSONError("Array too large"); }
			auto val = parseValue();
			if (!val) { --parseDepth; throw asvJSONError("Failed to parse array element"); }
			arr->arr->push_back(std::move(val));
			skip();
			if (cur() == ']') { next(); break; }
			if (cur() == ',') next();
			else throw asvJSONError("Expected ',' or ']'");
		}
		--parseDepth;
		return arr;
	}

	std::unique_ptr<asvJSONValue> parseNumber() {
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
		#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
			auto [ptr, ec] = std::from_chars(buf, buf + numLen, d);
			if (ec == std::errc() && ptr == buf + numLen) {
				// Check if precision is lost: count significant digits in the integer+fractional parts
				size_t sigDigits = 0;
				for (size_t i = (buf[0] == '-' ? 1 : 0); i < numLen; i++) {
					if (buf[i] != '.' && buf[i] != 'e' && buf[i] != 'E' && buf[i] != '+' && buf[i] != '-') sigDigits++;
				}
				// Also check if exponent shifts digits beyond double precision
				if (sigDigits > 17) {
					// Precision would be lost - store as raw number string
					auto v = asvJSONValue::makeString(buf, numLen);
					v->raw_number = true;
					return v;
				}
				if (!allowNaNInfinity && (std::isnan(d) || std::isinf(d))) throw asvJSONError("Invalid number: NaN or Infinity not allowed");
				return asvJSONValue::makeDouble(d);
			}
			if (ec == std::errc() && ptr != buf + numLen) throw asvJSONError("Invalid number");
		#else
			std::string numStr(buf, numLen);
			char* endptr;
			errno = 0;
			d = std::strtod(numStr.c_str(), &endptr);
			if (errno == ERANGE) throw asvJSONError("Invalid number: out of range");
			if (endptr != numStr.c_str() + numLen) throw asvJSONError("Invalid number");
			{
				size_t sigDigits = 0;
				for (size_t i = (buf[0] == '-' ? 1 : 0); i < numLen; i++) {
					if (buf[i] != '.' && buf[i] != 'e' && buf[i] != 'E' && buf[i] != '+' && buf[i] != '-') sigDigits++;
				}
				if (sigDigits > 17) {
					auto v = asvJSONValue::makeString(buf, numLen);
					v->raw_number = true;
					return v;
				}
			}
		#endif
			if (!allowNaNInfinity && (std::isnan(d) || std::isinf(d))) throw asvJSONError("Invalid number: NaN or Infinity not allowed");
			return asvJSONValue::makeDouble(d);
		} else {
			long long l;
			auto [ptr, ec] = std::from_chars(buf, buf + numLen, l);
			if (ec != std::errc() || ptr != buf + numLen) {
				// Integer too large for int64 - store as raw number string
				auto v = asvJSONValue::makeString(buf, numLen);
				v->raw_number = true;
				return v;
			}
			return asvJSONValue::makeInt(l);
		}
	}

public:
	asvJSON() = default;
	~asvJSON() = default;

	asvJSON(const asvJSON& other) {
		root = cloneValue(other.root.get());
	}

	asvJSON& operator=(const asvJSON& other) {
		if (this != &other) { root = cloneValue(other.root.get()); }
		return *this;
	}

	asvJSON(asvJSON&& other) noexcept
		: lastError(std::move(other.lastError)),
		  allowNaNInfinity(other.allowNaNInfinity),
		  root(std::move(other.root)),
		  jsonBuf(std::move(other.jsonBuf)),
		  json(jsonBuf),
		  pos(other.pos),
		  parseDepth(other.parseDepth) {
		other.pos = 0;
		other.parseDepth = 0;
		other.json = std::string_view();
	}

	asvJSON& operator=(asvJSON&& other) noexcept {
		if (this != &other) {
			root = std::move(other.root);
			jsonBuf = std::move(other.jsonBuf);
			json = jsonBuf;
			pos = other.pos;
			parseDepth = other.parseDepth;
			lastError = std::move(other.lastError);
			allowNaNInfinity = other.allowNaNInfinity;
			other.pos = 0;
			other.parseDepth = 0;
			other.json = std::string_view();
		}
		return *this;
	}

	bool parse(const std::string& s) {
		root = nullptr;
		jsonBuf = s;
		json = jsonBuf;
		pos = 0;
		try {
			root = parseValue();
			skip();
			if (pos != json.size()) { root = nullptr; lastError = "Trailing chars"; return false; }
			return root != nullptr;
		} catch (const asvJSONError& e) {
			lastError = e.what();
			root = nullptr;
			return false;
		}
	}

	bool parse(std::string_view s) {
		root = nullptr;
		jsonBuf.assign(s.data(), s.size());
		json = jsonBuf;
		pos = 0;
		try {
			root = parseValue();
			skip();
			if (pos != json.size()) { root = nullptr; lastError = "Trailing chars"; return false; }
			return root != nullptr;
		} catch (const asvJSONError& e) {
			lastError = e.what();
			root = nullptr;
			return false;
		}
	}

	std::string toTOON() const;
	bool fromTOON(std::string_view input);
	std::string toTRON() const;
	bool fromTRON(std::string_view input);
	std::string toGOON() const;
	bool fromGOON(std::string_view input);
	std::string toSexpr() const;
	bool fromSexpr(std::string_view input);
	std::string toJSON5(bool pretty = false) const;
	bool fromJSON5(std::string_view input);
	std::string toUDE(bool strict = false) const;
	bool fromUDE(std::string_view input, bool strict = false);

	std::string serialize(bool pretty = false) const {
		std::string out;
		out.reserve(512);
		if (root) {
			if (pretty) root->serializePretty(out, 0, allowNaNInfinity);
			else root->serialize(out, allowNaNInfinity);
		} else out.clear();
		return out;
	}

	size_t byteSize() const {
		if (!root) return 0;
		std::string out;
		out.reserve(512);
		root->serialize(out);
		return out.size();
	}

	bool writeToFile(const std::string& filename, bool pretty = false) const {
		std::ofstream out(filename);
		if (!out.is_open()) { lastError = "Failed to open file: " + filename; return false; }
		out << serialize(pretty);
		if (!out.good()) { lastError = "Failed to write file: " + filename; out.close(); return false; }
		out.close();
		return true;
	}

	bool readFromFile(const std::string& filename) {
		std::ifstream in(filename);
		if (!in.is_open()) { lastError = "Failed to open file: " + filename; return false; }
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();
		return parse(content);
	}

	[[nodiscard]] std::string getString(std::string_view key) const {
		if (!root) return "";
		auto* v = getNested(key);
		if (!v) return "";
		if (v->type == asvJSONValue::STRING) return std::string(v->str_data.data(), v->str_data.size());
		if (v->type == asvJSONValue::DATETIME) {
			char buf[32];
			std::tm tm;
			gmtimeSafe(&tm, &v->timestamp);
			std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
			return std::string(buf);
		}
		return "";
	}

	[[nodiscard]] std::string_view getStringView(std::string_view key) const noexcept {
		if (!root) return {};
		auto* v = getNested(key);
		if (!v || v->type != asvJSONValue::STRING) return {};
		return std::string_view(v->str_data.data(), v->str_data.size());
	}

	[[nodiscard]] int64_t getInt(std::string_view key) const noexcept {
		if (!root) return 0;
		auto* v = getNested(key);
		return v && v->type == asvJSONValue::INT ? v->num : 0;
	}

	[[nodiscard]] double getDouble(std::string_view key) const noexcept {
		if (!root) return 0.0;
		auto* v = getNested(key);
		if (!v) return 0.0;
		if (v->type == asvJSONValue::DOUBLE) return v->dbl;
		if (v->type == asvJSONValue::INT) return static_cast<double>(v->num);
		return 0.0;
	}

	[[nodiscard]] bool getBool(std::string_view key) const noexcept {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::BOOL_VAL ? v->flag : false;
	}

	[[nodiscard]] time_t getDateTime(std::string_view key) const {
		if (!root) return 0;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::DATETIME ? v->timestamp : 0;
	}

	[[nodiscard]] std::vector<uint8_t> getBinary(std::string_view key) const {
		if (!root) return {};
		auto* v = root->get(key);
		if (!v || !(v->type == asvJSONValue::BINARY || v->type == asvJSONValue::EXTENSION || v->type == asvJSONValue::OBJECTID)) return {};
		if (v->type == asvJSONValue::OBJECTID) {
			if (v->str_data.size() != 12) return {};
			return std::vector<uint8_t>(v->str_data.begin(), v->str_data.end());
		}
		return v->bin_data;
	}

	template<typename F>
	void setValue(std::string_view key, F&& factory) {
		if (!root || root->type != asvJSONValue::OBJECT) { root = asvJSONValue::makeObject(); }
		if (!root) return;
		auto v = factory();
		if (!v) return;
		auto it = mapFind(*root->obj, key);
		if (it != root->obj->end()) { it->second = std::move(v); }
		else { root->obj->emplace(std::string(key), std::move(v)); }
	}

	void setValue(std::string_view key, std::unique_ptr<asvJSONValue> val) {
		setValue(key, [v = std::move(val)]() mutable { return std::move(v); });
	}

	void putString(std::string_view key, std::string_view value) {
		setValue(key, [value]{ return asvJSONValue::makeString(value.data(), value.size()); });
	}

	asvJSONValue* putInt(std::string_view key, int64_t n) {
		auto v = asvJSONValue::makeInt(n);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putDouble(std::string_view key, double d) {
		auto v = asvJSONValue::makeDouble(d);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putFloat32(std::string_view key, float f) {
		auto v = asvJSONValue::makeDouble(static_cast<double>(f));
		if (!v) return nullptr;
		v->is_float32 = true;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putBool(std::string_view key, bool b) {
		auto v = asvJSONValue::makeBool(b);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putDateTime(std::string_view key, time_t ts, int ms = 0) {
		auto v = asvJSONValue::makeDateTime(ts, ms);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putNull(std::string_view key) {
		auto v = asvJSONValue::makeNull();
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putBinary(std::string_view key, const uint8_t* data, size_t len) {
		auto v = asvJSONValue::makeBinary(data, len);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	void putBinChunked(std::string_view key, const uint8_t* data, size_t size, size_t chunk_size = 76) {
		if (!root || root->type != asvJSONValue::OBJECT) { root = asvJSONValue::makeObject(); }
		if (!root) return;
		auto arr = asvJSONValue::makeArray();
		if (!arr) return;
		size_t bytes_per_chunk = (chunk_size / 4) * 3;
		for (size_t i = 0; i < size; i += bytes_per_chunk) {
			size_t chunk = std::min(bytes_per_chunk, size - i);
			std::string encoded = encodeBase64(data + i, chunk);
			auto v = asvJSONValue::makeString(encoded.c_str(), encoded.length());
			if (!v) return;
			arr->arr->emplace_back(std::move(v));
		}
		root->obj->emplace(std::string(key), std::move(arr));
	}

	std::vector<uint8_t> getBinChunked(std::string_view key) const {
		if (!root) return {};
		auto* v = root->get(key);
		if (!v || v->type != asvJSONValue::ARRAY || !v->arr) return {};
		std::vector<uint8_t> result;
		for (const auto& chunk : *v->arr) {
			if (chunk->type == asvJSONValue::BINARY && !chunk->bin_data.empty()) {
				result.insert(result.end(), chunk->bin_data.begin(), chunk->bin_data.end());
			} else if (chunk->type == asvJSONValue::STRING && !chunk->str_data.empty()) {
				bool err = false;
				auto decoded = decodeBase64Fast(chunk->str_data.data(), chunk->str_data.size(), &err);
				if (!err)
					result.insert(result.end(), decoded.begin(), decoded.end());
			}
		}
		return result;
	}

	asvJSONValue* putObjectId(std::string_view key, std::string_view oid) {
		auto v = asvJSONValue::makeObjectId(oid);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putTimestamp(std::string_view key, int64_t ts) {
		auto v = asvJSONValue::makeTimestamp(ts);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putRegex(std::string_view key, const char* pattern, const char* options) {
		auto v = asvJSONValue::makeRegex(pattern, options);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	asvJSONValue* putExtension(std::string_view key, int8_t extType, const uint8_t* data, size_t len) {
		auto v = asvJSONValue::makeExtension(extType, data, len);
		if (!v) return nullptr;
		auto* ptr = v.get();
		setValue(key, std::move(v));
		return ptr;
	}

	// Special type getters
	[[nodiscard]] std::string_view getObjectId(std::string_view key) const {
		if (!root) return {};
		auto* v = root->get(key);
		return (v && v->type == asvJSONValue::OBJECTID) ? std::string_view(v->str_data) : std::string_view();
	}

	[[nodiscard]] int64_t getTimestamp(std::string_view key) const {
		if (!root) return 0;
		auto* v = root->get(key);
		return (v && v->type == asvJSONValue::TIMESTAMP) ? v->num : 0;
	}

	[[nodiscard]] std::string_view getRegexPattern(std::string_view key) const {
		if (!root) return {};
		auto* v = root->get(key);
		if (!v || v->type != asvJSONValue::REGEX) return {};
		size_t sep = v->str_data.rfind('\0');
		return (sep != std::string_view::npos) ? std::string_view(v->str_data.data(), sep) : std::string_view(v->str_data);
	}

	[[nodiscard]] std::string_view getRegexOptions(std::string_view key) const {
		if (!root) return {};
		auto* v = root->get(key);
		if (!v || v->type != asvJSONValue::REGEX) return {};
		size_t sep = v->str_data.rfind('\0');
		return (sep != std::string_view::npos) ? std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1) : std::string_view();
	}

	[[nodiscard]] std::pair<std::string_view, std::string_view> getRegex(std::string_view key) const {
		if (!root) return {};
		auto* v = root->get(key);
		if (!v || v->type != asvJSONValue::REGEX) return {};
		size_t sep = v->str_data.rfind('\0');
		if (sep == std::string_view::npos) return {std::string_view(v->str_data), std::string_view()};
		return {std::string_view(v->str_data.data(), sep), std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1)};
	}

	bool getRegex(std::string_view key, std::string& pattern, std::string& options) const {
		if (!root) return false;
		auto* v = root->get(key);
		if (!v || v->type != asvJSONValue::REGEX) return false;
		size_t sep = v->str_data.rfind('\0');
		if (sep == std::string_view::npos) { pattern = v->str_data; options.clear(); }
		else { pattern = v->str_data.substr(0, sep); options = v->str_data.substr(sep + 1); }
		return true;
	}

	// Type checks
	bool isObjectId(std::string_view key) const { if (!root) return false; auto* v = root->get(key); return v && v->type == asvJSONValue::OBJECTID; }
	bool isTimestamp(std::string_view key) const { if (!root) return false; auto* v = root->get(key); return v && v->type == asvJSONValue::TIMESTAMP; }
	bool isRegex(std::string_view key) const { if (!root) return false; auto* v = root->get(key); return v && v->type == asvJSONValue::REGEX; }
	bool isBinary(std::string_view key) const { if (!root) return false; auto* v = root->get(key); return v && (v->type == asvJSONValue::BINARY || v->type == asvJSONValue::EXTENSION); }
	bool isDateTime(std::string_view key) const { if (!root) return false; auto* v = root->get(key); return v && v->type == asvJSONValue::DATETIME; }
	[[nodiscard]] std::string_view getObjectIdView(std::string_view key) const {
		auto* v = get(key);
		return (v && v->type == asvJSONValue::OBJECTID) ? std::string_view(v->str_data.data(), v->str_data.size()) : std::string_view();
	}

	[[nodiscard]] std::pair<int8_t, std::vector<uint8_t>> getExtension(std::string_view key) const {
		auto* v = get(key);
		if (v && v->type == asvJSONValue::EXTENSION) {
			return std::make_pair(v->ext_type, v->getBinary());
		}
		return std::make_pair<int8_t, std::vector<uint8_t>>(0, std::vector<uint8_t>());
	}

	bool isExtension(std::string_view key) const { if (!root) return false; auto* v = root->get(key); return v && v->type == asvJSONValue::EXTENSION; }

	// Nested special type getters
	std::string_view getNestedObjectId(std::string_view path) const {
		if (!root) return {};
		auto* v = getNested(path);
		return (v && v->type == asvJSONValue::OBJECTID) ? std::string_view(v->str_data) : std::string_view();
	}

	int64_t getNestedTimestamp(std::string_view path) const {
		if (!root) return 0;
		auto* v = getNested(path);
		return (v && v->type == asvJSONValue::TIMESTAMP) ? v->num : 0;
	}

	std::pair<std::string_view, std::string_view> getNestedRegex(std::string_view path) const {
		if (!root) return {};
		auto* v = getNested(path);
		if (!v || v->type != asvJSONValue::REGEX) return {};
		size_t sep = v->str_data.rfind('\0');
		if (sep == std::string_view::npos) return {std::string_view(v->str_data), std::string_view()};
		return {std::string_view(v->str_data.data(), sep), std::string_view(v->str_data.data() + sep + 1, v->str_data.size() - sep - 1)};
	}

	// Core get
	[[nodiscard]] asvJSONValue* getRoot() noexcept { return root.get(); }
	[[nodiscard]] const asvJSONValue* getRoot() const noexcept { return root.get(); }

	[[nodiscard]] asvJSONValue* get(std::string_view key) {
		return root ? root->get(key) : nullptr;
	}

	[[nodiscard]] const asvJSONValue* get(std::string_view key) const {
		return root ? root->get(key) : nullptr;
	}

	[[nodiscard]] const asvJSONValue* getNested(std::string_view path) const {
		if (!root || path.empty()) return root.get();
		const asvJSONValue* cur = root.get();
		size_t start = 0;
		while (start < path.size()) {
			if (!cur || cur->type != asvJSONValue::OBJECT) return nullptr;
			size_t end = start;
			bool hasEscape = false;
			while (end < path.size() && path[end] != '.') {
				if (path[end] == '\\' && end + 1 < path.size() && (path[end + 1] == '.' || path[end + 1] == '\\')) {
					hasEscape = true;
					end++;
				}
				end++;
			}
			if (start == end) return nullptr;
			std::string_view seg;
			std::string segBuf;
			if (hasEscape) {
				segBuf.reserve(end - start);
				for (size_t i = start; i < end; i++) {
					if (path[i] == '\\' && i + 1 < path.size()) i++;
					segBuf += path[i];
				}
				seg = segBuf;
			} else {
				seg = path.substr(start, end - start);
			}
			auto it = mapFind(*cur->obj, seg);
			if (it == cur->obj->end()) return nullptr;
			cur = it->second.get();
			start = end;
			if (start < path.size() && path[start] == '.') start++;
		}
		return cur;
	}

	[[nodiscard]] asvJSONValue* getNested(std::string_view path) {
		return const_cast<asvJSONValue*>(static_cast<const asvJSON*>(this)->getNested(path));
	}

	[[nodiscard]] bool hasKey(std::string_view key) const {
		return root && root->hasKey(key);
	}

	// remove, clear, size, getKeys
	void remove(std::string_view key) {
		if (!root || root->type != asvJSONValue::OBJECT || !root->obj) return;
		auto it = mapFind(*root->obj, key);
		if (it != root->obj->end()) root->obj->erase(it);
	}

	void clear() { root = nullptr; }

	std::vector<std::string> getKeys() const {
		std::vector<std::string> keys;
		if (root && root->type == asvJSONValue::OBJECT && root->obj) {
			keys.reserve(root->obj->size());
			for (const auto& [k, _] : *root->obj) keys.push_back(k);
		}
		return keys;
	}

	asvJSONValue* getObject() {
		if (!root || root->type != asvJSONValue::OBJECT) {
			root = asvJSONValue::makeObject();
		}
		return root.get();
	}
	const asvJSONValue* getObject() const { return root.get(); }

	const asvJSONValue* getConst(std::string_view key) const noexcept {
		return root ? root->getConst(key) : nullptr;
	}

	[[nodiscard]] const asvJSONValue* getConst(size_t idx) const noexcept {
		return root ? root->getConst(idx) : nullptr;
	}

	[[nodiscard]] const asvJSONValue* getArray(std::string_view key) const {
		auto* v = get(key);
		return v && v->type == asvJSONValue::ARRAY ? v : nullptr;
	}

	[[nodiscard]] asvJSONValue* getArray(std::string_view key) {
		return const_cast<asvJSONValue*>(static_cast<const asvJSON*>(this)->getArray(key));
	}

	asvJSONValue* getRootArray() const {
		return (root && root->type == asvJSONValue::ARRAY) ? root.get() : nullptr;
	}

	// Optional getters
	[[nodiscard]] std::string optString(std::string_view key, std::string_view def = "") const {
		if (!root) return std::string(def);
		auto* v = root->get(key);
		if (!v || v->type != asvJSONValue::STRING) return std::string(def);
		return std::string(v->str_data.data(), v->str_data.size());
	}

	[[nodiscard]] int64_t optInt(std::string_view key, int64_t def = 0) const {
		if (!root) return def;
		auto* v = root->get(key);
		return (v && v->type == asvJSONValue::INT) ? v->num : def;
	}

	[[nodiscard]] double optDouble(std::string_view key, double def = 0.0) const {
		if (!root) return def;
		auto* v = root->get(key);
		if (!v) return def;
		if (v->type == asvJSONValue::DOUBLE) return v->dbl;
		if (v->type == asvJSONValue::INT) return static_cast<double>(v->num);
		return def;
	}

	[[nodiscard]] bool optBool(std::string_view key, bool def = false) const {
		if (!root) return def;
		auto* v = root->get(key);
		return (v && v->type == asvJSONValue::BOOL_VAL) ? v->flag : def;
	}

	[[nodiscard]] bool isNull(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::NULL_VAL;
	}

	[[nodiscard]] bool isString(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::STRING;
	}

	[[nodiscard]] bool isInt(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::INT;
	}

	[[nodiscard]] bool isDouble(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::DOUBLE;
	}

	[[nodiscard]] bool isNumeric(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && (v->type == asvJSONValue::DOUBLE || v->type == asvJSONValue::INT);
	}

	[[nodiscard]] bool isBool(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::BOOL_VAL;
	}

	[[nodiscard]] bool isObject(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::OBJECT;
	}

	[[nodiscard]] bool isArray(std::string_view key) const {
		if (!root) return false;
		auto* v = root->get(key);
		return v && v->type == asvJSONValue::ARRAY;
	}

	std::string getDateTimeString(std::string_view key) const {
		if (!root) return "";
		auto* v = root->get(key);
		if (!v || v->type != asvJSONValue::DATETIME) return "";
		char buf[32];
		std::tm tm;
		gmtimeSafe(&tm, &v->timestamp);
		if (v->datetime_ms > 0)
			std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
		else
			std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
		std::string out(buf);
		if (v->datetime_ms > 0) {
			char msbuf[16];
			snprintf(msbuf, sizeof(msbuf), ".%03dZ", v->datetime_ms);
			out += msbuf;
		}
		return out;
	}

	int getDateTimeMs(std::string_view key) const {
		if (!root) return 0;
		auto* v = root->get(key);
		return (v && v->type == asvJSONValue::DATETIME) ? v->datetime_ms : 0;
	}

	time_t optDateTime(std::string_view key, time_t def = 0) const {
		if (!root) return def;
		auto* v = root->get(key);
		return (v && v->type == asvJSONValue::DATETIME) ? v->timestamp : def;
	}

	std::tm optDateTimeTM(std::string_view key, std::tm def = {}) const {
		std::tm tm = {};
		if (!root) return def;
		auto* v = root->get(key);
		if (v && v->type == asvJSONValue::DATETIME) {
			time_t t = v->timestamp;
			gmtimeSafe(&tm, &t);
			return tm;
		}
		return def;
	}

	// Array helpers
	asvJSONValue* arrayAddValue(std::unique_ptr<asvJSONValue> val) {
		if (!val) return nullptr;
		if (!root || root->type != asvJSONValue::ARRAY) {
			auto arr = asvJSONValue::makeArray();
			if (!arr) return nullptr;
			if (root) {
				auto wrapper = asvJSONValue::makeArray();
				if (!wrapper) return nullptr;
				wrapper->arr->push_back(std::move(root));
				root = std::move(wrapper);
			} else {
				root = std::move(arr);
			}
		}
		if (!asvJSONValue::checkArraySize(root->arr->size() + 1)) return nullptr;
		root->arr->push_back(std::move(val));
		return root->arr->back().get();
	}

	// These work on the root array (no key)
	asvJSONValue* arrayAddString(const char* s, size_t len) {
		return arrayAddValue(asvJSONValue::makeString(s, len));
	}
	asvJSONValue* arrayAddInt(int64_t n) {
		return arrayAddValue(asvJSONValue::makeInt(n));
	}
	asvJSONValue* arrayAddDouble(double d) {
		return arrayAddValue(asvJSONValue::makeDouble(d));
	}
	asvJSONValue* arrayAddBool(bool b) {
		return arrayAddValue(asvJSONValue::makeBool(b));
	}
	asvJSONValue* arrayAddNull() {
		return arrayAddValue(asvJSONValue::makeNull());
	}
	asvJSONValue* arrayAddDateTime(time_t ts, int ms = 0) {
		return arrayAddValue(asvJSONValue::makeDateTime(ts, ms));
	}

	// Key-based overloads: find/create array under key, then add
	asvJSONValue* arrayAddValue(std::string_view key, std::unique_ptr<asvJSONValue> val) {
		if (!root || root->type != asvJSONValue::OBJECT)
			root = asvJSONValue::makeObject();
		if (!root || !root->obj)
			return nullptr;

		auto it = root->obj->find(std::string(key));
		asvJSONValue* arrPtr;
		if (it != root->obj->end() && it->second->type == asvJSONValue::ARRAY) {
			arrPtr = it->second.get();
		} else {
			auto newArr = asvJSONValue::makeArray();
			if (!newArr) return nullptr;
			arrPtr = newArr.get();
			root->obj->insert_or_assign(std::string(key), std::move(newArr));
		}

		if (!asvJSONValue::checkArraySize(arrPtr->arr->size() + 1)) return nullptr;

		arrPtr->arr->push_back(std::move(val));
		return arrPtr->arr->back().get();
	}

	asvJSONValue* arrayAddString(std::string_view key, const char* s) {
		return arrayAddValue(key, asvJSONValue::makeString(s, strlen(s)));
	}
	asvJSONValue* arrayAddInt(std::string_view key, int64_t n) {
		return arrayAddValue(key, asvJSONValue::makeInt(n));
	}
	asvJSONValue* arrayAddDouble(std::string_view key, double d) {
		return arrayAddValue(key, asvJSONValue::makeDouble(d));
	}
	asvJSONValue* arrayAddBool(std::string_view key, bool b) {
		return arrayAddValue(key, asvJSONValue::makeBool(b));
	}
	asvJSONValue* arrayAddNull(std::string_view key) {
		return arrayAddValue(key, asvJSONValue::makeNull());
	}
	asvJSONValue* arrayAddDateTime(std::string_view key, time_t ts, int ms = 0) {
		return arrayAddValue(key, asvJSONValue::makeDateTime(ts, ms));
	}

	[[nodiscard]] const std::string& getLastError() const noexcept { return lastError; }

	static std::string typeToString(asvJSONValue::Type t) {
		return std::string(asvJSONValue::typeToString(t));
	}

	// Format methods
	std::vector<uint8_t> toMessagePack() const;
	bool fromMessagePack(const void* data, size_t size);
	std::vector<uint8_t> toCBOR() const;
	bool fromCBOR(const void* data, size_t size);
	std::string toBSON() const;
	bool fromBSON(const void* data, size_t size);
	std::vector<uint8_t> toProtobuf(const std::string& schemaJson = "") const;
	bool fromProtobuf(const void* data, size_t size, const std::string& schemaJson = "");
	std::string toProtobufText() const;
	bool fromProtobufText(const std::string& text);
	std::string toXML() const;
	bool fromXML(std::string_view input);
	std::string toYAML() const;
	bool fromYAML(std::string_view input);
	std::string toCSV() const;
	bool fromCSV(std::string_view input);
	std::string toTOML() const;
	bool fromTOML(std::string_view input);
	std::string toJSONLines() const;
	bool fromJSONLines(std::string_view input);
	std::string toINI() const;
	bool fromINI(std::string_view input);
	/// @brief Transfer ownership of the root value to the caller.
	/// @return Unique pointer to the root value (may be null).
	/// @note After calling, the document becomes empty; it can be reused via parse().
	[[nodiscard]] std::unique_ptr<asvJSONValue> releaseRoot() noexcept { return std::move(root); }

private:
	static bool isArrayIndex(std::string_view s) {
		if (s.empty()) return false;
		if (s.size() > 1 && s[0] == '0') return false;
		for (auto c : s) if (c < '0' || c > '9') return false;
		return true;
	}

public:
	// JSON Pointer
	asvJSONValue* getByPointer(std::string_view ptr);
	const asvJSONValue* getByPointer(std::string_view ptr) const;
	bool setByPointer(std::string_view ptr, std::unique_ptr<asvJSONValue> value);
	bool removeByPointer(std::string_view ptr);

	// Merge & Patch
	void merge(const asvJSON& other);
	bool applyPatch(const asvJSON& patch);
	asvJSON applyMergePatch(const asvJSON& patch, int depth = 0) const;

	// Static conversion helpers
	static std::vector<uint8_t> messagePackFromString(const std::string& jsonStr);
	static std::string stringFromMessagePack(const uint8_t* data, size_t size);
	static std::vector<uint8_t> bsonFromString(const std::string& jsonStr);
	static std::vector<uint8_t> cborFromString(const std::string& jsonStr);
	static std::vector<uint8_t> protobufFromString(const std::string& jsonStr);
	static std::string stringFromProtobuf(const uint8_t* data, size_t size);
	bool fromMessagePack(const std::string& data);
	bool fromBSON(const std::string& data);
	bool fromCBOR(const std::string& data);
	bool fromProtobuf(const std::string& data, const std::string& schemaJson = "");
};

// ======================= Type Formatting Helpers =======================

static void fmtDoubleVal(double d, std::string& out) {
	if (d == 0.0) { out += '0'; return; }
	std::string tmp;
	#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
		tmp.resize(64);
		auto [ptr, ec] = std::to_chars(tmp.data(), tmp.data() + tmp.size(), d);
		if (ec == std::errc()) { out.append(tmp.data(), ptr - tmp.data()); return; }
	#endif
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "%.16g", d);
	if (n > 0) out.append(buf, static_cast<size_t>(n));
}

static bool fmtNaNInfVal(double d, bool allowNaNInfinity, std::string& out) {
	if (!allowNaNInfinity) return false;
	if (std::isnan(d)) { out += "NaN"; return true; }
	if (d == INFINITY) { out += "Infinity"; return true; }
	if (d == -INFINITY) { out += "-Infinity"; return true; }
	return false;
}

static void fmtDateTimeVal(time_t ts, int ms, std::string& out) {
	char buf[32];
	std::tm tm{};
	if (!gmtimeSafe(&tm, &ts)) { out += "null"; return; }
	if (ms > 0) {
		std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
		out += buf;
		char msbuf[16];
		snprintf(msbuf, sizeof(msbuf), ".%03dZ", ms);
		out += msbuf;
	} else {
		std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
		out += buf;
	}
}

static void fmtObjectIdHexVal(std::string_view s, std::string& out) {
	static const char hex[] = "0123456789abcdef";
	for (unsigned char c : s) { out += hex[c >> 4]; out += hex[c & 0xF]; }
}

static void fmtRegexVal(std::string_view s, std::string& out) {
	size_t sep = 	s.rfind('\0');
	out.push_back('"');
	appendJsonEscaped(out, (sep != std::string_view::npos) ? s.substr(0, sep) : s);
	if (sep != std::string_view::npos && sep + 1 < s.size()) {
		out += '|';
		appendJsonEscaped(out, std::string_view(s.data() + sep + 1, s.size() - sep - 1));
	}
	out += '"';
}

static void fmtExtVal(int8_t type, const uint8_t* data, size_t len, std::string& out) {
	out += "__EXT__";
	char buf[8];
	snprintf(buf, sizeof(buf), "%d", static_cast<int>(type));
	out += buf;
	out += "__";
	out += encodeBase64(data, len);
}

static void appendJsonToken(std::string& out, const asvJSONValue* v, bool allowNaNInfinity) {
	switch (v->type) {
		case asvJSONValue::NULL_VAL: out += "null"; break;
		case asvJSONValue::BOOL_VAL: out += (v->flag ? "true" : "false"); break;
		case asvJSONValue::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) out.append(buf, ptr - buf);
			else out += '0';
			break;
		}
		case asvJSONValue::DOUBLE: {
			if (std::isnan(v->dbl) || std::isinf(v->dbl)) {
				if (allowNaNInfinity) { fmtNaNInfVal(v->dbl, true, out); break; }
				out += "null"; break;
			}
			fmtDoubleVal(v->dbl, out);
			break;
		}
		case asvJSONValue::STRING: {
			if (v->raw_number) { out.append(v->str_data); break; }
			out.push_back('"');
			appendJsonEscaped(out, v->str_data);
			out.push_back('"');
			break;
		}
		case asvJSONValue::DATETIME: {
			out.push_back('"');
			fmtDateTimeVal(v->timestamp, v->datetime_ms, out);
			out.push_back('"');
			break;
		}
		case asvJSONValue::BINARY: {
			out += "{\"$binary\":{\"base64\":\"";
			out += encodeBase64(v->bin_data.data(), v->bin_data.size());
			out += "\",\"subType\":\"00\"}}";
			break;
		}
		case asvJSONValue::OBJECTID: {
			out += "{\"$oid\":\"";
			fmtObjectIdHexVal(v->str_data, out);
			out += "\"}";
			break;
		}
		case asvJSONValue::REGEX: {
			{
				size_t sep = v->str_data.rfind('\0');
				std::string_view pattern = (sep != std::string_view::npos) ? v->str_data.substr(0, sep) : v->str_data;
				std::string_view opts = (sep != std::string_view::npos) ? v->str_data.substr(sep + 1) : "";
				out += "{\"$regex\":\"";
				appendJsonEscaped(out, pattern);
				out += "\"";
				if (!opts.empty()) {
					out += ",\"$options\":\"";
					appendJsonEscaped(out, opts);
					out += "\"";
				}
				out += "}";
			}
			break;
		}
		case asvJSONValue::TIMESTAMP: {
			out += "{\"$timestamp\":{\"t\":";
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) out.append(buf, ptr - buf);
			out += "}}";
			break;
		}
		case asvJSONValue::EXTENSION: {
			out += "{\"$binary\":{\"base64\":\"";
			out += encodeBase64(v->bin_data.data(), v->bin_data.size());
			char hex[3];
			snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(v->ext_type));
			out += "\",\"subType\":\"";
			out += hex;
			out += "\"}}";
			break;
		}
		default: out += "null"; break;
	}
}


// ======================= Core inline definitions =======================

inline std::unique_ptr<asvJSONValue> cloneValue(const asvJSONValue* v) {
	if (!v) return nullptr;
	switch (v->type) {
		case asvJSONValue::NULL_VAL: return asvJSONValue::makeNull();
		case asvJSONValue::BOOL_VAL: return asvJSONValue::makeBool(v->flag);
		case asvJSONValue::INT: return asvJSONValue::makeInt(v->num);
		case asvJSONValue::DOUBLE: { auto c = asvJSONValue::makeDouble(v->dbl); if (c) c->is_float32 = v->is_float32; return c; }
		case asvJSONValue::STRING: { auto c = asvJSONValue::makeString(v->str_data.data(), v->str_data.size()); if (c) c->raw_number = v->raw_number; return c; }
		case asvJSONValue::DATETIME: return asvJSONValue::makeDateTime(v->timestamp, v->datetime_ms);
		case asvJSONValue::BINARY: return asvJSONValue::makeBinary(v->bin_data.data(), v->bin_data.size());
		case asvJSONValue::OBJECTID: return asvJSONValue::makeObjectId(v->str_data);
		case asvJSONValue::REGEX: {
			size_t sep = v->str_data.rfind('\0');
			if (sep != std::string_view::npos) {
				auto pattern = v->str_data.substr(0, sep);
				auto opts = v->str_data.substr(sep + 1);
				return asvJSONValue::makeRegex(pattern.c_str(), opts.c_str());
			}
			return asvJSONValue::makeRegex(v->str_data.c_str(), "");
		}
		case asvJSONValue::TIMESTAMP: return asvJSONValue::makeTimestamp(v->num);
		case asvJSONValue::EXTENSION: return asvJSONValue::makeExtension(v->ext_type, v->bin_data.data(), v->bin_data.size());
		case asvJSONValue::ARRAY: {
			auto a = asvJSONValue::makeArray();
			if (!a) return nullptr;
			if (v->arr) {
				a->arr->reserve(v->arr->size());
				for (const auto& elem : *v->arr) { auto cloned = cloneValue(elem.get()); if (cloned) a->arr->push_back(std::move(cloned)); }
			}
			return a;
		}
		case asvJSONValue::OBJECT: {
			auto o = asvJSONValue::makeObject();
			if (!o) return nullptr;
			if (v->obj) {
#ifndef ASVJSON_USE_ORDERED_MAP
				o->obj->reserve(v->obj->size());
#endif
				for (const auto& [k, val] : *v->obj) {
					auto cloned = cloneValue(val.get());
					if (cloned) o->obj->emplace(k, std::move(cloned));
				}
			}
			return o;
		}
	}
	return asvJSONValue::makeNull();
}


// JSON Pointer, Merge, Patch

static std::string decodeJSONPointerKey(std::string_view seg) {
	std::string out;
	out.reserve(seg.size());
	for (size_t i = 0; i < seg.size(); i++) {
		if (seg[i] == '~' && i + 1 < seg.size()) {
			if (seg[i + 1] == '0') { out += '~'; i++; }
			else if (seg[i + 1] == '1') { out += '/'; i++; }
			else out += '~';
		} else {
			out += seg[i];
		}
	}
	return out;
}


inline asvJSONValue* asvJSON::getByPointer(std::string_view ptr) {
	if (!root) return nullptr;
	if (ptr.empty() || ptr == "/") return root.get();
	if (ptr[0] != '/') return nullptr;
	asvJSONValue* cur = root.get();
	size_t start = 1;
	while (start < ptr.size()) {
		if (!cur) return nullptr;
		size_t slash = ptr.find('/', start);
		std::string_view seg = (slash == std::string_view::npos) ? ptr.substr(start) : ptr.substr(start, slash - start);
		if (cur->type == asvJSONValue::OBJECT && cur->obj) {
			std::string key = decodeJSONPointerKey(seg);
			auto it = mapFind(*cur->obj, key);
			if (it == cur->obj->end()) return nullptr;
			cur = it->second.get();
		} else if (cur->type == asvJSONValue::ARRAY && cur->arr) {
			if (!isArrayIndex(seg)) return nullptr;
			size_t idx = 0;
			auto [p, ec] = std::from_chars(seg.data(), seg.data() + seg.size(), idx);
			if (ec != std::errc() || idx >= cur->arr->size()) return nullptr;
			cur = (*cur->arr)[idx].get();
		} else return nullptr;
		start = (slash == std::string_view::npos) ? ptr.size() : slash + 1;
	}
	return cur;
}

inline const asvJSONValue* asvJSON::getByPointer(std::string_view ptr) const {
	return const_cast<asvJSON*>(this)->getByPointer(ptr);
}

inline bool asvJSON::setByPointer(std::string_view ptr, std::unique_ptr<asvJSONValue> value) {
	if (ptr.empty() || ptr == "/") { root = std::move(value); return true; }
	if (ptr[0] != '/') return false;
	if (!root) {
		size_t nextSlash = ptr.find('/', 1);
		std::string_view firstSeg = (nextSlash == std::string_view::npos) ? ptr.substr(1) : ptr.substr(1, nextSlash - 1);
		if (isArrayIndex(firstSeg) || firstSeg == "-") {
			root = asvJSONValue::makeArray();
		} else {
			root = asvJSONValue::makeObject();
		}
		if (!root) return false;
	}
	asvJSONValue* cur = root.get();
	size_t start = 1;
	while (start < ptr.size()) {
		size_t slash = ptr.find('/', start);
		std::string_view seg = (slash == std::string_view::npos) ? ptr.substr(start) : ptr.substr(start, slash - start);
		bool isLast = (slash == std::string_view::npos);
		if (cur->type == asvJSONValue::OBJECT && cur->obj) {
			std::string key = decodeJSONPointerKey(seg);
			if (isLast) {
				cur->obj->insert_or_assign(std::move(key), std::move(value));
				return true;
			}
			auto it = mapFind(*cur->obj, key);
			if (it != cur->obj->end()) {
				cur = it->second.get();
			} else {
				size_t nextSlash = ptr.find('/', slash + 1);
				std::string_view nextSeg = (nextSlash == std::string_view::npos)
					? ptr.substr(slash + 1)
					: ptr.substr(slash + 1, nextSlash - slash - 1);
				if (isArrayIndex(nextSeg) || nextSeg == "-") {
					auto nextArr = asvJSONValue::makeArray();
					if (!nextArr) return false;
					auto* nextPtr = nextArr.get();
					cur->obj->emplace(std::move(key), std::move(nextArr));
					cur = nextPtr;
				} else {
					auto nextObj = asvJSONValue::makeObject();
					if (!nextObj) return false;
					auto* nextPtr = nextObj.get();
					cur->obj->emplace(std::move(key), std::move(nextObj));
					cur = nextPtr;
				}
			}
		} else if (cur->type == asvJSONValue::ARRAY && cur->arr) {
			if (seg == "-") {
				if (!isLast) return false;
				cur->arr->push_back(std::move(value));
				return true;
			}
			if (!isArrayIndex(seg)) return false;
			size_t idx = 0;
			auto [p, ec] = std::from_chars(seg.data(), seg.data() + seg.size(), idx);
			if (ec != std::errc()) return false;
			if (isLast) {
				if (idx >= cur->arr->size()) cur->arr->resize(idx + 1);
				(*cur->arr)[idx] = std::move(value);
				return true;
			}
			if (idx >= cur->arr->size()) return false;
			cur = (*cur->arr)[idx].get();
		} else return false;
		start = (slash == std::string_view::npos) ? ptr.size() : slash + 1;
	}
	return false;
}

inline bool asvJSON::removeByPointer(std::string_view ptr) {
	if (ptr.empty() || ptr == "/") { clear(); return true; }
	if (ptr[0] != '/') return false;
	if (!root) return false;
	if (ptr == "/-") return false;
	asvJSONValue* parent = nullptr;
	asvJSONValue* cur = root.get();
	std::string lastKey;
	size_t lastIdx = 0;
	bool lastIsArray = false;
	size_t start = 1;
	while (start < ptr.size()) {
		size_t slash = ptr.find('/', start);
		std::string_view seg = (slash == std::string_view::npos) ? ptr.substr(start) : ptr.substr(start, slash - start);
		bool isLast = (slash == std::string_view::npos);
		if (cur->type == asvJSONValue::OBJECT && cur->obj) {
			std::string key = decodeJSONPointerKey(seg);
			if (isLast) {
				auto it = mapFind(*cur->obj, key);
				if (it == cur->obj->end()) return false;
				cur->obj->erase(it);
				return true;
			}
			auto it = mapFind(*cur->obj, key);
			if (it == cur->obj->end()) return false;
			parent = cur; cur = it->second.get(); lastKey = key; lastIsArray = false;
		} else if (cur->type == asvJSONValue::ARRAY && cur->arr) {
			if (!isArrayIndex(seg)) return false;
			size_t idx = 0;
			auto [p, ec] = std::from_chars(seg.data(), seg.data() + seg.size(), idx);
			if (ec != std::errc() || idx >= cur->arr->size()) return false;
			if (isLast) {
				cur->arr->erase(cur->arr->begin() + static_cast<ptrdiff_t>(idx));
				return true;
			}
			parent = cur; cur = (*cur->arr)[idx].get(); lastIdx = idx; lastIsArray = true;
		} else return false;
		start = (slash == std::string_view::npos) ? ptr.size() : slash + 1;
	}
	return true;
}

// Merge & Patch

static void mergePatchRecursive(asvJSONValue* target, const asvJSONValue* patch, int depth) {
	if (!target || !patch) return;
	if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) return;
	if (patch->type != asvJSONValue::OBJECT || !patch->obj) {
		return;
	}
	if (target->type != asvJSONValue::OBJECT || !target->obj) {
		return;
	}
	for (const auto& [k, v] : *patch->obj) {
		if (v->type == asvJSONValue::NULL_VAL) {
			target->obj->erase(k);
			continue;
		}
		auto it = mapFind(*target->obj, k);
		if (it != target->obj->end() && it->second->type == asvJSONValue::OBJECT && v->type == asvJSONValue::OBJECT) {
			mergePatchRecursive(it->second.get(), v.get(), depth + 1);
		} else {
			target->obj->insert_or_assign(k, cloneValue(v.get()));
		}
	}
}


inline void asvJSON::merge(const asvJSON& other) {
	if (!other.root) return;
	if (!root || root->type != asvJSONValue::OBJECT) {
		root = cloneValue(other.root.get());
		return;
	}
	const asvJSONValue* patch = other.root.get();
	if (!patch || patch->type != asvJSONValue::OBJECT || !patch->obj) {
		root = cloneValue(patch);
		return;
	}
	for (const auto& [k, v] : *patch->obj) {
		auto it = mapFind(*root->obj, k);
		if (it != root->obj->end() && it->second->type == asvJSONValue::OBJECT && v->type == asvJSONValue::OBJECT) {
			mergePatchRecursive(it->second.get(), v.get(), 0);
		} else {
			root->obj->insert_or_assign(k, cloneValue(v.get()));
		}
	}
}

inline asvJSON asvJSON::applyMergePatch(const asvJSON& patch, int depth) const {
	asvJSON result = *this;
	if (!patch.root) return result;
	if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH)) return result;
	const asvJSONValue* p = patch.root.get();
	if (p->type != asvJSONValue::OBJECT || !p->obj) {
		result.root = cloneValue(p);
		return result;
	}
	if (!result.root || result.root->type != asvJSONValue::OBJECT || !result.root->obj) {
		result.root = asvJSONValue::makeObject();
	}
	for (const auto& [k, v] : *p->obj) {
		if (v->type == asvJSONValue::NULL_VAL) {
			if (result.root->obj) result.root->obj->erase(k);
		} else {
			auto it = result.root->obj ? mapFind(*result.root->obj, k) : result.root->obj->end();
			if (it != result.root->obj->end() && it->second->type == asvJSONValue::OBJECT && v->type == asvJSONValue::OBJECT) {
				mergePatchRecursive(it->second.get(), v.get(), depth + 1);
			} else {
				result.root->obj->insert_or_assign(k, cloneValue(v.get()));
			}
		}
	}
	return result;
}

static bool valuesEqual(const asvJSONValue* a, const asvJSONValue* b) {
	if (!a && !b) return true;
	if (!a || !b) return false;
	if (a->type != b->type) return false;
	switch (a->type) {
		case asvJSONValue::NULL_VAL: return true;
		case asvJSONValue::BOOL_VAL: return a->flag == b->flag;
		case asvJSONValue::INT: return a->num == b->num;
		case asvJSONValue::DOUBLE: return a->dbl == b->dbl;
		case asvJSONValue::STRING: return a->str_data == b->str_data;
		case asvJSONValue::DATETIME: return a->timestamp == b->timestamp && a->datetime_ms == b->datetime_ms;
		case asvJSONValue::BINARY: return a->bin_data == b->bin_data;
		case asvJSONValue::OBJECTID: return a->str_data == b->str_data;
		case asvJSONValue::TIMESTAMP: return a->num == b->num;
		case asvJSONValue::REGEX: return a->str_data == b->str_data;
		case asvJSONValue::EXTENSION: return a->ext_type == b->ext_type && a->bin_data == b->bin_data;
		case asvJSONValue::ARRAY: {
			if (static_cast<bool>(a->arr) != static_cast<bool>(b->arr)) return false;
			if (a->arr && b->arr) {
				if (a->arr->size() != b->arr->size()) return false;
				for (size_t i = 0; i < a->arr->size(); i++)
					if (!valuesEqual((*a->arr)[i].get(), (*b->arr)[i].get())) return false;
			}
			return true;
		}
		case asvJSONValue::OBJECT: {
			if (static_cast<bool>(a->obj) != static_cast<bool>(b->obj)) return false;
			if (a->obj && b->obj) {
				if (a->obj->size() != b->obj->size()) return false;
				for (const auto& [k, v] : *a->obj) {
					auto it = mapFind(*b->obj, k);
					if (it == b->obj->end()) return false;
					if (!valuesEqual(v.get(), it->second.get())) return false;
				}
			}
			return true;
		}
	}
	return false;
}


inline bool asvJSON::applyPatch(const asvJSON& patch) {
	if (!patch.root) return false;
	const asvJSONValue* p = patch.root.get();
	if (!p || p->type != asvJSONValue::ARRAY || !p->arr) return false;
	for (const auto& elem : *p->arr) {
		if (!elem || elem->type != asvJSONValue::OBJECT || !elem->obj) return false;
		auto opIt = mapFind(*elem->obj, std::string_view("op"));
		auto pathIt = mapFind(*elem->obj, std::string_view("path"));
		if (opIt == elem->obj->end() || pathIt == elem->obj->end()) return false;
		if (opIt->second->type != asvJSONValue::STRING || pathIt->second->type != asvJSONValue::STRING) return false;
		std::string_view op = opIt->second->str_data;
		std::string_view path = pathIt->second->str_data;
		if (op == "add" || op == "replace") {
			auto valIt = mapFind(*elem->obj, std::string_view("value"));
			if (valIt == elem->obj->end()) return false;
			if (!setByPointer(path, cloneValue(valIt->second.get()))) return false;
		} else if (op == "remove") {
			if (!removeByPointer(path)) return false;
		} else if (op == "move" || op == "copy") {
			auto fromIt = mapFind(*elem->obj, std::string_view("from"));
			if (fromIt == elem->obj->end() || fromIt->second->type != asvJSONValue::STRING) return false;
			auto* fromVal = getByPointer(fromIt->second->str_data);
			if (!fromVal) return false;
			if (op == "move") {
				auto fromClone = cloneValue(fromVal);
				if (!removeByPointer(fromIt->second->str_data)) return false;
				if (!setByPointer(path, std::move(fromClone))) return false;
			} else {
				if (!setByPointer(path, cloneValue(fromVal))) return false;
			}
		} else if (op == "test") {
			auto valIt = mapFind(*elem->obj, std::string_view("value"));
			if (valIt == elem->obj->end()) return false;
			auto* target = getByPointer(path);
			if (!target || !valuesEqual(target, valIt->second.get())) return false;
		} else return false;
	}
	return true;
}

// Wrapper definitions for format methods (bodies in their respective headers)
inline std::vector<uint8_t> asvJSON::toMessagePack() const {
	std::vector<uint8_t> out;
	if (root) root->toMessagePack(out);
	return out;
}

inline std::vector<uint8_t> asvJSON::toCBOR() const {
	std::vector<uint8_t> out;
	if (root) root->toCBOR(out);
	return out;
}

inline std::unique_ptr<asvJSONValue> parseMessagePack(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth);
inline std::unique_ptr<asvJSONValue> parseCBOR(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth);


inline bool asvJSON::fromMessagePack(const void* data, size_t size) {
	root = nullptr;
	if (!data || size < 1) return false;
	const uint8_t* u = static_cast<const uint8_t*>(data);
	try {
		size_t pos = 0;
		root = parseMessagePack(u, pos, size, 0);
		if (!root || pos != size) { root = nullptr; throw asvJSONError(pos != size ? "Trailing bytes" : "Parse failed"); }
		return true;
	} catch (const asvJSONError& e) { lastError = e.what(); root = nullptr; return false; }
}

inline bool asvJSON::fromCBOR(const void* data, size_t size) {
	root = nullptr;
	if (!data || size < 1) return false;
	const uint8_t* u = static_cast<const uint8_t*>(data);
	try {
		size_t pos = 0;
		root = parseCBOR(u, pos, size, 0);
		if (!root || pos != size) { root = nullptr; throw asvJSONError(pos != size ? "Trailing bytes" : "Parse failed"); }
		return true;
	} catch (const asvJSONError& e) { lastError = e.what(); root = nullptr; return false; }
}

inline std::string asvJSON::toBSON() const {
	std::vector<uint8_t> out;
	if (!root) return {};
	auto writeLE32 = [](std::vector<uint8_t>& buf, uint32_t v) {
		buf.push_back(static_cast<uint8_t>(v));
		buf.push_back(static_cast<uint8_t>(v >> 8));
		buf.push_back(static_cast<uint8_t>(v >> 16));
		buf.push_back(static_cast<uint8_t>(v >> 24));
	};
	std::vector<uint8_t> sub;
	sub.reserve(128);
	size_t subSizePos = sub.size();
	writeLE32(sub, 0);
	if (root->type == asvJSONValue::OBJECT && root->obj) {
		for (const auto& [k, v] : *root->obj) {
			std::vector<uint8_t> elem;
			elem.reserve(64 + k.size());
			size_t elemStart = elem.size();
			v->toBSON(elem);
			if (elem.size() > elemStart) {
				elem.insert(elem.begin() + elemStart + 1, k.begin(), k.end());
				elem.insert(elem.begin() + elemStart + 1 + k.size(), 0);
				sub.insert(sub.end(), elem.begin() + elemStart, elem.end());
			}
		}
	} else if (root->type == asvJSONValue::ARRAY && root->arr) {
		for (size_t i = 0; i < root->arr->size(); i++) {
			std::vector<uint8_t> elem;
			elem.reserve(64);
			size_t elemStart = elem.size();
			(*root->arr)[i]->toBSON(elem);
			if (elem.size() > elemStart) {
				char idx[16];
				int n = snprintf(idx, sizeof(idx), "%zu", i);
				elem.insert(elem.begin() + elemStart + 1, idx, idx + n);
				elem.insert(elem.begin() + elemStart + 1 + n, 0);
				sub.insert(sub.end(), elem.begin() + elemStart, elem.end());
			}
		}
	} else {
		root->toBSON(sub);
	}
	sub.push_back(0);
	if (sub.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) return {};
	uint32_t totalSize = static_cast<uint32_t>(sub.size());
	sub[subSizePos] = static_cast<uint8_t>(totalSize);
	sub[subSizePos + 1] = static_cast<uint8_t>(totalSize >> 8);
	sub[subSizePos + 2] = static_cast<uint8_t>(totalSize >> 16);
	sub[subSizePos + 3] = static_cast<uint8_t>(totalSize >> 24);
	out = std::move(sub);
	return std::string(out.begin(), out.end());
}

inline std::string asvJSON::toXML() const {
	std::string out;
	out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	if (root) {
		if (root->type == asvJSONValue::OBJECT && root->obj) {
			for (const auto& [k, v] : *(root->obj))
				v->toXML(out, k, 0);
		} else {
			root->toXML(out);
		}
	}
	return out;
}

inline std::string asvJSON::toYAML() const {
	std::string out;
	if (root) {
		if (root->type != asvJSONValue::ARRAY) {
			out += "---\n";
		}
		root->toYAML(out);
	}
	return out;
}

inline std::string asvJSON::toCSV() const {
	std::string out;
	if (root) root->toCSV(out);
	return out;
}

inline std::string asvJSON::toTOML() const {
	std::string out;
	if (root) root->toTOML(out);
	return out;
}

inline std::string asvJSON::toINI() const {
	std::string out;
	if (root) root->toINI(out);
	return out;
}

inline std::string asvJSON::toJSONLines() const {
	std::string out;
	if (!root) return out;
	if (root->type == asvJSONValue::ARRAY && root->arr) {
		for (const auto& elem : *root->arr) {
			elem->serialize(out, false);
			out += '\n';
		}
	} else {
		root->serialize(out, false);
		out += '\n';
	}
	return out;
}

inline bool asvJSON::fromJSONLines(std::string_view input) {
	try {
		auto arr = asvJSONValue::makeArray();
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
					throw asvJSONError("JSON Lines: invalid JSON near: " + std::string(line.substr(0, 30)));
				arr->arr->push_back(tmp.releaseRoot());
			}
			pos = end + 1;
		}
		root = std::move(arr);
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	}
}

inline std::unique_ptr<asvJSONValue> parseBSON(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth);


inline bool asvJSON::fromBSON(const void* data, size_t size) {
	root = nullptr;
	if (!data || size < 5) return false;
	try {
		size_t pos = 0;
		root = parseBSON(static_cast<const uint8_t*>(data), pos, size, 0);
		if (!root || pos != size) { root = nullptr; throw asvJSONError(pos != size ? "Trailing bytes" : "Parse failed"); }
		return true;
	} catch (const asvJSONError& e) { lastError = e.what(); root = nullptr; return false; }
}

// Static conversion helpers
inline std::vector<uint8_t> asvJSON::messagePackFromString(const std::string& jsonStr) {
	asvJSON j;
	if (!j.parse(jsonStr)) return {};
	return j.toMessagePack();
}

inline std::string asvJSON::stringFromMessagePack(const uint8_t* data, size_t size) {
	asvJSON j;
	if (!j.fromMessagePack(data, size)) return {};
	return j.serialize();
}

inline std::vector<uint8_t> asvJSON::bsonFromString(const std::string& jsonStr) {
	asvJSON j;
	if (!j.parse(jsonStr)) return {};
	auto s = j.toBSON();
	return std::vector<uint8_t>(s.begin(), s.end());
}

inline std::vector<uint8_t> asvJSON::cborFromString(const std::string& jsonStr) {
	asvJSON j;
	if (!j.parse(jsonStr)) return {};
	return j.toCBOR();
}

inline bool asvJSON::fromMessagePack(const std::string& data) {
	return fromMessagePack(static_cast<const void*>(data.data()), data.size());
}

inline bool asvJSON::fromBSON(const std::string& data) {
	return fromBSON(static_cast<const void*>(data.data()), data.size());
}

inline bool asvJSON::fromCBOR(const std::string& data) {
	return fromCBOR(static_cast<const void*>(data.data()), data.size());
}

} // namespace asvJSONInternal

using asvJSONInternal::asvJSONValue;
using asvJSONInternal::asvJSON;
using asvJSONInternal::asvJSONError;

#endif // ASVJSON_CORE_H
