#pragma once
// YAML serialization/parsing for asvJSON++
//
// Known limitations
// -----------------
// * DATETIME / TIMESTAMP round-trip through YAML relies on the core JSON
//   string auto-detection, not on YAML tagging. The encoder emits DATETIME as
//   a bare ISO-8601 scalar and TIMESTAMP as a bare integer (`dt: 2024-01-15T10:30:00.123Z`,
//   `ts: 1700000000`) with no explicit tag; the YAML scalar decoder maps such
//   scalars to a JSON string (DATETIME) or a JSON number (TIMESTAMP) without
//   deep date detection. The DATETIME value is only reconstructed afterwards,
//   when the intermediate JSON string passes through the JSON-layer ISO auto-
//   detect (which likewise handles `!!timestamp`/`!!datetime` only as the raw
//   string until that string reaches the JSON layer). A TIMESTAMP whose bare
//   value is a plain integer therefore loses its TIMESTAMP type through a
//   YAML round-trip unless an explicit `!!timestamp ...` tag is present that
//   the JSON-layer detection can resolve. Round-trip fidelity is guaranteed for
//   STRING / INT / DOUBLE / BOOL / NULL / ARRAY / OBJECT / BINARY / OBJECTID /
//   REGEX and for DATETIME values whose ISO text matches the JSON-layer
//   auto-detect pattern.
// * A root-level flow-collection document followed by a deeper plain line
//   (`{a: 1}\nb: 2`) has its trailing line parsed as a separate implicit
//   document; re-serialization concatenates the documents into a JSON array
//   only for `---`-delimited streams, so the plain line may be dropped if it
//   does not form a document boundary. Use `---` separators for multi-node
//   streams.
// * A mapping value followed by a sibling key at the parent indentation is
//   handled correctly (see the round-trip tests below), but a sequence or
//   mapping whose items resume at the same indent as an enclosing mapping that
//   has already closed requires the parser's frame-closing logic (closeFrames)
//   to run; the nested-mapping-same-indent case is covered by the round-trip
//   tests in test.cpp (testYaml...NestedIndent*).
// * At most ONE trailing comment is re-emitted, inline, and only when it is a
//   single-line one (a multi-line block comment has no inline form). Several
//   trailing comments on one node can only arrive from another format -- YAML's
//   own decoder binds at most one per value line -- and the extras are dropped.
//   Emitting them on following lines would not help: a `#` line there carries no
//   marker of which node it belongs to, so it would re-read as the LEADING
//   comment of the next node (or the root's footing).
// * Error messages report the line number RELATIVE to the sub-document being
//   parsed, not to the input: the recursive entry points (block anchors,
//   `!!yaml` embeds, explicit-key and block-set gathers) hand yamlParseDocImpl a
//   freshly gathered line vector, and `li + 1` counts from there. A top-level
//   error is the true line; an error inside an anchored or embedded block
//   reports its position within that block. This is not fixable with a base
//   offset alone, because yamlNormalizeSeqItems INSERTS and rewrites lines
//   (`- a: 1` becomes `-` + `  a: 1`), so an index no longer maps back to the
//   original document.
// * See also the "YAML Format Notes" section in AGENTS.md.

#include "../core.hpp"

namespace asvJSONInternal {

inline constexpr size_t MAX_YAML_ANCHORS = 100000;
inline constexpr size_t MAX_YAML_DEPTH = 64;

// Forward declarations for block/flow encoders
inline void yamlBlockVal(const asvJSONValue* v, std::string& out, int indent,
                         const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool wc,
                         bool isRoot = false);
inline void yamlFlowVal(const asvJSONValue* v, std::string& out, int indent,
                        const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool forceFlow,
                        bool isRoot = false);

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

// Trailing comment suffix for a value line ("" when the node carries none or
// comments are not being emitted, i.e. Auto/Flow output must stay comment-free
// -- leading comments are gated by wc but the trailing suffix was leaking).
inline std::string yamlTrail(const asvJSONValue* v, bool wc) {
	if (!wc || !v->comments || v->comments->trailing.empty()) return std::string();
	// One value line holds one comment. Emit the LAST SINGLE-LINE trailing
	// comment; a multi-line block comment cannot go inline at all, so a node
	// whose only trailing comment is a block one gets nothing (that is a
	// documented loss, see the KNOWN LIMITATIONS block in the header -- YAML's
	// own decoder cannot produce several trailing comments for one node, so it
	// only bites a tree bridged in from another format).
	// Taking the last LINE comment rather than only ever looking at the final
	// entry matters when a block comment precedes it: previously the block one
	// suppressed the whole line and the line comment was dropped too.
	for (auto it = v->comments->trailing.rbegin(); it != v->comments->trailing.rend(); ++it) {
		if (it->block) continue;
		return " #" + it->text;
	}
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
		case YamlWriteOptions::Style::Auto: yamlFlowVal(this, out, 0, "", false, opts, false, true); break;
		case YamlWriteOptions::Style::Flow: yamlFlowVal(this, out, 0, "", false, opts, true, true); break;
		default: yamlBlockVal(this, out, 0, "", false, opts, true, true);
	}
}

inline void asvJSONValue::toYAML(std::string& out, int indent, const std::string& key, bool isArrayItem, bool pretty) const {
	YamlWriteOptions o;
	if (!pretty) o.style = YamlWriteOptions::Style::Auto;
	switch (o.style) {
		case YamlWriteOptions::Style::Auto: yamlFlowVal(this, out, indent, key, isArrayItem, o, false,
		                                             indent == 0 && key.empty() && !isArrayItem); break;
		case YamlWriteOptions::Style::Flow: yamlFlowVal(this, out, indent, key, isArrayItem, o, true,
		                                             indent == 0 && key.empty() && !isArrayItem); break;
		default: yamlBlockVal(this, out, indent, key, isArrayItem, o, true,
		                      indent == 0 && key.empty() && !isArrayItem);
	}
}

inline std::string asvJSON::toYAML(const YamlWriteOptions& opts) const {
	std::string out;
	if (root) {
		if (opts.style == YamlWriteOptions::Style::Block && root->type != asvJSONValue::ARRAY) {
			out += "---\n";
		}
		root->toYAML(out, opts);
		if (opts.style == YamlWriteOptions::Style::Block && root->comments)
			emitCommentLines(out, root->comments->footing, "#");
	}
	return out;
}

// Write a REGEX value as the `!regex "<pattern>|<options>"` scalar.
// A literal '|' inside the pattern or the options is written as \u007C (the
// same convention TRON and XML use), so the '|' that separates the two
// fields stays unambiguous: the decoder splits the RAW scalar text and only
// then unescapes each side. Without the escape a pattern like "a|b" (no
// options) came back as the pattern "a" with the options "b".
inline void yamlFmtRegexVal(std::string_view s, std::string& out) {
	size_t sep = s.rfind('\0');
	std::string_view pat = (sep != std::string_view::npos) ? s.substr(0, sep) : s;
	std::string_view opts = (sep != std::string_view::npos && sep + 1 < s.size())
		? s.substr(sep + 1) : std::string_view();
	auto escField = [](std::string& o, std::string_view t) {
		std::string tmp;
		appendJsonEscaped(tmp, t);
		for (char c : tmp) {
			if (c == '|') o += "\\u007C";
			else o += c;
		}
	};
	out.push_back('"');
	escField(out, pat);
	if (!opts.empty()) {
		out += '|';
		escField(out, opts);
	}
	out.push_back('"');
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
			// YAML 1.1's !!float NaN spelling is unsigned (.nan), and the
			// decoder maps every NaN spelling -- .nan/.NaN/NaN, signed or not
			// -- to a positive NaN. Emitting -.nan for a negative-signed NaN
			// would therefore not round-trip (it would re-read as positive),
			// so the sign bit is intentionally dropped here (unlike .inf).
			if (std::isnan(v->dbl)) { out += ".nan"; return true; }
			if (std::isinf(v->dbl)) { out += (v->dbl > 0 ? ".inf" : "-.inf"); return true; }
			std::string val; fmtDoubleValDot0(v->dbl, val);
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
			std::string dt; fmtDateTimeVal(v->timestamp, v->datetime_ms, dt, v->tz_offset_min);
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
			std::string r; yamlFmtRegexVal(v->str_data, r);
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
                         const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool wc,
                         bool isRoot) {
	using T = asvJSONValue::Type;
	const int iw = yamlIndentWidth(o);
	auto prefix = [&]() -> std::string {
		if (isArrayItem && key.empty()) return std::string(indent * iw, ' ') + "- ";
		// An empty OBJECT key needs the quoted "'': " form, but the ROOT scalar
		// call also has an empty key and must stay a bare scalar: emitting
		// "'': value" wrapped it into an object with an empty key on reparse.
		// isRoot separates the two (a root object's members are emitted at the
		// same indent, so indent alone cannot).
		if (!key.empty() || (!isArrayItem && !isRoot)) return std::string(indent * iw, ' ') + yamlKeyOpt(key, o) + ": ";
		return "";
	};

	if (wc && v->comments) {
		std::vector<Comment> extra;
		for (const auto& c : v->comments->leading) extra.push_back(c);
		for (const auto& c : v->comments->closing) extra.push_back(c);
		for (const auto& c : extra) {
			if (c.block || c.text.find('\n') != std::string::npos) {
				size_t pos = 0;
				bool first = true;
				while (pos <= c.text.size()) {
					size_t nl = c.text.find('\n', pos);
					std::string_view l(c.text.data() + pos, nl == std::string_view::npos ? c.text.size() - pos : nl - pos);
					size_t b = 0, e = l.size();
					while (b < e && (l[b] == ' ' || l[b] == '\t' || l[b] == '\r')) ++b;
					while (e > b && (l[e - 1] == ' ' || l[e - 1] == '\t' || l[e - 1] == '\r')) --e;
					if (e > b) {
						if (!first) out += '\n';
						first = false;
						out += std::string(indent * iw, ' ') + "# " + std::string(l.data() + b, e - b);
					}
					if (nl == std::string::npos) break;
					pos = nl + 1;
				}
				if (!first) out += '\n';
			} else {
				out += std::string(indent * iw, ' ') + '#' + c.text + "\n";
			}
		}
	}

	switch (v->type) {
		case T::NULL_VAL:
			out += prefix() + "~" + yamlTrail(v, wc) + "\n"; break;
		case T::BOOL_VAL:
			out += prefix() + (v->flag ? "true" : "false") + yamlTrail(v, wc) + "\n"; break;
		case T::INT: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) out += prefix() + std::string(buf, ptr) + yamlTrail(v, wc) + "\n";
			break;
		}
		case T::DOUBLE: {
			if (std::isnan(v->dbl)) { out += prefix() + ".nan" + yamlTrail(v, wc) + "\n"; break; }
			if (std::isinf(v->dbl)) { out += prefix() + (v->dbl > 0 ? ".inf" : "-.inf") + yamlTrail(v, wc) + "\n"; break; }
			std::string val; fmtDoubleValDot0(v->dbl, val);
			out += prefix() + val + yamlTrail(v, wc) + "\n"; break;
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
					// A CR (or any other C0 control) inside a block-scalar line
					// cannot survive: CR is a line break to YAML and \r at the end
					// of a line made the line look blank, so "x\r\ny" decoded as
					// "x\n" + an extra blank. Quote such strings (the double-quoted
					// form escapes them losslessly).
					for (char cc : frag)
						if ((static_cast<unsigned char>(cc) < 0x20) && cc != '\t') { needQuote = true; break; }
					size_t j = 0;
					while (j < frag.size() && (frag[j] == ' ' || frag[j] == '\t')) j++;
if (j > 0 && j < frag.size() && frag[0] == ' ') hasLeadSpace = true;
				if (j > 0 && j == frag.size()) needQuote = true;          // space-only line
				if (j > 0 && frag[0] == '\t') needQuote = true;           // tab in indent margin
				// A '#'-leading line needs NO quoting: the decoder only treats
				// '#' as a comment when the line sits BELOW the block scalar's
				// content indent, and these lines are written at exactly
				// (indent + 1) * iw, which IS the content indent (YAML 1.2.2
				// Ex 8.5). Before the content indent is known the first line
				// establishes it and is data too. Quoting them anyway only made
				// valid multi-line strings ("#a\nb") unreadable.
					i = eol + 1;
				}
				if (!needQuote && hasLeadSpace) {
					int nb = (indent + 1) * iw;
					if (nb < 1 || nb > 9) needQuote = true; // no single-digit explicit indent
					else ind = ind.substr(0, 1) + char('0' + nb) + ind.substr(1);
				}
				if (needQuote) { out += prefix() + yamlDQuote(v->str_data) + yamlTrail(v, wc) + "\n"; break; }
				out += prefix() + ind + yamlTrail(v, wc) + "\n";
				for (size_t i = 0; i < v->str_data.size(); ) {
					size_t eol = v->str_data.find('\n', i);
					if (eol == std::string_view::npos) eol = v->str_data.size();
					out += std::string((indent + 1) * iw, ' ') + std::string(v->str_data.data() + i, eol - i) + "\n";
					i = eol + 1;
				}
			} else if (v->str_data.find('\n') != std::string_view::npos) {
				out += prefix() + yamlDQuote(v->str_data) + yamlTrail(v, wc) + "\n";
			} else if (yamlNeedQuoteOpt(v->str_data, o)) {
				out += prefix() + yamlQuoteStr(v->str_data, o) + yamlTrail(v, wc) + "\n";
			} else {
				out += prefix() + v->str_data + yamlTrail(v, wc) + "\n";
			}
			break;
		}
		case T::DATETIME: {
			std::string dt; fmtDateTimeVal(v->timestamp, v->datetime_ms, dt, v->tz_offset_min);
			out += prefix() + dt + yamlTrail(v, wc) + "\n"; break;
		}
		case T::BINARY:
			out += prefix() + "!!binary " + encodeBase64(v->bin_data.data(), v->bin_data.size()) + yamlTrail(v, wc) + "\n"; break;
		case T::OBJECTID: {
			std::string hex; fmtObjectIdHexVal(v->str_data, hex);
			out += prefix() + "!objectid " + hex + yamlTrail(v, wc) + "\n"; break;
		}
		case T::REGEX: {
			std::string r; yamlFmtRegexVal(v->str_data, r);
			out += prefix() + "!regex " + r + yamlTrail(v, wc) + "\n"; break;
		}
		case T::TIMESTAMP: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v->num);
			if (ec == std::errc()) out += prefix() + std::string(buf, ptr) + yamlTrail(v, wc) + "\n";
			break;
		}
		case T::EXTENSION:
			out += prefix() + "!ext " + std::to_string(v->ext_type) + " " + yamlQuote(encodeBase64(v->bin_data.data(), v->bin_data.size())) + yamlTrail(v, wc) + "\n";
			break;
		case T::CUSTOM_TAG: {
			// "key: !name <value>"; the value stays inline (force-flow) so the
			// tagged form returns to a single line.
			std::string childOut;
			if (v->custom_value && yamlTryFlowVal(v->custom_value.get(), childOut, o, true)) {
				std::string nm(v->str_data);
				if (!nm.empty() && nm[0] == '!') nm = nm.substr(1);
				out += prefix() + "!" + nm + " " + childOut + yamlTrail(v, wc) + "\n";
			} else {
				std::string nm(v->str_data);
				if (!nm.empty() && nm[0] == '!') nm = nm.substr(1);
				out += prefix() + "!" + nm + yamlTrail(v, wc) + "\n";
			}
			break;
		}
		case T::OBJECT: {
			if (v->yaml_set) {
				// Block form of a YAML !!set:
				//   root / keyed:  key: !!set      seq item:  - !!set
				//                  - a                        - a
				if (!v->obj || v->obj->empty()) { out += prefix() + "!!set {}" + yamlTrail(v, wc) + "\n"; break; }
				if (isRoot) {
					out += "!!set\n";
					for (const auto& [k, val] : *v->obj) out += std::string(indent * iw, ' ') + "- " + yamlQuoteKey(k) + "\n";
				} else {
					out += prefix() + "!!set" + yamlTrail(v, wc) + "\n";
					for (const auto& [k, val] : *v->obj) out += std::string((indent + 1) * iw, ' ') + "- " + yamlQuoteKey(k) + "\n";
				}
				break;
			}
			if (!v->obj || v->obj->empty()) { out += prefix() + "{}" + yamlTrail(v, wc) + "\n"; break; }
			if (isRoot) {
				for (const auto& [k, val] : *v->obj) yamlBlockVal(val.get(), out, indent, k, false, o, wc);
			} else {
				out += prefix() + yamlTrail(v, wc) + "\n";
				for (const auto& [k, val] : *v->obj) yamlBlockVal(val.get(), out, indent + 1, k, false, o, wc);
			}
			break;
		}
		case T::ARRAY: {
			if (!v->arr || v->arr->empty()) { out += prefix() + "[]" + yamlTrail(v, wc) + "\n"; break; }
			if (isRoot) {
				out += "---\n";
				for (const auto& item : *v->arr) yamlBlockVal(item.get(), out, 0, "", true, o, wc);
				break;
			}
			out += prefix() + yamlTrail(v, wc) + "\n";
			// A nested sequence used as a sequence item must indent its own
			// items one level deeper than its dash, otherwise the block parser
			// cannot tell parent items from child items ("m: [[1],[2]]").
			int childIndent = isArrayItem ? indent + 1 : indent;
			for (const auto& item : *v->arr) yamlBlockVal(item.get(), out, childIndent, "", true, o, wc);
			break;
		}
		default:
			out += prefix() + "~" + yamlTrail(v, wc) + "\n"; break;
	}
}

// Try flow mode: emit objects/arrays inline when they fit, fall back to block.
inline void yamlFlowVal(const asvJSONValue* v, std::string& out, int indent,
                        const std::string& key, bool isArrayItem, const YamlWriteOptions& o, bool forceFlow,
                        bool isRoot) {
	using T = asvJSONValue::Type;
	const int iw = yamlIndentWidth(o);
	auto prefix = [&]() -> std::string {
		if (isArrayItem && key.empty()) return std::string(indent * iw, ' ') + "- ";
		if (!key.empty() || (!isArrayItem && !isRoot)) return std::string(indent * iw, ' ') + yamlKeyOpt(key, o) + ": ";
		return "";
	};

	// flowMaxLen is a LINE budget, so the key/indent/dash prefix counts too --
	// measuring the value alone let "a_very_long_key_name: {x: 1, y: 2}" stay
	// flow even when the rendered line was far past the limit.
	auto fitsLine = [&](const std::string& pre, const std::string& inner) {
		if (forceFlow) return true;
		return pre.size() + inner.size() <= static_cast<size_t>(o.flowMaxLen);
	};

	if (v->type == T::OBJECT && v->yaml_set) {
		std::string inner;
		yamlTryFlowVal(v, inner, o, true);
		std::string trial = prefix();
		if (!fitsLine(trial, inner)) {
			// Too long for one line: emit the set in block form instead of
			// silently ignoring the length budget.
			yamlBlockVal(v, out, indent, key, isArrayItem, o, true, isRoot);
			return;
		}
		if (isRoot) out += inner + "\n";
		else out += trial + inner + "\n";
		return;
	}
	if (v->type == T::OBJECT && v->obj && !v->obj->empty()) {
		std::string trial = prefix();
		std::string inner;
		if (yamlTryFlowVal(v, inner, o, forceFlow) && fitsLine(trial, inner)) {
			if (isRoot) out += inner + "\n";
			else out += trial + inner + "\n";
		} else {
			if (isRoot) {
				for (const auto& [k, val] : *v->obj) yamlFlowVal(val.get(), out, indent, k, false, o, forceFlow, false);
			} else {
				out += prefix() + "\n";
				for (const auto& [k, val] : *v->obj) yamlFlowVal(val.get(), out, indent + 1, k, false, o, forceFlow, false);
			}
		}
	} else if (v->type == T::ARRAY && v->arr && !v->arr->empty()) {
		std::string trial = prefix();
		std::string inner;
		if (yamlTryFlowVal(v, inner, o, forceFlow) && fitsLine(trial, inner)) {
			if (isRoot) out += inner + "\n";
			else out += trial + inner + "\n";
		} else {
			out += prefix() + "\n";
			for (const auto& item : *v->arr) yamlFlowVal(item.get(), out, indent, "", true, o, forceFlow, false);
		}
	} else {
		// The fallback handles scalars (including root ones) and empty
		// collections. A root scalar has an empty key and !isArrayItem, which
		// is also the shape of an empty-key object member: isRoot keeps the
		// root value bare instead of wrapping it into "'': value".
		yamlBlockVal(v, out, indent, key, isArrayItem, o, false, isRoot);
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
					// Truncated (\x at end of string, fewer than two hex
					// digits left): keep the escape literally instead of
					// silently dropping "\x", symmetric with the \uZZZZ
					// fallback below (the loop's own i++ then copies the
					// remaining bytes verbatim).
					if (i + 2 >= s.size()) { out += "\\x"; break; }
					auto hex = [&](size_t off) -> int {
						char c = s[i + off];
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						return -1;
					};
					int hi = hex(1), lo = hex(2);
					if (hi < 0 || lo < 0) { out += "\\x"; out.append(s, i + 1, 2); i += 2; break; }
					out += static_cast<char>((static_cast<unsigned char>(hi) << 4) | static_cast<unsigned char>(lo));
					i += 2;
					break;
				}
				case 'u': {
					// Truncated (\u at end of string, fewer than four hex
					// digits left): keep it literally, like the invalid-hex
					// fallback below, rather than dropping "\u".
					if (i + 4 >= s.size()) { out += "\\u"; break; }
					auto hex = [&](size_t off) -> int {
						char c = s[i + off];
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						return -1;
					};
					unsigned int cp = 0;
					bool cpHexOk = true;
					for (int j = 0; j < 4; j++) { int h = hex(1 + j); if (h < 0) { cpHexOk = false; break; } cp = (cp << 4) | static_cast<unsigned int>(h); }
					// `\uZZZZ` is not a code point: emit the text as written (as
					// for any other unknown escape) instead of the NUL the
					// accumulate-and-break loop used to leave behind.
					if (!cpHexOk) { out += "\\u"; out.append(s.substr(i + 1, 4)); i += 4; break; }
					if (cp >= 0xD800 && cp <= 0xDBFF && i + 11 <= s.size() && s[i + 5] == '\\' && s[i + 6] == 'u') {
						unsigned int lo = 0;
						bool loHexOk = true;
						for (int j = 0; j < 4; j++) { int h = hex(7 + j); if (h < 0) { loHexOk = false; break; } lo = (lo << 4) | static_cast<unsigned int>(h); }
						if (loHexOk && lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 10; }
						else { i += 4; }
					} else { i += 4; }
					// An unpaired surrogate has no UTF-8 form: the 3-byte branch
					// below would emit ED A0 80 (CESU-8), which is invalid UTF-8.
					// Substituting U+FFFD matches cppRdUtf8 / udeAppendUtf8.
					if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
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
			// A '#' begins a comment only at the start of a line or after white
			// space (YAML Sec. 5.1); "a#b" is a plain scalar, "value # note" comments.
			if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t'))
				return s.substr(0, i);
			if (s[i] == '"') inDQ = true;
			if (s[i] == '\'') inSQ = true;
		}
	}
	return s;
}

// Convert YAML scalar to JSON (auto-detect type)
inline std::string yamlQuoteJsonScalar(std::string_view s);
inline std::string yamlScalarToJson(std::string_view s) {
	auto fmtDbl = [](double d) {
		std::string n;
		fmtDoubleVal(d, n);
		// Keep an integral-valued double a DOUBLE in the intermediate JSON
		// (a bare "1000" would re-parse as an integer).
		if (n.find_first_of(".eE") == std::string::npos) n += ".0";
		return n;
	};
	if (s.empty() || s == "~" || s == "null" || s == "NULL" || s == "Null") return "null";
	if (s == "true" || s == "TRUE" || s == "True" || s == "yes" || s == "YES" || s == "Yes" || s == "on" || s == "ON" || s == "On")
		return "true";
	if (s == "false" || s == "FALSE" || s == "False" || s == "no" || s == "NO" || s == "No" || s == "off" || s == "OFF" || s == "Off")
		return "false";
	// Non-finite floats: the YAML 1.1 spellings, with either sign. Checked
	// BEFORE the leading-'+' handling below because std::from_chars follows
	// strtod's grammar minus the sign -- it happily parses "Infinity" and
	// "NaN" -- so "+Infinity" used to become a bare "inf" token in the
	// intermediate JSON and failed there with a misleading "Unexpected: i".
	{
		std::string_view tok = s;
		bool neg = false;
		if (!tok.empty() && (tok[0] == '+' || tok[0] == '-')) { neg = (tok[0] == '-'); tok.remove_prefix(1); }
		if (tok == "NaN" || tok == ".NaN" || tok == ".nan" || tok == ".NAN") return "NaN";
		if (tok == "Infinity" || tok == ".Inf" || tok == ".inf" || tok == ".INF")
			return neg ? "-Infinity" : "Infinity";
	}
	// JSON accepts '-' but not '+' as a number sign. Strip the plus, validate
	// the remainder with the C++ number grammar, and re-emit a normalised
	// unsigned token (std::from_chars float formats reject a leading '+').
	if (!s.empty() && s[0] == '+') {
	#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
		std::string_view rest = s.substr(1);
		double dv = 0.0;
		auto [pp, pe] = std::from_chars(rest.data(), rest.data() + rest.size(), dv);
		if (pe == std::errc() && pp == rest.data() + rest.size()) {
			long long iv = 0;
			auto [ip, ie] = std::from_chars(rest.data(), rest.data() + rest.size(), iv);
			if (ie == std::errc() && ip == rest.data() + rest.size()) return std::to_string(iv);
			// A spelling like "+Inf" is not YAML's, so it stays a string: the
			// "inf" token is not representable in the intermediate JSON.
			if (!std::isfinite(dv)) return yamlQuoteJsonScalar(s);
			return fmtDbl(dv);
		}
	#else
		std::string tmp(s.substr(1));
		char* end = nullptr;
		long long iv = std::strtoll(tmp.c_str(), &end, 10);
		if (end == tmp.c_str() + tmp.size()) return std::to_string(iv);
		double dv = std::strtod(tmp.c_str(), &end);
		if (end == tmp.c_str() + tmp.size()) {
			if (!std::isfinite(dv)) return yamlQuoteJsonScalar(s);
			return fmtDbl(dv);
		}
		return yamlQuoteJsonScalar(tmp);
	#endif
	}
	// Non-finite floats: the YAML 1.1 spellings, with either sign. A sign is
	// accepted here because a signed number is already a number in this
	// implementation (see the leading-'+' handling below), so rejecting
	// "+.inf" while accepting "-.inf" was just an oversight.
	// Validate that the string is a valid number (not a YAML plain scalar that looks like a number)
	{
		bool validNumber = false;
	#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
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
	#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
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
				return fmtDbl(dv);
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
			// A token the C++ number grammar accepts but YAML has no spelling for
			// ("inf", "nan", "INF" without a dot, "1e999" overflowing to inf)
			// must stay a quoted string -- the raw token is not valid JSON.
			{
				double probe = 0.0;
				const char* cstr = nullptr;
				std::string tmp(s);
				cstr = tmp.c_str();
				char* end = nullptr;
				probe = std::strtod(cstr, &end);
				if (end == cstr + tmp.size() && !std::isfinite(probe)) return yamlQuoteJsonScalar(s);
			}
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

// Forward declaration for the block-scalar parser (used by the anchor pre-scan)
inline std::string yamlParseBlockScalar(const std::string& header, const std::vector<std::string>& lines, size_t& li, bool isFolded, int parentIndent);

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
				appendJsonEscaped(out, unesc);
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

// Split the scalar of a `!regex` tag into the pattern and the options.
// The double-quoted form (the one the encoder writes) is split on the RAW
// text: the encoder writes a literal '|' in the pattern as \u007C, so the
// separator is the last RAW '|' and an escaped pipe can never be mistaken
// for it. Each side is unescaped separately afterwards. Without this the
// unescaping ran first, so a pattern like "a|b" (no options) came back as
// the pattern "a" with the options "b".
// A scalar without an escaped pipe keeps the historical behavior (the
// already-decoded text is split at its last '|'), so documents written by
// earlier versions decode exactly as before.
inline void yamlSplitRegexScalar(std::string_view val, std::string& pattern, std::string& opts) {
	std::string_view s = val;
	while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.remove_prefix(1);
	if (!s.empty() && s[0] == '"') {
		// Collect the inner text with the escapes intact.
		std::string inner;
		bool closed = false;
		for (size_t i = 1; i < s.size(); i++) {
			if (s[i] == '\\' && i + 1 < s.size()) { inner += s[i]; inner += s[i + 1]; i++; continue; }
			if (s[i] == '"') { closed = true; break; }
			inner += s[i];
		}
		if (closed && inner.find("\\u007C") != std::string::npos) {
			size_t sep = inner.rfind('|');
			std::string patRaw = (sep == std::string::npos) ? inner : inner.substr(0, sep);
			std::string optRaw = (sep == std::string::npos) ? std::string() : inner.substr(sep + 1);
			pattern = yamlUnescapeDouble(patRaw);
			opts = yamlUnescapeDouble(optRaw);
			return;
		}
	}
	// Legacy path: decode the scalar first, then split at the last '|'.
	std::string resolved = yamlParseInlineValue(val);
	if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
		resolved = resolved.substr(1, resolved.size() - 2);
	std::string raw;
	try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
	size_t sep = raw.rfind('|');
	pattern = (sep != std::string::npos) ? raw.substr(0, sep) : raw;
	opts = (sep != std::string::npos) ? raw.substr(sep + 1) : std::string();
}

// Parse tagged YAML value -- handles all !!word / !word tags
inline std::string yamlParseTaggedValue(std::string_view s) {
	// Guard the tag-chain cycle (yamlParseTaggedValue <-> yamlParseInlineValue):
	// "!!int !!int ... 5" chains must not overflow the stack.
	// Do NOT reset the counter before throwing: the guard destructors unwind it
	// by one per frame on the way out, so a manual reset made the unwind run
	// from 0 to -(depth) and every failure RAISED the tolerated depth by ~64 for
	// the rest of the thread (measured: 63 -> 127 -> 191 -> 255), until the
	// counter could wrap. The RAII guard restores it to 0 by itself.
	static thread_local int yamlTagNestDepth = 0;
	struct YamlTagDepthGuard {
		int& d;
		explicit YamlTagDepthGuard(int& depth) : d(depth) { ++d; }
		~YamlTagDepthGuard() { --d; }
	} yamlTagDepthGuard(yamlTagNestDepth);
	if (yamlTagNestDepth >= static_cast<int>(MAX_YAML_DEPTH)) {
		throw asvJSONError("YAML: maximum tag nesting depth exceeded");
	}

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
					if (val[bi] == '0') { if (n > LLONG_MAX / 2) { valid = false; break; } n *= 2; }
					else if (val[bi] == '1') { if (n > (LLONG_MAX - 1) / 2) { valid = false; break; } n = n * 2 + 1; }
					else { valid = false; break; }
				}
				if (!valid) throw asvJSONError("YAML: !!int binary overflow");
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
			std::string inner;
			try { inner = unescapeJsonString(parsed.substr(1, parsed.size() - 2), true); }
			catch (const std::runtime_error&) { throw asvJSONError("YAML: invalid escape in string"); }
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
			#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
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
			std::string inner;
			try { inner = unescapeJsonString(parsed.substr(1, parsed.size() - 2), true); }
			catch (const std::runtime_error&) { throw asvJSONError("YAML: invalid escape in string"); }
			char* e = nullptr;
			errno = 0;
			double d = std::strtod(inner.c_str(), &e);
			if (errno == ERANGE) throw asvJSONError("YAML: !!float overflow");
			if (e == inner.c_str() + inner.size()) {
			#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
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
			#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
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
		std::string resolved = yamlParseInlineValue(val);
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		if (raw.empty() && !val.empty() && val != "\"\"") raw = std::string(val);
		std::string esc;
		appendJsonEscaped(esc, raw);
		return "{\"$binary\":{\"base64\":\"" + esc + "\",\"subType\":\"00\"}}";
	}
	if (tag == "!objectid") {
		std::string resolved = yamlParseInlineValue(val);
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		if (raw.empty() && !val.empty() && val != "\"\"") raw = std::string(val);
		std::string esc;
		appendJsonEscaped(esc, raw);
		return "{\"$oid\":\"" + esc + "\"}";
	}
	if (tag == "!regex") {
		std::string pattern, opts;
		yamlSplitRegexScalar(val, pattern, opts);
		std::string escPat, escOpts;
		appendJsonEscaped(escPat, pattern);
		std::string out = "{\"$regex\":\"" + escPat + "\"";
		if (!opts.empty()) {
			appendJsonEscaped(escOpts, opts);
			out += ",\"$options\":\"" + escOpts + "\"";
		}
		out += "}";
		return out;
	}
	if (tag == "!ext") {
		std::string remaining = std::string(val);
		size_t sp = remaining.find(' ');
		if (sp == std::string::npos) return yamlParseInlineValue(val);
		// The extension type is an int8_t, so only [-128, 127] is representable.
		// A bare from_chars + uint8_t cast silently wrapped a hand-written
		// "!ext 300 ..." to subtype 0x2c and turned a non-numeric type into 0
		// (BINARY). Reject an out-of-range/overflowing/garbage field instead,
		// symmetric with the TRON/ADE/GOON extension checks.
		const std::string num = remaining.substr(0, sp);
		errno = 0;
		char* endp = nullptr;
		long ltype = std::strtol(num.c_str(), &endp, 10);
		if (num.empty() || endp == num.c_str() || *endp != '\0' || errno == ERANGE ||
		    ltype < -128 || ltype > 127)
			throw asvJSONError("YAML: extension type out of range [-128, 127]");
		int extType = static_cast<int>(ltype);
		std::string resolved = yamlParseInlineValue(std::string_view(remaining).substr(sp + 1));
		if (resolved.size() >= 2 && resolved[0] == '"' && resolved.back() == '"')
			resolved = resolved.substr(1, resolved.size() - 2);
		std::string raw;
		try { raw = unescapeJsonString(resolved, true); } catch (...) { raw = resolved; }
		char hex[3];
		snprintf(hex, sizeof(hex), "%02x", static_cast<uint8_t>(extType));
		std::string esc;
		appendJsonEscaped(esc, raw);
		return "{\"$binary\":{\"base64\":\"" + esc + "\",\"subType\":\"" + hex + "\"}}";
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
	// !!omap / !!pairs -- an ordered sequence of single-key mappings. The flow
	// forms ([{a:1},{b:2}] and [[a,1],[b,2]]) are already the right shape and
	// parse through the flow parser. A BLOCK form ("!!omap\n- a: 1") is handled
	// by yamlTaggedBlockValue; reaching here with a non-flow, non-empty value
	// means the tag annotated something that is not a sequence, which used to
	// be reported as a silent empty array.
	if (tag == "!!omap" || tag == "!!pairs") {
		if (val.empty()) return "[]";
		if (val[0] == '[' || val[0] == '{') {
			std::string tmp(val);
			size_t fp = 0;
			return yamlParseFlowValue(tmp, fp, nullptr, nullptr, 0);
		}
		throw asvJSONError(std::string("YAML: ") + std::string(tag) + " tag applied to a non-sequence value");
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
		std::string escName;
		appendJsonEscaped(escName, name);
		std::string out = "{\"$customTag\":{\"name\":\"" + escName + "\",\"value\":";
		out += parsedVal;
		out += "}}";
		return out;
	}
}

inline bool yamlIsAnchorChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

// Pre-compute quote and flow-bracket state for a line - single O(n) pass.
struct YamlPrecomp {
	std::vector<bool> inQuote;  // true at position if inside '...' or "..."
	std::vector<bool> inFlow;   // true at position if inside {...} or [...]
};

inline YamlPrecomp yamlPrecomputeQuotes(std::string_view s) {
	YamlPrecomp r{std::vector<bool>(s.size(), false), std::vector<bool>(s.size(), false)};
	bool inSQuote = false, inDQuote = false;
	int depth = 0;
	for (size_t i = 0; i < s.size(); i++) {
		if (inSQuote) {
			if (s[i] == '\'') {
				if (i + 1 < s.size() && s[i + 1] == '\'') { i++; r.inQuote[i] = true; }
				else inSQuote = false;
			}
			r.inQuote[i] = true;
		} else if (inDQuote) {
			if (s[i] == '\\') { if (i + 1 < s.size()) { i++; r.inQuote[i] = true; } }
			else if (s[i] == '"') inDQuote = false;
			r.inQuote[i] = true;
		} else {
			if (s[i] == '\'') inSQuote = true;
			else if (s[i] == '"') inDQuote = true;
			else if (s[i] == '{' || s[i] == '[') depth++;
			else if (s[i] == '}' || s[i] == ']') { if (depth > 0) depth--; }
			r.inQuote[i] = false;
		}
		r.inFlow[i] = (depth > 0);
	}
	return r;
}
// Resolve *alias. No recursion happens here: the anchor map already holds
// fully-parsed JSON text, so the result is a plain lookup. A self-referential
// anchor ("&x [*x]") is caught earlier, while the anchor is being built, by
// yamlAnchorRefsSelf -- the previous "resolving" bookkeeping here could never
// fire, because nothing resolved an alias while another was in progress.
inline std::string yamlResolveAlias(std::string_view aliasName,
                                    std::unordered_map<std::string, std::string>& anchors,
                                    std::unordered_set<std::string>& /*resolving*/) {
	std::string name(aliasName);
	auto it = anchors.find(name);
	if (it == anchors.end()) {
		throw asvJSONError("YAML: undefined alias '" + name + "'");
	}
	return it->second;
}

// True when "text" references the anchor "name" as a bare alias, which for an
// anchor that is still being defined is a self-reference (an infinite
// structure this tree-based model cannot represent). Quoted occurrences do not
// count; an alias nested in a flow collection does, because it is still the
// same anchor. "*x" matches name "x" but not "xy" or "x.y" being a prefix of a
// longer name.
inline bool yamlAnchorRefsSelf(std::string_view text, std::string_view name) {
	auto pc = yamlPrecomputeQuotes(text);
	for (size_t i = 0; i + 1 < text.size(); i++) {
		if (text[i] != '*' || pc.inQuote[i]) continue;
		if (name.size() > text.size() - (i + 1)) continue;
		if (text.compare(i + 1, name.size(), name) != 0) continue;
		size_t after = i + 1 + name.size();
		if (after < text.size() && yamlIsAnchorChar(text[after])) continue;
		return true;
	}
	return false;
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
			// The escaped form is what the double-quoted KEY reader (and the JSON
			// text in between) understands, and it is the only way a CR survives:
			// emitted raw it truncates the key for every reader, and CRLF input
			// (common on Windows) puts CRs in plain keys.
			case '\r': out += "\\r"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			// Any other control byte has no short escape; \u00XX is. U+007F (DEL)
			// is not a control byte for this purpose and stays literal.
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
					out += buf;
				} else {
					out += c;
				}
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
	std::string result;
	int depth = 0;
	bool inSq = false, inDq = false;
	// Count initial depth from firstPart, and DROP a trailing comment: the
	// multi-line loop below builds its text incrementally, so it stops at a
	// comment marker -- but `result` used to be pre-seeded with the WHOLE
	// firstPart, so the `break` below only ended the depth scan while the
	// comment text stayed in the result. A single-line root flow then failed
	// with "trailing content after flow value" ("{a: 1} # comment").
	size_t cut = firstPart.size();
	for (size_t i = 0; i < firstPart.size(); i++) {
		char c = firstPart[i];
		if (inSq) { if (c == '\'' && i + 1 < firstPart.size() && firstPart[i + 1] == '\'') i++; else if (c == '\'') inSq = false; }
		else if (inDq) { if (c == '\\' && i + 1 < firstPart.size()) i++; else if (c == '"') inDq = false; }
		else {
			if (c == '\'') inSq = true; else if (c == '"') inDq = true;
			else if (c == '#' && (i == 0 || firstPart[i - 1] == ' ' || firstPart[i - 1] == '\t')) { cut = i; break; }
			else if (c == openCh) depth++; else if (c == closeCh) depth--;
		}
	}
	result.assign(firstPart.substr(0, cut));
	while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) result.pop_back();
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
			} else if (c == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) break;
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
		// A flow value must consume at least one byte. A stray closer/separator
		// that no branch consumes (e.g. '}' or ':' inside '[...]') would
		// otherwise leave pos unchanged and spin this loop forever, padding
		// `out` with commas until the process runs out of memory. The root and
		// block paths pre-gather the flow text and reject an unterminated
		// collection before reaching here, but the anchor/tag paths call
		// yamlParseInlineValue directly -- "&E[}" used to hang.
		size_t before = pos;
		std::string elem = yamlParseFlowValue(s, pos, anchors, resolving, depth + 1);
		if (pos == before)
			throw asvJSONError("YAML: unexpected character in flow sequence");
		out += elem;
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
	// Recursion backstop mirroring yamlParseDocImpl's entry guard: the caller
	// passes its OWN depth through, and both use `>`, so a document at exactly
	// MAX_YAML_DEPTH -- the deepest level the entry check admits -- passes here
	// as well. A `>=` instead rejected AT the limit: a depth-64 document
	// containing '&' anywhere (even an inert one inside a comment) died in this
	// pre-scan while the identical document without '&' parsed fine -- and
	// because that throw fires at the deepest legal recursion level, it did not
	// even unwind cleanly: observed as a stack-overflow process crash rather
	// than the intended error. This guard is unreachable through yamlParseDocImpl
	// (the entry check stops recursion one level sooner); it protects direct or
	// future callers. Do not "align" it with the `>=` guards (emitter frame
	// stack, yamlYamlDepth): those count STACK SIZE and reject when full, while
	// this one receives a depth VALUE on the entry check's own scale.
	if (depth > static_cast<int>(MAX_YAML_DEPTH)) throw asvJSONError("YAML: maximum anchor recursion depth exceeded");
	bool found = false;
	for (size_t li = 0; li < lines.size(); li++) {
		// Cheap pre-filter: lines without the '&' character cannot define anchors.
		if (lines[li].find('&') == std::string_view::npos) continue;
		int indent = countIndent(lines[li]);
		std::string_view content = stripIndent(lines[li]);
		if (content.empty() || content[0] == '#') continue;

		auto pc = yamlPrecomputeQuotes(content);
		size_t amp = 0;
		while ((amp = content.find('&', amp)) != std::string_view::npos) {
			if (pc.inQuote[amp] || pc.inFlow[amp]) {
				amp++;
				continue;
			}
			size_t ns = amp + 1;
			if (ns >= content.size() || !yamlIsAnchorChar(content[ns])) { amp++; continue; }
			size_t ne = ns;
			while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
			std::string name(content.substr(ns, ne - ns));
			if (anchors.count(name)) { amp = ne; continue; }
			if (anchors.size() >= MAX_YAML_ANCHORS) throw asvJSONError("YAML: too many anchors");

			found = true;
			std::string_view rest = content.substr(ne);
			while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);

			// A bare "*name" anywhere in the value being anchored refers to this
			// very anchor, i.e. a self-referential (infinite) structure. Report it
			// here instead of silently turning the alias into a plain string
			// ("&x [*x]" used to yield {"a":[["*x"]]}).
			if (yamlAnchorRefsSelf(rest, name))
				throw asvJSONError("YAML: cyclic alias '" + name + "'");

			if (!rest.empty() && rest[0] != '#') {
				if (yamlIsSoloTag(rest)) {
					// Anchor on a solo-tag line: the anchored value is the block
					// node on the following (deeper) lines, annotated by the tag.
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
					if (!rv.empty() && (rv[0] == '|' || rv[0] == '>')) {
						// Anchor on a block-scalar header ("&x |"): the anchored
						// value is the block scalar built from the following deep
						// lines, JSON-quoted exactly like the main pass emits it.
						size_t aLi = li;
						bool aFolded = (rv[0] == '>');
						std::string atext = yamlParseBlockScalar(std::string(rv), lines, aLi, aFolded, indent);
						std::string aesc;
						for (auto c : atext) {
							switch (c) {
								case '"': aesc += "\\\""; break;
								case '\\': aesc += "\\\\"; break;
								case '\n': aesc += "\\n"; break;
								case '\t': aesc += "\\t"; break;
								default: aesc += c;
							}
						}
						anchors[name] = "\"" + aesc + "\"";
					} else {
						anchors[name] = yamlParseInlineValue(rv);
					}
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
					// The anchored block must not reference the anchor it is
					// defining. Without this the recursive parse below could not
					// see the (still uninserted) name and reported the misleading
					// "undefined alias 'x'".
					for (const auto& cl : childLines) {
						if (yamlAnchorRefsSelf(stripIndent(cl), name))
							throw asvJSONError("YAML: cyclic alias '" + name + "'");
					}
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
			amp = ne; // advance past this anchor before searching for the next
		}
	}
	return found;
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
	// Both indicator orders are accepted, per YAML 1.2 s8.1.1.1
	// (c-b-block-header = indent-then-chomp OR chomp-then-indent), so "|2-"
	// and "|+2" are equally valid here. (The ade.md block scalar, a separate
	// simplified format, documents digit-first only and rejects "|+2".)
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
	//
	// Tabs. YAML 1.2 forbids tabs in INDENTATION, never in content: only SPACES
	// count as indentation here, and once the content indent is known exactly
	// that many spaces are stripped while everything past them -- including a
	// tab -- is content. A tab standing where indentation is still expected
	// (before the content indent is established, or short of it) is the
	// indentation error it looks like. A line holding nothing but spaces and
	// tabs is an empty line wherever it sits, never a tab-indent error.
	while (ci < lines.size()) {
		const std::string& raw = lines[ci];
		size_t spaces = 0;
		while (spaces < raw.size() && raw[spaces] == ' ') spaces++;
		const std::string_view after = std::string_view(raw).substr(spaces);
		if (after.find_first_not_of(" \t\r") == std::string_view::npos) {
			entries.push_back({"", 0, true});
			ci++;
			continue;
		}
		if (!after.empty() && after[0] == '\t') {
			// A tab may only appear here as content, i.e. at or past the content
			// indent; otherwise it would be doing indentation work.
			if (contentIndent < 0 || static_cast<int>(spaces) < contentIndent) {
				yamlRejectTabIndent(raw, static_cast<int>(ci) + 1);
				// yamlRejectTabIndent throws - ci++/continue below are dead code.
			}
		}
		const int si = static_cast<int>(spaces);
		// A '#'-prefixed line is only a trailing comment when it sits BELOW the
		// block scalar's content indent; at (or above) content indent it is
		// literal content (YAML 1.2.2 Ex 8.5). Before the content indent is
		// known the first non-empty line establishes it and is data too.
		if (!after.empty() && after[0] == '#' && contentIndent >= 0 && si < contentIndent) { ci++; continue; }
		if (parentIndent >= 0 && si <= parentIndent) break;
		if (contentIndent < 0) contentIndent = si;
		if (si >= contentIndent) {
			entries.push_back({std::string(after), si - contentIndent, false});
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
			} else {
				// Leading empty lines before the first content line are content
				// and are never folded away: each contributes a line break.
				folded.append(static_cast<size_t>(brk), '\n');
			}
			if (e.excess > 0) folded.append(static_cast<size_t>(e.excess), ' ');
			folded += e.content;
			prevEndsWs = !e.content.empty() && (e.content.back() == ' ' || e.content.back() == '\t');
			brk = 0;
			seen = true;
		}
		// The last content line's own line break is content too: "keep"
		// chomping must be able to preserve it, and "clip" must be able to
		// keep exactly one. Trailing blank lines follow it and are never
		// folded away. Both paths therefore end with a break here, and the
		// chomp stage below decides how many survive.
		if (seen) folded.append(static_cast<size_t>(brk) + 1, '\n');
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
		// Clip removes the extra trailing line breaks but keeps one whenever
		// the scalar is non-empty (per YAML 1.2), including a content made
		// only of blank lines (e.g. "k: |\n<blank>" yields "\n", not "") and
		// content whose last line does not end in a break (folded ">"); an
		// empty scalar stays empty ("k: |" with no content lines -> "").
		text.erase(text.size() - trailing);
		if (!text.empty() || trailing > 0) text += '\n';
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

// YAML forbids using tabs for indentation (Section 6). A tab appearing in the
// leading whitespace of a line is a structural error; throw to match ADE behavior.
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
		if (tagStr == "!!omap" || tagStr == "!!pairs") return "[]";
		if (tagStr == "!!binary") return "{\"$binary\":{\"base64\":\"\",\"subType\":\"00\"}}";
		if (!tagStr.empty() && tagStr[0] == '!' && tagStr[1] == '!')
			return yamlParseTaggedValue(tagStr); // scalar core tags: 0/0.0/false/""/null/...
		if (tagStr == "!" || tagStr == "?") return "null";
		{
			// bare custom tag => CUSTOM_TAG with a null value
			std::string escName;
			appendJsonEscaped(escName, name);
			return "{\"$customTag\":{\"name\":\"" + escName + "\",\"value\":null}}";
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
			std::string escName;
			appendJsonEscaped(escName, name);
			std::string out = "{\"$customTag\":{\"name\":\"" + escName + "\",\"value\":";
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
	// !!omap / !!pairs block form: a sequence of single-key mappings, which is
	// exactly what the sequence of child items parses to, so the block form and
	// the flow form produce the same JSON. A mapping (not a sequence) is a
	// representational error rather than a silently empty array.
	if (tagStr == "!!omap" || tagStr == "!!pairs") {
		if (childJson.empty() || childJson[0] != '[')
			throw asvJSONError(std::string("YAML: ") + tagStr + " tag applied to a non-sequence value");
		return childJson;
	}
	if (tagStr == "!" || tagStr == "?") return childJson;
	if (!tagStr.empty() && tagStr[0] == '!' && tagStr[1] == '!')
		throw asvJSONError(std::string("YAML: ") + tagStr + " tag applied to a non-scalar value");

	std::string escName;
	appendJsonEscaped(escName, name);
	std::string out = "{\"$customTag\":{\"name\":\"" + escName + "\",\"value\":";
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
		// Index loop, NOT a range-for: a `\` inside a double-quoted scalar must
		// consume the NEXT byte. `for (char c : s) { if (c == '\\') continue; }`
		// only advanced the iterator, so the escaped quote itself was then read
		// as a closing quote -- the scanner fell out of the string mid-line and
		// counted brackets that were string data, leaving flowDepth > 0 and
		// disabling the sequence-item rewrite for the rest of the document
		// (`- b: "x\"y[z"` turned later mappings into plain scalar strings).
		// A doubled '' needs no special case: it toggles the flag twice, so the
		// parity of the quote count is unchanged.
		bool dq = false, sq = false;
		for (size_t i = 0; i < s.size(); i++) {
			const char c = s[i];
			if (dq) { if (c == '\\' && i + 1 < s.size()) i++; else if (c == '"') dq = false; continue; }
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

// A plain (unquoted, untagged, non-block) scalar may span several lines in
// YAML: every line break folds to a space, and N-1 blank lines between two
// content lines fold to N-1 line breaks. Without this the continuation line was
// handled as a node of its own, so "k:\n  line1\n  line2" emitted the value
// "line1" and then a keyless "line2" (invalid JSON, rejected downstream) and
// "- p\n  q" silently turned one item into two.
//
// `first` is the scalar text already emitted for line `li`, and `out` holds
// the JSON text of the value starting at `valStart`. When a continuation line
// exists the emitted text is rolled back and the whole folded scalar is
// re-emitted from scratch, so the fold is interpreted by the regular scalar
// rules (a "true" split across lines stays the string "true" rather than
// becoming a boolean). Continuation lines must be indented at least
// `contIndent`, and anything that starts a new construct (a key, a sequence
// item, a flow collection, an explicit key, a comment) ends the scalar instead
// of extending it. Blank lines are consumed only when a later line does
// continue the scalar, so trailing blanks stay with the enclosing block.
inline bool yamlFoldPlainScalar(std::vector<std::string>& lines, size_t& li, int contIndent,
                                std::string& out, size_t valStart, std::string_view first) {
	size_t scan = li + 1;
	std::string folded(first);
	int breaks = 0;
	size_t lastFolded = li;
	bool any = false;
	while (scan < lines.size()) {
		std::string_view sv = stripIndent(lines[scan]);
		if (sv.empty()) { breaks++; scan++; continue; }
		if (countIndent(lines[scan]) < contIndent) break;
		if (sv[0] == '#') break;
		if (yamlIsDashStart(sv)) break;
		if (yamlFindKeyColonSep(sv) != std::string_view::npos) break;
		if (sv[0] == '{' || sv[0] == '[' || sv[0] == '?' || sv[0] == '|' || sv[0] == '>') break;
		if (breaks == 0) folded += ' ';
		else folded.append(static_cast<size_t>(breaks), '\n');
		folded.append(sv);
		breaks = 0;
		any = true;
		lastFolded = scan;
		scan++;
	}
	if (!any) return false;
	out.resize(valStart);
	out += yamlParseInlineValue(folded, 0);
	li = lastFolded;
	return true;
}

inline std::string yamlParseDocImpl(std::vector<std::string>& lines, int depth) {
	// Recursion guard: yamlParseDocImpl recurses via tagged block values,
	// explicit keys, block sets and anchored blocks (each passing depth+1).
	if (depth > static_cast<int>(MAX_YAML_DEPTH))
		throw asvJSONError("YAML: maximum nesting depth exceeded");
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

	// A root-level block scalar ("|" / ">" with optional indent/chomp
	// indicators) is a scalar document: parse it directly, then reject any
	// trailing content outside it instead of silently ignoring it.
	{
		std::string_view fc = stripIndent(lines[firstLine]);
		if (!fc.empty() && (fc[0] == '|' || fc[0] == '>')) {
			size_t bLi = firstLine;
			std::string got = yamlParseBlockScalar(std::string(fc), lines, bLi, fc[0] == '>', -1);
			for (size_t t = bLi + 1; t < lines.size(); t++) {
				std::string_view tc = stripIndent(lines[t]);
				if (tc.empty() || tc[0] == '#') continue;
				throw asvJSONError("YAML: unexpected content after block scalar");
			}
			return yamlQuoteJsonScalar(got);
		}
	}

	// A whole document that is a plain/quoted scalar (no mapping separator, no
	// sequence dash, no flow collection, tag, anchor, or block-scalar header)
	// is a root scalar. yamlScalarDocValue already implements the YAML plain
	// scalar folding rules (a single line break becomes a space, each blank
	// line becomes "\n"), and the JSON layer accepts a top-level scalar. The
	// old flow fell through to the object branch, opened '{', and rejected the
	// document with "Expected ':'", so a `toYAML()` round-trip of a root STRING
	// / INT / BOOL could not be read back.
	{
		std::string raw;
		for (size_t t = firstLine; t < lines.size(); t++) {
			std::string_view c = stripIndent(lines[t]);
			c = yamlStripComment(c);
			while (!c.empty() && (c.back() == ' ' || c.back() == '\t' || c.back() == '\r')) c.remove_suffix(1);
			if (t > firstLine) raw += '\n';
			raw.append(c);
		}
		bool isScalar = false;
		std::string sv = yamlScalarDocValue(raw, isScalar);
		if (isScalar) return sv;
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

		if (hasAmp && content.find('&') != std::string_view::npos) {
			// Strip &anchor tags that were registered by the pre-scan.
			// Only strip anchors present in the map to keep the pre-scan and
			// main pass in sync -- unregistered &name text is left as-is so
			// downstream parsing sees it rather than silently dropping it.
			auto pc = yamlPrecomputeQuotes(content);
			std::string cleaned;
			cleaned.reserve(content.size());
			size_t i = 0;
			while (i < content.size()) {
				if (content[i] == '&' && i + 1 < content.size() && yamlIsAnchorChar(content[i + 1]) && !pc.inQuote[i] && !pc.inFlow[i]) {
					size_t ns = i + 1;
					size_t ne = ns;
					while (ne < content.size() && yamlIsAnchorChar(content[ne])) ne++;
					std::string_view aname(content.data() + ns, ne - ns);
					if (anchors.find(std::string(aname)) != anchors.end()) {
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
		// Use yamlFindKeyColonSep: if the line does not start with ': ' (or ':'
		// at EOL), it is either a multi-line key continuation (deeper indent)
		// or an error -- never silently treated as a new sibling entry.
		if (!pendingKey.empty() && yamlFindKeyColonSep(content) != 0) {
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
					// The fold rolls `out` back to the value start, so the
					// marker must sit AFTER the `"key":` prefix (otherwise a
					// multi-line explicit-key value erased its own key).
					const size_t inlineValStart = out.size();
					out += yamlParseInlineValue(inlineVal);
					yamlFoldPlainScalar(lines, li, indent + 1, out, inlineValStart, inlineVal);
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
				const size_t itemValStart = out.size();
				out += yamlParseInlineValue(rest);
				yamlFoldPlainScalar(lines, li, indent + 1, out, itemValStart, rest);
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
					const size_t valStart = out.size();
					out += yamlParseInlineValue(valPart);
					yamlFoldPlainScalar(lines, li, indent + 1, out, valStart, valPart);
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
						else {
							const size_t dcStart = out.size();
							out += yamlParseInlineValue(dc);
							// A plain scalar in the deeper position continues on
							// the following lines at the SAME indent (they are all
							// part of this value), which is what
							// "key:\n  line1\n  line2" means.
							yamlFoldPlainScalar(lines, scan, countIndent(lines[scan]), out, dcStart, dc);
						}
					}
					li = scan; // consume the value line(s)
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
	std::vector<Comment> leading;        // full-line comments (marker stripped)
	std::vector<Comment> trailing;       // comment at the end of the value line
};

// Build a Comment from a raw "# ..." string (marker removed, line style).
inline Comment yamlCommentFromRaw(std::string_view s) {
	if (s.size() >= 1 && s[0] == '#') return Comment{std::string(s.substr(1)), false};
	return Comment{std::string(s), false};
}

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
			if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t'))
				return std::string(s.substr(i));
			if (s[i] == '"') inDQ = true;
			if (s[i] == '\'') inSQ = true;
		}
	}
	return std::string();
}

// One node address: "<len>:<bytes>". The length prefix reserves no byte, so a
// key holding any character -- U+001F, a colon, a NUL -- survives byte for byte
// (a bare separator cannot: it collides with data. Same scheme as iniPathSeg /
// json5PathSeg). An empty base is the root, so joining needs no special case.
inline std::string yamlPathSeg(std::string_view s) {
	return std::to_string(s.size()) + ":" + std::string(s);
}
inline std::string yamlPathJoin(const std::string& base, std::string_view seg) {
	return base + yamlPathSeg(seg);
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
	std::vector<Comment> pending;
	bool disabled = false;   // multi-document streams do not keep comments
	int boundaryCount = 0;
	int blockBody = -1;      // indent of a block-scalar header line; deeper lines are body text

	for (size_t li = 0; li < lines.size(); li++) {
		if (disabled) break;
		const int indent = countIndent(lines[li]);
		std::string_view c = stripIndent(lines[li]);

		if (blockBody >= 0) {
			// A blank line inside the body is body text, not the end of it: the
			// parser keeps collecting until a non-blank line at the header's
			// indent or shallower. Resetting here let a comment after such a line
			// be bound to the wrong node (or to nothing at all).
			if (c.empty() || indent > blockBody) continue;
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
			if (!c.empty()) pending.push_back(yamlCommentFromRaw(c));
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
			entryPath = yamlPathJoin(top.path, "[" + std::to_string(top.seqIdx) + "]");
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
						const size_t keyLen = keyPart.size();
						if (keyLen >= 2 && keyPart[0] == '"' && keyPart.back() == '"')
							entryPath = yamlPathJoin(top.path, yamlUnescapeDouble(keyPart.substr(1, keyLen - 2)));
						else if (keyLen >= 2 && keyPart[0] == '\'' && keyPart.back() == '\'')
							entryPath = yamlPathJoin(top.path, yamlUnescapeSingle(keyPart.substr(1, keyLen - 2)));
						else
							entryPath = yamlPathJoin(top.path, keyPart);
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
			Comment trail;
			if (!valuePart.empty()) {
				std::string raw = yamlTrailingComment(valuePart);
				if (!raw.empty()) trail = yamlCommentFromRaw(raw);
			}
			if (!pending.empty()) {
				YamlCommentEntry e; e.path = entryPath; e.leading = std::move(pending);
				out.push_back(std::move(e));
			}
			if (!trail.text.empty()) {
				// Merge with a just-created leading-only entry for the same node.
				if (!out.empty() && out.back().path == entryPath && out.back().trailing.empty())
					out.back().trailing.push_back(std::move(trail));
				else {
					YamlCommentEntry e; e.path = entryPath; e.trailing.push_back(std::move(trail));
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

// Resolve a comment path against the parsed tree. Path segments are
// length-prefixed ("<len>:<bytes>", see yamlPathSeg) -- a "\x1F" separator
// collided with itself: a key may legally hold that byte (raw, or written as
// "\u001F" in a double-quoted scalar), the collector built the path from the
// DECODED key, and splitting it produced two segments that match no node, so
// the comment was silently dropped. This is the same fix INI/JSON5 already have.
inline asvJSONValue* yamlFindPath(asvJSONValue* root, std::string_view path) {
	if (path.empty()) return root;
	asvJSONValue* cur = root;
	size_t pos = 0;
	while (pos < path.size()) {
		size_t colon = path.find(':', pos);
		if (colon == std::string_view::npos) return nullptr;
		size_t len = 0;
		for (size_t k = pos; k < colon; k++) {
			char d = path[k];
			if (d < '0' || d > '9' || len > (SIZE_MAX - 9) / 10) return nullptr;
			len = len * 10 + static_cast<size_t>(d - '0');
		}
		if (colon + 1 + len > path.size()) return nullptr;
		std::string_view seg = path.substr(colon + 1, len);
		// Dispatch on the node's TYPE, not on the segment's first byte: keys are
		// arbitrary text, so a key like "[abc]", "[0]" or "" under an OBJECT is a
		// plain map lookup. Sniffing seg[0] == '[' routed such keys into the array
		// branch, failed the index parse, returned nullptr, and silently dropped
		// the comment ("[abc]": 1 # c lost "c"). The collector writes "[N]"
		// only for ARRAY children, so brackets are a reliable signal there -- and
		// the size check below keeps an empty segment from reaching front().
		if (cur->type == asvJSONValue::ARRAY) {
			if (!cur->arr || seg.size() < 3 || seg.front() != '[' || seg.back() != ']') return nullptr;
			size_t idx = 0;
			for (size_t k = 1; k + 1 < seg.size(); k++) {
				char d = seg[k];
				if (d < '0' || d > '9') return nullptr;
				idx = idx * 10 + static_cast<size_t>(d - '0');
			}
			if (idx >= cur->arr->size()) return nullptr;
			cur = (*cur->arr)[idx].get();
		} else if (cur->type == asvJSONValue::OBJECT) {
			if (!cur->obj) return nullptr;
			auto it = mapFind(*cur->obj, seg);
			if (it == cur->obj->end()) return nullptr;
			cur = it->second.get();
		} else {
			return nullptr;
		}
		pos = colon + 1 + len;
	}
	return cur;
}

// Attach collected comments to the parsed tree (by resolved path).
inline void yamlAttachComments(asvJSONValue* root, const std::vector<YamlCommentEntry>& entries) {
	if (!root) return;
	for (const auto& e : entries) {
		if (e.path == "\x01") {
			if (!root->comments) root->comments.reset(new Comments());
			for (const auto& c : e.leading) root->comments->footing.push_back(c);
			continue;
		}
		asvJSONValue* node = yamlFindPath(root, e.path);
		if (!node) continue;
		if (!node->comments) node->comments.reset(new Comments());
		for (const auto& c : e.leading) node->comments->leading.push_back(c);
		for (const auto& c : e.trailing) node->comments->trailing.push_back(c);
	}
}

// Main YAML -> JSON entry point -- handles multi-document streams
inline std::string yamlToJson(std::string_view input) {
	auto lines = splitLines(input);
	if (lines.empty()) return "{}";

	// "---" may be followed by the document's first content on the SAME line
	// ("--- a: 1", "--- [1, 2]", "--- !!map"). Such a line is still a document
	// BOUNDARY, so the marker is left in place and the content is stashed for
	// buildDocInput to emit as the document's first line (rewriting the line in
	// place would have merged "--- a: 1\n--- b: 2" into one document). A trailing
	// "# comment" is dropped: it comments the marker line, not the value.
	std::vector<std::string> markerInline;  // per line: content after "---", if any
	markerInline.resize(lines.size());
	for (size_t i = 0; i < lines.size(); i++) {
		if (countIndent(lines[i]) != 0) continue;
		std::string_view s = lines[i];
		if (s.compare(0, 3, "---") != 0) continue;
		if (s.size() == 3) continue;
		if (s[3] != ' ' && s[3] != '\t') continue;   // "---abc" is content, not a marker
		size_t cs = 4;
		while (cs < s.size() && (s[cs] == ' ' || s[cs] == '\t')) cs++;
		if (cs >= s.size()) continue;                 // "---" + whitespace only
		if (s[cs] == '#') continue;                   // "--- # comment"
		std::string_view tail = s.substr(cs);
		// Keep only the part before a trailing comment ("# ..." at a space).
		for (size_t k = 0; k < tail.size(); k++) {
			if (tail[k] == '#' && (k == 0 || tail[k - 1] == ' ' || tail[k - 1] == '\t')) {
				while (k > 0 && (tail[k - 1] == ' ' || tail[k - 1] == '\t')) k--;
				tail = tail.substr(0, k);
				break;
			}
		}
		if (tail.empty()) continue;
		markerInline[i] = std::string(tail);
		// Normalize the line to a bare marker so the boundary scan below sees
		// it as one; buildDocInput puts the stashed content back.
		lines[i] = "---";
	}

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
			// "--- a: 1" carries the document's first content on the marker line,
			// so that line belongs to the range and counts as content.
			hasContent = !markerInline[i].empty();
			// Include any pending directives in the explicit document's range.
			docStart = (pendingDirStart == SIZE_MAX) ? i : pendingDirStart;
			pendingDirStart = SIZE_MAX;
			continue;
		}
		if (atIndent0 && yamlIsBoundaryMarker(stripped, false)) { // "..."
			// The boundary line itself may carry no content, but a "---" line with
			// inline content does (it was folded into the range by docStart = i).
			bool bodyHas = hasContent;
			if (!bodyHas) {
				for (size_t j = docStart; j < i; j++) {
					if (countIndent(lines[j]) != 0) continue;
					std::string_view bc = stripIndent(lines[j]);
					if (bc.empty() || bc[0] == '%') continue;
					if (yamlIsBoundaryMarker(bc, true) || yamlIsBoundaryMarker(bc, false)) continue;
					if (!markerInline[j].empty()) { bodyHas = true; break; }
					bodyHas = true;
					break;
				}
			}
			if (inDoc) docs.emplace_back(docStart, i, isExplicit, bodyHas);
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
			pendingDirStart = SIZE_MAX;
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
			const bool atIndent0 = countIndent(lines[i]) == 0;
			// The marker line may still carry a trailing comment ("--- # c") or
			// trailing spaces, so test for a real boundary rather than "== \"---\"".
			const bool marker = atIndent0 &&
			    (yamlIsBoundaryMarker(c, true) || yamlIsBoundaryMarker(c, false));
			if (marker && markerInline[i].empty()) continue;
			// A "---" line with inline content contributes that content as the
			// document's first line; the marker itself is not part of the body.
			const std::string& body = marker ? markerInline[i] : lines[i];
			if (haveAny) sub += '\n';
			sub += body;
			haveAny = true;
			lastEmpty = body.empty();
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
	bool prevNaN = allowNaNInfinity;
	allowNaNInfinity = true;
	try {
		if (input.empty()) throw asvJSONError("empty input");
		std::string json = yamlToJson(input);
		bool ok = parse(std::string_view(json));
		allowNaNInfinity = prevNaN;
		if (ok) {
			formatType = FormatType::YAML;
			// Attach comments for round-trip fidelity (YAML -> YAML).
			yamlAttachComments(root.get(), yamlCollectComments(input));
		}
		return ok;
	} catch (const asvJSONError& e) {
		allowNaNInfinity = prevNaN;
		lastError = e.what();
		return false;
	} catch (const std::exception& e) {
		// bad_alloc / length_error etc. must not escape: the public contract is
		// bool + lastError, like the other from* parsers (see fromMessagePack).
		// Restore the permissive flag here too -- a leaked allowNaNInfinity
		// would silently change later parses (same lesson as fromJSON5).
		allowNaNInfinity = prevNaN;
		lastError = e.what();
		return false;
	}
}

} // namespace asvJSONInternal
