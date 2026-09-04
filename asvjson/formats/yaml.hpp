#pragma once
// YAML serialization/parsing for asvJSON++

#include "../core.hpp"

namespace asvJSONInternal {

inline constexpr size_t MAX_YAML_ANCHORS = 100000;
inline constexpr size_t MAX_YAML_DEPTH = 64;

// Forward declarations for block/flow encoders
inline void yamlBlockVal(const asvJSONValue* v, std::string& out, int indent,
                         const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool wc);
inline void yamlFlowVal(const asvJSONValue* v, std::string& out, int indent,
                        const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool forceFlow);

// Options-aware quoting helpers
inline std::string yamlQuoteStr(std::string_view s, const YamlWriteOptions& o) {
	if (o.doubleQuotes) return yamlDQuote(s);
	// Single quotes cannot represent control characters safely (a literal tab,
	// CR or other control byte would be emitted raw and corrupt the document),
	// so fall back to double quotes which escape them.
	for (unsigned char c : s)
		if (c < 0x20) return yamlDQuote(s);
	return yamlQuote(s);
}

// Trailing comment suffix for a value line ("" when the node carries none).
inline std::string yamlTrail(const asvJSONValue* v) {
	if (v->yaml_comments && !v->yaml_comments->trailing.empty())
		return " " + v->yaml_comments->trailing;
	return std::string();
}

inline bool yamlNeedQuoteOpt(std::string_view s, const YamlWriteOptions& o) {
	if (o.alwaysQuoteStrings) return true;
	return yamlNeedsQuotes(s);
}

inline std::string yamlKeyOpt(const std::string& k, const YamlWriteOptions& o) {
	if (o.alwaysQuoteKeys) return yamlQuoteStr(k, o);
	if (o.doubleQuotes && yamlNeedsQuotes(k)) return yamlDQuote(k);
	return yamlQuoteKey(k);
}

inline int yamlIndentWidth(const YamlWriteOptions& o) {
	return o.indentWidth >= 1 ? o.indentWidth : 1;
}

inline void asvJSONValue::toYAML(std::string& out, bool pretty) const {
	toYAML(out, 0, "", false, pretty);
}

inline void asvJSONValue::toYAML(std::string& out, const YamlWriteOptions& opts) const {
	switch (opts.style) {
		case YamlWriteOptions::Style::Auto: yamlFlowVal(this, out, 0, "", false, opts, false); break;
		case YamlWriteOptions::Style::Flow: yamlFlowVal(this, out, 0, "", false, opts, true); break;
		default: yamlBlockVal(this, out, 0, "", false, opts, true);
	}
}

inline void asvJSONValue::toYAML(std::string& out, int indent, const std::string& key, bool isArrayItem, bool pretty) const {
	YamlWriteOptions o;
	if (!pretty) o.style = YamlWriteOptions::Style::Auto;
	switch (o.style) {
		case YamlWriteOptions::Style::Auto: yamlFlowVal(this, out, indent, key, isArrayItem, o, false); break;
		case YamlWriteOptions::Style::Flow: yamlFlowVal(this, out, indent, key, isArrayItem, o, true); break;
		default: yamlBlockVal(this, out, indent, key, isArrayItem, o, true);
	}
}

inline std::string asvJSON::toYAML(const YamlWriteOptions& opts) const {
	std::string out;
	if (root) {
		if (opts.style == YamlWriteOptions::Style::Block && root->type != asvJSONValue::ARRAY) {
			out += "---\n";
		}
		root->toYAML(out, opts);
		if (opts.style == YamlWriteOptions::Style::Block && root->yaml_comments)
			for (const auto& cl : root->yaml_comments->footing) out += cl + "\n";
	}
	return out;
}

// Try to emit value inline (for flow mode). Returns true if successful.
inline bool yamlTryFlowVal(const asvJSONValue* v, std::string& out, const YamlWriteOptions& o, bool forceFlow) {
	using T = asvJSONValue::Type;
	switch (v->type) {
		case T::NULL_VAL: out += "~"; return true;
		case T::BOOL_VAL: out += (v->flag ? "true" : "false"); return true;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) { out.append(buf, ptr); return true; }
			return false;
		}
		case T::DOUBLE: {
			if (std::isnan(v->dbl)) { out += ".nan"; return true; }
			if (std::isinf(v->dbl)) { out += (v->dbl > 0 ? ".inf" : "-.inf"); return true; }
			std::string val; fmtDoubleVal(v->dbl, val);
			out += val; return true;
		}
		case T::STRING: {
			if (v->str_data.find('\n') != std::string_view::npos) {
				if (!forceFlow) return false;
				out += yamlDQuote(v->str_data);
				return true;
			}
			if (yamlNeedQuoteOpt(v->str_data, o)) out += yamlQuoteStr(v->str_data, o);
			else out += v->str_data;
			return true;
		}
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(v->timestamp, v->datetime_ms, dt);
			out += dt; return true;
		}
		case T::BINARY: {
			out += "!!binary " + encodeBase64(v->bin_data.data(), v->bin_data.size());
			return true;
		}
		case T::OBJECTID: {
			std::string hex; fmtObjectIdHexVal(v->str_data, hex);
			out += "!objectid " + hex; return true;
		}
		case T::REGEX: {
			std::string r; fmtRegexVal(v->str_data, r);
			out += "!regex " + r; return true;
		}
		case T::TIMESTAMP: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) { out.append(buf, ptr); return true; }
			return false;
		}
		case T::EXTENSION:
			out += "!ext " + std::to_string(v->ext_type) + " " + yamlQuote(encodeBase64(v->bin_data.data(), v->bin_data.size()));
			return true;
		case T::CUSTOM_TAG: {
			// "!name <value>"; the value is kept inline (force-flow) so the
			// single-line tagged form reparses through yamlParseTaggedValue.
			std::string childOut;
			if (v->custom_value && yamlTryFlowVal(v->custom_value.get(), childOut, o, true)) {
				std::string nm(v->str_data);
				if (!nm.empty() && nm[0] == '!') nm = nm.substr(1);
				out += "!" + nm + " " + childOut;
				return true;
			}
			return false;
		}
		case T::OBJECT: {
			if (v->yaml_set) {
				// Set flow form: "!!set {a, b, c}".
				if (!v->obj || v->obj->empty()) { out += "!!set {}"; return true; }
				std::string inner = "{";
				bool sfirst = true;
				for (const auto& [k, val] : *v->obj) {
					if (!sfirst) inner += ", ";
					sfirst = false;
					inner += yamlKeyOpt(k, o);
				}
				inner += "}";
				if (!forceFlow && inner.size() > static_cast<size_t>(o.flowMaxLen)) return false;
				out += "!!set " + inner;
				return true;
			}
			if (!v->obj || v->obj->empty()) { out += "{}"; return true; }
			std::string inner = "{";
			bool first = true;
			for (const auto& [k, val] : *v->obj) {
				std::string childOut;
				if (!yamlTryFlowVal(val.get(), childOut, o, forceFlow)) return false;
				if (!first) inner += ", ";
				first = false;
				inner += yamlKeyOpt(k, o) + ": " + childOut;
			}
			inner += "}";
			if (!forceFlow && inner.size() > static_cast<size_t>(o.flowMaxLen)) return false;
			out += inner; return true;
		}
		case T::ARRAY: {
			if (!v->arr || v->arr->empty()) { out += "[]"; return true; }
			std::string inner = "[";
			bool first = true;
			for (const auto& item : *v->arr) {
				std::string childOut;
				if (!yamlTryFlowVal(item.get(), childOut, o, forceFlow)) return false;
				if (!first) inner += ", ";
				first = false;
				inner += childOut;
			}
			inner += "]";
			if (!forceFlow && inner.size() > static_cast<size_t>(o.flowMaxLen)) return false;
			out += inner; return true;
		}
		default: out += "~"; return true;
	}
}

// Emit a value in block style at the given indent.
inline void yamlBlockVal(const asvJSONValue* v, std::string& out, int indent,
                         const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool wc) {
	using T = asvJSONValue::Type;
	const int iw = yamlIndentWidth(o);
	auto prefix = [&]() -> std::string {
		if (isArrayItem && key.empty()) return std::string(indent * iw, ' ') + "- ";
		if (!key.empty() || !isArrayItem) return std::string(indent * iw, ' ') + yamlKeyOpt(key, o) + ": ";
		return "";
	};

	if (wc && v->yaml_comments && !v->yaml_comments->leading.empty()) {
		for (const auto& cl : v->yaml_comments->leading)
			out += std::string(indent * iw, ' ') + cl + "\n";
	}

	switch (v->type) {
		case T::NULL_VAL:
			out += prefix() + "~" + yamlTrail(v) + "\n"; break;
		case T::BOOL_VAL:
			out += prefix() + (v->flag ? "true" : "false") + yamlTrail(v) + "\n"; break;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) out += prefix() + std::string(buf, ptr) + yamlTrail(v) + "\n";
			break;
		}
		case T::DOUBLE: {
			if (std::isnan(v->dbl)) { out += prefix() + ".nan" + yamlTrail(v) + "\n"; break; }
			if (std::isinf(v->dbl)) { out += prefix() + (v->dbl > 0 ? ".inf" : "-.inf") + yamlTrail(v) + "\n"; break; }
			std::string val; fmtDoubleVal(v->dbl, val);
			out += prefix() + val + yamlTrail(v) + "\n"; break;
		}
		case T::STRING: {
			if (v->str_data.find('\n') != std::string_view::npos && !v->str_data.empty() && v->str_data[0] != '\n') {
				size_t trailing = 0;
				while (trailing < v->str_data.size() && v->str_data[v->str_data.size() - 1 - trailing] == '\n') trailing++;
				std::string ind;
				if (trailing == 0) ind = "|-";
				else if (trailing == 1) ind = "|";
				else ind = "|+";
				// Content lines that begin (beyond the block indent) with white
				// space cannot round-trip as plain block scalars: the block indent
				// is taken from the first content line, so leading spaces are
				// ambiguous. Use an explicit indent indicator when lines carry
				// extra leading spaces; space-only or tab-leading lines (which
				// would be misread as blank or rejected as indentation) fall back
				// to a quoted string.
				bool hasLeadSpace = false, needQuote = false;
				for (size_t i = 0; i < v->str_data.size(); ) {
					size_t eol = v->str_data.find('\n', i);
					if (eol == std::string_view::npos) eol = v->str_data.size();
					std::string_view frag(v->str_data.data() + i, eol - i);
					size_t j = 0;
					while (j < frag.size() && (frag[j] == ' ' || frag[j] == '\t')) j++;
					if (j > 0 && j < frag.size() && frag[0] == ' ') hasLeadSpace = true;
					if (j > 0 && j == frag.size()) needQuote = true;          // space-only line
					if (j > 0 && frag[0] == '\t') needQuote = true;           // tab in indent margin
					i = eol + 1;
				}
				if (!needQuote && hasLeadSpace) {
					int nb = (indent + 1) * iw;
					if (nb < 1 || nb > 9) needQuote = true; // no single-digit explicit indent
					else ind = ind.substr(0, 1) + char('0' + nb) + ind.substr(1);
				}
				if (needQuote) { out += prefix() + yamlDQuote(v->str_data) + yamlTrail(v) + "\n"; break; }
				out += prefix() + ind + yamlTrail(v) + "\n";
				for (size_t i = 0; i < v->str_data.size(); ) {
					size_t eol = v->str_data.find('\n', i);
					if (eol == std::string_view::npos) eol = v->str_data.size();
					out += std::string((indent + 1) * iw, ' ') + std::string(v->str_data.data() + i, eol - i) + "\n";
					i = eol + 1;
				}
			} else if (v->str_data.find('\n') != std::string_view::npos) {
				out += prefix() + yamlDQuote(v->str_data) + yamlTrail(v) + "\n";
			} else if (yamlNeedQuoteOpt(v->str_data, o)) {
				out += prefix() + yamlQuoteStr(v->str_data, o) + yamlTrail(v) + "\n";
			} else {
				out += prefix() + v->str_data + yamlTrail(v) + "\n";
			}
			break;
		}
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(v->timestamp, v->datetime_ms, dt);
			out += prefix() + dt + yamlTrail(v) + "\n"; break;
		}
		case T::BINARY:
			out += prefix() + "!!binary " + encodeBase64(v->bin_data.data(), v->bin_data.size()) + yamlTrail(v) + "\n"; break;
		case T::OBJECTID: {
			std::string hex; fmtObjectIdHexVal(v->str_data, hex);
			out += prefix() + "!objectid " + hex + yamlTrail(v) + "\n"; break;
		}
		case T::REGEX: {
			std::string r; fmtRegexVal(v->str_data, r);
			out += prefix() + "!regex " + r + yamlTrail(v) + "\n"; break;
		}
		case T::TIMESTAMP: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) out += prefix() + std::string(buf, ptr) + yamlTrail(v) + "\n";
			break;
		}
		case T::EXTENSION:
			out += prefix() + "!ext " + std::to_string(v->ext_type) + " " + yamlQuote(encodeBase64(v->bin_data.data(), v->bin_data.size())) + yamlTrail(v) + "\n";
			break;
		case T::CUSTOM_TAG: {
			// "key: !name <value>"; the value stays inline (force-flow) so the
			// tagged form returns to a single line.
			std::string childOut;
			if (v->custom_value && yamlTryFlowVal(v->custom_value.get(), childOut, o, true)) {
				std::string nm(v->str_data);
				if (!nm.empty() && nm[0] == '!') nm = nm.substr(1);
				out += prefix() + "!" + nm + " " + childOut + yamlTrail(v) + "\n";
			} else {
				std::string nm(v->str_data);
				if (!nm.empty() && nm[0] == '!') nm = nm.substr(1);
				out += prefix() + "!" + nm + yamlTrail(v) + "\n";
			}
			break;
		}
		case T::OBJECT: {
			if (v->yaml_set) {
				// Block form of a YAML !!set:
				//   root / keyed:  key: !!set      seq item:  - !!set
				//                  - a                        - a
				if (!v->obj || v->obj->empty()) { out += prefix() + "!!set {}" + yamlTrail(v) + "\n"; break; }
				if (key.empty() && !isArrayItem) {
					out += "!!set\n";
					for (const auto& [k, val] : *v->obj) out += std::string(indent * iw, ' ') + "- " + yamlQuoteKey(k) + "\n";
				} else {
					out += prefix() + "!!set" + yamlTrail(v) + "\n";
					for (const auto& [k, val] : *v->obj) out += std::string((indent + 1) * iw, ' ') + "- " + yamlQuoteKey(k) + "\n";
				}
				break;
			}
			if (!v->obj || v->obj->empty()) { out += prefix() + "{}" + yamlTrail(v) + "\n"; break; }
			if (key.empty() && !isArrayItem) {
				for (const auto& [k, val] : *v->obj) yamlBlockVal(val.get(), out, indent, k, false, o, wc);
			} else {
				out += prefix() + yamlTrail(v) + "\n";
				for (const auto& [k, val] : *v->obj) yamlBlockVal(val.get(), out, indent + 1, k, false, o, wc);
			}
			break;
		}
		case T::ARRAY: {
			if (!v->arr || v->arr->empty()) { out += prefix() + "[]" + yamlTrail(v) + "\n"; break; }
			if (!isArrayItem && key.empty()) {
				out += "---\n";
				for (const auto& item : *v->arr) yamlBlockVal(item.get(), out, 0, "", true, o, wc);
				break;
			}
			out += prefix() + yamlTrail(v) + "\n";
			// A nested sequence used as a sequence item must indent its own
			// items one level deeper than its dash, otherwise the block parser
			// cannot tell parent items from child items ("m: [[1],[2]]").
			int childIndent = isArrayItem ? indent + 1 : indent;
			for (const auto& item : *v->arr) yamlBlockVal(item.get(), out, childIndent, "", true, o, wc);
			break;
		}
		default:
			out += prefix() + "~" + yamlTrail(v) + "\n"; break;
	}
}

// Try flow mode: emit objects/arrays inline when they fit, fall back to block.
inline void yamlFlowVal(const asvJSONValue* v, std::string& out, int indent,
                        const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool forceFlow) {
	using T = asvJSONValue::Type;
	const int iw = yamlIndentWidth(o);
	auto prefix = [&]() -> std::string {
		if (isArrayItem && key.empty()) return std::string(indent * iw, ' ') + "- ";
		if (!key.empty()) return std::string(indent * iw, ' ') + yamlKeyOpt(key, o) + ": ";
		return "";
	};

	if (v->type == T::OBJECT && v->yaml_set) {
		std::string inner;
		yamlTryFlowVal(v, inner, o, true);
		if (key.empty() && !isArrayItem) out += inner + "\n";
		else out += prefix() + inner + "\n";
		return;
	}
	if (v->type == T::OBJECT && v->obj && !v->obj->empty()) {
		std::string trial = prefix();
		std::string inner;
		if (yamlTryFlowVal(v, inner, o, forceFlow)) {
			if (key.empty() && !isArrayItem) out += inner + "\n";
			else out += trial + inner + "\n";
		} else {
			if (key.empty() && !isArrayItem) {
				for (const auto& [k, val] : *v->obj) yamlFlowVal(val.get(), out, indent, k, false, o, forceFlow);
			} else {
				out += prefix() + "\n";
				for (const auto& [k, val] : *v->obj) yamlFlowVal(val.get(), out, indent + 1, k, false, o, forceFlow);
			}
		}
	} else if (v->type == T::ARRAY && v->arr && !v->arr->empty()) {
		std::string trial = prefix();
		std::string inner;
		if (yamlTryFlowVal(v, inner, o, forceFlow)) {
			if (key.empty() && !isArrayItem) out += inner + "\n";
			else out += trial + inner + "\n";
		} else {
			out += prefix() + "\n";
			for (const auto& item : *v->arr) yamlFlowVal(item.get(), out, indent, "", true, o, forceFlow);
		}
	} else {
		yamlBlockVal(v, out, indent, key, isArrayItem, o, false);
	}
}

// ==================== YAML Decoder (YAML -> JSON -> parse) ====================


// Unescape YAML double-quoted string (\n, \t, \\, \", \xNN, \uNNNN)
inline std::string yamlUnescapeDouble(std::string_view s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			switch (s[++i]) {
				case 'n': out += '\n'; break;
				case 't': out += '\t'; break;
				case 'r': out += '\r'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case '\\': out += '\\'; break;
				case '"': out += '"'; break;
				case '/': out += '/'; break;
				case '0': out += '\0'; break;
				case 'x': {
					if (i + 2 >= s.size()) break;
					auto hex = [&](size_t off) -> int {
						char c = s[i + off];
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						return 0;
					};
					unsigned char val = static_cast<unsigned char>((hex(1) << 4) | hex(2));
					out += static_cast<char>(val);
					i += 2;
					break;
				}
				case 'u': {
					if (i + 4 >= s.size()) break;
					auto hex = [&](size_t off) -> int {
						char c = s[i + off];
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						return -1;
					};
					unsigned int cp = 0;
					for (int j = 0; j < 4; j++) { int h = hex(1 + j); if (h < 0) break; cp = (cp << 4) | static_cast<unsigned int>(h); }
					if (cp >= 0xD800 && cp <= 0xDBFF && i + 10 <= s.size() && s[i + 5] == '\\' && s[i + 6] == 'u') {
						unsigned int lo = 0;
						for (int j = 0; j < 4; j++) { int h = hex(6 + j); if (h < 0) break; lo = (lo << 4) | static_cast<unsigned int>(h); }
						if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
						else { i += 4; }
					} else { i += 4; }
					if (cp < 0x80) { out += static_cast<char>(cp); }
					else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
					break;
				}
				default: out += s[i]; break;
			}
		} else {
			out += s[i];
		}
	}
	return out;
}

// Unescape YAML single-quoted string ('' -> ')
inline std::string yamlUnescapeSingle(std::string_view s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') {
			out += '\''; i++;
		} else {
			out += s[i];
		}
	}
	return out;
}

// Remove trailing comment from a line (respecting quotes and escapes)
inline std::string_view yamlStripComment(std::string_view s) {
	bool inSQ = false, inDQ = false;
	for (size_t i = 0; i < s.size(); i++) {
		if (inDQ) {
			if (s[i] == '\\' && i + 1 < s.size()) { i++; continue; }
			if (s[i] == '"') inDQ = false;
		} else if (inSQ) {
			if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { i++; continue; }
			if (s[i] == '\'') inSQ = false;
		} else {
			if (s[i] == '#') return s.substr(0, i);
			if (s[i] == '"') inDQ = true;
			if (s[i] == '\'') inSQ = true;
		}
	}
	return s;
}

// Convert YAML scalar to JSON (auto-detect type)
inline std::string yamlScalarToJson(std::string_view s) {
	if (s.empty() || s == "~" || s == "null" || s == "NULL" || s == "Null") return "null";
	if (s == "true" || s == "TRUE" || s == "True" || s == "yes" || s == "YES" || s == "Yes" || s == "on" || s == "ON" || s == "On")
		return "true";
	if (s == "false" || s == "FALSE" || s == "False" || s == "no" || s == "NO" || s == "No" || s == "off" || s == "OFF" || s == "Off")
		return "false";
	if (s == "NaN" || s == ".NaN" || s == ".nan" || s == ".NAN") return "NaN";
	if (s == "Infinity" || s == ".Inf" || s == ".inf" || s == ".INF") return "Infinity";
	if (s == "-Infinity" || s == "-.Inf" || s == "-.inf" || s == "-.INF") return "-Infinity";
	// Validate that the string is a valid number (not a YAML plain scalar that looks like a number)
	{
		bool validNumber = false;
	#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
		double d;
		auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), d);
		validNumber = (ec == std::errc() && ptr == s.data() + s.size() && s.size() > 0);
	#else
		if (s.size() > 0) {
			char* end = nullptr;
			std::string tmp(s);
			std::strtod(tmp.c_str(), &end);
			validNumber = (end == tmp.c_str() + tmp.size());
		}
	#endif
		if (validNumber) {
			// JSON rejects leading-zero integers ("007", "01e2") even though
			// the YAML 1.2 spec allows them as plain scalars. Re-emit a
			// normalized JSON-compatible number instead of a raw token.
	#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
			size_t sign = 0;
			if (!s.empty() && (s[0] == '+' || s[0] == '-')) sign = 1;
			if (sign < s.size() && s[sign] == '0' && sign + 1 < s.size() &&
			    s[sign + 1] >= '0' && s[sign + 1] <= '9') {
				long long iv = 0;
				auto [ip, ie] = std::from_chars(s.data(), s.data() + s.size(), iv);
				if (ie == std::errc() && ip == s.data() + s.size()) return std::to_string(iv);
				double dv = 0.0;
				auto [dp2, de2] = std::from_chars(s.data(), s.data() + s.size(), dv);
				if (de2 == std::errc() && dp2 == s.data() + s.size()) {
					std::string norm;
					fmtDoubleVal(dv, norm);
					return norm;
				}
			}
	#else
			size_t sign = 0;
			if (!s.empty() && (s[0] == '+' || s[0] == '-')) sign = 1;
			if (sign < s.size() && s[sign] == '0' && sign + 1 < s.size() &&
			    s[sign + 1] >= '0' && s[sign + 1] <= '9') {
				std::string tmp(s);
				char* end = nullptr;
				long long iv = std::strtoll(tmp.c_str(), &end, 10);
				if (end == tmp.c_str() + tmp.size()) return std::to_string(iv);
				double dv = std::strtod(tmp.c_str(), &end);
				if (end == tmp.c_str() + tmp.size()) {
					std::string norm;
					fmtDoubleVal(dv, norm);
					return norm;
				}
			}
	#endif
			return std::string(s);
		}
	}
	std::string out = "\"";
	for (auto c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
			         else out += c;
		}
	}
	out += "\"";
	return out;
}

inline std::string yamlParseInlineValue(std::string_view s, int depth = 0);

// Forward declaration for embedded "!!yaml ..." documents (nested parse)
inline std::string yamlToJson(std::string_view input);

// Forward declarations (definitions are later in this file)
inline std::string_view::size_type yamlFindKeyColonSep(std::string_view s);
inline std::string yamlQuoteJsonScalar(std::string_view s);
inline std::string yamlScalarDocValue(std::string_view raw, bool& isScalar);
inline std::string yamlProcessYamlEmbed(std::string raw);

// When `raw` is a whole YAML document consisting of plain/quoted scalar
// content (no mapping, sequence, or flow structure), reinterpret it as a
// single value -- a JSON fragment -- and report it via `isScalar`. A multi-line
// scalar is folded per YAML plain-scalar rules: a single line break between
// non-empty lines becomes a space, each blank line becomes a "\n". Otherwise
// `isScalar` is false and the caller must parse `raw` as a YAML document.
inline std::string yamlScalarDocValue(std::string_view raw, bool& isScalar) {
	isScalar = false;
	std::vector<std::string> lines; // trimmed content lines
	std::vector<int> gaps;          // blank/comment lines between consecutive content lines
	size_t start = 0;
	while (start < raw.size()) {
		size_t nl = raw.find('\n', start);
		std::string_view line = raw.substr(start, (nl == std::string_view::npos) ? raw.size() - start : nl - start);
		start = (nl == std::string_view::npos) ? raw.size() : nl + 1;
		size_t b = 0;
		while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) b++;
		size_t e = line.size();
		while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) e--;
		std::string_view t = line.substr(b, e - b);
		if (t.empty() || t[0] == '#') { if (!lines.empty()) gaps.back()++; continue; } // blank / comment line
		char f = t[0];
		if (f == '{' || f == '[' || f == '|' || f == '>' || f == '!' || f == '&' || f == '*') return std::string();
		if ((t.size() == 1 && (f == '-' || f == '?')) ||
		    (t.size() >= 2 && (f == '-' || f == '?') && (t[1] == ' ' || t[1] == '\t'))) return std::string();
		if (yamlFindKeyColonSep(t) != std::string_view::npos) return std::string(); // mapping
		lines.emplace_back(t);
		gaps.push_back(0);
	}
	if (lines.empty()) return std::string(); // empty / comment-only document
	isScalar = true;
	if (lines.size() == 1) return yamlParseInlineValue(lines[0]);
	std::string text;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i) {
			int g = gaps[i - 1];
			if (g == 0) text += ' ';
			else text.append(static_cast<size_t>(g), '\n');
		}
		text += lines[i];
	}
	return yamlQuoteJsonScalar(text);
}

// Shared entry for "!!yaml" embeds: a whole-scalar document becomes a value;
// anything else is parsed recursively as a YAML document, capped at
// MAX_YAML_DEPTH by a thread_local counter (concurrent fromYAML calls on
// other threads stay independent).
inline std::string yamlProcessYamlEmbed(std::string raw) {
	bool isScalar = false;
	std::string sv = yamlScalarDocValue(raw, isScalar);
	if (isScalar) return sv;
	static thread_local int yamlYamlDepth = 0;
	if (yamlYamlDepth >= MAX_YAML_DEPTH)
		throw asvJSONError("YAML: maximum !!yaml nesting depth exceeded");
	++yamlYamlDepth;
	std::string nested;
	try { nested = yamlToJson(raw); }
	catch (...) { --yamlYamlDepth; throw; }
	--yamlYamlDepth;
	return nested;
}

// Convert unescaped regex "pattern|opts" to internal pattern|opts
inline std::string yamlRegexToInternal(std::string_view s) {
	return std::string(s);
}

// Force a YAML value to be treated as a JSON string (for !!str tag)
inline std::string yamlForceStringVal(std::string_view val) {
	while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.remove_prefix(1);
	if (val.empty()) return "\"\"";
	// If already a properly quoted JSON string from yamlParseInlineValue, return as-is
	if (val.size() >= 2 && val[0] == '"' && val.back() == '"') return std::string(val);
	// Parse YAML quotes if present
	if (val.size() >= 2) {
		if (val[0] == '"') {
			size_t end = 1;
			while (end < val.size()) {
				if (val[end] == '\\') end += 2;
				else if (val[end] == '"') break;
				else end++;
			}
			if (end < val.size()) {
				std::string unesc = yamlUnescapeDouble(val.substr(1, end - 1));
				std::string out = "\"";
				for (auto c : unesc) {
					switch (c) {
						case '"': out += "\\\""; break;
						case '\\': out += "\\\\"; break;
						case '\n': out += "\\n"; break;
						case '\t': out += "\\t"; break;
						case '\r': out += "\\r"; break;
						default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
						         else out += c;
					}
				}
				out += "\"";
				return out;
			}
		} else if (val[0] == '\'') {
			size_t end = 1;
			for (; end < val.size(); end++) {
				if (val[end] == '\'' && end + 1 < val.size() && val[end + 1] == '\'') end++;
				else if (val[end] == '\'') break;
			}
			if (end < val.size()) {
				std::string unesc = yamlUnescapeSingle(val.substr(1, end - 1));
				std::string out = "\"";
				for (auto c : unesc) {
					if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c;
				}
				out += "\"";
				return out;
			}
		}
	}
	// Plain text: wrap in JSON string with escaping
	std::string out = "\"";
	for (auto c : val) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
			         else out += c;
		}
	}
	out += "\"";
	return out;
}

// Convert JSON array string ["a","b","c"] -> {"$set":["a","b","c"]} for !!set.
// Elements keep the same key text the object form used: quoted elements stay
// as-is (already JSON-escaped), unquoted elements are wrapped in quotes.
inline std::string yamlArrayToSetObject(std::string_view arr) {
	if (arr.size() < 2 || arr[0] != '[') return std::string(arr);
	std::string out = "{\"$set\":[";
	size_t i = 1;
	bool first = true;
	while (i < arr.size() && arr[i] != ']') {
		while (i < arr.size() && (arr[i] == ',' || arr[i] == ' ' || arr[i] == '\n' || arr[i] == '\t')) i++;
		if (i >= arr.size() || arr[i] == ']') break;
		if (!first) out += ",";
		first = false;
		size_t start = i;
		if (arr[i] == '"') {
			i++;
			while (i < arr.size() && !(arr[i] == '"' && arr[i - 1] != '\\')) { if (arr[i] == '\\') i++; i++; }
			if (i < arr.size()) i++;
		} else if (arr[i] == '{') {
			int d = 1; i++;
			while (i < arr.size() && d > 0) {
				if (arr[i] == '{') d++; else if (arr[i] == '}') d--;
				else if (arr[i] == '"') { i++; while (i < arr.size() && !(arr[i] == '"' && arr[i - 1] != '\\')) { if (arr[i] == '\\') i++; i++; } }
				i++;
			}
		} else if (arr[i] == '[') {
			int d = 1; i++;
			while (i < arr.size() && d > 0) {
				if (arr[i] == '[') d++; else if (arr[i] == ']') d--;
				else if (arr[i] == '"') { i++; while (i < arr.size() && !(arr[i] == '"' && arr[i - 1] != '\\')) { if (arr[i] == '\\') i++; i++; } }
				i++;
			}
		} else {
			while (i < arr.size() && arr[i] != ',' && arr[i] != ']' && arr[i] != ' ' && arr[i] != '\n' && arr[i] != '\t') i++;
		}
		if (start < i && arr[start] == '"') out.append(arr.data() + start, i - start);
		else { out += '"'; out.append(arr.data() + start, i - start); out += '"'; }
	}
	out += "]}";
	return out;
}

// Convert a JSON object of {key:null} members into the "$set" marker form
// {"$set":[...]}; return the input unchanged when any member has a non-null
// value (such an object is not a YAML set and is kept verbatim).
inline std::string yamlSetObjToMarked(std::string_view s) {
	if (s.size() < 2 || s[0] != '{') return std::string(s);
	std::string out = "{\"$set\":[";
	size_t i = 1;
	bool first = true;
	while (i < s.size() && s[i] != '}') {
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
		if (i >= s.size() || s[i] == '}') break;
		if (s[i] != '"') return std::string(s); // not a plain key:null object
		size_t keyStart = i;
		i++;
		while (i < s.size() && !(s[i] == '"' && s[i - 1] != '\\')) { if (s[i] == '\\') i++; i++; }
		if (i >= s.size()) return std::string(s);
		size_t keyEnd = i;
		i++; // past the closing quote
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
		if (i >= s.size() || s[i] != ':') return std::string(s);
		i++;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
		if (i + 4 > s.size() || s.substr(i, 4) != "null") return std::string(s);
		i += 4;
		if (!first) out += ",";
		first = false;
		out.append(s.data() + keyStart, keyEnd - keyStart + 1);
		while (i < s.size() && (s[i] == ',' || s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
	}
	out += "]}";
	return out;
}

// Tag directives (%TAG handle prefix) for the current document being parsed
static thread_local std::unordered_map<std::string, std::string> yamlDocTagMap;

// RAII guard: snapshot the thread-local tag map on entry and restore it on
// scope exit (including via exception). A failed parse can no longer leak
// partial %TAG directives into a subsequent parse on the same thread.
struct YamlDocTagMapGuard {
	std::unordered_map<std::string, std::string> saved_;
	YamlDocTagMapGuard() { saved_.swap(yamlDocTagMap); }
	~YamlDocTagMapGuard() { yamlDocTagMap.swap(saved_); }
	YamlDocTagMapGuard(const YamlDocTagMapGuard&) = delete;
	YamlDocTagMapGuard& operator=(const YamlDocTagMapGuard&) = delete;
};

// Resolve a tag shorthand using %TAG directives (longest handle match wins)
inline std::string yamlResolveTag(std::string_view rawTag) {
	std::string s(rawTag);
	std::string bestHandle;
	std::string bestPrefix;
	for (const auto& [handle, prefix] : yamlDocTagMap) {
		if (s.size() >= handle.size() && s.substr(0, handle.size()) == handle) {
			if (handle.size() > bestHandle.size()) { bestHandle = handle; bestPrefix = prefix; }
		}
	}
	if (!bestHandle.empty()) return bestPrefix + s.substr(bestHandle.size());
	return s;
}

// Forward declaration for flow parser (used by tagged value handler)
inline std::string yamlParseFlowValue(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving,
    int depth = 0);

// Parse tagged YAML value -- handles all !!word / !word tags
inline std::string yamlParseTaggedValue(std::string_view s) {
	// Extract tag name
	size_t tagEnd = 0;
	if (s.size() >= 2 && s[0] == '!') {
		if (s[1] == '!') {
			tagEnd = 2;
			while (tagEnd < s.size() && s[tagEnd] != ' ') tagEnd++;
		} else if (s[1] == '<') {
			tagEnd = s.find('>', 2);
			if (tagEnd != std::string_view::npos) tagEnd++; else tagEnd = s.size();
		} else {
			tagEnd = 1;
			while (tagEnd < s.size() && s[tagEnd] != ' ') tagEnd++;
		}
	}
	if (tagEnd == 0) return yamlParseInlineValue(s);

	std::string tagStr = yamlResolveTag(s.substr(0, tagEnd));
	std::string_view tag = tagStr;
	std::string_view val;
	while (tagEnd < s.size() && s[tagEnd] == ' ') tagEnd++;
	if (tagEnd < s.size()) val = s.substr(tagEnd);

	// Non-specific tags ("!": user-defined, "?": non-specific) apply nothing:
	// the value keeps its default (content/plain) interpretation.
	if (tag == "!" || tag == "?") return yamlParseInlineValue(val);

	// Standard YAML tags
	if (tag == "!!str") return yamlForceStringVal(val);
	if (tag == "!!int") {
		if (val.empty()) return "0";
		std::string valStr(val);
		// Handle YAML Core Schema integer prefixes: 0x (hex), 0o (octal), 0b (binary)
		if (val.size() >= 3 && val[0] == '0') {
			if (val[1] == 'x' || val[1] == 'X') {
				char* e = nullptr;
				errno = 0;
				long long n = std::strtoll(valStr.c_str() + 2, &e, 16);
				if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
				if (e == valStr.c_str() + valStr.size()) return std::to_string(n);
			} else if (val[1] == 'o' || val[1] == 'O') {
				char* e = nullptr;
				errno = 0;
				long long n = std::strtoll(valStr.c_str() + 2, &e, 8);
				if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
				if (e == valStr.c_str() + valStr.size()) return std::to_string(n);
			} else if ((val[0] == '0' && (val[1] == 'b' || val[1] == 'B')) ||
			           ((val[0] == '-' || val[0] == '+') && val.size() >= 4 && val[1] == '0' && (val[2] == 'b' || val[2] == 'B'))) {
				size_t start = (val[0] == '0') ? 2 : 3;
				int sign = 1;
				if (val[0] == '-') sign = -1;
				long long n = 0;
				bool valid = true;
				for (size_t bi = start; bi < val.size(); bi++) {
					if (val[bi] == '0') n *= 2;
					else if (val[bi] == '1') n = n * 2 + 1;
					else { valid = false; break; }
				}
				if (valid && (n > 0 || val.find_first_not_of("01", start) == std::string::npos))
					return std::to_string(sign * n);
			}
		}
		// Plain decimal integer (including negative)
		{
			char* e = nullptr;
			errno = 0;
			long long n = std::strtoll(valStr.c_str(), &e, 10);
			if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
			if (e == valStr.c_str() + valStr.size()) return std::to_string(n);
		}
		std::string parsed = yamlParseInlineValue(val);
		if (parsed.size() >= 2 && parsed[0] == '"' && parsed.back() == '"') {
			std::string inner = unescapeJsonString(parsed.substr(1, parsed.size() - 2), true);
			char* e = nullptr;
			errno = 0;
			long long n = std::strtoll(inner.c_str(), &e, 0);
			if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
			if (e == inner.c_str() + inner.size()) return std::to_string(n);
			throw asvJSONError("YAML: invalid !!int value");
		}
		// parsed must be a valid JSON number, not bool/null/string
		if (!parsed.empty() && (parsed[0] == '-' || (parsed[0] >= '0' && parsed[0] <= '9'))) {
			char* e = nullptr;
			errno = 0;
			long long n = std::strtoll(parsed.c_str(), &e, 10);
			if (errno == ERANGE) throw asvJSONError("YAML: !!int overflow");
			if (e == parsed.c_str() + parsed.size()) return std::to_string(n);
		}
		throw asvJSONError("YAML: invalid !!int value");
	}
	if (tag == "!!float") {
		if (val.empty()) return "0.0";
		// YAML Core Schema special float values
		if (val == ".inf" || val == ".Inf" || val == ".INF" || val == "Infinity") return "Infinity";
		if (val == "-.inf" || val == "-.Inf" || val == "-.INF" || val == "-Infinity") return "-Infinity";
		if (val == ".nan" || val == ".NaN" || val == ".NAN" || val == "NaN") return "NaN";
		// Try direct float parsing on raw val
		{
			std::string valStr(val);
			char* e = nullptr;
			errno = 0;
			double d = std::strtod(valStr.c_str(), &e);
			if (errno == ERANGE) throw asvJSONError("YAML: !!float overflow");
			if (e == valStr.c_str() + valStr.size() && valStr.size() > 0) {
			#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
				char buf[64];
				auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
				if (ec == std::errc()) return std::string(buf, ptr);
			#else
				char buf[64];
				int n = snprintf(buf, sizeof(buf), "%.16g", d);
				if (n > 0) return std::string(buf, static_cast<size_t>(n));
			#endif
			}
		}
		std::string parsed = yamlParseInlineValue(val);
		if (parsed.size() >= 2 && parsed[0] == '"' && parsed.back() == '"') {
			std::string inner = unescapeJsonString(parsed.substr(1, parsed.size() - 2), true);
			char* e = nullptr;
			errno = 0;
			double d = std::strtod(inner.c_str(), &e);
			if (errno == ERANGE) throw asvJSONError("YAML: !!float overflow");
			if (e == inner.c_str() + inner.size()) {
			#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
				char buf[64];
				auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
				if (ec == std::errc()) return std::string(buf, ptr);
			#else
				char buf[64];
				int n = snprintf(buf, sizeof(buf), "%.16g", d);
				if (n > 0) return std::string(buf, static_cast<size_t>(n));
			#endif
			}
			throw asvJSONError("YAML: invalid !!float value");
		}
		// parsed must be a valid JSON number
		if (!parsed.empty() && (parsed[0] == '-' || (parsed[0] >= '0' && parsed[0] <= '9'))) {
			char* e = nullptr;
			errno = 0;
			double d = std::strtod(parsed.c_str(), &e);
			if (errno == ERANGE) throw asvJSONError("YAML: !!float overflow");
			if (e == parsed.c_str() + parsed.size()) {
			#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
				char buf[64];
				auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
				if (ec == std::errc()) return std::string(buf, ptr);
			#else
				char buf[64];
				int n = snprintf(buf, sizeof(buf), "%.16g", d);
				if (n > 0) return std::string(buf, static_cast<size_t>(n));
			#endif
			}
		}
		throw asvJSONError("YAML: invalid !!float value");
	}
	if (tag == "!!bool") {
		if (val.empty()) return "false";
		std::string lower; lower.reserve(val.size());
		for (auto c : val) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (lower == "true" || lower == "yes" || lower == "on") return "true";
		return "false";
	}
	if (tag == "!!null") return "null";
	if (tag == "!!timestamp") {
		if (!val.empty()) return yamlParseInlineValue(val);
		return "\"\"";
	}
	if (tag == "!!value") {
		// YAML 1.1 "value": the scalar denoting a mapping's default value.
		// The canonical form is the scalar's default (content-based)
		// interpretation -- i.e. equivalent to no tag at all.
		if (val.empty()) return "null";
		return yamlParseInlineValue(val);
	}
	if (tag == "!!yaml") {
		// A YAML document embedded in a scalar ("!!yaml \"a: 1\"").
		// Unquote/unescape the scalar, then process it as an embedded document
		// (a whole scalar becomes a value; otherwise it is parsed recursively).
		if (val.empty()) return "null";
		std::string resolved = yamlParseInlineValue(val);
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		return yamlProcessYamlEmbed(std::move(raw));
	}

	// Special type tags (Extended JSON)
	if (tag == "!!binary") {
		return "{\"$binary\":{\"base64\":\"" + std::string(val) + "\",\"subType\":\"00\"}}";
	}
	if (tag == "!objectid") {
		return "{\"$oid\":\"" + std::string(val) + "\"}";
	}
	if (tag == "!regex") {
		std::string resolved = yamlParseInlineValue(val);
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		std::string internal = yamlRegexToInternal(raw);
		size_t sep = internal.rfind('|');
		std::string pattern = (sep != std::string::npos) ? internal.substr(0, sep) : internal;
		std::string opts = (sep != std::string::npos) ? internal.substr(sep + 1) : "";
		std::string out = "{\"$regex\":\"";
		for (auto c : pattern) {
			if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c;
		}
		out += "\"";
		if (!opts.empty()) {
			out += ",\"$options\":\"";
			for (auto c : opts) {
				if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else out += c;
			}
			out += "\"";
		}
		out += "}";
		return out;
	}
	if (tag == "!ext") {
		std::string remaining = std::string(val);
		size_t sp = remaining.find(' ');
		if (sp == std::string::npos) return yamlParseInlineValue(val);
		int extType = 0;
		std::from_chars(remaining.data(), remaining.data() + sp, extType);
		std::string resolved = yamlParseInlineValue(std::string_view(remaining).substr(sp + 1));
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		char hex[3];
		snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(extType));
		return "{\"$binary\":{\"base64\":\"" + raw + "\",\"subType\":\"" + hex + "\"}}";
	}

	// !!set -- convert to the {"$set":[...]} marker; the JSON object parser
	// turns it back into an OBJECT flagged as a YAML set.
	if (tag == "!!set") {
		if (val.empty()) return "{\"$set\":[]}";
		if (val[0] == '[') {
			std::string tmp(val);
			size_t fp = 0;
			std::string flowJson = yamlParseFlowValue(tmp, fp, nullptr, nullptr, 0);
			return yamlArrayToSetObject(flowJson);
		}
		if (val[0] == '{') {
			std::string tmp(val);
			size_t fp = 0;
			return yamlSetObjToMarked(yamlParseFlowValue(tmp, fp, nullptr, nullptr, 0));
		}
		return std::string(val);
	}
	// !!omap -- ensure array of single-key objects; for flow [{a:1},{b:2}] already correct
	// !!pairs -- ensure array of pairs; for flow [[a,1],[b,2]] already correct
	if (tag == "!!omap" || tag == "!!pairs") {
		if (val.empty()) return "[]";
		if (val[0] == '[' || val[0] == '{') {
			std::string tmp(val);
			size_t fp = 0;
			return yamlParseFlowValue(tmp, fp, nullptr, nullptr, 0);
		}
		return "[]";
	}
	// !!map, !!seq -- the value is a flow collection; parse it to JSON so
	// unquoted keys and YAML scalars become proper JSON (block context is
	// handled by the main loop via yamlTaggedBlockValue). The wrong kind (e.g.
	// "!!map [1]" or "!!seq {a:1}") is a representational error.
	if (tag == "!!map" || tag == "!!seq") {
		if (val.empty()) return (tag == "!!seq") ? "[]" : "{}";
		if (val[0] == '{' || val[0] == '[') {
			std::string tmp(val);
			size_t fp = 0;
			std::string flowJson = yamlParseFlowValue(tmp, fp, nullptr, nullptr, 0);
			if (tag == "!!seq" && (flowJson.empty() || flowJson[0] != '[')) throw asvJSONError("YAML: !!seq requires a sequence");
			if (tag == "!!map" && (flowJson.empty() || flowJson[0] != '{')) throw asvJSONError("YAML: !!map requires a mapping");
			return flowJson;
		}
		return std::string(val);
	}

	// Unknown tag -- preserve the annotation as a CUSTOM_TAG. The tag name is
	// stored separately from its value (the resolved full name for %TAG-relative
	// and verbatim !<...> tags), so "!name value" round-trips through YAML and
	// JSON ({"$customTag":{"name":...,"value":...}}).
	{
		std::string parsedVal = val.empty() ? "null" : yamlParseInlineValue(val);
		std::string name;
		if (tagStr.size() >= 2 && tagStr[0] == '!' && tagStr[1] == '<' && tagStr.back() == '>')
			name = tagStr.substr(2, tagStr.size() - 3); // verbatim URI: strip !< >
		else if (!tagStr.empty() && tagStr[0] == '!')
			name = tagStr.substr(1);                    // strip leading '!'
		else
			name = tagStr;
		std::string out = "{\"$customTag\":{\"name\":\"";
		for (auto c : name) {
			if (c == '"' || c == '\\') out += '\\';
			out += c;
		}
		out += "\",\"value\":";
		out += parsedVal;
		out += "}}";
		return out;
	}
}

inline bool yamlIsAnchorChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

// Check if position p is inside flow brackets ({...} or [...]) in string s
inline bool yamlInFlowBrackets(std::string_view s, size_t p) {
	int depth = 0;
	bool inSQuote = false, inDQuote = false;
	for (size_t i = 0; i < p && i < s.size(); i++) {
		if (inSQuote) {
			if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') i++;
			else if (s[i] == '\'') inSQuote = false;
		} else if (inDQuote) {
			if (s[i] == '\\') i++;
			else if (s[i] == '"') inDQuote = false;
		} else {
			if (s[i] == '{' || s[i] == '[') depth++;
			else if (s[i] == '}' || s[i] == ']') depth--;
			else if (s[i] == '\'') inSQuote = true;
			else if (s[i] == '"') inDQuote = true;
		}
	}
	return depth > 0;
}

// Check if & is inside quotes at the given position in s
// Handles escaped quotes: \" inside "", '' inside ''
inline bool yamlAmpInQuote(std::string_view s, size_t ampPos) {
	bool inSQuote = false, inDQuote = false;
	for (size_t i = 0; i < ampPos && i < s.size(); i++) {
		if (inSQuote) {
			if (s[i] == '\'') {
				if (i + 1 < s.size() && s[i + 1] == '\'') i++;
				else inSQuote = false;
			}
		} else if (inDQuote) {
			if (s[i] == '\\') i++;
			else if (s[i] == '"') inDQuote = false;
		} else {
			if (s[i] == '\'') inSQuote = true;
			else if (s[i] == '"') inDQuote = true;
		}
	}
	return inSQuote || inDQuote;
}

// Resolve *alias with cycle detection
inline std::string yamlResolveAlias(std::string_view aliasName,
                                    std::unordered_map<std::string, std::string>& anchors,
                                    std::unordered_set<std::string>& resolving) {
	std::string name(aliasName);
	if (resolving.count(name)) {
		throw asvJSONError("YAML: cyclic alias '" + name + "'");
	}
	auto it = anchors.find(name);
	if (it == anchors.end()) {
		throw asvJSONError("YAML: undefined alias '" + name + "'");
	}
	resolving.insert(name);
	std::string result = it->second;
	resolving.erase(name);
	return result;
}

// Forward declarations for flow parsers (anchored versions support &/* inside flow)
inline std::string yamlParseFlowMap(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors = nullptr,
    std::unordered_set<std::string>* resolving = nullptr,
    int depth = 0);
inline std::string yamlParseFlowSeq(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors = nullptr,
    std::unordered_set<std::string>* resolving = nullptr,
    int depth = 0);

// Parse a complete YAML inline value and return JSON string
inline std::string yamlParseInlineValue(std::string_view s, int depth) {
	while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.remove_prefix(1);
	if (s.empty()) return "null";
	if (s[0] == '!' && s.size() > 1) return yamlParseTaggedValue(s);
	if (s.size() >= 2) {
		if (s[0] == '"') {
			std::string inner;
			size_t i = 1;
			for (; i < s.size(); i++) {
				if (s[i] == '\\' && i + 1 < s.size()) { inner += s[i]; inner += s[i + 1]; i++; continue; }
				if (s[i] == '"') break;
				inner += s[i];
			}
			if (i >= s.size())
				throw asvJSONError("YAML: unterminated double-quoted string");
			size_t after = i + 1;
			while (after < s.size() && (s[after] == ' ' || s[after] == '\t')) after++;
			if (after < s.size())
				throw asvJSONError("YAML: trailing content after quoted string");
			std::string unesc = yamlUnescapeDouble(inner);
			std::string out = "\"";
			for (auto c : unesc) {
				switch (c) {
					case '"': out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n"; break;
					case '\t': out += "\\t"; break;
					case '\r': out += "\\r"; break;
					default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
					         else out += c;
				}
			}
			out += "\"";
			return out;
		} else if (s[0] == '\'') {
			size_t end = 1;
			for (; end < s.size(); end++) {
				if (s[end] == '\'' && end + 1 < s.size() && s[end + 1] == '\'') end++;
				else if (s[end] == '\'') break;
			}
			if (end >= s.size())
				throw asvJSONError("YAML: unterminated single-quoted string");
			size_t after = end + 1;
			while (after < s.size() && (s[after] == ' ' || s[after] == '\t')) after++;
			if (after < s.size())
				throw asvJSONError("YAML: trailing content after quoted string");
			std::string inner(s.substr(1, end - 1));
			std::string unesc = yamlUnescapeSingle(inner);
			std::string out = "\"";
			for (auto c : unesc) {
				switch (c) {
					case '"': out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n"; break;
					case '\t': out += "\\t"; break;
					case '\r': out += "\\r"; break;
					default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
					         else out += c;
				}
			}
			out += "\"";
			return out;
		}
		if (s[0] == '{' || s[0] == '[') {
			std::string tmp(s);
			size_t fp = 0;
			std::string got = yamlParseFlowValue(tmp, fp, nullptr, nullptr, depth);
			while (fp < tmp.size() && (tmp[fp] == ' ' || tmp[fp] == '\t')) fp++;
			if (fp < tmp.size()) throw asvJSONError("YAML: trailing content after flow value");
			return got;
		}
	}
	return yamlScalarToJson(s);
}

// JSON-escape a string for use as JSON object key
inline std::string yamlEscKey(std::string_view s) {
	std::string out;
	for (auto c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			default: out += c;
		}
	}
	return out;
}

// True when `s` is exactly a set tag (after %TAG resolution) with no trailing
// value -- the only shape that can introduce a block-style set ("k: !!set").
inline bool yamlIsSetBlockHeader(std::string_view s) {
	if (s.empty() || s[0] != '!') return false;
	// The tag must occupy the whole string; a trailing value ("!!set {a, b}")
	// routes the normal tagged-value path instead.
	if (s.find_first_of(" \t") != std::string_view::npos) return false;
	return yamlResolveTag(s) == "!!set";
}

inline std::string_view::size_type yamlFindKeyColonSep(std::string_view s);

inline int yamlFlowDepth(std::string_view s);
inline std::string yamlEscJsonText(std::string_view jsonText);
inline std::string yamlGatherFlow(const std::vector<std::string>& lines, std::string_view firstPart, size_t& li);
inline std::string yamlFlowJsonAsKey(const std::string& flowText,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving, int depth);
inline std::string yamlParseDocImpl(std::vector<std::string>& lines, int depth);

// Parse a block-style !!set: the header line plus the following "- key" /
// "? key" item lines. Works at document root or as a mapping/sequence value.
// Emits "key": null pairs, fixes the item indentation from the first item, and
// advances `li` past the last consumed line. Content at a different indentation
// ends the set and is left for the main loop. Keys stringify like mapping keys:
// scalar / quoted keys pass through; flow-collection members ("- {a}",
// "- [a,b]") become keys via their intermediate-JSON text; a bare "-" (or a
// "? key: value" pair) owns a compound block child node which is parsed
// recursively and its JSON text becomes the key string.
inline std::string yamlParseBlockSet(const std::vector<std::string>& lines, size_t& li,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving, int depth) {
	std::string out = "{\"$set\":[";
	bool first = true;
	int itemIndent = -1;
	size_t j = li + 1;
	while (j < lines.size()) {
		std::string_view sc = stripIndent(lines[j]);
		if (sc.empty() || sc[0] == '#') { j++; continue; }
		int ind = countIndent(lines[j]);
		if (itemIndent < 0) itemIndent = ind;
		else if (ind != itemIndent) break; // sibling content at another level ends the set

		bool bareDash = false;
		std::string body;
		if (!sc.empty() && sc[0] == '-') {
			if (sc.size() == 1) bareDash = true;
			else if (sc[1] == ' ') body = std::string(sc.substr(2));
			else break; // "-x": a plain scalar, ends the set
		} else if (sc.size() >= 2 && sc[0] == '?' && sc[1] == ' ') {
			body = std::string(sc.substr(2));
		} else {
			break; // mapping entry / flow / scalar -- ends the set
		}
		while (!body.empty() && (body[0] == ' ' || body[0] == '\t')) body.erase(0, 1);
		body = yamlStripComment(body);
		while (!body.empty() && (body.back() == ' ' || body.back() == '\t')) body.pop_back();

		std::string keyContent;
		if (bareDash) {
			// A bare "-" owns a compound child node on deeper lines. Collect the
			// whole child (including blank/comment lines) up to the next line at
			// the item indentation or shallower.
			size_t p = j + 1;
			std::vector<std::string> childLines;
			bool hasChild = false;
			while (p < lines.size()) {
				std::string_view cs = stripIndent(lines[p]);
				if (cs.empty() || cs[0] == '#') { childLines.push_back(lines[p]); p++; continue; }
				if (countIndent(lines[p]) <= ind) break;
				childLines.push_back(lines[p]);
				hasChild = true;
				p++;
			}
			if (!hasChild) break; // a trailing bare dash with no child ends the set

			std::vector<std::string> meaningful;
			for (const auto& cl : childLines) {
				std::string_view cs = stripIndent(cl);
				if (cs.empty() || cs[0] == '#') continue;
				meaningful.push_back(cl);
			}
			// A single plain scalar child is just a scalar set key.
			if (meaningful.size() == 1) {
				std::string_view cs = stripIndent(meaningful[0]);
				if (cs.size() >= 2 && cs[0] == '"' && cs.back() == '"') {
					keyContent = yamlEscKey(yamlUnescapeDouble(cs.substr(1, cs.size() - 2)));
				} else if (cs.size() >= 2 && cs[0] == '\'' && cs.back() == '\'') {
					keyContent = yamlEscKey(yamlUnescapeSingle(cs.substr(1, cs.size() - 2)));
				} else if (cs.empty() || cs[0] == '-' || cs[0] == '?' || cs[0] == '{' || cs[0] == '[' ||
				    cs[0] == '!' || cs[0] == '|' || cs[0] == '>' ||
				    yamlFindKeyColonSep(cs) != std::string_view::npos) {
					std::string childJson = yamlParseDocImpl(childLines, depth + 1);
					keyContent = yamlEscJsonText(childJson);
				} else {
					keyContent = yamlEscKey(cs);
				}
			} else {
				std::string childJson = yamlParseDocImpl(childLines, depth + 1);
				keyContent = yamlEscJsonText(childJson);
			}
			j = p - 1;
		} else if (body.empty()) {
			break;
		} else if (body[0] == '{' || body[0] == '[') {
			// Flow-collection member: stringify via its intermediate JSON text.
			if (yamlFlowDepth(body) > 0)
				body = yamlGatherFlow(lines, body, j);
			keyContent = yamlFlowJsonAsKey(body, anchors, resolving, depth);
		} else if (yamlFindKeyColonSep(body) != std::string_view::npos) {
			// Single-line mapping pair ("? x: v", "? {a}: v"): parse it as a
			// one-line document and use its JSON text as the key string.
			std::vector<std::string> oneLine;
			oneLine.push_back(std::string(static_cast<size_t>(ind), ' ') + body);
			std::string pairJson = yamlParseDocImpl(oneLine, depth + 1);
			keyContent = yamlEscJsonText(pairJson);
		} else {
			// Quoted keys are unescaped; plain keys stringify as-is.
			std::string rawKey;
			if (body.size() >= 2 && body[0] == '"' && body.back() == '"')
				rawKey = yamlUnescapeDouble(body.substr(1, body.size() - 2));
			else if (body.size() >= 2 && body[0] == '\'' && body.back() == '\'')
				rawKey = yamlUnescapeSingle(body.substr(1, body.size() - 2));
			else
				rawKey = body;
			keyContent = yamlEscKey(rawKey);
		}
		if (!first) out += ",";
		first = false;
		out += "\"" + keyContent + "\"";
		li = j;
		j++;
	}
	if (first) return "{\"$set\":[]}";
	out += "]}";
	return out;
}

// Emit a raw scalar value as a quoted, JSON-escaped string (used when a value
// must remain a string, e.g. block-scalar bodies).
inline std::string yamlQuoteJsonScalar(std::string_view s) {
	std::string escaped;
	for (auto c : s) {
		switch (c) {
			case '"': escaped += "\\\""; break;
			case '\\': escaped += "\\\\"; break;
			case '\n': escaped += "\\n"; break;
			case '\t': escaped += "\\t"; break;
			case '\r': escaped += "\\r"; break;
			default: if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); escaped += buf; }
			         else escaped += c;
		}
	}
	return "\"" + escaped + "\"";
}

// Forward declarations needed by flow parsers
inline std::string yamlParseTaggedValue(std::string_view s);

// Gather complete flow collection text across multiple lines, stripping comments
inline std::string yamlGatherFlow(const std::vector<std::string>& lines, std::string_view firstPart, size_t& li) {
	char openCh = firstPart[0];
	char closeCh = (openCh == '{') ? '}' : ']';
	std::string result(firstPart);
	int depth = 0;
	bool inSq = false, inDq = false;
	// Count initial depth from firstPart
	for (size_t i = 0; i < firstPart.size(); i++) {
		char c = firstPart[i];
		if (inSq) { if (c == '\'' && i + 1 < firstPart.size() && firstPart[i + 1] == '\'') i++; else if (c == '\'') inSq = false; }
		else if (inDq) { if (c == '\\') i++; else if (c == '"') inDq = false; }
		else {
			if (c == '\'') inSq = true; else if (c == '"') inDq = true;
			else if (c == '#') break;
			else if (c == openCh) depth++; else if (c == closeCh) depth--;
		}
	}
	if (depth <= 0) {
		while (!result.empty() && (result.back() == ' ' || result.back() == '\t' || result.back() == '\n')) result.pop_back();
		return result;
	}
	// Read more lines until depth closes
	while (li + 1 < lines.size() && depth > 0) {
		li++;
		std::string line(stripIndent(lines[li]));
		std::string clean;
		inSq = false; inDq = false;
		for (size_t i = 0; i < line.size(); i++) {
			char c = line[i];
			if (inSq) {
				if (c == '\'' && i + 1 < line.size() && line[i + 1] == '\'') { clean += c; i++; clean += line[i]; }
				else { clean += c; if (c == '\'') inSq = false; }
			} else if (inDq) {
				clean += c;
				if (c == '\\' && i + 1 < line.size()) { i++; clean += line[i]; }
				else if (c == '"') inDq = false;
			} else if (c == '#') break;
			else {
				if (c == '\'') inSq = true; else if (c == '"') inDq = true;
				if (c == openCh) depth++; else if (c == closeCh) depth--;
				clean += c;
			}
		}
		if (!result.empty() && !clean.empty()) result += ' ';
		result += clean;
	}
	if (depth > 0)
		throw asvJSONError("YAML: unterminated flow collection");
	// Trim trailing whitespace so "fp != size" reliably detects trailing content
	while (!result.empty() && (result.back() == ' ' || result.back() == '\t' || result.back() == '\n')) result.pop_back();
	return result;
}

// Net open-flow-bracket depth of `s` (quote-aware and '\''-doubling aware):
// >0 means an unterminated flow collection.
inline int yamlFlowDepth(std::string_view s) {
	bool inSq = false, inDq = false;
	int depth = 0;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (inSq) { if (c == '\'' && i + 1 < s.size() && s[i + 1] == '\'') i++; else if (c == '\'') inSq = false; }
		else if (inDq) { if (c == '\\') i++; else if (c == '"') inDq = false; }
		else {
			if (c == '\'') inSq = true; else if (c == '"') inDq = true;
			else if (c == '{' || c == '[') depth++;
			else if (c == '}' || c == ']') depth--;
		}
	}
	return depth;
}

// JSON-escape the intermediate JSON text of a flow node so it can be embedded
// as the content of a JSON string key (the text itself is bare, no quotes).
inline std::string yamlEscJsonText(std::string_view jsonText) {
	std::string out;
	for (char c : jsonText) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			default: out += c;
		}
	}
	return out;
}

// Parse a whole flow-collection node and deterministically represent it as the
// ESCAPED content of a JSON string key: the intermediate JSON of the node
// ("[\"a\",\"b\"]", "{\"x\":1}") becomes the key text itself. `flowText` must
// be complete (a caller that gathered lines passes the full text).
inline std::string yamlFlowJsonAsKey(const std::string& flowText,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving, int depth) {
	size_t fp = 0;
	std::string got = yamlParseFlowValue(flowText, fp, anchors, resolving, depth);
	if (fp < flowText.size())
		throw asvJSONError("YAML: trailing content after compound key");
	return yamlEscJsonText(got);
}

// Parse a flow mapping KEY and return it as a JSON string. JSON object keys
// must be strings, so plain scalar keys are stringified from their raw text,
// quoted keys pass through unchanged, and flow-collection (compound) keys are
// stringified deterministically via their intermediate JSON text.
inline std::string yamlParseFlowKey(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving,
    int depth) {
	while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
	if (pos >= s.size() || s[pos] == '}' || s[pos] == ',' || s[pos] == ':')
		throw asvJSONError("YAML: expected a mapping key");
	if (s[pos] == '{' || s[pos] == '[') {
		// Compound key: parse the flow node and stringify its intermediate JSON
		// as the JSON string key.
		std::string keyJson = yamlParseFlowValue(s, pos, anchors, resolving, depth);
		return "\"" + yamlEscJsonText(keyJson) + "\"";
	}
	if (s[pos] == '"' || s[pos] == '\'') return yamlParseFlowValue(s, pos, anchors, resolving, depth);
	size_t start = pos;
	while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != '\n') {
		if (s[pos] == ':' && (pos + 1 >= s.size() || s[pos + 1] == ' ' || s[pos + 1] == '\t' || s[pos + 1] == '\n' || s[pos + 1] == '}' || s[pos + 1] == ']' || s[pos + 1] == ',')) break;
		pos++;
	}
	std::string_view sv(s.data() + start, pos - start);
	while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
	if (sv.empty()) throw asvJSONError("YAML: empty mapping key");
	std::string out = "\"";
	for (auto c : sv) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			case '\r': out += "\\r"; break;
			default: out += c;
		}
	}
	out += "\"";
	return out;
}

inline std::string yamlParseFlowMap(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving,
    int depth) {
	if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
		throw asvJSONError("YAML: flow nesting too deep");
	pos++; // skip {
	std::string out = "{";
	bool first = true;
	while (pos < s.size()) {
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
		if (pos >= s.size() || s[pos] == '}') break;
		if (s[pos] == ',') { pos++; continue; }
		if (!first) out += ",";
		first = false;
		std::string key = yamlParseFlowKey(s, pos, anchors, resolving, depth + 1);
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
		if (pos < s.size() && s[pos] == ':') {
			pos++;
			while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
			out += key + ":" + yamlParseFlowValue(s, pos, anchors, resolving, depth + 1);
		} else {
			// "{a}" -- implicit entry with no value: valid YAML (sets, null
			// values). Emits the key with an implicit null, e.g. !!set {a, b}.
			out += key + ":null";
		}
	}
	if (pos >= s.size())
		throw asvJSONError("YAML: unterminated flow collection");
	if (s[pos] == '}') pos++;
	out += "}";
	return out;
}

inline std::string yamlParseFlowSeq(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving,
    int depth) {
	if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
		throw asvJSONError("YAML: flow nesting too deep");
	pos++; // skip [
	std::string out = "[";
	bool first = true;
	while (pos < s.size()) {
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
		if (pos >= s.size() || s[pos] == ']') break;
		if (s[pos] == ',') { pos++; continue; }
		if (!first) out += ",";
		first = false;
		out += yamlParseFlowValue(s, pos, anchors, resolving, depth + 1);
	}
	if (pos >= s.size())
		throw asvJSONError("YAML: unterminated flow collection");
	if (s[pos] == ']') pos++;
	out += "]";
	return out;
}

inline std::string yamlParseFlowValue(const std::string& s, size_t& pos,
    std::unordered_map<std::string, std::string>* anchors,
    std::unordered_set<std::string>* resolving,
    int depth) {
	while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n')) pos++;
	if (pos >= s.size()) return "null";
	// Handle &anchor inside flow
	if (anchors && s[pos] == '&' && pos + 1 < s.size() && yamlIsAnchorChar(s[pos + 1])) {
		size_t ns = pos + 1;
		size_t ne = ns;
		while (ne < s.size() && yamlIsAnchorChar(s[ne])) ne++;
		std::string name(s, ns, ne - ns);
		pos = ne;
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
		std::string val = yamlParseFlowValue(s, pos, anchors, resolving, depth);
		if (anchors->size() >= MAX_YAML_ANCHORS) throw asvJSONError("YAML: too many anchors");
		(*anchors)[name] = val;
		return val;
	}
	// Handle *alias inside flow
	if (anchors && resolving && s[pos] == '*' && pos + 1 < s.size() && yamlIsAnchorChar(s[pos + 1])) {
		size_t ns = pos + 1;
		size_t ne = ns;
		while (ne < s.size() && yamlIsAnchorChar(s[ne])) ne++;
		std::string name(s, ns, ne - ns);
		pos = ne;
		return yamlResolveAlias(name, *anchors, *resolving);
	}
	if (s[pos] == '{') return yamlParseFlowMap(s, pos, anchors, resolving, depth);
	if (s[pos] == '[') return yamlParseFlowSeq(s, pos, anchors, resolving, depth);
	if (s[pos] == '"') {
		size_t start = pos; pos++;
		while (pos < s.size()) { if (s[pos] == '\\') pos += 2; else if (s[pos] == '"') break; else pos++; }
		if (pos >= s.size())
			throw asvJSONError("YAML: unterminated double-quoted string in flow");
		pos++;
		return yamlParseInlineValue(std::string_view(s.data() + start, pos - start), depth);
	}
	if (s[pos] == '\'') {
		size_t start = pos; pos++;
		while (pos < s.size()) { if (s[pos] == '\'' && pos + 1 < s.size() && s[pos + 1] == '\'') pos += 2; else if (s[pos] == '\'') break; else pos++; }
		if (pos >= s.size())
			throw asvJSONError("YAML: unterminated single-quoted string in flow");
		pos++;
		return yamlParseInlineValue(std::string_view(s.data() + start, pos - start), depth);
	}
	if (s[pos] == '!' && pos + 1 < s.size()) {
		size_t tagStart = pos; pos++;
		while (pos < s.size() && s[pos] != ' ' && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != '\n' && s[pos] != '\t') pos++;
		std::string tagStr(s, tagStart, pos - tagStart);
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
		size_t valStart = pos;
		if (pos < s.size() && s[pos] == '"') {
			pos++;
			while (pos < s.size()) { if (s[pos] == '\\') pos += 2; else if (s[pos] == '"') break; else pos++; }
			if (pos < s.size()) pos++;
		} else if (pos < s.size() && s[pos] == '\'') {
			pos++;
			while (pos < s.size()) { if (s[pos] == '\'' && pos + 1 < s.size() && s[pos + 1] == '\'') pos += 2; else if (s[pos] == '\'') break; else pos++; }
			if (pos < s.size()) pos++;
		} else {
			if (pos < s.size() && (s[pos] == '{' || s[pos] == '[')) {
				// Value is a flow collection: consume the full balanced node
				// (handling nesting and quoted strings) so tags like
				// "!!set {a, b}" / "!!seq [1, 2]" keep their closing delimiter.
				char open = s[pos], close = (open == '{') ? '}' : ']';
				int depthFlow = 0;
				for (; pos < s.size(); pos++) {
					char c = s[pos];
					if (c == '"') { pos++; while (pos < s.size()) { if (s[pos] == '\\') pos += 2; else if (s[pos] == '"') break; else pos++; } }
					else if (c == '\'') { pos++; while (pos < s.size()) { if (s[pos] == '\'' && pos + 1 < s.size() && s[pos + 1] == '\'') pos += 2; else if (s[pos] == '\'') break; else pos++; } }
					else if (c == open) depthFlow++;
					else if (c == close) { depthFlow--; if (depthFlow == 0) { pos++; break; } }
				}
			} else {
				while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != '\n') pos++;
			}
		}
		std::string combined = tagStr + " " + std::string(s, valStart, pos - valStart);
		while (!combined.empty() && (combined.back() == ' ' || combined.back() == '\t')) combined.pop_back();
		return yamlParseTaggedValue(combined);
	}
	// Plain scalar -- stop at , ] } \n or : followed by space/end/closer
	size_t start = pos;
	while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != '\n') {
		if (s[pos] == ':' && (pos + 1 >= s.size() || s[pos + 1] == ' ' || s[pos + 1] == '\t' || s[pos + 1] == '\n' || s[pos + 1] == '}' || s[pos + 1] == ']' || s[pos + 1] == ',')) break;
		pos++;
	}
	std::string_view sv(s.data() + start, pos - start);
	while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
	if (sv.empty()) return "null";
	return yamlParseInlineValue(sv, depth);
}

inline std::string yamlToJson(std::string_view input);
inline std::string yamlParseDoc(std::string_view input, int depth = 0);
// Parse a document whose lines are already split (used by anchors to avoid
// re-splitting the collected subtree). Owns its own scoped tag-map snapshot.
inline std::string yamlParseDocImpl(std::vector<std::string>& lines, int depth);
// A "!!yaml" tag whose content is on following lines (block-scalar header or
// indented block collection) needs gathering; see yamlGatherYamlEmbed.
inline bool yamlYamlTagNeedsGather(std::string_view s, std::string& tail);
inline std::string yamlGatherYamlEmbed(const std::string& tail,
    const std::vector<std::string>& lines, size_t& li, int parentIndent);
// True when `s` is a tag token with no value content around it (only optional
// whitespace / a trailing comment / a block-scalar header).
inline bool yamlIsSoloTag(std::string_view s);
// Parse the value of a solo tag that annotates a block node on deeper lines.
inline std::string yamlTaggedBlockValue(std::string_view s,
    const std::vector<std::string>& lines, size_t& li, int parentIndent, int depth);

// Pre-scan lines for &anchor definitions, building name->JSON map.
// For block anchors, extracts child lines and recursively calls yamlToJson.
// depth: recursion guard to detect cycles (max 64).
// Returns true if any anchors were found.
inline bool yamlBuildAnchorMap(std::vector<std::string>& lines,
                               std::unordered_map<std::string, std::string>& anchors,
                               int depth = 0) {
	if (depth > 64) throw asvJSONError("YAML: maximum anchor recursion depth exceeded");
	bool found = false;
	for (size_t li = 0; li < lines.size(); li++) {
		// Cheap pre-filter: lines without the '&' character cannot define anchors.
		if (lines[li].find('&') == std::string_view::npos) continue;
		int indent = countIndent(lines[li]);
		std::string_view content = stripIndent(lines[li]);
		if (content.empty() || content[0] == '#') continue;

		size_t amp = content.find('&');
		if (amp == std::string_view::npos) continue;
		if (yamlAmpInQuote(content, amp)) continue;
		if (yamlInFlowBrackets(content, amp)) continue; // flow anchors handled during parse

		size_t ns = amp + 1;
		if (ns >= content.size() || !yamlIsAnchorChar(content[ns])) continue;
		size_t ne = ns;
		while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
		std::string name(content.substr(ns, ne - ns));
		if (anchors.count(name)) continue;

		if (anchors.size() >= MAX_YAML_ANCHORS) throw asvJSONError("YAML: too many anchors");

		found = true;
		std::string_view rest = content.substr(ne);
		while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);

		if (!rest.empty() && rest[0] != '#') {
			if (yamlIsSoloTag(rest)) {
				// Anchor on a solo-tag line: the anchored value is the block
				// node on the following (deeper) lines, annotated by the tag.
				// Capture it exactly like the main pass so aliases resolve to
				// the tagged block value, not to an empty scalar.
				std::string tRest(rest);
				std::string ytail;
				size_t aLi = li;
				if (yamlYamlTagNeedsGather(tRest, ytail))
					anchors[name] = yamlGatherYamlEmbed(ytail, lines, aLi, indent);
				else
					anchors[name] = yamlTaggedBlockValue(tRest, lines, aLi, indent, depth + 1);
			} else {
				// Scalar anchor -- value follows on same line. Strip a trailing
				// comment exactly like the main pass does, so aliases agree with
				// the anchored line's own value.
				std::string_view rv = yamlStripComment(rest);
				while (!rv.empty() && (rv.back() == ' ' || rv.back() == '\t')) rv.remove_suffix(1);
				anchors[name] = yamlParseInlineValue(rv);
			}
		} else {
			// Block anchor -- collect child lines (content only, like before)
			std::vector<std::string> childLines;
			size_t ci = li + 1;
			size_t minIndent = SIZE_MAX;
			while (ci < lines.size()) {
				int si = countIndent(lines[ci]);
				std::string_view sc = stripIndent(lines[ci]);
				if (sc.empty() || sc[0] == '#') { ci++; continue; }
				if (si > indent) {
					if (static_cast<size_t>(si) < minIndent) minIndent = static_cast<size_t>(si);
					childLines.push_back(lines[ci]);
					ci++;
				} else {
					break;
				}
			}
			if (!childLines.empty()) {
				// Normalize indentation: strip the minimum indent in place
				if (minIndent != SIZE_MAX && minIndent > 0) {
					for (auto& cl : childLines) {
						std::string_view cs = stripIndent(cl);
						if (cs.empty() || cs[0] == '#') continue;
						if (static_cast<size_t>(countIndent(cl)) >= minIndent) cl.erase(0, minIndent);
					}
				}
				anchors[name] = yamlParseDocImpl(childLines, depth + 1);
			} else {
				anchors[name] = "null";
			}
		}
	}
	return found;
}

// Strip &anchor tags from a content string, returning cleaned version
inline std::string yamlStripAnchors(std::string_view s, std::string* outAnchor) {
	std::string result;
	result.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		if (s[i] == '&') {
			if (i + 1 < s.size() && yamlIsAnchorChar(s[i + 1])) {
				size_t start = i + 1;
				size_t end = start;
				while (end < s.size() && yamlIsAnchorChar(s[end])) end++;
				if (outAnchor) *outAnchor = std::string(s.substr(start, end - start));
				i = end;
				while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
				continue;
			}
		}
		result += s[i++];
	}
	while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) result.pop_back();
	return result;
}

inline void yamlRejectTabIndent(const std::string& line, int lineNum);

// Collect a YAML block scalar (| or >). `header` is the block header text
// (starting with the style char, may include an indent indicator [1-9] and a
// chomp indicator +|-). `li` is the line index of the header line. Gathers
// content lines, applies folding for '>', then chomps trailing line breaks:
// clip (default) keeps one, '-' strips all, '+' keeps all.
// parentIndent < 0 disables the "content must be deeper than parent" check.
inline std::string yamlParseBlockScalar(const std::string& header, const std::vector<std::string>& lines, size_t& li, bool isFolded, int parentIndent) {
	char chomp = 0; // 0 = clip, '+' = keep, '-' = strip
	int explicitIndent = 0;
	size_t h = 1;
	for (; h < header.size(); h++) {
		char ch = header[h];
		if (ch == ' ' || ch == '\t') continue;
		if (ch >= '1' && ch <= '9') { explicitIndent = ch - '0'; continue; }
		if (ch == '+' || ch == '-') { chomp = ch; continue; }
		break; // '#' comment or invalid content
	}
	// After the indicators only a '#' comment (or end of header) may follow;
	// anything else ("| foo", "|2x") is a syntax error.
	while (h < header.size() && (header[h] == ' ' || header[h] == '\t')) h++;
	if (h < header.size() && header[h] != '#')
		throw asvJSONError("YAML: unexpected content after block scalar header");

	std::string text;
	int contentIndent = explicitIndent > 0 ? explicitIndent : -1;
	size_t ci = li + 1;
	// Collect content lines as structured entries so the folded path can apply
	// the YAML folding rules (a line break is folded to a space only between
	// two "neutral" lines; a line ending in white space or a more-indented
	// line keeps its break literal) and both paths preserve excess leading
	// spaces (indentation beyond the content indent is data, not indentation).
	struct YBSLine { std::string content; int excess; bool isBlank; };
	std::vector<YBSLine> entries;
	// splitLines does not emit a phantom trailing empty line at EOF, so all
	// remaining lines here are real content (a genuine trailing blank line is
	// collected and keeps block-scalar chomp semantics correct).
	while (ci < lines.size()) {
		int si = countIndent(lines[ci]);
		yamlRejectTabIndent(lines[ci], static_cast<int>(ci) + 1);
		std::string_view sc = stripIndent(lines[ci]);
		if (sc.empty()) { if (contentIndent > 0) entries.push_back({"", 0, true}); ci++; continue; }
		if (sc[0] == '#') { ci++; continue; }
		if (parentIndent >= 0 && si <= parentIndent) break;
		if (contentIndent < 0) contentIndent = si;
		if (si >= contentIndent) {
			entries.push_back({std::string(sc), si - contentIndent, false});
			ci++;
			continue;
		}
		break;
	}
	if (ci > li + 1) li = ci - 1;

	if (isFolded) {
		std::string folded;
		bool prevEndsWs = false;
		int brk = 0;
		bool seen = false;
		for (const auto& e : entries) {
			if (e.isBlank) { brk++; continue; }
			if (seen) {
				int k = brk + 1; // line breaks between the previous content line and this one
				if (!prevEndsWs && e.excess == 0) {
					if (k == 1) folded += ' ';
					else folded.append(static_cast<size_t>(k - 1), '\n');
				} else {
					// Fold does not join across a line ending in white space or a
					// more-indented line: those line breaks are literal.
					folded.append(static_cast<size_t>(k), '\n');
				}
			}
			if (e.excess > 0) folded.append(static_cast<size_t>(e.excess), ' ');
			folded += e.content;
			prevEndsWs = !e.content.empty() && (e.content.back() == ' ' || e.content.back() == '\t');
			brk = 0;
			seen = true;
		}
		if (seen) folded.append(static_cast<size_t>(brk), '\n'); // trailing blanks stay literal
		text = folded;
	} else {
		for (const auto& e : entries) {
			if (e.isBlank) { text += '\n'; continue; }
			if (e.excess > 0) text.append(static_cast<size_t>(e.excess), ' ');
			text += e.content;
			text += '\n';
		}
	}

	size_t trailing = 0;
	while (trailing < text.size() && text[text.size() - 1 - trailing] == '\n') trailing++;
	if (chomp == '-') {
		text.erase(text.size() - trailing);
	} else if (chomp == 0) {
		text.erase(text.size() - trailing);
		if (!text.empty()) text += '\n';
	}
	return text;
}

// True when `s` is a "!!yaml" tagged value whose tail is either empty, a
// trailing comment, or a valid block-scalar header ("|", ">", with optional
// indent digit and chomp indicator). In those shapes the embedded YAML content
// lives on the following lines and must be gathered by yamlGatherYamlEmbed.
// Inline scalar tails ("!!yaml \"a: 1\"") return false and go through the
// normal tagged-value path.
inline bool yamlYamlTagNeedsGather(std::string_view s, std::string& tail) {
	tail.clear();
	size_t i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
	size_t tagEnd = i;
	while (tagEnd < s.size() && s[tagEnd] != ' ' && s[tagEnd] != '\t') tagEnd++;
	if (s.substr(i, tagEnd - i) != "!!yaml") return false;
	std::string rem(s.substr(tagEnd));
	size_t p = 0;
	while (p < rem.size() && (rem[p] == ' ' || rem[p] == '\t')) p++;
	if (p >= rem.size() || rem[p] == '#') return true; // bare tag (or comment only)
	if (rem[p] != '|' && rem[p] != '>') return false;   // inline scalar/collection tail
	size_t q = p + 1;
	while (q < rem.size() && rem[q] >= '0' && rem[q] <= '9') q++;
	if (q < rem.size() && (rem[q] == '+' || rem[q] == '-')) q++;
	while (q < rem.size() && (rem[q] == ' ' || rem[q] == '\t')) q++;
	if (q < rem.size() && rem[q] == '#') { while (q < rem.size()) q++; }
	if (q != rem.size()) return false; // junk after the header
	tail = rem.substr(p, q - p);
	return true;
}

// Gather and parse the YAML document carried by a "!!yaml" tagged value whose
// content is on the following lines: a same-line block-scalar header (`tail`
// non-empty), a deeper "|"/">" header, or an indented block collection. For a
// bare tag with no deeper content the embed is null. Advances `li` past all
// consumed lines. `parentIndent` is the indent of the current key/entry.
inline std::string yamlGatherYamlEmbed(const std::string& tail,
                                       const std::vector<std::string>& lines,
                                       size_t& li, int parentIndent) {
	if (!tail.empty()) {
		bool isFolded = (tail[0] == '>');
		std::string text = yamlParseBlockScalar(tail, lines, li, isFolded, parentIndent);
		return yamlProcessYamlEmbed(std::move(text));
	}
	size_t sc = li + 1;
	while (sc < lines.size()) {
		std::string_view scv = stripIndent(lines[sc]);
		if (scv.empty() || scv[0] == '#') { sc++; continue; }
		break;
	}
	if (sc >= lines.size() || countIndent(lines[sc]) <= parentIndent) return "null";
	std::string_view scv = stripIndent(lines[sc]);
	if (scv[0] == '|' || scv[0] == '>') {
		bool isFolded = (scv[0] == '>');
		std::string text = yamlParseBlockScalar(std::string(scv), lines, sc, isFolded, parentIndent);
		li = sc;
		return yamlProcessYamlEmbed(std::move(text));
	}
	// Indented block collection: gather the whole region up to a dedent.
	int childIndent = countIndent(lines[sc]);
	size_t e = sc;
	while (e < lines.size()) {
		std::string_view t2 = stripIndent(lines[e]);
		if (t2.empty() || t2[0] == '#') { e++; continue; }
		if (countIndent(lines[e]) < childIndent) break;
		e++;
	}
	std::string buf;
	for (size_t i2 = sc; i2 < e; i2++) { buf += lines[i2]; buf += '\n'; }
	while (!buf.empty() && buf.back() == '\n') buf.pop_back();
	li = e - 1;
	return yamlProcessYamlEmbed(std::move(buf));
}

// YAML forbids using tabs for indentation (Section 10). A tab appearing in the
// leading whitespace of a line is a structural error; throw to match UDE behavior.
inline void yamlRejectTabIndent(const std::string& line, int lineNum) {
	for (char c : line) {
		if (c == ' ') continue;
		if (c == '\t') throw asvJSONError("YAML: tab used as indentation at line " + std::to_string(lineNum));
		break;
	}
}

// YAML requires the entries of one mapping/sequence (a single frame) to sit at
// a consistent indentation. The frame records the indent of its first entry;
// any later entry that breaks alignment is a structural error ("bad indent").
inline void yamlCheckFrameIndent(std::vector<FormatFrame>& stack, int indent) {
	if (stack.empty()) return;
	auto& top = stack.back();
	if (top.childIndent < 0) top.childIndent = indent;
	else if (indent != top.childIndent)
		throw asvJSONError("YAML: inconsistent indentation");
}

// True when a line opens (`- ` or bare `-`) a block sequence entry.
inline bool yamlIsDashStart(std::string_view s) {
	if (s.empty() || s[0] != '-') return false;
	if (s.size() == 1) return true;
	return s[1] == ' ';
}

// Find the first top-level ':' on a line, ignoring colons inside quotes and
// flow brackets. Used for both key:value detection and block-mapping lookahead.
inline std::string_view::size_type yamlFindKeyColon(std::string_view s) {
	bool dq = false, sq = false;
	int depth = 0;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (dq) { if (c == '\\' && i + 1 < s.size()) i++; else if (c == '"') dq = false; continue; }
		if (sq) { if (c == '\'' && i + 1 < s.size() && s[i + 1] == '\'') i++; else if (c == '\'') sq = false; continue; }
		if (c == '"') { dq = true; continue; }
		if (c == '\'') { sq = true; continue; }
		if (c == '[' || c == '{') { depth++; continue; }
		if (c == ']' || c == '}') { depth--; continue; }
		if (c == ':' && depth == 0) return i;
	}
	return std::string_view::npos;
}

// A top-level ':' is a mapping separator only when followed by space, tab, or
// end-of-line (YAML plain-scalar rule). "a:b", "10:30", "http://x:y" contain a
// colon but are plain scalars, so this returns npos for them.
inline std::string_view::size_type yamlFindKeyColonSep(std::string_view s) {
	size_t c = yamlFindKeyColon(s);
	if (c == std::string_view::npos) return std::string_view::npos;
	if (c + 1 < s.size() && s[c + 1] != ' ' && s[c + 1] != '\t') return std::string_view::npos;
	return c;
}

// True when `s` begins a block mapping entry ("key: value" with a space after
// the colon, or a quoted key). Excludes URLs ("http://x"), times ("10:30"),
// flow collections and block-scalar headers.
inline bool yamlIsBlockMappingStart(std::string_view s) {
	if (s.empty() || s[0] == '|' || s[0] == '>') return false;
	// Explicit-key form "? key" (and a bare "?") starts a block mapping entry.
	if (s[0] == '?' && (s.size() == 1 || s[1] == ' ' || s[1] == '\t')) return true;
	auto colon = yamlFindKeyColon(s);
	if (colon == std::string_view::npos || colon == 0) return false;
	if (colon + 1 < s.size() && s[colon + 1] != ' ' && s[colon + 1] != '\t') return false;
	return true;
}

// True when a line starts a block scalar ("|", ">", or a value that begins
// with one), i.e. the lines below it are scalar BODY text, not structure.
inline bool yamlIsBlockScalarHeader(std::string_view content) {
	if (content.size() >= 1 && (content[0] == '|' || content[0] == '>')) return true;
	if (content.size() >= 2 && content[0] == '-' && content[1] == ' ') {
		std::string_view rest = content.substr(2);
		while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);
		return !rest.empty() && (rest[0] == '|' || rest[0] == '>');
	}
	auto colon = yamlFindKeyColon(content);
	if (colon != std::string_view::npos && colon + 1 < content.size()) {
		size_t v = colon + 1;
		while (v < content.size() && (content[v] == ' ' || content[v] == '\t')) v++;
		return v < content.size() && (content[v] == '|' || content[v] == '>');
	}
	return false;
}

// True when `s` is a tag token with no value content around it (only optional
// whitespace / a trailing comment / a block-scalar header). Such a solo tag
// annotates the block node that starts on the following lines
// ("x: !!map\n  a: 1", "x: !!binary |\n  aGVsbG8=").
inline bool yamlIsSoloTag(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
	if (s.empty() || s[0] != '!') return false;
	size_t tagEnd = 0;
	if (s.size() >= 2 && s[1] == '!') {
		tagEnd = 2;
		while (tagEnd < s.size() && s[tagEnd] != ' ' && s[tagEnd] != '\t') tagEnd++;
	} else if (s.size() >= 2 && s[1] == '<') {
		tagEnd = s.find('>', 2);
		if (tagEnd == std::string_view::npos) tagEnd = s.size(); else tagEnd++;
	} else {
		tagEnd = 1;
		while (tagEnd < s.size() && s[tagEnd] != ' ' && s[tagEnd] != '\t') tagEnd++;
	}
	std::string_view rem = s.substr(tagEnd);
	while (!rem.empty() && (rem[0] == ' ' || rem[0] == '\t')) rem.remove_prefix(1);
	if (rem.empty() || rem[0] == '#') return true;
	if (rem[0] != '|' && rem[0] != '>') return false;
	// A valid block-scalar header may follow the tag
	// ("!!binary |", "!!binary |2-", "!!str >+ # comment").
	size_t q = 1;
	while (q < rem.size() && rem[q] >= '0' && rem[q] <= '9') q++;
	if (q < rem.size() && (rem[q] == '+' || rem[q] == '-')) q++;
	while (q < rem.size() && (rem[q] == ' ' || rem[q] == '\t')) q++;
	if (q == rem.size()) return true;
	return rem[q] == '#';
}

// Return the index in `s` of the first flow-collection brace ('{' or '[')
// that follows a leading tag token, or npos when `s` is not a "tag <flow>"
// form. Used to detect root-level tagged flow values like "!!set {a, b}",
// "!!seq [1, 2]", "!!map {a:1}", or "!cust [x]".
inline size_t yamlFlowAfterTag(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
	if (s.empty() || s[0] != '!') return std::string_view::npos;
	size_t i = 1;
	if (s.size() >= 2 && s[1] == '!') {
		while (i < s.size() && s[i] != ' ' && s[i] != '\t') i++;
	} else if (s.size() >= 2 && s[1] == '<') {
		size_t c = s.find('>', 2);
		if (c == std::string_view::npos) return std::string_view::npos;
		i = c + 1;
	} else {
		while (i < s.size() && s[i] != ' ' && s[i] != '\t') i++;
	}
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
	if (i < s.size() && (s[i] == '{' || s[i] == '[')) return i;
	return std::string_view::npos;
}

// Parse the value of a solo tag that annotates a block node on deeper lines
// ("x: !!map\n  a: 1", "- !!seq\n  - 1"). `parentIndent` is the indent of the
// tag line (the block child must be strictly deeper; -1 at document root takes
// the whole document as the child). Advances `li` to the last consumed line.
// Known collection tags ("!!map"/"!!seq") pass their node through (validating
// the kind); "!!binary" reads a block literal scalar child as base64; "!"/"?"
// resolve by content (default interpretation); any other tag wraps the parsed
// child in a CUSTOM_TAG. A tag with no deeper block child yields the defaults
// matching the inline solo-tag forms ("{}"/"[]"/empty types).
inline std::string yamlTaggedBlockValue(std::string_view s,
    const std::vector<std::string>& lines, size_t& li, int parentIndent, int depth) {
	size_t tagEnd = 0;
	if (s.size() >= 2 && s[1] == '!') {
		tagEnd = 2;
		while (tagEnd < s.size() && s[tagEnd] != ' ' && s[tagEnd] != '\t') tagEnd++;
	} else if (s.size() >= 2 && s[1] == '<') {
		tagEnd = s.find('>', 2);
		if (tagEnd == std::string_view::npos) tagEnd = s.size(); else tagEnd++;
	} else {
		tagEnd = 1;
		while (tagEnd < s.size() && s[tagEnd] != ' ' && s[tagEnd] != '\t') tagEnd++;
	}
	std::string tagStr = yamlResolveTag(s.substr(0, tagEnd));

	// A block-scalar header inlined after the tag ("!!binary |", "!!str >+").
	std::string header;
	{
		size_t hstart = tagEnd;
		while (hstart < s.size() && (s[hstart] == ' ' || s[hstart] == '\t')) hstart++;
		if (hstart < s.size() && (s[hstart] == '|' || s[hstart] == '>')) {
			size_t hend = hstart + 1;
			while (hend < s.size() && s[hend] >= '0' && s[hend] <= '9') hend++;
			if (hend < s.size() && (s[hend] == '+' || s[hend] == '-')) hend++;
			while (hend < s.size() && (s[hend] == ' ' || s[hend] == '\t')) hend++;
			if (hend == s.size() || s[hend] == '#')
				header = std::string(s.substr(hstart, hend - hstart));
		}
	}

	std::string name;
	if (tagStr.size() >= 2 && tagStr[0] == '!' && tagStr[1] == '<' && tagStr.back() == '>')
		name = tagStr.substr(2, tagStr.size() - 3); // verbatim URI: strip !< >
	else if (!tagStr.empty() && tagStr[0] == '!')
		name = tagStr.substr(1);
	else
		name = tagStr;

	size_t p = li + 1;
	while (p < lines.size()) {
		std::string_view c = stripIndent(lines[p]);
		if (c.empty() || c[0] == '#') { p++; continue; }
		break;
	}
	if (p >= lines.size() || countIndent(lines[p]) <= parentIndent) {
		// No deeper child: defaults matching the inline solo-tag forms.
		if (tagStr == "!!map") return "{}";
		if (tagStr == "!!seq") return "[]";
		if (tagStr == "!!binary") return "{\"$binary\":{\"base64\":\"\",\"subType\":\"00\"}}";
		if (!tagStr.empty() && tagStr[0] == '!' && tagStr[1] == '!')
			return yamlParseTaggedValue(tagStr); // scalar core tags: 0/0.0/false/""/null/...
		if (tagStr == "!" || tagStr == "?") return "null";
		{
			// bare custom tag => CUSTOM_TAG with a null value
			std::string out = "{\"$customTag\":{\"name\":\"";
			for (auto c : name) {
				if (c == '"' || c == '\\') out += '\\';
				out += c;
			}
			out += "\",\"value\":null}}";
			return out;
		}
	}

	if (!header.empty()) {
		// Block-scalar body on the lines below the tag line ("x: !!binary |").
		// yamlParseBlockScalar reads content starting at li+1, so point it at
		// the tag line itself (the header lives on it).
		size_t sp = p - 1;
		std::string text = yamlParseBlockScalar(header, lines, sp, header[0] == '>', parentIndent);
		if (sp != p - 1) li = sp; // content consumed through line sp (else the tag line is last)
		if (tagStr == "!!binary") {
			while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n')) text.pop_back();
			std::string out = "{\"$binary\":{\"base64\":";
			out += yamlQuoteJsonScalar(text);
			out += ",\"subType\":\"00\"}}";
			return out;
		}
		if (tagStr == "!!str") return yamlQuoteJsonScalar(text);
		if (tagStr == "!" || tagStr == "?") return yamlParseInlineValue(text);
		if (!tagStr.empty() && tagStr[0] == '!' && tagStr[1] == '!') {
			// Scalar core tag over a block scalar: feed the scalar text to the
			// tagged-value parser (trailing line break trimmed for 1-line bodies).
			std::string combined = tagStr + " " + text;
			while (!combined.empty() && combined.back() == '\n') combined.pop_back();
			return yamlParseTaggedValue(combined);
		}
		{
			std::string out = "{\"$customTag\":{\"name\":\"";
			for (auto c : name) {
				if (c == '"' || c == '\\') out += '\\';
				out += c;
			}
			out += "\",\"value\":";
			out += yamlParseInlineValue(text);
			out += "}}";
			return out;
		}
	}

	if (tagStr == "!!binary") {
		std::string_view first = stripIndent(lines[p]);
		std::string b64;
		if (!first.empty() && (first[0] == '|' || first[0] == '>')) {
			bool folded = (first[0] == '>');
			b64 = yamlParseBlockScalar(std::string(first), lines, p, folded, parentIndent);
			li = p; // yamlParseBlockScalar advanced p past the content lines
		} else {
			bool firstLine = true;
			while (p < lines.size()) {
				std::string_view c = stripIndent(lines[p]);
				if (c.empty() || c[0] == '#') { p++; continue; }
				if (countIndent(lines[p]) <= parentIndent) break;
				std::string ln(c);
				while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t')) ln.pop_back();
				if (!firstLine) b64 += '\n';
				firstLine = false;
				b64 += ln;
				p++;
			}
			li = p - 1;
		}
		while (!b64.empty() && (b64.back() == ' ' || b64.back() == '\t' || b64.back() == '\n')) b64.pop_back();
		std::string out = "{\"$binary\":{\"base64\":";
		out += yamlQuoteJsonScalar(b64);
		out += ",\"subType\":\"00\"}}";
		return out;
	}

	std::vector<std::string> childLines;
	size_t q = p;
	while (q < lines.size()) {
		std::string_view c = stripIndent(lines[q]);
		if (c.empty() || c[0] == '#') { childLines.push_back(lines[q]); q++; continue; }
		if (countIndent(lines[q]) <= parentIndent) break;
		childLines.push_back(lines[q]);
		q++;
	}
	std::string childJson = yamlParseDocImpl(childLines, depth + 1);
	li = q - 1;

	if (tagStr == "!!map" || tagStr == "!!seq") {
		char want = (tagStr == "!!map") ? '{' : '[';
		if (childJson.empty() || childJson[0] != want)
			throw asvJSONError(std::string("YAML: ") + tagStr + " tag applied to a non-" +
			    ((tagStr == "!!map") ? "mapping" : "sequence") + " value");
		return childJson;
	}
	if (tagStr == "!" || tagStr == "?") return childJson;
	if (!tagStr.empty() && tagStr[0] == '!' && tagStr[1] == '!')
		throw asvJSONError(std::string("YAML: ") + tagStr + " tag applied to a non-scalar value");

	std::string out = "{\"$customTag\":{\"name\":\"";
	for (auto c : name) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	out += "\",\"value\":";
	out += childJson;
	out += "}}";
	return out;
}

// Rewrite block-sequence items whose child node starts inline on the dash line
// ("- a: 1", "- a: [1, 2]", "- - x") into the canonical two-line form:
//   -
//     a: 1
// This lets the main parser treat objects and nested sequences as sequence
// items uniformly via the empty-dash lookahead. Block-scalar bodies and the
// inside of multi-line flow collections are left untouched.
inline void yamlNormalizeSeqItems(std::vector<std::string>& lines) {
	int bsHeaderIndent = -1; // >= 0 while inside a block-scalar body
	int flowDepth = 0;       // > 0 while inside a multi-line flow collection
	auto scanFlow = [&](std::string_view s) {
		bool dq = false, sq = false;
		for (char c : s) {
			if (dq) { if (c == '\\') continue; if (c == '"') dq = false; continue; }
			if (sq) { if (c == '\'') sq = false; continue; }
			if (c == '"') { dq = true; continue; }
			if (c == '\'') { sq = true; continue; }
			if (c == '{' || c == '[') flowDepth++;
			else if (c == '}' || c == ']') flowDepth--;
		}
	};
	size_t i = 0;
	while (i < lines.size()) {
		std::string& line = lines[i];
		int indent = countIndent(line);
		std::string_view content = stripIndent(line);
		if (bsHeaderIndent >= 0) {
			// Skip the body of a block scalar; body text is not structure.
			if (content.empty() || content[0] == '#') { i++; continue; }
			if (indent > bsHeaderIndent) { i++; continue; }
			bsHeaderIndent = -1; // scalar ended; process this line normally
		}
		if (content.empty() || content[0] == '#') { i++; continue; }

		if (flowDepth > 0) {
			// Inside a multi-line flow collection: no block sequence here.
			scanFlow(content);
			i++;
			continue;
		}
		scanFlow(content);
		if (flowDepth > 0) { i++; continue; }

		if (content[0] == '-' && content.size() >= 2 && content[1] == ' ') {
			std::string_view rest = content.substr(2);
			while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);
			if (!rest.empty() && (rest[0] == '|' || rest[0] == '>')) {
				bsHeaderIndent = indent;
				i++;
				continue;
			}
			if (!rest.empty()) {
				bool nested = yamlIsDashStart(rest);
				bool mapping = !nested && yamlIsBlockMappingStart(rest);
				if (nested || mapping) {
					std::string restCopy(rest);
					line = std::string(indent, ' ') + "-";
					lines.insert(lines.begin() + (std::ptrdiff_t)(i + 1),
					             std::string(indent + 2, ' ') + restCopy);
					continue; // reprocess the inserted line in place
				}
			}
			i++;
			continue;
		}

		if (yamlIsBlockScalarHeader(content)) { bsHeaderIndent = indent; i++; continue; }
		i++;
	}
}

// Parse a single YAML document (no multi-doc handling)
inline std::string yamlParseDoc(std::string_view input, int depth) {
	auto lines = splitLines(input);
	if (lines.empty()) return "{}";
	return yamlParseDocImpl(lines, depth);
}

inline std::string yamlParseDocImpl(std::vector<std::string>& lines, int depth) {
	// Restore the previous tag-map state on exit (even on exception).
	YamlDocTagMapGuard tagGuard;

	// Scan for directives (%YAML, %TAG) at the beginning of the document
	// Directives are at indent 0 and precede any content
	size_t dirEnd = 0;
	while (dirEnd < lines.size()) {
		std::string_view c = stripIndent(lines[dirEnd]);
		if (c.empty() || c[0] == '#') { dirEnd++; continue; }
		if (countIndent(lines[dirEnd]) > 0 || c[0] != '%') break;
		if (c.size() >= 5 && c.substr(0, 5) == "%TAG ") {
			// %TAG handle prefix
			std::string_view rest = c.substr(5);
			size_t pos = 0;
			while (pos < rest.size() && rest[pos] == ' ') pos++;
			if (pos < rest.size() && rest[pos] == '!') {
				size_t hStart = pos; pos++;
				if (pos < rest.size() && rest[pos] == '!') pos++;
				else if (pos < rest.size() && rest[pos] != ' ') {
					while (pos < rest.size() && rest[pos] != ' ' && rest[pos] != '!') pos++;
					if (pos < rest.size() && rest[pos] == '!') pos++;
				}
				std::string_view handle = rest.substr(hStart, pos - hStart);
				while (pos < rest.size() && rest[pos] == ' ') pos++;
				std::string_view pfx = rest.substr(pos);
				size_t hash = pfx.find('#');
				if (hash != std::string_view::npos) pfx = pfx.substr(0, hash);
				while (!pfx.empty() && (pfx.back() == ' ' || pfx.back() == '\t')) pfx.remove_suffix(1);
				if (!pfx.empty()) yamlDocTagMap[std::string(handle)] = std::string(pfx);
			}
		}
		// %YAML -- silently skip any version
		dirEnd++;
	}
	// Strip directive lines so they don't reach the main parser
	if (dirEnd > 0) lines.erase(lines.begin(), lines.begin() + (std::ptrdiff_t)dirEnd);

	// Normalize block-sequence items that inline their child node ("- a: 1",
	// "- - x") into the canonical "dash alone" form so objects and nested
	// sequences parse correctly as sequence items.
	yamlNormalizeSeqItems(lines);

	// Pre-scan for anchors (each document has independent anchor namespace).
	// Skip the scan entirely when no '&' appears anywhere: aliases then have
	// nothing to resolve to and fail later exactly as they would with an empty
	// map, and block/flow anchors are impossible without the character.
	std::unordered_map<std::string, std::string> anchors;
	std::unordered_set<std::string> resolving;
	bool hasAmp = false;
	for (const auto& ln : lines) {
		if (ln.find('&') != std::string::npos) { hasAmp = true; break; }
	}
	if (hasAmp) yamlBuildAnchorMap(lines, anchors, depth);

	std::string out;
	std::vector<FormatFrame> stack;
	std::string pendingKey; // explicit "? key" awaiting its ": value" line
	int pendingIndent = -1; // indent of the "? key" line (for multi-line key folding)

	// Find first meaningful line to establish root indent
	size_t firstLine = 0;
	while (firstLine < lines.size()) {
		std::string_view c = stripIndent(lines[firstLine]);
		if (c.empty() || c[0] == '#') firstLine++;
		else break;
	}
	if (firstLine >= lines.size()) return "{}";

	{
		std::string_view fc = stripIndent(lines[firstLine]);
		if (fc == "{}") return "{}";
		if (fc == "[]") return "[]";
		// Non-empty flow collection at root level: parse directly. A flow
		// collection followed by a depth-0 ": " separator ("[a, b]: v") is NOT
		// a flow document -- it is a block mapping with a compound (flow) key.
		if (!fc.empty() && (fc[0] == '{' || fc[0] == '[') &&
		    yamlFindKeyColonSep(fc) == std::string_view::npos) {
			size_t flowLi = firstLine;
			std::string flowText = yamlGatherFlow(lines, fc, flowLi);
			size_t fp = 0;
			std::string got = yamlParseFlowValue(flowText, fp, &anchors, &resolving, 0);
			if (fp < flowText.size()) throw asvJSONError("YAML: trailing content after flow value");
			return got;
		}
		// Tagged flow value at document root ("!!set {a, b}", "!!seq [1, 2]",
		// "!!map {a:1}", "!cust [x]"). Gather the flow collection, then apply
		// the tag (which re-parses and dispatches: !!set -> $set marker,
		// !!map/!!seq validate kind, custom tags wrap in CUSTOM_TAG, etc.).
		{
			size_t flowPos = yamlFlowAfterTag(fc);
			if (flowPos != std::string_view::npos) {
				std::string tagPart = std::string(fc.substr(0, flowPos));
				while (!tagPart.empty() && (tagPart.back() == ' ' || tagPart.back() == '\t')) tagPart.pop_back();
				size_t fLi = firstLine;
				std::string flowText = yamlGatherFlow(lines, std::string(fc.substr(flowPos)), fLi);
				std::string tagged = yamlParseTaggedValue(tagPart + " " + flowText);
				for (size_t t = fLi + 1; t < lines.size(); t++) {
					std::string_view tc = stripIndent(lines[t]);
					if (tc.empty() || tc[0] == '#') continue;
					throw asvJSONError("YAML: unexpected content after tagged value");
				}
				return tagged;
			}
		}
		// Block-style !!set at document root ("!!set\n- a\n- b")
		if (yamlIsSetBlockHeader(fc)) {
			size_t sLi = firstLine;
			std::string sgot = yamlParseBlockSet(lines, sLi, &anchors, &resolving, depth);
			// The set is the whole document: trailing content would be dropped
			// silently, so reject it.
			for (size_t t = sLi + 1; t < lines.size(); t++) {
				std::string_view tc = stripIndent(lines[t]);
				if (tc.empty() || tc[0] == '#') continue;
				throw asvJSONError("YAML: unexpected content after block set");
			}
			return sgot;
		}
		// Solo tag at document root annotates the whole document node
		// ("!!map\n a: 1", "!!seq\n- 1", "!mytype\n k: v")
		if (yamlIsSoloTag(fc)) {
			std::string sfc(fc);
			std::string ytail;
			if (yamlYamlTagNeedsGather(sfc, ytail)) {
				size_t yLi = firstLine;
				std::string ygot = yamlGatherYamlEmbed(ytail, lines, yLi, -1);
				for (size_t t = yLi + 1; t < lines.size(); t++) {
					std::string_view tc = stripIndent(lines[t]);
					if (tc.empty() || tc[0] == '#') continue;
					throw asvJSONError("YAML: unexpected content after !!yaml value");
				}
				return ygot;
			}
			size_t tLi = firstLine;
			std::string got = yamlTaggedBlockValue(sfc, lines, tLi, -1, depth);
			for (size_t t = tLi + 1; t < lines.size(); t++) {
				std::string_view tc = stripIndent(lines[t]);
				if (tc.empty() || tc[0] == '#') continue;
				throw asvJSONError("YAML: unexpected content after tagged value");
			}
			return got;
		}
	}

	bool rootIsArr = false;
	{
		std::string_view fc = stripIndent(lines[firstLine]);
		if (yamlIsDashStart(fc)) rootIsArr = true;
	}

	int rootIndent = countIndent(lines[firstLine]);

	// Pre-scan to determine root type
	if (rootIsArr) {
		out += '[';
		FormatFrame f; f.type = 'A'; f.align = rootIndent; f.first = true; f.hasVal = true; f.isRoot = true;
		if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
		stack.push_back(f);
	} else {
		out += '{';
		FormatFrame f; f.type = 'O'; f.align = rootIndent; f.first = true; f.hasVal = true; f.isRoot = true;
		if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
		stack.push_back(f);
	}

	size_t maxLine = lines.size();
	for (size_t li = firstLine; li < maxLine; li++) {
		int yamlLineNum = static_cast<int>(li) + 1;
		try {
		int indent = countIndent(lines[li]);
		yamlRejectTabIndent(lines[li], yamlLineNum);
		std::string content = std::string(stripIndent(lines[li]));
		if (content.empty() || content[0] == '#') continue;

		// Strip trailing comment (respecting quotes)
		content = std::string(yamlStripComment(content));
		while (!content.empty() && (content.back() == ' ' || content.back() == '\t')) content.pop_back();
		if (content.empty()) continue;

		// Strip &anchor tags (they were already captured by pre-scan)
		// Must check yamlAmpInQuote to avoid removing & inside quoted strings
		{
			std::string cleaned;
			size_t i = 0;
			while (i < content.size()) {
				if (content[i] == '&' && i + 1 < content.size() && yamlIsAnchorChar(content[i + 1]) && !yamlAmpInQuote(content, i)) {
					if (!yamlInFlowBrackets(content, i)) {
						size_t ns = i + 1;
						size_t ne = ns;
						while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
						i = ne;
						while (i < content.size() && (content[i] == ' ' || content[i] == '\t')) i++;
						continue;
					}
				}
				cleaned += content[i++];
			}
			while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '\t')) cleaned.pop_back();
			content = cleaned;
		}
		if (content.empty()) continue;

		// A pending explicit key ("? key") must be satisfied by a ": value" line.
		if (!pendingKey.empty() && yamlFindKeyColon(content) != 0) {
			// A deeper-indented line folds into a multi-line explicit key
			// (YAML plain-scalar continuation). Any other non-": value" content
			// is an error.
			if (indent > pendingIndent) {
				pendingKey += " " + yamlEscKey(content);
				continue;
			}
			throw asvJSONError("YAML: explicit key must be followed by ': value'");
		}

		// Handle flow containers {} and [] at any level
		if (content == "{}") {
			closeFrames(stack, out, indent);
			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;
			out += "{}";
			continue;
		}
		if (content == "[]") {
			closeFrames(stack, out, indent);
			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;
			out += "[]";
			continue;
		}

		if (content[0] == '?' && (content.size() == 1 || content[1] == ' ')) {
			// --- Explicit key: "? key" (value inline after ':' or on a later
			//     ": value" line). Scalar keys stringify to JSON; flow-collection
			//     (compound) keys are stringified deterministically via their
			//     intermediate JSON text. ---
			closeFrames(stack, out, indent);

			std::string rest;
			if (content.size() > 1) rest = content.substr(1);
			while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.erase(0, 1);

			// "?" with no inline key: the explicit key is a node on the
			// following (deeper) block lines, e.g.
			//   ?
			//     - a
			//     - b
			//   : v
			// Structure keys (block sequence/mapping, flow collection) are
			// stringified via their intermediate JSON just like flow compound
			// keys; scalar keys are folded per YAML plain-scalar rules.
			if (rest.empty()) {
				size_t keyStart = li + 1;
				while (keyStart < lines.size()) {
					std::string_view ksc = stripIndent(lines[keyStart]);
					if (ksc.empty() || ksc[0] == '#') keyStart++;
					else break;
				}
				if (keyStart < lines.size() && countIndent(lines[keyStart]) > indent) {
					size_t keyEnd = keyStart;
					while (keyEnd < lines.size()) {
						std::string_view kec = stripIndent(lines[keyEnd]);
						if (kec.empty() || kec[0] == '#') { keyEnd++; continue; }
						if (countIndent(lines[keyEnd]) <= indent) break;
						keyEnd++;
					}
					std::vector<std::string> keyLines;
					for (size_t kl = keyStart; kl < keyEnd; kl++) keyLines.push_back(lines[kl]);
					if (!keyLines.empty()) {
						std::string scalarDoc;
						for (const auto& kl : keyLines) {
							if (!scalarDoc.empty()) scalarDoc += '\n';
							scalarDoc += kl;
						}
						bool isScalarNode = false;
						std::string scalarJson = yamlScalarDocValue(scalarDoc, isScalarNode);
						if (isScalarNode) {
							// Scalar key node: keep the escaped content of the
							// JSON string fragment (strip its surrounding quotes);
							// non-string scalars (numbers, booleans) pass through
							// and are stringified on merge.
							std::string keyText = scalarJson;
							if (keyText.size() >= 2 && keyText.front() == '"' && keyText.back() == '"')
								keyText = keyText.substr(1, keyText.size() - 2);
							pendingKey = keyText;
						} else {
							std::string keyJson = yamlParseDocImpl(keyLines, depth + 1);
							pendingKey = yamlEscJsonText(keyJson);
						}
						pendingIndent = indent;
						li = keyEnd - 1;
						continue;
					}
				}
			}

			// Split "key" from an inline "key: value". Only a ':' followed by a
			// space/end may terminate a plain scalar key (YAML plain-scalar rule).
			size_t kc = std::string::npos;
			{
				bool dq = false, sq = false;
				int depth = 0;
				for (size_t i = 0; i < rest.size(); i++) {
					char c = rest[i];
					if (dq) { if (c == '\\' && i + 1 < rest.size()) i++; else if (c == '"') dq = false; continue; }
					if (sq) { if (c == '\'' && i + 1 < rest.size() && rest[i + 1] == '\'') i++; else if (c == '\'') sq = false; continue; }
					if (c == '"') { dq = true; continue; }
					if (c == '\'') { sq = true; continue; }
					if (c == '[' || c == '{') { depth++; continue; }
					if (c == ']' || c == '}') { depth--; continue; }
					if (c == ':' && depth == 0 && (i + 1 >= rest.size() || rest[i + 1] == ' ' || rest[i + 1] == '\t')) { kc = i; break; }
				}
			}

			std::string keyBody = rest;
			std::string inlineVal;
			if (kc != std::string::npos) {
				keyBody = rest.substr(0, kc);
				while (!keyBody.empty() && (keyBody.back() == ' ' || keyBody.back() == '\t')) keyBody.pop_back();
				inlineVal = rest.substr(kc + 1);
				while (!inlineVal.empty() && (inlineVal[0] == ' ' || inlineVal[0] == '\t')) inlineVal.erase(0, 1);
			}

			// Compound (flow-collection) key. An unbalanced key body spans
			// following lines; gather them, then stringify the whole node.
			bool isCompoundKey = !keyBody.empty() && (keyBody[0] == '{' || keyBody[0] == '[');
			std::string compoundKey;
			if (isCompoundKey) {
				if (yamlFlowDepth(keyBody) > 0)
					keyBody = yamlGatherFlow(lines, keyBody, li);
				compoundKey = yamlFlowJsonAsKey(keyBody, &anchors, &resolving, 0);
			}

			std::string rawKey;
			if (keyBody.size() >= 2 && keyBody[0] == '"' && keyBody.back() == '"') {
				rawKey = yamlUnescapeDouble(keyBody.substr(1, keyBody.size() - 2));
			} else if (keyBody.size() >= 2 && keyBody[0] == '\'' && keyBody.back() == '\'') {
				rawKey = yamlUnescapeSingle(keyBody.substr(1, keyBody.size() - 2));
			} else if (keyBody.empty()) {
				rawKey.clear();
			} else {
				rawKey = keyBody;
			}

			if (stack.empty() || stack.back().type != 'O') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
				out += '{';
				FormatFrame f; f.type = 'O'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
				stack.push_back(f);
			}
			yamlCheckFrameIndent(stack, indent);

			std::string jsonKey = isCompoundKey ? ("\"" + compoundKey + "\"") : ("\"" + yamlEscKey(rawKey) + "\"");
			if (!inlineVal.empty() && inlineVal[0] != '#') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				out += jsonKey + ":";
				if (inlineVal[0] == '|' || inlineVal[0] == '>') {
					bool isFolded = (inlineVal[0] == '>');
					std::string text = yamlParseBlockScalar(inlineVal, lines, li, isFolded, indent);
					out += yamlQuoteJsonScalar(text);
				} else if (inlineVal[0] == '{' || inlineVal[0] == '[') {
					std::string flowText = yamlGatherFlow(lines, inlineVal, li);
					size_t fp = 0;
					std::string got = yamlParseFlowValue(flowText, fp, &anchors, &resolving, 0);
					if (fp < flowText.size()) throw asvJSONError("YAML: trailing content after flow value");
					out += got;
				} else if (inlineVal[0] == '!' && inlineVal.size() > 1) {
					if (yamlIsSetBlockHeader(inlineVal)) {
						size_t sLi = li;
						out += yamlParseBlockSet(lines, sLi, &anchors, &resolving, depth);
						li = sLi;
					} else {
						std::string yt;
						if (yamlYamlTagNeedsGather(inlineVal, yt))
							out += yamlGatherYamlEmbed(yt, lines, li, indent);
						else if (yamlIsSoloTag(inlineVal))
							out += yamlTaggedBlockValue(inlineVal, lines, li, indent, depth);
						else
							out += yamlParseTaggedValue(inlineVal);
					}
				} else if (inlineVal.size() > 1 && inlineVal[0] == '*') {
					out += yamlResolveAlias(std::string_view(inlineVal).substr(1), anchors, resolving);
				} else {
					out += yamlParseInlineValue(inlineVal);
				}
			} else {
				pendingKey = isCompoundKey ? compoundKey : yamlEscKey(rawKey);
				pendingIndent = indent;
			}
			continue;
		}

		if (yamlIsDashStart(content)) {
			// --- Array item ---
			while (!stack.empty()) {
				auto& top = stack.back();
				if (top.isRoot && indent <= top.align) break;
				if (indent >= static_cast<int>(top.align)) break;
				out += (top.type == 'A') ? ']' : '}';
				stack.pop_back();
			}

			bool pushedA = false;
			if (stack.empty() || stack.back().type != 'A') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
				out += '[';
				FormatFrame f; f.type = 'A'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
				stack.push_back(f);
				pushedA = true;
			}
			if (pushedA) {
				// A block sequence opened inside an open mapping level mixes
				// structures: "a: 1\n- b". (A sequence as a mapping value is
				// always opened by the empty-value lookahead, not here.)
				if (stack.size() >= 2 && stack[stack.size() - 2].type == 'O')
					throw asvJSONError("YAML: sequence and mapping mixed at the same level");
			} else {
				yamlCheckFrameIndent(stack, indent);
			}

			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;

			std::string rest;
			if (content.size() >= 2) rest = content.substr(2);
			while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

			if (!rest.empty() && rest[0] == ':' &&
			    (rest.size() == 1 || rest[1] == ' ' || rest[1] == '\t')) {
				throw asvJSONError("YAML: mapping key expected before ':'");
			}

			if (rest.empty()) {
				// Bare dash: the item may be an object or nested sequence whose
				// children start on the following deeper-indented lines. Both
				// shapes are produced by the block encoder ("- \n  key: val"
				// and the normalized "- \n  - x" form).
				size_t sc = li + 1;
				while (sc < lines.size()) {
					std::string_view scv = stripIndent(lines[sc]);
					if (scv.empty() || scv[0] == '#') { sc++; continue; }
					break;
				}
				bool opened = false;
				if (sc < lines.size() && countIndent(lines[sc]) > indent) {
					std::string_view scv = stripIndent(lines[sc]);
					if (yamlIsBlockMappingStart(scv)) {
						if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
						out += '{';
						FormatFrame f; f.type = 'O'; f.align = indent + 1; f.first = true; f.hasVal = true; f.isRoot = false;
						stack.push_back(f);
						opened = true;
					} else if (yamlIsDashStart(scv)) {
						if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
						out += '[';
						FormatFrame f; f.type = 'A'; f.align = indent + 1; f.first = true; f.hasVal = true; f.isRoot = false;
						stack.push_back(f);
						opened = true;
					}
				}
				if (!opened) out += "null";
			} else if (rest.size() > 1 && rest[0] == '*') {
				out += yamlResolveAlias(rest.substr(1), anchors, resolving);
			} else if (rest.size() > 1 && rest[0] == '!') {
				if (yamlIsSetBlockHeader(rest)) {
					size_t sLi = li;
					out += yamlParseBlockSet(lines, sLi, &anchors, &resolving, depth);
					li = sLi;
				} else {
					std::string ytail;
					if (yamlYamlTagNeedsGather(rest, ytail))
						out += yamlGatherYamlEmbed(ytail, lines, li, indent);
					else if (yamlIsSoloTag(rest))
						out += yamlTaggedBlockValue(rest, lines, li, indent, depth);
					else
						out += yamlParseInlineValue(rest);
				}
			} else if (!rest.empty() && (rest[0] == '|' || rest[0] == '>')) {
				bool isFolded = (rest[0] == '>');
				std::string text = yamlParseBlockScalar(rest, lines, li, isFolded, indent);
				std::string escaped;
				for (auto c : text) {
					switch (c) {
						case '"': escaped += "\\\""; break;
						case '\\': escaped += "\\\\"; break;
						case '\n': escaped += "\\n"; break;
						case '\t': escaped += "\\t"; break;
						default: escaped += c;
					}
				}
				out += "\"" + escaped + "\"";
			} else {
				out += yamlParseInlineValue(rest);
			}
			continue;
		}

		// --- Block scalar "|" (literal) or ">" (folded) ---
		// Parse the header (style char, optional indent indicator [1-9], optional
		// chomp indicator +|-), collect content lines, fold if needed, then apply
		// chomping: clip (default) keeps one trailing line break, '-' strips all,
		// '+' keeps all (YAML 1.2 spec).
		if (content[0] == '|' || content[0] == '>') {
			bool isFolded = (content[0] == '>');
			closeFrames(stack, out, indent);

			std::string text = yamlParseBlockScalar(content, lines, li, isFolded, -1);

			addComma(stack, out);
			if (!stack.empty()) stack.back().first = false;

			std::string escaped;
			for (auto c : text) {
				switch (c) {
					case '"': escaped += "\\\""; break;
					case '\\': escaped += "\\\\"; break;
					case '\n': escaped += "\\n"; break;
					case '\t': escaped += "\\t"; break;
					default: escaped += c;
				}
			}
			out += "\"" + escaped + "\"";
			continue;
		}

		// --- Key-value pair ---
		// A ':' is a key separator only when followed by space, tab, or EOL
		// (YAML plain-scalar rule): "a:b", "10:30", "http://x:y" are plain
		// scalars, not mappings.
		size_t colonPos = yamlFindKeyColonSep(content);
		if (colonPos != std::string::npos) {
			closeFrames(stack, out, indent);

			std::string keyPart = content.substr(0, colonPos);
			while (!keyPart.empty() && (keyPart.back() == ' ' || keyPart.back() == '\t')) keyPart.pop_back();

			std::string valPart;
			if (colonPos + 1 < content.size()) valPart = content.substr(colonPos + 1);
			while (!valPart.empty() && (valPart[0] == ' ' || valPart[0] == '\t')) valPart.erase(0, 1);

			// A ": value" line after an explicit "? key" carries the pending key.
			bool pendingMerged = false;
			if (colonPos == 0 && !pendingKey.empty()) {
				pendingMerged = true;
				keyPart.clear();
			} else if (colonPos == 0) {
				throw asvJSONError("YAML: mapping key expected before ':'");
			}

			// Compound (flow-collection) key: stringify the flow node via its
			// intermediate JSON text ("[a, b]" -> key ["a","b"]).
			bool isCompoundKey = !keyPart.empty() && (keyPart[0] == '{' || keyPart[0] == '[');
			std::string compoundKey;
			if (isCompoundKey)
				compoundKey = yamlFlowJsonAsKey(keyPart, &anchors, &resolving, 0);

			std::string rawKey;
			if (keyPart.size() >= 2 && keyPart[0] == '"' && keyPart.back() == '"') {
				rawKey = yamlUnescapeDouble(keyPart.substr(1, keyPart.size() - 2));
			} else if (keyPart.size() >= 2 && keyPart[0] == '\'' && keyPart.back() == '\'') {
				rawKey = yamlUnescapeSingle(keyPart.substr(1, keyPart.size() - 2));
			} else {
				rawKey = std::string(keyPart);
			}

			bool pushedO = false;
			if (stack.empty() || stack.back().type != 'O') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
				out += '{';
				FormatFrame f; f.type = 'O'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
				stack.push_back(f);
				pushedO = true;
			}
			if (pushedO) {
				// A mapping started directly inside an open block sequence mixes
				// structures at one level, which YAML forbids: "a: 1\n- b" etc.
				if (stack.size() >= 2 && stack[stack.size() - 2].type == 'A')
					throw asvJSONError("YAML: sequence and mapping mixed at the same level");
			} else {
				yamlCheckFrameIndent(stack, indent);
			}

			std::string jsonKey;
			if (pendingMerged) { jsonKey = pendingKey; pendingKey.clear(); }
			else if (isCompoundKey) jsonKey = compoundKey;
			else jsonKey = yamlEscKey(rawKey);

			// A plain-scalar value must not start with the ':' indicator
			// ("a: : b", "a: :"): it would be an empty-keyed nested pair.
			if (!valPart.empty() && valPart[0] == ':' &&
			    (valPart.size() == 1 || valPart[1] == ' ' || valPart[1] == '\t')) {
				throw asvJSONError("YAML: expected value after ':'");
			}

			if (!valPart.empty() && valPart[0] != '#') {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				out += "\"" + jsonKey + "\":";

				if (valPart[0] == '|' || valPart[0] == '>') {
					bool isFolded = (valPart[0] == '>');
					std::string text = yamlParseBlockScalar(valPart, lines, li, isFolded, indent);
					std::string escaped;
					for (auto c : text) {
						switch (c) {
							case '"': escaped += "\\\""; break;
							case '\\': escaped += "\\\\"; break;
							case '\n': escaped += "\\n"; break;
							case '\t': escaped += "\\t"; break;
							default: escaped += c;
						}
					}
					out += "\"" + escaped + "\"";
				} else if (valPart[0] == '{' || valPart[0] == '[') {
					std::string flowText = yamlGatherFlow(lines, valPart, li);
					size_t fp = 0;
					std::string got = yamlParseFlowValue(flowText, fp, &anchors, &resolving, 0);
					if (fp < flowText.size()) throw asvJSONError("YAML: trailing content after flow value");
					out += got;
				} else if (valPart[0] == '!' && valPart.size() > 1) {
					if (yamlIsSetBlockHeader(valPart)) {
						size_t sLi = li;
						out += yamlParseBlockSet(lines, sLi, &anchors, &resolving, depth);
						li = sLi;
					} else {
						std::string ytail;
						if (yamlYamlTagNeedsGather(valPart, ytail))
							out += yamlGatherYamlEmbed(ytail, lines, li, indent);
						else if (yamlIsSoloTag(valPart))
							out += yamlTaggedBlockValue(valPart, lines, li, indent, depth);
						else
							out += yamlParseTaggedValue(valPart);
					}
				} else if (valPart.size() > 1 && valPart[0] == '*') {
					out += yamlResolveAlias(std::string_view(valPart).substr(1), anchors, resolving);
				} else {
					out += yamlParseInlineValue(valPart);
				}
			} else {
				addComma(stack, out);
				if (!stack.empty()) stack.back().first = false;
				out += "\"" + jsonKey + "\":";

				bool isArrayValue = false;
				bool isFlowValue = false;
				bool deeperScalar = false;
				bool deeperBlock = false; // deeper line is a "|"/">" block-scalar header
				bool deeperMapping = false; // deeper line is a "key: value" pair
				size_t scan = li + 1;
				while (scan < lines.size()) {
					std::string_view sc = stripIndent(lines[scan]);
					if (sc.empty() || sc[0] == '#') { scan++; continue; }
					int scIndent = countIndent(lines[scan]);
					if (scIndent < indent) break;
					if (sc.size() >= 1 && (sc[0] == '{' || sc[0] == '[')) {
						// A deeper flow KEYED MAPPING ("[a,b]: v") is a mapping;
						// any other flow node is the whole value itself.
						if (yamlFindKeyColonSep(sc) != std::string_view::npos) deeperMapping = true;
						else isFlowValue = true;
						break;
					}
					if (scIndent == indent) {
						if (yamlIsDashStart(sc)) isArrayValue = true;
						break;
					}
					if (sc.size() >= 1 && sc[0] == '?' &&
					    (sc.size() == 1 || sc[1] == ' ' || sc[1] == '\t')) { deeperMapping = true; break; }
					if (yamlIsDashStart(sc)) { isArrayValue = true; break; }
					if (yamlFindKeyColonSep(sc) != std::string_view::npos) { deeperMapping = true; break; } // deeper mapping
					deeperScalar = true;
					deeperBlock = yamlIsBlockScalarHeader(sc);
					break;
				}

				if (isFlowValue) {
					// Output flow value directly as value for this key
					std::string flowText = yamlGatherFlow(lines, std::string(stripIndent(lines[scan])), scan);
					size_t fp = 0;
					std::string got = yamlParseFlowValue(flowText, fp, &anchors, &resolving, 0);
					if (fp < flowText.size()) throw asvJSONError("YAML: trailing content after flow value");
					out += got;
					li = scan; // skip flow lines
					stack.back().hasVal = true;
				} else if (isArrayValue) {
					if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
					out += '[';
					FormatFrame f; f.type = 'A'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
					stack.push_back(f);
				} else if (deeperScalar) {
					// The value is a bare scalar / tagged value / alias /
					// block scalar written on the following deeper line:
					//   key:
					//     some scalar
					if (deeperBlock) {
						bool isFolded = (stripIndent(lines[scan])[0] == '>');
						std::string text = yamlParseBlockScalar(std::string(stripIndent(lines[scan])), lines, scan, isFolded, indent);
						out += yamlQuoteJsonScalar(text);
					} else {
						std::string dc(stripIndent(lines[scan]));
						if (dc.size() > 1 && dc[0] == '*') out += yamlResolveAlias(std::string_view(dc).substr(1), anchors, resolving);
						else if (dc.size() > 1 && dc[0] == '!') {
							if (yamlIsSoloTag(dc))
								out += yamlTaggedBlockValue(dc, lines, scan, indent, depth);
							else
								out += yamlParseTaggedValue(dc);
						}
						else out += yamlParseInlineValue(dc);
					}
					li = scan; // consume the value line
					stack.back().hasVal = true;
				} else if (deeperMapping) {
					// The value is a mapping whose entries follow on deeper lines:
					//   key:
					//     sub: value
					if (stack.size() >= MAX_YAML_DEPTH) throw asvJSONError("YAML: maximum nesting depth exceeded");
					out += '{';
					FormatFrame f; f.type = 'O'; f.align = indent; f.first = true; f.hasVal = true; f.isRoot = false;
					stack.push_back(f);
				} else {
					// Nothing follows "key:" -- YAML implicit null
					out += "null";
					stack.back().hasVal = true;
				}
			}
			continue;
		}

		// --- Bare value ---
		closeFrames(stack, out, indent);
		addComma(stack, out);
		if (!stack.empty()) stack.back().first = false;
		if (content.size() > 1 && content[0] == '*') {
			out += yamlResolveAlias(std::string_view(content).substr(1), anchors, resolving);
		} else {
			out += yamlParseInlineValue(content);
		}
		} catch (const asvJSONError& e) {
			throw asvJSONError(std::string(e.what()) + " (line " + std::to_string(yamlLineNum) + ")");
		}
	}

	// Close all remaining frames
	while (!stack.empty()) {
		out += (stack.back().type == 'A') ? ']' : '}';
		stack.pop_back();
	}

	if (out.empty()) return "{}";
	return out;
}

// Detect a "---" (isStart) / "..." (isEnd) document marker at column 0.
// Only trailing whitespace and an optional "#" comment may follow the marker;
// "--- foo" stays a content line.
inline bool yamlIsBoundaryMarker(std::string_view s, bool isStart) {
	if (s.size() < 3) return false;
	std::string_view m = isStart ? "---" : "...";
	if (s.compare(0, 3, m) != 0) return false;
	s = s.substr(3);
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
	return s.empty() || s.front() == '#';
}

// ==================== YAML comment round-trip ====================
// A safety-first structural scanner that decides, for each full-line comment
// and each trailing comment, the JSON path of the node it belongs to. It
// mirrors the block parser's indentation/key/sequence heuristics but never
// guesses: uncertain shapes (explicit "?" keys, compound/flow keys, block
// scalar bodies, multi-document streams) simply drop the pending comments
// rather than mis-place them. Comments attach to the parsed tree itself, so
// they survive a YAML -> YAML round-trip but never leak into JSON or other
// formats (equality and cloning ignore them).

struct YamlCommentEntry {
	std::string path;                    // "\x01" reserves the document footing
	std::vector<std::string> leading;    // full-line comments ("# ...")
	std::string trailing;                // comment at the end of the value line (incl. '#')
};

// The trailing "# ..." comment of a line (quote/escape aware), or empty.
inline std::string yamlTrailingComment(std::string_view s) {
	bool inSQ = false, inDQ = false;
	for (size_t i = 0; i < s.size(); i++) {
		if (inDQ) {
			if (s[i] == '\\' && i + 1 < s.size()) { i++; continue; }
			if (s[i] == '"') inDQ = false;
		} else if (inSQ) {
			if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { i++; continue; }
			if (s[i] == '\'') inSQ = false;
		} else {
			if (s[i] == '#') return std::string(s.substr(i));
			if (s[i] == '"') inDQ = true;
			if (s[i] == '\'') inSQ = true;
		}
	}
	return std::string();
}

inline std::vector<YamlCommentEntry> yamlCollectComments(std::string_view input) {
	std::vector<YamlCommentEntry> out;
	auto lines = splitLines(input);
	// Each frame is a block container: the indent of its entries, its kind
	// ('m' map / 's' sequence), the JSON path of the container itself, the
	// sequence counter, and the path of the last recorded entry (the container
	// opened by a deeper line).
	struct YFrame { int indent; char kind; std::string path; int seqIdx; std::string lastEntry; std::string open; };
	std::vector<YFrame> st;
	std::vector<std::string> pending;
	bool disabled = false;   // multi-document streams do not keep comments
	int boundaryCount = 0;
	int blockBody = -1;      // indent of a block-scalar header line; deeper lines are body text

	for (size_t li = 0; li < lines.size(); li++) {
		if (disabled) break;
		const int indent = countIndent(lines[li]);
		std::string_view c = stripIndent(lines[li]);

		if (blockBody >= 0) {
			if (indent > blockBody) continue;  // block-scalar body text is data
			blockBody = -1;
		}
		if (!c.empty() && indent == 0 && yamlIsBoundaryMarker(c, true)) {
			if (++boundaryCount > 1) { disabled = true; break; }
			pending.clear();
			continue;
		}
		if (!c.empty() && indent == 0 && yamlIsBoundaryMarker(c, false)) {
			disabled = true;
			break;
		}
		if (!c.empty() && c[0] == '%' && indent == 0) { pending.clear(); continue; }

		if (c.empty() || c[0] == '#') {
			if (!c.empty()) pending.push_back(std::string(c));
			continue;
		}

		// -- content line --
		std::string_view rest = c;
		bool dash = false;
		if (yamlIsDashStart(c)) {
			dash = true;
			rest = c.size() >= 2 ? c.substr(2) : std::string_view();
			while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);
		}

		const char kind = dash ? 's' : 'm';
		for (;;) {
			while (!st.empty() && st.back().indent > indent) st.pop_back();
			if (st.empty()) {
				// Root container frame (map or sequence).
				YFrame f; f.indent = indent; f.kind = kind; f.seqIdx = 0;
				st.push_back(f);
				break;
			}
			YFrame& top = st.back();
			if (indent > top.indent) {
				// Deeper line: the previous entry of the enclosing frame becomes
				// the container that owns this new frame's entries.
				YFrame f; f.indent = indent; f.kind = kind; f.seqIdx = 0;
				f.path = top.lastEntry;
				st.push_back(f);
				break;
			}
			if (indent == top.indent) {
				if (top.kind == 's' && !dash) {
					// A non-dash line at the sequence's own indent ends the block
					// sequence (the parser re-homes it as the parent's sibling).
					st.pop_back();
					continue;
				}
				if (top.kind == 'm' && dash && !top.open.empty()) {
					// A dash at the same indent as an empty-looking key continues
					// that key as a sequence value ("list:\n- a\n- b").
					YFrame f; f.indent = indent; f.kind = 's'; f.seqIdx = 0;
					f.path = top.open;
					st.push_back(f);
				}
				break; // regular entry in the current frame
			}
			break;
		}

		bool bindable = true;
		std::string entryPath;   // JSON path of the node this line names
		std::string valuePart;

		YFrame& top = st.back();
		if (top.kind == 's') {
			entryPath = top.path.empty() ? "[" + std::to_string(top.seqIdx) + "]"
			                             : top.path + "\x1F" + "[" + std::to_string(top.seqIdx) + "]";
			top.seqIdx++;
			valuePart = std::string(rest);
		} else {
			// '?', ':', '{' and '[' at the value position are explicit-key or
			// compound-key constructs without a single JSON key -- drop comments.
			if (rest.empty() || rest[0] == '?' || rest[0] == ':' || rest[0] == '{' || rest[0] == '[') {
				bindable = false;
			} else {
				size_t colon = yamlFindKeyColonSep(rest);
				if (colon == std::string_view::npos || colon == 0) {
					bindable = false;
				} else {
					std::string_view keyPart = rest.substr(0, colon);
					while (!keyPart.empty() && (keyPart.back() == ' ' || keyPart.back() == '\t')) keyPart.remove_suffix(1);
					if (!keyPart.empty() && (keyPart[0] == '{' || keyPart[0] == '[')) {
						bindable = false;
					} else {
						const std::string sep = top.path.empty() ? std::string() : "\x1F";
						if (keyPart.size() >= 2 && keyPart[0] == '"' && keyPart.back() == '"')
							entryPath = top.path + sep + yamlUnescapeDouble(keyPart.substr(1, keyPart.size() - 2));
						else if (keyPart.size() >= 2 && keyPart[0] == '\'' && keyPart.back() == '\'')
							entryPath = top.path + sep + yamlUnescapeSingle(keyPart.substr(1, keyPart.size() - 2));
						else
							entryPath = top.path + sep + std::string(keyPart);
					}
					if (colon + 1 < rest.size()) valuePart = std::string(rest.substr(colon + 1));
				}
			}
		}
		top.lastEntry = bindable ? entryPath : std::string();
		if (top.kind == 'm') {
			// Track empty-valued keys ("key:" with no value): a dash on the same
			// indent is their sequence value ("list:\n- a").
			bool hasVal = false;
			for (char ch : valuePart) {
				if (ch != ' ' && ch != '\t') { hasVal = ch != '#'; break; }
			}
			top.open = (bindable && !hasVal) ? entryPath : std::string();
		}

		if (bindable) {
			std::string trail = valuePart.empty() ? std::string() : yamlTrailingComment(valuePart);
			if (!pending.empty()) {
				YamlCommentEntry e; e.path = entryPath; e.leading = std::move(pending);
				out.push_back(std::move(e));
			}
			if (!trail.empty()) {
				// Merge with a just-created leading-only entry for the same node.
				if (!out.empty() && out.back().path == entryPath && out.back().trailing.empty())
					out.back().trailing = trail;
				else {
					YamlCommentEntry e; e.path = entryPath; e.trailing = trail;
					out.push_back(std::move(e));
				}
			}
		} else {
			pending.clear();
		}

		if (yamlIsBlockScalarHeader(c)) blockBody = indent;
	}
	if (!disabled && !pending.empty()) {
		YamlCommentEntry e; e.path = "\x01"; e.leading = std::move(pending);
		out.push_back(std::move(e));
	}
	return out;
}

// Resolve a comment path ("a\x1Fb\x1F[2]") against the parsed tree.
inline asvJSONValue* yamlFindPath(asvJSONValue* root, std::string_view path) {
	if (path.empty()) return root;
	asvJSONValue* cur = root;
	size_t pos = 0;
	while (pos <= path.size()) {
		size_t sep = path.find_first_of("\x1F", pos);
		std::string_view seg = path.substr(pos, sep == std::string_view::npos ? std::string_view::npos : sep - pos);
		if (seg.empty()) return nullptr;
		if (seg[0] == '[') {
			if (cur->type != asvJSONValue::ARRAY || !cur->arr || seg.size() < 3 || seg.back() != ']') return nullptr;
			size_t idx = 0;
			for (size_t k = 1; k + 1 < seg.size(); k++) {
				char d = seg[k];
				if (d < '0' || d > '9') return nullptr;
				idx = idx * 10 + static_cast<size_t>(d - '0');
			}
			if (idx >= cur->arr->size()) return nullptr;
			cur = (*cur->arr)[idx].get();
		} else {
			if (cur->type != asvJSONValue::OBJECT || !cur->obj) return nullptr;
			auto it = mapFind(*cur->obj, seg);
			if (it == cur->obj->end()) return nullptr;
			cur = it->second.get();
		}
		if (sep == std::string_view::npos) break;
		pos = sep + 1;
	}
	return cur;
}

// Attach collected comments to the parsed tree (by resolved path).
inline void yamlAttachComments(asvJSONValue* root, const std::vector<YamlCommentEntry>& entries) {
	if (!root) return;
	for (const auto& e : entries) {
		if (e.path == "\x01") {
			if (!root->yaml_comments) root->yaml_comments.reset(new YAMLComments());
			for (const auto& cl : e.leading) root->yaml_comments->footing.push_back(cl);
			continue;
		}
		asvJSONValue* node = yamlFindPath(root, e.path);
		if (!node) continue;
		if (!node->yaml_comments) node->yaml_comments.reset(new YAMLComments());
		for (const auto& cl : e.leading) node->yaml_comments->leading.push_back(cl);
		if (!e.trailing.empty()) node->yaml_comments->trailing = e.trailing;
	}
}

// Main YAML -> JSON entry point -- handles multi-document streams
inline std::string yamlToJson(std::string_view input) {
	auto lines = splitLines(input);
	if (lines.empty()) return "{}";

	// Find document boundaries. Each document is a half-open line range [start, end)
// plus flags for explicit start ("---" seen) and real content. "---" at column 0
// begins an explicit document (which may be empty => null); "..." at column 0
// ends the current document. Content without a preceding "---" forms an implicit
// document, which is never empty -- an implicit document with no content is no
// document at all.
std::vector<std::tuple<size_t, size_t, bool, bool>> docs; // {start, end, explicit, hasContent}
	size_t docStart = 0;
	bool inDoc = false;
	bool isExplicit = false;
	bool hasContent = false;
	size_t pendingDirStart = SIZE_MAX; // first %-directive line before next doc

	for (size_t i = 0; i < lines.size(); i++) {
		std::string_view stripped = stripIndent(lines[i]);
		if (stripped.empty() || stripped[0] == '#') continue;
		const bool atIndent0 = countIndent(lines[i]) == 0;

		if (atIndent0 && yamlIsBoundaryMarker(stripped, true)) { // "---"
			if (inDoc) docs.emplace_back(docStart, i, isExplicit, hasContent);
			inDoc = true;
			isExplicit = true;
			hasContent = false;
			docStart = i + 1;
			if (pendingDirStart != SIZE_MAX) { // include directives before ---
				docStart = pendingDirStart;
				pendingDirStart = SIZE_MAX;
			}
			continue;
		}
		if (atIndent0 && yamlIsBoundaryMarker(stripped, false)) { // "..."
			if (inDoc) docs.emplace_back(docStart, i, isExplicit, hasContent);
			inDoc = false;
			isExplicit = false;
			hasContent = false;
			continue;
		}
		// %-directives at indent 0 are not content -- they belong to the next doc
		if (atIndent0 && stripped[0] == '%') {
			if (pendingDirStart == SIZE_MAX) pendingDirStart = i;
			continue;
		}

		if (!inDoc) { // first content line of an implicit document
			inDoc = true;
			isExplicit = false;
			hasContent = false;
			docStart = (pendingDirStart == SIZE_MAX) ? i : pendingDirStart;
		}
		hasContent = true;
	}
	// EOF closes the current document
	if (inDoc) docs.emplace_back(docStart, lines.size(), isExplicit, hasContent);

	// Keep content documents as-is; an empty document yields null only when it
	// was explicitly opened with "---". Otherwise it is absent.
	std::vector<std::tuple<size_t, size_t, bool, bool>> keep;
	for (const auto& d : docs) {
		if (std::get<3>(d) || std::get<2>(d)) keep.push_back(d);
	}

	if (keep.empty()) return "{}";

	auto buildDocInput = [&](size_t start, size_t end) -> std::string {
		std::string sub;
		bool haveAny = false;
		bool lastEmpty = false;
		for (size_t i = start; i < end; i++) {
			std::string_view c = stripIndent(lines[i]);
			if ((c == "---" || c == "...") && countIndent(lines[i]) == 0) continue;
			if (haveAny) sub += '\n';
			sub += lines[i];
			haveAny = true;
			lastEmpty = lines[i].empty();
		}
		// A trailing empty line must survive the round-trip through splitLines:
		// joining with '\n' drops one trailing newline, so restore it here.
		if (haveAny && lastEmpty) sub += '\n';
		return sub;
	};

	if (keep.size() == 1) {
		if (std::get<3>(keep[0]))
			return yamlParseDoc(buildDocInput(std::get<0>(keep[0]), std::get<1>(keep[0])));
		return "null";
	}

	std::string result = "[";
	for (size_t d = 0; d < keep.size(); d++) {
		if (d > 0) result += ",";
		result += std::get<3>(keep[d])
			? yamlParseDoc(buildDocInput(std::get<0>(keep[d]), std::get<1>(keep[d])))
			: "null";
	}
	result += "]";
	return result;
}

inline bool asvJSON::fromYAML(std::string_view input) {
	formatType = FormatType::Unknown;
	try {
		if (input.empty()) throw asvJSONError("empty input");
		std::string json = yamlToJson(input);
		allowNaNInfinity = true;
		bool ok = parse(std::string_view(json));
		if (ok) {
			formatType = FormatType::YAML;
			// Attach comments for round-trip fidelity (YAML -> YAML).
			yamlAttachComments(root.get(), yamlCollectComments(input));
		}
		return ok;
	} catch (const asvJSONError& e) {
		lastError = e.what();
		return false;
	}
}

} // namespace asvJSONInternal
