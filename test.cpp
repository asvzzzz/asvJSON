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
	auto* v = asvJSONValue::makeString("hello", 5);
	ASSERT(v->type == asvJSONValue::STRING);
	ASSERT_EQ(v->str_data.size(), 5);
	ASSERT(strncmp(v->str_data.data(), "hello", 5) == 0);
	delete v;
}

TEST(testMakeInt) {
	auto* v = asvJSONValue::makeInt(42);
	ASSERT(v->type == asvJSONValue::INT);
	ASSERT_EQ(v->num, 42);
	delete v;
}

TEST(testMakeDouble) {
	auto* v = asvJSONValue::makeDouble(3.14);
	ASSERT(v->type == asvJSONValue::DOUBLE);
	ASSERT(v->dbl > 3.13 && v->dbl < 3.15);
	delete v;
}

TEST(testMakeBool) {
	auto* vt = asvJSONValue::makeBool(true);
	ASSERT(vt->type == asvJSONValue::BOOL_VAL);
	ASSERT(vt->flag == true);
	delete vt;
	
	auto* vf = asvJSONValue::makeBool(false);
	ASSERT(vf->type == asvJSONValue::BOOL_VAL);
	ASSERT(vf->flag == false);
	delete vf;
}

TEST(testMakeNull) {
	auto* v = asvJSONValue::makeNull();
	ASSERT(v->type == asvJSONValue::NULL_VAL);
	delete v;
}

TEST(testMakeObject) {
	auto* v = asvJSONValue::makeObject();
	ASSERT(v->type == asvJSONValue::OBJECT);
	ASSERT(v->obj != nullptr);
	ASSERT_EQ(v->size(), 0);
	delete v;
}

TEST(testMakeArray) {
	auto* v = asvJSONValue::makeArray();
	ASSERT(v->type == asvJSONValue::ARRAY);
	ASSERT(v->arr != nullptr);
	ASSERT_EQ(v->size(), 0);
	delete v;
}

TEST(testMakeDateTime) {
	time_t now = time(nullptr);
	auto* v = asvJSONValue::makeDateTime(now, 500);
	ASSERT(v->type == asvJSONValue::DATETIME);
	ASSERT_EQ(v->timestamp, now);
	ASSERT_EQ(v->datetime_ms, 500);
	delete v;
}

TEST(testMakeBinary) {
	uint8_t data[] = {0x01, 0x02, 0x03};
	auto* v = asvJSONValue::makeBinary(data, 3);
	ASSERT(v->type == asvJSONValue::BINARY);
	ASSERT_EQ(v->bin_data.size(), 3);
	ASSERT(v->bin_data.data()[0] == 0x01);
	delete v;
}

TEST(testMakeObjectId) {
	const char oid[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
	auto* v = asvJSONValue::makeObjectId(std::string_view(oid, 12));
	ASSERT(v->type == asvJSONValue::OBJECTID);
	ASSERT_EQ(v->str_data.size(), 12);
	delete v;
}

TEST(testMakeTimestamp) {
	auto* v = asvJSONValue::makeTimestamp(1234567890);
	ASSERT(v->type == asvJSONValue::TIMESTAMP);
	ASSERT_EQ(v->num, 1234567890);
	delete v;
}

TEST(testMakeRegex) {
	auto* v = asvJSONValue::makeRegex("pattern", "i");
	ASSERT(v->type == asvJSONValue::REGEX);
	ASSERT(v->str_data.size() > 0);
	delete v;
}

TEST(testObjectGet) {
	auto* obj = asvJSONValue::makeObject();
	auto* val = asvJSONValue::makeInt(100);
	obj->obj->emplace("key", val);
	
	auto* result = obj->get("key");
	ASSERT(result != nullptr);
	ASSERT_EQ(result->num, 100);
	
	auto* missing = obj->get("nonexistent");
	ASSERT(missing == nullptr);
	
	delete obj;
}

TEST(testArrayGet) {
	auto* arr = asvJSONValue::makeArray();
	arr->arr->push_back(std::unique_ptr<asvJSONValue>(asvJSONValue::makeInt(1)));
	arr->arr->push_back(std::unique_ptr<asvJSONValue>(asvJSONValue::makeInt(2)));
	arr->arr->push_back(std::unique_ptr<asvJSONValue>(asvJSONValue::makeInt(3)));
	
	ASSERT_EQ(arr->get(size_t(0))->num, 1);
	ASSERT_EQ(arr->get(size_t(1))->num, 2);
	ASSERT_EQ(arr->get(size_t(2))->num, 3);
	ASSERT(arr->get(size_t(10)) == nullptr);
	
	delete arr;
}

TEST(testSize) {
	auto* obj = asvJSONValue::makeObject();
	ASSERT_EQ(obj->size(), 0);
	obj->obj->emplace("a", asvJSONValue::makeInt(1));
	ASSERT_EQ(obj->size(), 1);
	obj->obj->emplace("b", asvJSONValue::makeInt(2));
	ASSERT_EQ(obj->size(), 2);
	delete obj;
	
	auto* arr = asvJSONValue::makeArray();
	ASSERT_EQ(arr->size(), 0);
	arr->arr->push_back(std::unique_ptr<asvJSONValue>(asvJSONValue::makeInt(1)));
	ASSERT_EQ(arr->size(), 1);
	arr->arr->push_back(std::unique_ptr<asvJSONValue>(asvJSONValue::makeInt(2)));
	ASSERT_EQ(arr->size(), 2);
	delete arr;
}

TEST(testValueHasKey) {
	auto* obj = asvJSONValue::makeObject();
	obj->obj->emplace("exists", asvJSONValue::makeInt(1));
	
	ASSERT(obj->hasKey("exists") == true);
	ASSERT(obj->hasKey("nonexistent") == false);
	
	delete obj;
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
	auto* v = asvJSONValue::makeString("hello", 5);
	ASSERT_EQ(v->getStringLen(), 5);
	delete v;
}

TEST(testValueGetInt) {
	auto* v = asvJSONValue::makeInt(12345);
	ASSERT_EQ(v->getInt(), 12345);
	delete v;
}

TEST(testValueGetDouble) {
	auto* v = asvJSONValue::makeDouble(2.718);
	ASSERT(v->getDouble() > 2.717 && v->getDouble() < 2.719);
	delete v;
}

TEST(testValueGetBool) {
	auto* vt = asvJSONValue::makeBool(true);
	ASSERT(vt->getBool() == true);
	delete vt;
	
	auto* vf = asvJSONValue::makeBool(false);
	ASSERT(vf->getBool() == false);
	delete vf;
}

TEST(testGetDateTime) {
	time_t now = time(nullptr);
	auto* v = asvJSONValue::makeDateTime(now, 100);
	ASSERT_EQ(v->getDateTime(), now);
	ASSERT_EQ(v->getDateTimeMs(), 100);
	delete v;
}

TEST(testGetBinary) {
	uint8_t data[] = {0xAA, 0xBB, 0xCC};
	auto* v = asvJSONValue::makeBinary(data, 3);
	auto bin = v->getBinary();
	ASSERT_EQ(bin.size(), 3);
	ASSERT_EQ(bin[0], 0xAA);
	delete v;
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

	// regex without options (no '|' separator)  regression test for BSON toBSON bug #2
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
	
	auto* newVal = asvJSONValue::makeString("Updated", 7);
	json.setByPointer("/name", newVal);
	
	ASSERT_EQ(json.getString("name"), "Updated");
}

TEST(testSetByPointerArrayExpand) {
	asvJSON json;
	json.parse(std::string("{\"arr\": [1]}"));
	
	auto* newVal = asvJSONValue::makeInt(2);
	json.setByPointer("/arr/5", newVal);
	
	auto* arr = json.getArray("arr");
	if (!arr) throw std::runtime_error("array not found");
	if (arr->size() != 6) throw std::runtime_error("array size should be 6");
}

TEST(testSetByPointerArrayAppend) {
	asvJSON json;
	json.parse(std::string("[1, 2, 3]"));
	
	auto* newVal = asvJSONValue::makeInt(4);
	json.setByPointer("/-", newVal);
	
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
	auto* v = asvJSONValue::makeString(huge.c_str(), huge.size());
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
	auto* v = asvJSONValue::makeString("\x00\x01\x1F", 3);
	ASSERT(v != nullptr);
	std::string out;
	v->serialize(out);
	ASSERT(out.find("\\u0000") != std::string::npos);
	delete v;
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
	// "test" with 1.0 (DOUBLE) should match INT 1
	asvJSON patch;
	patch.parse(std::string("[{\"op\": \"test\", \"path\": \"/x\", \"value\": 1.0}]"));
	if (!json.applyPatch(patch)) throw std::runtime_error("INT 1 should match DOUBLE 1.0");
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
	std::string encoded = base64_encode(data, 3);
	ASSERT_EQ(encoded, "AQID");
}

TEST(testBase64Decode) {
	std::string encoded = "AQID";
	auto decoded = base64_decode_fast(encoded.c_str(), encoded.length());
	ASSERT_EQ(decoded.size(), 3);
	ASSERT_EQ(decoded[0], 0x01);
	ASSERT_EQ(decoded[1], 0x02);
	ASSERT_EQ(decoded[2], 0x03);
}

TEST(testBase64Roundtrip) {
	setBase64Chars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
	uint8_t data[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9};
	std::string encoded = base64_encode(data, sizeof(data));
	auto decoded = base64_decode_fast(encoded.c_str(), encoded.length());
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
	std::string encoded = base64_encode(data, 3);
	auto decoded = base64_decode_fast(encoded.c_str(), encoded.length());
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
	auto* original = asvJSONValue::makeObject();
	original->obj->emplace("key", std::unique_ptr<asvJSONValue>(asvJSONValue::makeInt(42)));
	auto* cloned = cloneValue(original);
	ASSERT(cloned != nullptr);
	ASSERT_EQ(cloned->get("key")->getInt(), 42);
	auto* newVal = asvJSONValue::makeInt(99);
	original->obj->find("key")->second.reset(newVal);
	ASSERT_EQ(cloned->get("key")->getInt(), 42);
	delete original;
	delete cloned;
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
	auto* v = asvJSONValue::makeString("test", 4);
	std::string_view sv = v->getStringView();
	ASSERT(sv == "test");
	ASSERT(sv.length() == 4);
	delete v;
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
			const char* k = kv.first;
			while (*k) inner.push_back(static_cast<uint8_t>(*k++));
			inner.push_back(0);
			inner.push_back(0x12);
			addLE64(inner, static_cast<uint64_t>(kv.second));
		}
		uint32_t innerLen = 4 + static_cast<uint32_t>(inner.size()) + 1;
		std::vector<uint8_t> outerBody;
		outerBody.push_back('a'); outerBody.push_back('r'); outerBody.push_back('r');
		outerBody.push_back(0);
		outerBody.push_back(0x04);
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
		auto* v = asvJSONValue::makeString("test", 4);
		std::string out;
		v->toXML(out);
		ASSERT(!out.empty());
		ASSERT(out.find("test") != std::string::npos);
		ASSERT(out.find("<root>") != std::string::npos);
		delete v;
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
		ASSERT(yml.find("n: ~") != std::string::npos);
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
		auto* v = asvJSONValue::makeString("test", 4);
		std::string out;
		v->toYAML(out);
		ASSERT(!out.empty());
		ASSERT(out.find("---") != std::string::npos);
		ASSERT(out.find("test") != std::string::npos);
		delete v;
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
		auto* v = asvJSONValue::makeString("test", 4);
		std::string out;
		v->toCSV(out);
		ASSERT(out.find("test") != std::string::npos);
		ASSERT(out.find("value") == std::string::npos); // no header
		delete v;
	}
	// Empty object
	{
		asvJSON json;
		json.parse(std::string("{}"));
		std::string csv = json.toCSV();
		ASSERT(csv.empty());
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
	
	std::cout << "\n--- XML Serialization Tests ---\n";
	RUN(testToXML);
	
	std::cout << "\n--- YAML Serialization Tests ---\n";
	RUN(testToYAML);
	
	std::cout << "\n--- CSV Serialization Tests ---\n";
	RUN(testToCSV);
	
	std::cout << "\n--- TOON Serialization Tests ---\n";
	RUN(testToTOON);
	RUN(testToTOONAdvanced);
	
	std::cout << "\n========================================" << std::endl;
	std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
	std::cout << "========================================" << std::endl;
	
	return failed > 0 ? 1 : 0;
}
