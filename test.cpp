#include "asvJSON++.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <ctime>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>

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
	
	json.setByPointer("/name", asvJSONValue::makeString("Updated", 7).release());
	
	ASSERT_EQ(json.getString("name"), "Updated");
}

TEST(testSetByPointerArrayExpand) {
	asvJSON json;
	json.parse(std::string("{\"arr\": [1]}"));
	
	json.setByPointer("/arr/5", asvJSONValue::makeInt(2).release());
	
	auto* arr = json.getArray("arr");
	if (!arr) throw std::runtime_error("array not found");
	if (arr->size() != 6) throw std::runtime_error("array size should be 6");
}

TEST(testSetByPointerArrayAppend) {
	asvJSON json;
	json.parse(std::string("[1, 2, 3]"));
	
	json.setByPointer("/-", asvJSONValue::makeInt(4).release());
	
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
	bool ok = json.setByPointer("/arr/-", asvJSONValue::makeInt(4).release());
	ASSERT(ok);
	auto* arr = json.getArray("arr");
	ASSERT(arr != nullptr);
	ASSERT_EQ(arr->size(), 4);
	ASSERT_EQ(arr->get(3)->getInt(), 4);
	ok = json.setByPointer("/arr/-", asvJSONValue::makeInt(5).release());
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

TEST(testMessagePackCorruptedData) {
	asvJSON json;
	bool ok = json.fromMessagePack(nullptr, 0);
	if (ok) throw std::runtime_error("null data should fail");
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
	json.putBinChunked("large", data, 200, 76);
	auto retrieved = json.getBinChunked("large");
	ASSERT_EQ(retrieved.size(), 200U);
	for (size_t i = 0; i < 200; i++) {
		ASSERT_EQ(retrieved[i], static_cast<uint8_t>(i));
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
		json.putExtension("a", 1, ext1, 1);
		uint8_t ext2[] = {0x01, 0x02};
		json.putExtension("b", 2, ext2, 2);
		uint8_t ext4[] = {0x01, 0x02, 0x03, 0x04};
		json.putExtension("c", 3, ext4, 4);
		uint8_t ext8[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
		json.putExtension("d", 4, ext8, 8);
		auto mp = json.toMessagePack();
		asvJSON json2;
		json2.fromMessagePack(mp.data(), mp.size());
		ASSERT_EQ(json2.getExtension("a").first, 1);
		ASSERT_EQ(json2.getExtension("a").second.size(), static_cast<size_t>(1));
		ASSERT_EQ(json2.getExtension("b").first, 2);
		ASSERT_EQ(json2.getExtension("b").second.size(), static_cast<size_t>(2));
		ASSERT_EQ(json2.getExtension("c").first, 3);
		ASSERT_EQ(json2.getExtension("c").second.size(), static_cast<size_t>(4));
		ASSERT_EQ(json2.getExtension("d").first, 4);
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
		json2.setByPointer("/sub/oid", asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(oidBytes), 12)).release());
		json2.setByPointer("/sub/ts", asvJSONValue::makeTimestamp(1000).release());
		json2.setByPointer("/sub/rx", asvJSONValue::makeRegex("^test$", "gi").release());
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

	// ObjectId round-trips as BINARY (no standard CBOR tag for ObjectId)
	auto* oidV = json2.getRoot()->get("oid");
	ASSERT(oidV != nullptr);
	ASSERT_EQ(oidV->type, asvJSONValue::BINARY);
	ASSERT_EQ(oidV->bin_data.size(), static_cast<size_t>(12));

	// Timestamp round-trips as DATETIME (tag 1)
	auto* tsV = json2.getRoot()->get("ts");
	ASSERT(tsV != nullptr);
	ASSERT_EQ(tsV->type, asvJSONValue::DATETIME);
	ASSERT_EQ(tsV->timestamp, 1234567890);
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
		ASSERT(xml.find("<null_val/>") != std::string::npos || xml.find("<n/>") != std::string::npos);
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
	// Empty object (self-closing root)
	{
		asvJSON json;
		ASSERT(json.fromXML("<root/>"));
		ASSERT(json.isNull("root"));
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
		auto* arr = inner->get("item");
		ASSERT(arr != nullptr);
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT(arr->arr->size() == 3);
		ASSERT(arr->get(static_cast<size_t>(0))->getInt() == 1);
		ASSERT(arr->get(static_cast<size_t>(1))->getInt() == 2);
		ASSERT(arr->get(static_cast<size_t>(2))->getInt() == 3);
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
		auto* inner = json2.getRoot()->get("root");
		ASSERT(inner != nullptr);
		auto* ext = inner->get("ext");
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
		auto* inner = json2.getRoot()->get("root");
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
		auto* arr = inner->get("item");
		ASSERT(arr != nullptr);
		ASSERT(arr->type == asvJSONValue::ARRAY);
		ASSERT(arr->arr->size() == 3);
		ASSERT(arr->get(static_cast<size_t>(0))->getInt() == 1);
		ASSERT(arr->get(static_cast<size_t>(1))->getInt() == 2);
		ASSERT(arr->get(static_cast<size_t>(2))->getInt() == 3);
	}
	// Non-consecutive same-named children → single array
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
	// asvJSONValue::toYAML (direct)
	{
		auto v = asvJSONValue::makeString("test", 4);
		std::string out;
		v->toYAML(out);
		ASSERT(!out.empty());
		ASSERT(out.find("---") != std::string::npos);
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
		ASSERT_EQ(std::string(json.getString("text")), "hello\nworld");
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
		auto* rx = j2.getRoot()->get("rx");
		ASSERT(rx != nullptr && rx->type == asvJSONValue::REGEX);
		ASSERT_EQ(std::string_view(rx->str_data), "^test$|gi");
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
		ASSERT_EQ(std::string(json.getString("text")), "hello world");
	}
	// Folded block scalar with empty line
	{
		asvJSON json;
		ASSERT(json.fromYAML("text: >\n  para1\n\n  para2"));
		ASSERT_EQ(std::string(json.getString("text")), "para1\npara2");
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
	// Three-! tag (!!!int) treated as unknown -> value parsed normally
	{
		asvJSON json;
		ASSERT(json.fromYAML("n: !!!int 42"));
		ASSERT_EQ(json.getInt("n"), 42);
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
	// %TAG !handle! prefix -- custom tag resolves via handle
	{
		asvJSON json;
		ASSERT(json.fromYAML("%TAG !x! tag:example.com,2000:app/\nx: !x!foo hello"));
		ASSERT_EQ(std::string(json.getString("x")), "hello");
	}
	// %TAG with !! override -- still parses value normally (unknown resolved tag)
	{
		asvJSON json;
		ASSERT(json.fromYAML("%TAG !! tag:example.com,2000:app/\nx: !!str 42"));
		ASSERT_EQ(std::string(json.getString("x")), "42"); // !!str still works
	}
	// Directives before ---
	{
		asvJSON json;
		ASSERT(json.fromYAML("%TAG !x! tag:example.com,2000:app/\n---\nx: !x!foo hello"));
		ASSERT_EQ(std::string(json.getString("x")), "hello");
	}
	// Directives after ---
	{
		asvJSON json;
		ASSERT(json.fromYAML("---\n%TAG !x! tag:example.com,2000:app/\nx: !x!foo hello"));
		ASSERT_EQ(std::string(json.getString("x")), "hello");
	}
	// Multiple directives
	{
		asvJSON json;
		ASSERT(json.fromYAML("%YAML 1.2\n%TAG !x! tag:example.com,2000:app/\nkey: !x!test val"));
		ASSERT_EQ(std::string(json.getString("key")), "val");
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
		// Verify OBJECTID (data preserved as hex string in TRON)
		ASSERT_EQ(std::string(j2.getString("oid")), "0102030405060708090a0b0c");
		// Verify REGEX (data preserved as "pattern|opts" string)
		ASSERT_EQ(std::string(j2.getString("rx")), "^test$|gi");
		// Verify TIMESTAMP (data preserved as int)
		ASSERT_EQ(j2.getInt("ts"), int64_t(987654321));
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
		// OBJECTID round-trips as hex string
		ASSERT_EQ(std::string(j2.getString("oid")), "414243444546313233343536");
		// TIMESTAMP round-trips as int
		ASSERT_EQ(j2.getInt("ts"), int64_t(1000));
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

int main() {
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
	RUN(testBSONCorruptedData);
	RUN(testMessagePackCorruptedData);
	
	std::cout << "\n--- Duplicate Key Tests ---\n";
	RUN(testDuplicateKeyNoLeak);
	
	std::cout << "\n--- Serialize Pretty Indentation Tests ---\n";
	RUN(testSerializePrettyIndentation);
	
	std::cout << "\n--- GetConst Overloads ---\n";
	RUN(testGetConstOverloads);
	
	std::cout << "\n--- Unicode Tests ---\n";
	RUN(testUnicode);

	std::cout << "\n--- MessagePack Ext Tests ---\n";
	RUN(testMessagePackExt);

	std::cout << "\n--- BSON Array Tests ---\n";
	RUN(testBSONArray);

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

	std::cout << "\n--- Protobuf Tests ---\n";
	RUN(testProtobufBasic);
	RUN(testProtobufArrays);
	RUN(testProtobufNested);
	RUN(testProtobufSchema);
	RUN(testProtobufTextFormat);
	RUN(testProtobufStaticConverters);
	RUN(testProtobufStringOverload);
	RUN(testProtobufPackedFixed);

	std::cout << "\n--- XML Serialization Tests ---\n";
	RUN(testToXML);
	RUN(testFromXML);
	
	std::cout << "\n--- YAML Serialization Tests ---\n";
	RUN(testToYAML);
	RUN(testFromYAML);
	
	std::cout << "\n--- CSV Serialization Tests ---\n";
	RUN(testToCSV);
	RUN(testFromCSV);
	
	std::cout << "\n--- TOON Serialization Tests ---\n";
	RUN(testToTOON);
	RUN(testToTOONAdvanced);
	
	std::cout << "\n--- TRON Serialization Tests ---\n";
	RUN(testToTRON);
	RUN(testToTRONAdvanced);
	RUN(testToTRONComprehensive);
	
	std::cout << "\n--- GOON Serialization Tests ---\n";
	RUN(testToGOON);
	
	std::cout << "\n========================================" << std::endl;
	std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
	std::cout << "========================================" << std::endl;
	
	return failed > 0 ? 1 : 0;
}
