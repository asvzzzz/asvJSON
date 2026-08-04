#include <iostream>
#include <cstring>
#include <ctime>
#include <limits>
#include "asvJSON++.hpp"
using namespace asvJSONInternal;

void example_basic() {
	std::cout << "=== Basic Usage ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	
	json.putString("name", "John Doe");
	json.putInt("age", 30);
	json.putDouble("height", 180.5);
	json.putBool("isStudent", false);
	json.putNull("middleName");
	
	std::cout << "Name: " << json.getString("name") << std::endl;
	std::cout << "Age: " << json.getInt("age") << std::endl;
	std::cout << "Height: " << json.getDouble("height") << std::endl;
	std::cout << "Is Student: " << (json.getBool("isStudent") ? "true" : "false") << std::endl;
	std::cout << "Middle Name is null: " << (json.isNull("middleName") ? "true" : "false") << std::endl;
	
	std::cout << "Serialized: " << json.serialize() << std::endl;
	std::cout << "Pretty: " << json.serialize(true) << std::endl << std::endl;
}

void example_string_view_key() {
	std::cout << "=== std::string_view Key Overloads ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	
	std::string key = "testKey";
	std::string_view sv_key = key;
	
	json.putString(sv_key, "testValue");
	json.putInt(sv_key, 42);
	json.putDouble(sv_key, 3.14);
	json.putBool(sv_key, true);
	
	std::cout << "String: " << json.getString(sv_key) << std::endl;
	std::cout << "Int: " << json.getInt(sv_key) << std::endl;
	std::cout << "Double: " << json.getDouble(sv_key) << std::endl;
	std::cout << "Bool: " << (json.getBool(sv_key) ? "true" : "false") << std::endl;
	std::cout << "Has key: " << (json.hasKey(sv_key) ? "true" : "false") << std::endl << std::endl;
}

void example_string_view_zero_copy() {
	std::cout << "=== string_view Zero-Copy Access ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name": "John Doe", "data": "test data here"})";
	json.parse(input);
	
	auto* v = json.get("name");
	if (v && v->type == asvJSONValue::STRING) {
		std::string_view sv = v->getStringView();
		std::cout << "Zero-copy string_view: " << sv << std::endl;
	}
	
	v = json.get("data");
	if (v && v->type == asvJSONValue::STRING) {
		std::string_view sv = v->getStringView();
		std::cout << "Another zero-copy view: " << sv << std::endl;
	}
	std::cout << std::endl;
}

void example_optional_getters() {
	std::cout << "=== Optional Getters ===" << std::endl;
	
	asvJSON json;
	std::string input = "{\"name\": \"John\"}";
	json.parse(input);
	
	std::cout << "optString (exists): " << json.optString("name", "Unknown") << std::endl;
	std::cout << "optString (missing): " << json.optString("missing", "Default") << std::endl;
	std::cout << "optInt (missing): " << json.optInt("age", 0) << std::endl;
	std::cout << "optBool (missing): " << (json.optBool("active", true) ? "true" : "false") << std::endl << std::endl;
}

void example_binary_data() {
	std::cout << "=== Binary Data ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	
	uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	json.putBinary("data", data, sizeof(data));
	
	auto retrieved = json.getBinary("data");
	std::cout << "Original size: " << sizeof(data) << ", Retrieved size: " << retrieved.size() << std::endl;
	std::cout << "First byte: " << (int)retrieved[0] << std::endl;
	std::cout << "get() method check: " << (json.get("data") ? "exists" : "null") << std::endl << std::endl;
}

void example_chunked_binary() {
	std::cout << "=== Chunked Binary ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	
	std::vector<uint8_t> large_data(200, 0xAA);
	json.putBinChunked("largeData", large_data.data(), large_data.size(), 76);
	
	auto retrieved = json.getBinChunked("largeData");
	std::cout << "Original size: " << large_data.size() << ", Retrieved size: " << retrieved.size() << std::endl;
	auto* arr = json.getArray("largeData");
	if (arr) {
		std::cout << "Chunked array size: " << arr->size() << std::endl;
	}
	std::cout << "Serialized (chunked): " << json.serialize().substr(0, 100) << "..." << std::endl << std::endl;
}

void example_datetime() {
	std::cout << "=== DateTime ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	
	time_t now = time(nullptr);
	json.putDateTime("timestamp", now);
	
	std::cout << "Timestamp: " << json.getDateTime("timestamp") << std::endl;
	std::cout << "DateTime string: " << json.getDateTimeString("timestamp") << std::endl;
	std::cout << std::endl;
}

void example_datetime_with_ms() {
	std::cout << "=== DateTime with Milliseconds ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"timestamp": "2024-01-15T10:30:45.123Z"})";
	json.parse(input);
	
	time_t ts = json.getDateTime("timestamp");
	int ms = json.getDateTimeMs("timestamp");
	std::cout << "Timestamp: " << ts << ", Milliseconds: " << ms << std::endl;
	std::cout << "DateTime string: " << json.getDateTimeString("timestamp") << std::endl << std::endl;
}

void example_arrays() {
	std::cout << "=== Arrays ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"items": ["apple", "banana", "cherry"]})";
	json.parse(input);
	
	auto* arr = json.getArray("items");
	if (arr) {
		std::cout << "Array size: " << arr->size() << std::endl;
		for (size_t i = 0; i < arr->size(); i++) {
			auto* item = arr->get(i);
			if (item && item->type == asvJSONValue::STRING) {
				std::cout << "  [" << i << "] " << std::string(item->str_data.data(), item->str_data.size()) << std::endl;
			}
		}
	}
	std::cout << std::endl;
}

void example_nested_access() {
	std::cout << "=== Nested Access ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"user": {"name": "John", "address": {"city": "NYC", "zip": "10001"}}})";
	json.parse(input);
	
	auto* name = json.getNested("user.name");
	auto* city = json.getNested("user.address.city");
	auto* missing = json.getNested("user.phone");
	
	if (name && name->type == asvJSONValue::STRING) {
		std::cout << "user.name: " << std::string(name->str_data.data(), name->str_data.size()) << std::endl;
	}
	if (city && city->type == asvJSONValue::STRING) {
		std::cout << "user.address.city: " << std::string(city->str_data.data(), city->str_data.size()) << std::endl;
	}
	std::cout << "user.phone exists: " << (missing ? "true" : "false") << std::endl << std::endl;
}

void example_object_operations() {
	std::cout << "=== Object Operations ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"a": 1, "b": 2, "c": 3})";
	json.parse(input);
	
	auto keys = json.getKeys();
	std::cout << "Keys: ";
	for (const auto& k : keys) std::cout << k << " ";
	std::cout << std::endl;
	
	std::cout << "Size: " << json.size() << std::endl;
	std::cout << "Has 'a': " << (json.hasKey("a") ? "true" : "false") << std::endl;
	
	json.remove("b");
	std::cout << "After remove 'b', size: " << json.size() << std::endl;
	std::cout << "Serialized: " << json.serialize() << std::endl << std::endl;
}

void example_base64_custom_charset() {
	std::cout << "=== Custom Base64 Charset ===" << std::endl;
	
	setBase64Chars("XYZabcdefghijklmnopqrstuvwxyz0123456789+/ABCDEFGHIJKLMNOPQRSTUV");
	
	uint8_t data[] = {0x01, 0x02, 0x03};
	std::string encoded = encodeBase64(data, sizeof(data));
	
	std::cout << "Custom charset input (64 chars): XYZ..." << std::endl;
	std::cout << "Custom charset encoded: " << encoded << std::endl;
	std::cout << "Current charset (64 chars): " << getBase64Chars() << std::endl;

	setBase64Chars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
	std::cout << "Restored default charset." << std::endl << std::endl;
}

void example_file_io() {
	std::cout << "=== File I/O ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	json.putString("name", "John");
	json.putInt("age", 30);
	
	json.writeToFile("test_output.json", true);
	std::cout << "Written to test_output.json" << std::endl;
	
	asvJSON json2;
	json2.readFromFile("test_output.json");
	std::cout << "Read from file: " << json2.serialize() << std::endl << std::endl;
}

void example_comments() {
	std::cout << "=== Comments ===" << std::endl;
	
	std::string jsonWithComments = R"({
	// This is a single-line comment
	"name": "John",
	/* This is a
	   multi-line comment */
	"age": 30,
	# Shell-style comment
	"city": "NYC"
})";
	
	asvJSON json;
	bool parsed = json.parse(jsonWithComments);
	std::cout << "Parse with comments: " << (parsed ? "success" : "failed") << std::endl;
	std::cout << "Name: " << json.getString("name") << std::endl;
	std::cout << "Age: " << json.getInt("age") << std::endl << std::endl;
}

void example_pretty_print() {
	std::cout << "=== Pretty Print ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name":"John","age":30,"city":"NYC"})";
	json.parse(input);
	
	std::cout << "Compact: " << json.serialize() << std::endl;
	std::cout << "Pretty: " << json.serialize(true) << std::endl << std::endl;
}

void example_get_value() {
	std::cout << "=== get() method ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name": "John", "age": 30})";
	json.parse(input);
	
	auto* v = json.get("name");
	if (v) {
		std::cout << "Type: " << v->typeToString(v->type) << std::endl;
		std::cout << "Value: " << (v->type == asvJSONValue::STRING ? std::string(v->str_data.data(), v->str_data.size()) : "") << std::endl;
	}
	
	v = json.get("age");
	if (v) {
		std::cout << "Int value: " << v->getInt() << std::endl;
	}
	std::cout << std::endl;
}

void example_clear() {
	std::cout << "=== clear() method ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name": "John", "age": 30})";
	json.parse(input);
	std::cout << "Before clear - size: " << json.size() << std::endl;
	
	json.clear();
	std::cout << "After clear - size: " << json.size() << std::endl;
	std::cout << "Serialized: " << json.serialize() << std::endl << std::endl;
}

void example_get_root() {
	std::cout << "=== getObject() (get root) ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	
	auto* root = json.getObject();
	std::cout << "Root type: " << (root ? root->typeToString(root->type) : "null") << std::endl;
	
	json.putString("newKey", "value");
	std::cout << "After adding via root: " << json.serialize() << std::endl << std::endl;
}

void example_regex() {
	std::cout << "=== Regex (makeRegex) ===" << std::endl;
	
	auto regex = asvJSONValue::makeRegex("\\d+", "g");
	if (regex) {
		std::cout << "Regex type: " << regex->typeToString(regex->type) << std::endl;
		std::cout << "Regex value: " << std::string(regex->str_data.data(), regex->str_data.size()) << std::endl;
	}
	std::cout << std::endl;
}

void example_messagepack() {
	std::cout << "=== MessagePack ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name": "Test", "count": 42, "active": true})";
	json.parse(input);
	
	auto mp = json.toMessagePack();
	std::cout << "JSON size: " << json.serialize().size() << " bytes" << std::endl;
	std::cout << "MessagePack size: " << mp.size() << " bytes" << std::endl;
	
	asvJSON json2;
	json2.fromMessagePack(mp.data(), mp.size());
	std::cout << "Roundtrip - name: " << json2.getString("name") << ", count: " << json2.getInt("count") << std::endl << std::endl;
}

void example_bson() {
	std::cout << "=== BSON ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name": "Test", "count": 42})";
	json.parse(input);
	
	auto bson = json.toBSON();
	std::cout << "JSON size: " << json.serialize().size() << " bytes" << std::endl;
	std::cout << "BSON size: " << bson.size() << " bytes" << std::endl;
	
	asvJSON json2;
	json2.fromBSON(bson.data(), bson.size());
	std::cout << "Roundtrip - name: " << json2.getString("name") << ", count: " << json2.getInt("count") << std::endl << std::endl;
}

void example_cbor() {
	std::cout << "=== CBOR (RFC 8949) ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name": "Test", "count": 42, "active": true, "data": [1, 2.5, null]})";
	json.parse(input);
	
	auto cbor = json.toCBOR();
	std::cout << "JSON size: " << json.serialize().size() << " bytes" << std::endl;
	std::cout << "CBOR size: " << cbor.size() << " bytes" << std::endl;
	
	asvJSON json2;
	json2.fromCBOR(cbor.data(), cbor.size());
	std::cout << "Roundtrip - name: " << json2.getString("name")
	          << ", count: " << json2.getInt("count")
	          << ", active: " << (json2.getBool("active") ? "true" : "false") << std::endl << std::endl;
}

void example_json_pointer() {
	std::cout << "=== JSON Pointer ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"user": {"name": "John", "address": {"city": "NYC"}}})";
	json.parse(input);
	
	auto* v = json.getByPointer("/user/name");
	if (v) std::cout << "getByPointer /user/name: " << (v->type == asvJSONValue::STRING ? std::string(v->str_data.data(), v->str_data.size()) : "") << std::endl;
	
	json.setByPointer("/user/age", asvJSONValue::makeInt(30));
	std::cout << "After setByPointer /user/age: " << json.serialize() << std::endl;
	
	json.removeByPointer("/user/address");
	std::cout << "After removeByPointer /user/address: " << json.serialize() << std::endl;
	
	asvJSON json2;
	json2.parse(std::string("[1, 2, 3]"));
	json2.setByPointer("/-", asvJSONValue::makeInt(4));
	json2.setByPointer("/-", asvJSONValue::makeInt(5));
	std::cout << "After array append: " << json2.serialize() << std::endl << std::endl;
}

void example_json_patch() {
	std::cout << "=== JSON Patch (RFC 6902) ===" << std::endl;

	asvJSON json;
	std::string input = R"({"title": "Hello", "author": {"name": "John"}, "oldField": "removed"})";
	json.parse(input);

	asvJSON patch;
	std::string patchInput = R"([
		{"op": "replace", "path": "/title", "value": "World"},
		{"op": "add", "path": "/author/email", "value": "jane@example.com"},
		{"op": "add", "path": "/newField", "value": "added"},
		{"op": "remove", "path": "/oldField"}
	])";
	patch.parse(patchInput);

	if (json.applyPatch(patch)) {
		std::cout << "After applyPatch: " << json.serialize() << std::endl;
	} else {
		std::cout << "Patch failed: " << json.getLastError() << std::endl;
	}
	std::cout << std::endl;
}

void example_clone() {
	std::cout << "=== Clone (cloneValue) ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"name": "John", "age": 30})";
	json.parse(input);
	
	auto* original = json.get("name");
	if (original) {
		auto cloned = cloneValue(original);
		std::cout << "Original: " << (original->type == asvJSONValue::STRING ? std::string(original->str_data.data(), original->str_data.size()) : "") << std::endl;
		std::cout << "Cloned: " << (cloned->type == asvJSONValue::STRING ? std::string(cloned->str_data.data(), cloned->str_data.size()) : "") << std::endl;
	}
	std::cout << std::endl;
}

void example_get_array() {
	std::cout << "=== getArray() method ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"fruits": ["apple", "banana", "cherry"], "empty": []})";
	json.parse(input);
	
	auto* arr = json.getArray("fruits");
	if (arr) {
		std::cout << "fruits array size: " << arr->size() << std::endl;
		for (size_t i = 0; i < arr->size(); i++) {
			auto* item = arr->get(i);
			if (item && item->type == asvJSONValue::STRING) {
				std::cout << "  " << std::string(item->str_data.data(), item->str_data.size()) << std::endl;
			}
		}
	}
	
	auto* emptyArr = json.getArray("missing");
	std::cout << "Missing array: " << (emptyArr ? "exists" : "null") << std::endl << std::endl;
}

void example_array_add() {
	std::cout << "=== Array Add Methods ===" << std::endl;
	
	asvJSON json;
	std::string input = "{\"items\": []}";
	json.parse(input);
	
	json.arrayAddString("items", "apple");
	json.arrayAddInt("items", 42);
	json.arrayAddDouble("items", 3.14);
	json.arrayAddBool("items", true);
	json.arrayAddNull("items");
	
	auto* arr = json.getArray("items");
	if (arr) {
		std::cout << "Array size: " << arr->size() << std::endl;
		for (size_t i = 0; i < arr->size(); i++) {
			auto* item = arr->get(i);
			if (item) std::cout << "  [" << i << "] " << item->typeToString(item->type) << std::endl;
		}
	}
	std::cout << "Serialized: " << json.serialize() << std::endl << std::endl;
}

void example_array_add_datetime() {
	std::cout << "=== arrayAddDateTime ===" << std::endl;
	
	asvJSON json;
	std::string input = "{\"timestamps\": []}";
	json.parse(input);
	
	time_t now = time(nullptr);
	json.arrayAddDateTime("timestamps", now);
	
	auto* arr = json.getArray("timestamps");
	if (arr && arr->size() > 0) {
		auto* item = arr->get(static_cast<size_t>(0));
		if (item && item->type == asvJSONValue::DATETIME) {
			std::cout << "Added timestamp: " << item->timestamp << std::endl;
		}
	}
	std::cout << "Serialized: " << json.serialize() << std::endl << std::endl;
}

void example_opt_datetime() {
	std::cout << "=== Optional DateTime Methods ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"timestamp": "2024-01-15T10:30:00Z"})";
	json.parse(input);
	
	time_t dt = json.optDateTime("timestamp", 0);
	std::cout << "optDateTime (exists): " << dt << std::endl;
	
	time_t missing = json.optDateTime("missing", 1234567890);
	std::cout << "optDateTime (missing): " << missing << std::endl;
	
	std::tm tm = json.optDateTimeTM("timestamp", std::tm{});
	std::cout << "optDateTimeTM year: " << tm.tm_year + 1900 << std::endl << std::endl;
}

void example_type_to_string() {
	std::cout << "=== typeToString() method ===" << std::endl;
	
	auto str = asvJSONValue::makeString("test", 4);
	auto num = asvJSONValue::makeInt(42);
	auto dbl = asvJSONValue::makeDouble(1.5);
	auto boo = asvJSONValue::makeBool(true);
	auto nul = asvJSONValue::makeNull();
	auto obj = asvJSONValue::makeObject();
	auto arr = asvJSONValue::makeArray();
	uint8_t binData[] = {'a', 'b', 'c'};
	auto bin = asvJSONValue::makeBinary(binData, 3);
	auto dt = asvJSONValue::makeDateTime(time(nullptr));

	std::cout << "string: " << str->typeToString(str->type) << std::endl;
	std::cout << "int: " << num->typeToString(num->type) << std::endl;
	std::cout << "double: " << dbl->typeToString(dbl->type) << std::endl;
	std::cout << "bool: " << boo->typeToString(boo->type) << std::endl;
	std::cout << "null: " << nul->typeToString(nul->type) << std::endl;
	std::cout << "object: " << obj->typeToString(obj->type) << std::endl;
	std::cout << "array: " << arr->typeToString(arr->type) << std::endl;
	std::cout << "binary: " << bin->typeToString(bin->type) << std::endl;
	std::cout << "datetime: " << dt->typeToString(dt->type) << std::endl;
	std::cout << std::endl;
}

void example_merge() {
	std::cout << "=== merge() method ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"a": 1, "b": 2})";
	json.parse(input);
	
	asvJSON other;
	std::string otherInput = R"({"b": 3, "c": 4})";
	other.parse(otherInput);
	
	json.merge(other);
	
	std::cout << "After merge: " << json.serialize() << std::endl;
	std::cout << "a=" << json.getInt("a") << ", b=" << json.getInt("b") << ", c=" << json.getInt("c") << std::endl << std::endl;
}

void example_date_time_string() {
	std::cout << "=== getDateTimeString() ===" << std::endl;
	
	asvJSON json;
	std::string input = R"({"timestamp": "2024-01-15T10:30:45.123Z"})";
	json.parse(input);
	
	std::cout << "DateTime string: " << json.getDateTimeString("timestamp") << std::endl;
	
	std::string missing = json.getDateTimeString("missing");
	std::cout << "Missing key result: '" << missing << "'" << std::endl << std::endl;
}

void example_string_overloads() {
	std::cout << "=== String Key Overloads for get/put ===" << std::endl;
	
	asvJSON json;
	std::string input = "{}";
	json.parse(input);
	
	std::string key = "testKey";
	json.putString(key, "testValue");
	json.putInt(key, 100);
	json.putDouble(key, 2.5);
	json.putBool(key, true);
	json.putNull(key);
	
	std::cout << "getString: " << json.getString(key) << std::endl;
	std::cout << "getInt: " << json.getInt(key) << std::endl;
	std::cout << "getDouble: " << json.getDouble(key) << std::endl;
	std::cout << "getBool: " << (json.getBool(key) ? "true" : "false") << std::endl;
	std::cout << "isNull: " << (json.isNull(key) ? "true" : "false") << std::endl;
	std::cout << "hasKey: " << (json.hasKey(key) ? "true" : "false") << std::endl;
	std::cout << "remove: " << json.size() << " before" << std::endl;
	json.remove(key);
	std::cout << "remove: " << json.size() << " after" << std::endl << std::endl;
}

void example_put_float32() {
	std::cout << "=== putFloat32 / is_float32 ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{}"));
	json.putFloat32("value", 3.14f);

	auto* v = json.get("value");
	if (v) {
		std::cout << "Type: " << v->typeToString(v->type) << std::endl;
		std::cout << "Double value: " << v->getDouble() << std::endl;
		std::cout << "is_float32 flag: " << (v->is_float32 ? "true" : "false") << std::endl;
	}
	std::cout << "Serialized: " << json.serialize() << std::endl << std::endl;
}

void example_nan_infinity() {
	std::cout << "=== NaN and Infinity Handling ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{}"));
	json.putDouble("nan_val", std::numeric_limits<double>::quiet_NaN());
	json.putDouble("inf_val", std::numeric_limits<double>::infinity());

	std::cout << "Default serialization: " << json.serialize() << std::endl;

	json.allowNaNInfinity = true;
	std::cout << "With allowNaNInfinity: " << json.serialize() << std::endl << std::endl;
}

void example_type_checks() {
	std::cout << "=== Type Checks (isXxx) ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{}"));
	uint8_t bin[] = {0x01, 0x02, 0x03};
	json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
	json.putTimestamp("ts", 1000);
	json.putRegex("rx", "^test$", "gi");
	json.putBinary("bin", bin, 3);
	json.putDateTime("dt", time(nullptr));

	std::cout << "isObjectId('oid'): " << json.isObjectId("oid") << std::endl;
	std::cout << "isTimestamp('ts'): " << json.isTimestamp("ts") << std::endl;
	std::cout << "isRegex('rx'): " << json.isRegex("rx") << std::endl;
	std::cout << "isBinary('bin'): " << json.isBinary("bin") << std::endl;
	std::cout << "isDateTime('dt'): " << json.isDateTime("dt") << std::endl;
	std::cout << "isObjectId('missing'): " << json.isObjectId("missing") << std::endl;
	std::cout << "Serialized: " << json.serialize() << std::endl << std::endl;
}

void example_get_special_types() {
	std::cout << "=== Get Special Types ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{}"));
	json.putObjectId("oid", std::string_view("ABCDEF123456", 12));
	json.putTimestamp("ts", 1000);
	json.putRegex("rx", "^test$", "gi");

	std::cout << "getObjectId('oid'): " << json.getObjectId("oid") << std::endl;
	std::cout << "getTimestamp('ts'): " << json.getTimestamp("ts") << std::endl;

	auto rx = json.getRegex("rx");
	std::cout << "getRegex pattern: " << rx.first << std::endl;
	std::cout << "getRegex options: " << rx.second << std::endl;

	std::string pat, opt;
	json.getRegex("rx", pat, opt);
	std::cout << "pattern (ref): " << pat << ", options (ref): " << opt << std::endl << std::endl;
}

void example_extension() {
	std::cout << "=== Extension Types ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{}"));
	uint8_t extData[] = {0xDE, 0xAD, 0xBE, 0xEF};
	json.putExtension("ext", 7, extData, 4);

	std::cout << "isExtension('ext'): " << json.isExtension("ext") << std::endl;
	auto ext = json.getExtension("ext");
	std::cout << "Extension type: " << (int)ext.first << std::endl;
	std::cout << "Extension data size: " << ext.second.size() << std::endl;
	std::cout << "First byte: 0x" << std::hex << (int)ext.second[0] << std::dec << std::endl << std::endl;
}

void example_nested_special_types() {
	std::cout << "=== Nested Special Types ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{}"));
	json.getObject()->obj->emplace("sub", std::unique_ptr<asvJSONValue>(asvJSONValue::makeObject()));
	auto* sub = json.getRoot()->get("sub");
	sub->obj->emplace("oid", std::unique_ptr<asvJSONValue>(asvJSONValue::makeObjectId("ABCDEF123456")));
	sub->obj->emplace("ts", std::unique_ptr<asvJSONValue>(asvJSONValue::makeTimestamp(1000)));
	sub->obj->emplace("rx", std::unique_ptr<asvJSONValue>(asvJSONValue::makeRegex("^test$", "gi")));

	std::cout << "getNestedObjectId('sub.oid'): " << json.getNestedObjectId("sub.oid") << std::endl;
	std::cout << "getNestedTimestamp('sub.ts'): " << json.getNestedTimestamp("sub.ts") << std::endl;
	auto nrx = json.getNestedRegex("sub.rx");
	std::cout << "getNestedRegex pattern: " << nrx.first << std::endl;
	std::cout << "getNestedRegex options: " << nrx.second << std::endl << std::endl;
}

void example_static_msgpack_convert() {
	std::cout << "=== Static MessagePack Converters ===" << std::endl;

	std::string jsonInput = "{\"a\":1,\"b\":2}";
	auto mp = asvJSON::messagePackFromString(jsonInput);
	std::cout << "messagePackFromString size: " << mp.size() << " bytes" << std::endl;

	std::string jsonOut = asvJSON::stringFromMessagePack(mp.data(), mp.size());
	std::cout << "stringFromMessagePack: " << jsonOut << std::endl << std::endl;
}

void example_msgpack_bson_string() {
	std::cout << "=== fromMessagePack/fromBSON with std::string ===" << std::endl;

	asvJSON src;
	src.parse(std::string("{\"x\":42}"));

	auto mp = src.toMessagePack();
	std::string mpStr(reinterpret_cast<const char*>(mp.data()), mp.size());
	asvJSON json1;
	json1.fromMessagePack(mpStr);
	std::cout << "fromMessagePack(string): x=" << json1.getInt("x") << std::endl;

	auto bson = src.toBSON();
	std::string bsonStr(reinterpret_cast<const char*>(bson.data()), bson.size());
	asvJSON json2;
	json2.fromBSON(bsonStr);
	std::cout << "fromBSON(string): x=" << json2.getInt("x") << std::endl << std::endl;
}

void example_valid_utf8() {
	std::cout << "=== isValidUTF8 ===" << std::endl;

	uint8_t valid[] = {'H', 'e', 'l', 'l', 'o'};
	uint8_t invalid[] = {0xFF, 0xFE};
	uint8_t validRussian[] = {0xD0, 0x9F, 0xD1, 0x80, 0xD0, 0xB8, 0xD0, 0xB2, 0xD0, 0xB5, 0xD1, 0x82}; // "Hello" in Russian
	uint8_t validEmoji[] = {0xF0, 0x9F, 0x98, 0x80}; // U+1F600

	std::cout << "ASCII valid: " << isValidUTF8(valid, 5) << std::endl;
	std::cout << "Invalid bytes: " << isValidUTF8(invalid, 2) << std::endl;
	std::cout << "Russian valid: " << isValidUTF8(validRussian, 12) << std::endl;
	std::cout << "Emoji valid: " << isValidUTF8(validEmoji, 4) << std::endl << std::endl;
}

void example_root_array() {
	std::cout << "=== getRootArray ===" << std::endl;

	asvJSON json;
	json.parse(std::string("[10, 20, 30]"));

	asvJSONValue* arr = json.getRootArray();
	if (arr) {
		std::cout << "Root array size: " << arr->size() << std::endl;
		std::cout << "First element: " << arr->get(static_cast<size_t>(0))->getInt() << std::endl;
	}

	const asvJSON& constRef = json;
	const asvJSONValue* constArr = constRef.getRootArray();
	std::cout << "Const access - root type: " << (constArr ? constArr->typeToString(constArr->type) : "null") << std::endl << std::endl;
}

void example_get_const() {
	std::cout << "=== getConst (const-correct access) ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{\"key\": \"value\"}"));

	const asvJSON& constRef = json;
	const asvJSONValue* v = constRef.getConst("key");
	if (v && v->type == asvJSONValue::STRING) {
		std::cout << "Const value: " << std::string(v->str_data.data(), v->str_data.size()) << std::endl;
	}

	v = constRef.getConst(static_cast<size_t>(0));
	std::cout << "getConst by index: " << (v ? "exists" : "null") << std::endl << std::endl;
}

void example_copy_move() {
	std::cout << "=== Copy / Move Semantics ===" << std::endl;

	asvJSON original;
	original.parse(std::string("{\"a\": 1, \"b\": 2}"));

	asvJSON copied(original); // copy constructor
	std::cout << "Copied: " << copied.serialize() << std::endl;

	asvJSON moved(std::move(original)); // move constructor
	std::cout << "Moved: " << moved.serialize() << std::endl;

	asvJSON assigned;
	assigned = moved; // copy assignment
	std::cout << "Copy assigned: " << assigned.serialize() << std::endl;

	asvJSON moveAssigned;
	moveAssigned = std::move(assigned); // move assignment
	std::cout << "Move assigned: " << moveAssigned.serialize() << std::endl << std::endl;
}

void example_remove_by_pointer() {
	std::cout << "=== removeByPointer ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{\"user\": {\"name\": \"John\", \"age\": 30, \"city\": \"NYC\"}}"));

	std::cout << "Before: " << json.serialize() << std::endl;
	json.removeByPointer("/user/age");
	std::cout << "After remove /user/age: " << json.serialize() << std::endl;
	json.removeByPointer("/user");
	std::cout << "After remove /user: " << json.serialize() << std::endl << std::endl;
}

void example_to_xml() {
	std::cout << "=== toXML Serialization ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"name":"John & Jane","age":30,"active":true,"nickname":null,"pi":3.14159,"items":[1,2,3],"meta":{"nested":true}})"));
	std::cout << json.toXML() << std::endl;
}

void example_from_xml() {
	std::cout << "=== fromXML Parsing ===" << std::endl;

	std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<root>
  <name>John &amp; Jane</name>
  <age>30</age>
  <active>true</active>
  <items>
    <item>1</item>
    <item>2</item>
    <item>3</item>
  </items>
  <meta>
    <nested>true</nested>
  </meta>
</root>)";

	asvJSON json;
	if (json.fromXML(std::string_view(xml))) {
		auto* root = json.getRoot();
		if (root) root = root->get("root");
		if (root) {
			std::cout << "  name=" << (root->get("name") ? root->get("name")->getString() : "?")
			          << " age=" << (root->get("age") ? root->get("age")->getInt() : 0)
			          << " active=" << (root->get("active") && root->get("active")->getBool() ? "true" : "false")
			          << " nested=" << (root->get("meta") && root->get("meta")->get("nested") && root->get("meta")->get("nested")->getBool() ? "true" : "false")
			          << std::endl;
		}
	} else {
		std::cout << "  fromXML failed: " << json.getLastError() << std::endl;
	}
}

void example_xml_escaped_keys() {
	std::cout << "=== toXML with Escaped Keys ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"key with spaces":42,"data\nnewline":true,"tab\there":3.14})"));
	std::cout << json.toXML() << std::endl;
}

void example_opt_datetime_tm_default() {
	std::cout << "=== optDateTimeTM with default ===" << std::endl;

	asvJSON json;
	json.parse(std::string("{\"dt\": \"2024-01-15T10:30:00Z\"}"));

	std::tm tm = json.optDateTimeTM("dt");
	std::cout << "Year: " << (tm.tm_year + 1900) << ", Month: " << (tm.tm_mon + 1) << ", Day: " << tm.tm_mday << std::endl;

	std::tm def = {};
	def.tm_year = 70;
	std::tm missing = json.optDateTimeTM("nonexistent", def);
	std::cout << "Missing default year: " << (missing.tm_year + 1900) << std::endl << std::endl;
}

void example_to_yaml() {
	std::cout << "=== toYAML Serialization ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"active":true,"items":[1,2,3],"meta":{"nested":true,"pi":3.14},"empty":null})"));
	std::cout << json.toYAML() << std::endl;
}

void example_to_yaml_multiline() {
	std::cout << "=== toYAML with Multiline Strings ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"description":"This is a\nmultiline string\nwith several lines.","key":"short"})"));
	std::cout << json.toYAML() << std::endl;
}

void example_from_yaml() {
	std::cout << "=== fromYAML Parsing ===" << std::endl;

	asvJSON json;
	if (json.fromYAML("---\nname: John\nage: 30\nactive: true\nitems:\n- 1\n- 2\n- 3\nmeta:\n  nested: true\n  pi: 3.14\n")) {
		auto* meta = json.getRoot()->get("meta");
		bool nested = meta && meta->get("nested") ? meta->get("nested")->getBool() : false;
		std::cout << "  name=" << json.getString("name")
		          << " age=" << json.getInt("age")
		          << " active=" << (json.getBool("active") ? "true" : "false")
		          << " item_count=" << json.getRoot()->get("items")->arr->size()
		          << " nested=" << (nested ? "true" : "false")
		          << std::endl;
	} else {
		std::cout << "  fromYAML failed" << std::endl;
	}
}

void example_to_csv() {
	std::cout << "=== toCSV Serialization ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"meta":{"city":"NYC","zip":"10001"}})"));
	std::cout << json.toCSV() << std::endl;
}

void example_to_csv_array() {
	std::cout << "=== toCSV with Array of Objects ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"([{"x":10,"y":20,"z":30},{"x":40,"y":50},{"y":60,"w":70}])"));
	std::cout << json.toCSV() << std::endl;
}

void example_from_csv() {
	std::cout << "=== fromCSV Parsing ===" << std::endl;

	asvJSON json;
	std::string csv = "name,age,city\nAlice,30,\"New York\"\nBob,25,London\n";
	if (json.fromCSV(std::string_view(csv))) {
		std::cout << json.serialize(true) << std::endl;
		for (size_t i = 0; i < json.getRoot()->arr->size(); i++) {
			auto row = json.getRoot()->get(i);
			std::cout << "  name=" << row->get("name")->getString()
			          << " age=" << row->get("age")->getInt()
			          << " city=" << row->get("city")->getString() << std::endl;
		}
	} else {
		std::cout << "Parse failed: " << json.getLastError() << std::endl;
	}
}

void example_to_toon() {
	std::cout << "=== TOON Serialization ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"active":true,"items":[10,20,30],"address":{"city":"NYC"}})"));
	std::string toon = json.toTOON();
	std::cout << "TOON output:" << std::endl << toon << std::endl;

	asvJSON j2;
	if (j2.fromTOON(std::string_view(toon))) {
		std::cout << "Round-trip: name=" << j2.getString("name")
		          << " age=" << j2.getInt("age")
		          << " active=" << j2.getBool("active")
		          << " city=" << j2.getString("address.city") << std::endl;
	}
	std::cout << std::endl;
}

void example_to_tron() {
	std::cout << "=== TRON Serialization ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"([{"name":"Alice","age":30},{"name":"Bob","age":25}])"));
	std::string tron = json.toTRON();
	std::cout << "TRON output:" << std::endl << tron << std::endl;

	asvJSON j2;
	if (j2.fromTRON(std::string_view(tron))) {
		std::cout << "Round-trip:" << std::endl;
		for (size_t i = 0; i < j2.getRoot()->size(); i++) {
			auto* item = j2.getRoot()->get(i);
			std::cout << "  name=" << item->getConst("name")->getString()
			          << " age=" << item->getConst("age")->getInt() << std::endl;
		}
	}
	std::cout << std::endl;
}

void example_to_goon() {
	std::cout << "=== GOON Serialization ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"active":true,"items":[10,20,30],"address":{"city":"NYC"}})"));
	std::string goon = json.toGOON();
	std::cout << "GOON output:" << std::endl << goon << std::endl;

	asvJSON j2;
	if (j2.fromGOON(std::string_view(goon))) {
		std::cout << "Round-trip: name=" << j2.getString("name")
		          << " age=" << j2.getInt("age")
		          << " active=" << j2.getBool("active")
		          << " city=" << j2.getString("address.city") << std::endl;
	}

	// Tabular array example
	asvJSON tabJson;
	tabJson.parse(std::string(R"([{"x":1,"y":2},{"x":3,"y":4}])"));
	std::string tabGoon = tabJson.toGOON();
	std::cout << "Tabular GOON:" << std::endl << tabGoon << std::endl;

	asvJSON j3;
	if (j3.fromGOON(std::string_view(tabGoon))) {
		std::cout << "Tabular round-trip OK" << std::endl;
	}
	std::cout << std::endl;
}

void example_protobuf() {
	std::cout << "=== Protobuf Binary Format ===" << std::endl;

	// Schema-driven encoding/decoding (recommended)
	std::string schema = R"({
		"name":{"id":1,"type":"string"},
		"age":{"id":2,"type":"int32"},
		"active":{"id":3,"type":"bool"}
	})";

	asvJSON json;
	json.putString("name", "Alice");
	json.putInt("age", 30);
	json.putBool("active", true);

	auto buf = json.toProtobuf(schema);
	std::cout << "Binary protobuf size: " << buf.size() << " bytes" << std::endl;

	asvJSON json2;
	if (json2.fromProtobuf(buf.data(), buf.size(), schema)) {
		std::cout << "Round-trip: name=" << json2.getString("name")
		          << " age=" << json2.getInt("age")
		          << " active=" << json2.getBool("active") << std::endl;
	}

	// Protobuf text format
	std::string text = json.toProtobufText();
	std::cout << "Text format:" << std::endl << text << std::endl;

	asvJSON json3;
	if (json3.fromProtobufText(text)) {
		std::cout << "Text round-trip: name=" << json3.getString("name")
		          << " age=" << json3.getInt("age") << std::endl;
	}

	// Static converter helpers
	auto buf2 = asvJSON::protobufFromString(R"({"1":"hello","2":42})");
	auto jsStr = asvJSON::stringFromProtobuf(buf2.data(), buf2.size());
	std::cout << "Static helpers OK" << std::endl;

	std::cout << std::endl;
}

void example_to_toml() {
	std::cout << "=== TOML Serialization ===" << std::endl;

	// Basic round-trip
	{
		asvJSON json;
		json.parse(std::string(R"({"name":"John","age":30,"active":true,"items":[10,20,30],"address":{"city":"NYC"}})"));
		std::string toml = json.toTOML();
		std::cout << "TOML output:" << std::endl << toml << std::endl;

		asvJSON j2;
		if (j2.fromTOML(std::string_view(toml))) {
			std::cout << "Round-trip: name=" << j2.getString("name")
			          << " age=" << j2.getInt("age")
			          << " active=" << j2.getBool("active")
			          << " city=" << j2.getString("address.city") << std::endl;
		}
	}
	// Inline table with dotted keys
	{
		asvJSON j;
		j.fromTOML(std::string_view(R"(point = {x.y = 1, x.z = 2})"));
		std::cout << "Inline table dotted keys: x.y=" << j.getInt("point.x.y")
		          << " x.z=" << j.getInt("point.x.z") << std::endl;
	}
	std::cout << std::endl;
}

void example_json_lines() {
	std::cout << "=== JSON Lines (NDJSON) ===" << std::endl;

	// Encode array of objects to JSON Lines
	asvJSON json;
	json.parse(std::string(R"([{"name":"Alice","age":30},{"name":"Bob","age":25},{"name":"Charlie","age":35}])"));
	std::string jl = json.toJSONLines();
	std::cout << "JSON Lines output:" << std::endl << jl;

	// Decode back
	asvJSON j2;
	if (j2.fromJSONLines(std::string_view(jl))) {
		std::cout << "Round-trip: count=" << j2.getRoot()->size()
		          << " first=" << j2.getRoot()->get(static_cast<size_t>(0))->get("name")->getString()
		          << std::endl;
	}
	std::cout << std::endl;
}

void example_to_sexpr() {
	std::cout << "=== S-Expression Output ===" << std::endl;
	
	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"active":true,"address":{"city":"NYC","zip":10001}})"));
	std::string sexpr = json.toSexpr();
	std::cout << "S-Expression:" << std::endl << sexpr << std::endl << std::endl;
	
	// Round-trip
	asvJSON j2;
	if (j2.fromSexpr(std::string_view(sexpr))) {
		std::cout << "Round-trip: name=" << j2.getString("name")
		          << " age=" << j2.getInt("age")
		          << " city=" << j2.getString("address.city")
		          << std::endl;
	}
	std::cout << std::endl;
}

void example_from_sexpr() {
	std::cout << "=== S-Expression Input ===" << std::endl;
	
	asvJSON json;
	std::string_view input = "(items (\"apple\" \"banana\" \"cherry\") count 3)";
	if (json.fromSexpr(input)) {
		std::cout << "Parsed S-Expression:" << std::endl;
		std::cout << "  items: ";
		auto* arr = json.getRoot()->getConst("items");
		if (arr) {
			std::cout << "[";
			for (size_t i = 0; i < arr->size(); i++) {
				if (i > 0) std::cout << ", ";
				std::cout << arr->get(static_cast<size_t>(i))->getString();
			}
			std::cout << "]" << std::endl;
		}
		std::cout << "  count: " << json.getInt("count") << std::endl;
	}
	std::cout << std::endl;
}

void example_to_json5() {
	std::cout << "=== JSON5 Output ===" << std::endl;
	
	// Basic types - unquoted keys
	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"active":true,"address":{"city":"NYC","zip":10001}})"));
	std::string j5 = json.toJSON5();
	std::cout << "JSON5 (unquoted keys):" << std::endl << j5 << std::endl << std::endl;
	
	// Round-trip
	asvJSON j2;
	if (j2.fromJSON5(std::string_view(j5))) {
		std::cout << "Round-trip: name=" << j2.getString("name")
		          << " age=" << j2.getInt("age")
		          << " city=" << j2.getString("address.city")
		          << std::endl;
	}
	
	// Special types - Extended JSON format
	asvJSON j3;
	uint8_t bin[] = {0xde, 0xad, 0xbe, 0xef};
	j3.putObjectId("oid", std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c", 12));
	j3.putRegex("rx", "^test$", "gi");
	j3.putBinary("bin", bin, 4);
	j3.putExtension("ext", 42, bin, 4);
	j3.putDateTime("dt", 1705314645);
	j3.putInt("ts", 1712345678);
	std::string j5special = j3.toJSON5();
	std::cout << "JSON5 with special types (Extended JSON):" << std::endl << j5special << std::endl << std::endl;
	
	// Round-trip special types
	asvJSON j4;
	if (j4.fromJSON5(std::string_view(j5special))) {
		std::cout << "Special types round-trip OK:";
		if (j4.getRoot()->get("oid") && j4.getRoot()->get("oid")->type == asvJSONValue::OBJECTID) std::cout << " oid";
		if (j4.getRoot()->get("rx") && j4.getRoot()->get("rx")->type == asvJSONValue::REGEX) std::cout << " rx";
		if (j4.getRoot()->get("bin") && j4.getRoot()->get("bin")->type == asvJSONValue::BINARY) std::cout << " bin";
		if (j4.getRoot()->get("ext") && j4.getRoot()->get("ext")->type == asvJSONValue::EXTENSION) std::cout << " ext";
		if (j4.getRoot()->get("dt") && j4.getRoot()->get("dt")->type == asvJSONValue::DATETIME) std::cout << " dt";
		std::cout << std::endl;
	}
	std::cout << std::endl;
}

void example_from_json5() {
	std::cout << "=== JSON5 Input ===" << std::endl;
	
	// JSON5 with single quotes, unquoted keys, hex, trailing comma
	asvJSON json;
	std::string_view input = "{name:'Alice',age:0x1E,active:true,items:[1,2,3,],}";
	if (json.fromJSON5(input)) {
		std::cout << "Parsed JSON5:" << std::endl;
		std::cout << "  name=" << json.getString("name")
		          << " age=" << json.getInt("age")
		          << " active=" << json.getBool("active")
		          << " items[0]=" << json.getRoot()->getConst("items")->get(static_cast<size_t>(0))->getInt()
		          << std::endl;
	}
	std::cout << std::endl;
}

void example_to_ini() {
	std::cout << "=== INI Output ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"active":true,"address":{"city":"NYC","zip":"10001"},"network":{"server":{"ip":"10.0.0.1"},"client":{"retries":"3"}}})"));
	std::cout << json.toINI() << std::endl;
}

void example_from_ini() {
	std::cout << "=== INI Input ===" << std::endl;

	asvJSON json;
	std::string_view input = "; Sample config\nname = John\nage = 30\n\n[address]\ncity = NYC\nzip = 10001\n\n[network.server]\nip = 10.0.0.1\n\n[network.client]\nretries = 3\n";
	if (json.fromINI(input)) {
		std::cout << "  name=" << json.getString("name")
		          << " age=" << json.getString("age")
		          << " city=" << json.getString("address.city")
		          << " ip=" << json.getString("network.server.ip")
		          << " retries=" << json.getString("network.client.retries")
		          << std::endl;
	}
	std::cout << std::endl;
}

void example_to_ude() {
	std::cout << "=== UDE Output ===" << std::endl;

	asvJSON json;
	json.parse(std::string(R"({"name":"John","age":30,"active":true,"address":{"city":"NYC","zip":"10001"},"tags":["admin","user"]})"));
	std::cout << json.toUDE() << std::endl;
}

void example_from_ude() {
	std::cout << "=== UDE Input ===" << std::endl;

	asvJSON json;
	std::string_view input = "# UDE v1.0\nname: John\nage: 30\nactive: true\ntags: [admin, user]\n";
	if (json.fromUDE(input)) {
		std::cout << "  name=" << json.getString("name")
		          << " age=" << json.getInt("age")
		          << " active=" << json.getBool("active")
		          << " tags[0]=" << json.getRoot()->getConst("tags")->get(static_cast<size_t>(0))->getString()
		          << std::endl;
	}
	std::cout << std::endl;
}

int main() {
	std::cout << "========================================" << std::endl;
	std::cout << "   asvJSON++ C++17 Examples" << std::endl;
	std::cout << "========================================" << std::endl << std::endl;
	
	example_basic();
	example_string_view_key();
	example_string_view_zero_copy();
	example_optional_getters();
	example_binary_data();
	example_chunked_binary();
	example_datetime();
	example_datetime_with_ms();
	example_arrays();
	example_nested_access();
	example_object_operations();
	example_base64_custom_charset();
	example_file_io();
	example_comments();
	example_pretty_print();
	example_get_value();
	example_clear();
	example_get_root();
	example_regex();
	example_messagepack();
	example_bson();
	example_cbor();
	example_json_pointer();
	example_json_patch();
	example_clone();
	example_get_array();
	example_array_add();
	example_array_add_datetime();
	example_opt_datetime();
	example_type_to_string();
	example_merge();
	example_date_time_string();
	example_string_overloads();
	example_put_float32();
	example_nan_infinity();
	example_type_checks();
	example_get_special_types();
	example_extension();
	example_nested_special_types();
	example_static_msgpack_convert();
	example_msgpack_bson_string();
	example_valid_utf8();
	example_root_array();
	example_get_const();
	example_copy_move();
	example_remove_by_pointer();
	example_opt_datetime_tm_default();
	example_to_xml();
	example_from_xml();
	example_xml_escaped_keys();
	example_to_yaml();
	example_to_yaml_multiline();
	example_from_yaml();
	example_to_csv();
	example_to_csv_array();
	example_from_csv();
	example_to_toon();
	example_to_tron();
	example_to_goon();
	example_protobuf();
	example_to_toml();
	example_json_lines();
	example_to_sexpr();
	example_from_sexpr();
	example_to_json5();
	example_from_json5();
	example_to_ini();
	example_from_ini();
	example_to_ude();
	example_from_ude();
	
	std::cout << "========================================" << std::endl;
	std::cout << "   All examples completed!" << std::endl;
	std::cout << "========================================" << std::endl;
	
	return 0;
}