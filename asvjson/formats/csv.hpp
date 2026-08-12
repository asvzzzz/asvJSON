#pragma once
// CSV serialization/parsing for asvJSON++

#include "../core.hpp"

namespace asvJSONInternal {

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



/**
 * @brief Split CSV line into fields respecting RFC 4180 quotes and escapes
 */
inline std::vector<std::string> csvSplitLine(std::string_view line) {
	std::vector<std::string> fields;
	std::string cur;
	bool inQuotes = false;
	for (size_t i = 0; i < line.size(); i++) {
		char c = line[i];
		if (!inQuotes && c == '"') { inQuotes = true; continue; }
		if (inQuotes && c == '"') {
			if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; continue; }
			inQuotes = false; continue;
		}
		if (!inQuotes && c == ',') { fields.push_back(std::move(cur)); cur.clear(); continue; }
		cur += c;
	}
	fields.push_back(std::move(cur));
	return fields;
}

/**
 * @brief Detect JSON value type from CSV cell string
 */
inline std::unique_ptr<asvJSONValue> csvDetectType(std::string_view s) {
	if (s.empty() || s == "null" || s == "NULL" || s == "_") return asvJSONValue::makeNull();
	if (s == "true" || s == "TRUE" || s == "T") return asvJSONValue::makeBool(true);
	if (s == "false" || s == "FALSE" || s == "F") return asvJSONValue::makeBool(false);
	if (!s.empty() && (s[0] == '+' || s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) {
		// Skip a leading '+' (some from_chars implementations reject it) but keep
		// it for '-' which is universally accepted.
		const char* b = s.data() + (s[0] == '+' ? 1 : 0);
		long long v;
		auto [ptr, ec] = std::from_chars(b, s.data() + s.size(), v);
		if (ec == std::errc() && ptr == s.data() + s.size()) return asvJSONValue::makeInt(v);
		double d;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
		auto [ptr2, ec2] = std::from_chars(b, s.data() + s.size(), d);
		if (ec2 == std::errc() && ptr2 == s.data() + s.size()) return asvJSONValue::makeDouble(d);
#else
		char* end; errno = 0;
		d = std::strtod(s.data(), &end);
		if (errno != ERANGE && end == s.data() + s.size()) return asvJSONValue::makeDouble(d);
#endif
	}
	return asvJSONValue::makeString(s.data(), s.size());
}

inline bool asvJSON::fromCSV(std::string_view input) {
	try {
		if (input.empty()) throw asvJSONError("empty input");

		// Split into lines, respecting quoted multi-line fields (RFC 4180)
		std::vector<std::string_view> lines;
		size_t start = 0;
		bool inQuotes = false;
		for (size_t i = 0; i <= input.size(); i++) {
			if (i == input.size()) {
				if (!inQuotes) {
					size_t end = (i > start && input[i - 1] == '\r') ? (i - 1) : i;
					if (end > start) lines.push_back(input.substr(start, end - start));
				}
				break;
			}
			if (input[i] == '"') {
				if (i + 1 < input.size() && input[i + 1] == '"') { i++; continue; }
				inQuotes = !inQuotes;
			} else if (!inQuotes && input[i] == '\n') {
				size_t end = (i > start && input[i - 1] == '\r') ? (i - 1) : i;
				if (end > start) lines.push_back(input.substr(start, end - start));
				start = i + 1;
			}
		}
		if (inQuotes) throw asvJSONError("unclosed quote in CSV");

		if (lines.empty()) throw asvJSONError("empty CSV data");

		std::vector<std::string> headers = csvSplitLine(lines[0]);
		if (headers.empty()) throw asvJSONError("empty CSV header");

		auto arr = asvJSONValue::makeArray();
		if (!arr) throw asvJSONError("out of memory");

		for (size_t li = 1; li < lines.size(); li++) {
			if (lines[li].empty()) continue;
			auto fields = csvSplitLine(lines[li]);
			auto obj = asvJSONValue::makeObject();
			if (!obj) throw asvJSONError("out of memory");
			for (size_t fi = 0; fi < headers.size(); fi++) {
				std::string val = fi < fields.size() ? std::string(fields[fi]) : std::string();
				std::string unescaped;
				if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
					for (size_t vi = 1; vi < val.size() - 1; vi++) {
						if (val[vi] == '"' && vi + 1 < val.size() - 1 && val[vi + 1] == '"') { unescaped += '"'; vi++; }
						else unescaped += val[vi];
					}
				} else {
					unescaped = std::move(val);
				}
				obj->obj->emplace(headers[fi], csvDetectType(unescaped));
			}
			arr->arr->push_back(std::move(obj));
		}

		if (arr->arr->empty()) throw asvJSONError("no data rows");

		root = std::move(arr);
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	}
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
				std::string s; fmtDoubleVal(v->dbl, s); return s;
			}
			case T::STRING: return v->str_data;
			case T::DATETIME: {
				std::string s; fmtDateTimeVal(v->timestamp, v->datetime_ms, s); return s;
			}
			case T::BINARY: return encodeBase64(v->bin_data.data(), v->bin_data.size());
			case T::OBJECTID: {
				std::string s; fmtObjectIdHexVal(v->str_data, s); return s;
			}
			case T::REGEX: {
				size_t sep = v->str_data.rfind('\0');
				return (sep != std::string_view::npos) ? std::string(v->str_data.data(), sep) : v->str_data;
			}
			case T::TIMESTAMP: return std::to_string(v->num);
			case T::EXTENSION: return encodeBase64(v->bin_data.data(), v->bin_data.size());
			case T::ARRAY:
			case T::OBJECT: {
				std::string s; v->serialize(s, false); return s;
			}
			default: return {};
		}
	};
	using T = asvJSONValue::Type;

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
			if (obj->size() == 1) {
				const auto& [onlyKey, onlyVal] = *obj->begin();
				if (onlyVal->type == T::ARRAY && onlyVal->arr && !onlyVal->arr->empty()) {
					bool allObjs = true;
					for (const auto& v : *onlyVal->arr)
						if (v->type != T::OBJECT) { allObjs = false; break; }
					if (allObjs) { writeArrayOfObjects(*onlyVal->arr); break; }
				}
			}
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

} // namespace asvJSONInternal
