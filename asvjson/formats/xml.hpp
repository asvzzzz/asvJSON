#pragma once
// XML serialization for asvJSON++

#include "../core.hpp"

inline void asvJSONValue::toXML(std::string& out) const {
	toXML(out, "root", 0);
}

inline void asvJSONValue::toXML(std::string& out, const std::string& name, int indent) const {
	using T = asvJSONValue::Type;
	std::string pad(indent * 2, ' ');

	switch (type) {
		case T::NULL_VAL:
			out += pad + "<" + xmlSanitizeElementName(name) + "/>\n";
			break;
		case T::BOOL_VAL:
			out += pad + "<" + xmlSanitizeElementName(name) + ">" + (flag ? "true" : "false") + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += pad + "<" + xmlSanitizeElementName(name) + ">" + std::string(buf, ptr) + "</" + xmlSanitizeElementName(name) + ">\n"; }
			break;
		}
		case T::DOUBLE: {
			if (std::isnan(dbl) || std::isinf(dbl)) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			std::string val; fmtDoubleVal(dbl, val);
			out += pad + "<" + xmlSanitizeElementName(name) + ">" + xmlEscapeContent(val) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::STRING:
			out += pad + "<" + xmlSanitizeElementName(name) + ">" + xmlEscapeContent(str_data) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(timestamp, datetime_ms, dt);
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"datetime\">" + dt + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::BINARY: {
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"binary\">" + xmlEscapeContent(encodeBase64(bin_data.data(), bin_data.size())) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::OBJECTID: {
			std::string hex; fmtObjectIdHexVal(str_data, hex);
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"objectid\">" + hex + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::REGEX: {
			std::string r; fmtRegexVal(str_data, r);
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"regex\">" + xmlEscapeContent(r) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::TIMESTAMP: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num);
			if (ec == std::errc()) { out += pad + "<" + xmlSanitizeElementName(name) + " type=\"timestamp\">" + std::string(buf, ptr) + "</" + xmlSanitizeElementName(name) + ">\n"; }
			break;
		}
		case T::EXTENSION: {
			out += pad + "<" + xmlSanitizeElementName(name) + " type=\"extension\">" + xmlEscapeContent(encodeBase64(bin_data.data(), bin_data.size())) + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::OBJECT: {
			if (!obj) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			out += pad + "<" + xmlSanitizeElementName(name) + ">\n";
			for (const auto& [k, v] : *obj) v->toXML(out, k, indent + 1);
			out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		case T::ARRAY: {
			if (!arr) { out += pad + "<" + xmlSanitizeElementName(name) + "/>\n"; break; }
			out += pad + "<" + xmlSanitizeElementName(name) + ">\n";
			for (const auto& item : *arr) item->toXML(out, "item", indent + 1);
			out += pad + "</" + xmlSanitizeElementName(name) + ">\n";
			break;
		}
		default:
			out += pad + "<" + xmlSanitizeElementName(name) + "/>\n";
			break;
	}
}
