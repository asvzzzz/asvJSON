#pragma once
// CSV (RFC 4180) serialization/parsing for asvJSON++
//
// Deliberate behaviors worth knowing (each covered by a test in test.cpp):
//
// 1. Formula-injection guard is applied to BARE cells only. Text starting with
//    '=', '+', '-', '@' gets a leading '\'' on output; a cell that must be
//    quoted anyway (contains ',' / '"' / newline) is emitted as a plain
//    quoted literal WITHOUT the guard, because spreadsheets treat "..." as an
//    explicit text literal. Trade-off: "=foo" -> '=foo but "=foo,bar" ->
//    "=foo,bar" (see testCsvSpecialTypesStayText).
// 2. Hand-written '\''<char> loses the apostrophe. A bare cell typed as "'=x"
//    decodes to "=x" (csvStripInjection strips the guard from every bare
//    cell). Required so toCSV->fromCSV round-trips losslessly -- the encoder
//    re-adds the apostrophe -- but a silent loss for hand-authored input, so
//    keep apostrophe-leading text quoted ("'=x") if it must survive verbatim.
// 3. Roots do not always round-trip to the same JSON shape:
//    - every OBJECT root becomes TWO lines (header + one data row), so fromCSV
//      reads it back as an ARRAY holding that one object -- the root changes
//      from OBJECT to ARRAY even for a flat {"a":1,"b":2}. A nested value is
//      flattened to a dotted column on the way out and stays a flat dotted KEY
//      on the way back: {"a":{"b":1}} -> "a.b\n1\n" -> [{"a.b":1}]. Only
//      scalar leaves are flattened; an OBJECT leaf (an empty object stays a
//      leaf) becomes its JSON text in a cell.
//    - an OBJECT whose single key holds an ARRAY flattens that key as a
//      regular column of JSON text, so {"items":[{...}]} decodes back as
//      an ARRAY of {items: "..."} rows (see testCsvSingleKeyArrayOfObjects);
//    - a scalar root is a lone headerless data line that fromCSV re-reads as
//      a header, so it does not round-trip (a scalar export is data-only).
//    - an EMPTY OBJECT or ARRAY root produces an empty document (no header, no
//      rows), so toCSV() returns ""; fromCSV("") rejects it as empty input.
//      An empty CSV carries no schema, so there is nothing to preserve.
// 4. Cell typing follows the JSON number grammar plus a leading '+' extension.
//    ".5" is NOT a number (JSON forbids "." without a leading digit) and stays
//    STRING; "0"/"+0"/"-0" are INT 0. A STRING whose text would re-detect as a
//    different type is quoted on output so the round-trip preserves STRING.
//    An EMPTY STRING is such a case ("") and is written as the quoted literal
//    "", which re-reads as an explicit empty STRING (a bare empty cell is
//    NULL on decode).
// 5. A TIMESTAMP has no CSV cell syntax of its own: it is written as its bare
//    epoch number and comes back as an INT (the type is lost), exactly like
//    the other non-native types REGEX/BINARY/EXTENSION/OBJECTID, which keep
//    only their text form. CUSTOM_TAG keeps its Extended-JSON envelope text
//    (`{"$customTag":{"name":...,"value":...}}`) and decodes back as a STRING
//    holding that text (an empty cell would read as NULL and lose it wholly).
//    DATETIME is the one exception -- it has a native
//    bare ISO-8601 cell form that detects back as DATETIME. To keep a
//    TIMESTAMP a TIMESTAMP, store its text in a STRING cell. The one DATETIME
//    that cannot round-trip as DATETIME is a timestamp outside gmtimeSafe's
//    range, which fmtDateTimeVal renders as the literal "null"; that cell is
//    quoted so it decodes back as the STRING "null" (matching INI/TOML).
// 6. A row wider than the header is rejected, with one exception: trailing
//    BARE empty fields ("a,b" / "1,2,") are tolerated, since a trailing comma
//    carries no data. A trailing QUOTED empty (a cell holding only "") is an
//    explicit empty string -- a column the author wrote on purpose -- so it is
//    rejected like any other extra value instead of being dropped silently.
// 7. Quote handling is TOLERANT, not strict RFC 4180: a '"' opening quotes
//    anywhere in a field starts quote mode and strips its own apostrophes, so
//    hand-written foo"bar"baz decodes to the STRING "foobarbaz" (the interior
//    quotes are consumed, not kept). The quoted flag is still set, which makes
//    it an explicit text literal. toCSV is unaffected -- it quotes the whole
//    cell and doubles interior quotes, so its own output always round-trips
//    verbatim.

#include "../core.hpp"

#include <map>
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
// Tolerant about WHERE a quote may open: a '"' met outside a quoted run always
// starts quote mode, even mid-field (foo"bar"baz -> "foobarbaz"), and is
// dropped along with its partner. toCSV only ever emits a quote at the field
// start with interior quotes doubled, so its output parses back verbatim.
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
 * @brief JSON number shape gate for CSV cells
 */
inline bool csvNumberShapeOk(std::string_view s, size_t nfirst) {
	// JSON number shape gate: rejects forms a lenient strtod/from_chars accepts
	// but JSON forbids ("1.", "-.5", "1.e5", "1e", trailing junk, ...) so they
	// stay STRING instead of silently changing form. A leading '+' is an
	// intentional CSV extension (mirrors "+42" handling); leading zeros are
	// rejected by csvDetectType before this point.
	size_t p = nfirst;
	if (p >= s.size() || s[p] < '0' || s[p] > '9') return false;
	while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
	if (p < s.size() && s[p] == '.') {
		p++;
		size_t fs = p;
		while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
		if (p == fs) return false; // "." must be followed by at least one digit
	}
	if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
		p++;
		if (p < s.size() && (s[p] == '+' || s[p] == '-')) p++;
		size_t es = p;
		while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
		if (p == es) return false; // "e" / "e+" must be followed by digits
	}
	return p == s.size();
}

/**
 * @brief Detect JSON value type from CSV cell string
 */
inline std::unique_ptr<asvJSONValue> csvDetectType(std::string_view s) {
	if (s.empty() || s == "null" || s == "NULL" || s == "_") return asvJSONValue::makeNull();
	if (s == "true" || s == "TRUE" || s == "T") return asvJSONValue::makeBool(true);
	if (s == "false" || s == "FALSE" || s == "F") return asvJSONValue::makeBool(false);
	// ISO-8601 datetime detection: a bare cell matching the datetime form is
	// reconstructed as a DATETIME (preserving the UTC instant AND the displayed
	// tz offset), so toCSV/fromCSV round-trips datetimes exactly as the other
	// formats do. Checked first: the numeric path below would otherwise eat the
	// leading digits and fall through to a STRING ("2020-09-13T..." has digits,
	// '-' and ':' and fails the number-shape check). Plain STRING cells that
	// merely look like datetimes are quoted by the encoder (csvCell textGuard)
	// and never reach this path.
	{
		time_t ts = 0; int ms = 0; int tz = 0;
		if (tryParseDateTime(s, ts, &ms, &tz))
			return asvJSONValue::makeDateTime(ts, ms, tz);
	}
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
				// A leading '+' is not valid JSON number syntax, so a '+'-prefixed
				// overflow literal must not serialize bare (raw_number emits the
				// text unquoted); keep it a plain STRING instead.
				if (r && s[0] != '+') r->raw_number = true;
				return r;
			}
			if (!csvNumberShapeOk(s, nfirst)) return asvJSONValue::makeString(s.data(), s.size());
			double d;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GLIBCXX__) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11))
			// from_chars reports result_out_of_range on double overflow (1e400) or
			// total underflow (1e-400); ec2 != errc falls through to makeString,
			// keeping the literal's text -- the same outcome as the strtod branch
			// below (subnormal underflow is a real value, so from_chars succeeds).
			auto [ptr2, ec2] = std::from_chars(b, s.data() + s.size(), d);
			if (ec2 == std::errc() && ptr2 == s.data() + s.size()) return asvJSONValue::makeDouble(d);
#else
			std::string tmp(s);
			char* end; errno = 0;
			d = std::strtod(tmp.c_str(), &end);
			// strtod signals ERANGE for overflow (-> +/-HUGE_VAL) and for
			// underflow (0 when the value is gone entirely, or a subnormal
			// when it merely got smaller). Overflow and total underflow must
			// stay STRING, but a subnormal result is a real representable
			// value -- rejecting it would flip a valid number to a string.
			if (end == tmp.c_str() + tmp.size() && !std::isinf(d) &&
			    !(errno == ERANGE && d == 0.0))
				return asvJSONValue::makeDouble(d);
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
			// extras. Trailing BARE empty fields (from a trailing comma) are
			// tolerated: they carry no data, so the row is a prefix of the header.
			// A QUOTED empty (a cell holding only "") is different -- it is an
			// explicit empty string, a value the author wrote on purpose, and
			// dropping it would lose a column. It counts as extra data.
			if (fields.size() > headers.size()) {
				bool extraData = false;
				for (size_t k = headers.size(); k < fields.size(); k++)
					if (!fields[k].first.empty() || fields[k].second) { extraData = true; break; }
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
				// makeString returns nullptr when the cell exceeds MAX_STRING_LEN;
				// storing a null value would crash on later access.
				if (!val) throw asvJSONError("CSV: string too long");
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
	} catch (const std::exception& e) {
		// bad_alloc / length_error etc. must not escape: the public contract is
		// bool + lastError, like the other from* parsers (see fromMessagePack).
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
				// JSON serialization maps NaN/Inf to "null"; mirror that here so
				// a NaN/Inf cell keeps its row (an empty cell would vanish, and a
				// sole NaN column would leave a header-only CSV that fails to parse).
				if (std::isnan(v->dbl) || std::isinf(v->dbl)) return "null";
				std::string s; fmtDoubleVal(v->dbl, s);
				// An integral double must round-trip as a float, otherwise
				// "1" would re-decode as an INT (mirror the ADE/INI convention).
				if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
				    s.find('E') == std::string::npos)
					s += ".0";
				return s;
			}
			case T2::STRING: return v->str_data;
			case T2::DATETIME: {
				std::string s; fmtDateTimeVal(v->timestamp, v->datetime_ms, s, v->tz_offset_min); return s;
			}
			case T2::BINARY: return encodeBase64(v->bin_data.data(), v->bin_data.size());
			case T2::OBJECTID: {
				std::string s; fmtObjectIdHexVal(v->str_data, s); return s;
			}
			case T2::REGEX: {
				size_t sep = v->str_data.rfind('\0');
				// Only the pattern text survives a CSV round-trip: options are
				// dropped by design (special types have no CSV cell syntax and
				// decode back as STRING) -- see testCsvSpecialTypesStayText.
				return (sep != std::string::npos) ? std::string(v->str_data.data(), sep) : v->str_data;
			}
			case T2::TIMESTAMP: return std::to_string(v->num);
			case T2::EXTENSION: return encodeBase64(v->bin_data.data(), v->bin_data.size());
			case T2::CUSTOM_TAG: {
				// No CSV cell syntax: the Extended-JSON envelope is the text form
				// (the same convention INI/TOML/JavaProp use). Without this case
				// the `default` branch returned an EMPTY cell, which decodes as
				// NULL -- the whole tag payload vanished silently, and the
				// re-encode of that NULL wrote the literal "null".
				std::string s; v->serialize(s, false); return s;
			}
			case T2::ARRAY:
			case T2::OBJECT: {
				std::string s; v->serialize(s, false); return s;
			}
			default: return {};
		}
	};

	// Fully-escaped cell. A STRING whose text would decode as a different type
	// (number/bool/null/empty) is quoted so the round-trip preserves STRING.
	// The non-native types (REGEX/BINARY/EXTENSION/OBJECTID) have no
	// CSV cell syntax: their text is emitted bare except when it would be mis-read
	// as a number/bool/null on decode, in which case it is quoted as an explicit
	// text literal (a REGEX pattern "123" must not come back as INT 123). DATETIME
	// has a native bare cell form (ISO-8601 with optional offset) that detects
	// back as DATETIME on decode, so it is never quoted. TIMESTAMP
	// stays bare -- an epoch number is its natural CSV form -- and ARRAY/OBJECT
	// cells use their JSON text (already detected as STRING on decode). The
	// formula-injection guard applies to STRING and the special text types, and
	// to headers.
	auto csvCell = [&](const asvJSONValue* v) -> std::string {
		std::string s = cellValue(v);
		bool textGuard = false;
		bool quote = false;
		if (v) switch (v->type) {
			case T::STRING:
			case T::REGEX: case T::BINARY: case T::EXTENSION: case T::OBJECTID:
				textGuard = true;
				{ // Re-detection is consulted only for the text-ish types. INT/BOOL/
				  // DOUBLE/NULL/DATETIME/ARRAY/OBJECT cells already carry their fixed
				  // text and are never quoted, so skipping csvDetectType -- which
				  // allocates a throwaway asvJSONValue per call -- saves a heap
				  // round-trip for every numeric cell in a large document.
					auto d = csvDetectType(s);
					// Quote when re-detection would change the type, and when it
					// would promote a plain STRING to a raw_number (an int64-overflow
					// literal like "99999999999999999999" decodes bare with the
					// raw_number flag set, which alters JSON output). A source that
					// already IS raw_number must stay bare so it keeps its flag.
					quote = !d || d->type != T::STRING || (d->raw_number && !v->raw_number);
				}
				break;
			case T::DATETIME:
				// fmtDateTimeVal renders a timestamp outside gmtimeSafe's range
				// as the literal "null". Emitted bare it would re-detect as the
				// NULL value, silently changing the type, so quote it: the cell
				// then reads back as the STRING "null" (the same degenerate-case
				// convention as INI/TOML, which also emit a quoted "null").
				if (s == "null") quote = true;
				break;
			default: break;
		}
		return csvEscape(s, quote, textGuard);
	};

	auto writeArrayOfObjects = [&](const std::vector<std::unique_ptr<asvJSONValue>>& items) {
		if (items.empty()) return;
		// Column order = first-appearance order of each flattened key across the
		// items (matches the ObjectMapT insertion order the JSON/YAML/INI paths
		// preserve), not a lexicographic sort. Determinism is preserved by
		// collecting from a stable traversal; dedup uses a separate set.
		std::vector<std::string> keys;
		std::unordered_set<std::string> keySeen;
		std::unordered_set<const asvJSONValue*> visited;
		auto collect = [&](auto&& self, const asvJSONValue* v, const std::string& prefix,
						   std::unordered_set<const asvJSONValue*>& vis) -> void {
			if (!v) return;
			if (vis.count(v)) return;
			vis.insert(v);
			if (v->type == T::OBJECT && v->obj && !v->obj->empty()) {
				for (const auto& [k, val] : *v->obj)
					self(self, val.get(), prefix.empty() ? k : prefix + "." + k, vis);
			} else {
				std::string key = prefix;
				if (keySeen.insert(key).second) keys.push_back(std::move(key));
			}
		};
		for (const auto& item : items) collect(collect, item.get(), "", visited);
		for (size_t i = 0; i < keys.size(); i++) { if (i > 0) out += ','; out += csvEscape(keys[i], keys[i].empty()); }
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
					std::string key = prefix;
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
			// Same first-appearance ordering as writeArrayOfObjects: columns follow
			// the ObjectMapT insertion order of the flattened paths, not a
			// lexicographic sort. Collision detection rides on the dedup set.
			std::vector<std::pair<std::string, std::string>> flat;
			std::unordered_set<std::string> flatSeen;
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
					std::string key = prefix;
					// Two different paths flattening onto one column would
					// silently drop one value; reject instead of losing data.
					if (!flatSeen.insert(key).second)
						throw asvJSONError("CSV: flattened key collision on \"" + key + "\"");
					flat.emplace_back(std::move(key), csvCell(v));
				}
			};
			flatten(flatten, this, "", flattenVisited);
			for (size_t i = 0; i < flat.size(); i++) {
				if (i > 0) out += ',';
				out += csvEscape(flat[i].first, flat[i].first.empty());
			}
			out += '\n';
			for (size_t i = 0; i < flat.size(); i++) {
				if (i > 0) out += ',';
				out += flat[i].second;
			}
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
			// A scalar root is emitted as a lone headerless data line. The
			// decoder treats the first line as a header, so a scalar does not
			// round-trip through fromCSV -- this is the documented design
			// (a scalar CSV export is a data-only dump).
			out += csvCell(this); out += '\n';
			break;
	}
}

} // namespace asvJSONInternal
