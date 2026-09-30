#pragma once
// BSON serialization/parsing for asvJSON++
//
// Known conventions (documented, not bugs):
// - Duplicate document keys are last-wins (insert_or_assign), matching the JSON
//   parser; BSON explicitly requires unique keys but the decoder stays lenient.
// - Binary subtype 0x03 ("UUID old") always reads and discards the inner
//   4-byte length; the block is treated as the remaining blen - 4 bytes
//   (for blen < 4 the whole payload is kept) and the cursor is realigned to
//   the end of the binary block, so malformed input can never smuggle bytes
//   past a forged inner length.
// - BSON binary subtypes collide with asvJSON EXTENSION subtypes: 0x00 and
//   0x05 decode back as BINARY (the extension type is lost) and 0x03 is
//   consumed by the UUID(OLD) branch. EXTENSION values with ext_type in
//   {0x00, 0x03, 0x05} therefore do not survive a BSON round-trip unmodified.
// - Element type 0x06 (Undefined, deprecated by the spec) normalizes to null.
// - Regex element (0x0B) options are stored verbatim; they are not validated
//   against the spec's 'ilmsux' set or ordering (lenient read).
// - String values (0x02) are length-prefixed, so an embedded NUL is technically
//   invalid BSON; makeString strings are rejected by toBSON() rather than
//   emitted, mirroring the key check.
// - Timestamp (0x11) follows the spec: a uint64 with the increment in the low
//   32 bits (stored first) and the seconds in the high 32 bits.  asvJSONValue
//   stores only the seconds, so the increment is always encoded as 0 and a
//   non-zero increment from an external document is discarded on decode.
// - MAX_OBJECT_SIZE/MAX_ARRAY_SIZE/MAX_STRING_LEN/MAX_NESTING_DEPTH: the
//   decoder enforces MAX_NESTING_DEPTH, MAX_STRING_LEN (keys/pattern+options),
//   MAX_ARRAY_SIZE (for sequential array documents), and MAX_OBJECT_SIZE (for
//   all document objects and for non-sequential BSON arrays, which remain
//   OBJECTs in the tree).  The encoder does NOT proactively cap by MAX_* on
//   large out-of-contract trees (it will still generate bytes up to the
//   platform and the 32-bit length cap), mirroring the library's general
//   encoding policy.
// - toBSON() throws "BSON: cannot serialize unsupported type..." for types that
//   have no BSON mapping (e.g. CUSTOM_TAG), rather than silently emitting null.
//   This is documented behavior.

// Included explicitly rather than relied on transitively via core.hpp (same
// rule as ubjson.hpp / cbor.hpp): this file uses memcpy, std::numeric_limits
// and the fixed-width integer types directly.
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include "../core.hpp"

namespace asvJSONInternal {

inline void writeLE32(std::vector<uint8_t>& out, uint32_t v) {
	out.push_back(static_cast<uint8_t>(v));
	out.push_back(static_cast<uint8_t>(v >> 8));
	out.push_back(static_cast<uint8_t>(v >> 16));
	out.push_back(static_cast<uint8_t>(v >> 24));
}

inline void writeLE64(std::vector<uint8_t>& out, uint64_t v) {
	for (int i = 0; i < 8; i++) { out.push_back(static_cast<uint8_t>(v >> (i * 8))); }
}


inline void asvJSONValue::toBSON(std::vector<uint8_t>& out) const {
	using T = asvJSONValue::Type;
	switch (type) {
		case T::DOUBLE: {
			out.push_back(0x01);
			uint64_t dval; memcpy(&dval, &dbl, sizeof(dval));
			writeLE64(out, dval);
			break;
		}
		case T::STRING: {
			// slen = size+1 must fit in uint32 (the +1 is the terminator), and
			// BSON strings must not contain embedded NULs. Guard both before
			// casting, mirroring the OBJECT/ARRAY length checks.
			if (str_data.size() >= static_cast<size_t>(std::numeric_limits<uint32_t>::max())) throw asvJSONError("BSON string too large");
			if (str_data.find('\0') != std::string::npos) throw asvJSONError("BSON string contains NUL byte");
			out.push_back(0x02);
			uint32_t slen = static_cast<uint32_t>(str_data.size()) + 1;
			writeLE32(out, slen);
			out.insert(out.end(), str_data.begin(), str_data.end());
			out.push_back(0);
			break;
		}
		case T::OBJECT: {
			out.push_back(0x03);
			std::vector<uint8_t> sub;
			sub.reserve(128);
			size_t subSizePos = sub.size();
			writeLE32(sub, 0);
			if (obj) {
				for (const auto& [k, v] : *obj) {
					// BSON keys are NUL-terminated C strings; a key containing a
					// NUL byte cannot be represented (it would truncate on read).
					if (k.find('\0') != std::string::npos) throw asvJSONError("BSON key contains NUL byte");
					std::vector<uint8_t> elem;
					elem.reserve(64 + k.size());
					size_t elemStart = elem.size();
					v->toBSON(elem);
					if (elem.size() > elemStart) {
						elem.insert(elem.begin() + elemStart + 1, k.begin(), k.end());
						elem.insert(elem.begin() + elemStart + 1 + k.size(), 0);
						sub.insert(sub.end(), elem.begin() + elemStart, elem.end());
					}
				}
			}
			sub.push_back(0);
			if (sub.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) throw asvJSONError("BSON document too large");
			uint32_t totalSize = static_cast<uint32_t>(sub.size());
			sub[subSizePos] = static_cast<uint8_t>(totalSize);
			sub[subSizePos + 1] = static_cast<uint8_t>(totalSize >> 8);
			sub[subSizePos + 2] = static_cast<uint8_t>(totalSize >> 16);
			sub[subSizePos + 3] = static_cast<uint8_t>(totalSize >> 24);
			out.insert(out.end(), sub.begin(), sub.end());
			break;
		}
		case T::ARRAY: {
			out.push_back(0x04);
			std::vector<uint8_t> sub;
			sub.reserve(128);
			size_t subSizePos = sub.size();
			writeLE32(sub, 0);
			if (arr) {
				for (size_t i = 0; i < arr->size(); i++) {
					std::vector<uint8_t> elem;
					elem.reserve(64);
					size_t elemStart = elem.size();
					(*arr)[i]->toBSON(elem);
					if (elem.size() > elemStart) {
						char idx[32];
						auto [ptr, ec] = std::to_chars(idx, idx + sizeof(idx), i);
						if (ec != std::errc()) throw asvJSONError("BSON array index too large");
						size_t n = static_cast<size_t>(ptr - idx);
						elem.insert(elem.begin() + elemStart + 1, idx, idx + n);
						elem.insert(elem.begin() + elemStart + 1 + n, 0);
						sub.insert(sub.end(), elem.begin() + elemStart, elem.end());
					}
				}
			}
			sub.push_back(0);
			if (sub.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) throw asvJSONError("BSON document too large");
			uint32_t totalSize = static_cast<uint32_t>(sub.size());
			sub[subSizePos] = static_cast<uint8_t>(totalSize);
			sub[subSizePos + 1] = static_cast<uint8_t>(totalSize >> 8);
			sub[subSizePos + 2] = static_cast<uint8_t>(totalSize >> 16);
			sub[subSizePos + 3] = static_cast<uint8_t>(totalSize >> 24);
			out.insert(out.end(), sub.begin(), sub.end());
			break;
		}
		case T::BINARY: {
			// A size that overflows uint32 would silently truncate the length
			// field (the OBJECT/ARRAY branches already guard their sizes).
			if (bin_data.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) throw asvJSONError("BSON binary too large");
			out.push_back(0x05);
			uint32_t blen = static_cast<uint32_t>(bin_data.size());
			writeLE32(out, blen);
			out.push_back(0);
			out.insert(out.end(), bin_data.begin(), bin_data.end());
			break;
		}
		case T::BOOL_VAL: {
			out.push_back(0x08);
			out.push_back(flag ? 1 : 0);
			break;
		}
		case T::DATETIME: {
			out.push_back(0x09);
			int64_t t = timestamp;
			// Normalize to the documented [0, 999] millisecond range, exactly as
			// the MessagePack encoder does. The members are public, so a manually
			// built value really can hold any int here. This must happen BEFORE
			// the overflow math below: it is what makes both thresholds exact,
			// and it removes the old m<0 branch, which was reachable (public
			// members, no normalization) and wrong -- it reported overflow for
			// values that in fact encode fine, e.g. t = INT64_MIN/1000, m = -1.
			int64_t m = datetime_ms;
			if (m < 0) m = 0;
			if (m > 999) m = 999;
			// Guard t*1000+m against signed overflow without performing it.
			bool overflow = false;
			if (t > 0)
				// max/m here, so no wrap: overflow iff t > (max - m) / 1000.
				overflow = t > (std::numeric_limits<int64_t>::max() - m) / 1000;
			else if (t < 0) {
				// overflow iff t*1000 + m < INT64_MIN. Derived without
				// overflowing the subtraction: min = 1000*minQ + minR with
				// minR in [-999,0]; then min - m = 1000*minQ + (minR - m)
				// and the exact threshold for t is:
				//   t <= minQ + (minR - m)/1000 - 1   (C++ trunc division).
				// minR = INT64_MIN % 1000 = -808 and m <= 999, so (minR - m) is
				// in [-1807, -808]: always negative, which is precisely what
				// makes the truncating division below give the exact bound.
				int64_t d = (std::numeric_limits<int64_t>::min() % 1000) - m; // in [-1807, -808]
				int64_t threshold = (std::numeric_limits<int64_t>::min() / 1000) + (d / 1000) - 1;
				overflow = t <= threshold;
			}
			if (overflow)
				throw asvJSONError("BSON datetime out of int64 milliseconds range");
			int64_t ms = t * 1000 + m;
			writeLE64(out, static_cast<uint64_t>(ms));
			break;
		}
		case T::NULL_VAL: out.push_back(0x0A); break;
		case T::REGEX: {
			out.push_back(0x0B);
			size_t sep = str_data.rfind('\0');
			std::string_view pattern = (sep != std::string_view::npos) ? std::string_view(str_data.data(), sep) : str_data;
			std::string_view opts = (sep != std::string_view::npos) ? std::string_view(str_data.data() + sep + 1, str_data.size() - sep - 1) : std::string_view();
			out.insert(out.end(), pattern.begin(), pattern.end());
			out.push_back(0);
			out.insert(out.end(), opts.begin(), opts.end());
			out.push_back(0);
			break;
		}
		case T::INT: {
			int64_t n = num;
			if (n >= INT32_MIN && n <= INT32_MAX) {
				out.push_back(0x10);
				int32_t v = static_cast<int32_t>(n);
				writeLE32(out, static_cast<uint32_t>(v));
			} else {
				out.push_back(0x12);
				writeLE64(out, static_cast<uint64_t>(n));
			}
			break;
		}
		case T::TIMESTAMP: {
			// BSON 0x11 is a uint64 whose low 32 bits are the increment and whose
			// high 32 bits are the seconds, so the increment is stored first.
			// The seconds field is uint32; values outside [0, 2^32-1] would
			// silently wrap via the cast, so reject them instead.
			if (num < 0 || num > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
				throw asvJSONError("BSON timestamp out of uint32 seconds range");
			out.push_back(0x11);
			uint32_t t = static_cast<uint32_t>(num);
			writeLE32(out, 0);
			writeLE32(out, t);
			break;
		}
		case T::OBJECTID: {
			out.push_back(0x07);
			if (str_data.size() >= 12)
				out.insert(out.end(), str_data.begin(), str_data.begin() + 12);
			else {
				// Defensive: makeObjectId enforces exactly 12 bytes, so this
				// path is unreachable via public API.  Guard against direct
				// struct construction with short data.
				out.insert(out.end(), str_data.begin(), str_data.end());
				for (size_t i = str_data.size(); i < 12; i++) out.push_back(0);
			}
			break;
		}
		case T::EXTENSION: {
			if (bin_data.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) throw asvJSONError("BSON extension too large");
			out.push_back(0x05);
			uint32_t blen = static_cast<uint32_t>(bin_data.size());
			writeLE32(out, blen);
			out.push_back(static_cast<uint8_t>(ext_type));
			out.insert(out.end(), bin_data.begin(), bin_data.end());
			break;
		}
		default:
			throw asvJSONError("BSON: cannot serialize unsupported type " + std::to_string(static_cast<int>(type)));
	}
}

// BSON Parser

inline void readLE32(const uint8_t* data, size_t& pos, uint32_t& v) {
	v = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) | (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
	pos += 4;
}

inline void readLE64Double(const uint8_t* data, size_t& pos, double& d) {
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
	pos += 8;
	memcpy(&d, &v, sizeof(d));
}

inline std::unique_ptr<asvJSONValue> parseBSON(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth) {
	if (depth > asvJSONValue::MAX_NESTING_DEPTH) throw asvJSONError("BSON nesting too deep");
	if (pos + 4 > dataLen) throw asvJSONError("BSON document too short");
	uint32_t docSize;
	size_t docStart = pos;
	readLE32(data, pos, docSize);
	if (docSize < 5 || docSize > dataLen - docStart) throw asvJSONError("BSON invalid document size");
	size_t docEnd = docStart + docSize - 1;
	auto obj = asvJSONValue::makeObject();
	if (!obj) throw asvJSONError("BSON alloc failed");
	while (pos < docEnd) {
		// Enforce MAX_OBJECT_SIZE on the way in. The factory makeObject() does
		// not cap later insert_or_assign() calls, and a BSON element is only
		// 3 bytes at its smallest (type + 1-byte key + NUL), so a 16 MB hostile
		// document can mint millions of members -- well past the 1M cap the
		// library enforces everywhere else. Note this also covers a 0x04 array
		// whose keys are NOT sequential: that one stays an OBJECT, and so is
		// governed by this check rather than the MAX_ARRAY_SIZE one below.
		if (obj->obj->size() >= asvJSONValue::MAX_OBJECT_SIZE) throw asvJSONError("BSON object too large");
		uint8_t elemType = data[pos++];
		size_t keyStart = pos;
		while (pos < docEnd && data[pos] != 0) pos++;
		if (pos >= docEnd) throw asvJSONError("BSON missing key null");
		size_t keyLen = pos - keyStart;
		if (keyLen > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("BSON key too long");
		std::string key(reinterpret_cast<const char*>(data + keyStart), keyLen);
		pos++;
		switch (elemType) {
			case 0x01: { if (pos + 8 > docEnd) throw asvJSONError("BSON double: unexpected end"); double d; readLE64Double(data, pos, d); auto v1 = asvJSONValue::makeDouble(d); if (!v1) throw asvJSONError("BSON double alloc failed"); obj->obj->insert_or_assign(key, std::move(v1)); break; }
			case 0x02: {
				if (pos + 4 > docEnd) throw asvJSONError("BSON string");
				uint32_t slen; readLE32(data, pos, slen);
				if (slen < 1 || slen > docEnd - pos) throw asvJSONError("BSON string invalid");
				if (data[pos + slen - 1] != 0) throw asvJSONError("BSON string not NUL-terminated");
				auto val = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), slen - 1);
				pos += slen;
				if (!val) throw asvJSONError("BSON string too long");
				obj->obj->insert_or_assign(key, std::move(val));
				break;
			}
			case 0x03: {
				// Pass the parent's docEnd as the limit so a nested document can
				// never extend past its parent's boundary (otherwise a forged
				// child size would be validated only against the whole buffer).
				auto subObj = parseBSON(data, pos, docEnd, depth + 1);
				obj->obj->insert_or_assign(key, std::move(subObj));
				break;
			}
			case 0x04: {
				auto subArr = parseBSON(data, pos, docEnd, depth + 1);
				if (subArr && subArr->obj && !subArr->obj->empty()) {
					bool sequential = true;
					char idxBuf[32];
					for (size_t i = 0; i < subArr->obj->size(); i++) {
						auto [ptr, ec] = std::to_chars(idxBuf, idxBuf + sizeof(idxBuf), i);
						if (ec != std::errc()) { sequential = false; break; }
						if (subArr->obj->find(std::string(idxBuf, static_cast<size_t>(ptr - idxBuf))) == subArr->obj->end()) { sequential = false; break; }
					}
					if (sequential) {
						if (subArr->obj->size() > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("BSON array too large");
						subArr->type = asvJSONValue::ARRAY;
						auto newArr = std::make_unique<std::vector<std::unique_ptr<asvJSONValue>>>();
						newArr->resize(subArr->obj->size());
						for (size_t i = 0; i < subArr->obj->size(); i++) {
							auto [ptr, ec] = std::to_chars(idxBuf, idxBuf + sizeof(idxBuf), i);
							auto it = subArr->obj->find(std::string(idxBuf, static_cast<size_t>(ptr - idxBuf)));
							(*newArr)[i] = std::move(it->second);
						}
						subArr->obj.reset();
						subArr->arr = std::move(newArr);
					}
				} else if (subArr && subArr->type == asvJSONValue::OBJECT) {
					// Empty BSON array document  ->  empty asvJSON array
					subArr->type = asvJSONValue::ARRAY;
					subArr->obj.reset();
					subArr->arr = std::make_unique<std::vector<std::unique_ptr<asvJSONValue>>>();
				}
				obj->obj->insert_or_assign(key, std::move(subArr));
				break;
			}
			case 0x05: {
				if (pos + 4 > docEnd) throw asvJSONError("BSON binary");
				uint32_t blen; readLE32(data, pos, blen);
				if (pos >= docEnd) throw asvJSONError("BSON binary subtype");
				uint8_t subtype = data[pos++];
				// The binary block is blen bytes right after the subtype byte.
				// Validate that it is fully inside the document, then ALWAYS
				// advance pos to blockEnd -- a malformed inner length (subtype
				// 0x03) must never leave pos inside the blob, or the next
				// element would be read out of attacker-controlled bytes.
				if (blen > docEnd - pos) throw asvJSONError("BSON binary overflow");
				size_t blockEnd = pos + blen;
				size_t dataStart = pos;
				// Named payloadLen, not dataLen: the enclosing parseBSON
				// parameter `dataLen` is the limit for the WHOLE buffer, while
				// this is the payload length of this one binary field. The two
				// used to share a name, and the local shadowed the parameter.
				size_t payloadLen = blen;
				if (subtype == 0x03 && blen >= 4) {
					// UUID (old): the payload leads with a 4-byte length.
					// The inner length is read but not trusted for sizing --
					// payloadLen is always blen - 4 so the cursor is always
					// realigned to blockEnd regardless of the inner value.
					uint32_t oldLen; readLE32(data, pos, oldLen);
					(void)oldLen;
					dataStart = pos;
					payloadLen = blen - 4;
				}
				if (subtype == 0x00 || subtype == 0x03 || subtype == 0x05) {
					auto val = asvJSONValue::makeBinary(data + dataStart, payloadLen);
					if (!val) throw asvJSONError("BSON binary too large");
					obj->obj->insert_or_assign(key, std::move(val));
				} else {
					auto val = asvJSONValue::makeExtension(static_cast<int8_t>(subtype), data + dataStart, payloadLen);
					if (!val) throw asvJSONError("BSON extension too large");
					obj->obj->insert_or_assign(key, std::move(val));
				}
				pos = blockEnd;
				break;
			}
			case 0x06: { // Undefined (deprecated by the spec) normalizes to null, like 0x0A.
				auto v6 = asvJSONValue::makeNull(); if (!v6) throw asvJSONError("BSON null alloc failed"); obj->obj->insert_or_assign(key, std::move(v6)); break; }
			case 0x07: {
				if (pos + 12 > docEnd) throw asvJSONError("BSON objectid");
				auto val = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
				pos += 12;
				if (!val) throw asvJSONError("BSON objectid alloc failed");
				obj->obj->insert_or_assign(key, std::move(val));
				break;
			}
			case 0x08: {
				if (pos + 1 > docEnd) throw asvJSONError("BSON bool");
				auto v8 = asvJSONValue::makeBool(data[pos++] != 0); if (!v8) throw asvJSONError("BSON bool alloc failed"); obj->obj->insert_or_assign(key, std::move(v8)); break;
			}
			case 0x09: {
				if (pos + 8 > docEnd) throw asvJSONError("BSON datetime");
				uint64_t ms = 0;
				for (int i = 0; i < 8; i++) ms |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
				pos += 8;
				// BSON stores signed 64-bit UTC milliseconds; interpret as signed.
				int64_t ms64 = static_cast<int64_t>(ms);
				int64_t q = ms64 / 1000;
				int64_t r = ms64 % 1000;
				if (r < 0) { q--; r += 1000; } // floor semantics for negative values
				auto v9 = asvJSONValue::makeDateTime(static_cast<time_t>(q), static_cast<int>(r));
				if (!v9) throw asvJSONError("BSON datetime alloc failed");
				obj->obj->insert_or_assign(key, std::move(v9));
				break;
			}
			case 0x0A: { auto vA = asvJSONValue::makeNull(); if (!vA) throw asvJSONError("BSON null alloc failed"); obj->obj->insert_or_assign(key, std::move(vA)); break; }
			case 0x0B: {
				// Pattern and options are NUL-terminated C strings. Options are
				// stored verbatim -- the spec's 'ilmsux' set/order is not
				// enforced (lenient read; see header).
				size_t patStart = pos;
				while (pos < docEnd && data[pos] != 0) pos++;
				if (pos >= docEnd) throw asvJSONError("BSON regex missing pattern terminator");
				size_t patLen = pos - patStart;
				pos++;
				size_t optStart = pos;
				while (pos < docEnd && data[pos] != 0) pos++;
				if (pos >= docEnd) throw asvJSONError("BSON regex missing options terminator");
				size_t optLen = pos - optStart;
				pos++;
				if (patLen + optLen > asvJSONValue::MAX_STRING_LEN - 2) throw asvJSONError("BSON regex too large");
				std::string pattern(reinterpret_cast<const char*>(data + patStart), patLen);
				std::string options(reinterpret_cast<const char*>(data + optStart), optLen);
				auto val = asvJSONValue::makeRegex(pattern.c_str(), options.c_str());
				if (!val) throw asvJSONError("BSON regex invalid or too large");
				obj->obj->insert_or_assign(key, std::move(val));
				break;
			}
			case 0x10: {
				if (pos + 4 > docEnd) throw asvJSONError("BSON int32");
				uint32_t v; readLE32(data, pos, v);
				auto v10 = asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int32_t>(v)));
				if (!v10) throw asvJSONError("BSON int32 alloc failed");
				obj->obj->insert_or_assign(key, std::move(v10));
				break;
			}
			case 0x11: {
				if (pos + 8 > docEnd) throw asvJSONError("BSON timestamp");
				uint32_t inc; readLE32(data, pos, inc);
				uint32_t sec; readLE32(data, pos, sec);
				// BSON 0x11 is a uint64 stored as {increment, seconds}: the low
				// 32 bits carry the increment, the high 32 bits the seconds.
				// asvJSONValue::TIMESTAMP holds only the seconds value, so the
				// increment is discarded -- toBSON() always writes 0, and a
				// non-zero increment read from an external document is dropped.
				(void)inc;
				auto v11 = asvJSONValue::makeTimestamp(static_cast<int64_t>(sec));
				if (!v11) throw asvJSONError("BSON timestamp alloc failed");
				obj->obj->insert_or_assign(key, std::move(v11));
				break;
			}
			case 0x12: {
				if (pos + 8 > docEnd) throw asvJSONError("BSON int64");
				uint64_t v = 0;
				for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
				pos += 8;
				auto v12 = asvJSONValue::makeInt(static_cast<int64_t>(v));
				if (!v12) throw asvJSONError("BSON int64 alloc failed");
				obj->obj->insert_or_assign(key, std::move(v12));
				break;
			}
			case 0x13: {
				// Decimal128 is a 128-bit IEEE-754 decimal float that cannot be
				// represented losslessly by this library; fail rather than silently
				// replacing the value with null. This is unsupported whether the
				// 16-byte payload is present or truncated, so there is no point
				// reading it: the exception unwinds the whole parse and no caller
				// ever observes `pos` again. (The former `pos += 16` was dead code
				// -- it fed nobody.)
				throw asvJSONError("BSON decimal128 is not supported");
			}
			default: throw asvJSONError(std::string("BSON unknown element type: ") + std::to_string(elemType));
		}
	}
	// BSON documents must end with exactly one 0x00 byte at docEnd.
	if (pos != docEnd || data[docEnd] != 0) throw asvJSONError("BSON missing terminator");
	pos = docEnd + 1;
	return obj;
}

} // namespace asvJSONInternal
