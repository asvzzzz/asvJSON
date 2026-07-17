#pragma once
// YAML serialization for asvJSON++

#include "../core.hpp"

inline void asvJSONValue::toYAML(std::string& out) const {
	out += "---\n";
	toYAML(out, 0, "", false);
}

inline void asvJSONValue::toYAML(std::string& out, int indent, const std::string& key, bool isArrayItem) const {
	using T = asvJSONValue::Type;
	auto prefix = [&]() -> std::string {
		if (isArrayItem && key.empty()) return std::string(indent * 2, ' ') + "- ";
		if (!key.empty()) return std::string(indent * 2, ' ') + yamlQuoteKey(key) + ": ";
		return "";
	};

	switch (type) {
		case T::NULL_VAL:
			out += prefix() + "~\n";
			break;
		case T::BOOL_VAL:
			out += prefix() + (flag ? "true" : "false") + "\n";
			break;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += prefix() + std::string(buf, ptr) + "\n"; }
			break;
		}
		case T::DOUBLE: {
			if (std::isnan(dbl) || std::isinf(dbl)) { out += prefix() + "~\n"; break; }
			std::string val; fmtDoubleVal(dbl, val);
			out += prefix() + val + "\n";
			break;
		}
		case T::STRING: {
			if (str_data.find('\n') != std::string_view::npos) {
				out += prefix() + "|\n";
				for (size_t i = 0; i < str_data.size(); ) {
					size_t eol = str_data.find('\n', i);
					if (eol == std::string_view::npos) eol = str_data.size();
					out += std::string((indent + 1) * 2, ' ') + std::string(str_data.data() + i, eol - i) + "\n";
					i = eol + 1;
				}
			} else if (yamlNeedsQuotes(str_data)) {
				out += prefix() + yamlQuote(str_data) + "\n";
			} else {
				out += prefix() + str_data + "\n";
			}
			break;
		}
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(timestamp, datetime_ms, dt);
			out += prefix() + dt + "\n";
			break;
		}
		case T::BINARY:
			out += prefix() + "!!binary " + encodeBase64(bin_data.data(), bin_data.size()) + "\n";
			break;
		case T::OBJECTID: {
			std::string hex; fmtObjectIdHexVal(str_data, hex);
			out += prefix() + "!objectid " + hex + "\n";
			break;
		}
		case T::REGEX: {
			std::string r; fmtRegexVal(str_data, r);
			out += prefix() + "!regex " + yamlQuote(r) + "\n";
			break;
		}
		case T::TIMESTAMP: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += prefix() + std::string(buf, ptr) + "\n"; }
			break;
		}
		case T::EXTENSION:
			out += prefix() + "!ext " + yamlQuote(encodeBase64(bin_data.data(), bin_data.size())) + "\n";
			break;
		case T::OBJECT: {
			if (!obj || obj->empty()) { out += prefix() + "{}\n"; break; }
			out += prefix() + "\n";
			for (const auto& [k, v] : *obj) v->toYAML(out, indent + 1, k, false);
			break;
		}
		case T::ARRAY: {
			if (!arr || arr->empty()) { out += prefix() + "[]\n"; break; }
			if (!isArrayItem && key.empty()) {
				// Root array
				out += "---\n";
				for (const auto& item : *arr) item->toYAML(out, 0, "", true);
				break;
			}
			out += prefix() + "\n";
			for (const auto& item : *arr) item->toYAML(out, indent, "", true);
			break;
		}
		default:
			out += prefix() + "~\n";
			break;
	}
}
