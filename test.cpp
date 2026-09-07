#include "asvJSON++.hpp"
using namespace asvJSONInternal;
#include <iostream>
#include <cassert>
#include <cstring>
#include <ctime>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>
#include <limits>
#include <crtdbg.h>

int passed = 0;
int failed = 0;

#define TEST(name) void name()
#define RUN(name) do { \
	std::cout << "Testing " << #name << "... " << std::flush; \
	try { \
		name(); \
		std::cout << "PASSED" << std::endl; \
		passed++; \
	} catch (const std::exception& e) { \
		std::cout << "FAILED: " << e.what() << std::endl; \
		failed++; \
	} catch (...) { \
		std::cout << "FAILED: unknown exception" << std::endl; \
		failed++; \
	} \
} while(0)

#define ASSERT(cond) do { \
	if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); \
} while(0)

#define ASSERT_EQ(a, b) do { \
	if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " == " #b); \
} while(0)

struct ValueGuard {
	asvJSONValue* v;
	explicit ValueGuard(asvJSONValue* val) : v(val) {}
	~ValueGuard() { delete v; }
	ValueGuard(const ValueGuard&) = delete;
	ValueGuard& operator=(const ValueGuard&) = delete;
	ValueGuard(ValueGuard&& other) noexcept : v(other.v) { other.v = nullptr; }
	ValueGuard& operator=(ValueGuard&& other) noexcept { if (this != &other) { delete v; v = other.v; other.v = nullptr; } return *this; }
	asvJSONValue* operator->() { return v; }
	const asvJSONValue* operator->() const { return v; }
};

TEST(testMakeString) {
	auto v = asvJSONValue::makeString("hello", 5);
	ASSERT(v->type == asvJSONValue::STRING);
	ASSERT_EQ(v->str_data.size(), 5);
	ASSERT(strncmp(v->str_data.data(), "hello", 5) == 0);
}

TEST(testMakeInt) {
	auto v = asvJSONValue::makeInt(42);
	ASSERT(v->type == asvJSONValue::INT);
	ASSERT_EQ(v->num, 42);
}

TEST(testMakeDouble) {
	auto v = asvJSONValue::makeDouble(3.14);
	ASSERT(v->type == asvJSONValue::DOUBLE);
	ASSERT(v->dbl > 3.13 && v->dbl < 3.15);
}

TEST(testMakeBool) {
	auto vt = asvJSONValue::makeBool(true);
	ASSERT(vt->type == asvJSONValue::BOOL_VAL);
	ASSERT(vt->flag == true);
	
	auto vf = asvJSONValue::makeBool(false);
	ASSERT(vf->type == asvJSONValue::BOOL_VAL);
	ASSERT(vf->flag == false);
}

TEST(testMakeNull) {
	auto v = asvJSONValue::makeNull();
	ASSERT(v->type == asvJSONValue::NULL_VAL);
}

TEST(testMakeObject) {
	auto v = asvJSONValue::makeObject();
	ASSERT(v->type == asvJSONValue::OBJECT);
	ASSERT(v->obj != nullptr);
	ASSERT_EQ(v->size(), 0);
}

TEST(testMakeArray) {
	auto v = asvJSONValue::makeArray();
	ASSERT(v->type == asvJSONValue::ARRAY);
	ASSERT(v->arr != nullptr);
	ASSERT_EQ(v->size(), 0);
}

TEST(testMakeDateTime) {
	time_t now = time(nullptr);
	auto v = asvJSONValue::makeDateTime(now, 500);
	ASSERT(v->type == asvJSONValue::DATETIME);
	ASSERT_EQ(v->timestamp, now);
	ASSERT_EQ(v->datetime_ms, 500);
}

TEST(testMakeBinary) {
	uint8_t data[] = {0x01, 0x02, 0x03};
	auto v = asvJSONValue::makeBinary(data, 3);
	ASSERT(v->type == asvJSONValue::BINARY);
	ASSERT_EQ(v->bin_data.size(), 3);
	ASSERT(v->bin_data.data()[0] == 0x01);
}

TEST(testMakeObjectId) {
	const char oid[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
	auto v = asvJSONValue::makeObjectId(std::string_view(oid, 12));
	ASSERT(v->type == asvJSONValue::OBJECTID);
	ASSERT_EQ(v->str_data.size(), 12);
}

TEST(testMakeTimestamp) {
	auto v = asvJSONValue::makeTimestamp(1234567890);
	ASSERT(v->type == asvJSONValue::TIMESTAMP);
	ASSERT_EQ(v->num, 1234567890);
}

TEST(testMakeRegex) {
	auto v = asvJSONValue::makeRegex("pattern", "i");
	ASSERT(v->type == asvJSONValue::REGEX);
	ASSERT(v->str_data.size() > 0);
}

TEST(testObjectGet) {
	auto obj = asvJSONValue::makeObject();
	auto val = asvJSONValue::makeInt(100);
	obj->obj->emplace("key", std::move(val));
	
	auto* result = obj->get("key");
	ASSERT(result != nullptr);
	ASSERT_EQ(result->num, 100);
	
	auto* missing = obj->get("nonexistent");
	ASSERT(missing == nullptr);
	
}

TEST(testArrayGet) {
	auto arr = asvJSONValue::makeArray();
	arr->arr->push_back(asvJSONValue::makeInt(1));
	arr->arr->push_back(asvJSONValue::makeInt(2));
	arr->arr->push_back(asvJSONValue::makeInt(3));
	
	ASSERT_EQ(arr->get(size_t(0))->num, 1);
	ASSERT_EQ(arr->get(size_t(1))->num, 2);
	ASSERT_EQ(arr->get(size_t(2))->num, 3);
	ASSERT(arr->get(size_t(10)) == nullptr);
	
}

TEST(testSize) {
	auto obj = asvJSONValue::makeObject();
	ASSERT_EQ(obj->size(), 0);
	obj->obj->emplace("a", asvJSONValue::makeInt(1));
	ASSERT_EQ(obj->size(), 1);
	obj->obj->emplace("b", asvJSONValue::makeInt(2));
	ASSERT_EQ(obj->size(), 2);
	
	auto arr = asvJSONValue::makeArray();
	ASSERT_EQ(arr->size(), 0);
	arr->arr->push_back(asvJSONValue::makeInt(1));
	ASSERT_EQ(arr->size(), 1);
	arr->arr->push_back(asvJSONValue::makeInt(2));
	ASSERT_EQ(arr->size(), 2);
}

TEST(testValueHasKey) {
	auto obj = asvJSONValue::makeObject();
	obj->obj->emplace("exists", asvJSONValue::makeInt(1));
	
	ASSERT(obj->hasKey("exists") == true);
	ASSERT(obj->hasKey("nonexistent") == false);
	
}

TEST(testTypeToString) {
	ASSERT(asvJSONValue::typeToString(asvJSONValue::NULL_VAL) == "null");
	ASSERT(asvJSONValue::typeToString(asvJSONValue::STRING) == "string");
	ASSERT(asvJSONValue::typeToString(asvJSONValue::OBJECT) == "object");
	ASSERT(asvJSONValue::typeToString(asvJSONValue::ARRAY) == "array");
	ASSERT(asvJSONValue::typeToString(asvJSONValue::INT) == "int");
	ASSERT(asvJSONValue::typeToString(asvJSONValue::BOOL_VAL) == "bool");
	ASSERT(asvJSONValue::typeToString(asvJSONValue::DOUBLE) == "double");
	ASSERT(asvJSONValue::typeToString(asvJSONValue::DATETIME) == "datetime");
}

TEST(testGetStringLen) {
	auto v = asvJSONValue::makeString("hello", 5);
	ASSERT_EQ(v->getStringLen(), 5);
}

TEST(testValueGetInt) {
	auto v = asvJSONValue::makeInt(12345);
	ASSERT_EQ(v->getInt(), 12345);
}

TEST(testValueGetDouble) {
	auto v = asvJSONValue::makeDouble(2.718);
	ASSERT(v->getDouble() > 2.717 && v->getDouble() < 2.719);
}

TEST(testValueGetBool) {
	auto vt = asvJSONValue::makeBool(true);
	ASSERT(vt->getBool() == true);
	
	auto vf = asvJSONValue::makeBool(false);
	ASSERT(vf->getBool() == false);
}

TEST(testGetDateTime) {
	time_t now = time(nullptr);
	auto v = asvJSONValue::makeDateTime(now, 100);
	ASSERT_EQ(v->getDateTime(), now);
	ASSERT_EQ(v->getDateTimeMs(), 100);
}

TEST(testGetBinary) {
	uint8_t data[] = {0xAA, 0xBB, 0xCC};
	auto v = asvJSONValue::makeBinary(data, 3);
	auto bin = v->getBinary();
	ASSERT_EQ(bin.size(), 3);
	ASSERT_EQ(bin[0], 0xAA);
}

TEST(testParseString) {
	asvJSON json;
	json.parse(std::string("\"hello\""));
	ASSERT_EQ(json.serialize(), "\"hello\"");
}

TEST(testParseInt) {
	asvJSON json;
	json.parse(std::string("42"));
	ASSERT_EQ(json.serialize(), "42");
}

TEST(testParseDouble) {
	asvJSON json;
	json.parse(std::string("3.14159"));
	std::string s = json.serialize();
	ASSERT(s == "3.14159" || s == "3.1415899999999999");
}

TEST(testParseTrue) {
	asvJSON json;
	json.parse(std::string("true"));
	ASSERT_EQ(json.serialize(), "true");
}

TEST(testParseFalse) {
	asvJSON json;
	json.parse(std::string("false"));
	ASSERT_EQ(json.serialize(), "false");
}

TEST(testParseNull) {
	asvJSON json;
	json.parse(std::string("null"));
	ASSERT_EQ(json.serialize(), "null");
}

TEST(testParseObject) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"John\", \"age\": 30}"));
	ASSERT(json.get("name") != nullptr);
	ASSERT_EQ(json.getString("name"), "John");
	ASSERT_EQ(json.getInt("age"), 30);
}

TEST(testParseArray) {
	asvJSON json;
	json.parse(std::string("[1, 2, 3, 4, 5]"));
	auto* arr = json.getRootArray();
	ASSERT(arr != nullptr);
	ASSERT_EQ(arr->size(), 5);
}

TEST(testParseNested) {
	asvJSON json;
	json.parse(std::string("{\"user\": {\"name\": \"Alice\", \"age\": 25}}"));
	ASSERT_EQ(json.getString("user.name"), "Alice");
	ASSERT_EQ(json.getInt("user.age"), 25);
}

TEST(testParseEmpty) {
	asvJSON json;
	json.parse(std::string("{}"));
	ASSERT_EQ(json.size(), 0);
}

TEST(testParseEmptyArray) {
	asvJSON json;
	json.parse(std::string("[]"));
	auto* arr = json.getRootArray();
	ASSERT(arr != nullptr);
	ASSERT_EQ(arr->size(), 0);
}

TEST(testParseUTF8BOM) {
	// UTF-8 BOM (EF BB BF) must be stripped before parsing
	const char withBom[] = { '\xEF', '\xBB', '\xBF', '{', '"', 'a', '"', ':', '1', '}', 0 };
	{
		asvJSON json;
		ASSERT(json.parse(std::string_view(withBom, 10)));
		ASSERT_EQ(json.getInt("a"), int64_t(1));
	}
	// Leading whitespace after BOM
	const char bomWs[] = { '\xEF', '\xBB', '\xBF', ' ', '{', '"', 'a', '"', ':', '2', '}', 0 };
	{
		asvJSON json;
		ASSERT(json.parse(std::string_view(bomWs, 11)));
		ASSERT_EQ(json.getInt("a"), int64_t(2));
	}
	// BOM only at start; a BOM inside content is still rejected as invalid JSON
	{
		asvJSON json;
		ASSERT(json.parse(std::string_view("[1]")));
	}
	// std::string input (implicitly converts to string_view) must also strip BOM
	{
		asvJSON json;
		ASSERT(json.parse(std::string(withBom, 10)));
		ASSERT_EQ(json.getInt("a"), int64_t(1));
	}
	// readFromFile() uses the std::string parse path
	{
		asvJSON json;
		ASSERT(json.parse(std::string("\xEF\xBB\xBF" "{\"b\":3}")));
		ASSERT_EQ(json.getInt("b"), int64_t(3));
	}
}

TEST(testParseValidateUTF8) {
	// Off by default: invalid bytes inside strings are accepted as-is
	{
		const char bad[] = { '{', '"', 'a', '"', ':', '"', (char)0xC3, '(', '"', '}', 0 };
		asvJSON json;
		ASSERT(json.parse(std::string_view(bad, 10)));
	}
	// When enabled, the same input is rejected with a clear error
	{
		const char bad[] = { '{', '"', 'a', '"', ':', '"', (char)0xC3, '(', '"', '}', 0 };
		asvJSON json;
		json.validateUTF8 = true;
		ASSERT(!json.parse(std::string_view(bad, 10)));
		ASSERT_EQ(json.getLastError(), std::string("Invalid UTF-8 in input"));
	}
	// Valid UTF-8 (incl. multibyte and escaped) passes when enabled
	{
		asvJSON json;
		json.validateUTF8 = true;
		ASSERT(json.parse(std::string("{\"k\":\"\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\",\"u\":\"\\u00e9\"}")));
		ASSERT_EQ(json.getInt("missing"), 0);
	}
	// BOM is stripped before validation
	{
		asvJSON json;
		json.validateUTF8 = true;
		ASSERT(json.parse(std::string("\xEF\xBB\xBF" "{\"b\":3}")));
		ASSERT_EQ(json.getInt("b"), int64_t(3));
	}
}

TEST(testNegativeDateTime) {
	// Pre-1970 timestamps cannot be converted by gmtimeSafe; must not crash
	asvJSON json;
	json.putDateTime("dt", (time_t)-631152000); // 1950-01-01T00:00:00Z
	// getDateTimeString returns "" instead of reading uninitialized tm
	ASSERT_EQ(json.getDateTimeString("dt"), std::string(""));
	// Serializer emits the literal string "null" for the same value
	std::string s = json.serialize();
	ASSERT_EQ(s, std::string("{\"dt\":\"null\"}"));
	// And such a value round-trips through parse without crashing
	asvJSON j2;
	ASSERT(j2.parse(s));
	ASSERT_EQ(j2.serialize(), s);
}

TEST(testPutString) {
	asvJSON json;
	json.putString("name", "John Doe");
	ASSERT_EQ(json.getString("name"), "John Doe");
}

TEST(testPutInt) {
	asvJSON json;
	json.putInt("age", 30);
	ASSERT_EQ(json.getInt("age"), 30);
}

TEST(testPutDouble) {
	asvJSON json;
	json.putDouble("price", 19.99);
	ASSERT(json.getDouble("price") > 19.98 && json.getDouble("price") < 20.0);
}

TEST(testPutBool) {
	asvJSON json;
	json.putBool("active", true);
	ASSERT(json.getBool("active") == true);
	json.putBool("active", false);
	ASSERT(json.getBool("active") == false);
}

TEST(testPutDateTime) {
	asvJSON json;
	time_t now = time(nullptr);
	json.putDateTime("created", now);
	ASSERT_EQ(json.getDateTime("created"), now);
}

TEST(testPutNull) {
	asvJSON json;
	json.putNull("empty");
	ASSERT(json.isNull("empty") == true);
}

TEST(testPutBinary) {
	asvJSON json;
	uint8_t data[] = {0x01, 0x02, 0x03};
	json.putBinary("data", data, 3);
	auto bin = json.getBinary("data");
	ASSERT_EQ(bin.size(), 3);
}

TEST(testPutOverwrite) {
	asvJSON json;
	json.putInt("value", 10);
	json.putInt("value", 20);
	ASSERT_EQ(json.getInt("value"), 20);
}

TEST(testGetString) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"Test\"}"));
	ASSERT_EQ(json.getString("name"), "Test");
}

TEST(testGetInt) {
	asvJSON json;
	json.parse(std::string("{\"count\": 100}"));
	ASSERT_EQ(json.getInt("count"), 100);
}

TEST(testGetDouble) {
	asvJSON json;
	json.parse(std::string("{\"pi\": 3.14159}"));
	ASSERT(json.getDouble("pi") > 3.14);
}

TEST(testGetBool) {
	asvJSON json;
	json.parse(std::string("{\"flag\": true}"));
	ASSERT(json.getBool("flag") == true);
}

TEST(testIsNull) {
	asvJSON json;
	json.parse(std::string("{\"empty\": null, \"value\": 1}"));
	ASSERT(json.isNull("empty") == true);
	ASSERT(json.isNull("value") == false);
}

TEST(testHasKey) {
	asvJSON json;
	json.parse(std::string("{\"exists\": 1, \"other\": 2}"));
	ASSERT(json.hasKey("exists") == true);
	ASSERT(json.hasKey("missing") == false);
}

TEST(testOptString) {
	asvJSON json;
	json.parse(std::string("{\"key\": \"value\"}"));
	ASSERT_EQ(json.optString("key"), "value");
	ASSERT_EQ(json.optString("missing"), "");
	ASSERT_EQ(json.optString("missing", "default"), "default");
}

TEST(testOptInt) {
	asvJSON json;
	json.parse(std::string("{\"key\": 42}"));
	ASSERT_EQ(json.optInt("key"), 42);
	ASSERT_EQ(json.optInt("missing"), 0);
	ASSERT_EQ(json.optInt("missing", 100), 100);
}

TEST(testOptDouble) {
	asvJSON json;
	json.parse(std::string("{\"key\": 3.14}"));
	ASSERT(json.optDouble("key") > 3.13);
	ASSERT_EQ(json.optDouble("missing"), 0.0);
}

TEST(testOptBool) {
	asvJSON json;
	json.parse(std::string("{\"key\": true}"));
	ASSERT(json.optBool("key") == true);
	ASSERT(json.optBool("missing") == false);
}

TEST(testRemove) {
	asvJSON json;
	json.putInt("a", 1);
	json.putInt("b", 2);
	json.remove("a");
	ASSERT(json.hasKey("a") == false);
	ASSERT(json.hasKey("b") == true);
}

TEST(testClear) {
	asvJSON json;
	json.putInt("a", 1);
	json.putInt("b", 2);
	json.clear();
	ASSERT_EQ(json.size(), 0);
}

TEST(testSerialize) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"Test\"}"));
	std::string out = json.serialize();
	ASSERT(out.find("Test") != std::string::npos);
}

TEST(testSerializePretty) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"Test\"}"));
	std::string out = json.serialize(true);
	ASSERT(out.find('\n') != std::string::npos);
}

TEST(testSerializeNaNInfinity) {
	asvJSON json;
	json.putDouble("nan", std::numeric_limits<double>::quiet_NaN());
	json.putDouble("inf", std::numeric_limits<double>::infinity());
	std::string s1 = json.serialize();
	ASSERT(s1.find("null") != std::string::npos);
	json.allowNaNInfinity = true;
	std::string s2 = json.serialize();
	ASSERT(s2.find("NaN") != std::string::npos);
	ASSERT(s2.find("Infinity") != std::string::npos);
}

TEST(testWriteReadFile) {
	asvJSON json;
	json.putString("name", "Test");
	json.putInt("value", 42);
	
	json.writeToFile("test_output.json");
	
	asvJSON json2;
	json2.readFromFile("test_output.json");
	
	ASSERT_EQ(json2.getString("name"), "Test");
	ASSERT_EQ(json2.getInt("value"), 42);
	
	std::remove("test_output.json");
}

TEST(testMessagePack) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"Test\", \"count\": 42}"));
	
	auto mp = json.toMessagePack();
	ASSERT(mp.size() > 0);
	
	asvJSON json2;
	json2.fromMessagePack(mp.data(), mp.size());
	
	ASSERT_EQ(json2.getString("name"), "Test");
	ASSERT_EQ(json2.getInt("count"), 42);
}

TEST(testMessagePackRoundtrip) {
	asvJSON json;
	json.putString("str", "hello");
	json.putInt("num", 123);
	json.putBool("flag", true);
	json.putDouble("dbl", 1.5);
	json.putNull("nill");
	
	auto mp = json.toMessagePack();
	asvJSON json2;
	json2.fromMessagePack(mp.data(), mp.size());
	
	ASSERT_EQ(json2.getString("str"), "hello");
	ASSERT_EQ(json2.getInt("num"), 123);
}

TEST(testBSON) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"Test\", \"count\": 42}"));

	auto bson = json.toBSON();
	ASSERT(bson.size() > 0);

	asvJSON json2;
	json2.fromBSON(bson.data(), bson.size());

	ASSERT_EQ(json2.getString("name"), "Test");
	ASSERT_EQ(json2.getInt("count"), 42);
}

TEST(testBSONRegex) {
	asvJSON json;
	json.putRegex("re", "pattern", "ims");
	auto bson = json.toBSON();
	asvJSON json2;
	json2.fromBSON(bson.data(), bson.size());
	auto* v = json2.getRoot();
	if (!v) throw std::runtime_error("null root after BSON decode");
	auto* rv = v->get("re");
	if (!rv || rv->type != asvJSONValue::REGEX) throw std::runtime_error("not a regex after roundtrip");

	// regex without options (no '|' separator) - regression test for BSON toBSON bug #2
	asvJSON js2;
	js2.putRegex("re", "^pattern$", nullptr);
	auto bson2 = js2.toBSON();
	asvJSON js3;
	js3.fromBSON(bson2.data(), bson2.size());
	auto* v3 = js3.getRoot();
	if (!v3) throw std::runtime_error("null root for no-options regex");
	auto* rv3 = v3->get("re");
	if (!rv3 || rv3->type != asvJSONValue::REGEX) throw std::runtime_error("no-options regex lost after roundtrip");
}

TEST(testBSONRoundtrip) {
	asvJSON json;
	json.putString("str", "hello");
	json.putInt("num", 123);
	json.putBool("flag", true);
	json.putDouble("dbl", 1.5);
	
	auto bson = json.toBSON();
	asvJSON json2;
	json2.fromBSON(bson.data(), bson.size());
	
	ASSERT_EQ(json2.getString("str"), "hello");
	ASSERT_EQ(json2.getInt("num"), 123);
	ASSERT_EQ(json2.getBool("flag"), true);
	ASSERT(json2.getDouble("dbl") > 1.49 && json2.getDouble("dbl") < 1.51);
}

TEST(testGetByPointer) {
	asvJSON json;
	json.parse(std::string("{\"user\": {\"name\": \"Alice\"}}"));
	
	auto* result = json.getByPointer("/user/name");
	ASSERT(result != nullptr);
	ASSERT_EQ(std::string(result->str_data.data(), result->str_data.size()), "Alice");
}

TEST(testGetByPointerArray) {
	asvJSON json;
	json.parse(std::string("{\"items\": [10, 20, 30]}"));
	
	auto* result = json.getByPointer("/items/0");
	ASSERT(result != nullptr);
	ASSERT_EQ(result->num, 10);
}

TEST(testSetByPointer) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"Test\"}"));
	
	json.setByPointer("/name", asvJSONValue::makeString("Updated", 7));
	
	ASSERT_EQ(json.getString("name"), "Updated");
}

TEST(testSetByPointerArrayExpand) {
	asvJSON json;
	json.parse(std::string("{\"arr\": [1]}"));
	
	json.setByPointer("/arr/5", asvJSONValue::makeInt(2));
	
	auto* arr = json.getArray("arr");
	if (!arr) throw std::runtime_error("array not found");
	if (arr->size() != 6) throw std::runtime_error("array size should be 6");
}

TEST(testSetByPointerArrayAppend) {
	asvJSON json;
	json.parse(std::string("[1, 2, 3]"));
	
	json.setByPointer("/-", asvJSONValue::makeInt(4));

	auto* root = json.getRoot();
	if (!root || root->type != asvJSONValue::ARRAY) throw std::runtime_error("root not array");
	if (root->size() != 4) throw std::runtime_error("array size should be 4");
}

TEST(testGetDateTimeMethod) {
	asvJSON json;
	json.putDateTime("dt", 1234567890);
	json.putDateTime("dt2", 987654321);
	
	if (json.getDateTime("dt") != 1234567890) throw std::runtime_error("getDateTime failed");
	if (json.getDateTime("dt2") != 987654321) throw std::runtime_error("getDateTime failed");
}

TEST(testInvalidNumber) {
	asvJSON json;
	if (json.parse(std::string("1."))) throw std::runtime_error("1. should be invalid");
	if (json.parse(std::string(".5"))) throw std::runtime_error(".5 should be invalid");
	if (json.parse(std::string("1.e"))) throw std::runtime_error("1.e should be invalid");
	if (json.parse(std::string("1.e+"))) throw std::runtime_error("1.e+ should be invalid");
	if (!json.parse(std::string("1.5"))) throw std::runtime_error("1.5 should be valid");
	if (!json.parse(std::string("1e10"))) throw std::runtime_error("1e10 should be valid");
}

TEST(testBigNumberRoundTrip) {
	// Big decimal: too many significant digits for double precision
	{
		asvJSON json;
		ASSERT(json.parse(std::string("{\"val\": 12345678901987262932881764329723182403218436.19232714293743612984612}")));
		auto* v = json.getRoot()->get("val");
		ASSERT(v != nullptr);
		ASSERT(v->type == asvJSONValue::STRING);
		ASSERT(v->raw_number);
		// Round-trip: serialize and re-parse
		std::string out = json.serialize();
		ASSERT(out.find("12345678901987262932881764329723182403218436.19232714293743612984612") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.parse(out));
		auto* v2 = json2.getRoot()->get("val");
		ASSERT(v2 != nullptr);
		ASSERT(v2->type == asvJSONValue::STRING);
		ASSERT(v2->raw_number);
		ASSERT(std::string(v2->getString()) == "12345678901987262932881764329723182403218436.19232714293743612984612");
	}
	// Big integer: too large for int64
	{
		asvJSON json;
		ASSERT(json.parse(std::string("{\"val\": 999999999999999999999999999999}")));
		auto* v = json.getRoot()->get("val");
		ASSERT(v != nullptr);
		ASSERT(v->type == asvJSONValue::STRING);
		ASSERT(v->raw_number);
		std::string out = json.serialize();
		ASSERT(out.find("999999999999999999999999999999") != std::string::npos);
	}
	// Normal numbers still work as int/double
	{
		asvJSON json;
		ASSERT(json.parse(std::string("{\"a\": 42, \"b\": 3.14, \"c\": 15}")));
		ASSERT(json.getRoot()->get("a")->type == asvJSONValue::INT);
		ASSERT(json.getRoot()->get("b")->type == asvJSONValue::DOUBLE);
		ASSERT(json.getRoot()->get("c")->type == asvJSONValue::INT);
	}
}

TEST(testNumberOverflow) {
	// 1e999 overflows to +inf
	{
		asvJSON json;
		if (json.parse(std::string("{\"a\": 1e999}"))) throw std::runtime_error("1e999 should be rejected without allowNaNInfinity");
		json.allowNaNInfinity = true;
		if (!json.parse(std::string("{\"a\": 1e999}"))) throw std::runtime_error("1e999 should be accepted with allowNaNInfinity");
		if (!std::isinf(json.getDouble("a")) || json.getDouble("a") < 0) throw std::runtime_error("1e999 should be +inf");
	}
	// -1e999 overflows to -inf
	{
		asvJSON json;
		if (json.parse(std::string("{\"a\": -1e999}"))) throw std::runtime_error("-1e999 should be rejected without allowNaNInfinity");
		json.allowNaNInfinity = true;
		if (!json.parse(std::string("{\"a\": -1e999}"))) throw std::runtime_error("-1e999 should be accepted with allowNaNInfinity");
		if (!std::isinf(json.getDouble("a")) || json.getDouble("a") > 0) throw std::runtime_error("-1e999 should be -inf");
	}
	// 1e-999 underflows to 0 -> rejected even with allowNaNInfinity (finite result)
	{
		asvJSON json;
		if (json.parse(std::string("{\"a\": 1e-999}"))) throw std::runtime_error("1e-999 should be rejected without allowNaNInfinity");
		json.allowNaNInfinity = true;
		if (json.parse(std::string("{\"a\": 1e-999}"))) throw std::runtime_error("1e-999 should be rejected even with allowNaNInfinity (underflow, finite)");
	}
	// Normal large-but-finite doubles still parse fine
	{
		asvJSON json;
		ASSERT(json.parse(std::string("{\"a\": 1e308}")));
		if (json.getDouble("a") != 1e308) throw std::runtime_error("1e308 should round-trip exactly");
	}
}

TEST(testGetObjectIdView) {
	asvJSON json;
	const char oid[12] = {'5','0','7','f','1','f','7','7','b','c','f','8'};
	json.putObjectId("oid", std::string_view(oid, 12));

	auto sv = json.getObjectIdView("oid");
	if (sv.size() != 12) throw std::runtime_error("getObjectIdView length failed");
	if (std::memcmp(sv.data(), oid, 12) != 0) throw std::runtime_error("getObjectIdView content failed");
}

TEST(testRemoveByPointer) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1, \"b\": 2}"));
	
	json.removeByPointer("/b");
	
	ASSERT(json.hasKey("a") == true);
	ASSERT(json.hasKey("b") == false);
}

TEST(testMerge) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1, \"b\": 2}"));
	
	asvJSON patch;
	patch.parse(std::string("{\"b\": 3, \"c\": 4}"));
	
	json.merge(patch);
	
	ASSERT_EQ(json.getInt("a"), 1);
	ASSERT_EQ(json.getInt("b"), 3);
	ASSERT_EQ(json.getInt("c"), 4);
}

TEST(testMergeWithNonObject) {
	asvJSON target;
	target.parse(std::string("{\"a\": 1}"));
	asvJSON patch;
	patch.parse(std::string("42"));
	target.merge(patch);
	ASSERT_EQ(target.getRoot()->getInt(), 42);
}

TEST(testApplyPatch) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1}"));
	asvJSON patch;
	patch.parse(std::string("[{\"op\": \"replace\", \"path\": \"/a\", \"value\": 2}]"));
	ASSERT(json.applyPatch(patch) == true);
	ASSERT_EQ(json.optInt("a"), 2);
}

TEST(testApplyMergePatch) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1}"));
	asvJSON patch;
	patch.parse(std::string("{\"b\": 2}"));
	asvJSON result = json.applyMergePatch(patch);
	ASSERT_EQ(result.optInt("a"), 1);
	ASSERT_EQ(result.optInt("b"), 2);
}

TEST(testApplyMergePatchNullDelete) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1, \"b\": 2}"));
	asvJSON patch;
	patch.parse(std::string("{\"a\": null}"));
	asvJSON result = json.applyMergePatch(patch);
	ASSERT(result.hasKey("a") == false);
	ASSERT_EQ(result.optInt("b"), 2);
}

TEST(testCloneValue) {
	asvJSON json;
	json.parse(std::string("{\"name\": \"Test\", \"count\": 42}"));
	
	asvJSON json2;
	json2.parse(json.serialize());
	
	ASSERT_EQ(json2.getString("name"), "Test");
	ASSERT_EQ(json2.getInt("count"), 42);
}

TEST(testGetNested) {
	asvJSON json;
	json.parse(std::string("{\"user\": {\"profile\": {\"name\": \"Alice\"}}}"));
	
	auto* result = json.getNested("user.profile.name");
	ASSERT(result != nullptr);
	ASSERT_EQ(std::string(result->str_data.data(), result->str_data.size()), "Alice");

	json.parse(std::string("{\"user.name\": \"Bob\"}"));
	result = json.getNested("user\\.name");
	ASSERT(result != nullptr);
	ASSERT_EQ(std::string(result->str_data.data(), result->str_data.size()), "Bob");

	json.parse(std::string("{\"level1\": {\"level2.key\": \"Value\"}}"));
	result = json.getNested("level1.level2\\.key");
	ASSERT(result != nullptr);
	ASSERT_EQ(std::string(result->str_data.data(), result->str_data.size()), "Value");
}

TEST(testParseComments) {
	asvJSON json;
	bool ok = json.parse(std::string("{ // single line comment\n \"a\": 1 }"));
	ASSERT(ok == true);
	ASSERT_EQ(json.optInt("a"), 1);
}

TEST(testParseMultilineComments) {
	asvJSON json;
	bool ok = json.parse(std::string("{ /* multi\nline\ncomment */ \"b\": 2 }"));
	ASSERT(ok == true);
	ASSERT_EQ(json.optInt("b"), 2);
}

TEST(testParseHashComment) {
	asvJSON json;
	bool ok = json.parse(std::string("{ # hash comment\n \"c\": 3 }"));
	ASSERT(ok == true);
	ASSERT_EQ(json.optInt("c"), 3);
}

TEST(testNestingDepthLimit) {
	std::string deep = std::string(130, '[') + "null" + std::string(130, ']');
	asvJSON json;
	bool result = json.parse(deep);
	ASSERT(result == false);
	ASSERT(!json.getLastError().empty());
}

TEST(testStringTooLarge) {
	std::string huge(11 * 1024 * 1024, 'x');
	auto v = asvJSONValue::makeString(huge.c_str(), huge.size());
	ASSERT(v == nullptr);
}

TEST(testErrorMessages) {
	asvJSON json;
	bool ok = json.parse(std::string("{\"a\":}"));
	ASSERT(ok == false);
	ASSERT(!json.getLastError().empty());

	ok = json.parse(std::string("[1, 2,]"));
	ASSERT(ok); // Library extension: tolerates trailing commas (non-standard, RFC 8259 prohibits)
	ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->getInt(), 1);
	ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(1))->getInt(), 2);
}

TEST(testBasicParse) {
	asvJSON json;
	bool ok = json.parse(std::string("{\"a\":1}"));
	ASSERT(ok == true);
	ASSERT(json.hasKey("a") == true);
}

TEST(testControlCharsEscaped) {
	auto v = asvJSONValue::makeString("\x00\x01\x1F", 3);
	ASSERT(v != nullptr);
	std::string out;
	v->serialize(out);
	ASSERT(out.find("\\u0000") != std::string::npos);
}

TEST(testGetRoot) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1, \"b\": 2}"));
	auto* root = json.getRoot();
	if (!root) throw std::runtime_error("getRoot returned null");
	if (root->type != asvJSONValue::OBJECT) throw std::runtime_error("root is not object");
	if (root->size() != 2) throw std::runtime_error("wrong root size");
}

TEST(testMoveConstructor) {
	asvJSON json1;
	json1.parse(std::string("{\"x\": 10}"));
	asvJSON json2(std::move(json1));
	if (json2.getInt("x") != 10) throw std::runtime_error("move constructor failed");
}

TEST(testMoveAssignment) {
	asvJSON json1;
	json1.parse(std::string("{\"y\": 20}"));
	asvJSON json2;
	json2.parse(std::string("{\"z\": 30}"));
	json2 = std::move(json1);
	if (json2.getInt("y") != 20) throw std::runtime_error("move assignment failed");
}

TEST(testJSONPointerEscape) {
	asvJSON json;
	json.parse(std::string("{\"a/b\": {\"c~d\": 42}}"));
	auto* v = json.getByPointer("/a~1b/c~0d");
	if (!v) {
		throw std::runtime_error("pointer escape failed");
	}
	auto iv = v->getInt();
	if (iv != 42) throw std::runtime_error("wrong value");
}

TEST(testJSONPointerArrayAppend) {
	asvJSON json;
	json.parse(std::string("{\"arr\": [1, 2, 3]}"));
	bool ok = json.setByPointer("/arr/-", asvJSONValue::makeInt(4));
	ASSERT(ok);
	auto* arr = json.getArray("arr");
	ASSERT(arr != nullptr);
	ASSERT_EQ(arr->size(), 4);
	ASSERT_EQ(arr->get(3)->getInt(), 4);
	ok = json.setByPointer("/arr/-", asvJSONValue::makeInt(5));
	ASSERT(ok);
	arr = json.getArray("arr");
	ASSERT_EQ(arr->size(), 5);
	ASSERT_EQ(arr->get(4)->getInt(), 5);
}

TEST(testJSONPatchCopyMove) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1, \"b\": 2}"));
	// copy
	asvJSON patch;
	patch.parse(std::string("[{\"op\": \"copy\", \"from\": \"/a\", \"path\": \"/c\"}]"));
	bool ok = json.applyPatch(patch);
	if (!ok) throw std::runtime_error("copy apply failed");
	if (json.getInt("c") != 1) throw std::runtime_error("copy failed");
	if (json.getInt("a") != 1) throw std::runtime_error("copy should keep source");
	// move
	patch.parse(std::string("[{\"op\": \"move\", \"from\": \"/b\", \"path\": \"/d\"}]"));
	ok = json.applyPatch(patch);
	if (!ok) throw std::runtime_error("move apply failed");
	if (json.getInt("d") != 2) throw std::runtime_error("move target failed");
	if (json.hasKey("b")) throw std::runtime_error("move should remove source");
	// add
	patch.parse(std::string("[{\"op\": \"add\", \"path\": \"/e\", \"value\": 3}]"));
	ok = json.applyPatch(patch);
	if (!ok) throw std::runtime_error("add apply failed");
	if (json.getInt("e") != 3) throw std::runtime_error("add failed");
	// test (should succeed)
	patch.parse(std::string("[{\"op\": \"test\", \"path\": \"/a\", \"value\": 1}]"));
	ok = json.applyPatch(patch);
	if (!ok) throw std::runtime_error("test should pass");
	// test (should fail)
	patch.parse(std::string("[{\"op\": \"test\", \"path\": \"/a\", \"value\": 99}]"));
	ok = json.applyPatch(patch);
	if (ok) throw std::runtime_error("test should fail on mismatch");
	// remove
	patch.parse(std::string("[{\"op\": \"remove\", \"path\": \"/a\"}]"));
	ok = json.applyPatch(patch);
	if (!ok) throw std::runtime_error("remove apply failed");
	if (json.hasKey("a")) throw std::runtime_error("remove should remove key");
}

TEST(testJSONPatchTestIntDouble) {
	asvJSON json;
	json.parse(std::string("{\"x\": 1}"));
	// "test" with 1.0 (DOUBLE) should NOT match INT 1 (strict typing per RFC 6902)
	asvJSON patch;
	patch.parse(std::string("[{\"op\": \"test\", \"path\": \"/x\", \"value\": 1.0}]"));
	if (json.applyPatch(patch)) throw std::runtime_error("INT 1 should NOT match DOUBLE 1.0");
	// "test" with 1 (INT) should match INT 1
	patch.parse(std::string("[{\"op\": \"test\", \"path\": \"/x\", \"value\": 1}]"));
	if (!json.applyPatch(patch)) throw std::runtime_error("INT 1 should match INT 1");
	// "test" with 2 (INT) should NOT match 1 (INT)
	patch.parse(std::string("[{\"op\": \"test\", \"path\": \"/x\", \"value\": 2}]"));
	if (json.applyPatch(patch)) throw std::runtime_error("INT 2 should NOT match INT 1");
}

TEST(testBSONCorruptedData) {
	asvJSON json;
	bool ok = json.fromBSON(nullptr, 0);
	if (ok) throw std::runtime_error("null data should fail");
}

TEST(testBSONDecimal128) {
	// BSON decimal128 (element type 0x13) is rejected rather than silently
	// replaced with null, which would lose data.
	uint8_t data[] = {
		0x18, 0x00, 0x00, 0x00, // int32 length = 24
		0x13, 'd', 0x00,        // type decimal128, key "d"
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00                    // document terminator
	};
	asvJSON json;
	bool ok = json.fromBSON(data, sizeof(data));
	if (ok) throw std::runtime_error("decimal128 should fail");
	ASSERT(!json.getLastError().empty());
}

TEST(testMessagePackCorruptedData) {
	asvJSON json;
	// null/empty data
	bool ok = json.fromMessagePack(nullptr, 0);
	if (ok) throw std::runtime_error("null data should fail");

	// Truncated array header
	{
		uint8_t data[] = {0x93}; // array of 3, but no elements
		asvJSON json2;
		ok = json2.fromMessagePack(data, sizeof(data));
		if (ok) throw std::runtime_error("truncated array should fail");
		ASSERT(!json2.getLastError().empty());
	}

	// Truncated map header
	{
		uint8_t data[] = {0x81}; // map of 1, but no key/value
		asvJSON json2;
		ok = json2.fromMessagePack(data, sizeof(data));
		if (ok) throw std::runtime_error("truncated map should fail");
		ASSERT(!json2.getLastError().empty());
	}

	// Truncated string
	{
		uint8_t data[] = {0xa5, 'h', 'e', 'l'}; // str5 but only 3 bytes
		asvJSON json2;
		ok = json2.fromMessagePack(data, sizeof(data));
		if (ok) throw std::runtime_error("truncated string should fail");
		ASSERT(!json2.getLastError().empty());
	}

	// Truncated fixext4 (0xD6 needs 5 bytes: 1 type + 4 data)
	{
		uint8_t data[] = {0xD6, 0x01, 0x02}; // fixext4 but only 2 data bytes instead of 4
		asvJSON json2;
		ok = json2.fromMessagePack(data, sizeof(data));
		if (ok) throw std::runtime_error("truncated fixext4 should fail");
		ASSERT(!json2.getLastError().empty());
	}

	// Trailing bytes after valid value
	{
		uint8_t data[] = {0x01, 0x02}; // valid int 1, trailing byte 2
		asvJSON json2;
		ok = json2.fromMessagePack(data, sizeof(data));
		if (ok) throw std::runtime_error("trailing bytes should fail");
	}

	// Truncated bin8
	{
		uint8_t data[] = {0xc4, 0x05, 0x01, 0x02}; // bin8 len=5, only 2 bytes
		asvJSON json2;
		ok = json2.fromMessagePack(data, sizeof(data));
		if (ok) throw std::runtime_error("truncated bin8 should fail");
	}
}

TEST(testMsgPackUInt64Overflow) {
	// uint64 (0xCF) value above int64 max (0xFFFFFFFFFFFFFFFF)
	// must be rejected before makeInt() (which performs no range check).
	std::vector<uint8_t> data = { 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	asvJSON json;
	bool ok = json.fromMessagePack(data.data(), data.size());
	if (ok) throw std::runtime_error("fromMessagePack accepted uint64 above int64 max");
	ASSERT(!json.getLastError().empty());

	// Boundary: 0x7FFFFFFFFFFFFFFF must still parse as INT64_MAX.
	std::vector<uint8_t> maxData = { 0xCF, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	asvJSON json2;
	if (!json2.fromMessagePack(maxData.data(), maxData.size())) throw std::runtime_error("fromMessagePack rejected int64 max");
	ASSERT_EQ(json2.getRoot()->getInt(), INT64_MAX);
}

TEST(testDuplicateKeyNoLeak) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1}"));
	json.putInt("a", 2);
	json.putInt("a", 3);
	if (json.getInt("a") != 3) throw std::runtime_error("overwrite failed");
	auto* root = json.getRoot();
	if (!root || root->size() != 1) throw std::runtime_error("duplicate key leak");
}

TEST(testSerializePrettyIndentation) {
	asvJSON json;
	json.parse(std::string("{\"a\": {\"b\": 1}}"));
	auto s = json.serialize(true);
	size_t indentCount = 0;
	for (size_t i = 0; i + 1 < s.length(); i++) {
		if (s[i] == '\n' && s[i+1] == ' ') indentCount++;
	}
	if (indentCount == 0) throw std::runtime_error("no indentation found");
}

TEST(testGetConstOverloads) {
	asvJSON json;
	json.parse(std::string("{\"x\": \"hello\"}"));
	const asvJSON constJson = json;
	std::string result = constJson.getString("x");
	if (result != "hello") throw std::runtime_error("const getString failed");
}

TEST(testNestedAccessors) {
	// Build a nested document covering all value types
	asvJSON j;
	j.parse(std::string(R"({"user":{"name":"Alice","age":30,"score":9.5,"active":true,"nothing":null},"list":{"vals":[1,2,3]}})"));
	// Basic getters
	ASSERT_EQ(std::string(j.getString("user.name")), "Alice");
	ASSERT_EQ(j.getInt("user.age"), int64_t(30));
	ASSERT(j.getDouble("user.score") > 9.4 && j.getDouble("user.score") < 9.6);
	ASSERT_EQ(j.getBool("user.active"), true);
	// Optional getters (dotted path)
	ASSERT_EQ(std::string(j.optString("user.name")), "Alice");
	ASSERT_EQ(std::string(j.optString("user.missing", "dflt")), "dflt");
	ASSERT_EQ(j.optInt("user.age"), int64_t(30));
	ASSERT_EQ(j.optInt("user.missing", 7), int64_t(7));
	ASSERT(j.optDouble("user.score") > 9.4 && j.optDouble("user.score") < 9.6);
	ASSERT_EQ(j.optBool("user.active"), true);
	ASSERT_EQ(j.optBool("user.missing", true), true);
	// Type checks (dotted path)
	ASSERT(j.isNull("user.nothing"));
	ASSERT(j.isString("user.name"));
	ASSERT(j.isInt("user.age"));
	ASSERT(j.isDouble("user.score"));
	ASSERT(j.isNumeric("user.age"));
	ASSERT(j.isNumeric("user.score"));
	ASSERT(j.isBool("user.active"));
	ASSERT(!j.isObject("user.name"));
	ASSERT(j.isObject("user"));
	ASSERT(j.isArray("list.vals"));
	ASSERT(j.getNested("list.vals")->size() == size_t(3));
}

TEST(testBase64Encode) {
	uint8_t data[] = {0x01, 0x02, 0x03};
	std::string encoded = encodeBase64(data, 3);
	ASSERT_EQ(encoded, "AQID");
}

TEST(testBase64Decode) {
	std::string encoded = "AQID";
	auto decoded = decodeBase64Fast(encoded.c_str(), encoded.length());
	ASSERT_EQ(decoded.size(), 3);
	ASSERT_EQ(decoded[0], 0x01);
	ASSERT_EQ(decoded[1], 0x02);
	ASSERT_EQ(decoded[2], 0x03);
}

TEST(testBase64Roundtrip) {
	setBase64Chars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
	uint8_t data[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9};
	std::string encoded = encodeBase64(data, sizeof(data));
	auto decoded = decodeBase64Fast(encoded.c_str(), encoded.length());
	ASSERT_EQ(decoded.size(), sizeof(data));
	for (size_t i = 0; i < sizeof(data); i++) {
		ASSERT_EQ(decoded[i], data[i]);
	}
}

TEST(testBase64CustomCharset) {
	struct Base64Guard {
		std::string saved;
		Base64Guard() : saved(getBase64Chars()) {}
		~Base64Guard() { setBase64Chars(saved); }
	} guard;
	setBase64Chars("XYZabcdefghijklmnopqrstuvwxyz0123456789+/ABCDEFGHIJKLMNOPQRSTUV");
	std::string charset = getBase64Chars();
	ASSERT_EQ(charset.length(), 64U);
	
	uint8_t data[] = {0x01, 0x02, 0x03};
	std::string encoded = encodeBase64(data, 3);
	auto decoded = decodeBase64Fast(encoded.c_str(), encoded.length());
	ASSERT_EQ(decoded.size(), 3);
	ASSERT_EQ(decoded[0], 0x01);
	ASSERT_EQ(decoded[1], 0x02);
	ASSERT_EQ(decoded[2], 0x03);
}

TEST(testPutBinChunked) {
	asvJSON json;
	uint8_t data[200];
	for (size_t i = 0; i < 200; i++) data[i] = static_cast<uint8_t>(i);
	ASSERT(json.putBinChunked("large", data, 200, 76));
	auto retrieved = json.getBinChunked("large");
	ASSERT_EQ(retrieved.size(), 200U);
	for (size_t i = 0; i < 200; i++) {
		ASSERT_EQ(retrieved[i], static_cast<uint8_t>(i));
	}
	// Empty data
	asvJSON json2;
	ASSERT(json2.putBinChunked("empty", nullptr, 0, 76));
	auto emptyRet = json2.getBinChunked("empty");
	ASSERT_EQ(emptyRet.size(), 0U);
	// Small chunk sizes (clamped to 4) must not hang and must round-trip
	asvJSON json3;
	uint8_t data2[50];
	for (size_t i = 0; i < 50; i++) data2[i] = static_cast<uint8_t>(250 - i);
	for (size_t cs : {size_t(0), size_t(1), size_t(3), size_t(4), size_t(5)}) {
		std::string key = "cs" + std::to_string(cs);
		ASSERT(json3.putBinChunked(key, data2, 50, cs));
		auto r = json3.getBinChunked(key);
		ASSERT_EQ(r.size(), 50U);
		for (size_t i = 0; i < 50; i++) ASSERT_EQ(r[i], data2[i]);
	}
}

TEST(testGetKeys) {
	asvJSON json;
	json.parse(std::string("{}"));
	json.putString("a", "value_a");
	json.putString("b", "value_b");
	json.putString("c", "value_c");
	auto keys = json.getKeys();
	ASSERT_EQ(keys.size(), 3U);
	bool has_a = false, has_b = false, has_c = false;
	for (const auto& k : keys) {
		if (k == "a") has_a = true;
		if (k == "b") has_b = true;
		if (k == "c") has_c = true;
	}
	ASSERT(has_a && has_b && has_c);
}

TEST(testGetObject) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1, \"b\": 2}"));
	auto* obj = json.getObject();
	ASSERT(obj != nullptr);
	ASSERT_EQ(obj->type, asvJSONValue::OBJECT);
	ASSERT_EQ(obj->size(), 2);
	ASSERT(obj->get("a") != nullptr);
	ASSERT(obj->get("b") != nullptr);
	
	asvJSON json2;
	json2.parse(std::string("[1, 2, 3]"));
	auto* obj2 = json2.getObject();
	ASSERT(obj2 != nullptr);
	ASSERT_EQ(obj2->type, asvJSONValue::OBJECT);
	ASSERT_EQ(obj2->size(), 0);
}

TEST(testGetConst) {
	asvJSON json;
	json.parse(std::string("{\"key\": \"value\", \"num\": 42, \"arr\": [1,2,3]}"));
	asvJSONValue* root = json.get("");
	
	const asvJSONValue* val = root->getConst("key");
	ASSERT(val != nullptr);
	ASSERT_EQ(std::string(val->str_data.data(), val->str_data.size()), "value");
	
	val = root->getConst("num");
	ASSERT(val != nullptr);
	ASSERT_EQ(val->getInt(), 42);
	
	const asvJSONValue* arrVal = root->getConst("arr");
	ASSERT(arrVal != nullptr);
	ASSERT_EQ(arrVal->type, asvJSONValue::ARRAY);
	const asvJSONValue* elem0 = arrVal->getConst(static_cast<size_t>(0));
	ASSERT(elem0 != nullptr);
	ASSERT_EQ(elem0->getInt(), 1);
	
	const asvJSONValue* elem1 = arrVal->getConst(static_cast<size_t>(1));
	ASSERT(elem1 != nullptr);
	ASSERT_EQ(elem1->getInt(), 2);
	
	const asvJSONValue* elem2 = arrVal->getConst(static_cast<size_t>(2));
	ASSERT(elem2 != nullptr);
	ASSERT_EQ(elem2->getInt(), 3);
	
	ASSERT(arrVal->getConst(static_cast<size_t>(10)) == nullptr);
	
	ASSERT(root->getConst("missing") == nullptr);
}

TEST(testCloneValueDirect) {
	auto original = asvJSONValue::makeObject();
	original->obj->emplace("key", asvJSONValue::makeInt(42));
	auto cloned = cloneValue(original.get());
	ASSERT(cloned != nullptr);
	ASSERT_EQ(cloned->get("key")->getInt(), 42);
	auto newVal = asvJSONValue::makeInt(99);
	original->obj->find("key")->second.reset(newVal.release());
	ASSERT_EQ(cloned->get("key")->getInt(), 42);
}

TEST(testStressLargeArray) {
	std::string s = "[";
	for (int i = 0; i < 10000; i++) {
		if (i > 0) s += ",";
		s += std::to_string(i);
	}
	s += "]";
	asvJSON json;
	bool ok = json.parse(s);
	ASSERT(ok == true);
}

TEST(testStressManyKeys) {
	asvJSON json;
	json.parse(std::string("{}"));
	for (int i = 0; i < 1000; i++) {
		std::string key = "key_" + std::to_string(i);
		json.putInt(key.c_str(), i);
	}
	ASSERT_EQ(json.getKeys().size(), 1000U);
}

TEST(testStressDeepNesting) {
	std::string json_str = "null";
	for (int i = 0; i < 47; i++) {
		json_str = "[" + json_str + "]";
	}
	asvJSON json;
	bool ok = json.parse(json_str);
	ASSERT(ok == true);
}

std::atomic<int> thread_counter(0);

TEST(testCreationInThreads) {
	thread_counter = 0;
	std::thread t1([&]() {
		for (int i = 0; i < 100; i++) {
			asvJSON j;
			j.parse(std::string("{\"a\":1}"));
			thread_counter++;
		}
	});
	std::thread t2([&]() {
		for (int i = 0; i < 100; i++) {
			asvJSON j;
			j.parse(std::string("{\"b\":2}"));
			thread_counter++;
		}
	});
	t1.join();
	t2.join();
	ASSERT_EQ(thread_counter, 200);
}

TEST(testConcurrentAccess) {
	asvJSON shared;
	shared.parse(std::string("{\"counter\": 0}"));
	std::mutex mtx;
	std::atomic<int> sum{0};
	{
		std::vector<std::thread> threads;
		for (int i = 0; i < 4; i++) {
			threads.emplace_back([&shared, &mtx, &sum]() {
				for (int j = 0; j < 100; j++) {
					std::lock_guard<std::mutex> lock(mtx);
					auto val = shared.optInt("counter");
					shared.putInt("counter", val + 1);
					sum.fetch_add(1, std::memory_order_relaxed);
				}
			});
		}
		for (auto& t : threads) t.join();
	}
	ASSERT_EQ(sum.load(), 400);
}

TEST(testFuzzRandomStrings) {
	std::mt19937 rng(42);
	std::uniform_int_distribution<int> dist(0, 255);
	for (int iter = 0; iter < 100; iter++) {
		std::string s;
		s.reserve(100);
		for (int i = 0; i < 100; i++) {
			s.push_back(static_cast<char>(dist(rng)));
		}
		asvJSON json;
		json.parse(s);
	}
}

TEST(testFuzzEdgeCases) {
	const char* inputs[] = {
		"",
		"{",
		"[",
		"{\"",
		"[,",
		"{,}",
		"\"\\",
		"{{}",
		"[[]]",
		"truee",
		"falsse",
		"nullull"
	};
	for (auto* input : inputs) {
		asvJSON json;
		json.parse(std::string(input));
	}
}

TEST(testStringViewParse) {
	asvJSON json;
	std::string_view sv = "{\"x\": 42}";
	json.parse(sv);
	ASSERT(json.optInt("x") == 42);
}

TEST(testStringViewGet) {
	asvJSON json;
	json.parse(std::string("{\"key\": \"value\"}"));
	std::string result = json.getString(std::string_view("key"));
	ASSERT(result == "value");
}

TEST(testStringViewHasKey) {
	asvJSON json;
	json.parse(std::string("{\"exists\": 1}"));
	ASSERT(json.hasKey(std::string_view("exists")) == true);
	ASSERT(json.hasKey(std::string_view("missing")) == false);
}

TEST(testStringViewGetInt) {
	asvJSON json;
	json.parse(std::string("{\"num\": 100}"));
	ASSERT(json.getInt(std::string_view("num")) == 100);
}

TEST(testStringViewGetDouble) {
	asvJSON json;
	json.parse(std::string("{\"pi\": 3.14}"));
	ASSERT(json.getDouble(std::string_view("pi")) > 3.13);
}

TEST(testStringViewGetBool) {
	asvJSON json;
	json.parse(std::string("{\"flag\": true}"));
	ASSERT(json.getBool(std::string_view("flag")) == true);
}

TEST(testStringViewOptInt) {
	asvJSON json;
	json.parse(std::string("{\"key\": 42}"));
	ASSERT(json.optInt(std::string_view("key")) == 42);
	ASSERT(json.optInt(std::string_view("missing"), 99) == 99);
}

TEST(testStringViewOptBool) {
	asvJSON json;
	json.parse(std::string("{\"key\": true}"));
	ASSERT(json.optBool(std::string_view("key")) == true);
	ASSERT(json.optBool(std::string_view("missing")) == false);
}

TEST(testStringViewGetBinary) {
	asvJSON json;
	uint8_t data[] = {0x01, 0x02, 0x03};
	json.putBinary(std::string_view("data"), data, 3);
	auto bin = json.getBinary(std::string_view("data"));
	ASSERT(bin.size() == 3);
}

TEST(testStringViewRemove) {
	asvJSON json;
	json.parse(std::string("{\"a\": 1, \"b\": 2}"));
	json.remove(std::string_view("a"));
	ASSERT(json.hasKey(std::string_view("a")) == false);
	ASSERT(json.hasKey(std::string_view("b")) == true);
}

TEST(testValueGetStringView) {
	auto v = asvJSONValue::makeString("test", 4);
	std::string_view sv = v->getStringView();
	ASSERT(sv == "test");
	ASSERT(sv.length() == 4);
}

TEST(testValueGetConstSizeT) {
	asvJSON json;
	json.parse(std::string("{\"arr\": [10, 20, 30]}"));
	auto* arr = json.getArray(std::string_view("arr"));
	ASSERT(arr != nullptr);
const asvJSONValue* c = arr->getConst(static_cast<size_t>(0));
	ASSERT(c != nullptr);
	ASSERT(c->getInt() == 10);
}

TEST(testParseStringView) {
	asvJSON json;
	std::string_view sv = "{\"name\": \"Alice\", \"age\": 25}";
	json.parse(sv);
	ASSERT(json.getString(std::string_view("name")) == "Alice");
	ASSERT(json.getInt(std::string_view("age")) == 25);
}

TEST(testUnicode) {
	{
		asvJSON json;
		json.parse(std::string("\"A\""));
		ASSERT_EQ(json.getRoot()->type, asvJSONValue::STRING);
		ASSERT_EQ(json.getRoot()->str_data.size(), 1);
	}
	{
		asvJSON json;
		json.parse(std::string("\"\\u00E9\""));
		ASSERT_EQ(json.getRoot()->type, asvJSONValue::STRING);
		ASSERT_EQ(json.getRoot()->str_data.size(), 2);
		ASSERT(static_cast<unsigned char>(json.getRoot()->str_data.data()[0]) == 0xC3);
		ASSERT(static_cast<unsigned char>(json.getRoot()->str_data.data()[1]) == 0xA9);
	}
	{
		asvJSON json;
		json.parse(std::string("\"\\uD83D\\uDE00\""));
		std::string expected = "\xF0\x9F\x98\x80";
		ASSERT_EQ(json.getRoot()->type, asvJSONValue::STRING);
		ASSERT_EQ(json.getRoot()->str_data.size(), static_cast<size_t>(4));
		ASSERT(memcmp(json.getRoot()->str_data.data(), expected.data(), 4) == 0);
	}
	{
		asvJSON json;
		json.parse(std::string("\"\xF0\x9F\x98\x80\""));
		std::string expected = "\xF0\x9F\x98\x80";
		ASSERT_EQ(json.getRoot()->type, asvJSONValue::STRING);
		ASSERT_EQ(json.getRoot()->str_data.size(), static_cast<size_t>(4));
		ASSERT(memcmp(json.getRoot()->str_data.data(), expected.data(), 4) == 0);
	}
	{
		asvJSON json;
		json.parse(std::string("{\"a\":\"\\uD83D\\uDE00\",\"b\":\"\\u0041\"}"));
		std::string s = json.getString("a");
		ASSERT_EQ(s.size(), static_cast<size_t>(4));
		ASSERT_EQ(static_cast<unsigned char>(s[0]), 0xF0);
		ASSERT_EQ(static_cast<unsigned char>(s[3]), 0x80);
		ASSERT_EQ(json.getString("b"), "A");
		std::string out = json.serialize();
		asvJSON json2;
		json2.parse(out);
		ASSERT_EQ(json2.getString("b"), "A");
		ASSERT_EQ(json2.getString("a").size(), static_cast<size_t>(4));
	}
}

TEST(testMessagePackExt) {
	{
		uint8_t extData[] = {0xDE, 0xAD, 0xBE, 0xEF};
		asvJSON json;
		json.putExtension("ext", 7, extData, 4);
		ASSERT(json.isExtension("ext"));
		auto ext = json.getExtension("ext");
		ASSERT_EQ(ext.first, 7);
		ASSERT_EQ(ext.second.size(), static_cast<size_t>(4));
		ASSERT_EQ(ext.second[0], 0xDE);
		auto mp = json.toMessagePack();
		asvJSON json2;
		json2.fromMessagePack(mp.data(), mp.size());
		ASSERT(json2.isExtension("ext"));
		auto ext2 = json2.getExtension("ext");
		ASSERT_EQ(ext2.first, 7);
		ASSERT_EQ(ext2.second.size(), static_cast<size_t>(4));
		ASSERT_EQ(ext2.second[0], 0xDE);
	}
	{
		uint8_t ext1[] = {0x01};
		asvJSON json;
		json.putExtension("a", 5, ext1, 1);
		uint8_t ext2[] = {0x01, 0x02};
		json.putExtension("b", 6, ext2, 2);
		uint8_t ext4[] = {0x01, 0x02, 0x03, 0x04};
		json.putExtension("c", 7, ext4, 4);
		uint8_t ext8[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
		json.putExtension("d", 8, ext8, 8);
		auto mp = json.toMessagePack();
		asvJSON json2;
		json2.fromMessagePack(mp.data(), mp.size());
		ASSERT_EQ(json2.getExtension("a").first, 5);
		ASSERT_EQ(json2.getExtension("a").second.size(), static_cast<size_t>(1));
		ASSERT_EQ(json2.getExtension("b").first, 6);
		ASSERT_EQ(json2.getExtension("b").second.size(), static_cast<size_t>(2));
		ASSERT_EQ(json2.getExtension("c").first, 7);
		ASSERT_EQ(json2.getExtension("c").second.size(), static_cast<size_t>(4));
		ASSERT_EQ(json2.getExtension("d").first, 8);
		ASSERT_EQ(json2.getExtension("d").second.size(), static_cast<size_t>(8));
	}
	{
		asvJSON json;
		json.parse(std::string("{\"ts\":\"2024-01-15T10:30:45.123Z\"}"));
		auto mp = json.toMessagePack();
		asvJSON json2;
		json2.fromMessagePack(mp.data(), mp.size());
		ASSERT_EQ(json2.getDateTime("ts"), 1705314645);
		ASSERT_EQ(json2.getDateTimeMs("ts"), 123);
	}
}

TEST(testBSONArray) {
	auto makeBSON = [](const std::vector<std::pair<const char*, int64_t>>& keys) {
		auto addLE32 = [](std::vector<uint8_t>& v, uint32_t x) {
			v.push_back(static_cast<uint8_t>(x & 0xFF));
			v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
			v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
			v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
		};
		auto addLE64 = [](std::vector<uint8_t>& v, uint64_t x) {
			for (int i = 0; i < 8; i++) v.push_back(static_cast<uint8_t>((x >> (i * 8)) & 0xFF));
		};
		std::vector<uint8_t> inner;
		for (auto& kv : keys) {
			inner.push_back(0x12);
			const char* k = kv.first;
			while (*k) inner.push_back(static_cast<uint8_t>(*k++));
			inner.push_back(0);
			addLE64(inner, static_cast<uint64_t>(kv.second));
		}
		uint32_t innerLen = 4 + static_cast<uint32_t>(inner.size()) + 1;
		std::vector<uint8_t> outerBody;
		outerBody.push_back(0x04);
		outerBody.push_back('a'); outerBody.push_back('r'); outerBody.push_back('r');
		outerBody.push_back(0);
		addLE32(outerBody, innerLen);
		outerBody.insert(outerBody.end(), inner.begin(), inner.end());
		outerBody.push_back(0);
		uint32_t outerLen = 4 + static_cast<uint32_t>(outerBody.size()) + 1;
		std::vector<uint8_t> bson;
		addLE32(bson, outerLen);
		bson.insert(bson.end(), outerBody.begin(), outerBody.end());
		bson.push_back(0);
		return bson;
	};
	// Sequential keys "0","1","2" should convert to array
	{
		auto bson = makeBSON({{"0",10},{"1",20},{"2",30}});
		asvJSON json;
		bool ok = json.fromBSON(bson.data(), bson.size());
		ASSERT(ok);
		ASSERT(json.hasKey("arr"));
		ASSERT_EQ(json.getRoot()->get("arr")->type, asvJSONValue::ARRAY);
		ASSERT_EQ(json.getRoot()->get("arr")->size(), 3);
	}
	// Non-sequential keys "0","2","3" (skipping "1") should stay as object
	{
		auto bson = makeBSON({{"0",10},{"2",20},{"3",30}});
		asvJSON json;
		bool ok = json.fromBSON(bson.data(), bson.size());
		ASSERT(ok);
		ASSERT(json.hasKey("arr"));
		ASSERT_EQ(json.getRoot()->get("arr")->type, asvJSONValue::OBJECT);
		ASSERT_EQ(json.getRoot()->get("arr")->size(), 3);
	}
}

TEST(testBSONTimestampRoundtrip) {
	asvJSON json;
	asvJSONValue* v = json.putTimestamp("ts", 1234567890LL);
	ASSERT(v != nullptr);
	ASSERT_EQ(v->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(v->num, 1234567890LL);

	auto bson = json.toBSON();
	ASSERT(bson.size() > 0);

	asvJSON json2;
	bool ok = json2.fromBSON(bson.data(), bson.size());
	ASSERT(ok);
	auto* v2 = json2.getRoot()->get("ts");
	ASSERT(v2 != nullptr);
	ASSERT_EQ(v2->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(v2->num, 1234567890LL);
}

TEST(testBSONTimestampDecode) {
	// Hand-crafted BSON: {t: TIMESTAMP{t=1234, i=5678}}; increment must be ignored
	std::vector<uint8_t> bson = {
		0x10, 0x00, 0x00, 0x00, // total document size = 16
		0x11,                   // BSON type: timestamp
		't', 0x00,              // key "t"
		0xD2, 0x04, 0x00, 0x00, // t = 1234 (LE32)
		0x32, 0x16, 0x00, 0x00, // i = 5678 (LE32, must be ignored)
		0x00                    // terminator
	};
	asvJSON json;
	bool ok = json.fromBSON(bson.data(), bson.size());
	ASSERT(ok);
	auto* v = json.getRoot()->get("t");
	ASSERT(v != nullptr);
	ASSERT_EQ(v->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(v->num, 1234LL);
}

TEST(testBSONDateTimeRoundtrip) {
	asvJSON json;
	asvJSONValue* v = json.putDateTime("dt", 1705314645, 123);
	ASSERT(v != nullptr);
	ASSERT_EQ(v->type, asvJSONValue::DATETIME);
	ASSERT_EQ(v->timestamp, 1705314645LL);
	ASSERT_EQ(v->datetime_ms, 123);

	auto bson = json.toBSON();
	ASSERT(bson.size() > 0);

	asvJSON json2;
	bool ok = json2.fromBSON(bson.data(), bson.size());
	ASSERT(ok);
	auto* v2 = json2.getRoot()->get("dt");
	ASSERT(v2 != nullptr);
	ASSERT_EQ(v2->type, asvJSONValue::DATETIME);
	ASSERT_EQ(v2->timestamp, 1705314645LL);
	ASSERT_EQ(v2->datetime_ms, 123);
}

TEST(testBSONDateTimeNegativeRoundtrip) {
	// putDateTime(0, -500) normalizes to (-1, 500); encoded ms = -500
	asvJSON json;
	asvJSONValue* v = json.putDateTime("dt", 0, -500);
	ASSERT(v != nullptr);
	ASSERT_EQ(v->type, asvJSONValue::DATETIME);
	ASSERT_EQ(v->timestamp, -1LL);
	ASSERT_EQ(v->datetime_ms, 500);

	auto bson = json.toBSON();
	asvJSON json2;
	bool ok = json2.fromBSON(bson.data(), bson.size());
	ASSERT(ok);
	auto* v2 = json2.getRoot()->get("dt");
	ASSERT(v2 != nullptr);
	ASSERT_EQ(v2->type, asvJSONValue::DATETIME);
	ASSERT_EQ(v2->timestamp, -1LL);
	ASSERT_EQ(v2->datetime_ms, 500);
}

TEST(testBSONDateTimeOverflow) {
	// t*1000+m overflows int64: toBSON must throw instead of producing UB
	{
		asvJSON json;
		json.putDateTime("dt", std::numeric_limits<time_t>::max(), 0);
		bool threw = false;
		try {
			json.toBSON();
		} catch (const asvJSONError&) {
			threw = true;
		}
		ASSERT(threw);
	}
	{
		asvJSON json;
		json.putDateTime("dt", std::numeric_limits<time_t>::min(), 999);
		bool threw = false;
		try {
			json.toBSON();
		} catch (const asvJSONError&) {
			threw = true;
		}
		ASSERT(threw);
	}
}

TEST(testBSONStrictTerminator) {
	uint8_t data[] = {
		0x05, 0x00, 0x00, 0x00, // int32 length = 5
		0x01                    // non-zero document terminator
	};
	asvJSON json;
	bool ok = json.fromBSON(data, sizeof(data));
	if (ok) throw std::runtime_error("non-zero BSON terminator should fail");
	ASSERT(!json.getLastError().empty());
}

TEST(testBSONMalformedRegex) {
	{
		uint8_t data[] = {
			0x08, 0x00, 0x00, 0x00, // int32 length = 8
			0x0B, 'r', 0x00,        // type regex, key "r"
			0x78                    // pattern without NUL terminator
		};
		asvJSON json;
		bool ok = json.fromBSON(data, sizeof(data));
		if (ok) throw std::runtime_error("BSON regex missing pattern terminator should fail");
		ASSERT(!json.getLastError().empty());
	}
	{
		uint8_t data[] = {
			0x0A, 0x00, 0x00, 0x00, // int32 length = 10
			0x0B, 'r', 0x00,        // type regex, key "r"
			0x78, 0x00,             // pattern "x"
			0x69                    // options without NUL terminator
		};
		asvJSON json;
		bool ok = json.fromBSON(data, sizeof(data));
		if (ok) throw std::runtime_error("BSON regex missing options terminator should fail");
		ASSERT(!json.getLastError().empty());
	}
}

TEST(testBSONOversizedKey) {
	size_t keyLen = asvJSONValue::MAX_STRING_LEN + 1;
	size_t docSize = 4 + 1 + keyLen + 1 + 1; // header, type, key, key NUL, value byte
	std::vector<uint8_t> data(docSize, 'a');
	uint32_t sz = static_cast<uint32_t>(docSize);
	std::memcpy(data.data(), &sz, 4);
	data[4] = 0x08;
	data[4 + 1 + keyLen] = 0x00;
	data.back() = 0x00;

	asvJSON json;
	bool ok = json.fromBSON(data.data(), data.size());
	if (ok) throw std::runtime_error("BSON key longer than MAX_STRING_LEN should fail");
	ASSERT(!json.getLastError().empty());
}

TEST(testBSONExtensionRoundTrip) {
	// Bug: EXTENSION type had no encoder case, silently encoded as NULL.
	// Now encodes as BSON binary with ext_type as subtype byte.
	asvJSON json;
	json.parse("{}");
	const unsigned char b[] = {0x01, 0x02, 0x03};
	json.getRoot()->obj->emplace("ext", asvJSONValue::makeExtension(0x80, b, 3));

	auto bson = json.toBSON();
	ASSERT(bson.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromBSON(bson.data(), bson.size()));

	auto* v = json2.getRoot()->get("ext");
	ASSERT(v != nullptr);
	ASSERT_EQ(v->type, asvJSONValue::EXTENSION);
	ASSERT_EQ(static_cast<uint8_t>(v->ext_type), static_cast<uint8_t>(0x80));
	ASSERT_EQ(v->bin_data.size(), static_cast<size_t>(3));
	ASSERT_EQ(v->bin_data[0], 0x01);
	ASSERT_EQ(v->bin_data[1], 0x02);
	ASSERT_EQ(v->bin_data[2], 0x03);
}

TEST(testBSONEmptyArrayRoundTrip) {
	// Bug: empty array decoded as empty object (BSON 0x04 subdocument is
	// structurally identical to empty BSON document).
	asvJSON json;
	json.parse("{}");
	json.getRoot()->obj->emplace("arr", asvJSONValue::makeArray());

	auto bson = json.toBSON();
	ASSERT(bson.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromBSON(bson.data(), bson.size()));

	auto* v = json2.getRoot()->get("arr");
	ASSERT(v != nullptr);
	ASSERT_EQ(v->type, asvJSONValue::ARRAY);
	ASSERT_EQ(v->size(), static_cast<size_t>(0));
}

TEST(testBSONExtras) {
	// Bug: a key containing an embedded NUL byte was serialized as invalid
	// BSON (the key terminator truncated it on read).  It must throw.
	{
		asvJSON json;
		ASSERT(json.parse(std::string("{\"a\\u0000b\":1}")));
		try {
			auto bson = json.toBSON();
			ASSERT(false);
		} catch (const asvJSONError& e) {
			ASSERT(std::string(e.what()).find("NUL") != std::string::npos);
		}
	}
	// Same at a nested-object level.
	{
		asvJSON json;
		ASSERT(json.parse(std::string("{\"n\":{\"a\\u0000b\":1}}")));
		try {
			auto bson = json.toBSON();
			ASSERT(false);
		} catch (const asvJSONError&) {
		}
	}

	// Bug: timestamps outside the BSON uint32 seconds field silently wrapped.
	{
		asvJSON json;
		json.putTimestamp("t", -1);
		try {
			auto bson = json.toBSON();
			ASSERT(false);
		} catch (const asvJSONError&) {
		}
		json.putTimestamp("t", 4294967296LL);
		try {
			auto bson = json.toBSON();
			ASSERT(false);
		} catch (const asvJSONError&) {
		}
	}
	// Valid range still round-trips.
	{
		asvJSON json;
		json.putTimestamp("t", 4294967295LL);
		auto bson = json.toBSON();
		asvJSON json2;
		ASSERT(json2.fromBSON(bson.data(), bson.size()));
		auto* v = json2.getRoot()->get("t");
		ASSERT(v != nullptr);
		ASSERT_EQ(v->num, 4294967295LL);
	}

	// DateTime overflow guard: exact INT64 extremes must pass, one past must throw.
	{
		asvJSON json;
		// (max sec, 807 ms) == INT64_MAX ms exactly
		json.putDateTime("t", 9223372036854775LL, 807);
		auto bson = json.toBSON();
		ASSERT(bson.size() > 0);
		asvJSON json2;
		ASSERT(json2.fromBSON(bson.data(), bson.size()));
		auto* v = json2.getRoot()->get("t");
		ASSERT(v != nullptr);
		ASSERT_EQ(v->timestamp, 9223372036854775LL);
		ASSERT_EQ(v->datetime_ms, 807);

		// one ms past max throws
		json.putDateTime("t", 9223372036854775LL, 808);
		try {
			auto bson2 = json.toBSON();
			ASSERT(false);
		} catch (const asvJSONError&) {
		}

		// (-9223372036854775,-807) normalizes to (-9223372036854776,193) == INT64_MIN ms
		json.putDateTime("t", -9223372036854775LL, -807);
		bson = json.toBSON();
		ASSERT(bson.size() > 0);
		asvJSON json3;
		ASSERT(json3.fromBSON(bson.data(), bson.size()));
		v = json3.getRoot()->get("t");
		ASSERT(v != nullptr);
		ASSERT_EQ(v->timestamp, -9223372036854776LL);
		ASSERT_EQ(v->datetime_ms, 193);

		// one ms past min: (-9223372036854776, 191) -> INT64_MIN - 1 ms
		json.putDateTime("t", -9223372036854776LL, 191);
		try {
			auto bson3 = json.toBSON();
			ASSERT(false);
		} catch (const asvJSONError&) {
		}
	}
}

TEST(testISODateTime) {
	{
		asvJSON json;
		bool ok = json.parse(std::string("{\"dt\":\"2024-01-15T10:30:45.123Z\"}"));
		ASSERT(ok);
		time_t ts = json.getDateTime("dt");
		int ms = json.getDateTimeMs("dt");
		ASSERT_EQ(ts, 1705314645);
		ASSERT_EQ(ms, 123);
	}
	{
		asvJSON json;
		bool ok = json.parse(std::string("{\"dt\":\"2024-01-15T10:30:00+03:00\"}"));
		ASSERT(ok);
		time_t ts = json.getDateTime("dt");
		ASSERT_EQ(ts, 1705303800);
	}
	{
		asvJSON json;
		bool ok = json.parse(std::string("{\"dt\":\"2024-01-15T10:30:00-05:00\"}"));
		ASSERT(ok);
		time_t ts = json.getDateTime("dt");
		ASSERT_EQ(ts, 1705332600);
	}
	{
		asvJSON json;
		bool ok = json.parse(std::string("{\"dt\":\"2024-01-15T10:30:00+0300\"}"));
		ASSERT(ok);
		time_t ts = json.getDateTime("dt");
		ASSERT_EQ(ts, 1705303800);
	}
	{
		asvJSON json;
		bool ok = json.parse(std::string("{\"dt\":\"2024-01-16T00:00:00+14:00\"}"));
		ASSERT(ok);
		time_t ts = json.getDateTime("dt");
		ASSERT_EQ(ts, 1705312800);
	}
	{
		asvJSON json;
		bool ok = json.parse(std::string("{\"dt\":\"2024-01-15T10:30:00\"}"));
		ASSERT(ok);
		ASSERT_EQ(json.getRoot()->get("dt")->type, asvJSONValue::STRING);
	}
	{
		asvJSON json;
		bool ok = json.parse(std::string("{\"dt\":\"not-a-date\"}"));
		ASSERT(ok);
		ASSERT_EQ(json.getRoot()->get("dt")->type, asvJSONValue::STRING);
	}
}

TEST(testAPICoverage) {
	// putFloat32 / is_float32
	{
		asvJSON json;
		json.putFloat32("f", 3.14f);
		auto* v = json.getRoot()->get("f");
		ASSERT(v != nullptr);
		ASSERT_EQ(v->type, asvJSONValue::DOUBLE);
		ASSERT(v->is_float32);
		ASSERT_EQ(v->getDouble(), 3.14f);
	}

	// arrayAdd* methods
	{
		asvJSON json;
		json.arrayAddString("arr", "hello");
		json.arrayAddInt("arr", 42);
		json.arrayAddDouble("arr", 3.14);
		json.arrayAddBool("arr", true);
		json.arrayAddNull("arr");
		json.arrayAddDateTime("arr", 1705314645);
		auto* arr = json.getArray("arr");
		ASSERT(arr != nullptr);
		ASSERT_EQ(arr->size(), 6);
		ASSERT_EQ(arr->get(static_cast<size_t>(0))->type, asvJSONValue::STRING);
		ASSERT_EQ(arr->get(static_cast<size_t>(1))->type, asvJSONValue::INT);
		ASSERT_EQ(arr->get(static_cast<size_t>(2))->type, asvJSONValue::DOUBLE);
		ASSERT_EQ(arr->get(static_cast<size_t>(3))->type, asvJSONValue::BOOL_VAL);
		ASSERT_EQ(arr->get(static_cast<size_t>(4))->type, asvJSONValue::NULL_VAL);
		ASSERT_EQ(arr->get(static_cast<size_t>(5))->type, asvJSONValue::DATETIME);
	}

	// getDateTimeString, getDateTimeMs, optDateTime, optDateTimeTM
	{
		asvJSON json;
		json.parse(std::string("{\"dt\":\"2024-01-15T10:30:45.123Z\"}"));
		std::string dtStr = json.getDateTimeString("dt");
		ASSERT(!dtStr.empty());
		ASSERT_EQ(json.getDateTimeMs("dt"), 123);
		ASSERT_EQ(json.optDateTime("dt", 0), 1705314645);
		ASSERT_EQ(json.optDateTime("nonexistent", 999), 999);
		std::tm def = {};
		def.tm_year = 70;
		std::tm tm = json.optDateTimeTM("dt", def);
		ASSERT(tm.tm_year >= 124); // 2024-1970
		// Not found case
		std::tm tm2 = json.optDateTimeTM("nonexistent", def);
		ASSERT_EQ(tm2.tm_year, 70);
	}

	// isXxx type checks
	{
		asvJSON json;
		json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
		json.putTimestamp("ts", 1000);
		json.putRegex("rx", "^test$", "gi");
		uint8_t bin[] = {0x01, 0x02, 0x03};
		json.putBinary("bin", bin, 3);
		json.putDateTime("dt", 1705314645);
		ASSERT(json.isObjectId("oid"));
		ASSERT(json.isTimestamp("ts"));
		ASSERT(json.isRegex("rx"));
		ASSERT(json.isBinary("bin"));
		ASSERT(json.isDateTime("dt"));
		ASSERT(!json.isObjectId("nonexistent"));
		ASSERT(!json.isTimestamp("nonexistent"));
	}

	// getObjectId, getTimestamp, getRegex, getObject
	{
		asvJSON json;
		json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
		json.putTimestamp("ts", 1000);
		json.putRegex("rx", "^test$", "gi");
		ASSERT_EQ(json.getObjectId("oid"), "ABCDEF123456"); // ObjectId is exactly 12 bytes
		ASSERT_EQ(json.getTimestamp("ts"), 1000);
		auto rx = json.getRegex("rx");
		ASSERT_EQ(rx.first, "^test$");
		ASSERT_EQ(rx.second, "gi");
		std::string pat, opt;
		bool got = json.getRegex("rx", pat, opt);
		ASSERT(got);
		ASSERT_EQ(pat, "^test$");
		ASSERT_EQ(opt, "gi");
		// getObject creates root if absent
		asvJSON empty;
		asvJSONValue* obj = empty.getObject();
		ASSERT(obj != nullptr);
		ASSERT_EQ(obj->type, asvJSONValue::OBJECT);
	}

	// getNestedObjectId, getNestedTimestamp, getNestedRegex
	{
		asvJSON json2;
		const unsigned char oidBytes[12] = {0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0x00, 0x00};
		json2.setByPointer("/sub/oid", asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(oidBytes), 12)));
		json2.setByPointer("/sub/ts", asvJSONValue::makeTimestamp(1000));
		json2.setByPointer("/sub/rx", asvJSONValue::makeRegex("^test$", "gi"));
		ASSERT_EQ(json2.getNestedObjectId("sub.oid"), std::string(reinterpret_cast<const char*>(oidBytes), 12));
		ASSERT_EQ(json2.getNestedTimestamp("sub.ts"), 1000);
		auto nrx = json2.getNestedRegex("sub.rx");
		ASSERT_EQ(nrx.first, "^test$");
		ASSERT_EQ(nrx.second, "gi");
	}

	// isExtension / getExtension
	{
		asvJSON json;
		uint8_t extData[] = {0xDE, 0xAD, 0xBE, 0xEF};
		json.putExtension("ext", 7, extData, 4);
		ASSERT(json.isExtension("ext"));
		auto ext = json.getExtension("ext");
		ASSERT_EQ(ext.first, 7);
		ASSERT_EQ(ext.second.size(), 4);
		ASSERT_EQ(ext.second[0], 0xDE);
	}

	// fromMessagePack(const std::string&), fromBSON(const std::string&)
	{
		asvJSON src;
		src.parse(std::string("{\"a\":1}"));
		auto mp = src.toMessagePack();
		std::string mpStr(reinterpret_cast<const char*>(mp.data()), mp.size());
		asvJSON json1;
		bool ok = json1.fromMessagePack(mpStr);
		ASSERT(ok);
		ASSERT_EQ(json1.getInt("a"), 1);

		auto bson = src.toBSON();
		std::string bsonStr(reinterpret_cast<const char*>(bson.data()), bson.size());
		asvJSON json3;
		ok = json3.fromBSON(bsonStr);
		ASSERT(ok);
		ASSERT_EQ(json3.getInt("a"), 1);
	}

	// messagePackFromString, stringFromMessagePack
	{
		auto mp = asvJSON::messagePackFromString(std::string("{\"b\":2}"));
		ASSERT(!mp.empty());
		asvJSON j;
		j.fromMessagePack(mp.data(), mp.size());
		ASSERT_EQ(j.getInt("b"), 2);

		std::string jsonStr = asvJSON::stringFromMessagePack(mp.data(), mp.size());
		ASSERT(!jsonStr.empty());
		ASSERT(jsonStr.find("\"b\":2") != std::string::npos);
	}

	// isValidUTF8
	{
		uint8_t valid[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
		uint8_t invalid[] = {0xFF, 0xFE, 0x00}; // invalid UTF-8
		ASSERT(isValidUTF8(valid, 5));
		ASSERT(!isValidUTF8(invalid, 3));
	}
}

TEST(testParseEmptyString) {
	asvJSON json;
	bool ok = json.parse(std::string("\"\""));
	ASSERT(ok);
	ASSERT_EQ(json.getRoot()->type, asvJSONValue::STRING);
	ASSERT_EQ(json.getRoot()->str_data.size(), 0);
}

TEST(testLoneSurrogateRejected) {
	{
		asvJSON json;
		ASSERT(!json.parse(std::string("\"\\uD800\"")));
	}
	{
		asvJSON json;
		ASSERT(!json.parse(std::string("\"\\uDC00\"")));
	}
	{
		asvJSON json;
		ASSERT(!json.parse(std::string("\"\\uD800x\"")));
	}
	{
		asvJSON json;
		// Valid surrogate pair should still work
		ASSERT(json.parse(std::string("\"\\uD83D\\uDE00\"")));
	}
}

TEST(testMessagePackObjectIdRegexTimestamp) {
	asvJSON json;
	json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
	json.putRegex("re", "pattern", "ims");
	json.putTimestamp("ts", 1234567890);

	auto mp = json.toMessagePack();
	ASSERT(mp.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromMessagePack(mp.data(), mp.size()));
	// MsgPack ext format preserves OBJECTID, REGEX, TIMESTAMP types
	auto* oidV = json2.getRoot()->get("oid");
	ASSERT(oidV != nullptr);
	ASSERT_EQ(oidV->type, asvJSONValue::OBJECTID);
	ASSERT_EQ(oidV->str_data.size(), 12);
	// REGEX preserved via ext type 2
	auto* reV = json2.getRoot()->get("re");
	ASSERT(reV != nullptr);
	ASSERT_EQ(reV->type, asvJSONValue::REGEX);
	// TIMESTAMP preserved via ext type 3
	auto* tsV = json2.getRoot()->get("ts");
	ASSERT(tsV != nullptr);
	ASSERT_EQ(tsV->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(tsV->num, 1234567890);
}

TEST(testMsgPackRegexFidelity) {
	// REGEX is stored as "pattern\0options" (NUL-separated, see makeRegex) and
	// emitted as an ext of whichever size fits (fixext1/2/4/8/16 or ext8/16/32).
	// Every codec branch must decode it back to a REGEX with intact pattern
	// (a '|' inside the pattern must NOT be treated as a separator) and options.
	for (int L = 1; L <= 20; L++) {
		std::string p(L, 'a');
		if (L == 5) p = "a|bcd";
		asvJSON json;
		json.putRegex("re", p.c_str(), "im");
		auto mp = json.toMessagePack();
		asvJSON json2;
		ASSERT(json2.fromMessagePack(mp.data(), mp.size()));
		auto* v = json2.getRoot()->get("re");
		ASSERT(v != nullptr);
		ASSERT_EQ(v->type, asvJSONValue::REGEX);
		std::string pattern, opts;
		ASSERT(json2.getRegex("re", pattern, opts));
		if (pattern != p) throw std::runtime_error("MsgPack regex pattern corrupted (len " + std::to_string(L) + ")");
		if (opts != "im") throw std::runtime_error("MsgPack regex options corrupted (len " + std::to_string(L) + ")");
	}
	// Empty options round-trip across the same size branches.
	for (int L = 1; L <= 20; L++) {
		std::string p(L, 'b');
		asvJSON json;
		json.putRegex("re", p.c_str(), "");
		auto mp = json.toMessagePack();
		asvJSON json2;
		ASSERT(json2.fromMessagePack(mp.data(), mp.size()));
		std::string pattern, opts;
		ASSERT(json2.getRegex("re", pattern, opts));
		if (pattern != p) throw std::runtime_error("MsgPack regex (no opts) pattern corrupted (len " + std::to_string(L) + ")");
		if (!opts.empty()) throw std::runtime_error("MsgPack regex (no opts) options not empty (len " + std::to_string(L) + ")");
	}
}

TEST(testCBOR) {
	asvJSON json;
	json.putString("str", "hello");
	json.putInt("num", 123);
	json.putBool("flag", true);
	json.putNull("nill");
	json.putDouble("dbl", 1.5);

	auto cbor = json.toCBOR();
	ASSERT(cbor.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	ASSERT_EQ(json2.getString("str"), "hello");
	ASSERT_EQ(json2.getInt("num"), 123);
	ASSERT_EQ(json2.getBool("flag"), true);
	ASSERT(json2.isNull("nill"));
	ASSERT_EQ(json2.getDouble("dbl"), 1.5);
}

TEST(testCBORArray) {
	asvJSON json;
	json.parse(std::string("[1, \"two\", 3.0, true, null]"));
	auto cbor = json.toCBOR();
	ASSERT(cbor.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	auto* arr = json2.getRoot();
	ASSERT(arr != nullptr);
	ASSERT_EQ(arr->type, asvJSONValue::ARRAY);
	ASSERT_EQ(arr->size(), static_cast<size_t>(5));

	auto* v = arr->get(0);
	ASSERT(v != nullptr && v->type == asvJSONValue::INT && v->num == 1);
	v = arr->get(1);
	ASSERT(v != nullptr && v->type == asvJSONValue::STRING && v->str_data == "two");
	v = arr->get(2);
	ASSERT(v != nullptr && v->type == asvJSONValue::DOUBLE && v->dbl == 3.0);
	v = arr->get(3);
	ASSERT(v != nullptr && v->type == asvJSONValue::BOOL_VAL && v->flag == true);
	v = arr->get(4);
	ASSERT(v != nullptr && v->type == asvJSONValue::NULL_VAL);
}

TEST(testCBORIntegers) {
	asvJSON json;
	json.putInt("zero", 0);
	json.putInt("pos_small", 42);
	json.putInt("pos_byte", 200);
	json.putInt("pos_word", 70000);
	json.putInt("neg_small", -5);
	json.putInt("neg_byte", -200);
	json.putInt("neg_word", -70000);
	json.putInt("max64", 9223372036854775807LL);
	json.putInt("min64", -9223372036854775807LL - 1);

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	ASSERT_EQ(json2.getInt("zero"), 0);
	ASSERT_EQ(json2.getInt("pos_small"), 42);
	ASSERT_EQ(json2.getInt("pos_byte"), 200);
	ASSERT_EQ(json2.getInt("pos_word"), 70000);
	ASSERT_EQ(json2.getInt("neg_small"), -5);
	ASSERT_EQ(json2.getInt("neg_byte"), -200);
	ASSERT_EQ(json2.getInt("neg_word"), -70000);
	ASSERT_EQ(json2.getInt("max64"), 9223372036854775807LL);
	ASSERT_EQ(json2.getInt("min64"), -9223372036854775807LL - 1);
}

TEST(testCBORDouble) {
	asvJSON json;
	json.putDouble("pi", 3.141592653589793);
	json.putFloat32("f32", 1.5f);

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	ASSERT_EQ(json2.getDouble("pi"), 3.141592653589793);
	ASSERT_EQ(json2.getDouble("f32"), 1.5f);
}

TEST(testCBORBinary) {
	asvJSON json;
	uint8_t bin[] = {0x00, 0x01, 0x02, 0xFF, 0xFE};
	json.putBinary("bin", bin, 5);

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	auto v = json2.getBinary("bin");
	ASSERT_EQ(v.size(), static_cast<size_t>(5));
	ASSERT_EQ(v[0], 0x00);
	ASSERT_EQ(v[4], 0xFE);
}

TEST(testCBORDateTime) {
	{
		asvJSON json;
		json.putDateTime("dt", 1705314645, 123);

		auto cbor = json.toCBOR();
		asvJSON json2;
		ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

		ASSERT_EQ(json2.getDateTime("dt"), 1705314645);
		ASSERT_EQ(json2.getDateTimeMs("dt"), 123);
	}
	{
		asvJSON json;
		json.parse(std::string("{\"ts\":\"2024-01-15T10:30:45.123Z\"}"));
		auto cbor = json.toCBOR();
		asvJSON json2;
		ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));
		ASSERT_EQ(json2.getDateTime("ts"), 1705314645);
		ASSERT_EQ(json2.getDateTimeMs("ts"), 123);
	}
}

TEST(testCBORRegex) {
	asvJSON json;
	json.putRegex("re", "pattern", "ims");

	auto cbor = json.toCBOR();
	ASSERT(cbor.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	auto* v = json2.getRoot()->get("re");
	ASSERT(v != nullptr);
	ASSERT_EQ(v->type, asvJSONValue::REGEX);

	std::string pattern, options;
	ASSERT(json2.getRegex("re", pattern, options));
	ASSERT_EQ(pattern, std::string("pattern"));
	ASSERT_EQ(options, std::string("ims"));

	// Pattern containing the old, wrong '|' separator must keep its options intact.
	asvJSON json3;
	json3.putRegex("rx", "a|b", "g");
	auto cbor3 = json3.toCBOR();
	asvJSON json4;
	ASSERT(json4.fromCBOR(cbor3.data(), cbor3.size()));
	std::string p3, o3;
	ASSERT(json4.getRegex("rx", p3, o3));
	ASSERT_EQ(p3, std::string("a|b"));
	ASSERT_EQ(o3, std::string("g"));

	// Empty options round-trip.
	asvJSON json5;
	json5.putRegex("ro", "xyz", "");
	auto cbor5 = json5.toCBOR();
	asvJSON json6;
	ASSERT(json6.fromCBOR(cbor5.data(), cbor5.size()));
	std::string p5, o5;
	ASSERT(json6.getRegex("ro", p5, o5));
	ASSERT_EQ(p5, std::string("xyz"));
	ASSERT_EQ(o5, std::string(""));
}

TEST(testCBORExtension) {
	uint8_t extData[] = {0xDE, 0xAD, 0xBE, 0xEF};
	asvJSON json;
	json.putExtension("ext", 7, extData, 4);

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	ASSERT(json2.isExtension("ext"));
	auto ext = json2.getExtension("ext");
	ASSERT_EQ(ext.first, 7);
	ASSERT_EQ(ext.second.size(), static_cast<size_t>(4));
	ASSERT_EQ(ext.second[0], 0xDE);
}

TEST(testCBORObjectIdTimestamp) {
	asvJSON json;
	json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
	json.putTimestamp("ts", 1234567890);

	auto cbor = json.toCBOR();
	ASSERT(cbor.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	// ObjectId round-trips correctly via CBOR tag 4 (MongoDB convention)
	auto* oidV = json2.getRoot()->get("oid");
	ASSERT(oidV != nullptr);
	ASSERT_EQ(oidV->type, asvJSONValue::OBJECTID);
	ASSERT_EQ(oidV->str_data, std::string("ABCDEF123456", 12));

	// Timestamp round-trips as TIMESTAMP (tag 1 + integer)
	auto* tsV = json2.getRoot()->get("ts");
	ASSERT(tsV != nullptr);
	ASSERT_EQ(tsV->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(tsV->num, 1234567890);
}

TEST(testCBORTimestampRoundTrip) {
	// Bug: tag 1 + INT decoded as DATETIME instead of TIMESTAMP
	// Now TIMESTAMP encodes as tag 1 + int and round-trips correctly
	asvJSON json;
	json.putTimestamp("ts", 1700000000);
	json.putTimestamp("ts_neg", -1000);
	json.putTimestamp("ts_zero", 0);

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	auto* ts1 = json2.getRoot()->get("ts");
	ASSERT(ts1 != nullptr);
	ASSERT_EQ(ts1->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(ts1->num, 1700000000);

	auto* ts2 = json2.getRoot()->get("ts_neg");
	ASSERT(ts2 != nullptr);
	ASSERT_EQ(ts2->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(ts2->num, -1000);

	auto* ts3 = json2.getRoot()->get("ts_zero");
	ASSERT(ts3 != nullptr);
	ASSERT_EQ(ts3->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(ts3->num, 0);
}

TEST(testCBORObjectIdRoundTrip) {
	// Bug: ObjectId encoded as raw byte string, decoded as BINARY
	// Now ObjectId encodes as CBOR tag 4 and round-trips correctly
	asvJSON json;
	json.putObjectId("oid", std::string_view("ABCDEF123456", 12));

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	auto* oid = json2.getRoot()->get("oid");
	ASSERT(oid != nullptr);
	ASSERT_EQ(oid->type, asvJSONValue::OBJECTID);
	ASSERT_EQ(oid->str_data, std::string("ABCDEF123456", 12));
}

TEST(testCBORDatetimeVsTimestamp) {
	// DATETIME (tag 1 + float) and TIMESTAMP (tag 1 + int) must round-trip to distinct types
	asvJSON json;
	json.putDateTime("dt", 1700000000, 500);
	json.putTimestamp("ts", 1700000000);

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	auto* dt = json2.getRoot()->get("dt");
	ASSERT(dt != nullptr);
	ASSERT_EQ(dt->type, asvJSONValue::DATETIME);
	ASSERT_EQ(dt->timestamp, 1700000000);
	ASSERT_EQ(dt->datetime_ms, 500);

	auto* ts = json2.getRoot()->get("ts");
	ASSERT(ts != nullptr);
	ASSERT_EQ(ts->type, asvJSONValue::TIMESTAMP);
	ASSERT_EQ(ts->num, 1700000000);
}

TEST(testCBORNestedObject) {
	asvJSON json;
	json.parse(std::string("{\"a\":{\"b\":{\"c\":[1,2,3]}}}"));

	auto cbor = json.toCBOR();
	asvJSON json2;
	ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));

	// Verify the root is an object
	auto* root = json2.getRoot();
	ASSERT(root != nullptr);
	ASSERT_EQ(root->type, asvJSONValue::OBJECT);

	// Navigate manually
	auto* a = root->get("a");
	ASSERT(a != nullptr);
	ASSERT_EQ(a->type, asvJSONValue::OBJECT);

	auto* b = a->get("b");
	ASSERT(b != nullptr);
	ASSERT_EQ(b->type, asvJSONValue::OBJECT);

	auto* c = b->get("c");
	ASSERT(c != nullptr);
	ASSERT_EQ(c->type, asvJSONValue::ARRAY);
	ASSERT_EQ(c->size(), static_cast<size_t>(3));
	ASSERT_EQ(c->get(0)->num, 1);
	ASSERT_EQ(c->get(1)->num, 2);
	ASSERT_EQ(c->get(2)->num, 3);
}

TEST(testCBORCorrupted) {
	asvJSON json;
	uint8_t bad[] = {0xFF};
	ASSERT(!json.fromCBOR(bad, 1));
}

TEST(testCBOREmpty) {
	asvJSON json;
	ASSERT(!json.fromCBOR(nullptr, 0));
}

TEST(testCBORFromString) {
	auto cbor = asvJSON::cborFromString(std::string("{\"x\":1}"));
	ASSERT(cbor.size() > 0);

	asvJSON json;
	ASSERT(json.fromCBOR(cbor.data(), cbor.size()));
	ASSERT_EQ(json.getInt("x"), 1);
}

TEST(testCBORIndefiniteArray) {
	// Build CBOR with indefinite-length array (0x9F ... 0xFF)
	std::vector<uint8_t> data;
	data.push_back(0x9F); // array with indefinite length
	data.push_back(0x01); // 1 (unsigned)
	data.push_back(0x02); // 2
	data.push_back(0x03); // 3
	data.push_back(0xFF); // break

	asvJSON json;
	ASSERT(json.fromCBOR(data.data(), data.size()));
	auto* arr = json.getRoot();
	ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
	ASSERT_EQ(arr->size(), static_cast<size_t>(3));
	ASSERT_EQ(arr->get(0)->num, 1);
	ASSERT_EQ(arr->get(1)->num, 2);
	ASSERT_EQ(arr->get(2)->num, 3);
}

TEST(testCBORIndefiniteMap) {
	// Build CBOR with indefinite-length map (0xBF ... 0xFF)
	std::vector<uint8_t> data;
	data.push_back(0xBF); // map with indefinite length
	data.push_back(0x61); data.push_back('a'); // text(1) "a"
	data.push_back(0x01); // 1
	data.push_back(0x61); data.push_back('b'); // text(1) "b"
	data.push_back(0x02); // 2
	data.push_back(0xFF); // break

	asvJSON json;
	ASSERT(json.fromCBOR(data.data(), data.size()));
	ASSERT_EQ(json.getInt("a"), 1);
	ASSERT_EQ(json.getInt("b"), 2);
}

TEST(testCBORFloat16) {
	// Build CBOR with half-precision float: 0xF9 0x3C 0x00 = 1.0
	std::vector<uint8_t> data;
	data.push_back(0xF9);
	data.push_back(0x3C);
	data.push_back(0x00);

	asvJSON json;
	ASSERT(json.fromCBOR(data.data(), data.size()));
	auto* v = json.getRoot();
	ASSERT(v != nullptr && v->type == asvJSONValue::DOUBLE);
	ASSERT_EQ(v->dbl, 1.0);
}

TEST(testCBORUInt64Overflow) {
	// Unsigned int with 64-bit value above int64 max (0xFFFFFFFFFFFFFFFF)
	// must be rejected before makeInt() (which performs no range check).
	std::vector<uint8_t> data = { 0x1B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	asvJSON json;
	bool ok = json.fromCBOR(data.data(), data.size());
	if (ok) throw std::runtime_error("fromCBOR accepted unsigned int above int64 max");
	ASSERT(!json.getLastError().empty());
}

TEST(testCBORNegEpochDateTime) {
	// Tag 1 with a negative float64 epoch (pre-1970) must split into
	// second + millisecond the same way a date parser would (floor towards
	// -inf, ms in [0,1000)). Truncation-toward-zero previously produced
	// ts=0, ms=0 for -0.5s instead of ts=-1, ms=500.
	{
		// -0.5 s == 1969-12-31T23:59:59.500Z
		std::vector<uint8_t> data = { 0xC1, 0xFB, 0xBF, 0xE0, 0, 0, 0, 0, 0, 0 };
		asvJSON json;
		ASSERT(json.fromCBOR(data.data(), data.size()));
		auto* v = json.getRoot();
		ASSERT(v != nullptr && v->type == asvJSONValue::DATETIME);
		ASSERT_EQ(v->timestamp, static_cast<int64_t>(-1));
		ASSERT_EQ(v->datetime_ms, 500);
	}
	{
		// -1.5 s == 1969-12-31T23:59:58.500Z
		std::vector<uint8_t> data = { 0xC1, 0xFB, 0xBF, 0xF8, 0, 0, 0, 0, 0, 0 };
		asvJSON json;
		ASSERT(json.fromCBOR(data.data(), data.size()));
		auto* v = json.getRoot();
		ASSERT(v != nullptr && v->type == asvJSONValue::DATETIME);
		ASSERT_EQ(v->timestamp, static_cast<int64_t>(-2));
		ASSERT_EQ(v->datetime_ms, 500);
	}
	{
		// Positive epoch keeps working: 0.5 s == 1970-01-01T00:00:00.500Z
		std::vector<uint8_t> data = { 0xC1, 0xFB, 0x3F, 0xE0, 0, 0, 0, 0, 0, 0 };
		asvJSON json;
		ASSERT(json.fromCBOR(data.data(), data.size()));
		auto* v = json.getRoot();
		ASSERT(v != nullptr && v->type == asvJSONValue::DATETIME);
		ASSERT_EQ(v->timestamp, static_cast<int64_t>(0));
		ASSERT_EQ(v->datetime_ms, 500);
	}
}

TEST(testCBORNegInt64Overflow) {
	// Negative int with magnitude 0xFFFFFFFFFFFFFFFF would map to INT64_MIN-1,
	// out of range, and must be rejected.
	std::vector<uint8_t> data = { 0x3B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	asvJSON json;
	bool ok = json.fromCBOR(data.data(), data.size());
	if (ok) throw std::runtime_error("fromCBOR accepted negative int below int64 min");
	ASSERT(!json.getLastError().empty());
}

TEST(testCBORIndefiniteArrayTruncated) {
	// Indefinite-length array (0x9F) with no 0xFF terminator must be rejected.
	std::vector<uint8_t> data = { 0x9F, 0x01, 0x02 };
	asvJSON json;
	bool ok = json.fromCBOR(data.data(), data.size());
	if (ok) throw std::runtime_error("fromCBOR accepted indefinite array missing terminator");
	ASSERT(!json.getLastError().empty());
}

TEST(testCBORIndefiniteMapTruncated) {
	// Indefinite-length map (0xBF) with no 0xFF terminator must be rejected.
	std::vector<uint8_t> data = { 0xBF, 0x61, 0x61, 0x01 };
	asvJSON json;
	bool ok = json.fromCBOR(data.data(), data.size());
	if (ok) throw std::runtime_error("fromCBOR accepted indefinite map missing terminator");
	ASSERT(!json.getLastError().empty());
}

TEST(testCBORIndefiniteStringTruncated) {
	// Indefinite-length text string (0x7F) with no 0xFF terminator must be rejected.
	std::vector<uint8_t> data = { 0x7F, 0x61, 0x61 };
	asvJSON json;
	bool ok = json.fromCBOR(data.data(), data.size());
	if (ok) throw std::runtime_error("fromCBOR accepted indefinite string missing terminator");
	ASSERT(!json.getLastError().empty());
}

TEST(testCBORExtras) {
	// 1. JSON null must encode as CBOR null (0xF6), NOT undefined (0xF7).
	{
		asvJSON json;
		json.putNull("k");
		auto cbor = json.toCBOR();
		ASSERT_EQ(cbor.size(), static_cast<size_t>(4));
		ASSERT_EQ(cbor[3], 0xF6);
	}
	{
		asvJSON json;
		json.parse(std::string("[null]"));
		auto cbor = json.toCBOR();
		ASSERT_EQ(cbor.size(), static_cast<size_t>(2));
		ASSERT_EQ(cbor[1], 0xF6);
	}
	// Old 0xF7 (undefined) input still decodes as null (lenient).
	{
		uint8_t u[] = { 0xF7 };
		asvJSON json;
		ASSERT(json.fromCBOR(u, 1));
		ASSERT(json.getRoot() != nullptr && json.getRoot()->type == asvJSONValue::NULL_VAL);
	}

	// 2. Extension with an empty payload must not degrade to null.
	{
		asvJSON json;
		json.putExtension("e", 7, nullptr, 0);
		auto cbor = json.toCBOR();
		asvJSON json2;
		ASSERT(json2.fromCBOR(cbor.data(), cbor.size()));
		ASSERT(json2.isExtension("e"));
		std::vector<uint8_t> payload = json2.getExtension("e").second;
		ASSERT(payload.empty());
	}

	// 3. Indefinite text/byte strings must reject chunks of the wrong major type.
	{
		uint8_t data[] = { 0x7F, 0x42, 0x01, 0x02, 0xFF }; // indefinite text w/ bytes chunk
		asvJSON json;
		bool ok = json.fromCBOR(data, 5);
		if (ok) throw std::runtime_error("fromCBOR accepted binary chunk in indefinite text string");
		ASSERT(!json.getLastError().empty());
	}
	{
		uint8_t data[] = { 0x5F, 0x61, 0x61, 0xFF }; // indefinite bytes w/ text chunk
		asvJSON json;
		bool ok = json.fromCBOR(data, 4);
		if (ok) throw std::runtime_error("fromCBOR accepted text chunk in indefinite byte string");
		ASSERT(!json.getLastError().empty());
	}

	// 4. Reserved extended simple values 24-31 are syntax errors (RFC 8949 s3.3).
	{
		uint8_t data[] = { 0xF8, 0x1F };
		asvJSON json;
		bool ok = json.fromCBOR(data, 2);
		if (ok) throw std::runtime_error("fromCBOR accepted reserved simple value 31");
		ASSERT(!json.getLastError().empty());
	}
	// Valid extended simples still decode.
	{
		uint8_t f[] = { 0xF8, 0x14 };
		asvJSON json;
		ASSERT(json.fromCBOR(f, 2));
		ASSERT(json.getRoot() != nullptr && json.getRoot()->type == asvJSONValue::BOOL_VAL && !json.getRoot()->flag);
	}
}

TEST(testObjectKeyEscapes) {
	// Object keys with escape sequences must be decoded per RFC 7159
	{
		asvJSON json;
		// key with \n (newline) and \u0020 (space)
		ASSERT(json.parse(std::string("{\"foo\\nbar\": 1, \"key\\u0020space\": 2}")));
		auto* v1 = json.get("foo\nbar");
		ASSERT(v1 != nullptr);
		ASSERT_EQ(v1->type, asvJSONValue::INT);
		ASSERT_EQ(v1->num, 1);
		auto* v2 = json.get("key space");
		ASSERT(v2 != nullptr);
		ASSERT_EQ(v2->type, asvJSONValue::INT);
		ASSERT_EQ(v2->num, 2);
		// Roundtrip: serialize and parse again
		std::string s = json.serialize();
		asvJSON json2;
		ASSERT(json2.parse(s));
		ASSERT(json2.hasKey("foo\nbar"));
		ASSERT(json2.hasKey("key space"));
	}
	// Raw control characters in object keys are rejected (fast path detects them)
	{
		asvJSON json;
		ASSERT(!json.parse(std::string("{\"foo" "\x01" "bar\": 1}")));
	}
	// Object key too long
	{
		asvJSON json;
		std::string hugeKey(11 * 1024 * 1024, 'x');
		std::string input = "{\"" + hugeKey + "\": 1}";
		ASSERT(!json.parse(input));
	}
}

TEST(testToXML) {
	// Empty root
	{
		asvJSON json;
		json.parse(std::string("{}"));
		std::string xml = json.toXML();
		ASSERT(!xml.empty());
		ASSERT(xml.find("<?xml") != std::string::npos);
		ASSERT(xml.find("encoding=\"UTF-8\"") != std::string::npos);
	}
	// Scalar root
	{
		asvJSON json;
		json.parse(std::string("\"hello\""));
		std::string xml = json.toXML();
		ASSERT(xml.find("hello") != std::string::npos);
		ASSERT(xml.find("<root>") != std::string::npos);
		ASSERT(xml.find("</root>") != std::string::npos);
	}
	// All types
	{
		asvJSON json;
		json.putNull("n");
		json.putBool("b", true);
		json.putInt("i", -42);
		json.putDouble("d", 3.14);
		json.putString("s", "a & b < c");
		json.putDateTime("dt", 1705314645);
		uint8_t binData[] = {0xDE, 0xAD};
		json.putBinary("bin", binData, 2);
		json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
		json.putTimestamp("ts", 98765);
		json.putRegex("rx", "^test$", "gi");
		uint8_t extData[] = {0x01, 0x02};
		json.putExtension("ext", 42, extData, 2);

		std::string xml = json.toXML();
		ASSERT(xml.find("xsi:nil=\"true\"") != std::string::npos);
		ASSERT(xml.find("true") != std::string::npos);
		ASSERT(xml.find("-42") != std::string::npos);
		ASSERT(xml.find("a &amp; b &lt; c") != std::string::npos);
		ASSERT(xml.find("type=\"datetime\"") != std::string::npos);
		ASSERT(xml.find("type=\"binary\"") != std::string::npos);
		ASSERT(xml.find("type=\"objectid\"") != std::string::npos);
		ASSERT(xml.find("type=\"timestamp\"") != std::string::npos);
		ASSERT(xml.find("type=\"regex\"") != std::string::npos);
		ASSERT(xml.find("type=\"extension\"") != std::string::npos);
	}
	// Array
	{
		asvJSON json;
		json.parse(std::string("[1, 2, 3]"));
		std::string xml = json.toXML();
		ASSERT(xml.find("<item>1</item>") != std::string::npos);
		ASSERT(xml.find("<item>2</item>") != std::string::npos);
		ASSERT(xml.find("<item>3</item>") != std::string::npos);
	}
	// asvJSONValue::toXML (direct)
	{
		auto v = asvJSONValue::makeString("test", 4);
		std::string out;
		v->toXML(out);
		ASSERT(!out.empty());
		ASSERT(out.find("test") != std::string::npos);
		ASSERT(out.find("<root>") != std::string::npos);
	}
}

TEST(testFromXML) {
	// Empty self-closing root
	{
		asvJSON json;
		ASSERT(json.fromXML("<root/>"));
		ASSERT(json.getString("root") == "");
	}
	// Simple string value
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>hello</root>"));
		ASSERT(json.getString("root") == "hello");
	}
	// Integer detection
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>42</root>"));
		ASSERT(json.getInt("root") == 42);
	}
	// Double detection
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>3.14</root>"));
		ASSERT(json.getDouble("root") > 3.13 && json.getDouble("root") < 3.15);
	}
	// Bool detection
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>true</root>"));
		ASSERT(json.getBool("root") == true);
	}
	// Null detection
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>null</root>"));
		ASSERT(json.isNull("root"));
	}
	// Nested object
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><a>1</a><b>2</b></root>"));
		ASSERT(json.getInt("root.a") == 1);
		ASSERT(json.getInt("root.b") == 2);
	}
	// Array detection (consecutive same-named siblings)
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><item>1</item><item>2</item><item>3</item></root>"));
		auto* root = json.getRoot();
		auto* inner = root->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->type == asvJSONValue::ARRAY);
		ASSERT(inner->arr->size() == 3);
		ASSERT(inner->get(static_cast<size_t>(0))->getInt() == 1);
		ASSERT(inner->get(static_cast<size_t>(1))->getInt() == 2);
		ASSERT(inner->get(static_cast<size_t>(2))->getInt() == 3);
	}
	// Attributes as @attr
	{
		asvJSON json;
		ASSERT(json.fromXML("<root id=\"123\" name=\"test\"><child>val</child></root>"));
		ASSERT(json.getString("root.@id") == "123");
		ASSERT(json.getString("root.@name") == "test");
		ASSERT(json.getString("root.child") == "val");
	}
	// Self-closing with attribute
	{
		asvJSON json;
		ASSERT(json.fromXML("<root enabled=\"true\"/>"));
		ASSERT(json.getString("root.@enabled") == "true");
	}
	// XML declaration skipped
	{
		asvJSON json;
		ASSERT(json.fromXML("<?xml version=\"1.0\"?><root>data</root>"));
		ASSERT(json.getString("root") == "data");
	}
	// Comment skipped
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><!-- comment -->data</root>"));
		ASSERT(json.getString("root") == "data");
	}
	// Entity decoding
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>a &amp; b &lt; c</root>"));
		ASSERT(json.getString("root") == "a & b < c");
	}
	// Deeply nested
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><a><b><c>42</c></b></a></root>"));
		ASSERT(json.getInt("root.a.b.c") == 42);
	}
	// Mixed children and text (text becomes #text)
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>hello<child>world</child></root>"));
		ASSERT(json.getString("root.#text") == "hello");
		ASSERT(json.getString("root.child") == "world");
	}
	// Special type: datetime
	{
		asvJSON json;
		ASSERT(json.fromXML("<root type=\"datetime\">2024-01-15T10:30:45Z</root>"));
		auto* dt = json.getRoot()->get("root");
		ASSERT(dt != nullptr);
		ASSERT(dt->type == asvJSONValue::DATETIME);
	}
	// Special type: binary
	{
		asvJSON json;
		ASSERT(json.fromXML("<root type=\"binary\">3q0=</root>"));
		auto* bin = json.getRoot()->get("root");
		ASSERT(bin != nullptr);
		ASSERT(bin->type == asvJSONValue::BINARY);
	}
	// Special type: objectid
	{
		asvJSON json;
		ASSERT(json.fromXML("<root type=\"objectid\">ABCDEF1234567890abcdef12</root>"));
		auto* oid = json.getRoot()->get("root");
		ASSERT(oid != nullptr);
		ASSERT(oid->type == asvJSONValue::OBJECTID);
	}
	// Special type: regex
	{
		asvJSON json;
		ASSERT(json.fromXML("<root type=\"regex\">^test$|gi</root>"));
		auto* rx = json.getRoot()->get("root");
		ASSERT(rx != nullptr);
		ASSERT(rx->type == asvJSONValue::REGEX);
	}
	// Special type: timestamp
	{
		asvJSON json;
		ASSERT(json.fromXML("<root type=\"timestamp\">98765</root>"));
		auto* ts = json.getRoot()->get("root");
		ASSERT(ts != nullptr);
		ASSERT(ts->type == asvJSONValue::TIMESTAMP);
	}
	// Error: empty input
	{
		asvJSON json;
		ASSERT(!json.fromXML(""));
	}
	// Error: no XML
	{
		asvJSON json;
		ASSERT(!json.fromXML("not xml"));
	}
	// Nesting depth limit
	{
		asvJSON json;
		std::string deep;
		for (int i = 0; i < 60; i++) deep += "<a>";
		deep += "x";
		for (int i = 0; i < 60; i++) deep += "</a>";
		ASSERT(!json.fromXML("<root>" + deep + "</root>"));
	}
	// Extension ext_type round-trip
	{
		asvJSON json;
		uint8_t extData[] = {0x01, 0x02};
		json.putExtension("ext", 42, extData, 2);
		std::string xml = json.toXML();
		ASSERT(xml.find("exttype=\"42\"") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* ext = json2.getRoot()->get("ext");
		ASSERT(ext != nullptr);
		ASSERT(ext->type == asvJSONValue::EXTENSION);
		ASSERT(ext->ext_type == 42);
	}
	// Round-trip: toXML then fromXML
	{
		asvJSON json;
		json.putNull("n");
		json.putBool("b", true);
		json.putInt("i", -42);
		json.putDouble("d", 3.14);
		json.putString("s", "hello");
		json.putDateTime("dt", 1705314645);
		uint8_t binData[] = {0xDE, 0xAD};
		json.putBinary("bin", binData, 2);
		json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
		json.putTimestamp("ts", 98765);
		json.putRegex("rx", "^test$", "gi");
		uint8_t extData[] = {0x01, 0x02};
		json.putExtension("ext", 42, extData, 2);

		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* inner = json2.getRoot();
		ASSERT(inner != nullptr);
		ASSERT(inner->type == asvJSONValue::OBJECT);
		ASSERT(inner->hasKey("n"));
		ASSERT(inner->hasKey("b"));
		ASSERT(inner->hasKey("i"));
		ASSERT(inner->hasKey("d"));
		ASSERT(inner->hasKey("s"));
		ASSERT(inner->hasKey("dt"));
		ASSERT(inner->hasKey("bin"));
		ASSERT(inner->hasKey("oid"));
		ASSERT(inner->hasKey("ts"));
		ASSERT(inner->hasKey("rx"));
		ASSERT(inner->hasKey("ext"));
		ASSERT(inner->get("n")->type == asvJSONValue::NULL_VAL);
		ASSERT(inner->get("b")->getBool() == true);
		ASSERT(inner->get("i")->getInt() == -42);
		ASSERT(inner->get("d")->getDouble() > 3.13 && inner->get("d")->getDouble() < 3.15);
		ASSERT(std::string(inner->get("s")->getString()) == "hello");
	}
	// Round-trip: array
	{
		asvJSON json;
		json.parse(std::string("[1, 2, 3]"));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* inner = json2.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->type == asvJSONValue::ARRAY);
		ASSERT(inner->arr->size() == 3);
		ASSERT(inner->get(static_cast<size_t>(0))->getInt() == 1);
		ASSERT(inner->get(static_cast<size_t>(1))->getInt() == 2);
		ASSERT(inner->get(static_cast<size_t>(2))->getInt() == 3);
	}
	// Non-consecutive same-named children -> single array
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><item>1</item><other>2</other><item>3</item></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		auto* arr = inner->get("item");
		ASSERT(arr != nullptr);
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT(arr->arr->size() == 2);
		ASSERT(arr->get(static_cast<size_t>(0))->getInt() == 1);
		ASSERT(arr->get(static_cast<size_t>(1))->getInt() == 3);
		ASSERT(inner->get("other")->getInt() == 2);
	}
	// Self-closing with type + other attributes preserves all
	{
		asvJSON json;
		ASSERT(json.fromXML("<root custom=\"val\" type=\"datetime\"/>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->hasKey("@type"));
		ASSERT(inner->hasKey("@custom"));
		ASSERT(inner->get("@custom")->getStringView() == "val");
	}
	// Surrogate code point rejected in entity
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>&#xD800;</root>"));
		ASSERT(json.getString("root") == "");
	}
	// xsi:nil="true" - self-closing
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><age xsi:nil=\"true\"/></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->hasKey("age"));
		ASSERT(inner->get("age")->type == asvJSONValue::NULL_VAL);
	}
	// xsi:nil="true" - with closing tag
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><name xsi:nil=\"true\">should be ignored</name></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->hasKey("name"));
		ASSERT(inner->get("name")->type == asvJSONValue::NULL_VAL);
	}
	// xsi:nil="true" - standalone root
	{
		asvJSON json;
		ASSERT(json.fromXML("<root xsi:nil=\"true\"/>"));
		ASSERT(json.isNull("root"));
	}
	// xsi:nil="true" - whitespace before closing >
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><age xsi:nil=\"true\"></age ></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->get("age")->type == asvJSONValue::NULL_VAL);
	}
	// xsi:nil="true" - newline + spaces before closing >
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><x xsi:nil=\"true\">\n</x  ></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->get("x")->type == asvJSONValue::NULL_VAL);
	}
	// xsi:nil with custom namespace prefix (inst:nil with xmlns:inst=XSI)
	{
		asvJSON json;
		ASSERT(json.fromXML("<root xmlns:inst=\"http://www.w3.org/2001/XMLSchema-instance\"><age inst:nil=\"true\"/></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->get("age")->type == asvJSONValue::NULL_VAL);
	}
	// xsi:nil with custom prefix - value "1" instead of "true"
	{
		asvJSON json;
		ASSERT(json.fromXML("<root xmlns:x=\"http://www.w3.org/2001/XMLSchema-instance\"><x:age x:nil=\"1\"/></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->get("x:age")->type == asvJSONValue::NULL_VAL);
	}
	// xsi:nil with wrong prefix mapping should NOT be treated as nil
	{
		asvJSON json;
		ASSERT(json.fromXML("<root xmlns:inst=\"http://example.com\"><age inst:nil=\"true\">value</age></root>"));
		auto* inner = json.getRoot()->get("root");
		ASSERT(inner != nullptr);
		ASSERT(inner->get("age")->type != asvJSONValue::NULL_VAL);
	}
	// DOCTYPE skipped before root
	{
		asvJSON json;
		ASSERT(json.fromXML("<!DOCTYPE root SYSTEM \"foo.dtd\"><root>data</root>"));
		ASSERT(json.getString("root") == "data");
	}
	// XML declaration + DOCTYPE + comment
	{
		asvJSON json;
		ASSERT(json.fromXML("<?xml version=\"1.0\"?>\n<!DOCTYPE root [<!ELEMENT root (#PCDATA)>]>\n<!-- comment -->\n<root>ok</root>"));
		ASSERT(json.getString("root") == "ok");
	}
	// Mixed content: text-element-text produces #content array
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>hello<child>world</child>trailer</root>"));
		auto* root = json.getRoot()->get("root");
		ASSERT(root != nullptr);
		ASSERT(root->hasKey("#content"));
		auto* content = root->get("#content");
		ASSERT(content != nullptr);
		ASSERT(content->type == asvJSONValue::ARRAY);
		ASSERT(content->arr->size() == 3);
		ASSERT((*content->arr)[0]->type == asvJSONValue::STRING);
		ASSERT(std::string((*content->arr)[0]->getString()) == "hello");
		ASSERT((*content->arr)[1]->type == asvJSONValue::OBJECT);
		ASSERT((*content->arr)[1]->hasKey("child"));
		ASSERT(std::string((*content->arr)[1]->get("child")->getString()) == "world");
		ASSERT((*content->arr)[2]->type == asvJSONValue::STRING);
		ASSERT(std::string((*content->arr)[2]->getString()) == "trailer");
	}
	// Mixed content round-trip: text + element + text + self-closing
	{
		std::string input = "<root>text1<child>inner</child>text2<br/>text3</root>";
		asvJSON json;
		ASSERT(json.fromXML(input));
		std::string xml = json.toXML();
		ASSERT(xml.find("text1") != std::string::npos);
		ASSERT(xml.find("<child>") != std::string::npos);
		ASSERT(xml.find("inner") != std::string::npos);
		ASSERT(xml.find("text2") != std::string::npos);
		ASSERT(xml.find("<br") != std::string::npos);
		ASSERT(xml.find("text3") != std::string::npos);
		// Round-trip: re-parse the output
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* root2 = json2.getRoot()->get("root");
		ASSERT(root2 != nullptr);
		ASSERT(root2->hasKey("#content"));
	}
	// Mixed content with attributes on parent
	{
		asvJSON json;
		ASSERT(json.fromXML("<root lang=\"en\">text<b>bold</b>more</root>"));
		auto* root = json.getRoot()->get("root");
		ASSERT(root != nullptr);
		ASSERT(root->hasKey("@lang"));
		ASSERT(root->get("@lang")->getStringView() == "en");
		ASSERT(root->hasKey("#content"));
	}
	// Pure text -> #text key (no interleaving)
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>hello</root>"));
		ASSERT(json.getString("root") == "hello");
	}
	// Pure elements -> named keys (no interleaving)
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><a>1</a><b>2</b></root>"));
		ASSERT(json.getInt("root.a") == 1);
		ASSERT(json.getInt("root.b") == 2);
	}
	// Element-then-text (no interleaving) -> #text + named keys
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>hello<child>world</child></root>"));
		ASSERT(json.getString("root.#text") == "hello");
		ASSERT(json.getString("root.child") == "world");
	}
}

TEST(testFromXMLIntOverflow) {
	// int64 overflow must be preserved as an exact literal (raw_number), not degraded to double
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>99999999999999999999</root>"));
		auto* v = json.get("root");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT(v->raw_number);
		ASSERT_EQ(std::string(v->getString()), "99999999999999999999");
	}
	// Negative overflow
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>-99999999999999999999</root>"));
		auto* v = json.get("root");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT(v->raw_number);
		ASSERT_EQ(std::string(v->getString()), "-99999999999999999999");
	}
	// In-range integers still INT; decimals still DOUBLE
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><a>42</a><b>3.14</b></root>"));
		auto* a = json.getNested("root.a");
		ASSERT(a != nullptr && a->type == asvJSONValue::INT);
		ASSERT_EQ(json.getInt("root.a"), int64_t(42));
		auto* b = json.getNested("root.b");
		ASSERT(b != nullptr && b->type == asvJSONValue::DOUBLE);
	}
	// Round-trip: toXML preserves the exact literal
	{
		asvJSON json;
		ASSERT(json.fromXML("<root>99999999999999999999</root>"));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* v = json2.get("root");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT(v->raw_number);
		ASSERT_EQ(std::string(v->getString()), "99999999999999999999");
	}
}

TEST(testXMLStringRoundTrip) {
	// A STRING that looks like a number/bool/null must not be re-detected on
	// a toXML -> fromXML round trip (type="string" annotation preserves it).
	{
		asvJSON json;
		json.putString("s", "42");
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* v = json2.get("s");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(v->getString()), "42");
	}
	// Numeric-looking double string
	{
		asvJSON json;
		json.putString("s", "3.14");
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* v = json2.get("s");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(v->getString()), "3.14");
	}
	// Bool-looking string
	{
		asvJSON json;
		json.putString("s", "true");
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* v = json2.get("s");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(v->getString()), "true");
	}
	// Null-looking string
	{
		asvJSON json;
		json.putString("s", "null");
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* v = json2.get("s");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(v->getString()), "null");
	}
	// Empty string must stay STRING, not become NULL
	{
		asvJSON json;
		json.putString("s", "");
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* v = json2.get("s");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(v->getString()), "");
	}
	// A normal string has no type annotation and still round-trips
	{
		asvJSON json;
		json.putString("s", "hello");
		std::string xml = json.toXML();
		ASSERT(xml.find("type=\"string\"") == std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* v = json2.get("s");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(v->getString()), "hello");
	}
}

TEST(testXMLArrayStringRoundTrip) {
	// Arrays of numeric/bool/null-looking strings must preserve STRING type
	// on a toXML -> fromXML round trip.
	{
		asvJSON json;
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeString("42", 2));
		arr->arr->push_back(asvJSONValue::makeString("true", 4));
		arr->arr->push_back(asvJSONValue::makeString("", 0));
		arr->arr->push_back(asvJSONValue::makeString("hello", 5));
		json.setValue("a", std::move(arr));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* a = json2.get("a");
		ASSERT(a != nullptr && a->type == asvJSONValue::ARRAY);
		ASSERT(a->arr->size() == 4);
		ASSERT((*a->arr)[0]->type == asvJSONValue::STRING && std::string((*a->arr)[0]->getString()) == "42");
		ASSERT((*a->arr)[1]->type == asvJSONValue::STRING && std::string((*a->arr)[1]->getString()) == "true");
		ASSERT((*a->arr)[2]->type == asvJSONValue::STRING && std::string((*a->arr)[2]->getString()) == "");
		ASSERT((*a->arr)[3]->type == asvJSONValue::STRING && std::string((*a->arr)[3]->getString()) == "hello");
	}
	// Normal arrays of primitives still round-trip without type annotations
	{
		asvJSON json;
		json.parse(std::string("[1, true, null, 3.5]"));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* a = json2.get("root");
		ASSERT(a != nullptr && a->type == asvJSONValue::ARRAY);
		ASSERT(a->arr->size() == 4);
		ASSERT((*a->arr)[0]->type == asvJSONValue::INT && (*a->arr)[0]->getInt() == 1);
		ASSERT((*a->arr)[1]->type == asvJSONValue::BOOL_VAL && (*a->arr)[1]->getBool() == true);
		ASSERT((*a->arr)[2]->type == asvJSONValue::NULL_VAL);
	}
}

TEST(testXMLTextContentRoundTrip) {
	// Object with attribute + text content (#text) must round-trip instead of
	// emitting an invalid <#text> element that fails to re-parse.
	{
		asvJSON json;
		ASSERT(json.fromXML(R"(<root attr="x">hello</root>)"));
		auto* r = json.getRoot()->get("root");
		ASSERT(r != nullptr && r->hasKey("@attr") && r->hasKey("#text"));
		std::string xml = json.toXML();
		ASSERT(xml.find("<#text>") == std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* r2 = json2.getRoot()->get("root");
		ASSERT(r2 != nullptr && r2->hasKey("@attr") && r2->hasKey("#text"));
		ASSERT_EQ(std::string(r2->get("#text")->getString()), "hello");
	}
	// #text with special characters escapes on output and unescapes on input
	{
		asvJSON json;
		ASSERT(json.fromXML(R"(<root attr="x">a &amp; b</root>)"));
		auto* r = json.getRoot()->get("root");
		ASSERT(r != nullptr && r->hasKey("#text"));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* r2 = json2.getRoot()->get("root");
		ASSERT(r2 != nullptr && r2->hasKey("#text"));
		ASSERT_EQ(std::string(r2->get("#text")->getString()), "a & b");
	}
	// Numeric #text stays typed (INT) through the round trip
	{
		asvJSON json;
		ASSERT(json.fromXML(R"(<root attr="x">42</root>)"));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* r2 = json2.getRoot()->get("root");
		ASSERT(r2 != nullptr && r2->hasKey("#text"));
		ASSERT(r2->get("#text")->type == asvJSONValue::INT);
		ASSERT(r2->get("#text")->getInt() == 42);
	}
	// Attribute + element + trailing text round-trips
	{
		asvJSON json;
		ASSERT(json.fromXML(R"(<root lang="en"><p>hi</p>tail</root>)"));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* r2 = json2.getRoot()->get("root");
		ASSERT(r2 != nullptr);
	}
	// #text-bearing object with a child object that itself carries only #text:
	// the inner object must serialize as an element (not a nested <#text>)
	// so the document re-parses.
	{
		asvJSON json;
		json.setValue("root", asvJSONValue::makeObject());
		asvJSONValue* root = json.getRoot()->get("root");
		auto k0 = asvJSONValue::makeObject();
		k0->obj->emplace("k0", asvJSONValue::makeBool(false));
		k0->obj->emplace("#text", asvJSONValue::makeString("3.14", 4));
		auto k2 = asvJSONValue::makeObject();
		k2->obj->emplace("#text", asvJSONValue::makeString("hello", 5));
		k0->obj->emplace("k2", std::move(k2));
		root->obj->emplace("k0", std::move(k0));
		std::string xml = json.toXML();
		ASSERT(xml.find("<#text>") == std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* r2 = json2.getRoot()->get("root");
		ASSERT(r2 != nullptr && r2->hasKey("k0"));
	}
}

TEST(testXmlExtras) {
	// Mixed arrays with OBJECT/ARRAY items must not drop those items (they
	// previously serialized as empty <item></item>, losing data).
	{
		asvJSON json;
		json.parse(std::string("[1,{\"a\":2},\"x\",[5,6]]"));
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* a = json2.getRoot()->get("root");
		ASSERT(a != nullptr && a->type == asvJSONValue::ARRAY);
		ASSERT(a->arr->size() == 4);
		ASSERT((*a->arr)[0]->type == asvJSONValue::INT && (*a->arr)[0]->getInt() == 1);
		ASSERT((*a->arr)[1]->type == asvJSONValue::OBJECT);
		ASSERT((*a->arr)[1]->get("a") != nullptr && (*a->arr)[1]->get("a")->getInt() == 2);
		ASSERT((*a->arr)[2]->type == asvJSONValue::STRING && std::string((*a->arr)[2]->getString()) == "x");
		ASSERT((*a->arr)[3]->type == asvJSONValue::ARRAY);
		ASSERT((*a->arr)[3]->arr->size() == 2);
		ASSERT((*a->arr)[3]->get(static_cast<size_t>(0))->getInt() == 5);
		ASSERT((*a->arr)[3]->get(static_cast<size_t>(1))->getInt() == 6);
	}
	// Regex pattern/options must survive repeated toXML -> fromXML cycles
	// (previously the JSON-quoted form added a quote to the pattern each round).
	{
		asvJSON json;
		json.putRegex("rx", "^test$", "gi");
		json.putRegex("rx2", "plain", "");
		std::string xml = json.toXML();
		ASSERT(xml.find("type=\"regex\">^test$|gi<") != std::string::npos);
		ASSERT(xml.find("type=\"regex\">plain<") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* rx = json2.getRoot()->get("rx");
		ASSERT(rx != nullptr && rx->type == asvJSONValue::REGEX);
		size_t sep = rx->str_data.find('\0');
		ASSERT(sep != std::string::npos);
		ASSERT(rx->str_data.compare(0, sep, "^test$") == 0);
		ASSERT_EQ(std::string(rx->str_data.data() + sep + 1), "gi");
		auto* rx2 = json2.getRoot()->get("rx2");
		ASSERT(rx2 != nullptr && rx2->type == asvJSONValue::REGEX);
		ASSERT(rx2->str_data == std::string("plain\0", 6));
		// second cycle stays clean
		asvJSON json3;
		ASSERT(json3.fromXML(json2.toXML()));
		auto* rx3 = json3.getRoot()->get("rx");
		ASSERT(rx3 != nullptr && rx3->type == asvJSONValue::REGEX);
		size_t sep3 = rx3->str_data.find('\0');
		ASSERT(sep3 != std::string::npos);
		ASSERT(rx3->str_data.compare(0, sep3, "^test$") == 0);
		ASSERT_EQ(std::string(rx3->str_data.data() + sep3 + 1), "gi");
	}
	// Empty BINARY and EXTENSION values round-trip (previously became
	// attribute-only objects and lost their type).
	{
		asvJSON json;
		json.putBinary("b", nullptr, 0);
		json.putExtension("e", 5, nullptr, 0);
		std::string xml = json.toXML();
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* b = json2.getRoot()->get("b");
		ASSERT(b != nullptr && b->type == asvJSONValue::BINARY);
		ASSERT(b->bin_data.empty());
		auto* e = json2.getRoot()->get("e");
		ASSERT(e != nullptr && e->type == asvJSONValue::EXTENSION);
		ASSERT(e->ext_type == 5);
		ASSERT(e->bin_data.empty());
	}
	// <x></x> and <x/> are both empty STRINGs (consistent, not null-vs-string)
	{
		asvJSON json;
		ASSERT(json.fromXML("<x></x>"));
		auto* v = json.get("x");
		ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(v->getString()), "");
	}
	// Leading-zero numerals are STRINGs, not silently-degraded INTs
	{
		asvJSON json;
		ASSERT(json.fromXML("<root><a>042</a><b>-042</b><c>0</c><d>0.5</d></root>"));
		auto* a = json.getNested("root.a");
		ASSERT(a != nullptr && a->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(a->getString()), "042");
		auto* b = json.getNested("root.b");
		ASSERT(b != nullptr && b->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(b->getString()), "-042");
		ASSERT(json.getNested("root.c")->type == asvJSONValue::INT);
		ASSERT(json.getNested("root.d")->type == asvJSONValue::DOUBLE);
	}
	// NaN/Inf DOUBLES serialize as xsi:nil (re-parse as NULL) instead of the
	// string "nan" that previously broke the round trip.
	{
		asvJSON json;
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeDouble(std::numeric_limits<double>::quiet_NaN()));
		arr->arr->push_back(asvJSONValue::makeDouble(INFINITY));
		json.setValue("root", std::move(arr));
		std::string xml = json.toXML();
		ASSERT(xml.find("<item xsi:nil=\"true\"/>") != std::string::npos);
		ASSERT(xml.find("nan") == std::string::npos);
		ASSERT(xml.find("Infinity") == std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* a = json2.getRoot()->get("root");
		ASSERT(a != nullptr && a->type == asvJSONValue::ARRAY);
		ASSERT((*a->arr)[0]->type == asvJSONValue::NULL_VAL);
		ASSERT((*a->arr)[1]->type == asvJSONValue::NULL_VAL);
	}
	// Unterminated elements are rejected instead of silently accepted
	{
		asvJSON json;
		ASSERT(!json.fromXML("<root>abc"));
		ASSERT(!json.fromXML("<root><a>1</a>"));
	}
	// UTF-8 BOM is stripped before parsing
	{
		std::string b = std::string("\xEF\xBB\xBF") + "<root>data</root>";
		asvJSON json;
		ASSERT(json.fromXML(b));
		ASSERT(json.getString("root") == "data");
	}
	// @-prefixed keys holding non-STRING values stringify into attributes
	// instead of emitting an empty attribute value
	{
		asvJSON json;
		json.setValue("o", asvJSONValue::makeObject());
		auto* o = json.getRoot()->get("o");
		o->obj->emplace("@id", asvJSONValue::makeInt(42));
		o->obj->emplace("@on", asvJSONValue::makeBool(true));
		o->obj->emplace("x", asvJSONValue::makeString("1", 1));
		std::string xml = json.toXML();
		ASSERT(xml.find("id=\"42\"") != std::string::npos);
		ASSERT(xml.find("on=\"true\"") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* r = json2.getRoot()->get("o");
		ASSERT(r != nullptr);
		ASSERT_EQ(std::string(r->get("@id")->getString()), "42");
		ASSERT_EQ(std::string(r->get("@on")->getString()), "true");
		ASSERT(std::string(r->get("x")->getString()) == "1");
	}
	// Numeric-looking #text STRINGs get a type="string" annotation so they
	// survive a round trip as STRING
	{
		asvJSON json;
		json.setValue("root", asvJSONValue::makeObject());
		asvJSONValue* root = json.getRoot()->get("root");
		root->obj->emplace("@attr", asvJSONValue::makeString("x", 1));
		root->obj->emplace("#text", asvJSONValue::makeString("42", 2));
		std::string xml = json.toXML();
		ASSERT(xml.find("type=\"string\"") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* t = json2.getRoot()->get("root")->get("#text");
		ASSERT(t != nullptr && t->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(t->getString()), "42");
	}
	// Element keys containing # or @ are sanitized so the output re-parses
	{
		asvJSON json;
		json.parse(std::string(R"({"#foo":1,"a@b":2})"));
		std::string xml = json.toXML();
		ASSERT(xml.find("<#foo>") == std::string::npos);
		ASSERT(xml.find("<a@b>") == std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		ASSERT(json2.getRoot()->get("_foo")->getInt() == 1);
		ASSERT(json2.getRoot()->get("a_b")->getInt() == 2);
	}
	// Non-hex objectid content keeps its text instead of becoming a zeroed id
	{
		asvJSON json;
		ASSERT(json.fromXML("<x type=\"objectid\">zzzzzzzzzzzzzzzzzzzzzzzz</x>"));
		auto* v = json.get("x");
		ASSERT(v != nullptr && v->type != asvJSONValue::OBJECTID);
		ASSERT_EQ(std::string(v->get("#text")->getString()), "zzzzzzzzzzzzzzzzzzzzzzzz");
	}
	// Mixed-content numeric-looking STRING children get a type annotation so
	// the wrapper re-parses as STRING
	{
		asvJSON json;
		json.setValue("root", asvJSONValue::makeObject());
		asvJSONValue* root = json.getRoot()->get("root");
		root->obj->emplace("@tag", asvJSONValue::makeString("t", 1));
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeString("text ", 5));
		auto wrapper = asvJSONValue::makeObject();
		wrapper->obj->emplace("child", asvJSONValue::makeString("42", 2));
		arr->arr->push_back(std::move(wrapper));
		arr->arr->push_back(asvJSONValue::makeString(" tail", 5));
		root->obj->emplace("#content", std::move(arr));
		std::string xml = json.toXML();
		ASSERT(xml.find("<child type=\"string\">42</child>") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromXML(xml));
		auto* c = json2.getRoot()->get("root")->get("#content");
		ASSERT(c != nullptr && c->type == asvJSONValue::ARRAY);
		ASSERT((*c->arr)[1]->type == asvJSONValue::OBJECT);
		ASSERT((*c->arr)[1]->get("child")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string((*c->arr)[1]->get("child")->getString()), "42");
	}
}

TEST(testToYAML) {
	// Empty root
	{
		asvJSON json;
		json.parse(std::string("{}"));
		std::string yml = json.toYAML();
		ASSERT(!yml.empty());
		ASSERT(yml.find("---") != std::string::npos);
	}
	// Scalar root
	{
		asvJSON json;
		json.parse(std::string("\"hello\""));
		std::string yml = json.toYAML();
		ASSERT(yml.find("hello") != std::string::npos);
	}
	// All types
	{
		asvJSON json;
		json.putNull("n");
		json.putBool("b", true);
		json.putInt("i", -42);
		json.putDouble("d", 3.14);
		json.putString("s", "plain");
		json.putString("sq", "it's fine");
		json.putString("dq", "she said \"hi\"");
		json.putString("numlike", "123");
		json.putDateTime("dt", 1705314645);
		uint8_t binData[] = {0xDE, 0xAD};
		json.putBinary("bin", binData, 2);
		json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
		json.putTimestamp("ts", 98765);
		json.putRegex("rx", "^test$", "gi");
		uint8_t extData[] = {0x01, 0x02};
		json.putExtension("ext", 42, extData, 2);

		std::string yml = json.toYAML();
		ASSERT(yml.find("n: ~") != std::string::npos || yml.find("'n': ~") != std::string::npos);
		ASSERT(yml.find("b: true") != std::string::npos);
		ASSERT(yml.find("i: -42") != std::string::npos);
		ASSERT(yml.find("plain") != std::string::npos);
		ASSERT(yml.find("!objectid") != std::string::npos);
		ASSERT(yml.find("!regex") != std::string::npos);
		ASSERT(yml.find("!ext") != std::string::npos);
		ASSERT(yml.find("!!binary") != std::string::npos);
	}
	// Array
	{
		asvJSON json;
		json.parse(std::string("[1, 2, 3]"));
		std::string yml = json.toYAML();
		ASSERT(yml.find("- 1") != std::string::npos);
		ASSERT(yml.find("- 2") != std::string::npos);
		ASSERT(yml.find("- 3") != std::string::npos);
	}
	// Nested object
	{
		asvJSON json;
		json.parse(std::string("{\"a\": {\"b\": {\"c\": 42}}}"));
		std::string yml = json.toYAML();
		ASSERT(yml.find("a:") != std::string::npos);
		ASSERT(yml.find("  b:") != std::string::npos);
		ASSERT(yml.find("    c: 42") != std::string::npos);
	}
	// Object inside array: emitted as empty dash + indented keys
	{
		asvJSON json;
		json.parse(std::string("[{\"name\":\"a\",\"age\":30},{\"name\":\"b\",\"age\":40}]"));
		std::string yml = json.toYAML();
		ASSERT(yml.find("- ") != std::string::npos);
		ASSERT(yml.find("name: a") != std::string::npos || yml.find("'name': a") != std::string::npos);
		ASSERT(yml.find("age: 30") != std::string::npos);
	}
	// Array nested in array: empty dash + indented dash items
	{
		asvJSON json;
		json.parse(std::string("{\"m\":[[1],[2]]}"));
		std::string yml = json.toYAML();
		asvJSON j2;
		ASSERT(j2.fromYAML(yml));
		ASSERT_EQ(j2.getRoot()->get("m")->arr->at(0)->arr->at(0)->getInt(), 1);
		ASSERT_EQ(j2.getRoot()->get("m")->arr->at(1)->arr->at(0)->getInt(), 2);
	}
	// Numeric-looking strings are quoted on encode
	{
		asvJSON json;
		json.putString("a", "001");
		json.putString("b", "1.2");
		std::string yml = json.toYAML();
		ASSERT(yml.find("'001'") != std::string::npos || yml.find("\"001\"") != std::string::npos);
		ASSERT(yml.find("'1.2'") != std::string::npos || yml.find("\"1.2\"") != std::string::npos);
	}
	// asvJSONValue::toYAML (direct)
	{
		auto v = asvJSONValue::makeString("test", 4);
		std::string out;
		v->toYAML(out);
		ASSERT(!out.empty());
		ASSERT(out.find("test") != std::string::npos);
	}
	// Quoting
	{
		asvJSON json;
		json.putString("k", "true");
		std::string yml = json.toYAML();
		ASSERT(yml.find("'true'") != std::string::npos || yml.find("\"true\"") != std::string::npos);
	}
	// Multiline string
	{
		asvJSON json;
		json.putString("k", "line1\nline2");
		std::string yml = json.toYAML();
		ASSERT(yml.find('|') != std::string::npos);
		ASSERT(yml.find("line1") != std::string::npos);
	}
	// Empty-string key round-trips in block style ("'': v" instead of dropping
	// the key and emitting the value as a bare root scalar).
	{
		asvJSON json;
		json.parse(std::string("{\"\":\"v\"}"));
		std::string yml = json.toYAML();
		ASSERT(yml.find("'': v") != std::string::npos || yml.find("\"\": v") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromYAML(yml));
		ASSERT(j2.getRoot()->hasKey(""));
		ASSERT_EQ(std::string(j2.getRoot()->get("")->getString()), "v");
	}
	// Strings containing a tab round-trip (serialized with double quotes which
	// escape the tab, instead of a raw tab in single quotes that broke the
	// intermediate JSON on parse). Control chars are escaped the same way.
	{
		asvJSON json;
		json.putString("k", "tab\there");
		std::string yml = json.toYAML();
		ASSERT(yml.find("\"tab\\there\"") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromYAML(yml));
		ASSERT_EQ(std::string(j2.getRoot()->get("k")->getString()), "tab\there");
	}
	{
		asvJSON json;
		json.putString("k", "ctrl\x01x");
		std::string yml = json.toYAML();
		asvJSON j2;
		ASSERT(j2.fromYAML(yml));
		auto* v = j2.getRoot()->get("k");
		ASSERT_EQ(v->getStringLen(), 6);
		ASSERT_EQ(static_cast<unsigned char>(v->getString()[4]), (unsigned char)0x01);
	}
}

TEST(testFromYAML) {
	// Empty object
	{
		asvJSON json;
		ASSERT(json.fromYAML("---\n{}"));
		ASSERT(json.getRoot()->type == asvJSONValue::OBJECT);
	}
	// Basic key-value pairs
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\nb: true\nc: null\nd: hello"));
		ASSERT_EQ(json.getInt("a"), 1);
		ASSERT_EQ(json.getBool("b"), true);
		ASSERT(json.isNull("c"));
		ASSERT_EQ(std::string(json.getString("d")), "hello");
	}
	// Nested objects
	{
		asvJSON json;
		ASSERT(json.fromYAML("a:\n  b:\n    c: 42"));
		auto* b = json.getRoot()->get("a");
		ASSERT(b != nullptr);
		auto* bc = b->get("b");
		ASSERT(bc != nullptr);
		ASSERT_EQ(bc->get("c")->getInt(), 42);
	}
	// Array at root
	{
		asvJSON json;
		ASSERT(json.fromYAML("- 10\n- 20\n- 30"));
		auto* arr = json.getRoot();
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(3));
		ASSERT_EQ(arr->arr->at(0)->getInt(), 10);
	}
	// Array as object value (same indent as key)
	{
		asvJSON json;
		ASSERT(json.fromYAML("vals:\n- 1\n- 2\n- 3"));
		auto* v = json.getRoot()->get("vals");
		ASSERT(v != nullptr && v->type == asvJSONValue::ARRAY);
		ASSERT_EQ(v->arr->size(), size_t(3));
		ASSERT_EQ(v->arr->at(0)->getInt(), 1);
	}
	// Inline value after colon
	{
		asvJSON json;
		ASSERT(json.fromYAML("name: Alice\nage: 30"));
		ASSERT_EQ(std::string(json.getString("name")), "Alice");
		ASSERT_EQ(json.getInt("age"), 30);
	}
	// Multiline string (block scalar |)
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: |\n  hello\n  world"));
		ASSERT_EQ(std::string(json.getString("text")), "hello\nworld\n");
	}
	// Quoted keys
	{
		asvJSON json;
		ASSERT(json.fromYAML("'quoted key': value"));
		ASSERT_EQ(std::string(json.getString("quoted key")), "value");
	}
	// Quoted values (double)
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"hello\\nworld\""));
		ASSERT_EQ(std::string(json.getString("s")), "hello\nworld");
	}
	// Quoted values (single)
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: 'single quoted'"));
		ASSERT_EQ(std::string(json.getString("s")), "single quoted");
	}
	// Single-quoted scalar containing a tab parses (the control char is escaped
	// into the intermediate JSON rather than corrupting it).
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: 'a\tb'"));
		ASSERT_EQ(std::string(json.getString("s")), "a\tb");
	}
	// Empty containers
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: {}\nb: []"));
		ASSERT(json.getRoot()->get("a")->type == asvJSONValue::OBJECT);
		ASSERT(json.getRoot()->get("b")->type == asvJSONValue::ARRAY);
	}
	// Comments
	{
		asvJSON json;
		ASSERT(json.fromYAML("name: Alice # this is a comment\nage: 30"));
		ASSERT_EQ(std::string(json.getString("name")), "Alice");
		ASSERT_EQ(json.getInt("age"), 30);
	}
	// Document marker
	{
		asvJSON json;
		ASSERT(json.fromYAML("---\nname: test"));
		ASSERT_EQ(std::string(json.getString("name")), "test");
	}
	// Round-trip: generated YAML -> parse
	{
		asvJSON json;
		json.putNull("n");
		json.putBool("b", true);
		json.putInt("i", -42);
		json.putDouble("d", 3.14);
		json.putString("s", "plain");
		json.putString("qq", "it's \"fine\"");
		json.putString("ml", "line1\nline2");
		asvJSON json2;
		ASSERT(json2.fromYAML(json.toYAML()));
		ASSERT(json2.isNull("n"));
		ASSERT_EQ(json2.getBool("b"), true);
		ASSERT_EQ(json2.getInt("i"), -42);
		double dv = json2.getDouble("d");
		ASSERT(dv > 3.13 && dv < 3.15);
		ASSERT_EQ(std::string(json2.getString("s")), "plain");
	}
	// Object with array at deeper nesting
	{
		asvJSON json;
		ASSERT(json.fromYAML("outer:\n  inner:\n    - x\n    - y"));
		auto* outer = json.getRoot()->get("outer");
		ASSERT(outer != nullptr);
		auto* inner = outer->get("inner");
		ASSERT(inner != nullptr && inner->type == asvJSONValue::ARRAY);
		ASSERT_EQ(inner->arr->size(), size_t(2));
		ASSERT_EQ(std::string(inner->arr->at(0)->getString()), "x");
	}
	// Objects as sequence items (YAML block style)
	{
		asvJSON json;
		ASSERT(json.fromYAML("- name: a\n  age: 30\n- name: b\n  age: 40"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		auto* o0 = root->arr->at(0).get();
		ASSERT(o0 && o0->type == asvJSONValue::OBJECT);
		ASSERT_EQ(std::string(o0->get("name")->getString()), "a");
		ASSERT_EQ(o0->get("age")->getInt(), 30);
		auto* o1 = root->arr->at(1).get();
		ASSERT_EQ(std::string(o1->get("name")->getString()), "b");
		ASSERT_EQ(o1->get("age")->getInt(), 40);
	}
	// Sequence nested as object value at same indent
	{
		asvJSON json;
		ASSERT(json.fromYAML("base:\n- name: a\n  age: 30"));
		auto* base = json.getRoot()->get("base");
		ASSERT(base != nullptr && base->type == asvJSONValue::ARRAY);
		ASSERT_EQ(base->arr->size(), size_t(1));
		ASSERT_EQ(std::string(base->arr->at(0)->get("name")->getString()), "a");
		ASSERT_EQ(base->arr->at(0)->get("age")->getInt(), 30);
	}
	// Empty dash + indented keys (encoder emission form)
	{
		asvJSON json;
		ASSERT(json.fromYAML("- \n  age: 30\n  'n': a\n- \n  age: 40"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		ASSERT_EQ(std::string(root->arr->at(0)->get("n")->getString()), "a");
		ASSERT_EQ(root->arr->at(0)->get("age")->getInt(), 30);
		ASSERT_EQ(root->arr->at(1)->get("age")->getInt(), 40);
	}
	// Nested sequences
	{
		asvJSON json;
		ASSERT(json.fromYAML("- - 1\n- - 2\n- - 3"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(3));
		ASSERT_EQ(root->arr->at(0)->type, asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->at(0)->arr->at(0)->getInt(), 1);
		ASSERT_EQ(root->arr->at(2)->arr->at(0)->getInt(), 3);
	}
	// Deep object value inside sequence item
	{
		asvJSON json;
		ASSERT(json.fromYAML("- a:\n    x: 1\n  b: 2"));
		auto* o0 = json.getRoot()->arr->at(0).get();
		ASSERT_EQ(o0->get("a")->get("x")->getInt(), 1);
		ASSERT_EQ(o0->get("b")->getInt(), 2);
	}
	// Leading-zero numeric scalars normalize to valid JSON numbers
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 007\nb: 001"));
		ASSERT_EQ(json.getInt("a"), 7);
		ASSERT_EQ(json.getInt("b"), 1);
	}
	// Quoted key containing colon inside object-in-sequence
	{
		asvJSON json;
		ASSERT(json.fromYAML("- 'a:b': 1\n  'c': d"));
		auto* o0 = json.getRoot()->arr->at(0).get();
		ASSERT_EQ(o0->get("a:b")->getInt(), 1);
		ASSERT_EQ(std::string(o0->get("c")->getString()), "d");
	}
	// Non-string keys in flow mappings stringify to JSON string keys
	{
		asvJSON json;
		ASSERT(json.fromYAML("{1: x}"));
		ASSERT_EQ(std::string(json.getString("1")), "x");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("{true: x, null: y, 2.5: z}"));
		ASSERT_EQ(std::string(json.getRoot()->get("true")->getString()), "x");
		ASSERT_EQ(std::string(json.getRoot()->get("null")->getString()), "y");
		ASSERT_EQ(std::string(json.getRoot()->get("2.5")->getString()), "z");
	}
	// Complex (flow collection) keys stringify deterministically via their
	// intermediate JSON text: "{[a, b]: v}" -> key ["a","b"], value "v".
	{
		asvJSON json;
		ASSERT(json.fromYAML("{[a, b]: v}"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("{[a, b]: v, k: 2}"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
		ASSERT_EQ(json.getInt("k"), 2);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("? [a, b]: v"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("? [a, b]\n: v"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("? {x: 1}: v"));
		ASSERT_EQ(std::string(json.getString("{\"x\":1}")), "v");
	}
	// Multi-line block-style compound keys: the explicit key is a node on the
	// following (deeper) block lines, stringified via its intermediate JSON
	// just like flow compound keys.
	{
		asvJSON json;
		ASSERT(json.fromYAML("?\n  - a\n  - b\n: v"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("?\n  a: 1\n  b: 2\n: v"));
		ASSERT_EQ(std::string(json.getString("{\"a\":1,\"b\":2}")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("?\n  [a, b]\n: v"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
	}
	// Multi-line explicit keys that are plain/quoted scalars fold and stringify
	{
		asvJSON json;
		ASSERT(json.fromYAML("?\n  foo\n: v"));
		ASSERT_EQ(std::string(json.getString("foo")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("?\n  \"some key\"\n: v"));
		ASSERT_EQ(std::string(json.getString("some key")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("?\n  foo bar\n  baz\n: v"));
		ASSERT_EQ(std::string(json.getString("foo bar baz")), "v");
	}
	// Explicit-key mapping as a sequence item ("- ? key") and a sequence item
	// whose key is a deeper block node
	{
		asvJSON json;
		ASSERT(json.fromYAML("- ? key\n  : v"));
		ASSERT_EQ(std::string(json.getRoot()->arr->at(0)->get("key")->getString()), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("- ? [a, b]\n  : v"));
		ASSERT_EQ(std::string(json.getRoot()->arr->at(0)->get("[\"a\",\"b\"]")->getString()), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("- ?\n    - a\n    - b\n  : v"));
		ASSERT_EQ(std::string(json.getRoot()->arr->at(0)->get("[\"a\",\"b\"]")->getString()), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("k:\n  ?\n    - a\n  : v"));
		ASSERT_EQ(std::string(json.getRoot()->get("k")->get("[\"a\"]")->getString()), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("{[a, [b]]: v}"));
		ASSERT_EQ(std::string(json.getString("[\"a\",[\"b\"]]")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("{[{x: 1}, 2]: v}"));
		ASSERT_EQ(std::string(json.getString("[{\"x\":1},2]")), "v");
	}
	// Complex keys in block mappings
	{
		asvJSON json;
		ASSERT(json.fromYAML("[a, b]: v"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("{x: 1}: v\nc: 2"));
		ASSERT_EQ(std::string(json.getString("{\"x\":1}")), "v");
		ASSERT_EQ(json.getInt("c"), 2);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("k:\n  [a,b]: v"));
		asvJSONValue* kk = json.get("k");
		ASSERT(kk != nullptr);
		ASSERT_EQ(std::string(kk->get("[\"a\",\"b\"]")->getString()), "v");
	}
	// Multi-line flow compound key
	{
		asvJSON json;
		ASSERT(json.fromYAML("? [a,\n  b]\n: v"));
		ASSERT_EQ(std::string(json.getString("[\"a\",\"b\"]")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("? {x: 1,\n  y: 2}\n: v"));
		ASSERT_EQ(std::string(json.getString("{\"x\":1,\"y\":2}")), "v");
	}
	// Multi-line explicit scalar keys fold deeper lines
	{
		asvJSON json;
		ASSERT(json.fromYAML("? a\n  b\n: c"));
		ASSERT_EQ(std::string(json.getString("a b")), "c");
	}
	// Round-trip: reserialize with quoted key, reparsing reproduces it
	{
		asvJSON json;
		ASSERT(json.fromYAML("{x: 1}: v"));
		std::string y = json.toYAML();
		asvJSON json2;
		ASSERT(json2.fromYAML(y));
		ASSERT_EQ(std::string(json2.getString("{\"x\":1}")), "v");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("{[a, b]: v}"));
		ASSERT(std::string(json.serialize()).find("\"[\\\"a\\\",\\\"b\\\"]\"") != std::string::npos);
	}
	// Explicit keys: two-line "? key\n: value"
	{
		asvJSON json;
		ASSERT(json.fromYAML("? a\n: b"));
		ASSERT_EQ(std::string(json.getString("a")), "b");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("? 1\n: x"));
		ASSERT_EQ(std::string(json.getString("1")), "x");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("? 'q k'\n: v"));
		ASSERT_EQ(std::string(json.getString("q k")), "v");
	}
	// Explicit keys with inline value
	{
		asvJSON json;
		ASSERT(json.fromYAML("? a: b"));
		ASSERT_EQ(std::string(json.getString("a")), "b");
	}
	// Explicit key with a nested mapping value
	{
		asvJSON json;
		ASSERT(json.fromYAML("? k\n:\n  x: 1"));
		ASSERT_EQ(json.getInt("k.x"), 1);
	}
	// Explicit key mixed into a regular mapping
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n? x\n: 2"));
		ASSERT_EQ(json.getInt("a"), 1);
		ASSERT_EQ(json.getInt("x"), 2);
	}
	// Explicit key that is never followed by ": value" is an error
	{
		asvJSON json;
		ASSERT(!json.fromYAML("? a\nb: c"));
	}
	// --- Syntax-error strictness ---
	// "key:" with nothing following is an implicit null (was "{}")
	{
		asvJSON json;
		ASSERT(json.fromYAML("key:"));
		ASSERT(json.isNull("key"));
	}
	// "key:" whose value is a bare scalar on a deeper line
	{
		asvJSON json;
		ASSERT(json.fromYAML("key:\n  hello"));
		ASSERT_EQ(std::string(json.getString("key")), "hello");
	}
	// Empty value with a deeper mapping / sequence / flow value
	{
		asvJSON json;
		ASSERT(json.fromYAML("k:\n  x: 1"));
		ASSERT_EQ(json.getInt("k.x"), 1);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("k:\n- 1\n- 2"));
		auto* arr = json.getArray("k");
		ASSERT(arr && (*arr).size() == 2);
		ASSERT_EQ((*arr)[0].getInt(), 1);
		ASSERT_EQ((*arr)[1].getInt(), 2);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("k:\n  {a: 1}"));
		ASSERT_EQ(json.getInt("k.a"), 1);
	}
	// Colon without space is a plain scalar, not a mapping: "a:b" is not "a: b"
	{
		asvJSON json;
		ASSERT(!json.fromYAML("a:b"));
		ASSERT(std::string(json.lastError).find("Expected ':'") != std::string::npos);
	}
	// ": value" with no key
	{
		asvJSON json;
		ASSERT(!json.fromYAML(": value"));
		ASSERT(std::string(json.lastError).find("mapping key expected before ':'") != std::string::npos);
	}
	// "a: : b" -- colon indicator where a value is required
	{
		asvJSON json;
		ASSERT(!json.fromYAML("a: : b"));
		ASSERT(std::string(json.lastError).find("expected value after ':'") != std::string::npos);
	}
	// Unterminated flow collections
	{
		asvJSON json;
		ASSERT(!json.fromYAML("a: {b: 1"));
		ASSERT(std::string(json.lastError).find("unterminated flow collection") != std::string::npos);
	}
	{
		asvJSON json;
		ASSERT(!json.fromYAML("a: [1, 2"));
		ASSERT(std::string(json.lastError).find("unterminated flow collection") != std::string::npos);
	}
	// Trailing garbage after a flow value / quoted string
	{
		asvJSON json;
		ASSERT(!json.fromYAML("a: [1, 2] extra"));
		ASSERT(std::string(json.lastError).find("trailing content after flow value") != std::string::npos);
	}
	{
		asvJSON json;
		ASSERT(!json.fromYAML("key: \"abc\" extra"));
		ASSERT(std::string(json.lastError).find("trailing content after quoted string") != std::string::npos);
	}
	// Unterminated quoted strings
	{
		asvJSON json;
		ASSERT(!json.fromYAML("key: \"abc"));
		ASSERT(std::string(json.lastError).find("unterminated double-quoted string") != std::string::npos);
	}
	{
		asvJSON json;
		ASSERT(!json.fromYAML("key: 'abc"));
		ASSERT(std::string(json.lastError).find("unterminated single-quoted string") != std::string::npos);
	}
	// Inconsistent indentation inside one sequence / mixed seq-mapping
	{
		asvJSON json;
		ASSERT(!json.fromYAML("- a\n - b"));
		ASSERT(std::string(json.lastError).find("inconsistent indentation") != std::string::npos);
	}
	{
		asvJSON json;
		ASSERT(!json.fromYAML("a: 1\n- b"));
		ASSERT(std::string(json.lastError).find("sequence and mapping mixed") != std::string::npos);
	}
	// Garbage after a block scalar header ("| foo")
	{
		asvJSON json;
		ASSERT(!json.fromYAML("key: | foo\n  bar"));
		ASSERT(std::string(json.lastError).find("block scalar header") != std::string::npos);
	}
	// Dash with an empty key: "- : x"
	{
		asvJSON json;
		ASSERT(!json.fromYAML("- : x"));
		ASSERT(std::string(json.lastError).find("mapping key expected before ':'") != std::string::npos);
	}
	// Block scalar body must not be normalized (dash-looking lines preserved)
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: |\n  - a: 1\n  - b: 2"));
		ASSERT_EQ(std::string(json.getString("text")), "- a: 1\n- b: 2\n");
	}
	// Empty value in sequence object followed by nested mapping
	{
		asvJSON json;
		ASSERT(json.fromYAML("- name: a\n  extra:\n    x: 1"));
		auto* o0 = json.getRoot()->arr->at(0).get();
		ASSERT_EQ(std::string(o0->get("name")->getString()), "a");
		ASSERT_EQ(o0->get("extra")->get("x")->getInt(), 1);
	}
	// Encoder round-trip: arrays nested in arrays (m: [[1],[2]])
	{
		asvJSON json;
		json.parse(std::string("{\"m\":[[1],[2]]}"));
		asvJSON j2;
		ASSERT(j2.fromYAML(json.toYAML()));
		auto* m = j2.getRoot()->get("m");
		ASSERT(m != nullptr && m->type == asvJSONValue::ARRAY);
		ASSERT_EQ(m->arr->size(), size_t(2));
		ASSERT_EQ(m->arr->at(0)->arr->at(0)->getInt(), 1);
		ASSERT_EQ(m->arr->at(1)->arr->at(0)->getInt(), 2);
	}
	// Encoder round-trip: objects in arrays
	{
		asvJSON json;
		json.parse(std::string("{\"users\":[{\"name\":\"a\",\"age\":30},{\"name\":\"b\",\"age\":40}]}"));
		asvJSON j2;
		ASSERT(j2.fromYAML(json.toYAML()));
		auto* us = j2.getRoot()->get("users");
		ASSERT(us != nullptr && us->type == asvJSONValue::ARRAY);
		ASSERT_EQ(us->arr->size(), size_t(2));
		ASSERT_EQ(std::string(us->arr->at(0)->get("name")->getString()), "a");
		ASSERT_EQ(us->arr->at(0)->get("age")->getInt(), 30);
		ASSERT_EQ(std::string(us->arr->at(1)->get("name")->getString()), "b");
		ASSERT_EQ(us->arr->at(1)->get("age")->getInt(), 40);
	}
	// Encoder round-trip: numeric-looking strings stay strings
	{
		asvJSON json;
		json.putString("a", "001");
		json.putString("b", "1.2");
		json.putString("c", "-42");
		asvJSON j2;
		ASSERT(j2.fromYAML(json.toYAML()));
		ASSERT_EQ(std::string(j2.getRoot()->get("a")->getString()), "001");
		ASSERT_EQ(std::string(j2.getRoot()->get("b")->getString()), "1.2");
		ASSERT_EQ(std::string(j2.getRoot()->get("c")->getString()), "-42");
	}
	// Encoder round-trip: numeric/boolean-looking string keys round-trip
	{
		asvJSON json;
		json.putString("1", "one");
		json.putString("true", "yes");
		json.putString("a b", "spaced");
		asvJSON j2;
		ASSERT(j2.fromYAML(json.toYAML()));
		ASSERT_EQ(std::string(j2.getRoot()->get("1")->getString()), "one");
		ASSERT_EQ(std::string(j2.getRoot()->get("true")->getString()), "yes");
		ASSERT_EQ(std::string(j2.getRoot()->get("a b")->getString()), "spaced");
	}
	// Round-trip special types (OBJECTID, REGEX, BINARY, EXTENSION)
	{
		asvJSON json;
		json.putObjectId("oid", std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c", 12));
		json.putRegex("rx", "^test$", "gi");
		uint8_t bin[] = {0xde, 0xad};
		json.putBinary("bin", bin, 2);
		uint8_t ext[] = {0x01, 0x02};
		json.putExtension("ext", 42, ext, 2);
		json.putDateTime("dt", 1705314645);
		std::string yml = json.toYAML();
		asvJSON j2;
		ASSERT(j2.fromYAML(std::string_view(yml)));
		auto* oid = j2.getRoot()->get("oid");
		ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
		ASSERT_EQ(std::string_view(oid->str_data), std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c", 12));
		ASSERT_EQ(j2.getRegexPattern("rx"), "^test$");
		ASSERT_EQ(j2.getRegexOptions("rx"), "gi");
		auto* bin2 = j2.getRoot()->get("bin");
		ASSERT(bin2 != nullptr && bin2->type == asvJSONValue::BINARY);
		ASSERT_EQ(bin2->bin_data.size(), size_t(2));
		ASSERT_EQ(bin2->bin_data[0], 0xde);
		auto* ext2 = j2.getRoot()->get("ext");
		ASSERT(ext2 != nullptr && ext2->type == asvJSONValue::EXTENSION);
		ASSERT_EQ(ext2->ext_type, 42);
		ASSERT_EQ(ext2->bin_data.size(), size_t(2));
		auto* dt2 = j2.getRoot()->get("dt");
		ASSERT(dt2 != nullptr && dt2->type == asvJSONValue::DATETIME);
		ASSERT_EQ(dt2->timestamp, 1705314645);
	}
	// Folded block scalar >
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >\n  hello\n  world"));
		ASSERT_EQ(std::string(json.getString("text")), "hello world\n");
	}
	// Folded block scalar with empty line
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >\n  para1\n\n  para2"));
		ASSERT_EQ(std::string(json.getString("text")), "para1\npara2\n");
	}
	// Literal block scalar with strip chomp (|-) removes trailing newline
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: |-\n  hello\n  world"));
		ASSERT_EQ(std::string(json.getString("text")), "hello\nworld");
	}
	// Literal block scalar with keep chomp (|+) preserves all trailing newlines
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: |+\n  hello\n\n"));
		ASSERT_EQ(std::string(json.getString("text")), "hello\n\n");
	}
	// Folded block scalar with strip chomp (>-)
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >-\n  hello\n  world"));
		ASSERT_EQ(std::string(json.getString("text")), "hello world");
	}
	// Folded block scalar with keep chomp (>+)
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >+\n  hello\n\n"));
		ASSERT_EQ(std::string(json.getString("text")), "hello\n");
	}
	// Block scalar with indent indicator digit (ignored in favor of auto-detect)
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: |2-\n  hello\n  world"));
		ASSERT_EQ(std::string(json.getString("text")), "hello\nworld");
	}
	// Folded block scalar does not fold a line ending in white space
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >\n  hello   \n  world"));
		ASSERT_EQ(std::string(json.getString("text")), "hello   \nworld\n");
	}
	// Folded block scalar keeps the break literal after a more-indented line
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >\n  a\n    b\n  c"));
		ASSERT_EQ(std::string(json.getString("text")), "a\n  b c\n");
	}
	// Folded block scalar: trailing space on a line keeps its break literal
	// even across a blank line
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >-\n  a  \n\n  b"));
		ASSERT_EQ(std::string(json.getString("text")), "a  \n\nb");
	}
	// Literal block scalar preserves excess leading indentation as content
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: |\n  a\n    b\n  c"));
		ASSERT_EQ(std::string(json.getString("text")), "a\n  b\nc\n");
	}
	// Literal block scalar with explicit indent digit preserves excess spaces
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: |2\n    hello\n      world"));
		ASSERT_EQ(std::string(json.getString("text")), "  hello\n    world\n");
	}
	// Round-trip: multi-line strings with and without trailing newline
	{
		asvJSON json;
		json.putString("a", "line1\nline2");
		json.putString("b", "line1\nline2\n");
		json.putString("c", "line1\n\nline2\n\n");
		json.putString("d", "leading\n\n");
		json.putString("e", "\nlead");
		json.putString("f", "  lead\n  x\n");
		json.putString("g", "no\n\tbolt\nx");
		asvJSON j2;
		ASSERT(j2.fromYAML(json.toYAML()));
		ASSERT_EQ(std::string(j2.getString("a")), "line1\nline2");
		ASSERT_EQ(std::string(j2.getString("b")), "line1\nline2\n");
		ASSERT_EQ(std::string(j2.getString("c")), "line1\n\nline2\n\n");
		ASSERT_EQ(std::string(j2.getString("d")), "leading\n\n");
		ASSERT_EQ(std::string(j2.getString("e")), "\nlead");
		ASSERT_EQ(std::string(j2.getString("f")), "  lead\n  x\n");
		ASSERT_EQ(std::string(j2.getString("g")), "no\n\tbolt\nx");
	}
	// YAML comment round-trip: header, per-key trailing, nested leading,
	// sequence-element comments, block-scalar-header trailing, footer
	{
		asvJSON json;
		const char* in = "# header comment\n\ncity: Berlin # trailing on scalar\nnested:\n  # inner comment\n  value: 42 # after int\nlist:\n  # before item\n  - alpha\n  - beta # beta gets this\n  - gamma\nblock: | # block scalar header\n  line1\n\n# footer comment\n";
		ASSERT(json.fromYAML(in));
		std::string out = json.toYAML();
		ASSERT(out.find("# header comment") != std::string::npos);
		ASSERT(out.find("city: Berlin # trailing on scalar") != std::string::npos);
		ASSERT(out.find("# inner comment") != std::string::npos);
		ASSERT(out.find("value: 42 # after int") != std::string::npos);
		ASSERT(out.find("# before item") != std::string::npos);
		ASSERT(out.find("beta # beta gets this") != std::string::npos);
		ASSERT(out.find("| # block scalar header") != std::string::npos);
		ASSERT(out.find("# footer comment") != std::string::npos);
		// idempotent: re-parsing the emitted document re-emits it verbatim
		asvJSON j2;
		ASSERT(j2.fromYAML(out));
		ASSERT_EQ(out, j2.toYAML());
	}
	// Dashes at the same indent as the owning key ("list:\n- a\n- b")
	// still collect comments and round-trip
	{
		asvJSON json;
		ASSERT(json.fromYAML("list:\n- alpha\n- beta # t\nsib: 1\n# tail\n"));
		std::string out = json.toYAML();
		ASSERT(out.find("- beta # t") != std::string::npos);
		ASSERT(out.find("# tail") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromYAML(out));
		asvJSONValue* lst = j2.getRoot()->get("list");
		ASSERT(lst != nullptr && lst->type == asvJSONValue::ARRAY);
		ASSERT_EQ(lst->arr->size(), size_t(2));
		ASSERT_EQ(std::string(lst->arr->at(1)->getString()), "beta");
	}
	// A comment-prefixed line inside a block-scalar body is treated as a
	// comment by the decoder, so it is dropped from the content (and never
	// collected as a node comment) -- the body round-trips the remainder
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: |\n  # data not comment\n  more\n"));
		ASSERT_EQ(std::string(json.getString("s")), "more\n");
		asvJSON j2;
		ASSERT(j2.fromYAML(json.toYAML()));
		ASSERT_EQ(std::string(j2.getString("s")), "more\n");
	}
	// Comments are YAML-only metadata: they do not leak into the JSON
	// output, and a JSON round-trip yields a comment-free YAML document
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1 # trailing\n# lead\nb: 2\n"));
		std::string js = json.serialize();
		ASSERT(js.find('#') == std::string::npos);
		asvJSON j2;
		ASSERT(j2.parse(js));
		ASSERT_EQ(j2.toYAML(), std::string("---\na: 1\nb: 2\n"));
	}
	// Double-quoted YAML string with \n escape
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"hello\\nworld\""));
		ASSERT_EQ(std::string(json.getString("s")), "hello\nworld");
	}
	// Double-quoted YAML string with \x escape
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"\\x48\\x65\\x6c\\x6c\\x6f\""));
		ASSERT_EQ(std::string(json.getString("s")), "Hello");
	}
	// Double-quoted YAML string with \u escape
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"\\u0048\\u0065\\u006c\\u006c\\u006f\""));
		ASSERT_EQ(std::string(json.getString("s")), "Hello");
	}
	// Double-quoted YAML string: short \x (one hex digit) -- must not read out of bounds
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"\\x4\""));
		ASSERT_EQ(std::string(json.getString("s")), "4");
	}
	// Double-quoted YAML string: \x with zero hex digits -- escape dropped
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"\\x\""));
		ASSERT_EQ(std::string(json.getString("s")), "");
	}
	// Double-quoted YAML string: valid \x41 -> 'A'
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"\\x41\""));
		ASSERT_EQ(std::string(json.getString("s")), "A");
	}
	// Double-quoted YAML string: short \u (three hex digits) -- must not read out of bounds
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"\\u004\""));
		ASSERT_EQ(std::string(json.getString("s")), "004");
	}
	// Explicit tag !!str -- forces number to string
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: !!str 123"));
		ASSERT_EQ(std::string(json.getString("s")), "123");
	}
	// Explicit tag !!str -- plain text
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: !!str hello"));
		ASSERT_EQ(std::string(json.getString("s")), "hello");
	}
	// Explicit tag !!str -- no value
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: !!str"));
		ASSERT_EQ(std::string(json.getString("s")), "");
	}
	// Explicit tag !!int -- quoted string forced to int
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!int \"42\""));
		ASSERT_EQ(json.getInt("n"), 42);
	}
	// Explicit tag !!int -- plain number
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!int 42"));
		ASSERT_EQ(json.getInt("n"), 42);
	}
	// Explicit tag !!float -- integer forced to float
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!float 3"));
		ASSERT_EQ(json.getDouble("n"), 3.0);
	}
	// Explicit tag !!float -- float value
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!float 3.14"));
		double v = json.getDouble("n");
		ASSERT(v > 3.13 && v < 3.15);
	}
	// Explicit tag !!bool -- yes/true/on -> true
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: !!bool yes\nb: !!bool true\nc: !!bool on"));
		ASSERT_EQ(json.getBool("a"), true);
		ASSERT_EQ(json.getBool("b"), true);
		ASSERT_EQ(json.getBool("c"), true);
	}
	// Explicit tag !!bool -- no/false/off -> false
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: !!bool no\nb: !!bool false\nc: !!bool off"));
		ASSERT_EQ(json.getBool("a"), false);
		ASSERT_EQ(json.getBool("b"), false);
		ASSERT_EQ(json.getBool("c"), false);
	}
	// Explicit tag !!null
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!null"));
		ASSERT(json.isNull("n"));
	}
	// Explicit tag !!null with value (ignored)
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!null \"ignored\""));
		ASSERT(json.isNull("n"));
	}
	// Explicit tag !!timestamp -- value passed as string
	{
		asvJSON json;
		ASSERT(json.fromYAML("dt: !!timestamp 2024-01-15T10:30:00Z"));
		ASSERT_EQ(std::string(json.getString("dt")), "2024-01-15T10:30:00Z");
	}
	// Explicit tag !!int with invalid quoted value -> parse fails
	{
		asvJSON json;
		ASSERT(!json.fromYAML("n: !!int \"not-a-number\""));
	}
	// Explicit tag !!float with invalid quoted value -> parse fails
	{
		asvJSON json;
		ASSERT(!json.fromYAML("n: !!float \"not-a-number\""));
	}
	// Explicit tag !!value -- scalar keeps its default interpretation
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!value 42"));
		ASSERT_EQ(json.getInt("x"), 42);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!value true"));
		ASSERT_EQ(json.getBool("x"), true);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!value \"abc\""));
		ASSERT_EQ(std::string(json.getString("x")), "abc");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!value hello"));
		ASSERT_EQ(std::string(json.getString("x")), "hello");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!value"));
		ASSERT(json.isNull("x"));
	}
	// Explicit tag !!yaml -- scalar embeds a nested YAML document
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"a: 1\\nb: 2\""));
		if (json.lastError.empty()) {
			asvJSONValue* x = json.getRoot()->get("x");
			ASSERT(x != nullptr && x->type == asvJSONValue::OBJECT);
			ASSERT_EQ(json.getInt("x.a"), 1);
			ASSERT_EQ(json.getInt("x.b"), 2);
		}
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"- 1\\n- 2\""));
		if (json.lastError.empty()) {
			asvJSONValue* x = json.getRoot()->get("x");
			ASSERT(x != nullptr && x->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*x->arr).size(), 2u);
		}
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"{b: 2}\""));
		if (json.lastError.empty()) {
			asvJSONValue* x = json.getRoot()->get("x");
			ASSERT(x != nullptr && x->type == asvJSONValue::OBJECT);
			ASSERT_EQ(json.getInt("x.b"), 2);
		}
	}
	// !!yaml nests recursively
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: !!yaml \"b: !!yaml \\\"c: 1\\\"\""));
		if (json.lastError.empty()) {
			ASSERT_EQ(json.getInt("a.b.c"), 1);
		}
	}
	// !!yaml with no value -> null
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml"));
		ASSERT(json.isNull("x"));
	}
	// !!yaml inside a flow array -- still parsed as a nested document
	{
		asvJSON json;
		ASSERT(json.fromYAML("[1, !!yaml \"a: 1\"]"));
		if (json.lastError.empty()) {
			asvJSONValue* arr = json.getRoot();
			ASSERT(arr->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*arr->arr).size(), 2u);
			asvJSONValue* inner = (*arr->arr)[1].get();
			ASSERT(inner != nullptr && inner->type == asvJSONValue::OBJECT);
			ASSERT(inner->get("a") != nullptr && inner->get("a")->type == asvJSONValue::INT);
		} else {
			ASSERT(false);
		}
	}
	// !!yaml embedding a top-level scalar is reinterpreted as a value
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"42\""));
		ASSERT_EQ(json.getInt("x"), 42);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml hello"));
		ASSERT_EQ(std::string(json.getString("x")), "hello");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml true"));
		ASSERT_EQ(json.getBool("x"), true);
	}
	// "a:b" (no space after ':') is a plain scalar, not a mapping
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"a:b\""));
		ASSERT_EQ(std::string(json.getString("x")), "a:b");
	}
	// Malformed embedded structure is still an error, never a silent string
	{
		asvJSON json;
		ASSERT(!json.fromYAML("x: !!yaml \"{broken\""));
	}
	// Multi-line scalar embeds are folded per YAML plain-scalar rules
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"hello\\nworld\""));
		ASSERT_EQ(std::string(json.getString("x")), "hello world");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"hello\\n\\nworld\""));
		ASSERT_EQ(std::string(json.getString("x")), "hello\nworld");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml \"line1\\n  line2\\nline3\""));
		ASSERT_EQ(std::string(json.getString("x")), "line1 line2 line3");
	}
	// Block scalar after the !!.yaml tag on the same line
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml |\n  a: 1\n  b: 2"));
		if (json.lastError.empty()) {
			asvJSONValue* x = json.getRoot()->get("x");
			ASSERT(x != nullptr && x->type == asvJSONValue::OBJECT);
			ASSERT_EQ(json.getInt("x.b"), 2);
		} else {
			ASSERT(false);
		}
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml |\n  hello\n  world"));
		ASSERT_EQ(std::string(json.getString("x")), "hello world");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml |\n  - a\n  - b"));
		if (json.lastError.empty()) {
			asvJSONValue* x = json.getRoot()->get("x");
			ASSERT(x != nullptr && x->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*x->arr).size(), 2u);
		} else {
			ASSERT(false);
		}
	}
	// Tag alone; the embedded document is the deeper block collection
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml\n  a: 1\n  b: 2\nc: 3"));
		if (json.lastError.empty()) {
			ASSERT_EQ(json.getInt("x.b"), 2);
			ASSERT_EQ(json.getInt("c"), 3);
		} else {
			ASSERT(false);
		}
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!yaml\n  |\n  - a\n  - b"));
		if (json.lastError.empty()) {
			asvJSONValue* x = json.getRoot()->get("x");
			ASSERT(x != nullptr && x->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*x->arr).size(), 2u);
		} else {
			ASSERT(false);
		}
	}
	// !!yaml embed as a sequence item (bare tag and block scalar)
	{
		asvJSON json;
		ASSERT(json.fromYAML("- !!yaml\n  a: 1\n- z"));
		if (json.lastError.empty()) {
			asvJSONValue* arr = json.getRoot();
			ASSERT(arr->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*arr->arr).size(), 2u);
			asvJSONValue* n0 = (*arr->arr)[0].get();
			if (n0) {
				asvJSONValue* a = n0->get("a");
				ASSERT(a != nullptr && a->type == asvJSONValue::INT && a->getInt() == 1);
			} else {
				ASSERT(false);
			}
		} else {
			ASSERT(false);
		}
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("- !!yaml |\n  - a\n  - b\n- z"));
		if (json.lastError.empty()) {
			asvJSONValue* arr = json.getRoot();
			ASSERT(arr->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*arr->arr).size(), 2u);
			asvJSONValue* inner = (*arr->arr)[0].get();
			ASSERT(inner->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*inner->arr).size(), 2u);
		} else {
			ASSERT(false);
		}
	}
	// Three-! tag (!!!int) is a local tag ("!!int") -- preserved as CUSTOM_TAG
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!!int 42"));
		auto* n = json.getRoot()->get("n");
		ASSERT(n != nullptr && n->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(n->str_data), "!!int");
		if (n->custom_value) {
			ASSERT(n->custom_value->type == asvJSONValue::INT);
			ASSERT_EQ(n->custom_value->getInt(), 42);
		} else {
			ASSERT(false);
		}
	}
	// Explicit tags in array items
	{
		asvJSON json;
		ASSERT(json.fromYAML("- !!str 42\n- !!int \"99\"\n- !!bool yes"));
		auto* arr = json.getRoot();
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(3));
		ASSERT_EQ(std::string(arr->arr->at(0)->getString()), "42");
		ASSERT_EQ(arr->arr->at(1)->getInt(), 99);
		ASSERT_EQ(arr->arr->at(2)->getBool(), true);
	}
	// %YAML directive -- silently skipped
	{
		asvJSON json;
		ASSERT(json.fromYAML("%YAML 1.2\nkey: val"));
		ASSERT_EQ(std::string(json.getString("key")), "val");
	}
	// %TAG !handle! prefix -- custom tag resolves the full name via the handle and
	// is preserved as a CUSTOM_TAG
	{
		asvJSON json;
		ASSERT(json.fromYAML("%TAG !x! tag:example.com,2000:app/\nx: !x!foo hello"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "tag:example.com,2000:app/foo");
		if (x->custom_value) {
			ASSERT(x->custom_value->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(x->custom_value->str_data), "hello");
		} else {
			ASSERT(false);
		}
	}
	// %TAG !! override -- !!str resolves to a local tag, not the core schema
	{
		asvJSON json;
		ASSERT(json.fromYAML("%TAG !! tag:example.com,2000:app/\nx: !!str 42"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "tag:example.com,2000:app/str");
		if (x->custom_value) {
			ASSERT(x->custom_value->type == asvJSONValue::INT);
			ASSERT_EQ(x->custom_value->getInt(), 42);
		} else {
			ASSERT(false);
		}
	}
	// Directives before ---
	{
		asvJSON json;
		ASSERT(json.fromYAML("%TAG !x! tag:example.com,2000:app/\n---\nx: !x!foo hello"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "tag:example.com,2000:app/foo");
		if (x->custom_value) {
			ASSERT(x->custom_value->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(x->custom_value->str_data), "hello");
		} else {
			ASSERT(false);
		}
	}
	// Directives after ---
	{
		asvJSON json;
		ASSERT(json.fromYAML("---\n%TAG !x! tag:example.com,2000:app/\nx: !x!foo hello"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "tag:example.com,2000:app/foo");
		if (x->custom_value) {
			ASSERT(x->custom_value->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(x->custom_value->str_data), "hello");
		} else {
			ASSERT(false);
		}
	}
	// Multiple directives
	{
		asvJSON json;
		ASSERT(json.fromYAML("%YAML 1.2\n%TAG !x! tag:example.com,2000:app/\nkey: !x!test val"));
		auto* k = json.getRoot()->get("key");
		ASSERT(k != nullptr && k->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(k->str_data), "tag:example.com,2000:app/test");
		if (k->custom_value) {
			ASSERT(k->custom_value->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(k->custom_value->str_data), "val");
		} else {
			ASSERT(false);
		}
	}
	// Unknown tags are preserved as CUSTOM_TAG (name + value)
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: !schema \"urn:example:person\"\nx: !mytag 42\ny: !custom [1, 2]"));
		auto* s = json.getRoot()->get("s");
		ASSERT(s != nullptr && s->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(s->str_data), "schema");
		if (s->custom_value) {
			ASSERT(s->custom_value->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(s->custom_value->str_data), "urn:example:person");
		} else {
			ASSERT(false);
		}
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "mytag");
		if (x->custom_value) {
			ASSERT(x->custom_value->type == asvJSONValue::INT);
			ASSERT_EQ(x->custom_value->getInt(), 42);
		} else {
			ASSERT(false);
		}
		auto* y = json.getRoot()->get("y");
		ASSERT(y != nullptr && y->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(y->str_data), "custom");
		if (y->custom_value) {
			ASSERT(y->custom_value->type == asvJSONValue::ARRAY);
			ASSERT_EQ(y->custom_value->arr->size(), size_t(2));
		} else {
			ASSERT(false);
		}
	}
	// Custom tags round-trip through toYAML
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: !schema \"urn:example:person\"\nx: !mytag 42"));
		ASSERT(json.toYAML().find("!schema") != std::string::npos);
		ASSERT(json.toYAML().find("!mytag 42") != std::string::npos);
		asvJSON back;
		ASSERT(back.fromYAML(json.toYAML()));
		auto* s = back.getRoot()->get("s");
		ASSERT(s != nullptr && s->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(s->str_data), "schema");
	}
	// Custom tags round-trip through the JSON $customTag convention
	{
		asvJSON json;
		ASSERT(json.parse("{\"s\":{\"$customTag\":{\"name\":\"schema\",\"value\":\"urn:x\"}}}"));
		auto* s = json.getRoot()->get("s");
		ASSERT(s != nullptr && s->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(s->str_data), "schema");
		if (s->custom_value) {
			ASSERT(s->custom_value->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(s->custom_value->str_data), "urn:x");
		} else {
			ASSERT(false);
		}
	}
	// Bare unknown tag -> CUSTOM_TAG with a null value
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: !mytag"));
		auto* a = json.getRoot()->get("a");
		ASSERT(a != nullptr && a->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(a->str_data), "mytag");
		ASSERT(a->custom_value != nullptr && a->custom_value->type == asvJSONValue::NULL_VAL);
	}
	// Verbatim tag URI keeps the full name
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !<tag:example.com,2024:type> hello"));
		auto* k = json.getRoot()->get("k");
		ASSERT(k != nullptr && k->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(k->str_data), "tag:example.com,2024:type");
	}
	// Custom-tag transform hook: returning a replacement substitutes it for the
	// default CUSTOM_TAG node (YAML path).
	{
		asvJSON json;
		json.customTagTransform = [](const std::string& name, const asvJSONValue& v) -> std::unique_ptr<asvJSONValue> {
			if (name == "point") {
				std::string s = "POINT/" + std::to_string(v.get("x")->getInt()) + "/" + std::to_string(v.get("y")->getInt());
				return asvJSONValue::makeStringView(s);
			}
			return nullptr; // keep default CUSTOM_TAG for other tags
		};
		ASSERT(json.fromYAML("p: !point {x: 1, y: 2}"));
		auto* p = json.getRoot()->get("p");
		ASSERT(p != nullptr && p->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(p->getString()), "POINT/1/2");
	}
	// Custom-tag transform hook: returning nullptr keeps the default CUSTOM_TAG.
	{
		asvJSON json;
		json.customTagTransform = [](const std::string&, const asvJSONValue&) -> std::unique_ptr<asvJSONValue> {
			return nullptr;
		};
		ASSERT(json.fromYAML("p: !point {x: 1}"));
		auto* p = json.getRoot()->get("p");
		ASSERT(p != nullptr && p->type == asvJSONValue::CUSTOM_TAG);
	}
	// Custom-tag transform hook applies to the plain JSON $customTag path too.
	{
		asvJSON json;
		json.customTagTransform = [](const std::string& name, const asvJSONValue& v) -> std::unique_ptr<asvJSONValue> {
			if (name == "uuid") return asvJSONValue::makeStringView(std::string("UUID:").append(v.getString()));
			return nullptr;
		};
		ASSERT(json.parse("{\"id\":{\"$customTag\":{\"name\":\"uuid\",\"value\":\"abc\"}}}"));
		auto* id = json.getRoot()->get("id");
		ASSERT(id != nullptr && id->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(id->getString()), "UUID:abc");
	}
	// Inline flow mapping
	{
		asvJSON json;
		ASSERT(json.fromYAML("obj: {a: 1, b: 2}"));
		auto* o = json.getRoot()->get("obj");
		ASSERT(o != nullptr && o->type == asvJSONValue::OBJECT);
		ASSERT_EQ(o->get("a")->getInt(), 1);
		ASSERT_EQ(o->get("b")->getInt(), 2);
	}
	// Inline flow array
	{
		asvJSON json;
		ASSERT(json.fromYAML("arr: [1, 2, 3]"));
		auto* a = json.getRoot()->get("arr");
		ASSERT(a != nullptr && a->type == asvJSONValue::ARRAY);
		ASSERT_EQ(a->arr->size(), size_t(3));
		ASSERT_EQ(a->arr->at(0)->getInt(), 1);
		ASSERT_EQ(a->arr->at(2)->getInt(), 3);
	}
	// Empty flow collections
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: {}\nb: []"));
		ASSERT(json.getRoot()->get("a")->type == asvJSONValue::OBJECT);
		ASSERT(json.getRoot()->get("b")->type == asvJSONValue::ARRAY);
	}
	// Nested flow collections
	{
		asvJSON json;
		ASSERT(json.fromYAML("obj: {a: {b: 3}}"));
		auto* a = json.getRoot()->get("obj")->get("a");
		ASSERT(a != nullptr && a->type == asvJSONValue::OBJECT);
		ASSERT_EQ(a->get("b")->getInt(), 3);
	}
	// Multi-line flow mapping
	{
		asvJSON json;
		ASSERT(json.fromYAML("obj:\n  {a: 1,\n   b: 2}"));
		auto* o = json.getRoot()->get("obj");
		ASSERT(o != nullptr && o->type == asvJSONValue::OBJECT);
		ASSERT_EQ(o->get("a")->getInt(), 1);
		ASSERT_EQ(o->get("b")->getInt(), 2);
	}
	// Flow array of objects
	{
		asvJSON json;
		ASSERT(json.fromYAML("items: [{x: 1}, {x: 2}]"));
		auto* arr = json.getRoot()->get("items");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(2));
		ASSERT_EQ(arr->arr->at(0)->get("x")->getInt(), 1);
	}
	// Flow with comments
	{
		asvJSON json;
		ASSERT(json.fromYAML("obj: {a: 1 #comment\n, b: 2}"));
		auto* o = json.getRoot()->get("obj");
		ASSERT(o != nullptr && o->type == asvJSONValue::OBJECT);
		ASSERT_EQ(o->get("a")->getInt(), 1);
		ASSERT_EQ(o->get("b")->getInt(), 2);
	}
	// Flow with quoted keys and strings
	{
		asvJSON json;
		ASSERT(json.fromYAML("obj: {\"a\": \"hello\", 'b': world}"));
		auto* o = json.getRoot()->get("obj");
		ASSERT(o != nullptr && o->type == asvJSONValue::OBJECT);
		ASSERT_EQ(std::string(o->get("a")->getString()), "hello");
		ASSERT_EQ(std::string(o->get("b")->getString()), "world");
	}
	// Flow with tags
	{
		asvJSON json;
		ASSERT(json.fromYAML("items: [!!str 42, !!int \"99\"]"));
		auto* arr = json.getRoot()->get("items");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(2));
		ASSERT_EQ(std::string(arr->arr->at(0)->getString()), "42");
		ASSERT_EQ(arr->arr->at(1)->getInt(), 99);
	}
	// Anchor/alias inside flow sequence: inline values
	{
		asvJSON json;
		ASSERT(json.fromYAML("default: &ref 42\nitems: [1, *ref, 3]"));
		auto* arr = json.getRoot()->get("items");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(3));
		ASSERT_EQ(arr->arr->at(0)->getInt(), 1);
		ASSERT_EQ(arr->arr->at(1)->getInt(), 42);
		ASSERT_EQ(arr->arr->at(2)->getInt(), 3);
	}
	// Anchor/alias inside flow map value
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: &val hello\ny: {a: 1, b: *val}"));
		auto* inner = json.getRoot()->get("y");
		ASSERT(inner != nullptr && inner->type == asvJSONValue::OBJECT);
		ASSERT_EQ(inner->get("a")->getInt(), 1);
		ASSERT_EQ(std::string(inner->get("b")->getString()), "hello");
	}
	// Simple inline anchor inside flow (block-level anchor, flow alias)
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: &ref 42\ny: [1, *ref, 3]"));
		auto* arr = json.getRoot()->get("y");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(3));
		ASSERT_EQ(arr->arr->at(1)->getInt(), 42);
	}
	// Define anchor inside flow, reference in another flow
	{
		asvJSON json;
		ASSERT(json.fromYAML("data: {first: &anchor 99, second: *anchor}"));
		auto* obj = json.getRoot()->get("data");
		ASSERT(obj != nullptr && obj->type == asvJSONValue::OBJECT);
		ASSERT_EQ(obj->get("first")->getInt(), 99);
		ASSERT_EQ(obj->get("second")->getInt(), 99);
	}
	// Define anchor inside flow sequence, reference after
	{
		asvJSON json;
		ASSERT(json.fromYAML("items: [&a 10, 20, *a]\nother: *a"));
		auto* arr = json.getRoot()->get("items");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(3));
		ASSERT_EQ(arr->arr->at(0)->getInt(), 10);
		ASSERT_EQ(arr->arr->at(1)->getInt(), 20);
		ASSERT_EQ(arr->arr->at(2)->getInt(), 10);
		ASSERT_EQ(json.getInt("other"), 10);
	}
	// Block anchor: object value defined on its own lines, aliased below
	{
		asvJSON json;
		ASSERT(json.fromYAML("defaults: &base\n  host: h\n  port: 1\nu: *base\nv: *base"));
		auto* def = json.getRoot()->get("defaults");
		ASSERT(def != nullptr && def->type == asvJSONValue::OBJECT);
		ASSERT_EQ(def->get("host")->getString(), std::string("h"));
		ASSERT_EQ(def->get("port")->getInt(), 1);
		auto* u = json.getRoot()->get("u");
		ASSERT(u != nullptr && u->type == asvJSONValue::OBJECT);
		ASSERT_EQ(u->get("port")->getInt(), 1);
		ASSERT_EQ(json.getRoot()->get("v")->get("host")->getString(), std::string("h"));
	}
	// Block anchor with sequence value and a forward reference
	{
		asvJSON json;
		ASSERT(json.fromYAML("first: *back\nback: &back\n  - 1\n  - 2"));
		auto* arr = json.getRoot()->get("first");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(2));
		ASSERT_EQ(arr->arr->at(0)->getInt(), 1);
		ASSERT_EQ(arr->arr->at(1)->getInt(), 2);
	}
	// Scalar anchor: comment after the value must not leak into the alias
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: &x 5\nu: *x\n"));
		ASSERT_EQ(json.getInt("u"), 5);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: &x 5 # trailing\nu: *x\n"));
		ASSERT_EQ(json.getInt("u"), 5);
	}
	// Multiple documents separated by "---" -> array of documents
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n---\nb: 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		ASSERT_EQ(root->arr->at(0)->get("a")->getInt(), int64_t(1));
		ASSERT_EQ(root->arr->at(1)->get("b")->getInt(), int64_t(2));
	}
	// Single implicit document has no array wrapper
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n"));
		ASSERT(json.getRoot() != nullptr && json.getRoot()->type == asvJSONValue::OBJECT);
	}
	// "..." ends the current document; trailing content starts a new implicit one
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n...\nb: 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		ASSERT_EQ(root->arr->at(0)->get("a")->getInt(), int64_t(1));
		ASSERT_EQ(root->arr->at(1)->get("b")->getInt(), int64_t(2));
	}
	// "..." followed by whitespace or a comment is still a boundary marker
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n... \nb: 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n... # done\nb: 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
	}
	// Trailing "---" with no content -> empty explicit document (null)
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n---\n"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		ASSERT(root->arr->at(0)->type == asvJSONValue::OBJECT);
		ASSERT(root->arr->at(1)->type == asvJSONValue::NULL_VAL);
	}
	// Empty documents between "---" separators become null elements
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n---\n---\nb: 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(3));
		ASSERT(root->arr->at(0)->type == asvJSONValue::OBJECT);
		ASSERT(root->arr->at(1)->type == asvJSONValue::NULL_VAL);
		ASSERT(root->arr->at(2)->type == asvJSONValue::OBJECT);
	}
	// "---" followed by "..." yields a single null document
	{
		asvJSON json;
		ASSERT(json.fromYAML("---\n...\n"));
		ASSERT(json.getRoot() != nullptr && json.getRoot()->type == asvJSONValue::NULL_VAL);
	}
	// A leading "---" opens the (single) document, never an empty first one
	{
		asvJSON json;
		ASSERT(json.fromYAML("---\na: 1"));
		ASSERT(json.getRoot() != nullptr && json.getRoot()->type == asvJSONValue::OBJECT);
		ASSERT_EQ(json.getInt("a"), int64_t(1));
	}
	// %-directives still attach to the document that follows "---" after "..."
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\n...\n%TAG !x! tag:example.com,2000:app/\n---\nx: !x!foo hello"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		auto* x = root->arr->at(1)->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "tag:example.com,2000:app/foo");
		if (x->custom_value) {
			ASSERT(x->custom_value->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(x->custom_value->str_data), "hello");
		} else {
			ASSERT(false);
		}
	}
	// Explicit tags on block collection nodes: !!map at document root
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!map\na: 1\nb: 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::OBJECT);
		ASSERT_EQ(root->get("a")->getInt(), 1);
		ASSERT_EQ(root->get("b")->getInt(), 2);
	}
	// Explicit tags on block collection nodes: !!seq at document root
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!seq\n- 1\n- 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		ASSERT_EQ(root->arr->at(0)->getInt(), 1);
	}
	// !!map as a mapping value with a deeper block collection
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!map\n  a: 1\n  b: 2"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::OBJECT);
		ASSERT_EQ(x->get("a")->getInt(), 1);
		ASSERT_EQ(x->get("b")->getInt(), 2);
	}
	// !!seq as a mapping value with a deeper block collection
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!seq\n  - 1\n  - 2"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::ARRAY);
		ASSERT_EQ(x->arr->size(), size_t(2));
		ASSERT_EQ(x->arr->at(1)->getInt(), 2);
	}
	// !!seq with dict items that survive sequence normalization
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!seq\n  - a: 1\n  - b: 2"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::ARRAY);
		ASSERT_EQ(x->arr->size(), size_t(2));
		ASSERT_EQ(x->arr->at(0)->get("a")->getInt(), 1);
		ASSERT_EQ(x->arr->at(1)->get("b")->getInt(), 2);
	}
	// !!map as a block-sequence item
	{
		asvJSON json;
		ASSERT(json.fromYAML("- !!map\n  a: 1\n  b: 2\n- k: 3"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->arr->size(), size_t(2));
		ASSERT_EQ(root->arr->at(0)->get("b")->getInt(), 2);
		ASSERT_EQ(root->arr->at(1)->get("k")->getInt(), 3);
	}
	// !!binary with a deeper block literal scalar
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!binary\n  |\n    aGVsbG8="));
		auto* b = json.getRoot()->get("x");
		ASSERT(b != nullptr && b->type == asvJSONValue::BINARY);
		ASSERT_EQ(b->bin_data.size(), size_t(5));
		ASSERT_EQ(b->bin_data[0], 'h');
		ASSERT_EQ(b->bin_data[4], 'o');
	}
	// !!binary with a deeper plain scalar
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!binary\n  aGVsbG8="));
		auto* b = json.getRoot()->get("x");
		ASSERT(b != nullptr && b->type == asvJSONValue::BINARY);
		ASSERT_EQ(b->bin_data.size(), size_t(5));
		ASSERT_EQ(b->bin_data[0], 'h');
		ASSERT_EQ(b->bin_data[4], 'o');
	}
	// !!binary with a block-scalar header inlined after the tag
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!binary |\n  aGVsbG8="));
		auto* b = json.getRoot()->get("x");
		ASSERT(b != nullptr && b->type == asvJSONValue::BINARY);
		ASSERT_EQ(b->bin_data.size(), size_t(5));
		ASSERT_EQ(b->bin_data[0], 'h');
	}
	// !!str with a block-scalar header inlined after the tag keeps the text
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!str |\n  hello"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(x->str_data), "hello\n");
	}
	// Custom tag with a deeper block collection -> CUSTOM_TAG with nested value
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !mytag\n  a: 1\n  b: 2"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "mytag");
		ASSERT(x->custom_value != nullptr && x->custom_value->type == asvJSONValue::OBJECT);
		ASSERT_EQ(x->custom_value->get("b")->getInt(), 2);
	}
	// Verbatim custom tag with a deeper block collection
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !<tag:example.com,2024:type>\n  a: 1"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(x->str_data), "tag:example.com,2024:type");
		ASSERT(x->custom_value != nullptr && x->custom_value->type == asvJSONValue::OBJECT);
	}
	// Custom tag at document root annotating the whole node
	{
		asvJSON json;
		ASSERT(json.fromYAML("!type\n- 1\n- 2"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::CUSTOM_TAG);
		ASSERT_EQ(std::string(root->str_data), "type");
		ASSERT(root->custom_value != nullptr && root->custom_value->type == asvJSONValue::ARRAY);
		ASSERT_EQ(root->custom_value->arr->size(), size_t(2));
	}
	// Non-specific "!" tag: default content interpretation
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: ! hello"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(x->str_data), "hello");
	}
	// Non-specific "!" tag with an integer value resolves by content
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: ! 42"));
		ASSERT_EQ(json.getRoot()->get("x")->getInt(), 42);
	}
	// Nested block-tagged collections
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!map\n  y: !!seq\n    - 1\n    - 2"));
		auto* x = json.getRoot()->get("x");
		ASSERT(x != nullptr && x->type == asvJSONValue::OBJECT);
		auto* y = x->get("y");
		ASSERT(y != nullptr && y->type == asvJSONValue::ARRAY);
		ASSERT_EQ(y->arr->size(), size_t(2));
		ASSERT_EQ(y->arr->at(0)->getInt(), 1);
	}
	// A sibling key after a solo-tag value is not swallowed
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: 1\nb: !!map\nc: 3"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::OBJECT);
		ASSERT_EQ(root->get("a")->getInt(), 1);
		ASSERT(root->get("b")->type == asvJSONValue::OBJECT);
		ASSERT_EQ(root->get("c")->getInt(), 3);
	}
	// Solo tag with no deeper child: empty container defaults
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!map"));
		ASSERT(json.fromYAML("!!seq"));
		ASSERT(json.fromYAML("x: !!map"));
		ASSERT(json.fromYAML("x: !!seq"));
		ASSERT(json.getRoot()->get("x")->type == asvJSONValue::ARRAY);
	}
	// Scalar tags on block nodes are representational errors
	{
		asvJSON json;
		ASSERT(!json.fromYAML("x: !!str\n  a: 1"));
	}
	// !!map applied to a non-mapping block child is an error
	{
		asvJSON json;
		ASSERT(!json.fromYAML("x: !!map\n  - 1"));
	}
	// Anchor on a solo-tag line: aliases resolve to the tagged block value
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: &x !!map\n  k: 1\nb: *x"));
		auto* b = json.getRoot()->get("b");
		ASSERT(b != nullptr && b->type == asvJSONValue::OBJECT);
		ASSERT_EQ(b->get("k")->getInt(), 1);
	}
	// Anchor on a solo-tag !!seq line: aliases resolve to the tagged array
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: &x !!seq\n  - 1\n  - 2\nb: *x"));
		auto* b = json.getRoot()->get("b");
		ASSERT(b != nullptr && b->type == asvJSONValue::ARRAY);
		ASSERT_EQ(b->arr->size(), size_t(2));
		ASSERT_EQ(b->arr->at(1)->getInt(), 2);
	}
	// Anchor on a dashed solo-tag item: aliases resolve to the tagged block
	{
		asvJSON json;
		ASSERT(json.fromYAML("- &x !!map\n  k: 1\n- *x"));
		auto* it = json.getRoot()->arr->at(1).get();
		ASSERT(it != nullptr && it->type == asvJSONValue::OBJECT);
		ASSERT_EQ(it->get("k")->getInt(), 1);
	}
	// Core Schema: !!set with flow [a,b,c]
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!set [a, b, c]"));
		auto* obj = json.getRoot()->get("val");
		ASSERT(obj != nullptr && obj->type == asvJSONValue::OBJECT);
		ASSERT(obj->hasKey("a"));
		ASSERT(obj->hasKey("b"));
		ASSERT(obj->hasKey("c"));
		ASSERT(obj->get("a")->type == asvJSONValue::NULL_VAL);
		ASSERT(obj->get("b")->type == asvJSONValue::NULL_VAL);
		ASSERT(obj->get("c")->type == asvJSONValue::NULL_VAL);
	}
	// Core Schema: !!set with flow {a,b,c}
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!set {a, b, c}"));
		auto* obj = json.getRoot()->get("val");
		ASSERT(obj != nullptr && obj->type == asvJSONValue::OBJECT);
		ASSERT(obj->hasKey("a"));
		ASSERT(obj->hasKey("b"));
		ASSERT(obj->hasKey("c"));
		ASSERT(obj->get("a")->type == asvJSONValue::NULL_VAL);
	}
	// Core Schema: !!set with block "- key" items as a mapping value
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!set\n  - a\n  - b"));
		if (json.lastError.empty()) {
			auto* obj = json.getRoot()->get("val");
			ASSERT(obj != nullptr && obj->type == asvJSONValue::OBJECT);
			ASSERT(obj->hasKey("a") && obj->get("a")->type == asvJSONValue::NULL_VAL);
			ASSERT(obj->hasKey("b") && obj->get("b")->type == asvJSONValue::NULL_VAL);
		} else {
			ASSERT(false);
		}
	}
	// Core Schema: !!set block items at document root
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n- a\n- b"));
		if (json.lastError.empty()) {
			auto* obj = json.getRoot();
			ASSERT(obj != nullptr && obj->type == asvJSONValue::OBJECT);
			ASSERT(obj->hasKey("a"));
			ASSERT(obj->hasKey("b"));
		} else {
			ASSERT(false);
		}
	}
	// Core Schema: !!set with explicit "? key" block items (canonical set form)
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n? a\n? b"));
		if (json.lastError.empty()) {
			auto* obj = json.getRoot();
			ASSERT(obj->hasKey("a"));
			ASSERT(obj->hasKey("b"));
		} else {
			ASSERT(false);
		}
	}
	// Core Schema: !!set as a block-sequence item
	{
		asvJSON json;
		ASSERT(json.fromYAML("- !!set\n  - a\n  - b"));
		if (json.lastError.empty()) {
			auto* arr = json.getRootArray();
			ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
			asvJSONValue* obj = (*arr->arr)[0].get();
			ASSERT(obj != nullptr && obj->type == asvJSONValue::OBJECT);
			ASSERT(obj->hasKey("a"));
			ASSERT(obj->hasKey("b"));
		} else {
			ASSERT(false);
		}
	}
	// Empty block set
	{
		asvJSON json;
		ASSERT(json.fromYAML("e: !!set\nx: 1"));
		ASSERT(json.getRoot()->hasKey("e"));
		ASSERT(json.getRoot()->get("e")->type == asvJSONValue::OBJECT);
		ASSERT_EQ(json.getInt("x"), 1);
	}
	// Quoted block-set keys are unescaped; scalar keys stringify
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!set\n  - \"q k\"\n  - 1\n  - true"));
		if (json.lastError.empty()) {
			auto* obj = json.getRoot()->get("val");
			ASSERT(obj->hasKey("q k"));
			ASSERT(obj->hasKey("1"));
			ASSERT(obj->hasKey("true"));
		} else {
			ASSERT(false);
		}
	}
	// Trailing content after a root block set is rejected (not silently dropped)
	{
		asvJSON json;
		ASSERT(!json.fromYAML("!!set\n- a\nx: 1"));
		ASSERT(std::string(json.lastError).find("after block set") != std::string::npos);
	}
	// A block set as a mapping value may be followed by sibling keys
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set\n  - a\n  - b\no: 1"));
		if (json.lastError.empty()) {
			asvJSONValue* obj = json.getRoot()->get("k");
			ASSERT(obj != nullptr && obj->type == asvJSONValue::OBJECT);
			ASSERT(obj->hasKey("a") && obj->hasKey("b"));
			ASSERT_EQ(json.getInt("o"), 1);
		} else {
			ASSERT(false);
		}
	}
	// A block set as a sequence item may be followed by sibling items
	{
		asvJSON json;
		ASSERT(json.fromYAML("- !!set\n  - a\n- b"));
		if (json.lastError.empty()) {
			asvJSONValue* arr = json.getRoot();
			ASSERT(arr->type == asvJSONValue::ARRAY);
			ASSERT_EQ((*arr->arr).size(), 2u);
		} else {
			ASSERT(false);
		}
	}
	// Complex (non-scalar) block-set keys stringify like mapping keys
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set\n  - a\n  - x: 1"));
		ASSERT(json.getRoot()->get("k")->hasKey("a"));
		ASSERT(json.getRoot()->get("k")->hasKey("{\"x\":1}"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n- a\n- b: 1"));
		ASSERT(json.getRoot()->hasKey("a"));
		ASSERT(json.getRoot()->hasKey("{\"b\":1}"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set\n  - a\n  - {x}"));
		ASSERT(json.getRoot()->get("k")->hasKey("a"));
		ASSERT(json.getRoot()->get("k")->hasKey("{\"x\":null}"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set\n  - a\n  - - b"));
		ASSERT(json.getRoot()->get("k")->hasKey("a"));
		ASSERT(json.getRoot()->get("k")->hasKey("[\"b\"]"));
	}
	// Flow-collection set members on the item line
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n- {a, b}\n- [x]"));
		ASSERT(json.getRoot()->hasKey("{\"a\":null,\"b\":null}"));
		ASSERT(json.getRoot()->hasKey("[\"x\"]"));
	}
	// Bare "-" with a multi-key block mapping child
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n-\n  x: 1\n  y: 2"));
		ASSERT(json.getRoot()->hasKey("{\"x\":1,\"y\":2}"));
	}
	// Bare "-" with a block sequence child
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n-\n  - a\n  - b"));
		ASSERT(json.getRoot()->hasKey("[\"a\",\"b\"]"));
	}
	// Bare "-" with a plain scalar child stringifies as a scalar key
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n-\n  hello"));
		ASSERT(json.getRoot()->hasKey("hello"));
	}
	// Bare "-" with a quoted scalar child unescapes the key
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n-\n  \"q k\""));
		ASSERT(json.getRoot()->hasKey("q k"));
	}
	// One-line "? key: value" pair becomes a mapping key
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n? x: 1"));
		ASSERT(json.getRoot()->hasKey("{\"x\":1}"));
	}
	// Nested sets inside bare "-" children recurse correctly
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n-\n  k: !!set\n    - a"));
		ASSERT(json.getRoot()->hasKey("{\"k\":{\"$set\":[\"a\"]}}"));
	}
	// Quoted set keys keep colons (quote-aware: not treated as mapping keys)
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set\n  - \"x: 1\""));
		if (json.lastError.empty())
			ASSERT(json.getRoot()->get("k")->hasKey("x: 1"));
		else
			ASSERT(false);
	}
	// !!set round-trips through JSON as {"$set":[...]}: serialize form,
	// re-parse flags the object as a set again, and the JSON stays stable.
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set\n  - a\n  - b"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"k\":{\"$set\":[\"a\",\"b\"]}}");
		asvJSON json2;
		ASSERT(json2.parse(s.c_str()));
		std::string s2;
		json2.getRoot()->serialize(s2);
		ASSERT(std::string(s2) == s);
		// A JSON-round-tripped set still emits the block !!set form.
		ASSERT(json2.toYAML().find("!!set") != std::string::npos);
	}
	// !!set YAML block round-trip is idempotent (block style).
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set\n  - a\n  - b"));
		std::string y = json.toYAML();
		ASSERT(y.find("!!set") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromYAML(y.c_str()));
		ASSERT(json2.toYAML() == y);
	}
	// !!set as a mapping value in a flow object (tag + flow collection value).
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set {a, b}"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"k\":{\"$set\":[\"a\",\"b\"]}}");
	}
	// !!set flow array form [a, b, c]
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!set [a, b, c]"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"val\":{\"$set\":[\"a\",\"b\",\"c\"]}}");
		// non-empty but no value members -> set marker
		ASSERT(json.getRoot()->get("val")->yaml_set);
	}
	// !!set document root round-trip (block: "!!set\n- a\n- b").
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set\n- a\n- b"));
		std::string y = json.toYAML();
		ASSERT(y.find("!!set") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromYAML(y.c_str()));
		std::string s1, s2;
		json.getRoot()->serialize(s1);
		json2.getRoot()->serialize(s2);
		ASSERT(std::string(s1) == "{\"$set\":[\"a\",\"b\"]}");
		ASSERT(std::string(s2) == s1);
	}
	// !!set as a sequence item round-trips in block style.
	{
		asvJSON json;
		ASSERT(json.fromYAML("- !!set\n  - a\n  - b"));
		std::string y = json.toYAML();
		ASSERT(y.find("!!set") != std::string::npos);
		asvJSON json2;
		ASSERT(json2.fromYAML(y.c_str()));
		std::string s1, s2;
		json.getRoot()->serialize(s1);
		json2.getRoot()->serialize(s2);
		ASSERT(std::string(s1) == "[{\"$set\":[\"a\",\"b\"]}]");
		ASSERT(std::string(s2) == s1);
	}
	// Nested !!set inside another mapping round-trips.
	{
		asvJSON json;
		ASSERT(json.fromYAML("a:\n  b: !!set\n    - x\n    - y"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"a\":{\"b\":{\"$set\":[\"x\",\"y\"]}}}");
	}
	// Empty !!set round-trips as {"$set":[]}.
	{
		asvJSON json;
		ASSERT(json.fromYAML("k: !!set []"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"k\":{\"$set\":[]}}");
		asvJSON json2;
		ASSERT(json2.parse(s.c_str()));
		std::string s2;
		json2.getRoot()->serialize(s2);
		ASSERT(std::string(s2) == s);
	}
	// A plain object with a "$set" key holding non-strings stays a plain
	// object (MongoDB's update operator {"$set":[1,2]} is not a YAML set).
	{
		asvJSON json;
		ASSERT(json.parse("{\"$set\":[1,2]}"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"$set\":[1,2]}");
		ASSERT(!json.getRoot()->yaml_set);
	}
	// A !!set carrying a non-null member value is NOT marked as a set.
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!set {a: 1}"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"val\":{\"a\":1}}");
		ASSERT(!json.getRoot()->get("val")->yaml_set);
	}
	// operator== distinguishes a set from an equivalent plain object.
	{
		asvJSON a;
		ASSERT(a.fromYAML("!!set\n- a"));
		asvJSON b;
		ASSERT(b.parse("{\"a\":null}"));
		ASSERT(*(a.getRoot()) != *(b.getRoot()));
		asvJSON c;
		ASSERT(c.fromYAML("!!set\n- a"));
		ASSERT(*(a.getRoot()) == *(c.getRoot()));
	}
	// Root-level tagged flow values ("!!set {a, b}", "!!seq [1, 2]",
	// "!!map {a:1}") parse as the tagged value and round-trip.
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!set {a, b}"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"$set\":[\"a\",\"b\"]}");
		asvJSON json2;
		ASSERT(json2.parse(s.c_str()));
		std::string s2;
		json2.getRoot()->serialize(s2);
		ASSERT(std::string(s2) == s);
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!seq [1, 2]"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "[1,2]");
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("!!map {a: 1}"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"a\":1}");
		// !!map with a non-mapping flow value is a representational error.
		ASSERT(!json.fromYAML("!!map [1, 2]"));
		// !!seq with a non-sequence flow value is a representational error.
		ASSERT(!json.fromYAML("!!seq {a: 1}"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("!cust {a: 1}"));
		auto* root = json.getRoot();
		ASSERT(root != nullptr && root->type == asvJSONValue::CUSTOM_TAG);
	}
	// Root-level tagged flow value on a mapping value ("v: !!map {a: 1}")
	// also parses the flow into proper JSON (unquoted keys) and round-trips.
	{
		asvJSON json;
		ASSERT(json.fromYAML("v: !!map {a: 1}"));
		std::string s;
		json.getRoot()->serialize(s);
		ASSERT(std::string(s) == "{\"v\":{\"a\":1}}");
	}
	// Core Schema: !!omap [{a:1},{b:2}]
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!omap [{a: 1}, {b: 2}]"));
		auto* arr = json.getRoot()->get("val");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(2));
		ASSERT_EQ(arr->arr->at(0)->get("a")->getInt(), 1);
		ASSERT_EQ(arr->arr->at(1)->get("b")->getInt(), 2);
	}
	// Core Schema: !!pairs [[a,1],[b,2]]
	{
		asvJSON json;
		ASSERT(json.fromYAML("val: !!pairs [[a, 1], [b, 2]]"));
		auto* arr = json.getRoot()->get("val");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(2));
		ASSERT_EQ(arr->arr->at(0)->arr->size(), size_t(2));
		ASSERT_EQ(std::string(arr->arr->at(0)->arr->at(0)->getString()), "a");
		ASSERT_EQ(arr->arr->at(0)->arr->at(1)->getInt(), 1);
	}
	// Core Schema: !!int with 0x hex
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!int 0x1A"));
		ASSERT_EQ(json.getInt("x"), 26);
	}
	// Core Schema: !!int with 0o octal
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!int 0o755"));
		ASSERT_EQ(json.getInt("x"), 493);
	}
	// Core Schema: !!int with 0b binary
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!int 0b1101"));
		ASSERT_EQ(json.getInt("x"), 13);
	}
	// Core Schema: !!float .inf
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!float .inf"));
		double dv = json.getRoot()->get("x")->getDouble();
		ASSERT(std::isinf(dv) && dv > 0);
	}
	// Core Schema: !!float -.inf
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!float -.inf"));
		double dv = json.getRoot()->get("x")->getDouble();
		ASSERT(std::isinf(dv) && dv < 0);
	}
	// Core Schema: !!float .nan
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!float .nan"));
		double dv = json.getRoot()->get("x")->getDouble();
		ASSERT(std::isnan(dv));
	}
	// Core Schema: !!bool yes/on recognized as true
	{
		asvJSON json;
		ASSERT(json.fromYAML("a: !!bool yes\nb: !!bool on"));
		ASSERT(json.getBool("a"));
		ASSERT(json.getBool("b"));
	}
	// Core Schema: !!null with ~
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: !!null ~"));
		ASSERT(json.isNull("x"));
	}
	// Tag validation: invalid !!int should fail and report line number
	{
		asvJSON json;
		ASSERT(!json.fromYAML("bad: !!int not-a-number"));
		ASSERT(std::string(json.lastError).find("line") != std::string::npos);
	}
	// Tag validation: invalid !!float should fail and report line number
	{
		asvJSON json;
		ASSERT(!json.fromYAML("bad: !!float not-a-number"));
		ASSERT(std::string(json.lastError).find("line") != std::string::npos);
	}
	// Tab used as indentation is a structural error (matches UDE behavior)
	{
		asvJSON json;
		ASSERT(!json.fromYAML("a:\n\t- 1\n\t- 2"));
		ASSERT(std::string(json.lastError).find("tab") != std::string::npos);
	}
	// Tab inside a block scalar body is also rejected
	{
		asvJSON json;
		ASSERT(!json.fromYAML("text: |\n\t  hello"));
		ASSERT(std::string(json.lastError).find("tab") != std::string::npos);
	}
	// Block scalar inside array item (- |)
	{
		asvJSON json;
		ASSERT(json.fromYAML("- |\n  hello\n  world\n- second"));
		auto* arr = json.getRoot();
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->size(), size_t(2));
		ASSERT_EQ(std::string(arr->get(static_cast<size_t>(0))->getString()), "hello\nworld\n");
		ASSERT_EQ(std::string(arr->get(static_cast<size_t>(1))->getString()), "second");
	}
	// Folded block scalar inside array item (- >)
	{
		asvJSON json;
		ASSERT(json.fromYAML("- >\n  hello\n  world\n- second"));
		auto* arr = json.getRoot();
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->size(), size_t(2));
		ASSERT_EQ(std::string(arr->get(static_cast<size_t>(0))->getString()), "hello world\n");
	}
	// Double-quoted string with escaped hash (#) should not be stripped as comment
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: \"hello\\\"#world\""));
		ASSERT_EQ(std::string(json.getString("s")), "hello\"#world");
	}
	// Single-quoted string with escaped single quote (')') and hash
	{
		asvJSON json;
		ASSERT(json.fromYAML("s: 'it''s #fine'"));
		ASSERT_EQ(std::string(json.getString("s")), "it's #fine");
	}
	// Anchor recursion guard: deeply nested anchors should not crash
	// (builds anchors that recursively call yamlParseDoc; depth > 64 throws)
	{
		asvJSON json;
		// This should parse fine - no actual recursion cycle
		ASSERT(json.fromYAML("a: &ref1 42\nb: *ref1"));
		ASSERT_EQ(json.getInt("a"), 42);
		ASSERT_EQ(json.getInt("b"), 42);
	}
	// Flow mapping at root level: {a: 1, b: 2}
	{
		asvJSON json;
		ASSERT(json.fromYAML("{a: 1, b: 2}"));
		ASSERT(json.getRoot()->type == asvJSONValue::OBJECT);
		ASSERT_EQ(json.getInt("a"), 1);
		ASSERT_EQ(json.getInt("b"), 2);
	}
	// Flow sequence at root level: [1, 2, 3]
	{
		asvJSON json;
		ASSERT(json.fromYAML("[1, 2, 3]"));
		auto* arr = json.getRoot();
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->size(), size_t(3));
		ASSERT_EQ(arr->get(static_cast<size_t>(0))->getInt(), 1);
		ASSERT_EQ(arr->get(static_cast<size_t>(2))->getInt(), 3);
	}
	// Flow mapping with nested values
	{
		asvJSON json;
		ASSERT(json.fromYAML("{name: test, tags: [a, b]}"));
		ASSERT_EQ(std::string(json.getString("name")), "test");
		auto* tags = json.getRoot()->get("tags");
		ASSERT(tags != nullptr && tags->type == asvJSONValue::ARRAY);
		ASSERT_EQ(tags->size(), size_t(2));
	}
	// Multi-line flow mapping at root
	{
		asvJSON json;
		ASSERT(json.fromYAML("{a: 1,\n b: 2}"));
		ASSERT_EQ(json.getInt("a"), 1);
		ASSERT_EQ(json.getInt("b"), 2);
	}
}

TEST(testYamlExtras) {
	// '#' in plain scalar not preceded by whitespace must be kept as data
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: a#b"));
		ASSERT_EQ(std::string(json.getString("x")), std::string("a#b"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("- a#b"));
		ASSERT_EQ(json.getRoot()->type, asvJSONValue::ARRAY);
		ASSERT_EQ(json.getRoot()->arr->size(), size_t(1));
		ASSERT_EQ(std::string(json.getRoot()->arr->at(0)->getString()), std::string("a#b"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: http://x.org/p#frag"));
		ASSERT_EQ(std::string(json.getString("x")), std::string("http://x.org/p#frag"));
	}
	// '#' followed by whitespace is still a comment
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: ab #comment"));
		ASSERT_EQ(std::string(json.getString("x")), std::string("ab"));
	}
	// Leading blank lines in literal/folded block scalars are content
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: |\n\n  hello"));
		ASSERT_EQ(std::string(json.getString("x")), std::string("\nhello\n"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: >\n\n  hello"));
		ASSERT_EQ(std::string(json.getString("x")), std::string("\nhello\n"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: |\n\n\n  hello"));
		ASSERT_EQ(std::string(json.getString("x")), std::string("\n\nhello\n"));
	}
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: >\n\n\n  hello"));
		ASSERT_EQ(std::string(json.getString("x")), std::string("\n\nhello\n"));
	}
	// Encoder must not emit '#' content lines in block scalars (would be dropped)
	{
		asvJSON json;
		ASSERT(json.parse(std::string_view(
			"{\"s\":\"# line\\nmore\"}")));
		std::string y = json.toYAML();
		// The string must be quoted, not a block scalar (block would lose the # line)
		ASSERT(y.find('|') == std::string::npos
		       || y.find('\'') != std::string::npos
		       || y.find('"') != std::string::npos);
		asvJSON back;
		ASSERT(back.fromYAML(y));
		ASSERT_EQ(std::string(back.getString("s")), std::string("# line\nmore"));
	}
	// Flow context: [a#b] is a valid scalar "a#b"
	{
		asvJSON json;
		ASSERT(json.fromYAML("x: [a#b]"));
		auto* arr = json.getRoot()->get("x");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->size(), size_t(1));
		ASSERT_EQ(std::string(arr->arr->at(0)->getString()), std::string("a#b"));
	}
	// Plain scalar key containing '#' (not preceded by space)
	{
		asvJSON json;
		ASSERT(json.fromYAML("key#frag: val"));
		ASSERT_EQ(std::string(json.getString("key#frag")), std::string("val"));
	}
}

TEST(testToTOON) {
	// Basic object round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"name":"John","age":30,"active":true})")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(std::string(j2.getString("name")), "John");
		ASSERT_EQ(j2.getInt("age"), 30);
		ASSERT_EQ(j2.getBool("active"), true);
	}
	// Array round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("[10,20,30]")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(3));
		ASSERT_EQ(j2.getRoot()->get(0)->getInt(), int64_t(10));
	}
	// Nested object round-trip
	{
		asvJSON j;
		std::string src = R"({"name":"John","address":{"city":"NYC"}})";
		ASSERT(j.parse(std::string_view(src)));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(std::string(j2.getString("name")), "John");
		ASSERT_EQ(std::string(j2.getString("address.city")), "NYC");
	}
	// Null, bool, int
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"s":"hi","b":false,"n":null,"i":42})")));
		std::string toon = j.toTOON();
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(std::string(j2.getString("s")), "hi");
		ASSERT_EQ(j2.getBool("b"), false);
		ASSERT(j2.isNull("n"));
		ASSERT_EQ(j2.getInt("i"), int64_t(42));
	}
	// Empty object
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		std::string toon = j.toTOON();
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(0));
	}
	// Array of objects (tabular)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"x":1,"y":2},{"x":3,"y":4}])")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(2));
	}
}

TEST(testToTOONAdvanced) {
	// Named array with nested arrays round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"matrix":[[1,2],[3,4]]})")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(1));
		auto* matrix = j2.getRoot()->getConst("matrix");
		ASSERT(matrix != nullptr);
		ASSERT_EQ(matrix->size(), size_t(2));
		ASSERT_EQ(matrix->get(0)->size(), size_t(2));
		ASSERT_EQ(matrix->get(0)->get(0)->getInt(), int64_t(1));
	}
	// Named array with nested objects
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[{"x":1},{"y":2}]})")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(1));
		auto* items = j2.getRoot()->getConst("items");
		ASSERT(items != nullptr);
		ASSERT_EQ(items->size(), size_t(2));
	}
	// Named array with deeply nested arrays
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"data":[[1,[2,3]],[4,5]]})")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(1));
		auto* data = j2.getRoot()->getConst("data");
		ASSERT(data != nullptr);
		ASSERT_EQ(data->size(), size_t(2));
		ASSERT_EQ(data->get(0)->size(), size_t(2));
		ASSERT_EQ(data->get(0)->get(1)->size(), size_t(2));
	}
	// Named array with [] and list items
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[10,20,30]})")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(1));
		auto* items = j2.getRoot()->getConst("items");
		ASSERT(items != nullptr);
		ASSERT_EQ(items->size(), size_t(3));
		ASSERT_EQ(items->get(0)->getInt(), int64_t(10));
	}
	// Empty named array
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"empty":[]})")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(1));
		auto* empty = j2.getRoot()->getConst("empty");
		ASSERT(empty != nullptr);
		ASSERT_EQ(empty->size(), size_t(0));
	}
	// Empty named object
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"empty":{}})")));
		std::string toon = j.toTOON();
		ASSERT(!toon.empty());
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(1));
		auto* empty = j2.getRoot()->getConst("empty");
		ASSERT(empty != nullptr);
		ASSERT_EQ(empty->size(), size_t(0));
	}
	// Root array of nested arrays round-trip (structural preservation)
	// Spec (toon-format/spec §9.2/§9.4): primitive nested arrays render as
	// "- [M]: v1,v2" headers, NOT the flattened or inline-JSON "[1,2]" forms.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([[1,2],[3,4]])")));
		std::string toon = j.toTOON();
		ASSERT(toon.find("- [2]: 1,2") != std::string::npos);
		ASSERT(toon.find("- [2]: 3,4") != std::string::npos);
		ASSERT(toon.find("- [1,2]") == std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		auto* r = j2.getRoot();
		ASSERT(r != nullptr);
		ASSERT_EQ(r->type, asvJSONValue::ARRAY);
		ASSERT_EQ(r->size(), size_t(2));
		ASSERT_EQ(r->get(0)->size(), size_t(2));
		ASSERT_EQ(r->get(0)->get(0)->getInt(), int64_t(1));
		ASSERT_EQ(r->get(1)->get(1)->getInt(), int64_t(4));
	}
	// Root array of deeply nested arrays (multi-level)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([[[1,2],[3,4]],[[5,6]]])")));
		std::string toon = j.toTOON();
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		auto* r = j2.getRoot();
		ASSERT(r != nullptr);
		ASSERT_EQ(r->size(), size_t(2));
		ASSERT_EQ(r->get(0)->size(), size_t(2));
		ASSERT_EQ(r->get(0)->get(0)->get(0)->getInt(), int64_t(1));
		ASSERT_EQ(r->get(1)->get(0)->get(1)->getInt(), int64_t(6));
	}
	// Root array with an empty nested array (must keep the empty element)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([[],[1]])")));
		std::string toon = j.toTOON();
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		auto* r = j2.getRoot();
		ASSERT(r != nullptr);
		ASSERT_EQ(r->size(), size_t(2));
		ASSERT_EQ(r->get(0)->size(), size_t(0));
		ASSERT_EQ(r->get(1)->get(0)->getInt(), int64_t(1));
	}
	// Root array mixing nested arrays and objects
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([[1,2],{"x":3},[4],[5,6]])")));
		std::string toon = j.toTOON();
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		auto* r = j2.getRoot();
		ASSERT(r != nullptr);
		ASSERT_EQ(r->size(), size_t(4));
		ASSERT_EQ(r->get(0)->size(), size_t(2));
		ASSERT_EQ(r->get(1)->type, asvJSONValue::OBJECT);
		ASSERT_EQ(r->get(1)->get("x")->getInt(), int64_t(3));
		ASSERT_EQ(r->get(2)->size(), size_t(1));
		ASSERT_EQ(r->get(3)->get(1)->getInt(), int64_t(6));
	}
}

TEST(testToTOONSpecListForm) {
	// Decoder accepts the spec list-form nested-array headers (§9.2/§9.4):
	// "- [M]: v1,v2" (primitive), "- [M]:" + deeper items (non-uniform),
	// and "- [0]:" (empty inner array).
	{
		// Primitive inner arrays via list-form headers
		asvJSON j;
		ASSERT(j.fromTOON("k: []\n  - [2]: 1,2\n  - [2]: 3,4\n"));
		auto* items = j.getRoot()->getConst("k");
		ASSERT(items != nullptr);
		ASSERT_EQ(items->type, asvJSONValue::ARRAY);
		ASSERT_EQ(items->size(), size_t(2));
		ASSERT_EQ(items->get(0)->size(), size_t(2));
		ASSERT_EQ(items->get(0)->get(0)->getInt(), int64_t(1));
		ASSERT_EQ(items->get(1)->get(1)->getInt(), int64_t(4));
	}
	{
		// Empty inner array "- [0]:"
		asvJSON j;
		ASSERT(j.fromTOON("k: []\n  - [0]:\n  - [1]: 7\n"));
		auto* items = j.getRoot()->getConst("k");
		ASSERT(items != nullptr);
		ASSERT_EQ(items->size(), size_t(2));
		ASSERT_EQ(items->get(0)->size(), size_t(0));
		ASSERT_EQ(items->get(1)->get(0)->getInt(), int64_t(7));
	}
	{
		// Non-uniform inner array: "- [M]:" + deeper items (nested array + object)
		asvJSON j;
		ASSERT(j.fromTOON("k: []\n  - [2]:\n    - 1\n    - [2]: 2,3\n"));
		auto* items = j.getRoot()->getConst("k");
		ASSERT(items != nullptr);
		ASSERT_EQ(items->size(), size_t(1));
		ASSERT_EQ(items->get(0)->size(), size_t(2));
		ASSERT_EQ(items->get(0)->get(0)->getInt(), int64_t(1));
		ASSERT_EQ(items->get(0)->get(1)->size(), size_t(2));
		ASSERT_EQ(items->get(0)->get(1)->get(1)->getInt(), int64_t(3));
	}
	{
		// Root array whose elements are nested arrays in list form
		asvJSON j;
		ASSERT(j.fromTOON("- [2]: 10,20\n- [1]: 30\n"));
		auto* r = j.getRoot();
		ASSERT(r != nullptr);
		ASSERT_EQ(r->type, asvJSONValue::ARRAY);
		ASSERT_EQ(r->size(), size_t(2));
		ASSERT_EQ(r->get(0)->size(), size_t(2));
		ASSERT_EQ(r->get(0)->get(1)->getInt(), int64_t(20));
		ASSERT_EQ(r->get(1)->get(0)->getInt(), int64_t(30));
	}
	// Backward-compat: inline JSON literal "- [1,2]" still parses
	{
		asvJSON j;
		ASSERT(j.fromTOON("k: []\n  - [1,2]\n  - [3,4]\n"));
		auto* items = j.getRoot()->getConst("k");
		ASSERT(items != nullptr);
		ASSERT_EQ(items->size(), size_t(2));
		ASSERT_EQ(items->get(0)->get(0)->getInt(), int64_t(1));
		ASSERT_EQ(items->get(1)->get(1)->getInt(), int64_t(4));
	}
}

TEST(testToTOONHeadersStrict) {
	// Named "key[N]:" length-bearing headers (§9.1) -- encoder emits them and
	// the decoder round-trips them for primitive arrays.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"k":[1,2,3],"s":"x"})")));
		std::string toon = j.toTOON();
		ASSERT(toon.find("k[3]: 1,2,3") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		ASSERT_EQ(std::string(j2.getString("s")), "x");
		auto* arr = j2.getRoot()->getConst("k");
		ASSERT(arr != nullptr);
		ASSERT_EQ(arr->size(), size_t(3));
		ASSERT_EQ(arr->get(2)->getInt(), int64_t(3));
	}
	// Named non-primitive array uses "key[N]:" + list items at deeper indent.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"k":[{"a":1},{"a":2}]})")));
		std::string toon = j.toTOON();
		ASSERT(toon.find("k[2]:") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromTOON(std::string_view(toon)));
		auto* arr = j2.getRoot()->getConst("k");
		ASSERT(arr != nullptr);
		ASSERT_EQ(arr->size(), size_t(2));
		ASSERT_EQ(arr->get(1)->get("a")->getInt(), int64_t(2));
	}
	// Decoder accepts named "key[N]:" + indented list items (list form).
	{
		asvJSON j;
		ASSERT(j.fromTOON("k[2]:\n  - 1\n  - 2\n"));
		auto* arr = j.getRoot()->getConst("k");
		ASSERT(arr != nullptr);
		ASSERT_EQ(arr->size(), size_t(2));
		ASSERT_EQ(arr->get(1)->getInt(), int64_t(2));
	}
	// Root "[N]:" + list items parses without extra nesting (root array fix).
	{
		asvJSON j;
		ASSERT(j.fromTOON("[2]:\n  - 10\n  - 20\n"));
		auto* r = j.getRoot();
		ASSERT(r != nullptr);
		ASSERT_EQ(r->type, asvJSONValue::ARRAY);
		ASSERT_EQ(r->size(), size_t(2));
		ASSERT_EQ(r->get(1)->getInt(), int64_t(20));
	}
	// Root "[N]:" + inline primitive still parses as a flat array (no double-wrap).
	{
		asvJSON j;
		ASSERT(j.fromTOON("[3]: 1,2,3\n"));
		auto* r = j.getRoot();
		ASSERT(r != nullptr);
		ASSERT_EQ(r->type, asvJSONValue::ARRAY);
		ASSERT_EQ(r->size(), size_t(3));
		ASSERT_EQ(r->get(2)->getInt(), int64_t(3));
	}
	// Strict length: named inline array count must equal the declared N.
	{
		asvJSON j;
		ASSERT(!j.fromTOON("k[3]: 1,2\n"));
	}
	// Strict length: nested-array list header count must equal the declared M.
	{
		asvJSON j;
		ASSERT(!j.fromTOON("k[2]:\n  - [2]: 1\n"));
	}
	// Strict length: tabular row count must equal the declared N.
	{
		asvJSON j;
		ASSERT(!j.fromTOON("k[2]{a,b}:\n  x,1\n"));
	}
	// Empty array header "key[0]:" parses to an empty array.
	{
		asvJSON j;
		ASSERT(j.fromTOON("k[0]:\n"));
		auto* arr = j.getRoot()->getConst("k");
		ASSERT(arr != nullptr);
		ASSERT_EQ(arr->type, asvJSONValue::ARRAY);
		ASSERT_EQ(arr->size(), size_t(0));
	}
}

TEST(testToTRON) {
	// Basic object (no repeated schema -> JSON fallback)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"name":"John","age":30,"active":true})")));
		std::string tron = j.toTRON();
		ASSERT(!tron.empty());
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(std::string(j2.getString("name")), "John");
		ASSERT_EQ(j2.getInt("age"), 30);
		ASSERT_EQ(j2.getBool("active"), true);
	}
	// Array of objects with same structure -> class + instances
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"x":1,"y":2},{"x":3,"y":4},{"x":5,"y":6}])")));
		std::string tron = j.toTRON();
		ASSERT(!tron.empty());
		ASSERT(tron.find("class A:") != std::string::npos || tron.find("class A:") != std::string::npos);
		ASSERT(tron.find("A(1,2)") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(3));
		ASSERT_EQ(j2.getRoot()->get(0)->getConst("x")->getInt(), int64_t(1));
		ASSERT_EQ(j2.getRoot()->get(1)->getConst("y")->getInt(), int64_t(4));
	}
	// Single object -> no class
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"a":1,"b":2})")));
		std::string tron = j.toTRON();
		ASSERT(tron.find("class") == std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getInt("a"), int64_t(1));
	}
	// Object with 1 property -> no class even if repeated
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"x":1},{"x":2},{"x":3}])")));
		std::string tron = j.toTRON();
		ASSERT(tron.find("class") == std::string::npos);
	}
	// Empty object and array
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		std::string tron = j.toTRON();
		ASSERT(tron.find("{}") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(0));
	}
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("[]")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(0));
	}
	// Null root
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("null")));
		std::string tron = j.toTRON();
		ASSERT(tron == "null\n");
	}
	// Primitives round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"s":"hello","n":42,"d":3.14,"b":true,"v":null})")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(std::string(j2.getString("s")), "hello");
		ASSERT_EQ(j2.getInt("n"), int64_t(42));
		ASSERT(j2.getDouble("d") > 3.13 && j2.getDouble("d") < 3.15);
		ASSERT(j2.getBool("b"));
		ASSERT(j2.isNull("v"));
	}
	// Nested objects round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"outer":{"inner":{"a":1}}})")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getInt("outer.inner.a"), int64_t(1));
	}
}

TEST(testToTRONAdvanced) {
	// Round-trip: mixed types in array
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"a":1,"b":"x"},{"a":2,"b":"y"}])")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(2));
		ASSERT_EQ(std::string(j2.getRoot()->get(0)->getConst("b")->getString()), "x");
	}
	// Strings with special characters (quotes and backslash)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{\"msg\":\"hello \\\"world\\\"\"}")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(std::string(j2.getString("msg")), "hello \"world\"");
	}
	// Arrays of objects inside arrays
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"groups":[[{"p":1},{"p":2}],[{"p":3}]]})")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getRoot()->getConst("groups")->size(), size_t(2));
	}
	// Repeat round-trip: array of 2-prop objects
	{
		asvJSON j;
		j.parse(std::string_view(R"([{"x":1,"y":2},{"x":3,"y":4}])"));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getRoot()->get(0)->getConst("x")->getInt(), int64_t(1));
		ASSERT_EQ(j2.getRoot()->get(1)->getConst("y")->getInt(), int64_t(4));
	}
	// Direct TRON parsing: class definition + instances
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Point: x,y\n\nPoint(10,20)\n")));
		ASSERT_EQ(j.getInt("x"), int64_t(10));
		ASSERT_EQ(j.getInt("y"), int64_t(20));
	}
	// TRON parsing: array of class instances
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class A: a,b\n[A(1,2),A(3,4)]\n")));
		ASSERT_EQ(j.getRoot()->size(), size_t(2));
		ASSERT_EQ(j.getRoot()->get(0)->getConst("a")->getInt(), int64_t(1));
		ASSERT_EQ(j.getRoot()->get(1)->getConst("b")->getInt(), int64_t(4));
	}
	// TRON parsing: inheritance
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Base: id\nclass Item(Base): name,price\nItem(1,\"apple\",0.99)\n")));
		ASSERT_EQ(j.getInt("id"), int64_t(1));
		ASSERT_EQ(std::string(j.getString("name")), "apple");
	}
	// TRON parsing: named arguments
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class A: x,y\nA(x=10,y=20)\n")));
		ASSERT_EQ(j.getInt("x"), int64_t(10));
		ASSERT_EQ(j.getInt("y"), int64_t(20));
	}
	// TRON parsing: mixed positional + named (missing y -> null)
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class A: x,y,z\nA(1, z=30)\n")));
		ASSERT_EQ(j.getInt("x"), int64_t(1));
		ASSERT(j.isNull("y"));
		ASSERT_EQ(j.getInt("z"), int64_t(30));
	}
	// TRON parsing: comments (#)
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("# this is a comment\nclass A: x,y\nA(1,2)\n")));
		ASSERT_EQ(j.getInt("x"), int64_t(1));
	}
	// TRON parsing: trailing commas
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class A: x,y\nA(1,2,)\n")));
		ASSERT_EQ(j.getInt("x"), int64_t(1));
	}
	// TRON parsing: nested class instances
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Point: x,y\nclass Line: start,end\nLine(Point(1,2),Point(3,4))\n")));
		ASSERT_EQ(j.getRoot()->getConst("start")->getConst("x")->getInt(), int64_t(1));
		ASSERT_EQ(j.getRoot()->getConst("end")->getConst("y")->getInt(), int64_t(4));
	}
}

TEST(testToTRONComprehensive) {
	// 1. Round-trip: toTRON() + fromTRON() -> original JSON values
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"name":"Alice","age":30,"scores":[90,85,95],"meta":{"active":true,"tag":"v1"}})")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(std::string(j2.getString("name")), "Alice");
		ASSERT_EQ(j2.getInt("age"), int64_t(30));
		ASSERT_EQ(j2.getRoot()->getConst("scores")->size(), size_t(3));
		ASSERT_EQ(j2.getRoot()->getConst("scores")->get(1)->getInt(), int64_t(85));
		ASSERT(j2.getRoot()->getConst("meta")->getConst("active")->getBool());
		ASSERT_EQ(std::string(j2.getString("meta.tag")), "v1");
	}
	// Round-trip: empty object/array
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"empty_obj":{},"empty_arr":[]})")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getRoot()->getConst("empty_obj")->size(), size_t(0));
		ASSERT_EQ(j2.getRoot()->getConst("empty_arr")->size(), size_t(0));
	}
	// Round-trip: deeply nested
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"a":{"b":{"c":{"d":42}}}})")));
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getInt("a.b.c.d"), int64_t(42));
	}
	// 2. Class inheritance: class Child(Parent): ...
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Animal: species,age\nclass Dog(Animal): breed,name\nDog(\"Canine\",5,\"Beagle\",\"Rex\")\n")));
		ASSERT_EQ(std::string(j.getString("species")), "Canine");
		ASSERT_EQ(j.getInt("age"), int64_t(5));
		ASSERT_EQ(std::string(j.getString("breed")), "Beagle");
		ASSERT_EQ(std::string(j.getString("name")), "Rex");
	}
	// Inheritance with named args overriding parent keys
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Base: id,label\nclass Derived(Base): value,label\nDerived(id=1, label=\"child\", value=99)\n")));
		ASSERT_EQ(j.getInt("id"), int64_t(1));
		ASSERT_EQ(std::string(j.getString("label")), "child");
		ASSERT_EQ(j.getInt("value"), int64_t(99));
	}
	// 3. Named arguments: mixed positional + named
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Person: name,age,city\nPerson(\"Alice\", city=\"NYC\", age=30)\n")));
		ASSERT_EQ(std::string(j.getString("name")), "Alice");
		ASSERT_EQ(j.getInt("age"), int64_t(30));
		ASSERT_EQ(std::string(j.getString("city")), "NYC");
	}
	// All named (any order)
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Point: x,y\nPoint(y=20, x=10)\n")));
		ASSERT_EQ(j.getInt("x"), int64_t(10));
		ASSERT_EQ(j.getInt("y"), int64_t(20));
	}
	// Named arg key is a TRON keyword (quoted in class def, named arg as STRING)
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class A:\"class\",value\nA(\"myclass\", value=7)\n")));
		ASSERT_EQ(std::string(j.getString("class")), "myclass");
		ASSERT_EQ(j.getInt("value"), int64_t(7));
	}
	// 4. NaN/Infinity with allowNaNInfinity = true
	{
		asvJSON j;
		j.putDouble("nan", std::numeric_limits<double>::quiet_NaN());
		j.putDouble("inf", std::numeric_limits<double>::infinity());
		j.putDouble("neg_inf", -std::numeric_limits<double>::infinity());
		j.allowNaNInfinity = true;
		std::string tron = j.toTRON();
		ASSERT(tron.find("NaN") != std::string::npos || tron.find("nan") != std::string::npos);
		ASSERT(tron.find("Infinity") != std::string::npos);
		// Round-trip NaN/Infinity via TRON
		asvJSON j2;
		j2.allowNaNInfinity = true;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT(std::isnan(j2.getDouble("nan")));
		ASSERT(std::isinf(j2.getDouble("inf")));
		ASSERT(j2.getDouble("inf") > 0);
		ASSERT(std::isinf(j2.getDouble("neg_inf")));
		ASSERT(j2.getDouble("neg_inf") < 0);
	}
	// NaN/Infinity without allowNaNInfinity -> serialized as null, parse as null
	{
		asvJSON j;
		j.putDouble("nan", std::numeric_limits<double>::quiet_NaN());
		j.putDouble("inf", std::numeric_limits<double>::infinity());
		j.allowNaNInfinity = false;
		std::string tron = j.toTRON();
		ASSERT(tron.find("null") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT(j2.isNull("nan"));
		ASSERT(j2.isNull("inf"));
	}
	// 5. Special types: DATETIME, BINARY, OBJECTID, REGEX, TIMESTAMP, EXTENSION
	{
		asvJSON j;
		// DATETIME
		time_t dt_val = 1705314645;
		j.putDateTime("dt", dt_val);
		// BINARY
		uint8_t bin_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
		j.putBinary("bin", bin_data, 4);
		// OBJECTID
		j.putObjectId("oid", std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C", 12));
		// REGEX
		j.putRegex("rx", "^test$", "gi");
		// TIMESTAMP
		j.putTimestamp("ts", 987654321);
		// EXTENSION
		uint8_t ext_data[] = {0x01, 0x02, 0x03, 0x04};
		j.putExtension("ext", 42, ext_data, 4);
		// Round-trip via TRON
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		// Verify DATETIME (type preserved via ISO 8601 detection)
		ASSERT_EQ(j2.getDateTime("dt"), dt_val);
		// Verify BINARY (type preserved via __BASE64__ detection)
		auto bin_out = j2.getBinary("bin");
		ASSERT_EQ(bin_out.size(), size_t(4));
		ASSERT_EQ(bin_out[0], uint8_t(0xDE));
		ASSERT_EQ(bin_out[3], uint8_t(0xEF));
		// Verify OBJECTID (type preserved via __OID__/hex convention)
		ASSERT_EQ(std::string(j2.getObjectId("oid")), std::string("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C", 12));
		// Verify REGEX (type preserved via __REGEX__/pattern|opts convention)
		ASSERT(j2.hasKey("rx"));
		ASSERT_EQ(j2.getRoot()->getConst("rx")->type, asvJSONValue::REGEX);
		ASSERT_EQ(j2.getRegexPattern("rx"), "^test$");
		ASSERT_EQ(j2.getRegexOptions("rx"), "gi");
		// Verify TIMESTAMP (type preserved via __TS__ convention)
		ASSERT_EQ(j2.getRoot()->getConst("ts")->type, asvJSONValue::TIMESTAMP);
		ASSERT_EQ(j2.getTimestamp("ts"), int64_t(987654321));
		// Verify EXTENSION (type preserved via __EXT__ detection)
		auto ext = j2.getExtension("ext");
		ASSERT_EQ(ext.first, 42);
		ASSERT_EQ(ext.second.size(), size_t(4));
		ASSERT_EQ(ext.second[0], uint8_t(0x01));
	}
	// Round-trip: all special types inside an array context
	{
		asvJSON j;
		time_t dt_val = 1700000000;
		j.putDateTime("dt", dt_val);
		j.putObjectId("oid", std::string_view("ABCDEF123456", 12));
		j.putTimestamp("ts", 1000);
		j.putRegex("rx", "pattern", "ims");
		std::string tron = j.toTRON();
		asvJSON j2;
		ASSERT(j2.fromTRON(std::string_view(tron)));
		ASSERT_EQ(j2.getDateTime("dt"), dt_val);
		// OBJECTID round-trips with its type preserved
		ASSERT_EQ(std::string(j2.getObjectId("oid")), "ABCDEF123456");
		// TIMESTAMP round-trips with its type preserved
		ASSERT_EQ(j2.getRoot()->getConst("ts")->type, asvJSONValue::TIMESTAMP);
		ASSERT_EQ(j2.getTimestamp("ts"), int64_t(1000));
		// REGEX round-trips with its type preserved
		ASSERT_EQ(j2.getRegexPattern("rx"), "pattern");
		ASSERT_EQ(j2.getRegexOptions("rx"), "ims");
	}
	// Newline-separated property list (spec form) followed by data on the next
	// line whose first token is a class instantiation identifier.
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class Person:\n  first_name\n  last_name\n  age\nPerson(\"A\",\"B\",30)\n")));
		ASSERT_EQ(std::string(j.getString("first_name")), "A");
		ASSERT_EQ(std::string(j.getString("last_name")), "B");
		ASSERT_EQ(j.getInt("age"), int64_t(30));
	}
	// A data identifier on the line after a class definition must NOT be
	// swallowed into the class's property list (an identifier followed by '('
	// is an instantiation, not a property).
	{
		asvJSON j;
		ASSERT(j.fromTRON(std::string_view("class A: a\nclass B: u,v\nB(1,2)\n")));
		ASSERT_EQ(j.getInt("u"), int64_t(1));
		ASSERT_EQ(j.getInt("v"), int64_t(2));
	}
	// Too many positional arguments -> SyntaxError (spec).
	{
		asvJSON j;
		ASSERT(!j.fromTRON(std::string_view("class A: x,y\nA(1,2,3)\n")));
	}
	// Unknown named argument -> SyntaxError (spec).
	{
		asvJSON j;
		ASSERT(!j.fromTRON(std::string_view("class A: x,y\nA(x=1,y=2,z=3)\n")));
	}
	// Duplicate named argument -> SyntaxError (spec).
	{
		asvJSON j;
		ASSERT(!j.fromTRON(std::string_view("class A: x,y\nA(x=1,x=2)\n")));
	}
	// Positional and named assignment of the same property -> SyntaxError.
	{
		asvJSON j;
		ASSERT(!j.fromTRON(std::string_view("class A: x,y\nA(1, x=2)\n")));
	}
}

TEST(testToGOON) {
	// Round-trip: plain object
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"name":"Alice","age":30})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(std::string(j2.getString("name")), "Alice");
		ASSERT_EQ(j2.getInt("age"), int64_t(30));
	}
	// Round-trip: booleans, null, empty string
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"a":true,"b":false,"c":null,"d":""})")));
		std::string goon = j.toGOON();
		ASSERT(goon.find('T') != std::string::npos);
		ASSERT(goon.find('F') != std::string::npos);
		ASSERT(goon.find('_') != std::string::npos);
		ASSERT(goon.find('~') != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getBool("a"));
		ASSERT(!j2.getBool("b"));
		ASSERT(j2.isNull("c"));
		ASSERT_EQ(std::string(j2.getString("d")), "");
	}
	// Tabular array round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"x":1,"y":2},{"x":3,"y":4}])")));
		std::string goon = j.toGOON();
		ASSERT(goon.find("[2]") != std::string::npos);
		ASSERT(goon.find("{") != std::string::npos);
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(j2.getRoot()->size(), size_t(2));
		ASSERT_EQ(j2.getRoot()->get(0)->getConst("x")->getInt(), int64_t(1));
		ASSERT_EQ(j2.getRoot()->get(1)->getConst("y")->getInt(), int64_t(4));
	}
	// Round-trip: nested objects
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"outer":{"inner":{"a":1}}})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(j2.getInt("outer.inner.a"), int64_t(1));
	}
	// Mixed array (list format)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[1,"two",true,null]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(j2.getRoot()->getConst("items")->size(), size_t(4));
		ASSERT_EQ(j2.getRoot()->getConst("items")->get(0)->getInt(), int64_t(1));
		ASSERT_EQ(std::string(j2.getRoot()->getConst("items")->get(1)->getString()), "two");
	}
	// Direct GOON parsing: tabular array
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("users[2]{name,age}:\n  Alice,30\n  Bob,25\n")));
		ASSERT_EQ(j.getRoot()->size(), size_t(1));
		ASSERT(j.getRoot()->hasKey("users"));
		ASSERT_EQ(j.getRoot()->getConst("users")->size(), size_t(2));
		ASSERT_EQ(std::string(j.getRoot()->getConst("users")->get(0)->getConst("name")->getString()), "Alice");
		ASSERT_EQ(j.getRoot()->getConst("users")->get(1)->getConst("age")->getInt(), int64_t(25));
	}
	// Direct GOON parsing: literals
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("a: T\nb: F\nc: _\nd: ~\n")));
		ASSERT(j.getBool("a"));
		ASSERT(!j.getBool("b"));
		ASSERT(j.isNull("c"));
		ASSERT_EQ(std::string(j.getString("d")), "");
	}
	// Direct GOON parsing: nested object with indentation
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("outer:\n  inner:\n    a: 42\n")));
		ASSERT_EQ(j.getInt("outer.inner.a"), int64_t(42));
	}
	// Direct GOON parsing: list array with inline values
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("items[]: 10,20,30\n")));
		ASSERT_EQ(j.getRoot()->getConst("items")->size(), size_t(3));
		ASSERT_EQ(j.getRoot()->getConst("items")->get(0)->getInt(), int64_t(10));
		ASSERT_EQ(j.getRoot()->getConst("items")->get(2)->getInt(), int64_t(30));
	}
	// Direct GOON parsing: list array with - items
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("items[]:\n  - 10\n  - 20\n  - 30\n")));
		ASSERT_EQ(j.getRoot()->getConst("items")->size(), size_t(3));
		ASSERT_EQ(j.getRoot()->getConst("items")->get(1)->getInt(), int64_t(20));
	}
	// Direct GOON parsing: dictionary $N references
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("$:$0=admin,$1=user\nusers[2]{name,role}:\n  Alice,$0\n  Bob,$1\n")));
		ASSERT_EQ(j.getRoot()->size(), size_t(1));
		ASSERT(j.getRoot()->hasKey("users"));
		ASSERT_EQ(j.getRoot()->getConst("users")->size(), size_t(2));
		ASSERT_EQ(std::string(j.getRoot()->getConst("users")->get(0)->getConst("role")->getString()), "admin");
		ASSERT_EQ(std::string(j.getRoot()->getConst("users")->get(1)->getConst("role")->getString()), "user");
	}
	// Direct GOON parsing: run-length encoding *N
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("flags[3]{enabled}:\n  T*3\n")));
		ASSERT_EQ(j.getRoot()->size(), size_t(1));
		ASSERT(j.getRoot()->hasKey("flags"));
		ASSERT_EQ(j.getRoot()->getConst("flags")->size(), size_t(3));
		for (size_t i = 0; i < 3; i++)
			ASSERT(j.getRoot()->getConst("flags")->get(i)->getConst("enabled")->getBool());
	}
	// Round-trip: double values
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"pi":3.14,"neg":-2.5})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getDouble("pi") > 3.13 && j2.getDouble("pi") < 3.15);
		ASSERT(j2.getDouble("neg") > -2.51 && j2.getDouble("neg") < -2.49);
	}
	// Round-trip: array of objects with same schema -> tabular
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[{"id":1,"val":"a"},{"id":2,"val":"b"}]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(j2.getRoot()->getConst("items")->size(), size_t(2));
		ASSERT_EQ(j2.getRoot()->getConst("items")->get(0)->getConst("id")->getInt(), int64_t(1));
		ASSERT_EQ(std::string(j2.getRoot()->getConst("items")->get(1)->getConst("val")->getString()), "b");
	}
}

TEST(testGOONAudit) {
	// Bug 3: Colon in key round-trip
	{
		asvJSON j;
		j.parse(std::string_view("{}"));
		j.getRoot()->obj->emplace("a:b", asvJSONValue::makeInt(1));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(j2.getInt("a:b"), int64_t(1));
	}
	// Bug 4: Empty key round-trip
	{
		asvJSON j;
		j.parse(std::string_view("{}"));
		j.getRoot()->obj->emplace("", asvJSONValue::makeInt(42));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* v = j2.getRoot()->getConst("");
		ASSERT(v != nullptr);
		ASSERT_EQ(v->getInt(), int64_t(42));
	}
	// Bug 6: BINARY round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
		j.getRoot()->obj->emplace("bin", asvJSONValue::makeBinary(data, 4));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* v = j2.getRoot()->getConst("bin");
		ASSERT(v != nullptr);
		ASSERT(v->type == asvJSONValue::BINARY);
		ASSERT(v->bin_data.size() == 4);
		ASSERT(v->bin_data[0] == 0xDE);
	}
	// Bug 7: EXTENSION round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		uint8_t data[] = {1, 2, 3};
		j.getRoot()->obj->emplace("ext", asvJSONValue::makeExtension(127, data, 3));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* v = j2.getRoot()->getConst("ext");
		ASSERT(v != nullptr);
		ASSERT(v->type == asvJSONValue::EXTENSION);
		ASSERT(static_cast<uint8_t>(v->ext_type) == 127);
		ASSERT(v->bin_data.size() == 3);
		ASSERT(v->bin_data[0] == 1);
	}
	// Bug 8: OBJECTID round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		std::string oid_bytes = "1234567890ab";
		j.getRoot()->obj->emplace("id", asvJSONValue::makeObjectId(oid_bytes));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* v = j2.getRoot()->getConst("id");
		ASSERT(v != nullptr);
		ASSERT(v->type == asvJSONValue::OBJECTID);
		ASSERT(v->str_data == oid_bytes);
	}
	// Bug 9: TIMESTAMP round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		j.getRoot()->obj->emplace("ts", asvJSONValue::makeTimestamp(1234567890));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* v = j2.getRoot()->getConst("ts");
		ASSERT(v != nullptr);
		ASSERT(v->type == asvJSONValue::TIMESTAMP);
		ASSERT(v->num == 1234567890);
	}
	// Bug 10: REGEX round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("foo.*bar", "gi"));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* v = j2.getRoot()->getConst("re");
		ASSERT(v != nullptr);
		ASSERT(v->type == asvJSONValue::REGEX);
	}
	// Bug 1: Bare objects in arrays
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[{"c":3}]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* a = j2.getRoot()->getConst("items");
		ASSERT(a != nullptr);
		ASSERT(a->type == asvJSONValue::ARRAY);
		ASSERT(a->size() == 1);
		const auto* o = a->get(0);
		ASSERT(o != nullptr);
		ASSERT(o->type == asvJSONValue::OBJECT);
		ASSERT_EQ(o->getConst("c")->getInt(), int64_t(3));
	}
	// Bug 1b: Array of objects in list form (bare "-" items)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"arr":[{"x":1},{"y":2}]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* a = j2.getRoot()->getConst("arr");
		ASSERT(a != nullptr);
		ASSERT(a->type == asvJSONValue::ARRAY);
		ASSERT(a->size() == 2);
		ASSERT(a->get(0)->type == asvJSONValue::OBJECT);
		ASSERT_EQ(a->get(0)->getConst("x")->getInt(), int64_t(1));
		ASSERT(a->get(1)->type == asvJSONValue::OBJECT);
		ASSERT_EQ(a->get(1)->getConst("y")->getInt(), int64_t(2));
	}
	// Bug 2: Nested arrays in list items
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[1,[2,3]]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* a = j2.getRoot()->getConst("items");
		ASSERT(a != nullptr);
		ASSERT(a->size() == 2);
		ASSERT_EQ(a->get(0)->getInt(), int64_t(1));
		ASSERT(a->get(1)->type == asvJSONValue::ARRAY);
		ASSERT(a->get(1)->size() == 2);
		ASSERT_EQ(a->get(1)->get(0)->getInt(), int64_t(2));
		ASSERT_EQ(a->get(1)->get(1)->getInt(), int64_t(3));
	}
	// Bug 5: Deep nested arrays
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"matrix":[["x","y"],[10]]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* m = j2.getRoot()->getConst("matrix");
		ASSERT(m != nullptr);
		ASSERT(m->size() == 2);
		ASSERT(m->get(0)->type == asvJSONValue::ARRAY);
		ASSERT(m->get(0)->size() == 2);
		ASSERT(m->get(1)->type == asvJSONValue::ARRAY);
		ASSERT(m->get(1)->size() == 1);
	}
	// Multi-type round-trip
	{
		asvJSON j;
		j.parse(std::string_view("{}"));
		j.getRoot()->obj->emplace("str", asvJSONValue::makeString("hello", 5));
		j.getRoot()->obj->emplace("num", asvJSONValue::makeInt(42));
		j.getRoot()->obj->emplace("flag", asvJSONValue::makeBool(true));
		j.getRoot()->obj->emplace("empty", asvJSONValue::makeNull());
		j.getRoot()->obj->emplace("a:b", asvJSONValue::makeInt(99));
		j.getRoot()->obj->emplace("", asvJSONValue::makeInt(7));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(std::string(j2.getString("str")), "hello");
		ASSERT_EQ(j2.getInt("num"), int64_t(42));
		ASSERT(j2.getBool("flag"));
		ASSERT(j2.isNull("empty"));
		ASSERT_EQ(j2.getInt("a:b"), int64_t(99));
		const auto* v7 = j2.getRoot()->getConst("");
		ASSERT(v7 != nullptr);
		ASSERT_EQ(v7->getInt(), int64_t(7));
	}
}

TEST(testGOONExtras) {
	// Caret cross-row RLE in Compact mode (numeric column)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"a":1,"b":1},{"a":2,"b":2},{"a":3,"b":3}])")));
		GoonWriteOptions opts;
		opts.mode = GoonWriteOptions::Mode::Compact;
		std::string goon = j.toGOON(opts);
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getRoot()->size() == 3);
		ASSERT_EQ(j2.getRoot()->get(2)->getConst("b")->getInt(), int64_t(3));
	}
	// Caret across rows with dictionary references (string column)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"s":"x","n":1},{"s":"x","n":2},{"s":"x","n":3}])")));
		GoonWriteOptions opts;
		opts.mode = GoonWriteOptions::Mode::Compact;
		std::string goon = j.toGOON(opts);
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getRoot()->size() == 3);
		for (size_t i = 0; i < 3; i++) {
			ASSERT(j2.getRoot()->get(i)->hasKey("s"));
			ASSERT_EQ(std::string(j2.getRoot()->get(i)->getConst("s")->getString()), "x");
		}
		ASSERT_EQ(j2.getRoot()->get(2)->getConst("n")->getInt(), int64_t(3));
	}
	// Object nested in array in array (Compact and default)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[[{"x":1}],[{"x":2}]]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* a = j2.getRoot()->getConst("items");
		ASSERT(a != nullptr);
		ASSERT(a->size() == 2);
		ASSERT(a->get(0)->type == asvJSONValue::ARRAY);
		ASSERT_EQ(a->get(0)->get(0)->getConst("x")->getInt(), int64_t(1));
		ASSERT(a->get(1)->type == asvJSONValue::ARRAY);
		ASSERT_EQ(a->get(1)->get(0)->getConst("x")->getInt(), int64_t(2));
	}
	// Booleans inside nested arrays in Compact mode (valid JSON inline)
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"a":[[1,true]]})")));
		GoonWriteOptions opts;
		opts.mode = GoonWriteOptions::Mode::Compact;
		std::string goon = j.toGOON(opts);
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* a = j2.getRoot()->getConst("a");
		ASSERT(a != nullptr);
		ASSERT(a->get(0)->type == asvJSONValue::ARRAY);
		ASSERT(a->get(0)->get(0)->getInt() == 1);
		ASSERT(a->get(0)->get(1)->getBool());
	}
	// Quoted keys with backslash-comma and tab-comma
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		j.getRoot()->obj->emplace("a\\b,c", asvJSONValue::makeInt(1));
		j.getRoot()->obj->emplace(std::string("a\tb,c"), asvJSONValue::makeInt(2));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT_EQ(j2.getRoot()->getConst("a\\b,c")->getInt(), int64_t(1));
		ASSERT_EQ(j2.getRoot()->getConst(std::string("a\tb,c"))->getInt(), int64_t(2));
	}
	// REGEX pattern containing '|' and backslash, with and without options
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		j.getRoot()->obj->emplace("r1", asvJSONValue::makeRegex("a|b", nullptr));
		j.getRoot()->obj->emplace("r2", asvJSONValue::makeRegex("a|b", "i"));
		j.getRoot()->obj->emplace("r3", asvJSONValue::makeRegex("a\\b", "g"));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* r1 = j2.getRoot()->getConst("r1");
		ASSERT(r1 != nullptr);
		ASSERT(r1->type == asvJSONValue::REGEX);
		ASSERT(r1->str_data == std::string("a|b") + std::string(1, '\0'));
		ASSERT(j2.getRoot()->getConst("r2")->str_data == std::string("a|b") + std::string(1, '\0') + "i");
		ASSERT(j2.getRoot()->getConst("r3")->str_data == std::string("a\\b") + std::string(1, '\0') + "g");
	}
	// Forbidden key __proto__ round-trip
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"__proto__":1})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getRoot()->hasKey("__proto__"));
		ASSERT_EQ(j2.getRoot()->getConst("__proto__")->getInt(), int64_t(1));
	}
	// Root scalar round-trips (INT and STRING) and rejects trailing content
	{
		asvJSON j;
		j.setByPointer("/", asvJSONValue::makeInt(5));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getRoot() != nullptr);
		ASSERT(j2.getRoot()->type == asvJSONValue::INT);
		ASSERT_EQ(j2.getRoot()->getInt(), int64_t(5));
	}
	{
		asvJSON j;
		j.setByPointer("/", asvJSONValue::makeString("hello", 5));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getRoot() != nullptr);
		ASSERT(j2.getRoot()->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getRoot()->getString()), "hello");
	}
	{
		asvJSON j2;
		ASSERT(!j2.fromGOON(std::string_view("5\n  extra\n")));
	}
	// Comma in column name falls back to list format and round-trips
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"items":[{"a,b":1,"c":2}]})")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* it = j2.getRoot()->getConst("items");
		ASSERT(it != nullptr);
		ASSERT(it->size() == 1);
		ASSERT_EQ(it->get(0)->getConst("a,b")->getInt(), int64_t(1));
		ASSERT_EQ(it->get(0)->getConst("c")->getInt(), int64_t(2));
	}
	// Strings that look like numbers/Infinities/special-prefixes stay strings
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		j.getRoot()->obj->emplace("s1", asvJSONValue::makeString("NaN", 3));
		j.getRoot()->obj->emplace("s2", asvJSONValue::makeString("+1", 2));
		j.getRoot()->obj->emplace("s3", asvJSONValue::makeString("Infinity", 8));
		j.getRoot()->obj->emplace("s4", asvJSONValue::makeString("-Infinity", 9));
		j.getRoot()->obj->emplace("s5", asvJSONValue::makeString("__TS__42", 8));
		j.getRoot()->obj->emplace("s6", asvJSONValue::makeString("__BASE64__33", 12));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		ASSERT(j2.getRoot()->getConst("s1")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getRoot()->getConst("s1")->getString()), "NaN");
		ASSERT_EQ(std::string(j2.getRoot()->getConst("s2")->getString()), "+1");
		ASSERT_EQ(std::string(j2.getRoot()->getConst("s3")->getString()), "Infinity");
		ASSERT_EQ(std::string(j2.getRoot()->getConst("s4")->getString()), "-Infinity");
		ASSERT(j2.getRoot()->getConst("s5")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getRoot()->getConst("s5")->getString()), "__TS__42");
		ASSERT(j2.getRoot()->getConst("s6")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getRoot()->getConst("s6")->getString()), "__BASE64__33");
	}
	// Quoted special-prefix strings in hand-written GOON stay strings
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("a: \"__TS__42\"\n")));
		ASSERT(j.getRoot()->getConst("a")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j.getRoot()->getConst("a")->getString()), "__TS__42");
	}
	{
		asvJSON j;
		ASSERT(j.fromGOON(std::string_view("a: __TS__42\n")));
		ASSERT(j.getRoot()->getConst("a")->type == asvJSONValue::TIMESTAMP);
		ASSERT_EQ(j.getRoot()->getConst("a")->num, int64_t(42));
	}
	// Dictionary-referenced numeric/bool/special-looking string values
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"k":"42","n":1},{"k":"42","n":2}])")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		for (size_t i = 0; i < 2; i++) {
			const auto* k = j2.getRoot()->get(i)->getConst("k");
			ASSERT(k != nullptr);
			ASSERT(k->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(k->getString()), "42");
		}
	}
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"k":"true","n":1},{"k":"true","n":2}])")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		for (size_t i = 0; i < 2; i++) {
			const auto* k = j2.getRoot()->get(i)->getConst("k");
			ASSERT(k != nullptr);
			ASSERT(k->type == asvJSONValue::STRING);
			ASSERT_EQ(std::string(k->getString()), "true");
		}
	}
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"k":"__TS__42","n":1},{"k":"__TS__42","n":2}])")));
		std::string goon = j.toGOON();
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* k = j2.getRoot()->get(1)->getConst("k");
		ASSERT(k != nullptr);
		ASSERT(k->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(k->getString()), "__TS__42");
	}
	// BINARY inside inline array in Compact mode
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view("{}")));
		const unsigned char hb[] = {'H', 'i'};
		j.getRoot()->obj->emplace("arr", asvJSONValue::makeArray());
		const auto* arrp = j.getRoot()->getConst("arr");
		const_cast<asvJSONValue*>(arrp)->arr->emplace_back(asvJSONValue::makeBinary(hb, 2));
		const_cast<asvJSONValue*>(arrp)->arr->emplace_back(asvJSONValue::makeExtension(15, hb, 2));
		GoonWriteOptions opts;
		opts.mode = GoonWriteOptions::Mode::Compact;
		std::string goon = j.toGOON(opts);
		asvJSON j2;
		ASSERT(j2.fromGOON(std::string_view(goon)));
		const auto* r = j2.getRoot()->getConst("arr");
		ASSERT(r != nullptr);
		ASSERT(r->size() == 2);
		ASSERT(r->get(0)->type == asvJSONValue::BINARY);
		ASSERT(r->get(1)->type == asvJSONValue::EXTENSION);
		ASSERT(static_cast<int>(r->get(1)->ext_type) == 15);
		ASSERT(r->get(1)->bin_data.size() == 2);
		ASSERT(r->get(1)->bin_data[1] == 'i');
	}
}

TEST(testProtobufBasic) {
	// Schema-less: keys must be numeric field numbers
	asvJSON json;
	json.parse(std::string(R"({"1":"test","2":42,"3":true,"5":3.14})"));

	auto buf = json.toProtobuf();
	ASSERT(buf.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromProtobuf(buf.data(), buf.size()));
	// In schema-less mode, field names stay as numeric strings
	ASSERT(json2.getRoot()->hasKey("1"));
	ASSERT(json2.getRoot()->hasKey("2"));
	ASSERT(json2.getRoot()->hasKey("3"));
}

TEST(testProtobufArrays) {
	// Schema-driven array round-trip
	std::string schemaJson = R"({
		"msg":{"id":1,"type":"string"},
		"num":{"id":2,"type":"int32"}
	})";
	asvJSON json;
	json.putString("msg", "hello");
	json.putInt("num", 7);

	auto buf = json.toProtobuf(schemaJson);
	ASSERT(buf.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
	ASSERT_EQ(std::string(json2.getString("msg")), "hello");
	ASSERT_EQ(json2.getInt("num"), 7);
}

TEST(testProtobufNested) {
	// Schema-driven nested object
	std::string schemaJson = R"({
		"outer":{"id":1,"type":"message","fields":{
			"inner":{"id":1,"type":"message","fields":{
				"val":{"id":1,"type":"int32"}
			}}
		}}
	})";
	asvJSON json;
	json.parse(std::string(R"({"outer":{"inner":{"val":42}}})"));

	auto buf = json.toProtobuf(schemaJson);
	ASSERT(buf.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
	ASSERT_EQ(json2.getInt("outer.inner.val"), 42);
}

TEST(testProtobufSchema) {
	// Schema-driven round-trip
	std::string schemaJson = R"({
		"name":{"id":1,"type":"string"},
		"age":{"id":2,"type":"int32"},
		"active":{"id":3,"type":"bool"}
	})";

	asvJSON json;
	json.putString("name", "Alice");
	json.putInt("age", 30);
	json.putBool("active", true);

	auto buf = json.toProtobuf(schemaJson);
	ASSERT(buf.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
	ASSERT_EQ(std::string(json2.getString("name")), "Alice");
	ASSERT_EQ(json2.getInt("age"), 30);
	ASSERT_EQ(json2.getBool("active"), true);
}

TEST(testProtobufTextFormat) {
	// Text format round-trip (null values are skipped in serialization)
	asvJSON json;
	json.putString("name", "test");
	json.putInt("count", 42);
	json.putBool("flag", true);

	std::string text = json.toProtobufText();
	ASSERT(!text.empty());
	ASSERT(text.find("name") != std::string::npos);
	ASSERT(text.find("42") != std::string::npos);

	asvJSON json2;
	ASSERT(json2.fromProtobufText(text));
	ASSERT_EQ(std::string(json2.getString("name")), "test");
	ASSERT_EQ(json2.getInt("count"), 42);
	ASSERT_EQ(json2.getBool("flag"), true);
}

TEST(testProtobufTextIntOverflow) {
	// int64 overflow in text format must be preserved as an exact literal (raw_number),
	// not silently clamped to LLONG_MAX - same convention as core JSON and UDE.
	asvJSON json2;
	ASSERT(json2.fromProtobufText("big: 99999999999999999999999"));
	auto* v = json2.getRoot()->get("big");
	ASSERT(v != nullptr);
	ASSERT(v->type == asvJSONValue::STRING);
	ASSERT(v->raw_number);
	std::string out = json2.serialize();
	ASSERT(out.find("99999999999999999999999") != std::string::npos);

	// In-range integers must still parse as int
	asvJSON json3;
	ASSERT(json3.fromProtobufText("big: 12345"));
	ASSERT(json3.isInt("big"));
	ASSERT_EQ(json3.getInt("big"), 12345);
}

TEST(testProtobufStaticConverters) {
	// Static helper: protobufFromString / stringFromProtobuf (numeric keys = field numbers)
	std::string jsonStr = R"({"1":1,"2":"hello"})";
	auto buf = asvJSON::protobufFromString(jsonStr);
	ASSERT(buf.size() > 0);

	auto result = asvJSON::stringFromProtobuf(buf.data(), buf.size());
	ASSERT(!result.empty());
}

TEST(testProtobufStringOverload) {
	// fromProtobuf(string) overload with schema
	std::string schemaJson = R"({
		"msg":{"id":1,"type":"string"}
	})";
	asvJSON json;
	json.putString("msg", "hello");
	auto buf = json.toProtobuf(schemaJson);
	ASSERT(buf.size() > 0);

	asvJSON json2;
	ASSERT(json2.fromProtobuf(std::string(reinterpret_cast<const char*>(buf.data()), buf.size()), schemaJson));
	ASSERT_EQ(std::string(json2.getString("msg")), "hello");
}

TEST(testProtobufPackedFixed) {
	// Simple packed int32 first (known working path)
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"int32","repeated":true,"packed":true}
		})";
		asvJSON json;
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeInt(10));
		arr->arr->push_back(asvJSONValue::makeInt(20));
		json.setValue("vals", std::move(arr));
		auto buf = json.toProtobuf(schemaJson);
		ASSERT(buf.size() > 0);
		asvJSON json2;
		ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
		ASSERT(json2.getRoot()->hasKey("vals"));
		auto* vArr = json2.getRoot()->get("vals");
		ASSERT(vArr != nullptr && vArr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(vArr->arr->size(), size_t(2));
		ASSERT_EQ(vArr->arr->at(0)->getInt(), 10);
	}
	// Packed float
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"float","repeated":true,"packed":true}
		})";
		asvJSON json;
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeDouble(1.5));
		arr->arr->push_back(asvJSONValue::makeDouble(2.5));
		json.setValue("vals", std::move(arr));
		auto buf = json.toProtobuf(schemaJson);
		ASSERT(buf.size() > 0);
		asvJSON json2;
		ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
		ASSERT(json2.getRoot()->hasKey("vals"));
		auto* vArr = json2.getRoot()->get("vals");
		ASSERT(vArr != nullptr && vArr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(vArr->arr->size(), size_t(2));
		ASSERT(vArr->arr->at(0)->getDouble() > 1.4 && vArr->arr->at(0)->getDouble() < 1.6);
	}
	// Packed double
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"double","repeated":true,"packed":true}
		})";
		asvJSON json;
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeDouble(3.141592653589793));
		arr->arr->push_back(asvJSONValue::makeDouble(2.718281828459045));
		json.setValue("vals", std::move(arr));
		auto buf = json.toProtobuf(schemaJson);
		ASSERT(buf.size() > 0);
		asvJSON json2;
		ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
		ASSERT(json2.getRoot()->hasKey("vals"));
		auto* vArr = json2.getRoot()->get("vals");
		ASSERT(vArr != nullptr && vArr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(vArr->arr->size(), size_t(2));
		ASSERT(vArr->arr->at(0)->getDouble() > 3.14 && vArr->arr->at(0)->getDouble() < 3.15);
	}
	// Packed fixed32
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"fixed32","repeated":true,"packed":true}
		})";
		asvJSON json;
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeInt(100));
		arr->arr->push_back(asvJSONValue::makeInt(200));
		json.setValue("vals", std::move(arr));
		auto buf = json.toProtobuf(schemaJson);
		ASSERT(buf.size() > 0);
		asvJSON json2;
		ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
		ASSERT(json2.getRoot()->hasKey("vals"));
		auto* vArr = json2.getRoot()->get("vals");
		ASSERT(vArr != nullptr && vArr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(vArr->arr->size(), size_t(2));
		ASSERT_EQ(vArr->arr->at(0)->getInt(), 100);
	}
	// Packed fixed64
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"fixed64","repeated":true,"packed":true}
		})";
		asvJSON json;
		auto arr = asvJSONValue::makeArray();
		arr->arr->push_back(asvJSONValue::makeInt(10000000000LL));
		arr->arr->push_back(asvJSONValue::makeInt(20000000000LL));
		json.setValue("vals", std::move(arr));
		auto buf = json.toProtobuf(schemaJson);
		ASSERT(buf.size() > 0);
		asvJSON json2;
		ASSERT(json2.fromProtobuf(buf.data(), buf.size(), schemaJson));
		ASSERT(json2.getRoot()->hasKey("vals"));
		auto* vArr = json2.getRoot()->get("vals");
		ASSERT(vArr != nullptr && vArr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(vArr->arr->size(), size_t(2));
		ASSERT_EQ(vArr->arr->at(0)->getInt(), 10000000000LL);
	}
	// Malformed packed varint: the last byte of the packed field has the
	// continuation bit set (incomplete varint), and a following field's bytes
	// happen to complete it. Before the fix, the decoder read past the field's
	// `end` (bounded by the whole buffer size) and produced a bogus value
	// (vals=[2048]) while skipping the real `other` field. It must now reject
	// the input with "malformed varint: unexpected end".
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"int32","repeated":true,"packed":true},
			"other":{"id":2,"type":"int32"}
		})";
		// field 1 (wire 2): tag 0x0A, len=0x01, payload 0x80 (incomplete varint)
		// field 2 (wire 0): tag 0x10, value 0x2A
		const uint8_t raw[] = {0x0A, 0x01, 0x80, 0x10, 0x2A};
		asvJSON json;
		ASSERT(!json.fromProtobuf(raw, sizeof(raw), schemaJson));
	}
	// Same shape, but the following field is large enough that the pre-fix
	// desynced read completes on a following byte and the whole message still
	// "parses" (producing vals=[2048], other missing) instead of erroring.
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"int32","repeated":true,"packed":true},
			"other":{"id":2,"type":"int32"}
		})";
		// field 1 (wire 2): tag 0x0A, len=0x01, payload 0x80 (incomplete varint)
		// field 2 (wire 0): tag 0x10, value 0x2A, then field 3 (wire 0) tag 0x18 value 0x00
		const uint8_t raw[] = {0x0A, 0x01, 0x80, 0x10, 0x2A, 0x18, 0x00};
		asvJSON json;
		ASSERT(!json.fromProtobuf(raw, sizeof(raw), schemaJson));
	}
}

TEST(testProtobufSchemaLessRoundTrip) {
	// Schema-less wire round-trip must preserve STRING/DOUBLE/message/array types;
	// the embedded name map now carries per-field type codes so real values stay
	// faithful (previously strings corrupted into sub-messages and doubles into
	// huge ints).
	{
		asvJSON j;
		j.parse(std::string(R"({"a":"hello"})"));
		auto buf = j.toProtobuf();
		asvJSON j2;
		ASSERT(j2.fromProtobuf(buf.data(), buf.size()));
		ASSERT_EQ(std::string(j2.serialize()), std::string(R"({"a":"hello"})"));
	}
	{
		asvJSON j;
		j.parse(std::string(R"({"s":"42","i":42})"));
		auto buf = j.toProtobuf();
		asvJSON j2;
		ASSERT(j2.fromProtobuf(buf.data(), buf.size()));
		auto* s = j2.getRoot()->get("s");
		ASSERT(s != nullptr && s->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getString("s")), "42");
		ASSERT(j2.get("i") != nullptr && j2.get("i")->type == asvJSONValue::INT);
	}
	{
		asvJSON j;
		j.parse(std::string(R"({"arr":[1.5,2.5]})"));
		auto buf = j.toProtobuf();
		asvJSON j2;
		ASSERT(j2.fromProtobuf(buf.data(), buf.size()));
		auto* arr = j2.getRoot()->get("arr");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT(arr->arr->at(0)->type == asvJSONValue::DOUBLE);
		ASSERT(arr->arr->at(1)->type == asvJSONValue::DOUBLE);
	}
	{
		asvJSON j;
		j.parse(std::string(R"({"arr":["x","y"]})"));
		auto buf = j.toProtobuf();
		asvJSON j2;
		ASSERT(j2.fromProtobuf(buf.data(), buf.size()));
		auto* arr = j2.getRoot()->get("arr");
		ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(arr->arr->size(), size_t(2));
		ASSERT(arr->arr->at(0)->type == asvJSONValue::STRING);
	}
	{
		asvJSON j;
		j.parse(std::string(R"({"nested":{"k":"v"}})"));
		auto buf = j.toProtobuf();
		asvJSON j2;
		ASSERT(j2.fromProtobuf(buf.data(), buf.size()));
		ASSERT_EQ(std::string(j2.serialize()), std::string(R"({"nested":{"k":"v"}})"));
	}
	{
		asvJSON j;
		j.parse(std::string(R"({"empty":{}})"));
		auto buf = j.toProtobuf();
		asvJSON j2;
		ASSERT(j2.fromProtobuf(buf.data(), buf.size()));
		auto* e = j2.getRoot()->get("empty");
		ASSERT(e != nullptr && e->type == asvJSONValue::OBJECT);
	}
}

TEST(testProtobufTextArrayOfObjects) {
	// Text-format array-of-objects must serialize as a bracketed list and
	// round-trip (previously the malformed repeated-block form lost all but the
	// first element).
	asvJSON j;
	j.parse(std::string(R"({"arr":[{"x":1},{"x":2}]})"));
	std::string text = j.toProtobufText();
	asvJSON j2;
	ASSERT(j2.fromProtobufText(text));
	auto* arr = j2.getRoot()->get("arr");
	ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
	ASSERT_EQ(arr->arr->size(), size_t(2));
	ASSERT(arr->arr->at(0)->get("x") != nullptr && arr->arr->at(0)->get("x")->num == 1);
	ASSERT(arr->arr->at(1)->get("x") != nullptr && arr->arr->at(1)->get("x")->num == 2);
}

TEST(testProtobufExtras) {
	// Numeric field-name keys (text format: field numbers are valid keys) must
	// round-trip; previously the parser rejected keys starting with a digit.
	{
		asvJSON j;
		j.parse(std::string(R"({"1":"x","2":42})"));
		std::string text = j.toProtobufText();
		asvJSON j2;
		ASSERT(j2.fromProtobufText(text));
		ASSERT(j2.getRoot()->hasKey("1"));
		ASSERT(j2.getRoot()->hasKey("2"));
		ASSERT_EQ(std::string(j2.getString("1")), "x");
		ASSERT_EQ(j2.getInt("2"), int64_t(42));
	}
	// A bare top-level numeric field document must parse too.
	{
		asvJSON j2;
		ASSERT(j2.fromProtobufText("1: \"x\"\n"));
		ASSERT_EQ(std::string(j2.getString("1")), "x");
	}
	// Nested arrays in the text format must round-trip; previously the element
	// loop never advanced past an inner '[' (infinite loop / hang).
	{
		asvJSON j;
		j.parse(std::string(R"({"arr":[[1,2],[3,4]]})"));
		std::string text = j.toProtobufText();
		asvJSON j2;
		ASSERT(j2.fromProtobufText(text));
		auto* vArr = j2.getRoot()->get("arr");
		ASSERT(vArr != nullptr && vArr->type == asvJSONValue::ARRAY);
		ASSERT_EQ(vArr->arr->size(), size_t(2));
		ASSERT(vArr->arr->at(0)->type == asvJSONValue::ARRAY);
		ASSERT(vArr->arr->at(1)->type == asvJSONValue::ARRAY);
		ASSERT_EQ(vArr->arr->at(0)->arr->size(), size_t(2));
		ASSERT_EQ(vArr->arr->at(0)->arr->at(0)->num, int64_t(1));
		ASSERT_EQ(vArr->arr->at(1)->arr->at(1)->num, int64_t(4));
	}
	// Schema-less wire round-trip must preserve empty arrays and null values
	// (they emit no wire bytes, so the decoder reconstructs them from the
	// embedded name map).
	{
		asvJSON j;
		j.parse(std::string(R"({"arr":[],"n":null,"x":1})"));
		auto buf = j.toProtobuf();
		asvJSON j2;
		ASSERT(j2.fromProtobuf(buf.data(), buf.size()));
		auto* e = j2.getRoot()->get("arr");
		ASSERT(e != nullptr && e->type == asvJSONValue::ARRAY);
		ASSERT_EQ(e->arr->size(), size_t(0));
		auto* nn = j2.getRoot()->get("n");
		ASSERT(nn != nullptr && nn->type == asvJSONValue::NULL_VAL);
		ASSERT_EQ(j2.getInt("x"), int64_t(1));
	}
	// A UTF-8 BOM must not break the text parser.
	{
		asvJSON j2;
		ASSERT(j2.fromProtobufText(std::string("\xEF\xBB\xBFname: \"x\"")));
		ASSERT_EQ(std::string(j2.getString("name")), "x");
	}
	// Malformed packed fixed-size field with trailing garbage must be rejected,
	// not silently truncated (the extra byte made the payload 5 bytes for a
	// float field and was silently dropped before the fix).
	{
		std::string schemaJson = R"({
			"vals":{"id":1,"type":"float","repeated":true,"packed":true}
		})";
		// field 1 (wire 2): tag 0x0A, len 0x05, payload = 4-byte float + 1 trailing byte
		const uint8_t raw[] = {0x0A, 0x05, 0x00, 0x00, 0xC0, 0x3F, 0x00};
		asvJSON json;
		ASSERT(!json.fromProtobuf(raw, sizeof(raw), schemaJson));
	}
}

TEST(testToCSV) {
	// Empty root (scalar fallback outputs empty line? Actually "null" case)
	{
		asvJSON json;
		std::string csv = json.toCSV();
		ASSERT(csv.empty());
	}
	// Scalar root
	{
		asvJSON json;
		json.parse(std::string("\"hello\""));
		std::string csv = json.toCSV();
		ASSERT(!csv.empty());
		ASSERT(csv.find("hello") != std::string::npos);
		ASSERT(csv.find("value") == std::string::npos); // no header for scalar
	}
	// Object: flat keys -> header + one row
	{
		asvJSON json;
		json.parse(std::string("{\"name\":\"John\",\"age\":30}"));
		std::string csv = json.toCSV();
		ASSERT(csv.find("name") != std::string::npos);
		ASSERT(csv.find("age") != std::string::npos);
		ASSERT(csv.find("John") != std::string::npos);
		ASSERT(csv.find("30") != std::string::npos);
	}
	// Nested object flattening
	{
		asvJSON json;
		json.parse(std::string("{\"a\":{\"b\":1,\"c\":2}}"));
		std::string csv = json.toCSV();
		ASSERT(csv.find("a.b") != std::string::npos);
		ASSERT(csv.find("a.c") != std::string::npos);
		ASSERT(csv.find("1") != std::string::npos);
		ASSERT(csv.find("2") != std::string::npos);
	}
	// Array of objects: two-pass, union keys
	{
		asvJSON json;
		json.parse(std::string("[{\"x\":1},{\"y\":2}]"));
		std::string csv = json.toCSV();
		ASSERT(csv.find("x") != std::string::npos);
		ASSERT(csv.find("y") != std::string::npos);
		size_t posX = csv.find("1,\n");
		size_t posY = csv.find(",2");
		ASSERT(posX != std::string::npos || posY != std::string::npos);
		// Should have header row and two data rows
		int newlines = 0;
		for (auto c : csv) if (c == '\n') newlines++;
		ASSERT(newlines == 3);
	}
	// Mixed array -> "value" column
	{
		asvJSON json;
		json.parse(std::string("[1, \"two\", null]"));
		std::string csv = json.toCSV();
		ASSERT(csv.find("value") != std::string::npos);
		ASSERT(csv.find("1") != std::string::npos);
		ASSERT(csv.find("two") != std::string::npos);
	}
	// Comma/quotes in value -> proper escaping
	{
		asvJSON json;
		json.parse(std::string("{\"k\":\"a,b\\\"c\"}"));
		std::string csv = json.toCSV();
		ASSERT(csv.find("\"a,b\"\"c\"") != std::string::npos);
	}
	// asvJSONValue::toCSV (direct)
	{
		auto v = asvJSONValue::makeString("test", 4);
		std::string out;
		v->toCSV(out);
		ASSERT(out.find("test") != std::string::npos);
		ASSERT(out.find("value") == std::string::npos); // no header
	}
	// Empty object
	{
		asvJSON json;
		json.parse(std::string("{}"));
		std::string csv = json.toCSV();
		ASSERT(csv.empty());
	}
}

TEST(testFromCSV) {
  // Empty input
  {
    asvJSON json;
    bool ok = json.fromCSV(std::string_view(""));
    ASSERT(!ok);
  }
  // Basic array of objects
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("x,y\n1,2\n3,4")));
    ASSERT_EQ(json.getRoot()->arr->size(), 2U);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->get("x")->getInt(), 1);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->get("y")->getInt(), 2);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(1))->get("x")->getInt(), 3);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(1))->get("y")->getInt(), 4);
  }
  // Quoted field with comma
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("name,desc\n\"Smith, John\",\"a,b,c\"")));
    auto r = json.getRoot()->get(static_cast<size_t>(0));
    ASSERT_EQ(std::string(r->get("name")->getString()), "Smith, John");
    ASSERT_EQ(std::string(r->get("desc")->getString()), "a,b,c");
  }
  // Escaped quotes inside quoted field
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("k\n\"a\"\"b\"\"c\"")));
    ASSERT_EQ(std::string(json.getRoot()->get(static_cast<size_t>(0))->get("k")->getString()), "a\"b\"c");
  }
  // Type detection: int, double, bool, null, string
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("a,b,c,d,e\n42,3.14,TRUE,_,hello")));
    auto r = json.getRoot()->get(static_cast<size_t>(0));
    ASSERT_EQ(r->get("a")->getInt(), 42);
    ASSERT_EQ(r->get("b")->getDouble(), 3.14);
    ASSERT(r->get("c")->getBool());
    ASSERT(r->get("d")->type == asvJSONValue::NULL_VAL);
    ASSERT_EQ(std::string(r->get("e")->getString()), "hello");
  }
  // Leading plus sign detected as a number
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("a,b,c\n+42,+3.14,+x")));
    auto r = json.getRoot()->get(static_cast<size_t>(0));
    ASSERT_EQ(r->get("a")->getInt(), 42);
    ASSERT(r->get("b")->getDouble() > 3.13 && r->get("b")->getDouble() < 3.15);
    ASSERT_EQ(std::string(r->get("c")->getString()), "+x");
  }
  // Empty cell -> null
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("x,y\n1,\n,2")));
    ASSERT(json.getRoot()->get(static_cast<size_t>(0))->get("x")->getInt() == 1);
    ASSERT(json.getRoot()->get(static_cast<size_t>(0))->get("y")->type == asvJSONValue::NULL_VAL);
    ASSERT(json.getRoot()->get(static_cast<size_t>(1))->get("x")->type == asvJSONValue::NULL_VAL);
    ASSERT(json.getRoot()->get(static_cast<size_t>(1))->get("y")->getInt() == 2);
  }
  // Empty lines ignored
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("a\n1\n\n2\n")));
    ASSERT_EQ(json.getRoot()->arr->size(), 2U);
  }
  // CRLF line endings
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("a,b\r\n1,2\r\n3,4\r\n")));
    ASSERT_EQ(json.getRoot()->arr->size(), 2U);
  }
  // Missing fields -> null
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("a,b,c\n1,2")));
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->get("a")->getInt(), 1);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->get("b")->getInt(), 2);
    ASSERT(json.getRoot()->get(static_cast<size_t>(0))->get("c")->type == asvJSONValue::NULL_VAL);
  }
  // Single column named "value" -> handles array-of-scalars pattern
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("value\n1\nhello\n\n")));
    ASSERT_EQ(json.getRoot()->arr->size(), 2U);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->get("value")->getInt(), 1);
    ASSERT_EQ(std::string(json.getRoot()->get(static_cast<size_t>(1))->get("value")->getString()), "hello");
  }
  // Error: header only
  {
    asvJSON json;
    ASSERT(!json.fromCSV(std::string_view("a,b,c")));
  }
  // Multi-line quoted field (RFC 4180: newline inside quotes)
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("text\n\"hello\nworld\"\nfoo")));
    ASSERT_EQ(json.getRoot()->arr->size(), 2U);
    ASSERT_EQ(std::string(json.getRoot()->get(static_cast<size_t>(0))->get("text")->getString()), "hello\nworld");
    ASSERT_EQ(std::string(json.getRoot()->get(static_cast<size_t>(1))->get("text")->getString()), "foo");
  }
  // Multi-line with CRLF and quoted commas
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("a,b\r\n1,\"x\r\ny\"\r\n2,3")));
    ASSERT_EQ(json.getRoot()->arr->size(), 2U);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->get("a")->getInt(), 1);
    ASSERT_EQ(std::string(json.getRoot()->get(static_cast<size_t>(0))->get("b")->getString()), "x\r\ny");
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(1))->get("a")->getInt(), 2);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(1))->get("b")->getInt(), 3);
  }
  // Unclosed quote -> error
  {
    asvJSON json;
    ASSERT(!json.fromCSV(std::string_view("a\n\"unclosed")));
  }
}

TEST(testFromCSVIntOverflow) {
  // int64 overflow must be preserved as an exact literal (raw_number), not degraded to double
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("big\n99999999999999999999")));
    auto* v = json.getRoot()->get(static_cast<size_t>(0))->get("big");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT(v->raw_number);
    ASSERT_EQ(std::string(v->getString()), "99999999999999999999");
  }
  // Negative overflow
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("neg\n-99999999999999999999")));
    auto* v = json.getRoot()->get(static_cast<size_t>(0))->get("neg");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT(v->raw_number);
    ASSERT_EQ(std::string(v->getString()), "-99999999999999999999");
  }
  // In-range integers still INT; decimals still DOUBLE
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("a,b\n42,3.14")));
    ASSERT(json.getRoot()->get(static_cast<size_t>(0))->get("a")->type == asvJSONValue::INT);
    ASSERT_EQ(json.getRoot()->get(static_cast<size_t>(0))->get("a")->getInt(), int64_t(42));
    ASSERT(json.getRoot()->get(static_cast<size_t>(0))->get("b")->type == asvJSONValue::DOUBLE);
  }
  // Round-trip: toCSV preserves the exact literal
  {
    asvJSON json;
    ASSERT(json.fromCSV(std::string_view("big\n99999999999999999999")));
    std::string csv = json.toCSV();
    asvJSON json2;
    ASSERT(json2.fromCSV(std::string_view(csv)));
    auto* v = json2.getRoot()->get(static_cast<size_t>(0))->get("big");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT(v->raw_number);
    ASSERT_EQ(std::string(v->getString()), "99999999999999999999");
  }
}

TEST(testCSVRoundTrip) {
	// STRING literals that look like numbers/bools/null/empty must round-trip
	// as STRING, not be re-detected as another type on re-parse.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"a":"42","b":"3.14","c":"true","d":"null","e":"","f":"hello"})")));
		std::string csv = j.toCSV();
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		auto* r = j2.getRoot()->get(static_cast<size_t>(0));
		ASSERT(r->get("a")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("a")->getString()), "42");
		ASSERT(r->get("b")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("b")->getString()), "3.14");
		ASSERT(r->get("c")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("c")->getString()), "true");
		ASSERT(r->get("d")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("d")->getString()), "null");
		ASSERT(r->get("e")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("e")->getString()), "");
		ASSERT(r->get("f")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("f")->getString()), "hello");
	}
	// Empty string and null stay distinct across the round trip.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"k":""},{"k":null},{"k":"x"}])")));
		std::string csv = j.toCSV();
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		ASSERT_EQ(j2.getRoot()->arr->size(), 3U);
		ASSERT(j2.getRoot()->get(static_cast<size_t>(0))->get("k")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getRoot()->get(static_cast<size_t>(0))->get("k")->getString()), "");
		ASSERT(j2.getRoot()->get(static_cast<size_t>(1))->get("k")->type == asvJSONValue::NULL_VAL);
		ASSERT_EQ(std::string(j2.getRoot()->get(static_cast<size_t>(2))->get("k")->getString()), "x");
	}
	// Formula-injection guard (leading =+-@) must be stripped on decode so the
	// value and header keys round-trip losslessly.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"@x":1,"=y":2,"+z":3,"-w":"@v"}])")));
		std::string csv = j.toCSV();
		ASSERT(csv.find("'@x") != std::string::npos); // guard applied on output
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		auto* r = j2.getRoot()->get(static_cast<size_t>(0));
		ASSERT(r->hasKey("@x") && !r->hasKey("'@x"));
		ASSERT(r->hasKey("=y") && !r->hasKey("'=y"));
		ASSERT(r->hasKey("+z") && !r->hasKey("'+z"));
		ASSERT_EQ(r->get("@x")->getInt(), int64_t(1));
		ASSERT_EQ(r->get("=y")->getInt(), int64_t(2));
		ASSERT_EQ(r->get("+z")->getInt(), int64_t(3));
		ASSERT_EQ(std::string(r->get("-w")->getString()), "@v");
	}
	// A row whose single value is null (or empty string) must not be dropped.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"k":null}])")));
		std::string csv = j.toCSV();
		ASSERT(!csv.empty());
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		ASSERT_EQ(j2.getRoot()->arr->size(), 1U);
		ASSERT(j2.getRoot()->get(static_cast<size_t>(0))->get("k")->type == asvJSONValue::NULL_VAL);
	}
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"k":""}])")));
		asvJSON j2;
		ASSERT(j2.fromCSV(j.toCSV()));
		ASSERT_EQ(j2.getRoot()->arr->size(), 1U);
		ASSERT(j2.getRoot()->get(static_cast<size_t>(0))->get("k")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getRoot()->get(static_cast<size_t>(0))->get("k")->getString()), "");
	}
	// Array of scalars: empty string and null elements both survive.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"(["hello","","world"])")));
		asvJSON j2;
		ASSERT(j2.fromCSV(j.toCSV()));
		ASSERT_EQ(j2.getRoot()->arr->size(), 3U);
		ASSERT_EQ(std::string(j2.getRoot()->get(static_cast<size_t>(0))->get("value")->getString()), "hello");
		ASSERT(j2.getRoot()->get(static_cast<size_t>(1))->get("value")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(j2.getRoot()->get(static_cast<size_t>(1))->get("value")->getString()), "");
		ASSERT_EQ(std::string(j2.getRoot()->get(static_cast<size_t>(2))->get("value")->getString()), "world");
	}
	// Nested flatten with empty string round-trips.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"a":{"b":""},"c":1})")));
		asvJSON j2;
		ASSERT(j2.fromCSV(j.toCSV()));
		auto* r = j2.getRoot()->get(static_cast<size_t>(0));
		ASSERT(r->get("a.b")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("a.b")->getString()), "");
		ASSERT_EQ(r->get("c")->getInt(), int64_t(1));
	}
}

TEST(testCsvExtras) {
	// first data row of a parsed CSV (matches the get(0) convention used elsewhere)
	auto row = [](asvJSON& j) -> asvJSONValue* { return j.getRoot()->get(static_cast<size_t>(0)); };
	// Leading-zero bare numerals must stay STRINGs (JSON has no leading zeros),
	// otherwise "042" silently becomes 42 on a re-parse.
	{
		asvJSON j;
		ASSERT(j.fromCSV(std::string_view("a,b,c\n042,-042,+042")));
		auto* r = row(j);
		ASSERT(r->get("a")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("a")->getString()), "042");
		ASSERT(r->get("b")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("b")->getString()), "-042");
		ASSERT(r->get("c")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("c")->getString()), "+042");
		// "0" and "0.5" are still numbers
		ASSERT(j.fromCSV(std::string_view("a,b\n0,0.5")));
		auto* r2 = row(j);
		ASSERT(r2->get("a")->type == asvJSONValue::INT);
		ASSERT_EQ(r2->get("a")->getInt(), int64_t(0));
		ASSERT(r2->get("b")->type == asvJSONValue::DOUBLE);
		ASSERT(r2->get("b")->getDouble() > 0.49 && r2->get("b")->getDouble() < 0.51);
	}
	// Leading-zero STRINGs round-trip as STRING without quotes.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"zip":"01234"})")));
		std::string csv = j.toCSV();
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		ASSERT(row(j2)->get("zip")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(row(j2)->get("zip")->getString()), "01234");
	}
	// A UTF-8 BOM is stripped; the first header keeps its name.
	{
		asvJSON j;
		ASSERT(j.fromCSV(std::string("\xEF\xBB\xBF" "a,b\n1,2")));
		ASSERT(row(j)->get("a")->getInt() == 1);
		ASSERT(row(j)->get("b")->getInt() == 2);
	}
	// Ragged rows (more non-empty fields than headers) are rejected instead of
	// silently dropping data; a trailing comma only adds an empty field.
	{
		asvJSON j;
		ASSERT(!j.fromCSV(std::string_view("a,b\n1,2,3")));
		ASSERT(j.fromCSV(std::string_view("a,b\n1,2,")));
		ASSERT_EQ(j.getRoot()->arr->size(), 1U);
		// line index in the error message is 1-based data rows
		ASSERT(!j.fromCSV(std::string_view("a,b\n1,2\n3,4,5")));
		ASSERT(j.getLastError().find("row 2") != std::string::npos);
	}
	// Duplicate column headers are rejected instead of keeping one silently.
	{
		asvJSON j;
		ASSERT(!j.fromCSV(std::string_view("a,a\n1,2")));
		ASSERT(j.getLastError().find("duplicate column header") != std::string::npos);
	}
	// A value that starts with a guard apostrophe ("'=", "'+", "'-", "'@") is
	// quoted on output so the apostrophe is not mistaken for an injection guard.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"({"k":"'=foo","l":"'@v","m":"'+x"})")));
		std::string csv = j.toCSV();
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		auto* r = row(j2);
		ASSERT(r->get("k")->type == asvJSONValue::STRING);
		ASSERT_EQ(std::string(r->get("k")->getString()), "'=foo");
		ASSERT_EQ(std::string(r->get("l")->getString()), "'@v");
		ASSERT_EQ(std::string(r->get("m")->getString()), "'+x");
	}
	// The same applies to header keys: a key literally named "'@x" keeps its
	// apostrophe (it would be stripped as an injection guard otherwise).
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"'@x":1}])")));
		std::string csv = j.toCSV();
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		ASSERT(row(j2)->hasKey("'@x"));
		ASSERT(!row(j2)->hasKey("@x"));
		ASSERT_EQ(row(j2)->get("'@x")->getInt(), int64_t(1));
	}
	// Quoted headers carrying a guard character plus a comma survive the
	// round-trip ("=a,b" used to lose its '=' after quote stripping).
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"=a,b":3,"+c,d":4}])")));
		std::string csv = j.toCSV();
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		auto* r = row(j2);
		ASSERT(r->hasKey("=a,b"));
		ASSERT_EQ(r->get("=a,b")->getInt(), int64_t(3));
		ASSERT(r->hasKey("+c,d"));
		ASSERT_EQ(r->get("+c,d")->getInt(), int64_t(4));
	}
	// Injection-guarded and apostrophe-quoted keys stay distinct columns.
	{
		asvJSON j;
		ASSERT(j.parse(std::string_view(R"([{"@x":1,"'@x":2}])")));
		std::string csv = j.toCSV();
		asvJSON j2;
		ASSERT(j2.fromCSV(csv));
		auto* r = row(j2);
		ASSERT(r->hasKey("@x"));
		ASSERT(r->hasKey("'@x"));
		ASSERT_EQ(r->get("@x")->getInt(), int64_t(1));
		ASSERT_EQ(r->get("'@x")->getInt(), int64_t(2));
	}
}

TEST(testToTOML) {
  // Round-trip: simple object
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"name":"John","age":30})")));
    std::string toml = j.toTOML();
    ASSERT(!toml.empty());
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT_EQ(std::string(j2.getString("name")), "John");
    ASSERT_EQ(j2.getInt("age"), int64_t(30));
  }
  // Round-trip: nested object
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"owner":{"name":"Tom","age":30}})")));
    std::string toml = j.toTOML();
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT_EQ(std::string(j2.getString("owner.name")), "Tom");
    ASSERT_EQ(j2.getInt("owner.age"), int64_t(30));
  }
  // Round-trip: array of objects (wrapped in [[item]] for root array)
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"([{"x":1,"y":2},{"x":3,"y":4}])")));
    std::string toml = j.toTOML();
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT(j2.hasKey("item"));
    ASSERT_EQ(j2.getRoot()->get("item")->size(), size_t(2));
    ASSERT_EQ(j2.getRoot()->get("item")->get(static_cast<size_t>(0))->get("x")->getInt(), int64_t(1));
  }
  // Round-trip: boolean, null, float
  // Note: TOML has no null type, so null values are skipped
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"active":true,"empty":null,"pi":3.14})")));
    std::string toml = j.toTOML();
    ASSERT(toml.find("empty") == std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT(j2.getBool("active"));
    ASSERT(!j2.hasKey("empty"));
    ASSERT(j2.getDouble("pi") > 3.13 && j2.getDouble("pi") < 3.15);
  }
  // Empty object
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    std::string toml = j.toTOML();
    ASSERT(toml.empty());
  }
  // Root level array of primitives (wrapped as "items" key)
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"([1,2,3])")));
    std::string toml = j.toTOML();
    ASSERT(!toml.empty());
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT(j2.hasKey("items"));
    ASSERT_EQ(j2.getRoot()->get("items")->size(), size_t(3));
  }
  // Round-trip: a table's scalar members must be emitted before its child
  // [section] headers, else a sibling key after "[child]" leaks into the child
  // table ("y" would move inside "o").
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"o":{"a":1},"y":1})")));
    std::string toml = j.toTOML();
    // The root-level "y" must appear before the "[o]" header so it stays at
    // document root instead of being scoped into table o.
    size_t yPos = toml.find("y = 1");
    size_t oPos = toml.find("[o]");
    ASSERT(yPos != std::string::npos && oPos != std::string::npos);
    ASSERT(yPos < oPos);
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT_EQ(j2.getInt("y"), int64_t(1));
    ASSERT(j2.hasKey("o"));
    ASSERT_EQ(j2.getInt("o.a"), int64_t(1));
    ASSERT(!j2.getRoot()->get("o")->hasKey("y"));
  }
  // Same invariant with a null-collapsed subtable and a trailing root sibling.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"o":{"x":null},"y":1})")));
    std::string toml = j.toTOML();
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT_EQ(j2.getInt("y"), int64_t(1));
    ASSERT(!j2.getRoot()->get("o")->hasKey("y"));
  }
  // Keys containing control characters must be quoted/escaped so that the
  // emitted TOML round-trips (a raw newline in a quoted key is invalid TOML).
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"a\nb":1,"c\td":2,"e\re":3})")));
    std::string toml = j.toTOML();
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT_EQ(j2.getInt("a\nb"), int64_t(1));
    ASSERT_EQ(j2.getInt("c\td"), int64_t(2));
    ASSERT_EQ(j2.getInt("e\re"), int64_t(3));
  }
  {
    // Key with the DEL character (0x7F) -- must be escaped, not emitted raw.
    asvJSON j;
    ASSERT(j.parse(std::string_view("{\"a\x7f""b\":1}")));
    std::string toml = j.toTOML();
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT_EQ(j2.getInt("a\x7f""b"), int64_t(1));
  }
  // Values with DEL (0x7F) must escape to \\u007F so the decoder (which rejects
  // a raw 0x7F in a basic string) can read them back.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{\"s\":\"a\x7f""b\"}")));
    std::string toml = j.toTOML();
    ASSERT(toml.find("\\u007F") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(toml)));
    ASSERT_EQ(j2.getString("s"), std::string("a\x7f""b"));
  }
}

TEST(testFromTOML) {
  // Direct parsing: simple key=value
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(name = "Alice"
age = 30)")));
    ASSERT_EQ(std::string(j.getString("name")), "Alice");
    ASSERT_EQ(j.getInt("age"), int64_t(30));
  }
  // Direct parsing: table
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"([owner]
name = "Bob"
age = 25)")));
    ASSERT_EQ(std::string(j.getString("owner.name")), "Bob");
    ASSERT_EQ(j.getInt("owner.age"), int64_t(25));
  }
  // Direct parsing: inline table
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(point = {x = 1, y = 2})")));
    ASSERT_EQ(j.getInt("point.x"), int64_t(1));
    ASSERT_EQ(j.getInt("point.y"), int64_t(2));
  }
  // Direct parsing: boolean
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(flag = true)")));
    ASSERT(j.getBool("flag"));
  }
  // Direct parsing: array of tables
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"([[products]]
name = "Hammer"
price = 10
[[products]]
name = "Nail"
price = 2)")));
    ASSERT_EQ(j.getRoot()->hasKey("products"), true);
    ASSERT_EQ(j.getRoot()->getConst("products")->size(), size_t(2));
    ASSERT_EQ(std::string(j.getRoot()->getConst("products")->get(static_cast<size_t>(0))->getConst("name")->getString()), "Hammer");
    ASSERT_EQ(j.getRoot()->getConst("products")->get(static_cast<size_t>(1))->getConst("price")->getInt(), int64_t(2));
  }
  // Empty input
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("")));
  }
  // Numbers: integer
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("n = 42")));
    ASSERT_EQ(j.getInt("n"), int64_t(42));
  }
  // Numbers: float
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("n = 3.14")));
    ASSERT(j.getDouble("n") > 3.13 && j.getDouble("n") < 3.15);
  }
  // Quoted key
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"("key with spaces" = "value")")));
    ASSERT_EQ(std::string(j.getString("key with spaces")), "value");
  }
  // Dotted key
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(a.b.c = 42)")));
    ASSERT_EQ(j.getInt("a.b.c"), int64_t(42));
  }
  // Literal string
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(path = 'C:\Windows\System32')")));
    ASSERT_EQ(std::string(j.getString("path")), "C:\\Windows\\System32");
  }
  // Multi-line basic string
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(str = """
line1
line2""")")));
    ASSERT_EQ(std::string(j.getString("str")), "line1\nline2");
  }
  // Interior quote runs (1-2) in multi-line strings are content, not closers
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(str = """x"y""")")));
    ASSERT_EQ(std::string(j.getString("str")), std::string("x\"y"));
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(str = """x""y""")")));
    ASSERT_EQ(std::string(j.getString("str")), std::string("x\"\"y"));
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(str = '''x'y''')")));
    ASSERT_EQ(std::string(j.getString("str")), std::string("x'y"));
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(str = '''x''y''')")));
    ASSERT_EQ(std::string(j.getString("str")), std::string("x''y"));
  }
  // Quote runs of 4-5 at the end: leading quotes content, final 3 close
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(str = """x"""" )")));
    ASSERT_EQ(std::string(j.getString("str")), std::string("x\""));
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view(R"(str = """x""""")")));
    ASSERT_EQ(std::string(j2.getString("str")), std::string("x\"\""));
  }
  // Empty multi-line string """""" is an empty string, not a continuation
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(str = """""" )")));
    ASSERT_EQ(std::string(j.getString("str")), std::string(""));
  }
  // Hex number
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("n = 0xFF")));
    ASSERT_EQ(j.getInt("n"), int64_t(255));
  }
  // Octal number
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("n = 0o77")));
    ASSERT_EQ(j.getInt("n"), int64_t(63));
  }
  // Binary number
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("n = 0b1010")));
    ASSERT_EQ(j.getInt("n"), int64_t(10));
  }
  // Inline array
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("arr = [1, 2, 3]")));
    ASSERT_EQ(j.getRoot()->getConst("arr")->size(), size_t(3));
    ASSERT_EQ(j.getRoot()->getConst("arr")->get(static_cast<size_t>(0))->getInt(), int64_t(1));
  }
  // Comment
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(# this is a comment
key = "value" # inline comment)")));
    ASSERT_EQ(std::string(j.getString("key")), "value");
  }
  // Inline table with dotted keys (merged into correct nested structure)
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view(R"(point = {x.y = 1, x.z = 2})")));
    ASSERT_EQ(j.getInt("point.x.y"), int64_t(1));
    ASSERT_EQ(j.getInt("point.x.z"), int64_t(2));
  }
  // Local time HH:MM:SS (with optional fractional seconds)
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("lt = 07:32:00")));
    ASSERT_EQ(std::string(j.getString("lt")), "07:32:00");
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("lt = 07:32:00.999")));
    ASSERT_EQ(std::string(j.getString("lt")), "07:32:00.999");
  }
  // Local time in an inline array and an inline table
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("a = [07:32:00]")));
    ASSERT_EQ(std::string(j.getRoot()->getConst("a")->get(static_cast<size_t>(0))->getString()), "07:32:00");
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("it = {t = 07:32:00}")));
    ASSERT_EQ(std::string(j.getString("it.t")), "07:32:00");
  }
  // Offset datetimes preserve their timezone offset (not normalized to UTC Z)
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("d = 2024-01-15T10:30:00+03:00")));
    ASSERT_EQ(std::string(j.getString("d")), "2024-01-15T10:30:00+03:00");
    // UTC epoch stays correct despite the preserved display offset
    ASSERT_EQ(j.getDateTime("d"), int64_t(1705303800));
    ASSERT(j.serialize().find("2024-01-15T10:30:00+03:00") != std::string::npos);
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("d = 1979-05-27T07:32:00-07:00")));
    ASSERT_EQ(std::string(j.getString("d")), "1979-05-27T07:32:00-07:00");
  }
  // Offset datetime with fractional seconds
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("d = 1979-05-27T07:32:00.999-07:00")));
    ASSERT(j.serialize().find(".999-07:00") != std::string::npos);
  }
  // Z offset remains Z (UTC)
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("d = 1979-05-27T07:32:00Z")));
    ASSERT_EQ(std::string(j.getString("d")), "1979-05-27T07:32:00Z");
  }
  // Trailing comma in inline arrays is a parse error (per TOML spec)
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = [1, 2,]")));
    ASSERT(!j.fromTOML(std::string_view("a = [1,]")));
    ASSERT(!j.fromTOML(std::string_view("a = [1, ]")));
    ASSERT(!j.fromTOML(std::string_view("a = [[1,2],]")));
  }
  // Missing array element between commas is a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = [1,,2]")));
  }
  // Trailing comma in inline tables is a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = {x = 1,}")));
    ASSERT(!j.fromTOML(std::string_view("a = {x = 1, }")));
    ASSERT(!j.fromTOML(std::string_view("a = {b = {c = 1,}}")));
  }
  // Empty key segment in dotted key paths is a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view(".a = 1")));
    ASSERT(!j.fromTOML(std::string_view("a..b = 1")));
    ASSERT(!j.fromTOML(std::string_view("a. = 1")));
    ASSERT(!j.fromTOML(std::string_view("a.b. = 1")));
    ASSERT(!j.fromTOML(std::string_view("[a.]\nx = 1")));
  }
  // Trailing content after a value is a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = 5 hello")));
    ASSERT(!j.fromTOML(std::string_view("a = true false")));
    ASSERT(!j.fromTOML(std::string_view("a = \"hi\" extra")));
    ASSERT(!j.fromTOML(std::string_view("a = 0xFF zz")));
    ASSERT(!j.fromTOML(std::string_view("a = 07:32:00 xx")));
    ASSERT(!j.fromTOML(std::string_view("a = inf zz")));
    // trailing content must not break valid space-separated date-time
    {
      asvJSON j2;
      ASSERT(j2.fromTOML(std::string_view("a = 1979-05-27 07:32:00")));
    }
  }
  // Leading zeros in numbers are a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = 07")));
    ASSERT(!j.fromTOML(std::string_view("a = 007")));
    ASSERT(!j.fromTOML(std::string_view("a = -007.5")));
    ASSERT(j.fromTOML(std::string_view("a = 0")));
    ASSERT(j.fromTOML(std::string_view("a = 0x0")));
  }
  // Numeric overflow is a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = 1e99999")));
    ASSERT(!j.fromTOML(std::string_view("a = -1e99999")));
    ASSERT(!j.fromTOML(std::string_view("a = 99999999999999999999")));
    ASSERT(j.fromTOML(std::string_view("a = 9223372036854775807")));
  }
  // Sign before hex/octal/binary is a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = -0xFF")));
    ASSERT(!j.fromTOML(std::string_view("a = +0xFF")));
    ASSERT(!j.fromTOML(std::string_view("a = -0o17")));
    ASSERT(!j.fromTOML(std::string_view("a = +0b101")));
    ASSERT(j.fromTOML(std::string_view("a = 0xFF")));
  }
  // Invalid Unicode escapes in basic strings are a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = \"\\uZZZZ\"")));
    ASSERT(!j.fromTOML(std::string_view("a = \"\\U000G0000\"")));
    ASSERT(!j.fromTOML(std::string_view("a = \"\\u12\"")));
    ASSERT(!j.fromTOML(std::string_view("a = \"\\U0001F6\"")));
    ASSERT(!j.fromTOML(std::string_view("a = \"\\uD83D\"")));
    ASSERT(!j.fromTOML(std::string_view("a = \"\\uDE00\"")));
    ASSERT(!j.fromTOML(std::string_view("a = \"\\U00110000\"")));
    // valid escapes including an astral char and a surrogate pair
    ASSERT(j.fromTOML(std::string_view("a = \"\\u0041\"")));
    ASSERT(j.fromTOML(std::string_view("a = \"\\U0001F600\"")));
    ASSERT(j.fromTOML(std::string_view("a = \"\\uD83D\\uDE00\"")));
    {
      asvJSON j2;
      ASSERT(j2.fromTOML(std::string_view("a = \"\\uD83D\\uDE00\"")));
      std::string val = j2.getString("a");
      // U+1F600 encoded as UTF-8 (F0 9F 98 80)
      ASSERT_EQ(val, std::string("\xF0\x9F\x98\x80"));
    }
  }
  // Trailing content after a table header is a parse error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("[a.b] z [c]")));
    ASSERT(!j.fromTOML(std::string_view("[a.b]z")));
    ASSERT(!j.fromTOML(std::string_view("[[a]] x")));
    ASSERT(!j.fromTOML(std::string_view("[a] x")));
  }
  // A ']' inside a quoted table key is not the header closer
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("[a.\"b]c\"]\nx = 1")));
    ASSERT_EQ(j.getRoot()->get("a")->get("b]c")->get("x")->getInt(), int64_t(1));
  }
  // Empty quoted keys are preserved
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("\"\" = 1")));
    ASSERT_EQ(j.getRoot()->get("")->getInt(), int64_t(1));
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view("a.\"\" = 2")));
    ASSERT_EQ(j2.getRoot()->get("a")->get("")->getInt(), int64_t(2));
  }
  // Multiline arrays: comma-optional newlines, inline comments, trailing comma
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("a = [\n1\n2\n]")));
    ASSERT_EQ(j.getRoot()->get("a")->size(), size_t(2));
    ASSERT_EQ(j.getRoot()->get("a")->get(static_cast<size_t>(0))->getInt(), int64_t(1));
    ASSERT_EQ(j.getRoot()->get("a")->get(static_cast<size_t>(1))->getInt(), int64_t(2));
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("a = [1,\n2, # trailing\n3]")));
    ASSERT_EQ(j.getRoot()->get("a")->size(), size_t(3));
  }
  {
    // Trailing comma is legal in a multiline array
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("a = [1,\n2,\n]")));
    ASSERT_EQ(j.getRoot()->get("a")->size(), size_t(2));
  }
  // Unclosed multiline array is an error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = [1,\n2,3")));
  }
  // Separator comma between inline-table entries (was mis-rejected as
  // 'invalid key: unexpected character')
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("point = {x = 1, y = 2}")));
    ASSERT_EQ(j.getInt("point.x"), int64_t(1));
    ASSERT_EQ(j.getInt("point.y"), int64_t(2));
  }
  // Nested inline table with a quoted brace stays balanced
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("a = {b = \"x}y\", c = 1}")));
    ASSERT_EQ(std::string(j.getString("a.b")), "x}y");
    ASSERT_EQ(j.getInt("a.c"), int64_t(1));
  }
  // Missing comma between inline-table pairs is an error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = {x = 1 y = 2}")));
  }
  // Non-ASCII bare keys are accepted
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("ключ = \"значение\"")));
    ASSERT_EQ(std::string(j.getString("ключ")), "значение");
    asvJSON j2;
    ASSERT(j2.fromTOML(std::string_view("[раздел]\nключ = 1")));
    ASSERT_EQ(j2.getInt("раздел.ключ"), int64_t(1));
  }
  // Strict floats: dot-leading, dot-trailing, empty exponent are errons
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = .5")));
    ASSERT(!j.fromTOML(std::string_view("a = 5.")));
    ASSERT(!j.fromTOML(std::string_view("a = 5.e20")));
    ASSERT(!j.fromTOML(std::string_view("a = 1e")));
    ASSERT(!j.fromTOML(std::string_view("a = 1e+")));
  }
  // Strict integers: leading zeros / underscores errors
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = 007")));
    ASSERT(!j.fromTOML(std::string_view("a = _1")));
    ASSERT(!j.fromTOML(std::string_view("a = 1_")));
    ASSERT(!j.fromTOML(std::string_view("a = 1__0")));
    ASSERT(!j.fromTOML(std::string_view("a = 1_.5")));
    ASSERT(!j.fromTOML(std::string_view("a = 1e2_2"))); // underscore banned in exponent
    ASSERT(j.fromTOML(std::string_view("a = 1_000")));
    ASSERT(j.fromTOML(std::string_view("a = 0xff_ff")));
  }
  // inf / nan are case-sensitive lowercase only
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("a = inf")));
    ASSERT(std::isinf(j.getDouble("a")) && j.getDouble("a") > 0);
    ASSERT(j.fromTOML(std::string_view("a = -inf")));
    ASSERT(std::isinf(j.getDouble("a")) && j.getDouble("a") < 0);
    ASSERT(j.fromTOML(std::string_view("a = nan")));
    ASSERT(std::isnan(j.getDouble("a")));
    ASSERT(!j.fromTOML(std::string_view("a = INF")));
    ASSERT(!j.fromTOML(std::string_view("a = NAN")));
    ASSERT(!j.fromTOML(std::string_view("a = infx")));
  }
  // Unterminated / unknown-escape strings throw
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = \"abc")));
    ASSERT(!j.fromTOML(std::string_view("a = 'abc")));
    ASSERT(!j.fromTOML(std::string_view("a = \"abc\\")));
    ASSERT(!j.fromTOML(std::string_view("a = \"x\\q\"") ));
    ASSERT(!j.fromTOML(std::string_view("a = \"x\\/\"") ));
    ASSERT(!j.fromTOML(std::string_view("a = \"\"\"abc")));
    ASSERT(!j.fromTOML(std::string_view("a = '''abc")));
    // Trailing content after a closed multiline string is an error
    ASSERT(!j.fromTOML(std::string_view("a = \"\"\"x\"\"\" y")));
  }
  // Table redefinition guards
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("[a]\nb=1\n[a]\nc=2")));
    // a plain header super-table may be reopened (implicit tables)
    ASSERT(j.fromTOML(std::string_view("[a.b]\nc=1\n[a]\nd=2")));
    // ... but a dotted-key super-table may not be redefined by a header
    ASSERT(!j.fromTOML(std::string_view("[fruit]\napple.color = \"red\"\n[fruit.apple]\nsmooth = true")));
    // a header may still add a sub-table inside a dotted-key table
    ASSERT(j.fromTOML(std::string_view("[fruit]\napple.color = \"red\"\n[fruit.apple.texture]\nsmooth = true")));
  }
  // Array-of-tables: repeated headers, sub-tables, nested sub-arrays
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("[[a]]\nb=1\n[[a]]\nb=2")));
    ASSERT_EQ(j.getRoot()->get("a")->size(), size_t(2));
  }
  {
    asvJSON j;
    ASSERT(j.fromTOML(std::string_view("[[fruits]]\nname=\"apple\"\n[fruits.physical]\ncolor=\"red\"\n[[fruits.varieties]]\nname=\"red\"\n[[fruits.varieties]]\nname=\"granny\"\n[[fruits]]\nname=\"banana\"")));
    auto arr = j.getRoot()->get("fruits");
    ASSERT_EQ(arr->size(), size_t(2));
    ASSERT_EQ(std::string(arr->get(static_cast<size_t>(0))->get("physical")->get("color")->getString()), "red");
    ASSERT_EQ(arr->get(static_cast<size_t>(0))->get("varieties")->size(), size_t(2));
    ASSERT_EQ(arr->get(static_cast<size_t>(1))->get("name")->getString(), std::string("banana"));
  }
  // Appending to a statically-defined array is an error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("fruits = []\n[[fruits]]\nx = 1")));
  }
  // A key/value pair must end at EOF/line: two pairs on one line is an error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("a = 1 b = 2")));
    // ... and so is a colon-only pair (no '=')
    ASSERT(!j.fromTOML(std::string_view("a: 1")));
  }
  // A key with no value is an error
  {
    asvJSON j;
    ASSERT(!j.fromTOML(std::string_view("x =")));
  }
}

TEST(testToJSONLines) {
  // Array of objects -> one JSON line per element
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"([{"a":1,"b":2},{"a":3,"b":4}])")));
    std::string jl = j.toJSONLines();
    ASSERT(!jl.empty());
    asvJSON j2;
    ASSERT(j2.fromJSONLines(std::string_view(jl)));
    ASSERT_EQ(j2.getRoot()->size(), size_t(2));
    ASSERT_EQ(j2.getRoot()->get(static_cast<size_t>(0))->get("a")->getInt(), int64_t(1));
    ASSERT_EQ(j2.getRoot()->get(static_cast<size_t>(1))->get("b")->getInt(), int64_t(4));
  }
  // Single object -> one line
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"x":42})")));
    std::string jl = j.toJSONLines();
    std::string expected = R"({"x":42})" "\n";
    ASSERT_EQ(jl, expected);
  }
}

TEST(testFromJSONLines) {
  // Multiple JSON lines
  {
    asvJSON j;
    ASSERT(j.fromJSONLines(std::string_view(R"({"a":1}
{"a":2}
{"a":3}
)")));
    ASSERT_EQ(j.getRoot()->size(), size_t(3));
    ASSERT_EQ(j.getRoot()->get(static_cast<size_t>(0))->get("a")->getInt(), int64_t(1));
    ASSERT_EQ(j.getRoot()->get(static_cast<size_t>(2))->get("a")->getInt(), int64_t(3));
  }
  // Windows line endings
  {
    asvJSON j;
    ASSERT(j.fromJSONLines(std::string_view("{\"b\":1}\r\n{\"b\":2}\r\n")));
    ASSERT_EQ(j.getRoot()->size(), size_t(2));
  }
  // Empty lines should be skipped
  {
    asvJSON j;
    ASSERT(j.fromJSONLines(std::string_view("{\"c\":1}\n\n{\"c\":3}\n")));
    ASSERT_EQ(j.getRoot()->size(), size_t(2));
  }
  // Error on invalid JSON
  {
    asvJSON j;
    ASSERT(!j.fromJSONLines(std::string_view("not valid json")));
  }
}

TEST(testToSexpr) {
  // Basic object round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"name":"John","age":30,"active":true})")));
    std::string s = j.toSexpr();
    ASSERT(!s.empty());
    ASSERT_EQ(s.front(), '(');
    ASSERT_EQ(s.back(), ')');
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("name")), "John");
    ASSERT_EQ(j2.getInt("age"), int64_t(30));
    ASSERT_EQ(j2.getBool("active"), true);
  }
  // Array round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[10,20,30]")));
    std::string s = j.toSexpr();
    ASSERT_EQ(s, "(10 20 30)");
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(j2.getRoot()->size(), size_t(3));
    ASSERT_EQ(j2.getRoot()->get(static_cast<size_t>(0))->getInt(), int64_t(10));
  }
  // Nested object round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{\"name\":\"John\",\"address\":{\"city\":\"NYC\"}}")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("name")), "John");
    ASSERT_EQ(std::string(j2.getString("address.city")), "NYC");
  }
  // Null, bool, int, double
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"s":"hi","b":false,"n":null,"i":42,"d":3.14})")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("s")), "hi");
    ASSERT_EQ(j2.getBool("b"), false);
    ASSERT(j2.isNull("n"));
    ASSERT_EQ(j2.getInt("i"), int64_t(42));
    ASSERT(j2.getDouble("d") > 3.13 && j2.getDouble("d") < 3.15);
  }
  // Empty object
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    std::string s = j.toSexpr();
    ASSERT_EQ(s, "(() #t)");
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(j2.getRoot()->type, asvJSONValue::OBJECT);
    ASSERT_EQ(j2.getRoot()->size(), size_t(0));
  }
  // Empty array
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[]")));
    std::string s = j.toSexpr();
    ASSERT_EQ(s, "()");
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(j2.getRoot()->size(), size_t(0));
  }
  // Nested arrays
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"([[1,2],[3,4]])")));
    std::string s = j.toSexpr();
    ASSERT(j.fromSexpr(std::string_view(s)));
    ASSERT_EQ(j.getRoot()->size(), size_t(2));
  }
  // Comments
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(name \"John\" ; this is a comment\n age 30)")));
    ASSERT_EQ(std::string(j.getString("name")), "John");
    ASSERT_EQ(j.getInt("age"), int64_t(30));
  }
  // Symbol keys
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(first-name \"Alice\" last-name \"Bob\")")));
    ASSERT_EQ(std::string(j.getString("first-name")), "Alice");
    ASSERT_EQ(std::string(j.getString("last-name")), "Bob");
  }
  // nil / #t / #f
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(a nil b #t c #f)")));
    ASSERT(j.isNull("a"));
    ASSERT_EQ(j.getBool("b"), true);
    ASSERT_EQ(j.getBool("c"), false);
  }
  // Numeric keys must be quoted in output
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"123":"value","-42":"neg","true":"bool"})")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("123")), "value");
    ASSERT_EQ(std::string(j2.getString("-42")), "neg");
    ASSERT_EQ(std::string(j2.getString("true")), "bool");
  }
}

TEST(testFromSexpr) {
  // Simple object
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(name \"John\" age 30)")));
    ASSERT_EQ(std::string(j.getString("name")), "John");
    ASSERT_EQ(j.getInt("age"), int64_t(30));
  }
  // Nested
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(person (name \"Alice\" age 25))")));
    ASSERT_EQ(std::string(j.getString("person.name")), "Alice");
    ASSERT_EQ(j.getInt("person.age"), int64_t(25));
  }
  // Pure array
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(1 2 3)")));
    ASSERT_EQ(j.getRoot()->size(), size_t(3));
    ASSERT_EQ(j.getRoot()->get(static_cast<size_t>(0))->getInt(), int64_t(1));
  }
  // Mixed: object with array values
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(numbers (1 2 3) flags (#t #f))")));
    ASSERT_EQ(j.getRoot()->getConst("numbers")->size(), size_t(3));
    ASSERT_EQ(j.getRoot()->getConst("flags")->size(), size_t(2));
    ASSERT_EQ(j.getRoot()->getConst("flags")->get(static_cast<size_t>(0))->getBool(), true);
  }
  // Empty
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("()")));
    ASSERT_EQ(j.getRoot()->size(), size_t(0));
  }
  // Numbers
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(a 42 b -3 c 3.14)")));
    ASSERT_EQ(j.getInt("a"), int64_t(42));
    ASSERT_EQ(j.getInt("b"), int64_t(-3));
  }
  // Standard Lisp form: nested objects with mixed types
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(root (name \"asvJSON++\") (version \"1.12.0\") (active true) (tags \"json\" \"c++\"))")));
    ASSERT_EQ(std::string(j.getString("root.name")), "asvJSON++");
    ASSERT_EQ(std::string(j.getString("root.version")), "1.12.0");
    ASSERT(j.getConst("root")->getConst("active")->getBool());
    ASSERT_EQ(j.getRoot()->getConst("root")->getConst("tags")->size(), size_t(2));
    ASSERT_EQ(std::string(j.getRoot()->getConst("root")->getConst("tags")->get(static_cast<size_t>(0))->getString()), "json");
    ASSERT_EQ(std::string(j.getRoot()->getConst("root")->getConst("tags")->get(static_cast<size_t>(1))->getString()), "c++");
  }
  // Quoted-string keys parsed as object
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view(R"(("name" "John" "age" 30))")));
    ASSERT_EQ(std::string(j.getString("name")), "John");
    ASSERT_EQ(j.getInt("age"), int64_t(30));
  }
  // Two quoted strings: array, not object (round-trip fidelity)
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view(R"(("key" "val"))")));
    ASSERT(j.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j.getRoot()->size(), size_t(2));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "key");
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(1))->getString()), "val");
  }
  // Quoted-string keys with special chars round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"hello world":"test","123":"num"})")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("hello world")), "test");
    ASSERT_EQ(std::string(j2.getString("123")), "num");
  }
  // Quoted strings are data, not keys - odd-length group stays an array
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view(R"((items ("apple" "banana" "cherry") count 3))")));
    ASSERT(j.isArray("items"));
    auto* arr = j.getArray("items");
    ASSERT(arr != nullptr);
    ASSERT_EQ(arr->size(), size_t(3));
    ASSERT_EQ(std::string(arr->get(0)->getString()), "apple");
    ASSERT_EQ(std::string(arr->get(1)->getString()), "banana");
    ASSERT_EQ(std::string(arr->get(2)->getString()), "cherry");
    ASSERT_EQ(j.getInt("count"), int64_t(3));
  }
  // Error on empty
  {
    asvJSON j;
    ASSERT(!j.fromSexpr(std::string_view("")));
  }
  // #true / #false are SYMBOLS, not booleans (only #t / #f are booleans)
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(a #true b #false)")));
    ASSERT_EQ(std::string(j.getString("a")), "#true");
    ASSERT_EQ(std::string(j.getString("b")), "#false");
  }
  // Trailing dot or bare exponent rejected as number
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(a 1.)")));
    ASSERT_EQ(std::string(j.getString("a")), "1.");
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view("(b 1e)")));
    ASSERT_EQ(std::string(j2.getString("b")), "1e");
    asvJSON j3;
    ASSERT(j3.fromSexpr(std::string_view("(c 1E)")));
    ASSERT_EQ(std::string(j3.getString("c")), "1E");
  }
  // Unknown escape sequences preserve backslash
  {
    asvJSON j;
    std::string in1 = R"(("a\q41b"))";
    ASSERT(j.fromSexpr(std::string_view(in1)));
    ASSERT(j.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "a\\q41b");
  }
  // \u (4-digit) and \U (8-digit) Unicode escapes decode to the code point
  {
    asvJSON j;
    std::string in2 = R"("x\u0041")";
    ASSERT(j.fromSexpr(std::string_view(in2)));
    ASSERT_EQ(std::string(j.getRoot()->getString()), "xA");

    asvJSON j2;
    std::string in3 = R"("x\U0001F600")";
    ASSERT(j2.fromSexpr(std::string_view(in3)));
    ASSERT_EQ(std::string(j2.getRoot()->getString()), "x\xF0\x9F\x98\x80");

    // é = U+00E9, 3-byte UTF-8
    asvJSON j3;
    std::string in4 = R"("x\u00e9")";
    ASSERT(j3.fromSexpr(std::string_view(in4)));
    ASSERT_EQ(std::string(j3.getRoot()->getString()), "x\xC3\xA9");
  }
  // Round-trip: non-ASCII survives toSexpr -> fromSexpr
  {
    asvJSON j;
    std::string in5 = R"({"a":"x\u00e9","b":"y\u0001F600"})";
    ASSERT(j.parse(std::string_view(in5)));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("a")), "x\u00e9");
    ASSERT_EQ(std::string(j2.getString("b")), "y\u0001F600");
  }
}

// Regression: S-Expression key-marker (\x01) collision and \u/\U surrogate handling
TEST(testSexprAudit) {
  // A VALUE string beginning with byte 0x01 must round-trip as data, not be
  // misread as a marked key.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"a":"\u0001foo"})")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("a")), "\u0001foo");
  }
  // The same value as the FIRST item of a nested array placed under a key.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"k":["\u0001foo",2]})")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.isArray("k"));
    ASSERT_EQ(j2.getConst("k")->size(), size_t(2));
    ASSERT_EQ(std::string(j2.getConst("k")->get(static_cast<size_t>(0))->getString()), "\u0001foo");
    ASSERT_EQ(j2.getConst("k")->get(static_cast<size_t>(1))->getInt(), int64_t(2));
  }
  // A KEY that itself begins with byte 0x01 (encoder double-marks).
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"\u0001foo":"v"})")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("\u0001foo")), "v");
  }
  // Hand-written surrogate pair is combined into the supplementary code point.
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(a \"x\\uD83D\\uDE00y\")")));
    ASSERT_EQ(std::string(j.getString("a")), "x\U0001F600y");
  }
  // A lone surrogate is preserved literally (backslash no longer dropped).
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("(a \"x\\uD83Dy\")")));
    ASSERT_EQ(std::string(j.getString("a")), "x\\uD83Dy");
  }
}

TEST(testSexprSpecialTypes) {
  // Special types used to be serialized to `nil` (silently lost) on a
  // round-trip. They now serialize via the Extended JSON conventions and the
  // decoder's JSON pass reconstructs the typed values.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    uint8_t bin[] = {0xde, 0xad, 0xbe, 0xef};
    j.getRoot()->obj->emplace("bin", asvJSONValue::makeBinary(bin, 4));
    j.getRoot()->obj->emplace("oid", asvJSONValue::makeObjectId("1234567890ab"));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("foo.*bar", "gi"));
    j.getRoot()->obj->emplace("ts", asvJSONValue::makeTimestamp(1234567890));
    j.getRoot()->obj->emplace("ext", asvJSONValue::makeExtension(127, bin, 4));
    j.getRoot()->obj->emplace("dt", asvJSONValue::makeDateTime(1700000000, 123, 0));
    std::string s = j.toSexpr();
    ASSERT(!s.empty());
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    auto* bin2 = j2.getRoot()->getConst("bin");
    ASSERT(bin2 != nullptr && bin2->type == asvJSONValue::BINARY);
    ASSERT_EQ(bin2->bin_data.size(), size_t(4));
    ASSERT_EQ(bin2->bin_data[0], 0xde);
    ASSERT_EQ(bin2->bin_data[3], 0xef);
    auto* oid = j2.getRoot()->getConst("oid");
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("1234567890ab"));
    auto* re = j2.getRoot()->getConst("re");
    ASSERT(re != nullptr && re->type == asvJSONValue::REGEX);
    ASSERT_EQ(j2.getRegexPattern("re"), "foo.*bar");
    ASSERT_EQ(j2.getRegexOptions("re"), "gi");
    auto* ts = j2.getRoot()->getConst("ts");
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(1234567890));
    auto* ext = j2.getRoot()->getConst("ext");
    ASSERT(ext != nullptr && ext->type == asvJSONValue::EXTENSION);
    ASSERT_EQ(ext->ext_type, 127);
    ASSERT_EQ(ext->bin_data.size(), size_t(4));
    auto* dt = j2.getRoot()->getConst("dt");
    ASSERT(dt != nullptr && dt->type == asvJSONValue::DATETIME);
    ASSERT_EQ(dt->timestamp, int64_t(1700000000));
  }
  // Special types nested inside an array also survive.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[]")));
    j.getRoot()->arr->push_back(asvJSONValue::makeObjectId("abcdefabcdef"));
    j.getRoot()->arr->push_back(asvJSONValue::makeTimestamp(999));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot() != nullptr && j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->size(), size_t(2));
    auto* oid = j2.getRoot()->get(static_cast<size_t>(0));
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("abcdefabcdef"));
    auto* ts = j2.getRoot()->get(static_cast<size_t>(1));
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(999));
  }
  // A value-only regex with empty options still round-trips.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("^abc$", ""));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT_EQ(j2.getRegexPattern("re"), "^abc$");
    ASSERT_EQ(j2.getRegexOptions("re"), "");
  }
}

TEST(testSexprExtras) {
  // Even-length scalar arrays (>=4 elements) used to decode as key/value
  // objects. The encoder now emits an ARRAY marker so they round-trip as arrays.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[1,2,3,4]")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->size(), size_t(4));
    ASSERT_EQ(j2.getRoot()->get(3)->getInt(), int64_t(4));
  }
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"(["a","b","c","d"])")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->get(2)->getString(), std::string("c"));
  }
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[true,false,true,false]")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->get(1)->getBool(), false);
  }
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"([1,"a",2,"b"])")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->get(1)->getString(), std::string("a"));
  }
  // Nested even-length scalar arrays keep their nesting.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[[10,20,30,40]]")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT(j2.getRoot()->get(0)->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->get(0)->size(), size_t(4));
  }
  // Small and odd-sized arrays are unaffected.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[1,2]")));
    std::string s = j.toSexpr();
    ASSERT_EQ(s, "(1 2)");
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot()->type == asvJSONValue::ARRAY);
  }
  // Objects with scalar values still decode as objects (control).
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"a":1,"b":2,"c":3,"d":4})")));
    std::string s = j.toSexpr();
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    ASSERT(j2.getRoot()->type == asvJSONValue::OBJECT);
    ASSERT_EQ(j2.getInt("d"), int64_t(4));
  }
  // DATETIME with a nonzero UTC offset survives (was flattened to Z).
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("dt", asvJSONValue::makeDateTime(1599999999, 0, 120));
    std::string s = j.toSexpr();
    ASSERT(s.find("+02:00") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromSexpr(std::string_view(s)));
    auto* dt = j2.getRoot()->getConst("dt");
    ASSERT(dt != nullptr && dt->type == asvJSONValue::DATETIME);
    ASSERT_EQ(dt->timestamp, int64_t(1599999999));
    ASSERT_EQ(dt->tz_offset_min, 120);
  }
  // UTF-8 BOM at the start of the input is skipped.
  {
    asvJSON j;
    ASSERT(j.fromSexpr(std::string_view("\xEF\xBB\xBF(name \"John\")")));
    ASSERT_EQ(std::string(j.getString("name")), "John");
  }
}

TEST(testTOONSpecialTypes) {
  // Special types used to be serialized to `null` (silently lost) on a
  // round-trip. They now serialize via the Extended JSON conventions and the
  // decoder's JSON pass reconstructs the typed values.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    uint8_t bin[] = {0xde, 0xad, 0xbe, 0xef};
    j.getRoot()->obj->emplace("bin", asvJSONValue::makeBinary(bin, 4));
    j.getRoot()->obj->emplace("oid", asvJSONValue::makeObjectId("1234567890ab"));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("foo.*bar", "gi"));
    j.getRoot()->obj->emplace("ts", asvJSONValue::makeTimestamp(1234567890));
    j.getRoot()->obj->emplace("ext", asvJSONValue::makeExtension(127, bin, 4));
    j.getRoot()->obj->emplace("dt", asvJSONValue::makeDateTime(1700000000, 123, 0));
    std::string s = j.toTOON();
    ASSERT(!s.empty());
    asvJSON j2;
    ASSERT(j2.fromTOON(std::string_view(s)));
    auto* bin2 = j2.getRoot()->getConst("bin");
    ASSERT(bin2 != nullptr && bin2->type == asvJSONValue::BINARY);
    ASSERT_EQ(bin2->bin_data.size(), size_t(4));
    ASSERT_EQ(bin2->bin_data[0], 0xde);
    ASSERT_EQ(bin2->bin_data[3], 0xef);
    auto* oid = j2.getRoot()->getConst("oid");
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("1234567890ab"));
    auto* re = j2.getRoot()->getConst("re");
    ASSERT(re != nullptr && re->type == asvJSONValue::REGEX);
    ASSERT_EQ(j2.getRegexPattern("re"), "foo.*bar");
    ASSERT_EQ(j2.getRegexOptions("re"), "gi");
    auto* ts = j2.getRoot()->getConst("ts");
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(1234567890));
    auto* ext = j2.getRoot()->getConst("ext");
    ASSERT(ext != nullptr && ext->type == asvJSONValue::EXTENSION);
    ASSERT_EQ(ext->ext_type, 127);
    ASSERT_EQ(ext->bin_data.size(), size_t(4));
    auto* dt = j2.getRoot()->getConst("dt");
    ASSERT(dt != nullptr && dt->type == asvJSONValue::DATETIME);
    ASSERT_EQ(dt->timestamp, int64_t(1700000000));
  }
  // Special types nested inside an array survive (rendered in list form so the
  // convention objects are not split on their internal commas).
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[]")));
    j.getRoot()->arr->push_back(asvJSONValue::makeObjectId("abcdefabcdef"));
    j.getRoot()->arr->push_back(asvJSONValue::makeTimestamp(999));
    uint8_t b2[] = {0x01, 0x02, 0x03};
    j.getRoot()->arr->push_back(asvJSONValue::makeBinary(b2, 3));
    std::string s = j.toTOON();
    asvJSON j2;
    ASSERT(j2.fromTOON(std::string_view(s)));
    ASSERT(j2.getRoot() != nullptr && j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->size(), size_t(3));
    auto* oid = j2.getRoot()->get(static_cast<size_t>(0));
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("abcdefabcdef"));
    auto* ts = j2.getRoot()->get(static_cast<size_t>(1));
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(999));
    auto* bin3 = j2.getRoot()->get(static_cast<size_t>(2));
    ASSERT(bin3 != nullptr && bin3->type == asvJSONValue::BINARY);
    ASSERT_EQ(bin3->bin_data.size(), size_t(3));
    ASSERT_EQ(bin3->bin_data[2], 0x03);
  }
  // A value-only regex with empty options still round-trips.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("^abc$", ""));
    std::string s = j.toTOON();
    asvJSON j2;
    ASSERT(j2.fromTOON(std::string_view(s)));
    ASSERT_EQ(j2.getRegexPattern("re"), "^abc$");
    ASSERT_EQ(j2.getRegexOptions("re"), "");
  }
}

TEST(testTRONSpecialTypes) {
  // Special types used to be serialized without a recoverable marker (ObjectId
  // as a bare hex string -> STRING, Regex as "pattern|opts" -> STRING, Timestamp
  // as a bare number -> INT), silently losing the type on round-trip. They now
  // serialize via the __OID__/__REGEX__/__TS__ prefix conventions (matching the
  // GOON encoder) and the decoder reconstructs the typed values.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    uint8_t bin[] = {0xde, 0xad, 0xbe, 0xef};
    j.getRoot()->obj->emplace("bin", asvJSONValue::makeBinary(bin, 4));
    j.getRoot()->obj->emplace("oid", asvJSONValue::makeObjectId("1234567890ab"));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("foo.*bar", "gi"));
    j.getRoot()->obj->emplace("ts", asvJSONValue::makeTimestamp(1234567890));
    j.getRoot()->obj->emplace("ext", asvJSONValue::makeExtension(127, bin, 4));
    j.getRoot()->obj->emplace("dt", asvJSONValue::makeDateTime(1700000000, 123, 0));
    std::string s = j.toTRON();
    ASSERT(!s.empty());
    asvJSON j2;
    ASSERT(j2.fromTRON(std::string_view(s)));
    auto* bin2 = j2.getRoot()->getConst("bin");
    ASSERT(bin2 != nullptr && bin2->type == asvJSONValue::BINARY);
    ASSERT_EQ(bin2->bin_data.size(), size_t(4));
    ASSERT_EQ(bin2->bin_data[0], 0xde);
    ASSERT_EQ(bin2->bin_data[3], 0xef);
    auto* oid = j2.getRoot()->getConst("oid");
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("1234567890ab"));
    auto* re = j2.getRoot()->getConst("re");
    ASSERT(re != nullptr && re->type == asvJSONValue::REGEX);
    ASSERT_EQ(j2.getRegexPattern("re"), "foo.*bar");
    ASSERT_EQ(j2.getRegexOptions("re"), "gi");
    auto* ts = j2.getRoot()->getConst("ts");
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(1234567890));
    auto* ext = j2.getRoot()->getConst("ext");
    ASSERT(ext != nullptr && ext->type == asvJSONValue::EXTENSION);
    ASSERT_EQ(ext->ext_type, 127);
    ASSERT_EQ(ext->bin_data.size(), size_t(4));
    auto* dt = j2.getRoot()->getConst("dt");
    ASSERT(dt != nullptr && dt->type == asvJSONValue::DATETIME);
    ASSERT_EQ(dt->timestamp, int64_t(1700000000));
  }
  // Special types nested inside an array survive.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[]")));
    j.getRoot()->arr->push_back(asvJSONValue::makeObjectId("abcdefabcdef"));
    j.getRoot()->arr->push_back(asvJSONValue::makeTimestamp(999));
    std::string s = j.toTRON();
    asvJSON j2;
    ASSERT(j2.fromTRON(std::string_view(s)));
    ASSERT(j2.getRoot() != nullptr && j2.getRoot()->type == asvJSONValue::ARRAY);
    ASSERT_EQ(j2.getRoot()->size(), size_t(2));
    auto* oid = j2.getRoot()->get(static_cast<size_t>(0));
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("abcdefabcdef"));
    auto* ts = j2.getRoot()->get(static_cast<size_t>(1));
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(999));
  }
  // A regex whose pattern contains a '|' preserves the split on the LAST '|'.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("a|b", "im"));
    std::string s = j.toTRON();
    asvJSON j2;
    ASSERT(j2.fromTRON(std::string_view(s)));
    auto* re = j2.getRoot()->getConst("re");
    ASSERT(re != nullptr && re->type == asvJSONValue::REGEX);
    ASSERT_EQ(j2.getRegexPattern("re"), "a|b");
    ASSERT_EQ(j2.getRegexOptions("re"), "im");
  }
  // A value-only regex with empty options still round-trips.
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("^abc$", ""));
    std::string s = j.toTRON();
    asvJSON j2;
    ASSERT(j2.fromTRON(std::string_view(s)));
    ASSERT_EQ(j2.getRegexPattern("re"), "^abc$");
    ASSERT_EQ(j2.getRegexOptions("re"), "");
  }
}


TEST(testToJSON5) {
  // Basic object round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"name":"John","age":30,"active":true})")));
    std::string s = j.toJSON5();
    ASSERT(!s.empty());
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("name")), "John");
    ASSERT_EQ(j2.getInt("age"), int64_t(30));
    ASSERT_EQ(j2.getBool("active"), true);
  }
  // Array round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("[10,20,30]")));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    ASSERT_EQ(j2.getRoot()->size(), size_t(3));
    ASSERT_EQ(j2.getRoot()->get(static_cast<size_t>(0))->getInt(), int64_t(10));
  }
  // Nested object
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{\"a\":{\"b\":{\"c\":42}}}")));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    ASSERT_EQ(j2.getInt("a.b.c"), int64_t(42));
  }
  // Special types serialize as null (not lost)
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"s":"hi","b":false,"n":null,"i":42})")));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    ASSERT_EQ(std::string(j2.getString("s")), "hi");
    ASSERT_EQ(j2.getBool("b"), false);
    ASSERT(j2.isNull("n"));
    ASSERT_EQ(j2.getInt("i"), int64_t(42));
  }
  // Output uses unquoted keys for simple identifiers
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view(R"({"simpleKey":1,"_ok":2,"$valid":3})")));
    std::string s = j.toJSON5();
    ASSERT(s.find("\"simpleKey\"") == std::string::npos);
    ASSERT(s.find("simpleKey") != std::string::npos);
  }
  // Special types round-trip (Extended JSON)
  {
    asvJSON j;
    j.putObjectId("oid", std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c", 12));
    j.putRegex("rx", "^test$", "gi");
    uint8_t bin[] = {0xde, 0xad};
    j.putBinary("bin", bin, 2);
    j.putExtension("ext", 42, bin, 2);
    j.putDateTime("dt", 1705314645);
    j.putInt("ts", 1712345678);
    std::string s = j.toJSON5();
    ASSERT(!s.empty());
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    auto* oid = j2.getRoot()->get("oid");
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c", 12));
    ASSERT_EQ(j2.getRegexPattern("rx"), "^test$");
    ASSERT_EQ(j2.getRegexOptions("rx"), "gi");
    auto* bin2 = j2.getRoot()->get("bin");
    ASSERT(bin2 != nullptr && bin2->type == asvJSONValue::BINARY);
    ASSERT_EQ(bin2->bin_data.size(), size_t(2));
    ASSERT_EQ(bin2->bin_data[0], 0xde);
    ASSERT_EQ(bin2->bin_data[1], 0xad);
    auto* ext2 = j2.getRoot()->get("ext");
    ASSERT(ext2 != nullptr && ext2->type == asvJSONValue::EXTENSION);
    ASSERT_EQ(ext2->ext_type, 42);
    ASSERT_EQ(ext2->bin_data.size(), size_t(2));
    auto* dt2 = j2.getRoot()->get("dt");
    ASSERT(dt2 != nullptr && dt2->type == asvJSONValue::DATETIME);
    ASSERT_EQ(dt2->timestamp, 1705314645);
  }
}

TEST(testFromJSON5) {
  // Single-quoted strings
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'name':'John','age':30}")));
    ASSERT_EQ(std::string(j.getString("name")), "John");
    ASSERT_EQ(j.getInt("age"), int64_t(30));
  }
  // Unquoted keys
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{name:\"John\",age:30}")));
    ASSERT_EQ(std::string(j.getString("name")), "John");
    ASSERT_EQ(j.getInt("age"), int64_t(30));
  }
  // Trailing commas
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'a':1,'b':2,}")));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
    ASSERT_EQ(j.getInt("b"), int64_t(2));
  }
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[1,2,3,]")));
    ASSERT_EQ(j.getRoot()->size(), size_t(3));
  }
  // Hex numbers
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'val':0xFF}")));
    ASSERT_EQ(j.getInt("val"), int64_t(255));
  }
  // Overflowing hex / octal / binary numbers are rejected, not silently wrapped
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("{'val':0xFFFFFFFFFFFFFFFFFFFFFFFF}")));
    ASSERT(!j.fromJSON5(std::string_view("{'val':0o77777777777777777777777777}")));
    ASSERT(!j.fromJSON5(std::string_view("{'val':0b11111111111111111111111111111111111111111111111111111111111111111}")));
  }
  // Octal numbers
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'val':0o77}")));
    ASSERT_EQ(j.getInt("val"), int64_t(63));
  }
  // Binary numbers
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'val':0b1010}")));
    ASSERT_EQ(j.getInt("val"), int64_t(10));
  }
  // Leading decimal
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'val':.5}")));
    ASSERT(j.getDouble("val") > 0.49 && j.getDouble("val") < 0.51);
  }
  // Plus sign
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'val':+42}")));
    ASSERT_EQ(j.getInt("val"), int64_t(42));
  }
  // Mixed JSON5 features
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{unquoted:'single',\"double\":0xFF,trailing:0o10,}")));
    ASSERT_EQ(std::string(j.getString("unquoted")), "single");
    ASSERT_EQ(j.getInt("double"), int64_t(255));
    ASSERT_EQ(j.getInt("trailing"), int64_t(8));
  }
  // Comments (already handled by JSON parser)
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{'a':1 // inline\n,'b':2}")));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
    ASSERT_EQ(j.getInt("b"), int64_t(2));
  }
  // Error on empty
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("")));
  }
  // Parse Extended JSON (MongoDB format)
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(R"json5({"$oid":"0102030405060708090a0b0c"})json5")));
    auto* oid = j.getRoot();
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
  }
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(R"json5({"$regex":"^test$","$options":"gi"})json5")));
    auto [pat, opt] = j.getNestedRegex("");
    ASSERT_EQ(pat, "^test$");
    ASSERT_EQ(opt, "gi");
  }
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(R"json5({"$timestamp":{"t":1712345678}})json5")));
    auto* ts = j.getRoot();
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(1712345678));
  }
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(R"json5({"$binary":{"base64":"3q0=","subType":"00"}})json5")));
    auto* bin = j.getRoot();
    ASSERT(bin != nullptr && bin->type == asvJSONValue::BINARY);
    ASSERT_EQ(bin->bin_data.size(), size_t(2));
    ASSERT_EQ(bin->bin_data[0], 0xde);
    ASSERT_EQ(bin->bin_data[1], 0xad);
  }
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(R"json5({"$binary":{"base64":"3q0=","subType":"2a"}})json5")));
    auto* ext = j.getRoot();
    ASSERT(ext != nullptr && ext->type == asvJSONValue::EXTENSION);
    ASSERT_EQ(ext->ext_type, 42);
    ASSERT_EQ(ext->bin_data.size(), size_t(2));
    ASSERT_EQ(ext->bin_data[0], 0xde);
    ASSERT_EQ(ext->bin_data[1], 0xad);
  }
  // Trailing decimal point: 5. -> 5.0
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[5.]")));
    ASSERT_EQ(j.getRoot()->size(), size_t(1));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->type == asvJSONValue::DOUBLE ||
          j.getRoot()->get(static_cast<size_t>(0))->type == asvJSONValue::INT);
  }
  // Trailing decimal with exponent: 5.e10 -> 5.0e10
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[5.e10]")));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->type == asvJSONValue::DOUBLE);
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->getDouble() > 4.99e10);
  }
  // Line continuation in double-quoted strings
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"hello\\\nworld\"]")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "helloworld");
  }
  // Line continuation in single-quoted strings
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("['hello\\\nworld']")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "helloworld");
  }
  // Line continuation with CRLF
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"hello\\\r\nworld\"]")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "helloworld");
  }
  // +Infinity literal
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[+Infinity]")));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->type == asvJSONValue::DOUBLE);
    ASSERT(std::isinf(j.getRoot()->get(static_cast<size_t>(0))->getDouble()));
  }
  // +NaN literal
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[+NaN]")));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->type == asvJSONValue::DOUBLE);
    ASSERT(std::isnan(j.getRoot()->get(static_cast<size_t>(0))->getDouble()));
  }
  // -NaN literal
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[-NaN]")));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->type == asvJSONValue::DOUBLE);
    ASSERT(std::isnan(j.getRoot()->get(static_cast<size_t>(0))->getDouble()));
  }
  // Extended whitespace: VT, FF between tokens
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{\x0b\x0c}")));
  }
  // Extended whitespace: NBSP between tokens
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\xC2\xA0 1]")));
    ASSERT_EQ(j.getRoot()->get(static_cast<size_t>(0))->getInt(), int64_t(1));
  }
  // Unicode identifier key (Japanese: 日本語)
  {
    asvJSON j;
    const char json5src[] = "{'hello':{" "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" ":'val'}}";
    ASSERT(j.fromJSON5(std::string_view(json5src, sizeof(json5src) - 1)));
    auto* h = j.get("hello");
    ASSERT(h != nullptr && h->type == asvJSONValue::OBJECT);
    std::string key = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    ASSERT_EQ(std::string(j.getString(std::string_view("hello." + key))), "val");
  }
  // \\v escape -> vertical tab
  {
    asvJSON j;
    const char inp[] = { '[', '"', 'a', '\\', 'v', 'b', '"', ']', 0 };
    ASSERT(j.fromJSON5(std::string_view(inp, 8)));
    auto* v = j.getRoot()->get(static_cast<size_t>(0));
    ASSERT(v != nullptr);
    ASSERT_EQ(v->type, asvJSONValue::STRING);
    std::string_view sv = v->str_data;
    ASSERT_EQ(sv.size(), size_t(3));
    ASSERT_EQ(sv[0], 'a');
    ASSERT_EQ(static_cast<unsigned char>(sv[1]), '\v');
    ASSERT_EQ(sv[2], 'b');
  }
  // \\v in single-quoted strings
  {
    asvJSON j;
    const char inp[] = { '[', '\'', 'a', '\\', 'v', 'b', '\'', ']', 0 };
    ASSERT(j.fromJSON5(std::string_view(inp, 8)));
    auto* v = j.getRoot()->get(static_cast<size_t>(0));
    ASSERT(v != nullptr);
    ASSERT_EQ(v->type, asvJSONValue::STRING);
    std::string_view sv = v->str_data;
    ASSERT_EQ(sv.size(), size_t(3));
    ASSERT_EQ(sv[0], 'a');
    ASSERT_EQ(sv[1], '\v');
    ASSERT_EQ(sv[2], 'b');
  }
  // \\0 escape -> null character
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"a\\0b\"]")));
    auto* v = j.getRoot()->get(static_cast<size_t>(0));
    ASSERT(v != nullptr);
    ASSERT_EQ(v->type, asvJSONValue::STRING);
    std::string_view sv = v->str_data;
    ASSERT_EQ(sv.size(), size_t(3));
    ASSERT_EQ(sv[0], 'a');
    ASSERT_EQ(sv[1], '\0');
    ASSERT_EQ(sv[2], 'b');
  }
  // \\x41 escape -> 'A'
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"\\x41\"]")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "A");
  }
  // \\x escape in single-quoted strings
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("['\\x41']")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "A");
  }
  // Raw control character in string (tab)
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"a\tb\"]")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "a\tb");
  }
  // Raw control character in single-quoted string (newline)
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("['a\nb']")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "a\nb");
  }
  // Sign + whitespace: - Infinity
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[- Infinity]")));
    ASSERT(std::isinf(j.getRoot()->get(static_cast<size_t>(0))->getDouble()));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->getDouble() < 0);
  }
  // Sign + whitespace: + Infinity
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[+ Infinity]")));
    ASSERT(std::isinf(j.getRoot()->get(static_cast<size_t>(0))->getDouble()));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->getDouble() > 0);
  }
  // Sign + whitespace: + NaN
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[+ NaN]")));
    ASSERT(std::isnan(j.getRoot()->get(static_cast<size_t>(0))->getDouble()));
  }
  // Sign + whitespace: + 5
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[+ 5]")));
    ASSERT_EQ(j.getRoot()->get(static_cast<size_t>(0))->getInt(), int64_t(5));
  }
  // Sign + whitespace: - 5
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[- 5]")));
    ASSERT_EQ(j.getRoot()->get(static_cast<size_t>(0))->getInt(), int64_t(-5));
  }
  // Sign + whitespace: - .5
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[- .5]")));
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->getDouble() > -0.51);
    ASSERT(j.getRoot()->get(static_cast<size_t>(0))->getDouble() < -0.49);
  }
  // \\u standard escape (\\u0041 -> 'A')
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"\\u0041\"]")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "A");
  }
  // \\u{...} BMP codepoint (\\u{41} -> 'A')
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"\\u{41}\"]")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "A");
  }
  // \\u{...} supplementary plane (\\u{1F600} -> U+1F600)
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"\\u{1F600}\"]")));
    std::string s = j.getRoot()->get(static_cast<size_t>(0))->getString();
    ASSERT_EQ(s.size(), size_t(4));
    // U+1F600 (GRINNING FACE) in UTF-8: F0 9F 98 80
    ASSERT_EQ(static_cast<unsigned char>(s[0]), 0xF0);
    ASSERT_EQ(static_cast<unsigned char>(s[1]), 0x9F);
    ASSERT_EQ(static_cast<unsigned char>(s[2]), 0x98);
    ASSERT_EQ(static_cast<unsigned char>(s[3]), 0x80);
  }
  // \\u{...} in single-quoted strings
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("['\\u{1F600}']")));
    std::string s = j.getRoot()->get(static_cast<size_t>(0))->getString();
    ASSERT_EQ(s.size(), size_t(4));
    ASSERT_EQ(static_cast<unsigned char>(s[0]), 0xF0);
    ASSERT_EQ(static_cast<unsigned char>(s[1]), 0x9F);
    ASSERT_EQ(static_cast<unsigned char>(s[2]), 0x98);
    ASSERT_EQ(static_cast<unsigned char>(s[3]), 0x80);
  }
  // \\u{...} zero codepoint
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"a\\u{0}b\"]")));
    auto* v = j.getRoot()->get(static_cast<size_t>(0));
    ASSERT(v != nullptr);
    ASSERT_EQ(v->str_data.size(), size_t(3));
    ASSERT_EQ(v->str_data[0], 'a');
    ASSERT_EQ(v->str_data[1], '\0');
    ASSERT_EQ(v->str_data[2], 'b');
  }
  // \\0 followed by digit should fail (no octal)
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"a\\01b\"]")));
  }
  // \\x without 2 hex digits should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\xZZ\"]")));
  }
  // \\x with single hex digit should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\x4\"]")));
  }
  // \\u without 4 hex digits should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\u12\"]")));
  }
  // \\u with invalid hex digits should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\u00ZZ\"]")));
  }
  // \\u{...} with no hex digits should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\u{}\"]")));
  }
  // \\u{...} without closing brace should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\u{41\"]")));
  }
  // \\u{...} with invalid hex should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\u{XYZ1}\"]")));
  }
  // \\u{...} with codepoint > 0x10FFFF should fail
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"\\u{110000}\"]")));
  }
  // Control character (U+0001) outside string should fail
  {
    asvJSON j;
    const char inp[] = { '[', 0x01, ']', 0 };
    ASSERT(!j.fromJSON5(std::string_view(inp, 3)));
  }
  // Control character (U+0008 = \\b) outside string should fail
  {
    asvJSON j;
    const char inp[] = { '[', 0x08, ']', 0 };
    ASSERT(!j.fromJSON5(std::string_view(inp, 3)));
  }
  // Allowed whitespace control chars outside string: tab, newline, vt, ff
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"a\",\t\"b\"]")));
  }
  {
    asvJSON j;
    const char inp[] = { '[', '\"', 'a', '\"', ',', '\v', '\"', 'b', '\"', ']', 0 };
    ASSERT(j.fromJSON5(std::string_view(inp, 10)));
  }
  // Zs space: U+2003 (EM SPACE) between tokens
  {
    asvJSON j;
    const char inp[] = { '[', '\"', 'a', '\"', ',', static_cast<char>(0xE2), static_cast<char>(0x80), static_cast<char>(0x83), '\"', 'b', '\"', ']', 0 };
    ASSERT(j.fromJSON5(std::string_view(inp, 12)));
  }
  // Zs space: U+3000 (IDEOGRAPHIC SPACE) between tokens
  {
    asvJSON j;
    const char inp[] = { '[', '\"', 'a', '\"', ',', (char)0xE3, (char)0x80, (char)0x80, '\"', 'b', '\"', ']', 0 };
    ASSERT(j.fromJSON5(std::string_view(inp, 12)));
  }
  // Zs space: U+205F (MEDIUM MATHEMATICAL SPACE) between tokens
  {
    asvJSON j;
    const char inp[] = { '[', '\"', 'a', '\"', ',', (char)0xE2, (char)0x81, (char)0x9F, '\"', 'b', '\"', ']', 0 };
    ASSERT(j.fromJSON5(std::string_view(inp, 12)));
  }
  // Multiple Zs spaces before unquoted key
  {
    asvJSON j;
    const char inp[] = { '{', (char)0xE2, (char)0x80, (char)0x83, (char)0xC2, (char)0xA0, 'k', 'e', 'y', ':', '1', '}', 0 };
    ASSERT(j.fromJSON5(std::string_view(inp, 12)));
    ASSERT_EQ(j.getRoot()->getConst("key")->getInt(), int64_t(1));
  }
  // -NaN preserves the negative sign through serialization (round-trip)
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{\"x\":-NaN}")));
    std::string s = j.toJSON5();
    ASSERT(s.find("-NaN") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    auto* v = j2.getRoot()->getConst("x");
    ASSERT(v != nullptr && v->type == asvJSONValue::DOUBLE);
    ASSERT(std::isnan(v->getDouble()));
    ASSERT(std::signbit(v->getDouble()));
  }
  // +NaN serializes as plain NaN
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("{\"x\":+NaN}")));
    std::string s = j.toJSON5();
    ASSERT(s.find("NaN") != std::string::npos);
    ASSERT(s.find("-NaN") == std::string::npos);
  }
}

TEST(testFromJSON5Conformance) {
  // Unknown escape drops the backslash (JSON5 Sec 5.1 example 5: '\A\C\/\D\C' == 'AC/DC')
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(R"(['\A\C\/\D\C'])")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "AC/DC");
  }
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("[\"\\A\"]")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "A");
  }
  // The escape: `\'` stays the same character in both quote styles
  {
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view("['\\\'']")));
    ASSERT_EQ(std::string(j.getRoot()->get(static_cast<size_t>(0))->getString()), "'");
  }
  // Decimal digit (1-9) after backslash must be rejected (no octal escapes)
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("['\\7']")));
  }
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[\"a\\9b\"]")));
  }
  // A radix literal has no fraction part
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[0xFF.5]")));
  }
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[0b11.]")));
  }
  // A trailing comma must follow a value (not an open bracket / em-dash / colon)
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[,]")));
  }
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("{,}")));
  }
  {
    asvJSON j;
    ASSERT(!j.fromJSON5(std::string_view("[1,,]")));
  }
  // Multibyte whitespace is a token separator, not part of an identifier key
  {
    const char src[] = "{\xe2\x80\xa8k\xe2\x80\xa9" ":1}";
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(src, sizeof(src) - 1)));
    ASSERT_EQ(j.size(), size_t(1));
    auto* k = j.getRoot()->get("k");
    ASSERT(k != nullptr && k->type == asvJSONValue::INT);
    ASSERT_EQ(k->num, int64_t(1));
  }
  {
    const char src[] = "{k\xef\xbb\xbf" ":1}";
    asvJSON j;
    ASSERT(j.fromJSON5(std::string_view(src, sizeof(src) - 1)));
    ASSERT_EQ(j.size(), size_t(1));
    auto* k = j.getRoot()->get("k");
    ASSERT(k != nullptr && k->type == asvJSONValue::INT);
  }
}

TEST(testJSON5Audit) {
  // REGEX round-trip: the encoder used to bind a std::string_view to the dead
  // temporary from str_data.substr(...), leaving a dangling view into freed
  // memory that corrupted the emitted pattern/options (e.g. "\u0000oo.*bar").
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("foo.*bar", "gi"));
    std::string s = j.toJSON5();
    // pattern and options must survive intact (no leading \u0000 corruption)
    ASSERT(s.find("\"$regex\":\"foo.*bar\"") != std::string::npos);
    ASSERT(s.find("\"$options\":\"gi\"") != std::string::npos);
    ASSERT(s.find("\\u0000") == std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    ASSERT_EQ(j2.getRegexPattern("re"), "foo.*bar");
    ASSERT_EQ(j2.getRegexOptions("re"), "gi");
  }
  // Empty options: only the $regex member is emitted, still intact
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("re", asvJSONValue::makeRegex("^start$", ""));
    std::string s = j.toJSON5();
    ASSERT(s.find("\"$regex\":\"^start$\"") != std::string::npos);
    ASSERT(s.find("\\u0000") == std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    ASSERT_EQ(j2.getRegexPattern("re"), "^start$");
    ASSERT_EQ(j2.getRegexOptions("re"), "");
  }
  // Special types still round-trip through toJSON5/fromJSON5
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    uint8_t bin[] = {0xde, 0xad, 0xbe, 0xef};
    j.getRoot()->obj->emplace("oid", asvJSONValue::makeObjectId("1234567890ab"));
    j.getRoot()->obj->emplace("bin", asvJSONValue::makeBinary(bin, 4));
    j.getRoot()->obj->emplace("ts", asvJSONValue::makeTimestamp(1234567890));
    j.getRoot()->obj->emplace("ext", asvJSONValue::makeExtension(127, bin, 4));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    auto* oid = j2.getRoot()->getConst("oid");
    ASSERT(oid != nullptr && oid->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(std::string_view(oid->str_data), std::string_view("1234567890ab"));
    auto* bin2 = j2.getRoot()->getConst("bin");
    ASSERT(bin2 != nullptr && bin2->type == asvJSONValue::BINARY);
    ASSERT_EQ(bin2->bin_data.size(), size_t(4));
    ASSERT_EQ(bin2->bin_data[0], 0xde);
    ASSERT_EQ(bin2->bin_data[3], 0xef);
    auto* ts = j2.getRoot()->getConst("ts");
    ASSERT(ts != nullptr && ts->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(ts->num, int64_t(1234567890));
    auto* ext = j2.getRoot()->getConst("ext");
    ASSERT(ext != nullptr && ext->type == asvJSONValue::EXTENSION);
    ASSERT_EQ(ext->ext_type, 127);
  }
  // NaN / Infinity values survive a JSON5 round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("nan", asvJSONValue::makeDouble(std::numeric_limits<double>::quiet_NaN()));
    j.getRoot()->obj->emplace("ginf", asvJSONValue::makeDouble(std::numeric_limits<double>::infinity()));
    j.getRoot()->obj->emplace("ninf", asvJSONValue::makeDouble(-std::numeric_limits<double>::infinity()));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    auto* nan = j2.getRoot()->getConst("nan");
    ASSERT(nan != nullptr && nan->type == asvJSONValue::DOUBLE && std::isnan(nan->dbl));
    auto* ginf = j2.getRoot()->getConst("ginf");
    ASSERT(ginf != nullptr && ginf->type == asvJSONValue::DOUBLE && std::isinf(ginf->dbl) && ginf->dbl > 0);
    auto* ninf = j2.getRoot()->getConst("ninf");
    ASSERT(ninf != nullptr && ninf->type == asvJSONValue::DOUBLE && std::isinf(ninf->dbl) && ninf->dbl < 0);
  }
}

TEST(testJSON5Extras) {
  // Escape round-trip through double-quoted strings: \n \t \r \b \f \\ \"
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    std::string val("a\nb\tc\rd\be\ff\\g\"h", 15);
    j.getRoot()->obj->emplace("s", asvJSONValue::makeString(val.data(), (int)val.size()));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    auto* v = j2.getRoot()->getConst("s");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT(v->str_data == val);
  }
  // Escaped \n \t \r \b \f and \' inside single-quoted JSON5 strings
  {
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view("{'s':'a\\nb\\tc\\rd\\be\\ff\\\\g\\''}")));
    auto* v = j2.getRoot()->getConst("s");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    std::string want("a\nb\tc\rd\be\ff\\g'", 14);
    ASSERT(v->str_data == want);
  }
  // Empty binary / extension payloads keep their types through a JSON5 round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("b", asvJSONValue::makeBinary(nullptr, 0));
    j.getRoot()->obj->emplace("e", asvJSONValue::makeExtension(7, nullptr, 0));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    auto* b = j2.getRoot()->getConst("b");
    ASSERT(b != nullptr && b->type == asvJSONValue::BINARY && b->bin_data.empty());
    auto* e = j2.getRoot()->getConst("e");
    ASSERT(e != nullptr && e->type == asvJSONValue::EXTENSION && e->bin_data.empty() && e->ext_type == 7);
  }
  // A $regex-looking plain object with extra members stays a plain object:
  // REGEX needs the sole member, or the $regex+$options pair
  {
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view("{$timestamp:5,$regex:\"x\"}")));
    auto* v = j2.getRoot();
    ASSERT(v != nullptr && v->type == asvJSONValue::OBJECT);
    ASSERT(v->obj->find("$timestamp") != v->obj->end());
    ASSERT(v->obj->find("$regex") != v->obj->end());
    ASSERT(v->getConst("$regex")->type == asvJSONValue::STRING);
  }
  // DATETIME with a nonzero UTC offset survives a JSON5 round-trip
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("dt", asvJSONValue::makeDateTime(1599999999, 0, 120));
    std::string s = j.toJSON5();
    ASSERT(s.find("+02:00") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    auto* dt = j2.getRoot()->getConst("dt");
    ASSERT(dt != nullptr && dt->type == asvJSONValue::DATETIME);
    ASSERT_EQ(dt->timestamp, int64_t(1599999999));
    ASSERT_EQ(dt->tz_offset_min, 120);
  }
  // Sole $regex with $options still decodes to a REGEX value
  {
    asvJSON j;
    ASSERT(j.parse(std::string_view("{}")));
    j.getRoot()->obj->emplace("r", asvJSONValue::makeRegex("a.\\d+", "im"));
    std::string s = j.toJSON5();
    asvJSON j2;
    ASSERT(j2.fromJSON5(std::string_view(s)));
    ASSERT_EQ(j2.getRegexPattern("r"), "a.\\d+");
    ASSERT_EQ(j2.getRegexOptions("r"), "im");
  }
}

TEST(testToINI) {
  // Simple object
  {
    asvJSON j;
    j.parse(std::string("{\"key\":\"val\",\"num\":42,\"flag\":true,\"nullval\":null}"));
    std::string ini = j.toINI();
    ASSERT(ini.find("key = val") != std::string::npos);
    ASSERT(ini.find("num = 42") != std::string::npos);
    ASSERT(ini.find("flag = true") != std::string::npos);
  }
  // Nested sections via dot notation
  {
    asvJSON j;
    j.parse(std::string(R"({"db":{"host":"localhost","port":"5432"},"log":{"level":"debug"}})"));
    std::string ini = j.toINI();
    ASSERT(ini.find("[db]") != std::string::npos);
    ASSERT(ini.find("host = localhost") != std::string::npos);
    ASSERT(ini.find("port = \"5432\"") != std::string::npos);
    ASSERT(ini.find("[log]") != std::string::npos);
    ASSERT(ini.find("level = debug") != std::string::npos);
  }
  // Deep nesting
  {
    asvJSON j;
    j.parse(std::string(R"({"network":{"server":{"ip":"10.0.0.1"},"client":{"retries":"3"}}})"));
    std::string ini = j.toINI();
    ASSERT(ini.find("[network.server]") != std::string::npos);
    ASSERT(ini.find("ip = 10.0.0.1") != std::string::npos);
    ASSERT(ini.find("[network.client]") != std::string::npos);
    ASSERT(ini.find("retries = \"3\"") != std::string::npos);
  }
  // Quoted value (string with spaces)
  {
    asvJSON j;
    j.putString("title", "Hello World");
    std::string ini = j.toINI();
    ASSERT(ini.find(R"(title = "Hello World")") != std::string::npos);
  }
  // Array of objects
  {
    asvJSON j;
    j.parse(std::string(R"({"items":[{"x":"1"},{"x":"2"}]})"));
    std::string ini = j.toINI();
    ASSERT(ini.find("[items]") != std::string::npos);
  }
  // Empty root
  {
    asvJSON j;
    j.parse(std::string("{}"));
    ASSERT_EQ(j.toINI(), std::string(""));
  }
  // Keys containing [ ] are escaped in section headers
  {
    asvJSON j;
    j.parse(std::string(R"({"a[b]":{"c[d]":"v"}})"));
    std::string ini = j.toINI();
    ASSERT(ini.find("a\\[b\\]]") != std::string::npos);
    ASSERT(ini.find("[a[b]]") == std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    ASSERT_EQ(std::string(j2.getString("a[b].c[d]")), "v");
  }
}

TEST(testFromINI) {
  // Simple key-value with type inference
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("key = val\nnum = 42\nflag = true\nempty =\n")));
    ASSERT_EQ(std::string(j.getString("key")), "val");
    ASSERT(j.get("num") != nullptr && j.get("num")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("num"), int64_t(42));
    ASSERT(j.get("flag") != nullptr && j.get("flag")->type == asvJSONValue::BOOL_VAL);
    ASSERT_EQ(j.getBool("flag"), true);
    ASSERT_EQ(std::string(j.getString("empty")), "");
  }
  // Sections
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("[db]\nhost = localhost\nport = 5432\n")));
    ASSERT(j.getNested("db") != nullptr);
    ASSERT_EQ(std::string(j.getString("db.host")), "localhost");
    ASSERT_EQ(j.getInt("db.port"), int64_t(5432));
  }
  // Nested sections (dot notation)
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("[network.server]\nip = 10.0.0.1\n[network.client]\nretries = 3\n")));
    ASSERT_EQ(std::string(j.getString("network.server.ip")), "10.0.0.1");
    ASSERT_EQ(j.getInt("network.client.retries"), int64_t(3));
  }
  // Quoted values (always strings, even for true/42)
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("title = \"Hello World\"\npath = \"C:\\\\Program Files\"\nstr42 = \"42\"\n")));
    ASSERT_EQ(std::string(j.getString("title")), "Hello World");
    ASSERT_EQ(std::string(j.getString("path")), "C:\\Program Files");
    ASSERT(j.get("str42") != nullptr && j.get("str42")->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j.getString("str42")), "42");
  }
  // Line comments (; and #, including inline for bare values)
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("; line comment\nkey = val\n# another line comment\nfoo = bar\nbare = hello ; inline\nnum = 42 # inline\n")));
    ASSERT_EQ(std::string(j.getString("key")), "val");
    ASSERT_EQ(std::string(j.getString("foo")), "bar");
    ASSERT_EQ(std::string(j.getString("bare")), "hello");
    ASSERT_EQ(j.getInt("num"), int64_t(42));
  }
  // Escape sequences
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("msg = hello\\nworld\ntab = a\\tb\n")));
    ASSERT_EQ(std::string(j.getString("msg")), "hello\nworld");
    ASSERT_EQ(std::string(j.getString("tab")), "a\tb");
  }
  // Global keys + sections
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("global = val\n[sec]\nk = v\n")));
    ASSERT_EQ(std::string(j.getString("global")), "val");
    ASSERT_EQ(std::string(j.getString("sec.k")), "v");
  }
  // Empty input
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("")));
  }
  // Colon as delimiter
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("key: val\n")));
    ASSERT_EQ(std::string(j.getString("key")), "val");
  }
  // Space delimiter
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("key val\n")));
    ASSERT_EQ(std::string(j.getString("key")), "val");
  }
  // Line continuation
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("msg = hello\\\nworld\n")));
    ASSERT_EQ(std::string(j.getString("msg")), "helloworld");
  }
  // [[section]] for arrays of objects
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("[[items]]\nx = 1\n[[items]]\nx = 2\n")));
    auto* items = j.get("items");
    ASSERT(items != nullptr && items->type == asvJSONValue::ARRAY);
    ASSERT_EQ(items->arr->size(), size_t(2));
    auto* first = items->get(size_t(0));
    ASSERT(first != nullptr && first->type == asvJSONValue::OBJECT);
    auto* x1 = first->get("x");
    ASSERT(x1 != nullptr && x1->type == asvJSONValue::INT);
    ASSERT_EQ(x1->getInt(), int64_t(1));
    auto* second = items->get(size_t(1));
    ASSERT(second != nullptr && second->type == asvJSONValue::OBJECT);
    auto* x2 = second->get("x");
    ASSERT(x2 != nullptr && x2->type == asvJSONValue::INT);
    ASSERT_EQ(x2->getInt(), int64_t(2));
  }
  // Duplicate keys → arrays
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("tag = cpp\ntag = json\n")));
    auto* tags = j.get("tag");
    ASSERT(tags != nullptr && tags->type == asvJSONValue::ARRAY);
    ASSERT_EQ(tags->arr->size(), size_t(2));
    ASSERT_EQ(std::string(tags->arr->at(0)->getString()), "cpp");
    ASSERT_EQ(std::string(tags->arr->at(1)->getString()), "json");
  }
  // Section keys with escaped brackets [ ]
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("[a\\[b\\]]\nc[d] = v\n")));
    ASSERT_EQ(std::string(j.getString("a[b].c[d]")), "v");
  }
  // Dotted keys at root level split into nested objects
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("general.name = asvJSON++\ngeneral.version = 1.15.0\ngeneral.active = true\nmetadata.author = asv\nmetadata.license = MIT\n")));
    ASSERT_EQ(std::string(j.getString("general.name")), "asvJSON++");
    ASSERT_EQ(std::string(j.getString("general.version")), "1.15.0");
    ASSERT_EQ(j.getBool("general.active"), true);
    ASSERT_EQ(std::string(j.getString("metadata.author")), "asv");
    ASSERT_EQ(std::string(j.getString("metadata.license")), "MIT");
    auto* g = j.get("general");
    ASSERT(g != nullptr && g->type == asvJSONValue::OBJECT);
    auto* m = j.get("metadata");
    ASSERT(m != nullptr && m->type == asvJSONValue::OBJECT);
  }
  // Dotted keys inside a section
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("[server]\nhost.name = example.com\nhost.port = 8080\n")));
    ASSERT_EQ(std::string(j.getString("server.host.name")), "example.com");
    ASSERT_EQ(j.getInt("server.host.port"), int64_t(8080));
  }
}

TEST(testFromINIIntOverflow) {
  // int64 overflow must be preserved as an exact literal (raw_number), not clamped to LLONG_MAX
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("big = 99999999999999999999999\n")));
    auto* v = j.get("big");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT(v->raw_number);
    ASSERT_EQ(std::string(j.getString("big")), "99999999999999999999999");
  }
  // Negative overflow
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("neg = -99999999999999999999999\n")));
    auto* v = j.get("neg");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT(v->raw_number);
    ASSERT_EQ(std::string(j.getString("neg")), "-99999999999999999999999");
  }
  // In-range integers still parse as INT
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("n = 12345\nn2 = -9876543210123\n")));
    ASSERT(j.get("n") != nullptr && j.get("n")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("n"), int64_t(12345));
    ASSERT(j.get("n2") != nullptr && j.get("n2")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("n2"), int64_t(-9876543210123));
  }
}

TEST(testINIRoundTrip) {
  // STRING values that look like numbers/bools/null must be quoted so the
  // round-trip preserves the STRING type (not INT/DOUBLE/BOOL/NULL).
  {
    asvJSON j;
    j.parse(std::string("{\"k\":\"42\"}"));
    std::string ini = j.toINI();
    ASSERT(ini.find("k = \"42\"") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    auto* v = j2.getRoot()->get("k");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j2.getString("k")), "42");
  }
  {
    asvJSON j;
    j.parse(std::string("{\"k\":\"3.14\"}"));
    std::string ini = j.toINI();
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    auto* v = j2.getRoot()->get("k");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j2.getString("k")), "3.14");
  }
  {
    asvJSON j;
    j.parse(std::string("{\"k\":\"true\"}"));
    std::string ini = j.toINI();
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    auto* v = j2.getRoot()->get("k");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j2.getString("k")), "true");
  }
  {
    asvJSON j;
    j.parse(std::string("{\"k\":\"false\"}"));
    std::string ini = j.toINI();
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    auto* v = j2.getRoot()->get("k");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j2.getString("k")), "false");
  }
  {
    asvJSON j;
    j.parse(std::string("{\"k\":\"null\"}"));
    std::string ini = j.toINI();
    ASSERT(ini.find("k = \"null\"") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    auto* v = j2.getRoot()->get("k");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j2.getString("k")), "null");
  }
  // NULL round-trips as null (bare), not as the string "null"
  {
    asvJSON j;
    j.parse(std::string("{\"k\":null}"));
    std::string ini = j.toINI();
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    auto* v = j2.getRoot()->get("k");
    ASSERT(v != nullptr && v->type == asvJSONValue::NULL_VAL);
  }
  // Full mixed round-trip: array-of-scalars with empty string and null preserved
  {
    asvJSON j;
    j.parse(std::string("{\"arr\":[\"42\",42,null,\"\",\"x\"]}"));
    std::string ini = j.toINI();
    asvJSON j2;
    ASSERT(j2.fromINI(ini));
    auto* arr = j2.getRoot()->get("arr");
    ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
    ASSERT_EQ(arr->arr->size(), size_t(5));
    ASSERT(arr->arr->at(0)->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(arr->arr->at(0)->getString()), "42");
    ASSERT(arr->arr->at(1)->type == asvJSONValue::INT);
    ASSERT_EQ(arr->arr->at(1)->getInt(), int64_t(42));
    ASSERT(arr->arr->at(2)->type == asvJSONValue::NULL_VAL);
    ASSERT(arr->arr->at(3)->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(arr->arr->at(3)->getString()), "");
    ASSERT(arr->arr->at(4)->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(arr->arr->at(4)->getString()), "x");
  }
}

TEST(testIniExtras) {
  // UTF-8 BOM is stripped before parsing
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("\xEF\xBB\xBFkey = val\n")));
    ASSERT_EQ(std::string(j.getString("key")), "val");
    ASSERT(!j.getRoot()->hasKey("\xEF\xBB\xBFkey"));
  }
  // Bare leading-zero numerics stay STRING (zip codes, file modes)
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("zip = 042\nneg = -042\nplus = +042\nzero = 0\nd = 0.5\n")));
    auto* z = j.get("zip");
    ASSERT(z != nullptr && z->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j.getString("zip")), "042");
    ASSERT(j.get("neg") != nullptr && j.get("neg")->type == asvJSONValue::STRING);
    ASSERT(j.get("plus") != nullptr && j.get("plus")->type == asvJSONValue::STRING);
    ASSERT(j.get("zero") != nullptr && j.get("zero")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("zero"), int64_t(0));
    ASSERT(j.get("d") != nullptr && j.get("d")->type == asvJSONValue::DOUBLE);
  }
  // Leading-zero STRING round-trips bare ("042" stays bare 042, not quoted)
  {
    asvJSON j, j2;
    j.parse(std::string("{\"zip\":\"042\"}"));
    std::string ini = j.toINI();
    ASSERT(ini.find("\"042\"") == std::string::npos);
    ASSERT(j2.fromINI(ini));
    auto* v = j2.getRoot()->get("zip");
    ASSERT(v != nullptr && v->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j2.getString("zip")), "042");
  }
  // Arrays of objects with nested sub-objects round-trip (flattened inside [[..]])
  {
    asvJSON j, j2;
    j.parse(std::string("{\"items\":[{\"name\":\"a\",\"host\":{\"ip\":\"1.2.3.4\"}},{\"name\":\"b\",\"host\":{\"ip\":\"5.6.7.8\"}}]}"));
    std::string ini = j.toINI();
    ASSERT(ini.find("host.ip = 1.2.3.4") != std::string::npos);
    ASSERT(ini.find("[items.host]") == std::string::npos);
    ASSERT(j2.fromINI(ini));
    auto* items = j2.getRoot()->get("items");
    ASSERT(items != nullptr && items->type == asvJSONValue::ARRAY);
    ASSERT_EQ(items->arr->size(), size_t(2));
    auto* h0 = items->get(size_t(0))->get("host");
    ASSERT(h0 != nullptr && h0->type == asvJSONValue::OBJECT);
    ASSERT_EQ(std::string(h0->get("ip")->getString()), "1.2.3.4");
    auto* h1 = items->get(size_t(1))->get("host");
    ASSERT(h1 != nullptr && h1->type == asvJSONValue::OBJECT);
    ASSERT_EQ(std::string(h1->get("ip")->getString()), "5.6.7.8");
  }
  // Nested arrays-of-objects inside array elements (incl. bracketed keys)
  {
    asvJSON j, j2;
    j.parse(std::string("{\"a[b]\":[{\"y\":[{\"z\":1},{\"z\":2}]}]}"));
    std::string ini = j.toINI();
    ASSERT(ini.find("[[a\\[b\\]]]") != std::string::npos);
    ASSERT(ini.find("a[b].y]]") == std::string::npos);
    ASSERT(j2.fromINI(ini));
    auto* a = j2.getRoot()->get("a[b]");
    ASSERT(a != nullptr && a->type == asvJSONValue::ARRAY);
    ASSERT_EQ(a->arr->size(), size_t(1));
    auto* y = a->get(size_t(0))->get("y");
    ASSERT(y != nullptr && y->type == asvJSONValue::ARRAY);
    ASSERT_EQ(y->arr->size(), size_t(2));
    ASSERT_EQ(y->get(size_t(0))->get("z")->getInt(), int64_t(1));
    ASSERT_EQ(y->get(size_t(1))->get("z")->getInt(), int64_t(2));
  }
  // [[arr]] over a pre-existing non-array value is rejected, not overwritten
  {
    asvJSON j;
    ASSERT(!j.fromINI(std::string("x = 1\n[[x]]\na = 2\n")));
    ASSERT(j.getLastError().find("array section") != std::string::npos);
  }
  {
    asvJSON j;
    ASSERT(!j.fromINI(std::string("[x]\nk = 1\n[[x]]\na = 2\n")));
    ASSERT(j.getLastError().find("array section") != std::string::npos);
  }
  // Trailing junk after a section header is rejected, not silently dropped
  {
    asvJSON j;
    ASSERT(!j.fromINI(std::string("[sec] foo\nk = v\n")));
    ASSERT(j.getLastError().find("unexpected content after section header") != std::string::npos);
  }
  {
    asvJSON j;
    ASSERT(!j.fromINI(std::string("[[items]] junk\n")));
    ASSERT(j.getLastError().find("unexpected content after section header") != std::string::npos);
  }
  // Section headers may be followed by a comment
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("[sec] ; note\nk = v\n[other] # note\n")));
    ASSERT_EQ(std::string(j.getString("sec.k")), "v");
  }
  // Escaped brackets in section names still survive the trailing check
  {
    asvJSON j;
    ASSERT(j.fromINI(std::string("[a\\[b\\]]\nc[d] = v\n")));
    ASSERT_EQ(std::string(j.getString("a[b].c[d]")), "v");
  }
}

TEST(testToUDE) {
  // Simple object: scalar types serialize bare
  {
    asvJSON j;
    j.parse(std::string("{\"key\":\"val\",\"num\":42,\"flag\":true,\"nullval\":null}"));
    std::string ude = j.toUDE();
    ASSERT(ude.find("key: val") != std::string::npos);
    ASSERT(ude.find("num: 42") != std::string::npos);
    ASSERT(ude.find("flag: true") != std::string::npos);
    ASSERT(ude.find("nullval: null") != std::string::npos);
  }
  // Strings that would be misread as other types are quoted
  {
    asvJSON j;
    j.putString("title", "Hello World");
    j.putString("numstr", "5432");
    j.putString("boolstr", "true");
    std::string ude = j.toUDE();
    ASSERT(ude.find(R"(title: "Hello World")") != std::string::npos);
    ASSERT(ude.find(R"(numstr: "5432")") != std::string::npos);
    ASSERT(ude.find(R"(boolstr: "true")") != std::string::npos);
  }
  // Empty root object
  {
    asvJSON j;
    j.parse(std::string("{}"));
    ASSERT_EQ(j.toUDE(), std::string(""));
  }
  // Root array
  {
    asvJSON j;
    j.parse(std::string("[1,2,3]"));
    ASSERT_EQ(j.toUDE(), std::string("[1, 2, 3]\n"));
  }
  // Array of objects is serialized multiline
  {
    asvJSON j;
    j.parse(std::string("{\"items\":[{\"x\":1},{\"x\":2}]}"));
    std::string ude = j.toUDE();
    ASSERT(ude.find("items: [") != std::string::npos);
    ASSERT(ude.find("x: 1") != std::string::npos);
    ASSERT(ude.find("x: 2") != std::string::npos);
  }
  // Multiline string becomes a block scalar
  {
    asvJSON j;
    j.putString("msg", "line1\nline2");
    std::string ude = j.toUDE();
    ASSERT(ude.find("msg: |") != std::string::npos);
    ASSERT(ude.find("  line1") != std::string::npos);
    ASSERT(ude.find("  line2") != std::string::npos);
  }
  // Keys that are not identifier-like are quoted
  {
    asvJSON j;
    j.putString("my key", "v");
    std::string ude = j.toUDE();
    ASSERT(ude.find(R"("my key": v)") != std::string::npos);
  }
  // Binary becomes !base64
  {
    asvJSON j;
    uint8_t data[] = {0x01, 0x02, 0x03};
    j.putBinary("data", data, 3);
    std::string ude = j.toUDE();
    ASSERT(ude.find(R"(!base64 "AQID")") != std::string::npos);
  }
  // Regex becomes !regex
  {
    asvJSON j;
    j.putRegex("re", "a+b", "i");
    std::string ude = j.toUDE();
    ASSERT(ude.find("!regex ") != std::string::npos);
  }
  // Datetime becomes !datetime
  {
    asvJSON j;
    j.putDateTime("ts", time(nullptr));
    std::string ude = j.toUDE();
    ASSERT(ude.find("!datetime \"") != std::string::npos);
  }
}

TEST(testFromUDE) {
  // Simple key-value with type inference
  {
    asvJSON j;
    ASSERT(j.fromUDE("key: val\nnum: 42\nflag: true\nnil: null\n"));
    ASSERT_EQ(std::string(j.getString("key")), "val");
    ASSERT(j.get("num") != nullptr && j.get("num")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("num"), int64_t(42));
    ASSERT(j.get("flag") != nullptr && j.get("flag")->type == asvJSONValue::BOOL_VAL);
    ASSERT_EQ(j.getBool("flag"), true);
    ASSERT(j.get("nil") != nullptr && j.get("nil")->type == asvJSONValue::NULL_VAL);
  }
  // Quoted strings with escapes
  {
    asvJSON j;
    ASSERT(j.fromUDE("path: \"C:\\\\Program Files\"\nmsg: \"hello\\nworld\"\n"));
    ASSERT_EQ(std::string(j.getString("path")), "C:\\Program Files");
    ASSERT_EQ(std::string(j.getString("msg")), "hello\nworld");
  }
  // Dotted keys produce nested objects
  {
    asvJSON j;
    ASSERT(j.fromUDE("a.b.c: 1\n"));
    ASSERT(j.getNested("a") != nullptr);
    ASSERT_EQ(j.getInt("a.b.c"), int64_t(1));
  }
  // Inline array
  {
    asvJSON j;
    ASSERT(j.fromUDE("list: [1, 2, 3]\n"));
    auto* arr = j.get("list");
    ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
    ASSERT_EQ(arr->arr->size(), size_t(3));
    ASSERT_EQ(arr->arr->at(0)->getInt(), int64_t(1));
  }
  // Multiline array
  {
    asvJSON j;
    ASSERT(j.fromUDE("list: [\n  1,\n  2,\n  3\n]\n"));
    auto* arr = j.get("list");
    ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
    ASSERT_EQ(arr->arr->size(), size_t(3));
  }
  // Inline object
  {
    asvJSON j;
    ASSERT(j.fromUDE("db: {host: localhost, port: 5432}\n"));
    ASSERT_EQ(std::string(j.getString("db.host")), "localhost");
    ASSERT_EQ(j.getInt("db.port"), int64_t(5432));
  }
  // Block scalar '|'
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  line1\n  line2\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "line1\nline2");
  }
  // Folded scalar '>'
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: >\n  line1\n  line2\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "line1 line2");
  }
  // Number bases and doubles
  {
    asvJSON j;
    ASSERT(j.fromUDE("h: 0xFF\no: 0o17\nb: 0b1010\nd: 1.5\n"));
    ASSERT_EQ(j.getInt("h"), int64_t(255));
    ASSERT_EQ(j.getInt("o"), int64_t(15));
    ASSERT_EQ(j.getInt("b"), int64_t(10));
    ASSERT(j.get("d") != nullptr && j.get("d")->type == asvJSONValue::DOUBLE);
    ASSERT_EQ(j.getDouble("d"), 1.5);
  }
  // Anchors and aliases
  {
    asvJSON j;
    ASSERT(j.fromUDE("name: &a hello\ncopy: *a\n"));
    ASSERT_EQ(std::string(j.getString("name")), "hello");
    ASSERT_EQ(std::string(j.getString("copy")), "hello");
  }
  // Tags for special types
  {
    asvJSON j;
    ASSERT(j.fromUDE("data: !base64 \"AQID\"\nts: !datetime \"2024-01-15T10:30:00Z\"\nraw: !bin 0x0102\nre: !regex \"a+b|i\"\next: !ext \"5:AQID\"\n"));
    ASSERT(j.get("data") != nullptr && j.get("data")->type == asvJSONValue::BINARY);
    ASSERT_EQ(j.getBinary("data").size(), size_t(3));
    ASSERT(j.get("ts") != nullptr && j.get("ts")->type == asvJSONValue::DATETIME);
    ASSERT(j.get("raw") != nullptr && j.get("raw")->type == asvJSONValue::BINARY);
    ASSERT(j.get("re") != nullptr && j.get("re")->type == asvJSONValue::REGEX);
    ASSERT(j.get("ext") != nullptr && j.get("ext")->type == asvJSONValue::EXTENSION);
    auto ext = j.getExtension("ext");
    ASSERT_EQ(ext.first, int8_t(5));
    ASSERT_EQ(ext.second.size(), size_t(3));
  }
  // Multiple documents -> array of documents
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 1\n// --- End of Document ---\nb: 2\n"));
    auto* root = j.getRoot();
    ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
    ASSERT_EQ(root->arr->size(), size_t(2));
    ASSERT_EQ(root->arr->at(0)->get("a")->getInt(), int64_t(1));
    ASSERT_EQ(root->arr->at(1)->get("b")->getInt(), int64_t(2));
  }
  // Comments: #, // and /* ... */
  {
    asvJSON j;
    ASSERT(j.fromUDE("# line comment\na: 1\n// slash comment\nb: 2\n/* block\ncomment */ c: 3\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
    ASSERT_EQ(j.getInt("b"), int64_t(2));
    ASSERT_EQ(j.getInt("c"), int64_t(3));
  }
  // Header line is skipped as a comment
  {
    asvJSON j;
    ASSERT(j.fromUDE("# UDE v1.0\nname: test\n"));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
  // Plain text fallback for a single-line document
  {
    asvJSON j;
    ASSERT(j.fromUDE("just some plain text"));
    ASSERT_EQ(std::string(j.getRoot()->getString()), "just some plain text");
  }
  // Empty input -> empty object
  {
    asvJSON j;
    ASSERT(j.fromUDE(""));
    ASSERT(j.getRoot() != nullptr && j.getRoot()->type == asvJSONValue::OBJECT);
  }
  // Duplicate keys: last wins by default, rejected in strict mode
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 1\na: 2\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(2));
  }
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: 1\na: 2\n", true));
  }
}

TEST(testUDEStringEscapes) {  // JSON control escapes \r \b \f \/
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"x\\ry\\bz\\fw\\/v\"\n"));
    ASSERT_EQ(std::string(j.getString("a")), "x\ry\bz\fw/v");
  }
  // Byte escapes \xHH (exactly 2 hex digits)
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"\\x41\\x42\\x0a\"\n"));
    std::string s = j.getString("a");
    ASSERT_EQ(s.size(), size_t(3));
    ASSERT_EQ(s[0], 'A');
    ASSERT_EQ(s[1], 'B');
    ASSERT_EQ(static_cast<unsigned char>(s[2]), 0x0A);
  }
  // A single hex digit is rejected (\xHH requires exactly two)
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: \"\\x4\"\n"));
    ASSERT(!j.fromUDE("a: \"\\xA\"\n"));
  }
  // Unicode \uXXXX (BMP, including surrogate pairs)
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"\\u0041\\u00e9\"\n"));
    ASSERT_EQ(std::string(j.getString("a")), "A\xC3\xA9");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"\\uD83D\\uDE00\"\n")); // U+1F600 grinning face
    std::string s = j.getString("a");
    ASSERT_EQ(s, "\xF0\x9F\x98\x80");
  }
  // Unicode \UXXXXXXXX and \u{...}
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"\\U0001F600\"\n"));
    ASSERT_EQ(std::string(j.getString("a")), "\xF0\x9F\x98\x80");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"\\u{1F600}\"\n"));
    ASSERT_EQ(std::string(j.getString("a")), "\xF0\x9F\x98\x80");
  }
  // \r\n normalization inside a multiline string
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"line1\r\nline2\"\n"));
    ASSERT_EQ(std::string(j.getString("a")), "line1\nline2");
  }
  // Multiline double-quoted strings are allowed
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"first\nsecond\"\n"));
    ASSERT_EQ(std::string(j.getString("a")), "first\nsecond");
  }
  // Line continuation joins lines and strips leading whitespace
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: \"one \\\n  two\"\n"));
    ASSERT_EQ(std::string(j.getString("a")), "one two");
  }
  // Single-quoted strings are literal; '' is an escaped quote
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 'hello world'\n"));
    ASSERT_EQ(std::string(j.getString("a")), "hello world");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 'it''s'\n"));
    ASSERT_EQ(std::string(j.getString("a")), "it's");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 'C:\\path\\nope'\n")); // backslashes are literal
    ASSERT_EQ(std::string(j.getString("a")), "C:\\path\\nope");
  }
  // Single-quoted keys are supported too
  {
    asvJSON j;
    ASSERT(j.fromUDE("'a.b': 1\n"));
    // The key is literal (not expanded as a dotted key).
    auto* k = j.getRoot()->getConst("a.b");
    ASSERT(k != nullptr);
    ASSERT_EQ(k->getInt(), int64_t(1));
    ASSERT(j.getNested("a") == nullptr);
  }
  // Round-trip: single-quoted input serializes to double-quoted output
  {
    asvJSON j, j2;
    ASSERT(j.fromUDE("a: 'O''Brien'\n"));
    std::string ude = j.toUDE();
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("a")), "O'Brien");
  }
  // Invalid escapes are rejected
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: \"\\q\"\n"));
  }
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: \"\\xZZ\"\n"));
  }
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: 'unterminated\n"));
  }
}

TEST(testUDETypes) {
  // Explicit type tags: !int, !float, !string, !bool
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: !int 42\nb: !int \"123\"\nc: !int 3.0\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("a"), int64_t(42));
    ASSERT(j.get("b") != nullptr && j.get("b")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("b"), int64_t(123));
    ASSERT(j.get("c") != nullptr && j.get("c")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("c"), int64_t(3));
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: !float 42\nb: !float \"1.5\"\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::DOUBLE);
    ASSERT_EQ(j.getDouble("a"), 42.0);
    ASSERT(j.get("b") != nullptr && j.get("b")->type == asvJSONValue::DOUBLE);
    ASSERT_EQ(j.getDouble("b"), 1.5);
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: !string 42\nb: !string 3.5\nc: !string true\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j.getString("a")), "42");
    ASSERT_EQ(std::string(j.getString("b")), "3.5");
    ASSERT_EQ(std::string(j.getString("c")), "true");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: !bool \"yes\"\nb: !bool 0\nc: !bool true\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::BOOL_VAL);
    ASSERT_EQ(j.getBool("a"), true);
    ASSERT_EQ(j.getBool("b"), false);
    ASSERT_EQ(j.getBool("c"), true);
  }
  // !null discards its value
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: !null 42\nb: !null\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::NULL_VAL);
    ASSERT(j.get("b") != nullptr && j.get("b")->type == asvJSONValue::NULL_VAL);
  }
  // Invalid coercions are rejected
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: !int \"abc\"\n"));
  }
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: !float [1]\n"));
  }
  // Digit separators
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 1_000_000\nb: 0xFF_FF\nc: 3_14.5\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(1000000));
    ASSERT_EQ(j.getInt("b"), int64_t(65535));
    ASSERT(j.get("c") != nullptr && j.get("c")->type == asvJSONValue::DOUBLE);
    ASSERT_EQ(j.getDouble("c"), 314.5);
  }
  // Invalid separator placement is not a number
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 1__000\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::STRING);
  }
  // Big integers stored as raw_number and round-trip
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 123456789012345678901234567890\n"));
    auto* v = j.get("a");
    ASSERT(v != nullptr);
    ASSERT(v->type == asvJSONValue::STRING);
    ASSERT(v->raw_number);
    ASSERT_EQ(v->str_data, "123456789012345678901234567890");
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    auto* v2 = j2.get("a");
    ASSERT(v2 != nullptr);
    ASSERT(v2->type == asvJSONValue::STRING);
    ASSERT(v2->raw_number);
    ASSERT_EQ(v2->str_data, "123456789012345678901234567890");
  }
  // NaN and Infinity round-trip as float literals
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: .nan\nb: .inf\nc: -.inf\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::DOUBLE);
    ASSERT(std::isnan(j.getDouble("a")));
    ASSERT(std::isinf(j.getDouble("b")) && j.getDouble("b") > 0);
    ASSERT(std::isinf(j.getDouble("c")) && j.getDouble("c") < 0);
    std::string ude = j.toUDE();
    ASSERT(ude.find(".nan") != std::string::npos);
    ASSERT(ude.find(".inf") != std::string::npos);
    ASSERT(ude.find("-.inf") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(std::isnan(j2.getDouble("a")));
    ASSERT(std::isinf(j2.getDouble("b")));
    ASSERT(std::isinf(j2.getDouble("c")));
  }
  // Integer-valued doubles serialize with ".0" so the type is preserved
  {
    asvJSON j, j2;
    ASSERT(j.fromUDE("a: 42.0\n"));
    std::string ude = j.toUDE();
    ASSERT(ude.find("42.0") != std::string::npos);
    ASSERT(j2.fromUDE(ude));
    ASSERT(j2.get("a") != nullptr && j2.get("a")->type == asvJSONValue::DOUBLE);
  }
  // !datetime: date-only and timezone offset
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: !datetime \"2026-07-30\"\nb: !datetime \"2026-07-30T14:45:00+03:00\"\n"));
    ASSERT(j.get("a") != nullptr && j.get("a")->type == asvJSONValue::DATETIME);
    ASSERT(j.get("b") != nullptr && j.get("b")->type == asvJSONValue::DATETIME);
    std::string ude = j.toUDE();
    ASSERT(ude.find("!datetime \"") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(j2.get("a")->type == asvJSONValue::DATETIME);
    ASSERT(j2.get("b")->type == asvJSONValue::DATETIME);
  }
  // !schema preserves the annotation as a CUSTOM_TAG
  {
    asvJSON j;
    ASSERT(j.fromUDE("s: !schema \"urn:example:person\"\n"));
    ASSERT(j.get("s") != nullptr && j.get("s")->type == asvJSONValue::CUSTOM_TAG);
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    ASSERT(j2.get("s") != nullptr && j2.get("s")->type == asvJSONValue::CUSTOM_TAG);
  }
}

TEST(testUDEEdgeCases) {
  // "a.b".c : literal first segment (contains a dot) + nested "c"
  {
    asvJSON j;
    ASSERT(j.fromUDE("\"a.b\".c: 1\n"));
    auto* ab = j.get("a.b");
    ASSERT(ab != nullptr && ab->type == asvJSONValue::OBJECT);
    auto* c = ab->get("c");
    ASSERT(c != nullptr && c->type == asvJSONValue::INT);
    ASSERT_EQ(c->num, int64_t(1));
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    auto* ab2 = j2.get("a.b");
    ASSERT(ab2 != nullptr && ab2->type == asvJSONValue::OBJECT);
    ASSERT_EQ(ab2->get("c")->getInt(), int64_t(1));
  }
  // A fully quoted dotted key is a single literal key
  {
    asvJSON j;
    ASSERT(j.fromUDE("\"a.b.c\": 1\n"));
    auto* v = j.get("a.b.c");
    ASSERT(v != nullptr && v->type == asvJSONValue::INT);
    ASSERT_EQ(v->num, int64_t(1));
  }
  // Unquoted dotted key still nests
  {
    asvJSON j;
    ASSERT(j.fromUDE("a.b.c: 1\n"));
    ASSERT_EQ(j.getByPointer("/a/b/c")->getInt(), int64_t(1));
  }
  // Mixed quoted/unquoted segments
  {
    asvJSON j;
    ASSERT(j.fromUDE("\"my key\".sub: 2\n"));
    auto* sub = j.get("my key");
    ASSERT(sub != nullptr && sub->type == asvJSONValue::OBJECT);
    ASSERT_EQ(sub->get("sub")->getInt(), int64_t(2));
  }
  // Leading/trailing commas in arrays: [,], [1,], [1,,2]
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: [,]\n"));
    auto* a = j.get("a");
    ASSERT(a != nullptr && a->type == asvJSONValue::ARRAY);
    ASSERT_EQ(a->arr->size(), size_t(0));
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: [1,]\n"));
    auto* a = j.get("a");
    ASSERT(a != nullptr && a->arr->size() == size_t(1));
    ASSERT_EQ(a->arr->at(0)->num, int64_t(1));
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: [1,,2]\n"));
    auto* a = j.get("a");
    ASSERT(a != nullptr && a->arr->size() == size_t(2));
    ASSERT_EQ(a->arr->at(0)->num, int64_t(1));
    ASSERT_EQ(a->arr->at(1)->num, int64_t(2));
  }
  // Leading/trailing commas in objects: {,}, {a:1,}
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: {,}\n"));
    auto* a = j.get("a");
    ASSERT(a != nullptr && a->type == asvJSONValue::OBJECT);
    ASSERT_EQ(a->obj->size(), size_t(0));
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: {x: 1,}\n"));
    auto* a = j.get("a");
    ASSERT(a != nullptr && a->obj->size() == size_t(1));
    ASSERT_EQ(a->get("x")->getInt(), int64_t(1));
  }
  // Tabs are allowed as whitespace separators between tokens
  {
    asvJSON j;
    ASSERT(j.fromUDE("a:\t1\nb:\t\"x\"\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
    ASSERT_EQ(std::string(j.getString("b")), "x");
  }
  // Tabs are rejected in the block scalar indentation margin
  {
    asvJSON j;
    ASSERT(!j.fromUDE("msg: |\n\tline1\n"));
  }
  // Tabs after the margin are ordinary content
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  a\tb\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "a\tb");
  }
}

TEST(testUDEMergeKeys) {
  // Basic merge: <<: *default copies entries of the anchored object
  {
    asvJSON j;
    ASSERT(j.fromUDE("base: &def {a: 1, b: 2}\n<<: *def\nc: 3\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
    ASSERT_EQ(j.getInt("b"), int64_t(2));
    ASSERT_EQ(j.getInt("c"), int64_t(3));
  }
  // Explicit key defined before the merge wins
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 9\n<<: {a: 1, b: 2}\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(9));
    ASSERT_EQ(j.getInt("b"), int64_t(2));
  }
  // Explicit key defined after the merge overrides
  {
    asvJSON j;
    ASSERT(j.fromUDE("<<: {a: 1}\na: 9\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(9));
  }
  // Sequence of merge sources: later objects win
  {
    asvJSON j;
    ASSERT(j.fromUDE("m1: &m1 {a: 1, b: 2}\nm2: &m2 {b: 3, c: 4}\n<<: [*m1, *m2]\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
    ASSERT_EQ(j.getInt("b"), int64_t(3));
    ASSERT_EQ(j.getInt("c"), int64_t(4));
  }
  // Merge inside an inline object
  {
    asvJSON j;
    ASSERT(j.fromUDE("d: &def {x: 1, y: 2}\nr: {<<: *def, z: 3}\n"));
    auto* r = j.get("r");
    ASSERT(r != nullptr && r->type == asvJSONValue::OBJECT);
    ASSERT_EQ(r->get("x")->getInt(), int64_t(1));
    ASSERT_EQ(r->get("y")->getInt(), int64_t(2));
    ASSERT_EQ(r->get("z")->getInt(), int64_t(3));
  }
  // A quoted "<<" is a literal key, not a merge
  {
    asvJSON j;
    ASSERT(j.fromUDE("\"<<\": 5\n"));
    auto* v = j.get("<<");
    ASSERT(v != nullptr && v->type == asvJSONValue::INT);
    ASSERT_EQ(v->num, int64_t(5));
  }
  // Invalid merge value is rejected
  {
    asvJSON j;
    ASSERT(!j.fromUDE("<<: 42\n"));
  }
}

TEST(testUDEDocuments) {
  // YAML-style "---" document separator -> array of documents
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 1\n---\nb: 2\n"));
    auto* root = j.getRoot();
    ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
    ASSERT_EQ(root->arr->size(), size_t(2));
    ASSERT_EQ(root->arr->at(0)->get("a")->getInt(), int64_t(1));
    ASSERT_EQ(root->arr->at(1)->get("b")->getInt(), int64_t(2));
  }
  // Leading "---" does not create an empty first document
  {
    asvJSON j;
    ASSERT(j.fromUDE("---\na: 1\n"));
    auto* root = j.getRoot();
    ASSERT(root != nullptr && root->type == asvJSONValue::OBJECT);
    ASSERT_EQ(j.getInt("a"), int64_t(1));
  }
  // "..." document end marker closes the current document
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 1\n...\nb: 2\n"));
    auto* root = j.getRoot();
    ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
    ASSERT_EQ(root->arr->size(), size_t(2));
  }
  // Version header accepted: current and older minor versions
  {
    asvJSON j;
    ASSERT(j.fromUDE("# UDE v1.0\nname: test\n"));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("# UDE v1.1\nname: test\n"));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
  // Future breaking major version is rejected
  {
    asvJSON j;
    ASSERT(!j.fromUDE("# UDE v2.0\nname: test\n"));
  }
  // Pre-1.0 version is rejected
  {
    asvJSON j;
    ASSERT(!j.fromUDE("# UDE v0.9\nname: test\n"));
  }
  // Malformed version headers are rejected
  {
    asvJSON j;
    ASSERT(!j.fromUDE("# UDE v1\nname: test\n"));
  }
  {
    asvJSON j;
    ASSERT(!j.fromUDE("# UDE vx.1\nname: test\n"));
  }
  // The "strict" flag in the header enables strict mode (quoted keys required)
  {
    asvJSON j;
    ASSERT(!j.fromUDE("# UDE v1.1 strict\n\"a\": 1\n\"a\": 2\n"));
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("# UDE v1.1 strict\n\"a\": 1\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
  }
  // Unknown flags are ignored (forward compatible)
  {
    asvJSON j;
    ASSERT(j.fromUDE("# UDE v1.1 experimental\nname: test\n"));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
}

TEST(testUDECustomTags) {
  // Canonical !<name> <value> form: name stored separately from value
  {
    asvJSON j;
    ASSERT(j.fromUDE("note: !custom \"some value\"\n"));
    auto* v = j.get("note");
    ASSERT(v != nullptr && v->type == asvJSONValue::CUSTOM_TAG);
    ASSERT_EQ(std::string(v->str_data), "custom");
    ASSERT(v->custom_value != nullptr && v->custom_value->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(v->custom_value->str_data), "some value");
  }
  // Non-string values are preserved as structured values, not flattened text
  {
    asvJSON j;
    ASSERT(j.fromUDE("n: !num 42\nf: !point {x: 1, y: 2}\n"));
    auto* n = j.get("n");
    ASSERT(n != nullptr && n->type == asvJSONValue::CUSTOM_TAG);
    ASSERT(n->custom_value != nullptr && n->custom_value->type == asvJSONValue::INT);
    ASSERT_EQ(n->custom_value->num, int64_t(42));
    auto* f = j.get("f");
    ASSERT(f != nullptr && f->type == asvJSONValue::CUSTOM_TAG);
    ASSERT(f->custom_value != nullptr && f->custom_value->type == asvJSONValue::OBJECT);
    ASSERT_EQ(f->custom_value->get("x")->getInt(), int64_t(1));
    ASSERT_EQ(f->custom_value->get("y")->getInt(), int64_t(2));
  }
  // Namespaced tags !ns:tag are representable
  {
    asvJSON j;
    ASSERT(j.fromUDE("v: !app:version \"1.2.3\"\n"));
    auto* v = j.get("v");
    ASSERT(v != nullptr && v->type == asvJSONValue::CUSTOM_TAG);
    ASSERT_EQ(std::string(v->str_data), "app:version");
  }
  // Verbatim URI tags !<uri> are supported
  {
    asvJSON j;
    ASSERT(j.fromUDE("v: !<tag:example.com,2026:type> 7\n"));
    auto* v = j.get("v");
    ASSERT(v != nullptr && v->type == asvJSONValue::CUSTOM_TAG);
    ASSERT_EQ(std::string(v->str_data), "tag:example.com,2026:type");
    ASSERT(v->custom_value != nullptr && v->custom_value->type == asvJSONValue::INT);
    ASSERT_EQ(v->custom_value->num, int64_t(7));
  }
  // Extended JSON representation: {"$customTag":{"name":...,"value":...}}
  {
    asvJSON j;
    ASSERT(j.fromUDE("note: !custom \"some value\"\nn: !num 42\n"));
    std::string json = j.serialize();
    ASSERT(json.find("\"$customTag\":{\"name\":\"custom\",\"value\":\"some value\"}") != std::string::npos);
    ASSERT(json.find("\"$customTag\":{\"name\":\"num\",\"value\":42}") != std::string::npos);
  }
  // UDE round-trip preserves name and value
  {
    asvJSON j;
    ASSERT(j.fromUDE("note: !custom \"some value\"\nn: !num 42\nv: !app:version \"1.2.3\"\n"));
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    ASSERT_EQ(std::string(j2.get("note")->str_data), "custom");
    ASSERT_EQ(std::string(j2.get("note")->custom_value->str_data), "some value");
    ASSERT_EQ(j2.get("n")->custom_value->num, int64_t(42));
    ASSERT_EQ(std::string(j2.get("v")->str_data), "app:version");
  }
  // !schema keeps the object/string as the structured value
  {
    asvJSON j;
    ASSERT(j.fromUDE("s: !schema {type: object}\n"));
    auto* v = j.get("s");
    ASSERT(v != nullptr && v->type == asvJSONValue::CUSTOM_TAG);
    ASSERT_EQ(std::string(v->str_data), "schema");
    ASSERT(v->custom_value != nullptr && v->custom_value->type == asvJSONValue::OBJECT);
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    ASSERT_EQ(std::string(j2.get("s")->str_data), "schema");
    ASSERT(j2.get("s")->custom_value != nullptr);
  }
}

TEST(testUDEEncoding) {
  // UTF-8 BOM is stripped, document parses normally
  {
    asvJSON j;
    ASSERT(j.fromUDE("\xEF\xBB\xBF" "a: 1\n"));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
  }
  // UTF-16 LE with BOM (ASCII payload)
  {
    std::string in = "\xFF\xFE";
    for (char c : std::string("name: test\n")) { in += c; in += '\x00'; }
    asvJSON j;
    ASSERT(j.fromUDE(in));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
  // UTF-16 BE with BOM
  {
    std::string in = "\xFE\xFF";
    for (char c : std::string("name: test\n")) { in += '\x00'; in += c; }
    asvJSON j;
    ASSERT(j.fromUDE(in));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
  // UTF-16 LE with BOM and a surrogate pair (emoji) plus non-ASCII text
  {
    // "emoji: \"\U0001F600\"\n" -- UTF-16 LE: 0xD83D 0xDE00 for the emoji
    std::string in = "\xFF\xFE";
    for (char c : std::string("emoji: \"")) { in += c; in += '\x00'; }
    in += '\x3D'; in += '\xD8'; // high surrogate 0xD83D
    in += '\x00'; in += '\xDE'; // low surrogate  0xDE00
    in += '"'; in += '\x00';
    in += '\n'; in += '\x00';
    asvJSON j;
    ASSERT(j.fromUDE(in));
    ASSERT_EQ(std::string(j.getString("emoji")), "\xF0\x9F\x98\x80");
  }
  // UTF-32 LE with BOM (build bytes explicitly: std::string literals with
  // embedded NUL truncate via strlen, so no literal here)
  {
    std::string in;
    in += (char)0xFF; in += (char)0xFE; in += (char)0x00; in += (char)0x00;
    for (char c : std::string("name: test\n")) { in += c; in += '\x00'; in += '\x00'; in += '\x00'; }
    asvJSON j;
    ASSERT(j.fromUDE(in));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
  // UTF-32 BE with BOM
  {
    std::string in;
    in += (char)0x00; in += (char)0x00; in += (char)0xFE; in += (char)0xFF;
    for (char c : std::string("name: test\n")) { in += '\x00'; in += '\x00'; in += '\x00'; in += c; }
    asvJSON j;
    ASSERT(j.fromUDE(in));
    ASSERT_EQ(std::string(j.getString("name")), "test");
  }
  // UTF-8 BOM is not discarded: header and multi-document streams still work
  {
    asvJSON j;
    ASSERT(j.fromUDE("\xEF\xBB\xBF" "a: 1\n---\nb: 2\n"));
    auto* root = j.getRoot();
    ASSERT(root != nullptr && root->type == asvJSONValue::ARRAY);
    ASSERT_EQ(root->arr->size(), size_t(2));
  }
}

TEST(testUDERoundTrip) {
  {
    asvJSON j;
    j.parse(std::string(R"({"name":"hello","count":7,"ratio":1.5,"ok":true,"nested":{"a":"x","b":[1,2,3]},"words":"two words"})"));
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("name")), "hello");
    ASSERT_EQ(j2.getInt("count"), int64_t(7));
    ASSERT_EQ(j2.getDouble("ratio"), 1.5);
    ASSERT_EQ(j2.getBool("ok"), true);
    ASSERT_EQ(std::string(j2.getString("nested.a")), "x");
    auto* arr = j2.getNested("nested.b");
    ASSERT(arr != nullptr && arr->type == asvJSONValue::ARRAY);
    ASSERT_EQ(arr->arr->size(), size_t(3));
    ASSERT_EQ(std::string(j2.getString("words")), "two words");
  }
  // Binary round-trip
  {
    asvJSON j;
    uint8_t data[] = {0x01, 0x02, 0x03, 0xFA};
    j.putBinary("data", data, 4);
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    ASSERT(j2.get("data") != nullptr && j2.get("data")->type == asvJSONValue::BINARY);
    auto bin = j2.getBinary("data");
    ASSERT_EQ(bin.size(), size_t(4));
    ASSERT_EQ(bin[3], uint8_t(0xFA));
  }
  // Multiline string round-trip via block scalar
  {
    asvJSON j;
    j.putString("msg", "line1\nline2");
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("msg")), "line1\nline2");
  }
  // Unknown tags are preserved as CUSTOM_TAG: type, UDE round-trip, clone and
  // JSON serialization all handle it without data corruption or UB
  {
    asvJSON j;
    ASSERT(j.fromUDE("note: !custom \"some value\"\nn: !num 42\n"));
    auto* v = j.get("note");
    ASSERT(v != nullptr && v->type == asvJSONValue::CUSTOM_TAG);
    std::string ude = j.toUDE();
    ASSERT(ude.find("note: !custom") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(j2.get("note") != nullptr && j2.get("note")->type == asvJSONValue::CUSTOM_TAG);
    // Cloning (copy constructor) preserves the custom tag
    asvJSON jc(j);
    ASSERT(jc.get("note") != nullptr && jc.get("note")->type == asvJSONValue::CUSTOM_TAG);
    ASSERT(*j.getRoot() == *jc.getRoot());
    // JSON serialization marks the tagged value instead of silently dropping it
    ASSERT(j.serialize().find("$customTag") != std::string::npos);
  }
}

TEST(testUDERoundTripSpecialTypes) {
  // DateTime round-trip
  {
    asvJSON j;
    j.putDateTime("dt", 1693824000); // 2023-09-04 10:40:00 UTC
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(j2.get("dt") != nullptr && j2.get("dt")->type == asvJSONValue::DATETIME);
    ASSERT_EQ(j2.getDateTime("dt"), 1693824000);
  }
  // Binary round-trip
  {
    asvJSON j;
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    j.putBinary("blob", data, 4);
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    auto* b = j2.get("blob");
    ASSERT(b != nullptr && b->type == asvJSONValue::BINARY);
    ASSERT_EQ(b->bin_data.size(), size_t(4));
    ASSERT_EQ(b->bin_data[0], uint8_t(0xDE));
    ASSERT_EQ(b->bin_data[3], uint8_t(0xEF));
  }
  // ObjectId round-trip (24-char hex <-> 12 raw bytes)
  {
    asvJSON j;
    char oid[] = {char(0x50), char(0x7f), char(0x1f), char(0x77), char(0xbc), char(0xf8), char(0x6c), char(0x79), char(0x94), char(0x39), char(0x12), char(0x34)};
    j.putObjectId("oid", std::string_view(oid, sizeof(oid)));
    std::string ude = j.toUDE();
    ASSERT(ude.find("!objectid 507f1f77bcf86c7994391234") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    auto* o = j2.get("oid");
    ASSERT(o != nullptr && o->type == asvJSONValue::OBJECTID);
    ASSERT_EQ(o->str_data.size(), size_t(12));
    ASSERT_EQ(std::string_view(j2.getObjectIdView("oid")).size(), size_t(12));
  }
  // Regex round-trip (pattern and options)
  {
    asvJSON j;
    j.putRegex("re", "^[a-z]+$", "im");
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    auto* r = j2.get("re");
    ASSERT(r != nullptr && r->type == asvJSONValue::REGEX);
    ASSERT_EQ(j2.getRegexPattern("re"), "^[a-z]+$");
    ASSERT_EQ(j2.getRegexOptions("re"), "im");
  }
  // Regex round-trip: pattern containing a literal '|' (split on the last '|')
  {
    asvJSON j;
    j.putRegex("re2", "a|b", "im");
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(j2.getRegexPattern("re2"), "a|b");
    ASSERT_EQ(j2.getRegexOptions("re2"), "im");
  }
  // Timestamp round-trip (64-bit int)
  {
    asvJSON j;
    j.putTimestamp("ts", 1234567890123456789LL);
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    auto* t = j2.get("ts");
    ASSERT(t != nullptr && t->type == asvJSONValue::TIMESTAMP);
    ASSERT_EQ(j2.getTimestamp("ts"), 1234567890123456789LL);
  }
  // Extension round-trip (type:base64)
  {
    asvJSON j;
    uint8_t data[] = {0x12, 0x34, 0xAB, 0xCD};
    j.putExtension("csv_data", 127, data, 4);
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    auto e = j2.getExtension("csv_data");
    ASSERT_EQ(e.first, int8_t(127));
    ASSERT_EQ(e.second.size(), size_t(4));
    ASSERT_EQ(e.second[0], uint8_t(0x12));
    ASSERT_EQ(e.second[3], uint8_t(0xCD));
  }
}

TEST(testUDEBlockScalarChomp) {
  // Default chomping (clip) keeps exactly one trailing newline
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  hello\n\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "hello\n");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  hello\n\n\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "hello\n");
  }
  // No trailing blank lines -> clip leaves the value unchanged
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  line1\n  line2\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "line1\nline2");
  }
  // '-' strips all trailing newlines
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |-\n  hello\n\n\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "hello");
  }
  // '+' keeps all trailing newlines; empty lines are content and do not
  // terminate the scalar (blank-line termination was removed)
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |+\n  hello\n\n\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "hello\n\n");
  }
  // Empty first line does not break the block scalar (implicit indent)
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n\n  hello\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "\nhello");
  }
  // Empty first line with explicit indent: the base indent is known from the
  // indicator, so a blank first line yields an empty value (clip chomping)
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |2\n\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "");
  }
  // Empty lines inside the block are preserved as content; the value ends with
  // a single newline after clip chomping
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  a\n\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "a\n");
  }
  // A dedent terminates the scalar, so a following key is parsed normally
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  a\n\nother: 1\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "a\n");
    ASSERT_EQ(j.getInt("other"), int64_t(1));
  }
  // Folded scalars fold a single break into a space; a blank line is kept as a
  // paragraph break
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: >\n  a\n\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "a\n");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: >\n  a\n  b\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "a b");
  }
  // Trailing spaces inside block-scalar lines are preserved verbatim
  {
    asvJSON j;
    ASSERT(j.fromUDE("msg: |\n  a  \n  b\n"));
    ASSERT_EQ(std::string(j.getString("msg")), "a  \nb");
  }
  // A string with trailing spaces round-trips (serializer keeps them)
  {
    asvJSON j;
    j.putString("msg", "a  \nb");
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    ASSERT_EQ(std::string(j2.getString("msg")), "a  \nb");
  }
  // A single content line with one trailing newline round-trips as a folded
  // block scalar ('>'), which is the only case where folding is lossless
  {
    asvJSON j;
    j.putString("msg", "hello\n");
    std::string ude = j.toUDE();
    ASSERT(ude.find("msg: >") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("msg")), "hello\n");
  }
  // A string ending in multiple newlines cannot round-trip as a block scalar,
  // so it is quoted instead
  {
    asvJSON j;
    j.putString("msg", "hello\n\n");
    std::string ude = j.toUDE();
    ASSERT(ude.find("msg: \"") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("msg")), "hello\n\n");
  }
  {
    asvJSON j;
    j.putString("msg", "hello\n\n");
    asvJSON j2;
    ASSERT(j2.fromUDE(j.toUDE()));
    ASSERT_EQ(std::string(j2.getString("msg")), "hello\n\n");
  }
  // Leading newline round-trips as a block scalar (blank line before the
  // base indent is known is preserved)
  {
    asvJSON j;
    j.putString("msg", "\nhello");
    std::string ude = j.toUDE();
    ASSERT(ude.find("msg: |") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("msg")), "\nhello");
  }
  // A single trailing newline round-trips as a '|+' block scalar
  {
    asvJSON j;
    j.putString("msg", "a\nb\n");
    std::string ude = j.toUDE();
    ASSERT(ude.find("msg: |+") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("msg")), "a\nb\n");
  }
  // Internal blank lines cannot round-trip as a block scalar (Appendix A), so
  // the serializer falls back to a quoted string
  {
    asvJSON j;
    j.putString("msg", "a\n\nb");
    std::string ude = j.toUDE();
    ASSERT(ude.find("msg: \"") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("msg")), "a\n\nb");
  }
  // A first line with leading whitespace cannot round-trip as a block scalar
  // (it would be absorbed into the base indent), so it is quoted instead
  {
    asvJSON j;
    j.putString("msg", "  a\n  b");
    std::string ude = j.toUDE();
    ASSERT(ude.find("msg: \"") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT_EQ(std::string(j2.getString("msg")), "  a\n  b");
  }
}

TEST(testUDESpecConformance) {
  // Quoted dotted key is a literal key, unquoted dotted key nests
  {
    asvJSON j;
    ASSERT(j.fromUDE("\"a.b.c\": 1\n"));
    ASSERT(j.get("a") == nullptr);
    auto it = j.getRoot()->obj->find("a.b.c");
    ASSERT(it != j.getRoot()->obj->end());
    ASSERT_EQ(it->second->getInt(), int64_t(1));
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("a.b.c: 1\n"));
    ASSERT(j.getNested("a") != nullptr);
    ASSERT_EQ(j.getInt("a.b.c"), int64_t(1));
  }
  // Literal dotted key round-trips through the serializer
  {
    asvJSON j;
    j.putInt("a.b.c", 1);
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(j2.get("a") == nullptr);
    auto it = j2.getRoot()->obj->find("a.b.c");
    ASSERT(it != j2.getRoot()->obj->end());
    ASSERT_EQ(it->second->getInt(), int64_t(1));
  }
  // Document separator is exact: a comment merely containing the phrase
  // "End of Document" must not split the stream
  {
    asvJSON j;
    ASSERT(j.fromUDE("a: 1\n// we reached the End of Document phase\nb: 2\n"));
    ASSERT(j.getRoot() != nullptr && j.getRoot()->type == asvJSONValue::OBJECT);
    ASSERT_EQ(j.getInt("a"), int64_t(1));
    ASSERT_EQ(j.getInt("b"), int64_t(2));
  }
  // A leading '+' is not a valid UDE number sign (SIGNED_INT ::= '-'? INT)
  {
    asvJSON j;
    ASSERT(j.fromUDE("x: +42\n"));
    ASSERT(j.get("x") != nullptr && j.get("x")->type == asvJSONValue::STRING);
    ASSERT_EQ(std::string(j.getString("x")), "+42");
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("x: -42\n"));
    ASSERT(j.get("x") != nullptr && j.get("x")->type == asvJSONValue::INT);
    ASSERT_EQ(j.getInt("x"), int64_t(-42));
  }
  // Unterminated block comment is an error
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: 1\n/* dangling"));
  }
}

TEST(testUDEStrict) {
  // All keys must be quoted in strict mode
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: 1\n", true));
  }
  {
    asvJSON j;
    ASSERT(j.fromUDE("\"a\": 1\n", true));
    ASSERT_EQ(j.getInt("a"), int64_t(1));
  }
  // Duplicate keys rejected in strict mode
  {
    asvJSON j;
    ASSERT(!j.fromUDE("\"a\": 1\n\"a\": 2\n", true));
  }
  // Strict serialization round-trips (keys re-quoted)
  {
    asvJSON j;
    j.parse(std::string(R"({"name":"x","nested":{"v":1}})"));
    std::string ude = j.toUDE(true);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude, true));
    ASSERT_EQ(std::string(j2.getString("name")), "x");
    ASSERT_EQ(j2.getInt("nested.v"), int64_t(1));
  }
}

TEST(testUDEAnchors) {
  // Basic alias after the anchor definition
  {
    asvJSON j;
    ASSERT(j.fromUDE("name: &a hello\ncopy: *a\n"));
    ASSERT_EQ(std::string(j.getString("name")), "hello");
    ASSERT_EQ(std::string(j.getString("copy")), "hello");
  }
  // Anchor values survive later replacement of the key (no dangling pointer)
  {
    asvJSON j;
    ASSERT(j.fromUDE("x: &a hello\nx: replaced\ny: *a\n"));
    ASSERT_EQ(std::string(j.getString("x")), "replaced");
    ASSERT_EQ(std::string(j.getString("y")), "hello");
  }
  // Self-referential alias is rejected
  {
    asvJSON j;
    ASSERT(!j.fromUDE("a: &x *x\n"));
  }
}

TEST(testUDERawNumberRoundTrip) {
  // High-precision decimal stored as raw_number string by JSON parser
  // must round-trip through UDE as a string (quoted), not a double.
  {
    asvJSON j;
    ASSERT(j.parse("{\"d\":0.42857142857142855}"));
    auto* v = j.getRoot()->get("d");
    ASSERT(v != nullptr);
    ASSERT(v->type == asvJSONValue::STRING);
    ASSERT(v->raw_number);
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(*j.getRoot() == *j2.getRoot());
    auto* v2 = j2.getRoot()->get("d");
    ASSERT(v2->type == asvJSONValue::STRING);
  }
  // Big integer (int64 overflow) stays bare and round-trips
  {
    asvJSON j;
    ASSERT(j.parse("{\"n\":12345678901234567890}"));
    auto* v = j.getRoot()->get("n");
    ASSERT(v->raw_number);
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(*j.getRoot() == *j2.getRoot());
  }
}

TEST(testUDECommentPrefixStrings) {
  // String value starting with "//" must be quoted to avoid comment parsing
  {
    asvJSON j;
    j.parse("{\"k\":\"//}\"}");
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(*j.getRoot() == *j2.getRoot());
    ASSERT_EQ(std::string(j2.getString("k")), "//}");
  }
  // String value starting with "/*"
  {
    asvJSON j;
    j.parse("{\"k\":\"/* block\"}");
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(*j.getRoot() == *j2.getRoot());
    ASSERT_EQ(std::string(j2.getString("k")), "/* block");
  }
  // Plain "//" string
  {
    asvJSON j;
    j.parse("{\"k\":\"//\"}");
    std::string ude = j.toUDE();
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(*j.getRoot() == *j2.getRoot());
    ASSERT_EQ(std::string(j2.getString("k")), "//");
  }
  // Single "/" string should remain unquoted
  {
    asvJSON j;
    j.parse("{\"k\":\"/foo\"}");
    std::string ude = j.toUDE();
    ASSERT(ude.find("/foo") != std::string::npos);
    asvJSON j2;
    ASSERT(j2.fromUDE(ude));
    ASSERT(*j.getRoot() == *j2.getRoot());
  }
}

TEST(testUDEBlockScalarKeepChomping) {
  // |+ with trailing blank line: the join already captures the separator \n
  {
    asvJSON j;
    ASSERT(j.fromUDE("key: |+\n  hello\n\n"));
    ASSERT_EQ(std::string(j.getString("key")), "hello\n");
  }
  // |- strips all trailing newlines
  {
    asvJSON j;
    ASSERT(j.fromUDE("key: |-\n  hello\n\n"));
    ASSERT_EQ(std::string(j.getString("key")), "hello");
  }
  // | (clip) keeps exactly one trailing newline
  {
    asvJSON j;
    ASSERT(j.fromUDE("key: |\n  hello\n\n"));
    ASSERT_EQ(std::string(j.getString("key")), "hello\n");
  }
  // |+ with no trailing blank line
  {
    asvJSON j;
    ASSERT(j.fromUDE("key: |+\n  hello"));
    ASSERT_EQ(std::string(j.getString("key")), "hello");
  }
  // |+ with two trailing blank lines
  {
    asvJSON j;
    ASSERT(j.fromUDE("key: |+\n  hello\n\n\n"));
    ASSERT_EQ(std::string(j.getString("key")), "hello\n\n");
  }
  // Folded >+ with trailing blank line: the blank line becomes a paragraph
  // break ('\n') during folding, same as literal
  {
    asvJSON j;
    ASSERT(j.fromUDE("key: >+\n  hello\n\n"));
    ASSERT_EQ(std::string(j.getString("key")), "hello\n");
  }
}

TEST(testByteSizeNullRoot) {
	asvJSON j;
	ASSERT_EQ(j.byteSize(), 0);
}

TEST(testByteSizeEmptyObject) {
	asvJSON j;
	ASSERT(j.parse(std::string_view("{\"empty\":null}")));
	ASSERT_EQ(j.byteSize(), j.serialize().size());
	ASSERT_EQ(j.byteSize(), size_t(14));
}

TEST(testByteSizeSimpleString) {
	asvJSON j;
	ASSERT(j.parse(std::string_view("\"hello\"")));
	ASSERT_EQ(j.byteSize(), j.serialize().size());
	ASSERT_EQ(j.byteSize(), size_t(7));
}

TEST(testByteSizeNestedStructure) {
	asvJSON j;
	ASSERT(j.parse(std::string_view("{\"key1\":\"value1\",\"nested\":{\"nested_key\":42}}")));
	ASSERT_EQ(j.byteSize(), j.serialize().size());
}

TEST(testByteSizeWithCountAndName) {
	asvJSON j;
	ASSERT(j.parse(std::string_view("{\"name\":\"test\",\"count\":5}")));
	ASSERT_EQ(j.byteSize(), j.serialize().size());
}

TEST(testFromAnyAndFormatType) {
	{
		asvJSON doc;
		ASSERT(doc.fromAny(R"({"a":1})"));
		ASSERT(doc.getFormatType() == FormatType::JSON);
		ASSERT(doc.serialize().find("\"a\"") != std::string::npos);
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny("a: 1\nb: 2\n"));
		ASSERT(doc.getFormatType() == FormatType::YAML);
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny(R"(<root><a>1</a></root>)"));
		ASSERT(doc.getFormatType() == FormatType::XML);
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny("name,age,city\nAlice,30,NY\nBob,25,LA\n"));
		ASSERT(doc.getFormatType() == FormatType::CSV);
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny("[section]\nkey = value\n"));
		ASSERT(doc.getFormatType() == FormatType::INI);
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny("(root (a 1))"));
		ASSERT(doc.getFormatType() == FormatType::SExpr);
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny("{a: 1}"));
		ASSERT(doc.getFormatType() == FormatType::JSON5);
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny("[1]\n[2]\n"));
		ASSERT(doc.getFormatType() == FormatType::JSONLines);
	}
	{
		asvJSON doc;
		ASSERT(!doc.fromAny(""));
	}
	{
		asvJSON doc;
		ASSERT(doc.fromAny(R"({"a":1})"));
		ASSERT(doc.getFormatType() == FormatType::JSON);
		doc.fromAny("a: 1\n");
		ASSERT(doc.getFormatType() == FormatType::YAML);
	}
}

TEST(testJSONSchema) {
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","properties":{"name":{"type":"string"},"age":{"type":"number"}},"required":["name"]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"name":"Alice","age":30})")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"age":30})")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
		ASSERT(!err.empty());
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"array","items":{"type":"number"}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([1,2,3])")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"array","items":{"type":"number"}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([1,"two",3])")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"string","minLength":2,"maxLength":5})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"("abc")")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"string","minLength":2,"maxLength":5})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"("a")")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"number","minimum":0,"maximum":10})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"number","minimum":0,"maximum":10})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("15")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","properties":{"x":{"type":"integer"}},"additionalProperties":false})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"x":1,"y":2})")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"enum":[1,2,3]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("2")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"enum":[1,2,3]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("4")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"const":42})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("42")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"allOf":[{"type":"number"},{"minimum":5}]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("10")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"allOf":[{"type":"number"},{"minimum":5}]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("3")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"anyOf":[{"type":"string"},{"type":"number"}]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"("hello")")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"oneOf":[{"type":"string"},{"type":"number"}]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"("hello")")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"not":{"type":"null"}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("42")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"boolean"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("true")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"null"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("null")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"array","items":{"type":"number"},"minItems":2,"maxItems":3})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([1,2])")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"array","items":{"type":"number"},"minItems":2,"maxItems":3})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([1])")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","properties":{"a":{"type":"number"}},"patternProperties":{"^x_":{"type":"string"}},"additionalProperties":false})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"x_foo":"hi","a":1})")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"string","pattern":"^[a-z]+$"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"("hello")")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"string","pattern":"^[a-z]+$"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"("Hello")")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"number","exclusiveMinimum":0,"exclusiveMaximum":10})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"number","exclusiveMinimum":0,"exclusiveMaximum":10})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("0")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","properties":{"a":{"type":"number"}},"required":["a"],"if":{"properties":{"a":{"const":1}}},"then":{"properties":{"b":{"type":"string"}},"required":["b"]}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1,"b":"hi"})")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","properties":{"a":{"type":"number"}},"required":["a"],"if":{"properties":{"a":{"const":1}}},"then":{"properties":{"b":{"type":"string"}},"required":["b"]}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":["string","null"]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("null")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":["string","null"]})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("42")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// Circular $ref must be detected, not overflow the stack.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"definitions":{"a":{"$ref":"#/definitions/a"}},"$ref":"#/definitions/a"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("1")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// Mutual (A -> B -> A) circular $ref.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"definitions":{"a":{"$ref":"#/definitions/b"},"b":{"$ref":"#/definitions/a"}},"$ref":"#/definitions/a"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("1")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// A legitimate recursive schema that consumes a document level per step
	// must still validate (the guard only rejects $ref loops).
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"definitions":{"node":{"type":"object","properties":{"value":{"type":"number"},"child":{"$ref":"#/definitions/node"}},"required":["value"]}},"$ref":"#/definitions/node"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"value":1,"child":{"value":2,"child":{"value":3}}})")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
}

TEST(testJSONSchemaExtras) {
	// H1: an empty document (root == nullptr) must not crash and must
	// validate as JSON null.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"null"})")));
		asvJSON doc;
		ASSERT(doc.validate(schema));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object"})")));
		asvJSON doc;
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// H2: exact int64 comparisons at the full int64 range (no double rounding).
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"number","minimum":9223372036854775807})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("9223372036854775806")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"number","maximum":9223372036854775806})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("9223372036854775807")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"const":9223372036854775807})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("9223372036854775806")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// H2: exact multipleOf for wide integers; double path keeps tolerance.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"multipleOf":2})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view("10000000000000001")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view("10000000000000000")));
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"multipleOf":0.1})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view("0.3")));
		ASSERT(doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view("0.35")));
		ASSERT(!doc.validate(schema, err));
	}
	// M3: draft-04 legacy boolean exclusiveMinimum/Maximum modifiers.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"number","minimum":5,"exclusiveMinimum":true})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view("5")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view("6")));
		ASSERT(doc.validate(schema, err));
	}
	// M2: JSON Pointer percent-encoding and ~ escapes in $ref.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"definitions":{"a b":{"type":"number"}},"$ref":"#/definitions/a%20b"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"definitions":{"a/b":{"type":"number"}},"$ref":"#/definitions/a~1b"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	// M2: an empty reference token ("#/definitions/") resolves to the key "".
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"definitions":{"":{"type":"number"}},"$ref":"#/definitions/"})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view("5")));
		ASSERT(doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view("\"x\"")));
		ASSERT(!doc.validate(schema, err)); // "" holds a number schema, not the definitions bucket
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"$ref":"#/"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(!doc.validate(schema, err)); // no "" key -> unresolved
	}
	// draft-07: keywords sibling to $ref are ignored.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"definitions":{"n":{"type":"number"}},"$ref":"#/definitions/n","type":"string"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	// L4: a $ref cycle reached again through anyOf must be detected, not
	// overflow the stack (previously every branch got a fresh depth counter).
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"anyOf":[{"$ref":"#/definitions/loop"}],"definitions":{"loop":{"anyOf":[{"$ref":"#/definitions/loop"}]}}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		bool ok = doc.validate(schema, err); // must not crash
		ASSERT(!ok);
	}
	// L1: malformed keyword values are schema errors, not silent no-ops.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":5})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
		ASSERT(err.find("Invalid schema") != std::string::npos);
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"minLength":-1})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("\"x\"")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"enum":"x"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"required":"x"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"uniqueItems":1})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([1])")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"multipleOf":0})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"minimum":"x"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"pattern":5})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("\"x\"")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"exclusiveMinimum":"x"})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// L2: uniqueItems fast path for scalars (+ double/string/mixed parity).
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"array","uniqueItems":true})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view(R"([1,2,3])")));
		ASSERT(doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"([1,2,1])")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"([0.0,-0.0])")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"(["a","a"])")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"([1,1.0])")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"([{"a":1},{"a":1}])")));
		ASSERT(!doc.validate(schema, err)); // container fallback
	}
	// minProperties / maxProperties.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","minProperties":2,"maxProperties":2})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view(R"({"a":1,"b":2})")));
		ASSERT(doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		ASSERT(!doc.validate(schema, err));
	}
	// dependencies: property-array form and schema form.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","dependencies":{"a":["b"]}})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"({"a":1,"b":2})")));
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","dependencies":{"a":{"required":["b"]}}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// dependentRequired.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","dependentRequired":{"a":["b"]}})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		ASSERT(!doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"({"a":1,"b":2})")));
		ASSERT(doc.validate(schema, err));
	}
	// contains.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"array","contains":{"type":"number"}})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view(R"([1,"a"])")));
		ASSERT(doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"(["a"])")));
		ASSERT(!doc.validate(schema, err));
	}
	// additionalItems: false with a tuple `items` rejects extras.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"array","items":[{"type":"number"}],"additionalItems":false})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view(R"([1])")));
		ASSERT(doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"([1,2])")));
		ASSERT(!doc.validate(schema, err));
	}
	// propertyNames.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"object","propertyNames":{"pattern":"^[a-z]+$"}})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse(std::string_view(R"({"abc":1})")));
		ASSERT(doc.validate(schema, err));
		ASSERT(doc.parse(std::string_view(R"({"Abc":1})")));
		ASSERT(!doc.validate(schema, err));
	}
	// A key matched by both `properties` and `patternProperties` is checked by
	// both schemas.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"properties":{"a":{"const":1}},"patternProperties":{"^a$":{"const":2}}})")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
	// M4: Unicode code points (not bytes) for minLength/maxLength; a stray
	// continuation byte counts as one character.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"string","minLength":1,"maxLength":1})")));
		asvJSON doc;
		std::string err;
		ASSERT(doc.parse("\"\xF0\x9F\x98\x80\"")); // U+1F600 = one code point
		ASSERT(doc.validate(schema, err));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view(R"({"type":"string","minLength":1})")));
		asvJSON doc;
		ASSERT(doc.parse("\"\x80\"")); // lone continuation byte counts once
		std::string err;
		ASSERT(doc.validate(schema, err));
	}
	// Root-level boolean schemas.
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view("true")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		ASSERT(doc.validate(schema));
	}
	{
		asvJSON schema;
		ASSERT(schema.parse(std::string_view("false")));
		asvJSON doc;
		ASSERT(doc.parse(std::string_view("5")));
		std::string err;
		ASSERT(!doc.validate(schema, err));
	}
}

TEST(testJSONPath) {
	// -- dot notation --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"name":"Alice","age":30})")));
		auto r = doc.query("$.name");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT_EQ(r[0]->str_data, "Alice");
	}
	// -- bracket notation --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"name":"Alice"})")));
		auto r = doc.query("$['name']");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT_EQ(r[0]->str_data, "Alice");
	}
	// -- nested access --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":{"b":{"c":99}}})")));
		auto r = doc.query("$.a.b.c");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 99);
	}
	// -- array index --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30])")));
		auto r = doc.query("$[1]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 20);
	}
	// -- negative index --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30])")));
		auto r = doc.query("$[-1]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 30);
	}
	// -- slice [0:2] --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30,40])")));
		auto r = doc.query("$[0:2]");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT(r[0]->num == 10);
		ASSERT(r[1]->num == 20);
	}
	// -- slice [1:] --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30])")));
		auto r = doc.query("$[1:]");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT(r[0]->num == 20);
		ASSERT(r[1]->num == 30);
	}
	// -- slice [::2] --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30,40,50])")));
		auto r = doc.query("$[::2]");
		ASSERT_EQ(r.size(), size_t(3));
		ASSERT(r[0]->num == 10);
		ASSERT(r[1]->num == 30);
		ASSERT(r[2]->num == 50);
	}
	// -- slice [::-1] --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30])")));
		auto r = doc.query("$[::-1]");
		ASSERT_EQ(r.size(), size_t(3));
		ASSERT(r[0]->num == 30);
		ASSERT(r[1]->num == 20);
		ASSERT(r[2]->num == 10);
	}
	// -- wildcard --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1,"b":2,"c":3})")));
		auto r = doc.query("$.*");
		ASSERT_EQ(r.size(), size_t(3));
	}
	// -- array wildcard --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([1,2,3])")));
		auto r = doc.query("$[*]");
		ASSERT_EQ(r.size(), size_t(3));
	}
	// -- recursive descent --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":{"b":1},"c":{"b":2}})")));
		auto r = doc.query("$..b");
		ASSERT_EQ(r.size(), size_t(2));
	}
	// -- filter: simple --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"v":1},{"v":2},{"v":3}])")));
		auto r = doc.query("$[?(@.v == 2)]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->obj->find("v")->second->num == 2);
	}
	// -- filter: comparison operators --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"v":1},{"v":2},{"v":3}])")));
		auto r = doc.query("$[?(@.v > 1)]");
		ASSERT_EQ(r.size(), size_t(2));
	}
	// -- filter: && and || --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"a":1,"b":10},{"a":2,"b":20},{"a":3,"b":30}])")));
		auto r = doc.query("$[?(@.a == 1 && @.b == 10)]");
		ASSERT_EQ(r.size(), size_t(1));
		auto r2 = doc.query("$[?(@.a == 1 || @.a == 3)]");
		ASSERT_EQ(r2.size(), size_t(2));
	}
	// -- filter: ! (not) --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"v":1},{"v":2},{"v":3}])")));
		auto r = doc.query("$[?(!(@.v == 2))]");
		ASSERT_EQ(r.size(), size_t(2));
	}
	// -- filter: parentheses --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"a":1},{"a":2},{"a":3}])")));
		auto r = doc.query("$[?((@.a == 1) || (@.a == 3))]");
		ASSERT_EQ(r.size(), size_t(2));
	}
	// -- function: length() --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"items":[1,2,3]},{"items":[1,2]}])")));
		auto r = doc.query("$[?(@.items.length() > 2)]");
		ASSERT_EQ(r.size(), size_t(1));
	}
	// -- function: length() on string --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"name":"hello"})")));
		auto r = doc.query("$.name.length()");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 5);
	}
	// -- function: count() --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"x":1},{"x":2}])")));
		auto r = doc.query("$[?(@.x.count() == 1)]");
		ASSERT_EQ(r.size(), size_t(2));
	}
	// -- function: min/max/sum/avg --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"values":[10,20,30]})")));
		auto rMin = doc.query("$.values.min()");
		ASSERT_EQ(rMin.size(), size_t(1));
		ASSERT(rMin[0]->num == 10);
		auto rMax = doc.query("$.values.max()");
		ASSERT_EQ(rMax.size(), size_t(1));
		ASSERT(rMax[0]->num == 30);
		auto rSum = doc.query("$.values.sum()");
		ASSERT_EQ(rSum.size(), size_t(1));
		ASSERT(rSum[0]->type == asvJSONValue::INT);
		ASSERT(rSum[0]->num == 60);
		auto rAvg = doc.query("$.values.avg()");
		ASSERT_EQ(rAvg.size(), size_t(1));
		ASSERT(rAvg[0]->type == asvJSONValue::DOUBLE);
	}
	// -- function: match() (full-string match per RFC 9535) --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"email":"alice@example.com"},{"email":"bob@test.com"}])")));
		auto r = doc.query(R"($[?(match(@.email, 'alice@example\\.com'))])");
		ASSERT_EQ(r.size(), size_t(1));
	}
	// -- function: search() --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"name":"John Smith"},{"name":"Jane Doe"}])")));
		auto r = doc.query("$[?(search(@.name, 'ohn'))]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT_EQ(r[0]->obj->find("name")->second->str_data, "John Smith");
	}
	// -- queryFirst --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":{"b":42}})")));
		auto* v = doc.queryFirst("$.a.b");
		ASSERT(v != nullptr);
		ASSERT(v->num == 42);
	}
	// -- queryFirst: not found --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		auto* v = doc.queryFirst("$.b");
		ASSERT(v == nullptr);
	}
	// -- empty results --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1})")));
		auto r = doc.query("$.b");
		ASSERT_EQ(r.size(), size_t(0));
	}
	// -- isValidPath --
	{
		ASSERT(asvJSON::isValidPath("$"));
		ASSERT(asvJSON::isValidPath("$.name"));
		ASSERT(asvJSON::isValidPath("$[0]"));
		ASSERT(asvJSON::isValidPath("$..name"));
		ASSERT(!asvJSON::isValidPath("name"));
		ASSERT(!asvJSON::isValidPath(""));
	}
	// -- nested filter --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"users":[{"name":"Alice","scores":[90,85,95]},{"name":"Bob","scores":[70,60]}]})")));
		auto r = doc.query("$.users[?(@.scores.length() > 2)].name");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT_EQ(r[0]->str_data, "Alice");
	}
	// -- chained bracket + dot --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"items":[{"name":"a"},{"name":"b"}]})")));
		auto r = doc.query("$.items[0].name");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT_EQ(r[0]->str_data, "a");
	}
	// -- bracket string in object --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":{"b":{"c":99}}})")));
		auto r = doc.query("$['a']['b']['c']");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 99);
	}
	// -- value() function --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"x":42})")));
		auto r = doc.query("$.x.value()");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 42);
	}
	// -- less-than / less-equal --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"v":1},{"v":2},{"v":3}])")));
		auto r = doc.query("$[?(@.v < 3)]");
		ASSERT_EQ(r.size(), size_t(2));
		auto r2 = doc.query("$[?(@.v <= 2)]");
		ASSERT_EQ(r2.size(), size_t(2));
	}
	// -- filter: RFC 9535 existence test (bare query selects the node
	//    regardless of its value: null/false/empty are all selectable) --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"a":[]},{"a":{}},{"a":1},{"a":"x"}])")));
		auto r = doc.query("$[?(@.a)]");
		ASSERT_EQ(r.size(), size_t(4));
	}
	// -- bracket union: names --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":1,"b":2,"c":3})")));
		auto r = doc.query("$['a','c']");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT(r[0]->num == 1);
		ASSERT(r[1]->num == 3);
	}
	// -- bracket union: indexes --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30,40])")));
		auto r = doc.query("$[0,2]");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT(r[0]->num == 10);
		ASSERT(r[1]->num == 30);
	}
	// -- bracket union: mixed --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30])")));
		auto r = doc.query("$[2,0]");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT(r[0]->num == 30);
		ASSERT(r[1]->num == 10);
	}
	// -- slice: negative start/end normalization --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"(["a","b","c","d","e","f","g"])")));
		auto r = doc.query("$[-3:-1]");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT_EQ(r[0]->str_data, "e");
		ASSERT_EQ(r[1]->str_data, "f");
		auto r2 = doc.query("$[-3:]");
		ASSERT_EQ(r2.size(), size_t(3));
		ASSERT_EQ(r2[0]->str_data, "e");
		ASSERT_EQ(r2[1]->str_data, "f");
		ASSERT_EQ(r2[2]->str_data, "g");
	}
	// -- slice: full slice must not hang --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([10,20,30])")));
		auto r = doc.query("$[:]");
		ASSERT_EQ(r.size(), size_t(3));
	}
	// -- filter referencing root with $ --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"threshold":5,"items":[{"v":1},{"v":10}]})")));
		auto r = doc.query("$.items[?(@.v > $.threshold)]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->obj->find("v")->second->num == 10);
		auto r2 = doc.query("$.items[?(@.v > $['threshold'])]");
		ASSERT_EQ(r2.size(), size_t(1));
	}
	// -- bracket applied to multiple recursive-descent results --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":{"book":[1,2,3]},"b":{"book":[4,5]}})")));
		auto r = doc.query("$..book[1]");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT(r[0]->num == 2);
		ASSERT(r[1]->num == 5);
		auto rw = doc.query("$..book[*]");
		ASSERT_EQ(rw.size(), size_t(5));
	}
	// -- nested bracket subscript inside a filter --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"x":[{"arr":[5,6],"v":10},{"arr":[1],"v":20}]})")));
		auto r = doc.query("$.x[?(@.arr[0] == 5)].v");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 10);
	}
}

TEST(testJSONPathExtras) {
	// -- invalid selectors never throw; query() is lenient and returns empty --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([1,2,3])")));
		ASSERT_EQ(doc.query("$[-]").size(), size_t(0));
		ASSERT_EQ(doc.query("$[1.5]").size(), size_t(0));
		ASSERT_EQ(doc.query("$[0:3:0]").size(), size_t(0));
		ASSERT_EQ(doc.query("$[99999999999999999999999999]").size(), size_t(0));
		ASSERT_EQ(doc.query(R"($["\z"])").size(), size_t(0));
	}
	// -- isValidPath is strict (RFC 9535 grammar + I-JSON ranges + step != 0) --
	{
		ASSERT(asvJSON::isValidPath("$"));
		ASSERT(asvJSON::isValidPath("$.a.b[0]['c']"));
		ASSERT(asvJSON::isValidPath("$.*"));
		ASSERT(asvJSON::isValidPath("$..*"));
		ASSERT(asvJSON::isValidPath("$..['a']"));
		ASSERT(asvJSON::isValidPath("$[-1]"));
		ASSERT(asvJSON::isValidPath("$[0:1]"));
		ASSERT(asvJSON::isValidPath("$[::2]"));
		ASSERT(asvJSON::isValidPath("$[?(@.a.length() > 1)]"));
		ASSERT(asvJSON::isValidPath("$[?(match(@.a, 'x'))]"));
		ASSERT(asvJSON::isValidPath("$[?(@.a == 1 && @.b)]"));
		ASSERT(!asvJSON::isValidPath("$[-]"));
		ASSERT(!asvJSON::isValidPath("$[1.5]"));
		ASSERT(!asvJSON::isValidPath("$[01]"));
		ASSERT(!asvJSON::isValidPath("$[0:3:0]"));
		ASSERT(!asvJSON::isValidPath(R"($['ab])"));
		ASSERT(!asvJSON::isValidPath("$[99999999999999999999999999]"));
		ASSERT(!asvJSON::isValidPath("$[?(@.a == )]"));
		ASSERT(!asvJSON::isValidPath("$[?(@.a.match())]"));
		ASSERT(!asvJSON::isValidPath("$[?(match('x', 'y'))]"));
		ASSERT(!asvJSON::isValidPath("$[?(match(@.a))]"));
	}
	// -- descendant segment with bracket selectors --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":{"b":[1,2]},"c":{"b":[3]}})")));
		auto r = doc.query("$..b[0]");
		ASSERT_EQ(r.size(), size_t(2));
		ASSERT(r[0]->num == 1);
		ASSERT(r[1]->num == 3);
	}
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"a":[1,2,3]})")));
		auto r = doc.query("$..[0]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 1);
		auto rw = doc.query("$..[*]");
		ASSERT_EQ(rw.size(), size_t(4));
	}
	// -- comparison with missing selectors: empty == empty, empty < null false --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"a":1},{"a":2}])")));
		auto r = doc.query("$[?(@.missing == @.missing2)]");
		ASSERT_EQ(r.size(), size_t(2));
		auto r2 = doc.query("$[?(@.missing <= null)]");
		ASSERT_EQ(r2.size(), size_t(0));
	}
	// -- int64 precision preserved in filter comparisons and sum() --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"v":9223372036854775807},{"v":9223372036854775806}])")));
		auto r = doc.query("$[?(@.v > 9223372036854775806)]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->obj->find("v")->second->num == 9223372036854775807ll);
		auto r2 = doc.query("$[?(@.v >= 9223372036854775806)]");
		ASSERT_EQ(r2.size(), size_t(2));
	}
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"values":[9223372036854775807,9223372036854775807]})")));
		auto r = doc.query("$.values.sum()");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->type == asvJSONValue::DOUBLE);
		ASSERT(r[0]->dbl == 18446744073709551616.0);
	}
	// -- unicode escapes in name selectors --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"({"A":1})")));
		auto r = doc.query(R"($["\u0041"])");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->num == 1);
	}
	// -- match/search on non-string: LogicalFalse (empty result) --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"a":1}])")));
		ASSERT_EQ(doc.query("$[?(search(@.a, 'x'))]").size(), size_t(0));
		ASSERT_EQ(doc.query("$[?(match(@.a, 'x'))]").size(), size_t(0));
	}
	// -- length() on a scalar is Nothing (not 0), so the filter is false --
	{
		asvJSON doc;
		ASSERT(doc.parse(std::string_view(R"([{"a":1},{"a":[1,2]}])")));
		auto r = doc.query("$[?(@.a.length() > 0)]");
		ASSERT_EQ(r.size(), size_t(1));
		ASSERT(r[0]->obj->find("a")->second->type == asvJSONValue::ARRAY);
	}
}

TEST(testJsonSax) {
	// -- basic object --
	{
		std::vector<std::string> events;
		struct : JsonSaxHandler {
			std::vector<std::string>* ev;
			void onStartObject() override { ev->push_back("{"); }
			void onEndObject() override { ev->push_back("}"); }
			void onKey(std::string_view k) override { ev->push_back(std::string("key:") + std::string(k)); }
			void onStringValue(std::string_view v) override { ev->push_back(std::string("str:") + std::string(v)); }
			void onIntegerValue(int64_t v) override { ev->push_back("int:" + std::to_string(v)); }
			void onDoubleValue(double v) override { ev->push_back("dbl:" + std::to_string(v)); }
			void onBoolValue(bool v) override { ev->push_back(v ? "bool:true" : "bool:false"); }
			void onNullValue() override { ev->push_back("null"); }
		} h;
		h.ev = &events;
		JsonSaxParser parser;
		ASSERT(parser.parse(R"({"a":1,"b":"hello","c":true,"d":null,"e":3.14})", h));
		ASSERT_EQ(events.size(), size_t(12));
		ASSERT_EQ(events[0], "{");
		ASSERT_EQ(events[1], "key:a");
		ASSERT_EQ(events[2], "int:1");
		ASSERT_EQ(events[3], "key:b");
		ASSERT_EQ(events[4], "str:hello");
		ASSERT_EQ(events[5], "key:c");
		ASSERT_EQ(events[6], "bool:true");
		ASSERT_EQ(events[7], "key:d");
		ASSERT_EQ(events[8], "null");
		ASSERT_EQ(events[9], "key:e");
		ASSERT_EQ(events[10].substr(0, 4), "dbl:");
		ASSERT_EQ(events[11], "}");
	}

	// -- basic array --
	{
		std::vector<std::string> events;
		struct : JsonSaxHandler {
			std::vector<std::string>* ev;
			void onStartArray() override { ev->push_back("["); }
			void onEndArray() override { ev->push_back("]"); }
			void onIntegerValue(int64_t v) override { ev->push_back("int:" + std::to_string(v)); }
		} h;
		h.ev = &events;
		JsonSaxParser parser;
		ASSERT(parser.parse("[1,2,3]", h));
		ASSERT_EQ(events.size(), size_t(5));
		ASSERT_EQ(events[0], "[");
		ASSERT_EQ(events[1], "int:1");
		ASSERT_EQ(events[2], "int:2");
		ASSERT_EQ(events[3], "int:3");
		ASSERT_EQ(events[4], "]");
	}

	// -- nested structure --
	{
		int objCount = 0, arrCount = 0;
		struct : JsonSaxHandler {
			int* oc; int* ac;
			void onStartObject() override { (*oc)++; }
			void onStartArray() override { (*ac)++; }
		} h;
		h.oc = &objCount; h.ac = &arrCount;
		JsonSaxParser parser;
		ASSERT(parser.parse(R"({"a":[{"b":1},{"c":[2,3]}]})", h));
		ASSERT_EQ(objCount, 3);
		ASSERT_EQ(arrCount, 2);
	}

	// -- empty object and array --
	{
		int objCount = 0, arrCount = 0;
		struct : JsonSaxHandler {
			int* oc; int* ac;
			void onStartObject() override { (*oc)++; }
			void onStartArray() override { (*ac)++; }
		} h;
		h.oc = &objCount; h.ac = &arrCount;
		JsonSaxParser parser;
		ASSERT(parser.parse("{}", h));
		ASSERT_EQ(objCount, 1);
		ASSERT(parser.parse("[]", h));
		ASSERT_EQ(arrCount, 1);
	}

	// -- string escapes --
	{
		std::string val;
		struct : JsonSaxHandler {
			std::string* v;
			void onStringValue(std::string_view s) override { *v = std::string(s); }
		} h;
		h.v = &val;
		JsonSaxParser parser;
		ASSERT(parser.parse(R"("hello\nworld")", h));
		ASSERT_EQ(val, "hello\nworld");
		ASSERT(parser.parse(R"("tab\there")", h));
		ASSERT_EQ(val, "tab\there");
		ASSERT(parser.parse("\"quote: \\\"hi\\\"\"", h));
		ASSERT_EQ(val, "quote: \"hi\"");
	}

	// -- unicode escape --
	{
		std::string val;
		struct : JsonSaxHandler {
			std::string* v;
			void onStringValue(std::string_view s) override { *v = std::string(s); }
		} h;
		h.v = &val;
		JsonSaxParser parser;
		ASSERT(parser.parse("\"\\u0041\"", h));
		ASSERT_EQ(val, "A");
		ASSERT(parser.parse("\"\\uD83D\\uDE00\"", h));
		ASSERT(val.size() > 0); // emoji
	}

	// -- error: unexpected char --
	{
		bool errored = false;
		struct : JsonSaxHandler {
			bool* e;
			void onError(std::string_view) override { *e = true; }
		} h;
		h.e = &errored;
		JsonSaxParser parser;
		ASSERT(!parser.parse("abc", h));
		ASSERT(errored);
	}

	// -- error: unclosed string --
	{
		bool errored = false;
		struct : JsonSaxHandler {
			bool* e;
			void onError(std::string_view) override { *e = true; }
		} h;
		h.e = &errored;
		JsonSaxParser parser;
		ASSERT(!parser.parse(R"("unclosed)", h));
		ASSERT(errored);
	}

	// -- parseSax via asvJSON --
	{
		std::vector<std::string> events;
		struct : JsonSaxHandler {
			std::vector<std::string>* ev;
			void onStartObject() override { ev->push_back("{"); }
			void onEndObject() override { ev->push_back("}"); }
			void onKey(std::string_view k) override { ev->push_back(std::string("key:") + std::string(k)); }
			void onStringValue(std::string_view v) override { ev->push_back(std::string("str:") + std::string(v)); }
		} h;
		h.ev = &events;
		asvJSON doc;
		ASSERT(doc.parseSax(R"({"x":"y"})", h));
		ASSERT_EQ(events.size(), size_t(4));
	}
}

TEST(testJsonStream) {
	// -- basic: feed in chunks --
	{
		std::vector<std::string> events;
		struct : JsonSaxHandler {
			std::vector<std::string>* ev;
			void onStartObject() override { ev->push_back("{"); }
			void onEndObject() override { ev->push_back("}"); }
			void onKey(std::string_view k) override { ev->push_back(std::string("key:") + std::string(k)); }
			void onStringValue(std::string_view v) override { ev->push_back(std::string("str:") + std::string(v)); }
			void onIntegerValue(int64_t v) override { ev->push_back("int:" + std::to_string(v)); }
		} h;
		h.ev = &events;
		JsonStreamParser sp(h);
		ASSERT(sp.feed(std::string_view("{\"name")));
		ASSERT(sp.feed(std::string_view("\":\"Ali")));
		ASSERT(sp.feed(std::string_view("ce\",\"ag")));
		ASSERT(sp.feed(std::string_view("e\":30}")));
		ASSERT(sp.finish());
		ASSERT_EQ(events.size(), size_t(6));
		ASSERT_EQ(events[0], "{");
		ASSERT_EQ(events[1], "key:name");
		ASSERT_EQ(events[2], "str:Alice");
		ASSERT_EQ(events[3], "key:age");
		ASSERT_EQ(events[4], "int:30");
		ASSERT_EQ(events[5], "}");
	}

	// -- feed byte by byte --
	{
		std::vector<std::string> events;
		struct : JsonSaxHandler {
			std::vector<std::string>* ev;
			void onStartArray() override { ev->push_back("["); }
			void onEndArray() override { ev->push_back("]"); }
			void onIntegerValue(int64_t v) override { ev->push_back("int:" + std::to_string(v)); }
		} h;
		h.ev = &events;
		JsonStreamParser sp(h);
		std::string input = "[1,2,3]";
		for (char c : input) {
			ASSERT(sp.feed(std::string_view(&c, 1)));
		}
		ASSERT(sp.finish());
		ASSERT_EQ(events.size(), size_t(5));
		ASSERT_EQ(events[0], "[");
		ASSERT_EQ(events[1], "int:1");
		ASSERT_EQ(events[2], "int:2");
		ASSERT_EQ(events[3], "int:3");
		ASSERT_EQ(events[4], "]");
	}

	// -- string split across chunks --
	{
		std::string val;
		struct : JsonSaxHandler {
			std::string* v;
			void onStringValue(std::string_view s) override { *v = std::string(s); }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed(R"(  "hel)"));
		ASSERT(sp.feed(R"(lo wor)"));
		ASSERT(sp.feed(R"(ld"  )"));
		ASSERT(sp.finish());
		ASSERT_EQ(val, "hello world");
	}

	// -- number split across chunks --
	{
		int64_t val = 0;
		struct : JsonSaxHandler {
			int64_t* v;
			void onIntegerValue(int64_t i) override { *v = i; }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("12"));
		ASSERT(sp.feed("34"));
		ASSERT(sp.finish());
		ASSERT_EQ(val, 1234);
	}

	// -- double across chunks --
	{
		double val = 0;
		struct : JsonSaxHandler {
			double* v;
			void onDoubleValue(double d) override { *v = d; }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("3.1"));
		ASSERT(sp.feed("4"));
		ASSERT(sp.finish());
		ASSERT(std::abs(val - 3.14) < 0.01);
	}

	// -- nested objects --
	{
		int depth = 0, maxDepth = 0;
		struct : JsonSaxHandler {
			int* depth; int* maxDepth;
			void onStartObject() override { (*depth)++; if (*depth > *maxDepth) *maxDepth = *depth; }
			void onEndObject() override { (*depth)--; }
		} h;
		h.depth = &depth; h.maxDepth = &maxDepth;
		JsonStreamParser sp(h);
		ASSERT(sp.feed(R"({"a":{"b":{"c":1}}})"));
		ASSERT(sp.finish());
		ASSERT_EQ(maxDepth, 3);
	}

	// -- incomplete string error --
	{
		bool errored = false;
		struct : JsonSaxHandler {
			bool* e;
			void onError(std::string_view) override { *e = true; }
		} h;
		h.e = &errored;
		JsonStreamParser sp(h);
		ASSERT(sp.feed(R"("incomplete)"));
		ASSERT(!sp.finish());
		ASSERT(errored);
	}

	// -- empty input --
	{
		JsonSaxHandler h;
		JsonStreamParser sp(h);
		ASSERT(sp.finish());
	}

	// -- single value --
	{
		bool val = false;
		struct : JsonSaxHandler {
			bool* v;
			void onBoolValue(bool b) override { *v = b; }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("true"));
		ASSERT(sp.finish());
		ASSERT(val);
	}

	// -- comments across chunks --
	{
		std::vector<std::string> events;
		struct : JsonSaxHandler {
			std::vector<std::string>* ev;
			void onIntegerValue(int64_t v) override { ev->push_back("int:" + std::to_string(v)); }
		} h;
		h.ev = &events;
		JsonStreamParser sp(h);
		sp.allowComments = true;
		ASSERT(sp.feed("[1, // comment\n2]"));
		ASSERT(sp.finish());
		ASSERT_EQ(events.size(), size_t(2));
		ASSERT_EQ(events[0], "int:1");
		ASSERT_EQ(events[1], "int:2");
	}

	// -- regression: backslash escape split across a chunk boundary --
	// The `\"` must be treated as an escaped quote even when the backslash is the
	// last byte of a chunk and the quote arrives in the next chunk.
	{
		std::string val;
		struct : JsonSaxHandler {
			std::string* v;
			void onStringValue(std::string_view s) override { *v = std::string(s); }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		std::string input = R"(["abc\"def"])";
		for (char c : input) {
			ASSERT(sp.feed(std::string_view(&c, 1)));
		}
		ASSERT(sp.finish());
		ASSERT_EQ(val, "abc\"def");
	}

	// -- regression: literal split across chunks with a compact() in between --
	// `true` arrives as two chunks that straddle the literal; the parser must not
	// compact away the partial literal nor loop forever.
	{
		bool val = false;
		struct : JsonSaxHandler {
			bool* v;
			void onBoolValue(bool b) override { *v = b; }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("tr"));
		ASSERT(sp.feed("ue"));
		ASSERT(sp.finish());
		ASSERT(val);
	}
	{
		bool val = false;
		struct : JsonSaxHandler {
			bool* v;
			void onBoolValue(bool b) override { *v = b; }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		std::string input = "false";
		for (size_t i = 0; i < input.size(); i += 2) {
			ASSERT(sp.feed(std::string_view(input.data() + i, std::min<size_t>(2, input.size() - i))));
		}
		ASSERT(sp.finish());
		ASSERT(!val);
	}
	{
		bool emitted = false;
		struct : JsonSaxHandler {
			bool* e;
			void onNullValue() override { *e = true; }
		} h;
		h.e = &emitted;
		JsonStreamParser sp(h);
		std::string input = "null";
		for (char c : input) {
			ASSERT(sp.feed(std::string_view(&c, 1)));
		}
		ASSERT(sp.finish());
		ASSERT(emitted);
	}

	// -- regression: `//` comment whose first slash lands at a chunk boundary --
	// A dangling `/` at the end of a chunk must not spin forever or error; it
	// waits for the next chunk to form `//` or `/*`.
	{
		std::vector<std::string> events;
		struct : JsonSaxHandler {
			std::vector<std::string>* ev;
			void onKey(std::string_view k) override { ev->push_back("key:" + std::string(k)); }
			void onIntegerValue(int64_t v) override { ev->push_back("int:" + std::to_string(v)); }
		} h;
		h.ev = &events;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("{/"));
		ASSERT(sp.feed("/ c\n\"a\":1}"));
		ASSERT(sp.finish());
		ASSERT_EQ(events.size(), size_t(2));
		ASSERT_EQ(events[0], "key:a");
		ASSERT_EQ(events[1], "int:1");
	}
	// -- regression: block comment `/*` split at the first slash boundary --
	{
		bool got = false;
		int64_t val = 0;
		struct : JsonSaxHandler {
			bool* g; int64_t* v;
			void onIntegerValue(int64_t i) override { *g = true; *v = i; }
		} h;
		h.g = &got; h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("[1, /"));
		ASSERT(sp.feed("* b\n*/2]"));
		ASSERT(sp.finish());
		ASSERT(got);
		ASSERT_EQ(val, 2);
	}
}

TEST(testJsonSaxStrict) {
	// Malformed non-JSON that used to be silently accepted must now fail, in
	// parity with the DOM parser.
	{
		bool errored = false;
		struct : JsonSaxHandler {
			bool* e;
			void onError(std::string_view) override { *e = true; }
		} h;
		h.e = &errored;
		JsonSaxParser p;
		ASSERT(!p.parse("[1,2,}", h)); // trailing garbage
		ASSERT(errored);
	}
	{
		JsonSaxHandler h;
		JsonSaxParser p;
		// Trailing commas are accepted (DOM parity); leading/double commas and
		// missing values are not.
		ASSERT(p.parse("[1,]", h));
		ASSERT(p.parse("{\"a\":1,}", h));
		ASSERT(p.parse("[1,2]", h));
	}
	{
		bool errored = false;
		struct : JsonSaxHandler {
			bool* e;
			void onError(std::string_view) override { *e = true; }
		} h;
		h.e = &errored;
		JsonSaxParser p;
		ASSERT(!p.parse("[,]", h));
		ASSERT(!p.parse("[1 2]", h));
		ASSERT(!p.parse("{\"a\":}", h));
		ASSERT(!p.parse("{\"a\":1\"b\":2}", h));
		ASSERT(!p.parse("[01]", h));
		ASSERT(!p.parse("[1+2]", h));
		ASSERT(!p.parse("[1e+]", h));
	}
	// onError is delivered exactly once per failure.
	{
		int errors = 0;
		struct : JsonSaxHandler {
			int* n;
			void onError(std::string_view) override { (*n)++; }
		} h;
		h.n = &errors;
		JsonSaxParser p;
		ASSERT(!p.parse("abc", h));
		ASSERT_EQ(errors, 1);
	}
	// A phantom NaN continuation is rejected: single-pass previously accepted
	// `parseSax("abc")` style garbage via strtod fallbacks only.
	{
		bool errored = false;
		struct : JsonSaxHandler {
			bool* e;
			void onError(std::string_view) override { *e = true; }
		} h;
		h.e = &errored;
		JsonSaxParser p;
		ASSERT(!p.parse("1e999", h));
		ASSERT(errored);
	}
	// validateUTF8 on the single-pass parser mirrors the DOM parser.
	{
		JsonSaxHandler h;
		JsonSaxParser p;
		std::string bad;
		bad += '"'; bad += static_cast<char>(0xFF); bad += '"';
		ASSERT(p.parse(bad, h)); // off: bytes flow through as string content
		p.validateUTF8 = true;
		ASSERT(!p.parse(bad, h));
	}
}

TEST(testJsonStreamStrict) {
	// Malformed input that used to be silently accepted must now fail, in
	// parity with the DOM parser.
	{
		JsonSaxHandler hh;
		// Missing separators / values / invalid numbers are rejected.
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed(R"({"a": })"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("[1 2]"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed(R"({"a":1"b":2})"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("[01]"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("[1+2]"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("[1e+]"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("[-]"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("[,]"));
		}
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("{,}"));
		}
		// Double comma in the middle is rejected.
		{
			JsonStreamParser sp(hh);
			ASSERT(!sp.feed("[1,,2]"));
		}
		// Trailing commas are accepted (DOM parity).
		{ // [1,]
			JsonStreamParser sp(hh);
			ASSERT(sp.feed(std::string("[1,]")));
			ASSERT(sp.finish());
		}
		{ // {"a":1,}
			JsonStreamParser sp(hh);
			ASSERT(sp.feed(std::string("{\"a\":1,}")));
			ASSERT(sp.finish());
		}
		{ // {"a":1, + }
			JsonStreamParser sp(hh);
			ASSERT(sp.feed(std::string("{\"a\":1,")));
			ASSERT(sp.feed("}"));
			ASSERT(sp.finish());
		}
	}
	// Escaped keys are unescaped, matching the single-pass parser and the DOM.
	{
		std::string key;
		struct : JsonSaxHandler {
			std::string* k;
			void onKey(std::string_view kk) override { *k = std::string(kk); }
		} h;
		h.k = &key;
		JsonStreamParser sp(h);
		std::string input = R"({"a\u0041":1})";
		for (char c : input) ASSERT(sp.feed(std::string_view(&c, 1)));
		ASSERT(sp.finish());
		ASSERT_EQ(key, "aA");
	}
	// NaN/Infinity literals split across chunk boundaries.
	{
		bool nan = false;
		int ev = 0;
		struct : JsonSaxHandler {
			bool* n; int* e;
			void onDoubleValue(double d) override { (*e)++; if (std::isnan(d)) *n = true; }
		} h;
		h.n = &nan; h.e = &ev;
		JsonStreamParser sp(h);
		sp.allowNaNInfinity = true;
		const char* t = "[NaN]";
		for (size_t i = 0; i < 5; i++) ASSERT(sp.feed(std::string_view(&t[i], 1)));
		ASSERT(sp.finish());
		ASSERT(nan);
	}
	{
		bool inf = false, ninf = false;
		int ev = 0;
		struct : JsonSaxHandler {
			bool* p; bool* m; int* e;
			void onDoubleValue(double d) override { (*e)++; if (std::isinf(d)) { if (d < 0) *m = true; else *p = true; } }
		} h;
		h.p = &inf; h.m = &ninf; h.e = &ev;
		JsonStreamParser sp(h);
		sp.allowNaNInfinity = true;
		const std::string t = "[Infinity,-Infinity]";
		for (size_t i = 0; i < t.size(); i++) ASSERT(sp.feed(std::string_view(&t[i], 1)));
		ASSERT(sp.finish());
		ASSERT(inf); ASSERT(ninf);
	}
	// A lone `-` at the end of a chunk can start `-Infinity` in the next one.
	{
		bool ninf = false;
		struct : JsonSaxHandler {
			bool* n;
			void onDoubleValue(double d) override { if (std::isinf(d) && d < 0) *n = true; }
		} h;
		h.n = &ninf;
		JsonStreamParser sp(h);
		sp.allowNaNInfinity = true;
		ASSERT(sp.feed("[-"));
		ASSERT(sp.feed("Infinity]"));
		ASSERT(sp.finish());
		ASSERT(ninf);
	}
	// Comment after ':' must resume value parsing.
	{
		int64_t val = 0;
		struct : JsonSaxHandler {
			int64_t* v;
			void onIntegerValue(int64_t i) override { *v = i; }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed(R"({"a": // c
1})"));
		ASSERT(sp.finish());
		ASSERT_EQ(val, 1);
	}
	{
		int64_t val = 0;
		struct : JsonSaxHandler {
			int64_t* v;
			void onIntegerValue(int64_t i) override { *v = i; }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed(R"({"a":/*x*/2})"));
		ASSERT(sp.finish());
		ASSERT_EQ(val, 2);
	}
	// JSON5-style /* */ comments resume correctly.
	{
		JsonSaxHandler hh;
		JsonStreamParser sp(hh);
		ASSERT(sp.feed("[1, /* x */ 2]"));
		ASSERT(sp.finish());
	}
	// validateUTF8 with a multibyte char split across chunks.
	{
		std::string val;
		struct : JsonSaxHandler {
			std::string* v;
			void onStringValue(std::string_view s) override { *v = std::string(s); }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		sp.validateUTF8 = true;
		ASSERT(sp.feed("[\""));
		ASSERT(sp.feed("\xC2\xA9")); // U+00A9 split
		ASSERT(sp.feed("\"]"));
		ASSERT(sp.finish());
		ASSERT(val == "\xC2\xA9");
	}
	// validateUTF8 rejects invalid bytes and truncated tails.
	{
		struct : JsonSaxHandler {} h;
		JsonStreamParser sp(h);
		sp.validateUTF8 = true;
		ASSERT(!sp.feed("[\"\xFF\"]"));
	}
	{
		struct : JsonSaxHandler {} h;
		JsonStreamParser sp(h);
		sp.validateUTF8 = true;
		ASSERT(sp.feed("[1,\xC2"));
		ASSERT(!sp.finish()); // truncated 2-byte sequence at EOF
	}
	// A UTF-8 BOM is stripped from the stream.
	{
		std::string val;
		struct : JsonSaxHandler {
			std::string* v;
			void onStringValue(std::string_view s) override { *v = std::string(s); }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("\xEF\xBB\xBF\"x\""));
		ASSERT(sp.finish());
		ASSERT_EQ(val, "x");
	}
	{
		std::string val;
		struct : JsonSaxHandler {
			std::string* v;
			void onStringValue(std::string_view s) override { *v = std::string(s); }
		} h;
		h.v = &val;
		JsonStreamParser sp(h);
		ASSERT(sp.feed("\xEF"));
		ASSERT(sp.feed("\xBB\xBF\"y\""));
		ASSERT(sp.finish());
		ASSERT_EQ(val, "y");
	}
	// Per-value size limits are enforced (previously a bypass in JsonSaxToDom).
	{
		struct : JsonSaxHandler {} h;
		JsonStreamParser sp(h);
		std::string big = "\"";
		big.append(asvJSONValue::MAX_STRING_LEN + 1, 'a');
		big += '"';
		ASSERT(!sp.feed(big));
	}
	{
		struct : JsonSaxHandler {} h;
		JsonStreamParser sp(h);
		std::string arr = "[";
		for (size_t i = 0; i < asvJSONValue::MAX_ARRAY_SIZE; i++) arr += "0,";
		arr += "0]";
		ASSERT(!sp.feed(arr)); // MAX_ARRAY_SIZE exceeded
	}
	{
		struct : JsonSaxHandler {} h;
		JsonStreamParser sp(h);
		std::string obj = "{\"a\":0";
		for (size_t i = 0; i < asvJSONValue::MAX_OBJECT_SIZE; i++) obj += ",\"a\":0";
		obj += "}";
		ASSERT(!sp.feed(obj)); // MAX_OBJECT_SIZE exceeded (key events are counted)
	}
}

TEST(testSaxToDom) {
	// -- basic object --
	{
		JsonSaxToDom adapter;
		JsonSaxParser parser;
		ASSERT(parser.parse(R"({"name":"Alice","age":30,"active":true,"data":null})", adapter));
		auto result = adapter.getResult();
		ASSERT(result != nullptr);
		ASSERT_EQ(result->type, asvJSONValue::OBJECT);
		ASSERT_EQ(result->obj->find("name")->second->str_data, "Alice");
		ASSERT(result->obj->find("age")->second->num == 30);
		ASSERT(result->obj->find("active")->second->getBool());
		ASSERT(result->obj->find("data")->second->type == asvJSONValue::NULL_VAL);
	}

	// -- basic array --
	{
		JsonSaxToDom adapter;
		JsonSaxParser parser;
		ASSERT(parser.parse("[1,2,3]", adapter));
		auto result = adapter.getResult();
		ASSERT(result != nullptr);
		ASSERT_EQ(result->type, asvJSONValue::ARRAY);
		ASSERT_EQ(result->arr->size(), size_t(3));
		ASSERT((*result->arr)[0]->num == 1);
		ASSERT((*result->arr)[1]->num == 2);
		ASSERT((*result->arr)[2]->num == 3);
	}

	// -- nested structure --
	{
		JsonSaxToDom adapter;
		JsonSaxParser parser;
		ASSERT(parser.parse(R"({"items":[{"x":10},{"x":20}]})", adapter));
		auto result = adapter.getResult();
		ASSERT(result != nullptr);
		auto* items = result->obj->find("items")->second.get();
		ASSERT_EQ(items->type, asvJSONValue::ARRAY);
		ASSERT_EQ(items->arr->size(), size_t(2));
		ASSERT((*items->arr)[0]->obj->find("x")->second->num == 10);
		ASSERT((*items->arr)[1]->obj->find("x")->second->num == 20);
	}

	// -- via streaming parser --
	{
		JsonSaxToDom adapter;
		JsonStreamParser sp(adapter);
		ASSERT(sp.feed(R"({"a":1,"b":[2,3]})"));
		ASSERT(sp.finish());
		auto result = adapter.getResult();
		ASSERT(result != nullptr);
		ASSERT(result->obj->find("a")->second->num == 1);
		auto* b = result->obj->find("b")->second.get();
		ASSERT_EQ(b->arr->size(), size_t(2));
		ASSERT((*b->arr)[0]->num == 2);
		ASSERT((*b->arr)[1]->num == 3);
	}

	// -- round-trip: DOM -> SAX -> DOM --
	{
		asvJSON original;
		original.parse(std::string_view(R"({"x":42,"y":"hello","z":[1,true,null]})"));
		std::string serialized = original.serialize();

		JsonSaxToDom adapter;
		JsonSaxParser parser;
		parser.parse(serialized, adapter);
		auto result = adapter.getResult();

		// Verify values directly on the SAX-constructed DOM
		ASSERT_EQ(result->obj->find("x")->second->num, 42.0);
		ASSERT_EQ(result->obj->find("y")->second->str_data, "hello");
		auto* z = result->obj->find("z")->second.get();
		ASSERT_EQ(z->type, asvJSONValue::ARRAY);
		ASSERT_EQ(z->arr->size(), size_t(3));
		ASSERT((*z->arr)[0]->num == 1);
		ASSERT((*z->arr)[1]->getBool());
		ASSERT((*z->arr)[2]->type == asvJSONValue::NULL_VAL);
	}

	// -- streaming to DOM with large chunked input --
	{
		JsonSaxToDom adapter;
		JsonStreamParser sp(adapter);
		// Feed a large array in small chunks
		std::string input = "[";
		for (int i = 0; i < 100; i++) {
			if (i > 0) input += ",";
			input += std::to_string(i);
		}
		input += "]";
		// Feed in 16-byte chunks
		for (size_t i = 0; i < input.size(); i += 16) {
			size_t len = std::min(size_t(16), input.size() - i);
			ASSERT(sp.feed(std::string_view(input.data() + i, len)));
		}
		ASSERT(sp.finish());
		auto result = adapter.getResult();
		ASSERT(result != nullptr);
		ASSERT_EQ(result->type, asvJSONValue::ARRAY);
		ASSERT_EQ(result->arr->size(), size_t(100));
		ASSERT((*result->arr)[0]->num == 0);
		ASSERT((*result->arr)[99]->num == 99);
	}
}

TEST(testReadWriteFileAny) {
	// Helper: write a temp file, read it back, verify, clean up
	auto tmpFile = [](const std::string& ext) -> std::string {
		return "test_tmp_" + ext + "." + ext;
	};
	auto cleanup = [](const std::string& path) { std::remove(path.c_str()); };

	// --- JSON round-trip ---
	{
		std::string path = tmpFile("json");
		{
			asvJSON w;
			w.parse(R"({"x":1,"y":"hello"})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("x"), 1);
			ASSERT_EQ(r.getString("y"), "hello");
		}
		cleanup(path);
	}

	// --- YAML round-trip ---
	{
		std::string path = tmpFile("yaml");
		{
			asvJSON w;
			w.parse(R"({"a":10,"b":"world"})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("a"), 10);
			ASSERT_EQ(r.getString("b"), "world");
		}
		cleanup(path);
	}

	// --- .yml extension ---
	{
		std::string path = tmpFile("yml");
		{
			asvJSON w;
			w.parse(R"({"k":"v"})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getString("k"), "v");
		}
		cleanup(path);
	}

	// --- XML round-trip ---
	{
		std::string path = tmpFile("xml");
		{
			asvJSON w;
			w.parse(R"({"tag":"val"})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getString("tag"), "val");
		}
		cleanup(path);
	}

	// --- CSV round-trip ---
	{
		std::string path = tmpFile("csv");
		{
			asvJSON w;
			w.parse(R"([{"a":1,"b":2},{"a":3,"b":4}])");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getRoot()->type, asvJSONValue::ARRAY);
			auto& arr = *r.getRoot()->arr;
			ASSERT_EQ(arr[0]->obj->find("a")->second->getInt(), 1);
			ASSERT_EQ(arr[1]->obj->find("b")->second->getInt(), 4);
		}
		cleanup(path);
	}

	// --- JSON5 round-trip ---
	{
		std::string path = tmpFile("json5");
		{
			asvJSON w;
			w.parse(R"({"m":42})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("m"), 42);
		}
		cleanup(path);
	}

	// --- JSON Lines round-trip ---
	{
		std::string path = tmpFile("jsonl");
		{
			asvJSON w;
			w.parse(R"({"a":1})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			// JSONLines wraps into an array
			ASSERT_EQ(r.getRoot()->type, asvJSONValue::ARRAY);
			ASSERT_EQ((*r.getRoot()->arr)[0]->obj->find("a")->second->getInt(), 1);
		}
		cleanup(path);
	}

	// --- TOML round-trip ---
	{
		std::string path = tmpFile("toml");
		{
			asvJSON w;
			w.parse(R"({"title":"test","value":99})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			// TOML may wrap in a table structure, check via pointer
			ASSERT(r.getInt("value") == 99 || r.getInt("title.value") == 99);
		}
		cleanup(path);
	}

	// --- INI round-trip ---
	{
		std::string path = tmpFile("ini");
		{
			asvJSON w;
			w.parse(R"({"key":"val","num":7})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getString("key"), "val");
			ASSERT_EQ(r.getInt("num"), 7);
		}
		cleanup(path);
	}

	// --- S-Expression round-trip ---
	{
		std::string path = tmpFile("sexp");
		{
			asvJSON w;
			w.parse(R"({"x":1})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("x"), 1);
		}
		cleanup(path);
	}

	// --- TOON round-trip ---
	{
		std::string path = tmpFile("toon");
		{
			asvJSON w;
			w.parse(R"({"p":"q","n":5})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getString("p"), "q");
			ASSERT_EQ(r.getInt("n"), 5);
		}
		cleanup(path);
	}

	// --- UDE round-trip ---
	{
		std::string path = tmpFile("ude");
		{
			asvJSON w;
			w.parse(R"({"u":true})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT(r.getBool("u"));
		}
		cleanup(path);
	}

	// --- BSON round-trip (binary) ---
	{
		std::string path = tmpFile("bson");
		{
			asvJSON w;
			w.parse(R"({"b":42,"s":"ok"})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("b"), 42);
			ASSERT_EQ(r.getString("s"), "ok");
		}
		cleanup(path);
	}

	// --- MessagePack round-trip (binary) ---
	{
		std::string path = tmpFile("msgpack");
		{
			asvJSON w;
			w.parse(R"({"m":"data","v":7})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getString("m"), "data");
			ASSERT_EQ(r.getInt("v"), 7);
		}
		cleanup(path);
	}

	// --- CBOR round-trip (binary) ---
	{
		std::string path = tmpFile("cbor");
		{
			asvJSON w;
			w.parse(R"({"c":true})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT(r.getBool("c"));
		}
		cleanup(path);
	}

	// --- Unknown extension falls back to fromAny ---
	{
		std::string path = "test_tmp_fallback.xyz";
		{
			std::ofstream out(path);
			out << R"({"z":99})";
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("z"), 99);
		}
		cleanup(path);
	}

	// --- .gz suffix strips to .json ---
	{
		std::string path = "test_tmp_strip.json.gz";
		{
			std::ofstream out(path);
			out << R"({"g":1})";
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("g"), 1);
		}
		cleanup(path);
	}

	// --- writeToFileAny unknown ext defaults to JSON ---
	{
		std::string path = "test_tmp_unknown.xyz";
		{
			asvJSON w;
			w.parse(R"({"w":5})");
			ASSERT(w.writeToFileAny(path));
		}
		{
			asvJSON r;
			ASSERT(r.readFromFileAny(path));
			ASSERT_EQ(r.getInt("w"), 5);
		}
		cleanup(path);
	}

	// --- nonexistent file returns false ---
	{
		asvJSON r;
		ASSERT(!r.readFromFileAny("nonexistent_file_12345.json"));
	}

	// --- pretty flag works for JSON writeToFileAny ---
	{
		std::string path = tmpFile("json");
		{
			asvJSON w;
			w.parse(R"({"p":1})");
			ASSERT(w.writeToFileAny(path, true));
		}
		{
			std::ifstream in(path);
			std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			ASSERT(content.find('\n') != std::string::npos); // pretty = newlines
		}
		cleanup(path);
	}
}

TEST(testOperatorBracket) {
	// --- Object key access ---
	{
		asvJSON doc;
		doc.parse(R"({"name":"Alice","age":30})");
		ASSERT_EQ(std::string(doc["name"].getString()), "Alice");
		ASSERT_EQ(doc["age"].getInt(), 30);
	}
	// --- Nested access ---
	{
		asvJSON doc;
		doc.parse(R"({"user":{"address":{"city":"Moscow","zip":"101000"}}})");
		ASSERT_EQ(std::string(doc["user"]["address"]["city"].getString()), "Moscow");
		ASSERT_EQ(std::string(doc["user"]["address"]["zip"].getString()), "101000");
	}
	// --- Array index access ---
	{
		asvJSON doc;
		doc.parse(R"({"items":[10,20,30]})");
		ASSERT_EQ(doc["items"][0].getInt(), 10);
		ASSERT_EQ(doc["items"][1].getInt(), 20);
		ASSERT_EQ(doc["items"][2].getInt(), 30);
	}
	// --- Null propagation: missing key returns null proxy ---
	{
		asvJSON doc;
		doc.parse(R"({"a":1})");
		auto proxy = doc["nonexistent"];
		ASSERT(!proxy);
		ASSERT(proxy.isNull());
		// Chaining on null should stay null
		auto deep = doc["nonexistent"]["foo"]["bar"];
		ASSERT(!deep);
		ASSERT(deep.isNull());
	}
	// --- Null propagation: non-object intermediate ---
	{
		asvJSON doc;
		doc.parse(R"({"a":"string"})");
		auto proxy = doc["a"]["key"];
		ASSERT(!proxy);
	}
	// --- Mixed object/array navigation ---
	{
		asvJSON doc;
		doc.parse(R"({"users":[{"name":"Alice","scores":[90,95]},{"name":"Bob","scores":[80,85]}]})");
		ASSERT_EQ(std::string(doc["users"][0]["name"].getString()), "Alice");
		ASSERT_EQ(doc["users"][0]["scores"][1].getInt(), 95);
		ASSERT_EQ(std::string(doc["users"][1]["name"].getString()), "Bob");
		ASSERT_EQ(doc["users"][1]["scores"][0].getInt(), 80);
	}
	// --- Type checking via proxy ---
	{
		asvJSON doc;
		doc.parse(R"({"num":42,"str":"hello","flag":true,"flt":3.14,"nil":null})");
		ASSERT(doc["num"].type() == asvJSONValue::INT);
		ASSERT(doc["str"].type() == asvJSONValue::STRING);
		ASSERT(doc["flag"].type() == asvJSONValue::BOOL_VAL);
		ASSERT(doc["flt"].type() == asvJSONValue::DOUBLE);
		ASSERT(doc["nil"].type() == asvJSONValue::NULL_VAL);
		ASSERT_EQ(doc["num"].getInt(), int64_t(42));
		ASSERT(doc["flag"].getBool());
	}
	// --- Proxy from asvJSONValue directly ---
	{
		auto root = asvJSONValue::makeObject();
		auto inner = asvJSONValue::makeObject();
		inner->obj->emplace("x", asvJSONValue::makeInt(42));
		root->obj->emplace("a", std::move(inner));
		auto proxy = (*root)["a"]["x"];
		ASSERT(proxy);
		ASSERT_EQ(proxy.getInt(), 42);
	}
	// --- Empty doc ---
	{
		asvJSON doc;
		doc.parse("{}");
		auto proxy = doc["key"];
		ASSERT(!proxy);
	}
	// --- hasKey via proxy ---
	{
		asvJSON doc;
		doc.parse(R"({"user":{"name":"Alice"}})");
		ASSERT(doc["user"].hasKey("name"));
		ASSERT(!doc["user"].hasKey("age"));
		ASSERT(!doc["missing"].hasKey("anything"));
	}
	// --- size via proxy ---
	{
		asvJSON doc;
		doc.parse(R"({"arr":[1,2,3],"obj":{"a":1}})");
		ASSERT_EQ(doc["arr"].size(), size_t(3));
		ASSERT_EQ(doc["obj"].size(), size_t(1));
		ASSERT_EQ(doc["missing"].size(), size_t(0));
	}
}

TEST(testIteratorAPI) {
	// --- Object iteration on asvJSON (for auto& [key, val] : doc) ---
	{
		asvJSON doc;
		doc.parse(R"({"name":"Alice","age":30,"active":true})");
		std::map<std::string, std::string> collected;
		for (auto& [key, val] : doc) {
			if (val->type == asvJSONValue::STRING) collected[key] = val->getString();
			else if (val->type == asvJSONValue::INT) collected[key] = std::to_string(val->getInt());
			else if (val->type == asvJSONValue::BOOL_VAL) collected[key] = val->getBool() ? "true" : "false";
		}
		ASSERT_EQ(collected.size(), size_t(3));
		ASSERT_EQ(collected["name"], "Alice");
		ASSERT_EQ(collected["age"], "30");
		ASSERT_EQ(collected["active"], "true");
	}
	// --- Object iteration on asvJSONValue (for auto& [key, val] : *val) ---
	{
		auto root = asvJSONValue::makeObject();
		root->obj->emplace("x", asvJSONValue::makeInt(10));
		root->obj->emplace("y", asvJSONValue::makeInt(20));
		int64_t sum = 0;
		for (auto& [key, val] : *root) {
			if (val->type == asvJSONValue::INT) sum += val->getInt();
		}
		ASSERT_EQ(sum, int64_t(30));
	}
	// --- Object iteration on JsonProxy ---
	{
		asvJSON doc;
		doc.parse(R"({"a":{"x":1,"y":2},"b":3})");
		int64_t sum = 0;
		for (auto& [key, val] : doc["a"]) {
			if (val->type == asvJSONValue::INT) sum += val->getInt();
		}
		ASSERT_EQ(sum, int64_t(3));
	}
	// --- Null proxy iteration (empty range) ---
	{
		asvJSON doc;
		doc.parse(R"({"x":1})");
		size_t count = 0;
		for (auto& [key, val] : doc["missing"]) { (void)key; (void)val; count++; }
		ASSERT_EQ(count, size_t(0));
	}
	// --- Array iteration via values() on asvJSON ---
	{
		asvJSON doc;
		doc.parse(R"([10,20,30])");
		int64_t sum = 0;
		for (auto& val : doc.values()) {
			if (val->type == asvJSONValue::INT) sum += val->getInt();
		}
		ASSERT_EQ(sum, int64_t(60));
	}
	// --- Array iteration via values() on asvJSONValue ---
	{
		auto root = asvJSONValue::makeArray();
		root->arr->push_back(asvJSONValue::makeInt(5));
		root->arr->push_back(asvJSONValue::makeInt(15));
		int64_t sum = 0;
		for (auto& val : root->values()) {
			sum += val->getInt();
		}
		ASSERT_EQ(sum, int64_t(20));
	}
	// --- Array iteration via JsonProxy::values() ---
	{
		asvJSON doc;
		doc.parse(R"({"items":[{"id":1},{"id":2},{"id":3}]})");
		int64_t total = 0;
		for (auto& item : doc["items"].values()) {
			auto* id = item->get("id");
			if (id) total += id->getInt();
		}
		ASSERT_EQ(total, int64_t(6));
	}
	// --- Empty object iteration ---
	{
		asvJSON doc;
		doc.parse("{}");
		size_t count = 0;
		for (auto& [key, val] : doc) { (void)key; (void)val; count++; }
		ASSERT_EQ(count, size_t(0));
	}
	// --- Empty array iteration ---
	{
		asvJSON doc;
		doc.parse("[]");
		size_t count = 0;
		for (auto& val : doc.values()) { (void)val; count++; }
		ASSERT_EQ(count, size_t(0));
	}
	// --- Const iteration ---
	{
		asvJSON doc;
		doc.parse(R"({"a":1,"b":2})");
		const auto& cdoc = doc;
		int64_t sum = 0;
		for (const auto& [key, val] : cdoc) {
			if (val->type == asvJSONValue::INT) sum += val->getInt();
		}
		ASSERT_EQ(sum, int64_t(3));
	}
	// --- Nested: iterate object inside array ---
	{
		asvJSON doc;
		doc.parse(R"({"users":[{"name":"Alice","score":90},{"name":"Bob","score":80}]})");
		int64_t totalScore = 0;
		for (auto& user : doc["users"].values()) {
			auto* score = user->get("score");
			if (score) totalScore += score->getInt();
		}
		ASSERT_EQ(totalScore, int64_t(170));
	}
	// --- getRoot() iteration (backward compat) ---
	{
		asvJSON doc;
		doc.parse(R"({"k":"v"})");
		bool found = false;
		for (auto& [key, val] : *doc.getRoot()) {
			if (key == "k" && std::string(val->getString()) == "v") found = true;
		}
		ASSERT(found);
	}
}

TEST(testEmplacePush) {
	// emplace into object
	{
		auto obj = asvJSONValue::makeObject();
		ASSERT(obj->emplace("x", asvJSONValue::makeInt(42)));
		ASSERT_EQ(obj->get("x")->getInt(), int64_t(42));
		// overwrite
		ASSERT(obj->emplace("x", asvJSONValue::makeInt(99)));
		ASSERT_EQ(obj->get("x")->getInt(), int64_t(99));
	}
	// emplace auto-creates object
	{
		auto val = asvJSONValue::makeNull();
		ASSERT(val->emplace("a", asvJSONValue::makeBool(true)));
		ASSERT(val->type == asvJSONValue::OBJECT);
		ASSERT(val->get("a")->getBool());
	}
	// push into array
	{
		auto arr = asvJSONValue::makeArray();
		ASSERT(arr->push(asvJSONValue::makeInt(1)));
		ASSERT(arr->push(asvJSONValue::makeInt(2)));
		ASSERT_EQ(arr->size(), size_t(2));
		ASSERT_EQ((*arr->arr)[0]->getInt(), int64_t(1));
		ASSERT_EQ((*arr->arr)[1]->getInt(), int64_t(2));
	}
	// push auto-creates array
	{
		auto val = asvJSONValue::makeNull();
		ASSERT(val->push(asvJSONValue::makeStringView("hello")));
		ASSERT(val->type == asvJSONValue::ARRAY);
	}
	// push null returns false
	{
		auto arr = asvJSONValue::makeArray();
		ASSERT(!arr->push(nullptr));
	}
	// emplace null returns false
	{
		auto obj = asvJSONValue::makeObject();
		ASSERT(!obj->emplace("k", nullptr));
	}
}

TEST(testEquality) {
	// Primitives
	{
		auto a = asvJSONValue::makeInt(42);
		auto b = asvJSONValue::makeInt(42);
		auto c = asvJSONValue::makeInt(43);
		ASSERT(*a == *b);
		ASSERT(*a != *c);
	}
	// Strings
	{
		auto a = asvJSONValue::makeStringView("hello");
		auto b = asvJSONValue::makeStringView("hello");
		auto c = asvJSONValue::makeStringView("world");
		ASSERT(*a == *b);
		ASSERT(*a != *c);
	}
	// Different types
	{
		auto a = asvJSONValue::makeInt(1);
		auto b = asvJSONValue::makeBool(true);
		ASSERT(*a != *b);
	}
	// Null
	{
		auto a = asvJSONValue::makeNull();
		auto b = asvJSONValue::makeNull();
		ASSERT(*a == *b);
	}
	// Objects
	{
		auto a = asvJSONValue::makeObject();
		a->emplace("x", asvJSONValue::makeInt(1));
		auto b = asvJSONValue::makeObject();
		b->emplace("x", asvJSONValue::makeInt(1));
		ASSERT(*a == *b);
		b->emplace("y", asvJSONValue::makeInt(2));
		ASSERT(*a != *b);
	}
	// Arrays
	{
		auto a = asvJSONValue::makeArray();
		a->push(asvJSONValue::makeInt(1));
		a->push(asvJSONValue::makeInt(2));
		auto b = asvJSONValue::makeArray();
		b->push(asvJSONValue::makeInt(1));
		b->push(asvJSONValue::makeInt(2));
		ASSERT(*a == *b);
		b->push(asvJSONValue::makeInt(3));
		ASSERT(*a != *b);
	}
	// Nested
	{
		asvJSON da, db;
		da.parse(R"({"a":{"b":1},"c":[1,2]})");
		db.parse(R"({"a":{"b":1},"c":[1,2]})");
		ASSERT(*da.getRoot() == *db.getRoot());
	}
	// Booleans
	{
		auto a = asvJSONValue::makeBool(true);
		auto b = asvJSONValue::makeBool(true);
		auto c = asvJSONValue::makeBool(false);
		ASSERT(*a == *b);
		ASSERT(*a != *c);
	}
	// Doubles
	{
		auto a = asvJSONValue::makeDouble(3.14);
		auto b = asvJSONValue::makeDouble(3.14);
		auto c = asvJSONValue::makeDouble(2.71);
		ASSERT(*a == *b);
		ASSERT(*a != *c);
	}
	// via asvJSON
	{
		asvJSON da, db;
		da.parse(R"({"x":1,"y":"hi"})");
		db.parse(R"({"x":1,"y":"hi"})");
		ASSERT(*da.getRoot() == *db.getRoot());
		db.parse(R"({"x":1,"y":"bye"})");
		ASSERT(*da.getRoot() != *db.getRoot());
	}
}

TEST(testMergeDeep) {
	// Basic object merge — other overrides
	{
		asvJSON base, overlay;
		base.parse(R"({"a":1,"b":2})");
		overlay.parse(R"({"b":3,"c":4})");
		base.merge(overlay);
		ASSERT_EQ(base.get("a")->getInt(), int64_t(1));
		ASSERT_EQ(base.get("b")->getInt(), int64_t(3));
		ASSERT_EQ(base.get("c")->getInt(), int64_t(4));
	}
	// Deep merge — nested objects recurse
	{
		asvJSON base, overlay;
		base.parse(R"({"a":{"x":1,"y":2}})");
		overlay.parse(R"({"a":{"y":3,"z":4}})");
		base.merge(overlay);
		ASSERT_EQ(base.getNested("a.x")->getInt(), int64_t(1));
		ASSERT_EQ(base.getNested("a.y")->getInt(), int64_t(3));
		ASSERT_EQ(base.getNested("a.z")->getInt(), int64_t(4));
	}
	// Merge into empty
	{
		asvJSON base, overlay;
		overlay.parse(R"({"k":"v"})");
		base.merge(overlay);
		ASSERT_EQ(std::string(base.get("k")->getString()), "v");
	}
	// Merge non-object overwrites
	{
		asvJSON base, overlay;
		base.parse(R"({"a":1})");
		overlay.parse(R"("string")");
		base.merge(overlay);
		ASSERT_EQ(std::string(base.getRoot()->getString()), "string");
	}
	// Merge null in patch removes key (RFC 7396)
	{
		asvJSON base, patch;
		base.parse(R"({"a":1,"b":2})");
		patch.parse(R"({"b":null})");
		base.merge(patch);
		ASSERT_EQ(base.get("a")->getInt(), int64_t(1));
		ASSERT(!base.hasKey("b"));
	}
}

TEST(testDiff) {
	// Identical — empty diff
	{
		asvJSON a, b;
		a.parse(R"({"x":1})");
		b.parse(R"({"x":1})");
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(0));
	}
	// Added key
	{
		asvJSON a, b;
		a.parse(R"({"x":1})");
		b.parse(R"({"x":1,"y":2})");
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(1));
		auto* op = (*d.getRoot()->arr)[0]->get("op");
		ASSERT(std::string(op->getString()) == "add");
		ASSERT_EQ((*d.getRoot()->arr)[0]->get("value")->getInt(), int64_t(2));
	}
	// Removed key
	{
		asvJSON a, b;
		a.parse(R"({"x":1,"y":2})");
		b.parse(R"({"x":1})");
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(1));
		auto* op = (*d.getRoot()->arr)[0]->get("op");
		ASSERT(std::string(op->getString()) == "remove");
	}
	// Replaced value
	{
		asvJSON a, b;
		a.parse(R"({"x":1})");
		b.parse(R"({"x":2})");
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(1));
		auto* op = (*d.getRoot()->arr)[0]->get("op");
		ASSERT(std::string(op->getString()) == "replace");
	}
	// Nested diff
	{
		asvJSON a, b;
		a.parse(R"({"a":{"x":1}})");
		b.parse(R"({"a":{"x":2}})");
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(1));
		auto* path = (*d.getRoot()->arr)[0]->get("path");
		ASSERT(std::string(path->getString()) == "a.x");
	}
	// Array diff
	{
		asvJSON a, b;
		a.parse(R"([1,2,3])");
		b.parse(R"([1,9,3,4])");
		auto d = a.diff(b);
		// index 1: replace 2→9, index 3: add 4
		ASSERT_EQ(d.getRoot()->size(), size_t(2));
	}
	// Type change
	{
		asvJSON a, b;
		a.parse(R"({"x":1})");
		b.parse(R"({"x":"hello"})");
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(1));
		auto* op = (*d.getRoot()->arr)[0]->get("op");
		ASSERT(std::string(op->getString()) == "replace");
	}
	// Both empty
	{
		asvJSON a, b;
		a.parse("{}");
		b.parse("{}");
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(0));
	}
	// Both null
	{
		asvJSON a, b;
		auto d = a.diff(b);
		ASSERT_EQ(d.getRoot()->size(), size_t(0));
	}
}

TEST(testPrettyPrint) {
	// --- YAML compact (flow) vs pretty (block) ---
	{
		asvJSON doc;
		doc.parse(R"({"name":"Alice","age":30,"active":true})");

		std::string block = doc.toYAML(true);
		std::string flow = doc.toYAML(false);

		// Block mode should have --- header and key: value pairs
		ASSERT(block.find("---") != std::string::npos);
		ASSERT(block.find("name:") != std::string::npos);

		// Compact mode should have flow-style object
		ASSERT(flow.find("{") != std::string::npos);
		ASSERT(flow.find("}") != std::string::npos);
		ASSERT(flow.find("name:") != std::string::npos);

		// Flow should be shorter
		ASSERT(flow.size() <= block.size());
	}
	// --- YAML nested objects ---
	{
		asvJSON doc;
		doc.parse(R"({"server":{"host":"localhost","port":8080}})");

		std::string block = doc.toYAML(true);
		std::string flow = doc.toYAML(false);

		// Block has indented nested keys
		ASSERT(block.find("host:") != std::string::npos);
		ASSERT(block.find("port:") != std::string::npos);
		// Flow should inline the nested object
		ASSERT(flow.find("{") != std::string::npos);
	}
	// --- YAML arrays ---
	{
		asvJSON doc;
		doc.parse(R"({"tags":["cpp","json","lib"]})");

		std::string block = doc.toYAML(true);
		std::string flow = doc.toYAML(false);

		// Block mode uses "- " prefix for array items
		ASSERT(block.find("- ") != std::string::npos);
		// Flow mode uses [a, b, c]
		ASSERT(flow.find("[") != std::string::npos);
		ASSERT(flow.find("]") != std::string::npos);
	}
	// --- YAML empty containers ---
	{
		asvJSON doc;
		doc.parse(R"({"obj":{},"arr":[]})");

		std::string flow = doc.toYAML(false);
		ASSERT(flow.find("{}") != std::string::npos);
		ASSERT(flow.find("[]") != std::string::npos);
	}

	// --- TOML pretty (expanded) vs compact (inline tables) ---
	{
		asvJSON doc;
		doc.parse(R"({"name":"server","config":{"host":"localhost","port":8080}})");

		std::string expanded = doc.toTOML(true);
		std::string compact = doc.toTOML(false);

		// Expanded uses [section] headers
		ASSERT(expanded.find("[config]") != std::string::npos);
		ASSERT(expanded.find("host = ") != std::string::npos);

		// Compact uses inline table syntax
		ASSERT(compact.find("{") != std::string::npos);
		ASSERT(compact.find("host") != std::string::npos);
		// No section headers in compact mode
		ASSERT(compact.find("[config]") == std::string::npos);
	}
	// --- TOML compact: arrays ---
	{
		asvJSON doc;
		doc.parse(R"({"items":[1,2,3]})");

		std::string compact = doc.toTOML(false);
		ASSERT(compact.find("[1, 2, 3]") != std::string::npos);
	}

	// --- INI pretty (sections) vs compact (flat dotted keys) ---
	{
		asvJSON doc;
		doc.parse(R"({"name":"server","db":{"host":"localhost","port":5432}})");

		std::string pretty = doc.toINI(true);
		std::string compact = doc.toINI(false);

		// Pretty uses section headers
		ASSERT(pretty.find("[db]") != std::string::npos);
		ASSERT(pretty.find("host = ") != std::string::npos);

		// Compact uses dotted flat keys
		ASSERT(compact.find("db.host = ") != std::string::npos);
		ASSERT(compact.find("db.port = ") != std::string::npos);
		// No section headers in compact mode
		ASSERT(compact.find("[db]") == std::string::npos);
	}
	// --- INI compact: nested sections ---
	{
		asvJSON doc;
		doc.parse(R"({"a":{"b":{"c":42}}})");

		std::string compact = doc.toINI(false);
		ASSERT(compact.find("a.b.c = 42") != std::string::npos);

		std::string pretty = doc.toINI(true);
		ASSERT(pretty.find("[a.b]") != std::string::npos);
	}

	// --- Default is pretty=true ---
	{
		asvJSON doc;
		doc.parse(R"({"a":1,"b":{"x":2}})");

		// toYAML() without args should be block style
		std::string yamlDefault = doc.toYAML();
		ASSERT(yamlDefault.find("---") != std::string::npos);

		// toTOML() without args should use section headers
		std::string tomlDefault = doc.toTOML();
		ASSERT(tomlDefault.find("[b]") != std::string::npos);
	}
}

TEST(testYamlWriteOptions) {
	// YamlWriteOptions defaults must reproduce the classic block output.
	YamlWriteOptions def;
	{
		asvJSON doc;
		doc.parse(R"({"name":"Alice","age":30,"active":true})");
		ASSERT_EQ(doc.toYAML(YamlWriteOptions{}), doc.toYAML(true));
	}
	// Style::Auto must reproduce toYAML(false).
	{
		asvJSON doc;
		doc.parse(R"({"name":"Alice","age":30,"active":true})");
		YamlWriteOptions o; o.style = YamlWriteOptions::Style::Auto;
		ASSERT_EQ(doc.toYAML(o), doc.toYAML(false));
	}
	// Custom indent width.
	{
		asvJSON doc;
		doc.parse(R"({"a":{"b":{"c":42}}})");
		YamlWriteOptions o; o.indentWidth = 4;
		std::string y = doc.toYAML(o);
		ASSERT(y.find("    b:") != std::string::npos);
		ASSERT(y.find("        c: 42") != std::string::npos);
	}
	// Style::Flow forces flow everywhere, even beyond flowMaxLen.
	{
		asvJSON doc;
		doc.parse(R"({"items":[1,2,3,4,5,6,7,8],"s":"abc"})");
		YamlWriteOptions o; o.style = YamlWriteOptions::Style::Flow;
		std::string y = doc.toYAML(o);
		ASSERT(y.find("items: [1, 2, 3, 4, 5, 6, 7, 8]") != std::string::npos);
		ASSERT(y.find("s: abc") != std::string::npos);
		ASSERT(y.find("\n- ") == std::string::npos);
	}
	// Style::Flow inlines multiline strings as double-quoted scalars.
	{
		asvJSON doc;
		doc.parse(R"({"note":"line one\nline two"})");
		YamlWriteOptions o; o.style = YamlWriteOptions::Style::Flow;
		std::string y = doc.toYAML(o);
		ASSERT(y.find("\"line one\\nline two\"") != std::string::npos);
	}
	// Auto flowMaxLen controls the block fallback threshold.
	{
		asvJSON doc;
		doc.parse(R"({"longkey1":"value","longkey2":"value"})");
		YamlWriteOptions o; o.style = YamlWriteOptions::Style::Auto;
		YamlWriteOptions tiny = o; tiny.flowMaxLen = 12;
		std::string wide = doc.toYAML(o);
		std::string tight = doc.toYAML(tiny);
		ASSERT(wide.find("{") != std::string::npos);      // fits in flow
		ASSERT(tight.find("longkey1:") != std::string::npos); // too long -> block
	}
	// alwaysQuoteStrings quotes every string scalar.
	{
		asvJSON doc;
		doc.parse(R"({"a":"plain","b":"has space"})");
		YamlWriteOptions o; o.alwaysQuoteStrings = true;
		std::string y = doc.toYAML(o);
		ASSERT(y.find("'plain'") != std::string::npos);
		ASSERT(y.find("'has space'") != std::string::npos);
	}
	// doubleQuotes switches quoting to "...".
	{
		asvJSON doc;
		doc.parse(R"({"a b":"plain","y":"kw"})");
		YamlWriteOptions o; o.doubleQuotes = true;
		std::string y = doc.toYAML(o);
		ASSERT(y.find("\"a b\":") != std::string::npos);
		ASSERT(y.find("\"y\":") != std::string::npos);
	}
	// alwaysQuoteKeys quotes every mapping key.
	{
		asvJSON doc;
		doc.parse(R"({"simple":{"x":1}})");
		YamlWriteOptions o; o.alwaysQuoteKeys = true;
		std::string y = doc.toYAML(o);
		ASSERT(y.find("'simple':") != std::string::npos);
		ASSERT(y.find("'x': 1") != std::string::npos);
	}
	// Round-trips for every option combination.
	{
		std::vector<YamlWriteOptions> combos;
		combos.push_back(YamlWriteOptions{});
		YamlWriteOptions a; a.style = YamlWriteOptions::Style::Auto; combos.push_back(a);
		YamlWriteOptions f; f.style = YamlWriteOptions::Style::Flow; combos.push_back(f);
		YamlWriteOptions i; i.indentWidth = 4; combos.push_back(i);
		YamlWriteOptions q; q.alwaysQuoteStrings = true; combos.push_back(q);
		YamlWriteOptions d; d.doubleQuotes = true; combos.push_back(d);
		YamlWriteOptions k; k.alwaysQuoteKeys = true; combos.push_back(k);
		YamlWriteOptions mix; mix.style = YamlWriteOptions::Style::Auto; mix.indentWidth = 3;
		mix.doubleQuotes = true; combos.push_back(mix);
		for (auto& o : combos) {
			asvJSON doc;
			doc.parse(R"({"name":"Alice in wonderland","age":30,"note":"line one\nline two","tags":["cpp","json"],"n":{"x":1}})");
			asvJSON back;
			ASSERT(back.fromYAML(doc.toYAML(o)));
			ASSERT(*back.getRoot() == *doc.getRoot());
		}
	}
	// Root array in Flow mode.
	{
		asvJSON doc;
		doc.parse(R"([{"a":"x"},2,3])");
		YamlWriteOptions o; o.style = YamlWriteOptions::Style::Flow;
		std::string y = doc.toYAML(o);
		ASSERT(y.find("[{a: x}, 2, 3]") != std::string::npos);
	}
}

TEST(testIsChanged) {
	// After parse - not changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		ASSERT(!j.isChanged());
	}
	// putString - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "hello");
		ASSERT(j.isChanged());
	}
	// putInt - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putInt("c", 42);
		ASSERT(j.isChanged());
	}
	// putDouble - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putDouble("d", 3.14);
		ASSERT(j.isChanged());
	}
	// putBool - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putBool("b", true);
		ASSERT(j.isChanged());
	}
	// putNull - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putNull("n");
		ASSERT(j.isChanged());
	}
	// putBinary - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		uint8_t data[] = {0x01, 0x02};
		j.putBinary("bin", data, 2);
		ASSERT(j.isChanged());
	}
	// remove - changed
	{
		asvJSON j;
		j.parse(R"({"a":1,"b":2})");
		j.remove("a");
		ASSERT(j.isChanged());
	}
	// remove non-existent key - NOT changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.remove("nonexistent");
		ASSERT(!j.isChanged());
	}
	// clear - resets changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "x");
		ASSERT(j.isChanged());
		j.clear();
		ASSERT(!j.isChanged());
	}
	// parse after mutation - resets changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "x");
		ASSERT(j.isChanged());
		j.parse(R"({"z":true})");
		ASSERT(!j.isChanged());
	}
	// clone - clean
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "x");
		ASSERT(j.isChanged());
		asvJSON j2(j);
		ASSERT(!j2.isChanged());
	}
	// copy assignment - clean
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "x");
		ASSERT(j.isChanged());
		asvJSON j2;
		j2 = j;
		ASSERT(!j2.isChanged());
	}
	// move - transfers dirty
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "x");
		ASSERT(j.isChanged());
		asvJSON j2(std::move(j));
		ASSERT(j2.isChanged());
	}
	// arrayAdd* - changed
	{
		asvJSON j;
		j.parse(R"([])");
		j.arrayAddInt(10);
		ASSERT(j.isChanged());
	}
	// setByPointer - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.setByPointer("/new", asvJSONValue::makeInt(99));
		ASSERT(j.isChanged());
	}
	// removeByPointer - changed
	{
		asvJSON j;
		j.parse(R"({"a":1,"b":2})");
		j.removeByPointer("/a");
		ASSERT(j.isChanged());
	}
	// merge - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		asvJSON patch;
		patch.parse(R"({"b":2})");
		j.merge(patch);
		ASSERT(j.isChanged());
	}
	// applyPatch - changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		asvJSON patch;
		patch.parse(R"([{"op":"add","path":"/b","value":2}])");
		j.applyPatch(patch);
		ASSERT(j.isChanged());
	}
	// writeToFile - does not change dirty
	{
		asvJSON j;
		j.parse(R"({"k":"v"})");
		ASSERT(!j.isChanged());
		j.writeToFile("_test_ischanged.json");
		ASSERT(!j.isChanged());
		std::remove("_test_ischanged.json");
	}
	// readFromFile - resets changed
	{
		asvJSON j;
		j.parse(R"({"k":"v"})");
		j.writeToFile("_test_ischanged.json");
		j.putString("x", "y");
		ASSERT(j.isChanged());
		j.readFromFile("_test_ischanged.json");
		ASSERT(!j.isChanged());
		std::remove("_test_ischanged.json");
	}
	// putFloat32 - changed
	{
		asvJSON j;
		j.parse(R"({})");
		j.putFloat32("f", 1.0f);
		ASSERT(j.isChanged());
	}
	// putTimestamp - changed
	{
		asvJSON j;
		j.parse(R"({})");
		j.putTimestamp("ts", 12345);
		ASSERT(j.isChanged());
	}
	// putObjectId - changed (must be exactly 12 raw bytes)
	{
		asvJSON j;
		j.parse(R"({})");
		j.putObjectId("oid", "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c");
		ASSERT(j.isChanged());
	}
	// putRegex - changed
	{
		asvJSON j;
		j.parse(R"({})");
		j.putRegex("rx", "^test$", "i");
		ASSERT(j.isChanged());
	}
	// putDateTime - changed
	{
		asvJSON j;
		j.parse(R"({})");
		j.putDateTime("dt", 1234567890);
		ASSERT(j.isChanged());
	}
	// fromYAML - resets changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "x");
		ASSERT(j.isChanged());
		j.fromYAML("---\nkey: value\n");
		ASSERT(!j.isChanged());
	}
	// fromJSON5 - resets changed
	{
		asvJSON j;
		j.parse(R"({"a":1})");
		j.putString("b", "x");
		ASSERT(j.isChanged());
		j.fromJSON5(R"({key: "value"})");
		ASSERT(!j.isChanged());
	}
	// arrayAddValue (root level) - changed
	{
		asvJSON j;
		j.arrayAddString("hello", 5);
		ASSERT(j.isChanged());
	}
	// arrayAddValue (key-based) - changed
	{
		asvJSON j;
		j.parse(R"({})");
		j.arrayAddInt("items", 42);
		ASSERT(j.isChanged());
	}
	// putBinChunked - changed
	{
		asvJSON j;
		j.parse(R"({})");
		uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
		j.putBinChunked("data", data, 5);
		ASSERT(j.isChanged());
	}
}

TEST(testKeyOrder) {
	// Object keys keep insertion order by default -> parse/serialize round-trips
	// preserve key order (JSON, YAML, INI).
	auto orderOk = [](const std::string& s, const char* first, const char* second, const char* third) {
		size_t a = s.find(first), b = s.find(second), c = s.find(third);
		return a != std::string::npos && b != std::string::npos && c != std::string::npos && a < b && b < c;
	};
	// JSON parse -> serialize
	{
		asvJSON j;
		ASSERT(j.parse(R"({"b":1,"a":2,"k":3})"));
		ASSERT(orderOk(j.serialize(), "\"b\"", "\"a\"", "\"k\""));
	}
	// YAML round-trip (z, a, m stay in order)
	{
		asvJSON j;
		ASSERT(j.fromYAML("z: 1\na: 2\nm: 3\n"));
		ASSERT(orderOk(j.toYAML(), "z:", "a:", "m:"));
		ASSERT(orderOk(j.serialize(), "\"z\"", "\"a\"", "\"m\""));
	}
	// Nested object order + update-in-place keeps position
	{
		asvJSON j;
		ASSERT(j.parse(R"({"c":{"x":1,"y":2},"d":4})"));
		std::string s = j.serialize();
		ASSERT(s.find("\"c\"") < s.find("\"d\""));
		ASSERT(j.putInt("c.y", 99));
		s = j.serialize();
		ASSERT(s.find("\"c\"") < s.find("\"d\""));
		ASSERT(s.find("99") != std::string::npos);
	}
	// Duplicate key: last wins, keeps original position
	{
		asvJSON j;
		ASSERT(j.parse(R"({"a":1,"b":2,"a":3})"));
		ASSERT(j.getRoot()->obj->size() == 2);
		std::string s = j.serialize();
		ASSERT(s.find("\"a\"") < s.find("\"b\""));
		ASSERT(s.find(":3") != std::string::npos);
		ASSERT(s.find(":1") == std::string::npos);
	}
	// Merge updates existing keys in place, appends new ones
	{
		asvJSON a, b;
		ASSERT(a.parse(R"({"x":1,"y":2})"));
		ASSERT(b.parse(R"({"y":9,"z":3})"));
		a.merge(b);
		std::string s = a.serialize();
		ASSERT(orderOk(s, "\"x\"", "\"y\"", "\"z\""));
	}
	// User iteration order == insertion order
	{
		asvJSON j;
		ASSERT(j.parse(R"({"first":1,"second":2})"));
		std::vector<std::string> keys;
		for (const auto& [k, v] : *j.getRoot()->obj) keys.push_back(k);
		ASSERT(keys.size() == 2);
		ASSERT(keys[0] == "first" && keys[1] == "second");
	}
	// INI global keys keep insertion order
	{
		asvJSON j;
		ASSERT(j.fromINI("b = 1\na = 2\n"));
		std::string s = j.serialize();
		ASSERT(s.find("\"b\"") < s.find("\"a\""));
	}
}

int main() {
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	std::cout << "========================================" << std::endl;
	std::cout << "   asvJSON++ C++17 Test Suite" << std::endl;
	std::cout << "========================================" << std::endl << std::endl;
	
	std::cout << "--- Factory Tests ---\n";
	RUN(testMakeString);
	RUN(testMakeInt);
	RUN(testMakeDouble);
	RUN(testMakeBool);
	RUN(testMakeNull);
	RUN(testMakeObject);
	RUN(testMakeArray);
	RUN(testMakeDateTime);
	RUN(testMakeBinary);
	RUN(testMakeObjectId);
	RUN(testMakeTimestamp);
	RUN(testMakeRegex);
	
	std::cout << "\n--- asvJSONValue Tests ---\n";
	RUN(testObjectGet);
	RUN(testArrayGet);
	RUN(testSize);
	RUN(testValueHasKey);
	RUN(testTypeToString);
	RUN(testGetStringLen);
	RUN(testValueGetInt);
	RUN(testValueGetDouble);
	RUN(testValueGetBool);
	RUN(testGetDateTime);
	RUN(testGetBinary);
	
	std::cout << "\n--- Parse Tests ---\n";
	RUN(testParseString);
	RUN(testParseInt);
	RUN(testParseDouble);
	RUN(testParseTrue);
	RUN(testParseFalse);
	RUN(testParseNull);
	RUN(testParseObject);
	RUN(testParseArray);
	RUN(testParseNested);
	RUN(testParseEmpty);
	RUN(testParseEmptyArray);
	RUN(testParseUTF8BOM);
	RUN(testParseValidateUTF8);
	RUN(testNegativeDateTime);
	
	std::cout << "\n--- Put Tests ---\n";
	RUN(testPutString);
	RUN(testPutInt);
	RUN(testPutDouble);
	RUN(testPutBool);
	RUN(testPutDateTime);
	RUN(testPutNull);
	RUN(testPutBinary);
	RUN(testPutOverwrite);
	
	std::cout << "\n--- Get Tests ---\n";
	RUN(testGetString);
	RUN(testGetInt);
	RUN(testGetDouble);
	RUN(testGetBool);
	RUN(testIsNull);
	RUN(testHasKey);
	RUN(testOptString);
	RUN(testOptInt);
	RUN(testOptDouble);
	RUN(testOptBool);
	
	std::cout << "\n--- Remove/Clear Tests ---\n";
	RUN(testRemove);
	RUN(testClear);
	
	std::cout << "\n--- Serialization Tests ---\n";
	RUN(testSerialize);
	RUN(testSerializePretty);
	RUN(testSerializeNaNInfinity);
	
	std::cout << "\n--- File Tests ---\n";
	RUN(testWriteReadFile);
	
	std::cout << "\n--- MessagePack Tests ---\n";
	RUN(testMessagePack);
	RUN(testMessagePackRoundtrip);
	
	std::cout << "\n--- BSON Tests ---\n";
	RUN(testBSON);
	RUN(testBSONRegex);
	RUN(testBSONRoundtrip);
	
	std::cout << "\n--- JSON Pointer Tests ---\n";
	RUN(testGetByPointer);
	RUN(testGetByPointerArray);
	RUN(testSetByPointer);
	RUN(testSetByPointerArrayExpand);
	RUN(testSetByPointerArrayAppend);
	RUN(testGetDateTimeMethod);
	RUN(testGetObjectIdView);
	RUN(testInvalidNumber);
	RUN(testBigNumberRoundTrip);
	RUN(testNumberOverflow);
	RUN(testRemoveByPointer);
	std::cout << "\n--- Merge Tests ---\n";
	RUN(testMerge);

	RUN(testMergeWithNonObject);
	RUN(testApplyPatch);
	RUN(testApplyMergePatch);
	RUN(testApplyMergePatchNullDelete);
	
	std::cout << "\n--- Clone Tests ---\n";
	RUN(testCloneValue);
	
	std::cout << "\n--- Nested Tests ---\n";
	RUN(testGetNested);
	
	std::cout << "\n--- Base64 Tests ---\n";
	RUN(testBase64Encode);
	RUN(testBase64Decode);
	RUN(testBase64Roundtrip);
	RUN(testBase64CustomCharset);
	
	std::cout << "\n--- BinChunked Tests ---\n";
	RUN(testPutBinChunked);
	
	std::cout << "\n--- Comment Parsing Tests ---\n";
	RUN(testParseComments);
	RUN(testParseMultilineComments);
	RUN(testParseHashComment);
	
	std::cout << "\n--- GetKeys Test ---\n";
	RUN(testGetKeys);
	
	std::cout << "\n--- Clone Direct Test ---\n";
	RUN(testCloneValueDirect);
	
	std::cout << "\n--- Stress Tests ---\n";
	RUN(testStressLargeArray);
	RUN(testStressManyKeys);
	RUN(testStressDeepNesting);
	
	std::cout << "\n--- Thread Safety Tests ---\n";
	RUN(testCreationInThreads);
	RUN(testConcurrentAccess);
	
	std::cout << "\n--- Fuzz Tests ---\n";
	RUN(testFuzzRandomStrings);
	RUN(testFuzzEdgeCases);
	
	std::cout << "\n--- New Security Tests ---\n";
	RUN(testNestingDepthLimit);
	RUN(testStringTooLarge);
	RUN(testErrorMessages);
	RUN(testBasicParse);
	RUN(testControlCharsEscaped);
	
	std::cout << "\n--- Root Tests ---\n";
	RUN(testGetRoot);
	
	std::cout << "\n--- Move Constructor Tests ---\n";
	RUN(testMoveConstructor);
	RUN(testMoveAssignment);
	
	std::cout << "\n--- JSON Pointer Escape Tests ---\n";
	RUN(testJSONPointerEscape);
	RUN(testJSONPointerArrayAppend);
	
	std::cout << "\n--- JSON Patch Extended Tests ---\n";
	RUN(testJSONPatchCopyMove);
	RUN(testJSONPatchTestIntDouble);
	
	std::cout << "\n--- Corrupted Data Tests ---\n";
	RUN(testBSONCorruptedData);	RUN(testBSONDecimal128);	RUN(testMessagePackCorruptedData);	RUN(testMsgPackUInt64Overflow);
	
	std::cout << "\n--- Duplicate Key Tests ---\n";
	RUN(testDuplicateKeyNoLeak);
	
	std::cout << "\n--- Serialize Pretty Indentation Tests ---\n";
	RUN(testSerializePrettyIndentation);
	
	std::cout << "\n--- GetConst Overloads ---\n";
	RUN(testGetConstOverloads);
	RUN(testNestedAccessors);
	
	std::cout << "\n--- Unicode Tests ---\n";
	RUN(testUnicode);

	std::cout << "\n--- MessagePack Ext Tests ---\n";
	RUN(testMessagePackExt);

	std::cout << "\n--- BSON Array Tests ---\n";
	RUN(testBSONArray);

	std::cout << "\n--- BSON Timestamp/DateTime Tests ---\n";
	RUN(testBSONTimestampRoundtrip);
	RUN(testBSONTimestampDecode);
	RUN(testBSONDateTimeRoundtrip);
	RUN(testBSONDateTimeNegativeRoundtrip);
	RUN(testBSONDateTimeOverflow);
	RUN(testBSONStrictTerminator);
	RUN(testBSONMalformedRegex);
	RUN(testBSONOversizedKey);
	RUN(testBSONExtensionRoundTrip);
	RUN(testBSONEmptyArrayRoundTrip);
	RUN(testBSONExtras);

	std::cout << "\n--- ISO 8601 DateTime Tests ---\n";
	RUN(testISODateTime);

	std::cout << "\n--- API Coverage Tests ---\n";
	RUN(testAPICoverage);

	std::cout << "\n--- StringView Tests ---\n";
	RUN(testStringViewParse);
	RUN(testStringViewGet);
	RUN(testStringViewHasKey);
	RUN(testStringViewGetInt);
	RUN(testStringViewGetDouble);
	RUN(testStringViewGetBool);
	RUN(testStringViewOptInt);
	RUN(testStringViewOptBool);
	RUN(testStringViewGetBinary);
	RUN(testStringViewRemove);
	RUN(testValueGetStringView);
	RUN(testValueGetConstSizeT);
	RUN(testParseStringView);
	
	std::cout << "\n--- Empty String / Lone Surrogate / MsgPack Types ---\n";
	RUN(testParseEmptyString);
	RUN(testLoneSurrogateRejected);
	RUN(testMessagePackObjectIdRegexTimestamp);
	RUN(testMsgPackRegexFidelity);
	
	std::cout << "\n--- Object Key Escape Tests ---\n";
	RUN(testObjectKeyEscapes);
	
	std::cout << "\n--- CBOR Serialization Tests ---\n";
	RUN(testCBOR);
	RUN(testCBORArray);
	RUN(testCBORIntegers);
	RUN(testCBORDouble);
	RUN(testCBORBinary);
	RUN(testCBORDateTime);
	RUN(testCBORRegex);
	RUN(testCBORExtension);
	RUN(testCBORObjectIdTimestamp);
	RUN(testCBORNestedObject);
	RUN(testCBORCorrupted);
	RUN(testCBOREmpty);
	RUN(testCBORFromString);
	RUN(testCBORIndefiniteArray);
	RUN(testCBORIndefiniteMap);
RUN(testCBORFloat16);                                                                      
	RUN(testCBORNegEpochDateTime);                                                             
	RUN(testCBORUInt64Overflow);
	RUN(testCBORNegInt64Overflow);
	RUN(testCBORIndefiniteArrayTruncated);
	RUN(testCBORIndefiniteMapTruncated);
	RUN(testCBORIndefiniteStringTruncated);
	RUN(testCBORTimestampRoundTrip);
	RUN(testCBORObjectIdRoundTrip);
	RUN(testCBORDatetimeVsTimestamp);
	RUN(testCBORExtras);

	std::cout << "\n--- Protobuf Tests ---\n";
	RUN(testProtobufBasic);
	RUN(testProtobufArrays);
	RUN(testProtobufNested);
	RUN(testProtobufSchema);
	RUN(testProtobufTextFormat);
	RUN(testProtobufTextIntOverflow);
	RUN(testProtobufStaticConverters);
	RUN(testProtobufStringOverload);
	RUN(testProtobufPackedFixed);
	RUN(testProtobufSchemaLessRoundTrip);
	RUN(testProtobufTextArrayOfObjects);
	RUN(testProtobufExtras);

	std::cout << "\n--- XML Serialization Tests ---\n";
	RUN(testToXML);
	RUN(testFromXML);
	RUN(testFromXMLIntOverflow);
	RUN(testXMLStringRoundTrip);
	RUN(testXMLArrayStringRoundTrip);
	RUN(testXMLTextContentRoundTrip);
	RUN(testXmlExtras);
	
	std::cout << "\n--- YAML Serialization Tests ---\n";
	RUN(testToYAML);
	RUN(testFromYAML);
	
	std::cout << "\n--- CSV Serialization Tests ---\n";
	RUN(testToCSV);
	RUN(testFromCSV);
	RUN(testFromCSVIntOverflow);
	RUN(testCSVRoundTrip);
	RUN(testCsvExtras);
	
	std::cout << "\n--- TOON Serialization Tests ---\n";
	RUN(testToTOON);
	RUN(testToTOONAdvanced);
	RUN(testToTOONSpecListForm);
	RUN(testToTOONHeadersStrict);
	RUN(testTOONSpecialTypes);
	
	std::cout << "\n--- TRON Serialization Tests ---\n";
	RUN(testToTRON);
	RUN(testToTRONAdvanced);
	RUN(testToTRONComprehensive);
	RUN(testTRONSpecialTypes);
	
	std::cout << "\n--- GOON Serialization Tests ---\n";
	RUN(testToGOON);
	RUN(testGOONAudit);
	RUN(testGOONExtras);
	
	std::cout << "\n--- TOML Serialization Tests ---\n";
	RUN(testToTOML);
	RUN(testFromTOML);

	std::cout << "\n--- JSON Lines Serialization Tests ---\n";
	// ByteSize Tests
	std::cout << "\n--- ByteSize Serialization Tests ---\n";
	RUN(testByteSizeNullRoot);
	RUN(testByteSizeEmptyObject);
	RUN(testByteSizeSimpleString);
	RUN(testByteSizeNestedStructure);
	RUN(testByteSizeWithCountAndName);

	RUN(testToJSONLines);
	RUN(testFromJSONLines);

	std::cout << "\n--- S-Expression Serialization Tests ---\n";
	RUN(testToSexpr);
	RUN(testFromSexpr);
    RUN(testSexprAudit);
    RUN(testSexprSpecialTypes);
    RUN(testSexprExtras);

	std::cout << "\n--- JSON5 Serialization Tests ---\n";
	RUN(testToJSON5);
	RUN(testFromJSON5);
    RUN(testFromJSON5Conformance);
    RUN(testJSON5Audit);
    RUN(testJSON5Extras);

	std::cout << "\n--- INI Serialization Tests ---\n";
	RUN(testToINI);
	RUN(testFromINI);
	RUN(testFromINIIntOverflow);
	RUN(testINIRoundTrip);
	RUN(testIniExtras);

	std::cout << "\n--- UDE Serialization Tests ---\n";
	RUN(testToUDE);
	RUN(testFromUDE);
	RUN(testUDEStringEscapes);
	RUN(testUDETypes);
	RUN(testUDEEdgeCases);
	RUN(testUDEMergeKeys);
	RUN(testUDEDocuments);
	RUN(testUDECustomTags);
	RUN(testUDEEncoding);
	RUN(testUDERoundTrip);
	RUN(testUDERoundTripSpecialTypes);
	RUN(testUDEBlockScalarChomp);
	RUN(testUDESpecConformance);
	RUN(testUDEStrict);
	RUN(testUDEAnchors);
	RUN(testUDERawNumberRoundTrip);
	RUN(testUDECommentPrefixStrings);
	RUN(testUDEBlockScalarKeepChomping);

	std::cout << "\n--- FromAny & FormatType Tests ---\n";
	RUN(testFromAnyAndFormatType);

	std::cout << "\n--- JSON Schema Tests ---\n";
	RUN(testJSONSchema);
	RUN(testJSONSchemaExtras);

	std::cout << "\n--- JSONPath (RFC 9535) Tests ---\n";
	RUN(testJSONPath);
	RUN(testJSONPathExtras);

	std::cout << "\n--- JSON SAX Parser Tests ---\n";
	RUN(testJsonSax);
	RUN(testJsonStream);
	RUN(testJsonSaxStrict);
	RUN(testJsonStreamStrict);
	RUN(testSaxToDom);

	std::cout << "\n--- readFromFileAny / writeToFileAny Tests ---\n";
	RUN(testReadWriteFileAny);

	std::cout << "\n--- operator[] Tests ---\n";
	RUN(testOperatorBracket);

	std::cout << "\n--- Iterator API Tests ---\n";
	RUN(testIteratorAPI);

	std::cout << "\n--- Emplace/Push Tests ---\n";
	RUN(testEmplacePush);

	std::cout << "\n--- Equality Tests ---\n";
	RUN(testEquality);

	std::cout << "\n--- Deep Merge Tests ---\n";
	RUN(testMergeDeep);

	std::cout << "\n--- Diff Tests ---\n";
	RUN(testDiff);

	std::cout << "\n--- Pretty-Print Tests ---\n";
	RUN(testPrettyPrint);

	std::cout << "\n--- YAML Write Options Tests ---\n";
	RUN(testYamlWriteOptions);

	std::cout << "\n--- YAML Extras Tests ---\n";
	RUN(testYamlExtras);

	std::cout << "\n--- IsChanged Tests ---\n";
	RUN(testIsChanged);

	std::cout << "\n--- Key Order Tests ---\n";
	RUN(testKeyOrder);

	std::cout << "\n========================================" << std::endl;
	std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
	std::cout << "========================================" << std::endl;

	return failed > 0 ? 1 : 0;
}
