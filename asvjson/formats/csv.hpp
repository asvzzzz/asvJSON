#pragma once
// CSV serialization/parsing for asvJSON++

#include "../core.hpp"

#include <unordered_set>

namespace asvJSONInternal {

inline std::string csvEscape(std::string_view s, bool forceQuote = false, bool injectionTarget = true) {
	bool need = forceQuote;
	if (!need) for (auto c : s) if (c == ',' || c == '"' || c == '\n' || c == '\r') { need = true; break; }
	if (!need) {
		// Formula-injection guard: prefix leading =+-@ with a single quote so the
		// cell is treated as text by spreadsheets. A cell that already starts
		// with a guard apostrophe ("'=", "'+", "'-", "'@") must be quoted instead:
		// left bare, csvStripInjection would mistake it for a guarded cell and eat
		// the apostrophe on decode.
		if (injectionTarget && !s.empty() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@')) {
			return "'" + std::string(s);
		}
		if (injectionTarget && s.size() >= 2 && s[0] == '\'' &&
		    (s[1] == '=' || s[1] == '+' || s[1] == '-' || s[1] == '@')) {
			need = true;
		} else {
			return std::string(s);
		}
	}
	std::string out; out += '"';
	for (auto c : s) { if (c == '"') out += "\"\""; else out += c; }
	out += '"';
	return out;
}

// Strip the single-quote formula-injection guard added by csvEscape, restoring
// the original value on decode so toCSV->fromCSV round-trips losslessly.
inline std::string csvStripInjection(std::string_view s) {
	if (s.size() >= 2 && s[0] == '\'' && (s[1] == '=' || s[1] == '+' || s[1] == '-' || s[1] == '@'))
		return std::string(s.substr(1));
	return std::string(s);
}



// Split CSV line into fields respecting RFC 4180 quotes and escapes, reporting
// whether each field was quoted with "...". A quoted field is treated as an
// explicit text literal on decode (used for both data rows and the header row),
// which lets empty strings and numeric-looking strings round-trip as STRING.
inline std::vector<std::pair<std::string, bool>> csvSplitLineQ(std::string_view line) {
	std::vector<std::pair<std::string, bool>> fields;
	std::string cur;
	bool inQuotes = false;
	bool quoted = false;
	for (size_t i = 0; i < line.size(); i++) {
		char c = line[i];
		if (!inQuotes && c == '"') { inQuotes = true; quoted = true; continue; }
		if (inQuotes && c == '"') {
			if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; continue; }
			inQuotes = false; continue;
		}
		if (!inQuotes && c == ',') { fields.emplace_back(std::move(cur), quoted); cur.clear(); quoted = false; continue; }
		cur += c;
	}
	fields.emplace_back(std::move(cur), quoted);
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
		// JSON numbers never carry leading zeros ("042", "-042", "+042"), so
		// those literals must stay STRINGs (otherwise the value silently changes
		// to 42 on a re-parse). "0", "-0", "0.5" and "-0.5" are still numbers.
		size_t nfirst = (s[0] == '+' || s[0] == '-') ? 1 : 0;
		// "0x"/"0X" literals are C-style hex floats, not JSON numbers. strtod and
		// from_chars would happily parse "0x10" as 16.0, silently changing the
		// cell's form; JSON has no hex syntax, so keep the literal as text.
		if (s.size() > nfirst + 1 && s[nfirst] == '0' && (s[nfirst + 1] == 'x' || s[nfirst + 1] == 'X'))
			return asvJSONValue::makeString(s.data(), s.size());
		bool leadZero = s.size() > nfirst + 1 && s[nfirst] == '0' &&
		                s[nfirst + 1] >= '0' && s[nfirst + 1] <= '9';
		if (!leadZero) {
			// Skip a leading '+' (some from_chars implementations reject it) but keep
			// it for '-' which is universally accepted.
			const char* b = s.data() + (s[0] == '+' ? 1 : 0);
			long long v;
			auto [ptr, ec] = std::from_chars(b, s.data() + s.size(), v);
			if (ec == std::errc() && ptr == s.data() + s.size()) return asvJSONValue::makeInt(v);
			bool allDigits = (s.size() > 1);
			const char* p0 = (s[0] == '-') ? b + 1 : b;
			for (const char* p = p0; p < s.data() + s.size(); ++p)
				if (*p < '0' || *p > '9') { allDigits = false; break; }
			if (allDigits) {
				auto r = asvJSONValue::makeString(s.data(), s.size());
				if (r) r->raw_number = true;
				return r;
			}
			double d;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
			auto [ptr2, ec2] = std::from_chars(b, s.data() + s.size(), d);
			if (ec2 == std::errc() && ptr2 == s.data() + s.size()) return asvJSONValue::makeDouble(d);
#else
			std::string tmp(s);
			char* end; errno = 0;
			d = std::strtod(tmp.c_str(), &end);
			if (errno != ERANGE && end == tmp.c_str() + tmp.size()) return asvJSONValue::makeDouble(d);
#endif
		}
	}
	return asvJSONValue::makeString(s.data(), s.size());
}

inline bool asvJSON::fromCSV(std::string_view input) {
	dirty_ = false;
	formatType = FormatType::Unknown;
	try {
		if (input.empty()) throw asvJSONError("empty input");

		// Strip UTF-8 BOM (EF BB BF) if present (mirrors the JSON parser).
		if (input.size() >= 3 && (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF)
			input = input.substr(3);

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

		auto rawHeaders = csvSplitLineQ(lines[0]);
		if (rawHeaders.empty()) throw asvJSONError("empty CSV header");
		std::vector<std::string> headers;
		headers.reserve(rawHeaders.size());
		// A quoted header is an explicit text literal appended verbatim; only an
		// unquoted header gets the formula-injection guard stripped (a header
		// like "'@x" that toCSV quoted on purpose must not lose its apostrophe).
		for (auto& [h, hq] : rawHeaders)
			headers.push_back(hq ? std::move(h) : csvStripInjection(h));

		// Duplicate column headers make the layout ambiguous; reject the input
		// instead of silently keeping one column's data and dropping the other's.
		{
			std::unordered_set<std::string> seen;
			for (const auto& h : headers)
				if (!seen.insert(h).second) throw asvJSONError("CSV: duplicate column header \"" + h + "\"");
		}

		auto arr = asvJSONValue::makeArray();
		if (!arr) throw asvJSONError("out of memory");

		for (size_t li = 1; li < lines.size(); li++) {
			if (lines[li].empty()) continue;
			auto fields = csvSplitLineQ(lines[li]);
			// A row with more fields than the header must not silently drop the
			// extras. Trailing empty fields (from a trailing comma) are tolerated.
			if (fields.size() > headers.size()) {
				bool extraData = false;
				for (size_t k = headers.size(); k < fields.size(); k++)
					if (!fields[k].first.empty()) { extraData = true; break; }
				if (extraData)
					throw asvJSONError("CSV: row " + std::to_string(li) + " has " + std::to_string(fields.size()) +
					                   " fields, expected " + std::to_string(headers.size()));
			}
			auto obj = asvJSONValue::makeObject();
			if (!obj) throw asvJSONError("out of memory");
			for (size_t fi = 0; fi < headers.size(); fi++) {
				bool quoted = fi < fields.size() && fields[fi].second;
				std::string text = fi < fields.size() ? std::move(fields[fi].first) : std::string();
				std::unique_ptr<asvJSONValue> val;
				if (quoted) {
					// RFC 4180 quoted field is an explicit text literal: preserve
					// STRING even when empty or numeric-looking.
					val = asvJSONValue::makeString(text.data(), text.size());
				} else {
					// Bare field: strip the formula-injection guard then detect type.
					text = csvStripInjection(text);
					val = csvDetectType(text);
				}
				obj->obj->emplace(headers[fi], std::move(val));
			}
			arr->arr->push_back(std::move(obj));
		}

		if (arr->arr->empty()) throw asvJSONError("no data rows");

		root = std::move(arr);
		formatType = FormatType::CSV;
		return true;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		root = nullptr;
		return false;
	}
}

inline void asvJSONValue::toCSV(std::string& out) const {
	using T = asvJSONValue::Type;

	// Unescaped textual representation of a value cell, without quoting.
	auto cellValue = [](const asvJSONValue* v) -> std::string {
		if (!v) return {};
		using T2 = asvJSONValue::Type;
		switch (v->type) {
			case T2::NULL_VAL: return "null"; // bare literal so an all-null row survives (not dropped as an empty line)
			case T2::BOOL_VAL: return v->flag ? "true" : "false";
			case T2::INT: return std::to_string(v->num);
			case T2::DOUBLE: {
				if (std::isnan(v->dbl) || std::isinf(v->dbl)) return {};
				std::string s; fmtDoubleVal(v->dbl, s);
				// An integral double must round-trip as a float, otherwise
				// "1" would re-decode as an INT (mirror the UDE/INI convention).
				if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
				    s.find('E') == std::string::npos)
					s += ".0";
				return s;
			}
			case T2::STRING: return v->str_data;
			case T2::DATETIME: {
				std::string s; fmtDateTimeVal(v->timestamp, v->datetime_ms, s); return s;
			}
			case T2::BINARY: return encodeBase64(v->bin_data.data(), v->bin_data.size());
			case T2::OBJECTID: {
				std::string s; fmtObjectIdHexVal(v->str_data, s); return s;
			}
			case T2::REGEX: {
				size_t sep = v->str_data.rfind('\0');
				return (sep != std::string::npos) ? std::string(v->str_data.data(), sep) : v->str_data;
			}
			case T2::TIMESTAMP: return std::to_string(v->num);
			case T2::EXTENSION: return encodeBase64(v->bin_data.data(), v->bin_data.size());
			case T2::ARRAY:
			case T2::OBJECT: {
				std::string s; v->serialize(s, false); return s;
			}
			default: return {};
		}
	};

	// Fully-escaped cell. A STRING whose text would decode as a different type
	// (number/bool/null/empty) is quoted so the round-trip preserves STRING.
	// The non-native types (REGEX/BINARY/EXTENSION/OBJECTID/DATETIME) have no
	// CSV cell syntax: their text is emitted bare except when it would be mis-read
	// as a number/bool/null on decode, in which case it is quoted as an explicit
	// text literal (a REGEX pattern "123" must not come back as INT 123). TIMESTAMP
	// stays bare -- an epoch number is its natural CSV form -- and ARRAY/OBJECT
	// cells use their JSON text (already detected as STRING on decode). The
	// formula-injection guard applies to STRING and the special text types, and
	// to headers.
	auto csvCell = [&](const asvJSONValue* v) -> std::string {
		std::string s = cellValue(v);
		bool textGuard = false;
		bool quote = false;
		if (v) {
			auto d = csvDetectType(s);
			switch (v->type) {
				case T::STRING:
				case T::REGEX: case T::BINARY: case T::EXTENSION: case T::OBJECTID: case T::DATETIME:
					textGuard = true;
					quote = !d || d->type != T::STRING;
					break;
				default: break;
			}
		}
		return csvEscape(s, quote, textGuard);
	};

	auto writeArrayOfObjects = [&](const std::vector<std::unique_ptr<asvJSONValue>>& items) {
		if (items.empty()) return;
		std::map<std::string, bool> keyMap;
		std::unordered_set<const asvJSONValue*> visited;
		auto collect = [&](auto&& self, const asvJSONValue* v, const std::string& prefix,
						   std::unordered_set<const asvJSONValue*>& vis) -> void {
			if (!v) return;
			if (vis.count(v)) return;
			vis.insert(v);
			if (v->type == T::OBJECT && v->obj && !v->obj->empty()) {
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
				if (v->type == T::OBJECT && v->obj && !v->obj->empty()) {
					for (const auto& [k, val] : *v->obj)
						self(self, val.get(), prefix.empty() ? k : prefix + "." + k, vis);
				} else {
					std::string key = prefix.empty() ? "value" : prefix;
					// Two different paths flattening onto one column would
					// silently drop one value; reject instead of losing data.
					if (!flat.emplace(key, csvCell(v)).second)
						throw asvJSONError("CSV: flattened key collision on \"" + key + "\"");
				}
			};
			fill(fill, item.get(), "", fillVisited);
			for (size_t i = 0; i < keys.size(); i++) {
				if (i > 0) out += ',';
				auto it = flat.find(keys[i]);
				out += (it != flat.end()) ? it->second : "";
			}
			out += '\n';
		}
	};

	switch (type) {
		case T::OBJECT: {
			if (!obj || obj->empty()) return;
			std::map<std::string, std::string> flat;
			std::unordered_set<const asvJSONValue*> flattenVisited;
			auto flatten = [&](auto&& self, const asvJSONValue* v, const std::string& prefix,
							   std::unordered_set<const asvJSONValue*>& vis) -> void {
				if (!v) return;
				if (vis.count(v)) return;
				vis.insert(v);
				if (v->type == T::OBJECT && v->obj && !v->obj->empty()) {
					for (const auto& [k, val] : *v->obj)
						self(self, val.get(), prefix.empty() ? k : prefix + "." + k, vis);
				} else {
					std::string key = prefix.empty() ? "value" : prefix;
					// Two different paths flattening onto one column would
					// silently drop one value; reject instead of losing data.
					if (!flat.emplace(key, csvCell(v)).second)
						throw asvJSONError("CSV: flattened key collision on \"" + key + "\"");
				}
			};
			flatten(flatten, this, "", flattenVisited);
			bool first = true;
			for (const auto& [k, _] : flat) { if (!first) out += ','; out += csvEscape(k); first = false; }
			out += '\n';
			first = true;
			for (const auto& [_, v] : flat) { if (!first) out += ','; out += v; first = false; }
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
				for (const auto& v : *arr) { out += csvCell(v.get()); out += '\n'; }
			}
			break;
		}
		default:
			out += csvCell(this); out += '\n';
			break;
	}
}

} // namespace asvJSONInternal
