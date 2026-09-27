#pragma once
// MessagePack (https://msgpack.org) serialization/parsing for asvJSON++
//
// asvJSON-specific extension types (NOT part of the MessagePack spec -- an
// external consumer only understands the -1 timestamp form):
//   ext  1    ObjectId    --  12 raw bytes, MongoDB ObjectId (MongoDB convention)
//   ext  2    Regex       --  "pattern\0options" (asvJSON makeRegex wire form)
//   ext  3    Timestamp   --  fixext8 int64 epoch seconds (asvJSON-specific;
//                             exists to distinguish TIMESTAMP from DATETIME,
//                             which have no MessagePack counterparts)
//   ext  4    Custom Tag  --  msgpack-encoded name string followed by the
//                             msgpack-encoded annotated value (null if none),
//                             mirroring the CBOR tag-258 [name, value] layout
//   ext -1    Datetime    --  the standard MessagePack timestamp (fixext4
//                             32-bit seconds, fixext8 34-bit sec + 30-bit ns,
//                             or the ext8 timestamp-96 12-byte form)
//
// Duplicate map keys are last-wins, matching the JSON and CBOR parsers.
//
// A well-known ext type carrying a body of the wrong length (ObjectId != 12
// bytes, Timestamp != 8 bytes) decodes to a plain EXTENSION node keeping the
// type byte and the raw body, not to null -- the same no-data-loss fallback
// the custom-tag (ext 4) decoder uses. The encoder never emits those forms, so
// this only affects hand-crafted or foreign input; a decode -> encode ->
// decode round-trip of such a node is stable.

#include "../core.hpp"

namespace asvJSONInternal {

inline void writeMsgPackExt(std::vector<uint8_t>& out, int8_t extType, const uint8_t* data, size_t len) {
	if (len == 1) { out.push_back(0xD4); }
	else if (len == 2) { out.push_back(0xD5); }
	else if (len == 4) { out.push_back(0xD6); }
	else if (len == 8) { out.push_back(0xD7); }
	else if (len == 16) { out.push_back(0xD8); }
	else if (len <= 0xFF) { out.push_back(0xC7); out.push_back(static_cast<uint8_t>(len)); }
	else if (len <= 0xFFFF) { out.push_back(0xC8); out.push_back(static_cast<uint8_t>(len >> 8)); out.push_back(static_cast<uint8_t>(len)); }
	else { out.push_back(0xC9); out.push_back(static_cast<uint8_t>(len >> 24)); out.push_back(static_cast<uint8_t>(len >> 16)); out.push_back(static_cast<uint8_t>(len >> 8)); out.push_back(static_cast<uint8_t>(len)); }
	out.push_back(static_cast<uint8_t>(extType));
	if (len && data) out.insert(out.end(), data, data + len);
}


inline void asvJSONValue::toMessagePack(std::vector<uint8_t>& out) const {
	using T = asvJSONValue::Type;
	switch (type) {
		case T::NULL_VAL: out.push_back(0xC0); break;
		case T::BOOL_VAL: out.push_back(flag ? 0xC3 : 0xC2); break;
		case T::INT: {
			int64_t n = num;
			if (n >= 0) {
				if (n <= 0x7F) { out.push_back(static_cast<uint8_t>(n)); }
				else if (n <= 0xFF) { out.push_back(0xCC); out.push_back(static_cast<uint8_t>(n)); }
				else if (n <= 0xFFFF) { out.push_back(0xCD); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); }
				else if (n <= 0xFFFFFFFFu) { out.push_back(0xCE); out.push_back(static_cast<uint8_t>(n >> 24)); out.push_back(static_cast<uint8_t>(n >> 16)); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); }
				else { out.push_back(0xCF); out.push_back(static_cast<uint8_t>(n >> 56)); out.push_back(static_cast<uint8_t>(n >> 48)); out.push_back(static_cast<uint8_t>(n >> 40)); out.push_back(static_cast<uint8_t>(n >> 32)); out.push_back(static_cast<uint8_t>(n >> 24)); out.push_back(static_cast<uint8_t>(n >> 16)); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); }
			} else {
				if (n >= -32) { out.push_back(static_cast<uint8_t>(static_cast<int8_t>(n))); }
				else if (n >= INT8_MIN) { out.push_back(0xD0); out.push_back(static_cast<uint8_t>(static_cast<int8_t>(n))); }
				else if (n >= INT16_MIN) { out.push_back(0xD1); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); }
				else if (n >= INT32_MIN) { out.push_back(0xD2); out.push_back(static_cast<uint8_t>(n >> 24)); out.push_back(static_cast<uint8_t>(n >> 16)); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); }
				else { out.push_back(0xD3); out.push_back(static_cast<uint8_t>(n >> 56)); out.push_back(static_cast<uint8_t>(n >> 48)); out.push_back(static_cast<uint8_t>(n >> 40)); out.push_back(static_cast<uint8_t>(n >> 32)); out.push_back(static_cast<uint8_t>(n >> 24)); out.push_back(static_cast<uint8_t>(n >> 16)); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); }
			}
			break;
		}
		case T::DOUBLE: {
			if (is_float32) {
				out.push_back(0xCA);
				float f = static_cast<float>(dbl);
				uint32_t fval;
				memcpy(&fval, &f, sizeof(fval));
				out.push_back(static_cast<uint8_t>(fval >> 24));
				out.push_back(static_cast<uint8_t>(fval >> 16));
				out.push_back(static_cast<uint8_t>(fval >> 8));
				out.push_back(static_cast<uint8_t>(fval));
			} else {
				out.push_back(0xCB);
				uint64_t dval;
				memcpy(&dval, &dbl, sizeof(dval));
				out.push_back(static_cast<uint8_t>(dval >> 56));
				out.push_back(static_cast<uint8_t>(dval >> 48));
				out.push_back(static_cast<uint8_t>(dval >> 40));
				out.push_back(static_cast<uint8_t>(dval >> 32));
				out.push_back(static_cast<uint8_t>(dval >> 24));
				out.push_back(static_cast<uint8_t>(dval >> 16));
				out.push_back(static_cast<uint8_t>(dval >> 8));
				out.push_back(static_cast<uint8_t>(dval));
			}
			break;
		}
		case T::STRING: {
			if (str_data.size() <= 31) { out.push_back(static_cast<uint8_t>(0xA0 | str_data.size())); }
			else if (str_data.size() <= 0xFF) { out.push_back(0xD9); out.push_back(static_cast<uint8_t>(str_data.size())); }
			else if (str_data.size() <= 0xFFFF) { out.push_back(0xDA); out.push_back(static_cast<uint8_t>(str_data.size() >> 8)); out.push_back(static_cast<uint8_t>(str_data.size())); }
			else { out.push_back(0xDB); out.push_back(static_cast<uint8_t>(str_data.size() >> 24)); out.push_back(static_cast<uint8_t>(str_data.size() >> 16)); out.push_back(static_cast<uint8_t>(str_data.size() >> 8)); out.push_back(static_cast<uint8_t>(str_data.size())); }
			out.insert(out.end(), str_data.begin(), str_data.end());
			break;
		}
		case T::BINARY: {
			if (bin_data.size() <= 0xFF) { out.push_back(0xC4); out.push_back(static_cast<uint8_t>(bin_data.size())); }
			else if (bin_data.size() <= 0xFFFF) { out.push_back(0xC5); out.push_back(static_cast<uint8_t>(bin_data.size() >> 8)); out.push_back(static_cast<uint8_t>(bin_data.size())); }
			else { out.push_back(0xC6); out.push_back(static_cast<uint8_t>(bin_data.size() >> 24)); out.push_back(static_cast<uint8_t>(bin_data.size() >> 16)); out.push_back(static_cast<uint8_t>(bin_data.size() >> 8)); out.push_back(static_cast<uint8_t>(bin_data.size())); }
			out.insert(out.end(), bin_data.begin(), bin_data.end());
			break;
		}
		case T::ARRAY: {
			if (!arr) { out.push_back(0x90); break; }
			if (arr->size() <= 0x0F) { out.push_back(static_cast<uint8_t>(0x90 | arr->size())); }
			else if (arr->size() <= 0xFFFF) { out.push_back(0xDC); out.push_back(static_cast<uint8_t>(arr->size() >> 8)); out.push_back(static_cast<uint8_t>(arr->size())); }
			else { out.push_back(0xDD); out.push_back(static_cast<uint8_t>(arr->size() >> 24)); out.push_back(static_cast<uint8_t>(arr->size() >> 16)); out.push_back(static_cast<uint8_t>(arr->size() >> 8)); out.push_back(static_cast<uint8_t>(arr->size())); }
			for (const auto& v : *arr) v->toMessagePack(out);
			break;
		}
		case T::OBJECT: {
			if (!obj) { out.push_back(0x80); break; }
			if (obj->size() <= 0x0F) { out.push_back(static_cast<uint8_t>(0x80 | obj->size())); }
			else if (obj->size() <= 0xFFFF) { out.push_back(0xDE); out.push_back(static_cast<uint8_t>(obj->size() >> 8)); out.push_back(static_cast<uint8_t>(obj->size())); }
			else { out.push_back(0xDF); out.push_back(static_cast<uint8_t>(obj->size() >> 24)); out.push_back(static_cast<uint8_t>(obj->size() >> 16)); out.push_back(static_cast<uint8_t>(obj->size() >> 8)); out.push_back(static_cast<uint8_t>(obj->size())); }
			for (const auto& [k, v] : *obj) {
				if (k.size() <= 31) { out.push_back(static_cast<uint8_t>(0xA0 | k.size())); }
				else if (k.size() <= 0xFF) { out.push_back(0xD9); out.push_back(static_cast<uint8_t>(k.size())); }
				else if (k.size() <= 0xFFFF) { out.push_back(0xDA); out.push_back(static_cast<uint8_t>(k.size() >> 8)); out.push_back(static_cast<uint8_t>(k.size())); }
				else { out.push_back(0xDB); out.push_back(static_cast<uint8_t>(k.size() >> 24)); out.push_back(static_cast<uint8_t>(k.size() >> 16)); out.push_back(static_cast<uint8_t>(k.size() >> 8)); out.push_back(static_cast<uint8_t>(k.size())); }
				out.insert(out.end(), k.begin(), k.end());
				v->toMessagePack(out);
			}
			break;
		}
		case T::DATETIME: {
			uint32_t ns = static_cast<uint32_t>(datetime_ms) * 1000000;
			// Defensive clamp; unreachable when datetime_ms is in its documented [0, 999] range.
			if (ns > 999999999) ns = 999999999;
			if (timestamp >= 0 && timestamp < 0x100000000LL && ns == 0) {
				// fixext4: seconds precision
				out.push_back(0xD6);
				out.push_back(static_cast<uint8_t>(-1)); // extType = 0xFF
				out.push_back(static_cast<uint8_t>((timestamp >> 24) & 0xFF));
				out.push_back(static_cast<uint8_t>((timestamp >> 16) & 0xFF));
				out.push_back(static_cast<uint8_t>((timestamp >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(timestamp & 0xFF));
			} else if (timestamp >= 0 && timestamp < (1LL << 34)) {
				// fixext8: 34-bit seconds + 30-bit nanoseconds
				out.push_back(0xD7);
				out.push_back(static_cast<uint8_t>(-1)); // extType = 0xFF
				uint64_t packed = (static_cast<uint64_t>(ns) << 34) | static_cast<uint64_t>(timestamp);
				for (int i = 7; i >= 0; i--) {
					out.push_back(static_cast<uint8_t>((packed >> (i * 8)) & 0xFF));
				}
			} else {
				// ext8 (timestamp 96): 4 bytes nanoseconds + 8 bytes seconds
				// (nanoseconds first, per the MessagePack timestamp 96 layout)
				out.push_back(0xC7);
				out.push_back(12);
				out.push_back(static_cast<uint8_t>(-1)); // extType = 0xFF
				out.push_back(static_cast<uint8_t>((ns >> 24) & 0xFF));
				out.push_back(static_cast<uint8_t>((ns >> 16) & 0xFF));
				out.push_back(static_cast<uint8_t>((ns >> 8) & 0xFF));
				out.push_back(static_cast<uint8_t>(ns & 0xFF));
				for (int i = 7; i >= 0; i--) {
					out.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
				}
			}
			break;
		}
		case T::EXTENSION:
			writeMsgPackExt(out, ext_type, bin_data.data(), bin_data.size());
			break;
		case T::OBJECTID: {
			if (str_data.size() != 12) { out.push_back(0xC0); break; }
			writeMsgPackExt(out, 1, reinterpret_cast<const uint8_t*>(str_data.data()), 12);
			break;
		}
		case T::TIMESTAMP: {
			// Store as fixext8 (0xD7) with type 3 for roundtrip preservation
			out.push_back(0xD7);
			out.push_back(static_cast<uint8_t>(3)); // extType=3 for Timestamp
			uint64_t n = static_cast<uint64_t>(num);
			for (int i = 7; i >= 0; i--) out.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
			break;
		}
		case T::REGEX: {
			if (str_data.empty()) { out.push_back(0xC0); break; }
			writeMsgPackExt(out, 2, reinterpret_cast<const uint8_t*>(str_data.data()), str_data.size());
			break;
		}
		case T::CUSTOM_TAG: {
			// Ext type 4 (asvJSON-specific): body = msgpack-encoded name string
			// followed by the msgpack-encoded value (an explicit nil when the
			// annotation has no value). Before this case a CUSTOM_TAG fell into
			// default: -> 0xC0 and silently degraded to null on decode.
			std::vector<uint8_t> body;
			if (str_data.size() <= 31) { body.push_back(static_cast<uint8_t>(0xA0 | str_data.size())); }
			else if (str_data.size() <= 0xFF) { body.push_back(0xD9); body.push_back(static_cast<uint8_t>(str_data.size())); }
			else if (str_data.size() <= 0xFFFF) { body.push_back(0xDA); body.push_back(static_cast<uint8_t>(str_data.size() >> 8)); body.push_back(static_cast<uint8_t>(str_data.size())); }
			else { body.push_back(0xDB); body.push_back(static_cast<uint8_t>(str_data.size() >> 24)); body.push_back(static_cast<uint8_t>(str_data.size() >> 16)); body.push_back(static_cast<uint8_t>(str_data.size() >> 8)); body.push_back(static_cast<uint8_t>(str_data.size())); }
			body.insert(body.end(), str_data.begin(), str_data.end());
			if (custom_value) custom_value->toMessagePack(body); else body.push_back(0xC0);
			writeMsgPackExt(out, 4, body.data(), body.size());
			break;
		}
		default: out.push_back(0xC0); break;
	}
}

// MessagePack Parser

// ext type 2 payload is a regex in makeRegex form: "pattern\0options"
inline std::unique_ptr<asvJSONValue> msgpackDecodeRegex(const uint8_t* data, size_t len) {
	if (len == 0) return asvJSONValue::makeNull();
	std::string s(reinterpret_cast<const char*>(data), len);
	size_t sep = s.rfind('\0');
	std::string pattern = (sep != std::string::npos) ? s.substr(0, sep) : s;
	std::string opts = (sep != std::string::npos) ? s.substr(sep + 1) : std::string();
	auto re = asvJSONValue::makeRegex(pattern.c_str(), opts.empty() ? nullptr : opts.c_str());
	if (!re) return asvJSONValue::makeNull();
	return re;
}

// ext type 4 payload is an asvJSON Custom Tag: a msgpack-encoded name string
// followed by the msgpack-encoded annotated value (nil when the annotation has
// none). Defined after parseMessagePack; used by every ext-carrying branch
// (0xC7/0xC8/0xC9 and fixext1/2/4/8/16 = 0xD4-0xD8).
inline std::unique_ptr<asvJSONValue> msgpackDecodeCustomTag(const uint8_t* data, size_t& pos, size_t len, size_t depth);

inline std::unique_ptr<asvJSONValue> parseMessagePack(const uint8_t* data, size_t& pos, size_t dataLen, size_t depth) {
	if (depth > asvJSONValue::MAX_NESTING_DEPTH) throw asvJSONError("MsgPack nesting too deep");
	if (pos >= dataLen) throw asvJSONError("MsgPack unexpected end");
	uint8_t tag = data[pos++];
	if (tag <= 0x7F) { return asvJSONValue::makeInt(tag); }
	if (tag >= 0xE0) { return asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int8_t>(tag))); }
	if (tag >= 0xA0 && tag <= 0xBF) {
		uint32_t len = tag & 0x1F;
		if (len > dataLen - pos) throw asvJSONError("MsgPack string too long");
		if (!isValidUTF8(data + pos, static_cast<size_t>(len))) throw asvJSONError("MsgPack invalid UTF-8 in string");
		auto val = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), len);
		pos += len;
		if (!val) throw asvJSONError("MsgPack string alloc failed");
		return val;
	}
	if (tag >= 0x90 && tag <= 0x9F) {
		uint32_t n = tag & 0x0F;
		auto arr = asvJSONValue::makeArray();
		if (!arr) throw asvJSONError("MsgPack array alloc failed");
		arr->arr->reserve(n);
		for (uint32_t i = 0; i < n; i++) arr->arr->push_back(parseMessagePack(data, pos, dataLen, depth + 1));
		return arr;
	}
	if (tag >= 0x80 && tag <= 0x8F) {
		uint32_t n = tag & 0x0F;
		auto obj = asvJSONValue::makeObject();
		if (!obj) throw asvJSONError("MsgPack map alloc failed");
		for (uint32_t i = 0; i < n; i++) {
			auto k = parseMessagePack(data, pos, dataLen, depth + 1);
			if (!k || k->type != asvJSONValue::STRING) throw asvJSONError("MsgPack non-string map key");
			auto v = parseMessagePack(data, pos, dataLen, depth + 1);
			if (!v) throw asvJSONError("MsgPack map value failed");
			obj->obj->insert_or_assign(k->str_data, std::move(v)); // last wins (duplicate keys), same convention as the JSON parser
		}
		return obj;
	}
	switch (tag) {
		case 0xC0: return asvJSONValue::makeNull();
		case 0xC2: return asvJSONValue::makeBool(false);
		case 0xC3: return asvJSONValue::makeBool(true);
		case 0xC4: case 0xC5: case 0xC6: {
			uint32_t len;
			if (tag == 0xC4) { if (pos >= dataLen) throw asvJSONError("MsgPack bin8"); len = data[pos++]; }
			else if (tag == 0xC5) { if (pos + 2 > dataLen) throw asvJSONError("MsgPack bin16"); len = (static_cast<uint32_t>(data[pos]) << 8) | data[pos + 1]; pos += 2; }
			else { if (pos + 4 > dataLen) throw asvJSONError("MsgPack bin32"); len = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3]; pos += 4; }
			if (len > dataLen - pos) throw asvJSONError("MsgPack bin overflow");
			auto val = asvJSONValue::makeBinary(data + pos, len);
			pos += len;
			if (!val) throw asvJSONError("MsgPack binary alloc failed");
			return val;
		}
		case 0xC7: {
			if (pos >= dataLen) throw asvJSONError("MsgPack ext8");
			uint8_t len = data[pos++];
			if (pos >= dataLen) throw asvJSONError("MsgPack ext8 type");
			int8_t extType = static_cast<int8_t>(data[pos++]);
			// Header conventions: 1=ObjectId, 2=Regex, 3=Timestamp,
			// 4=Custom Tag, -1=Datetime (MessagePack timestamp forms).
			if (len > dataLen - pos) throw asvJSONError("MsgPack ext8 overflow");
			// High-precision datetime (timestamp 96): len == 12 && extType == -1.
			// Per the MessagePack spec this form is only ever carried by ext8,
			// which is why the 0xC8/0xC9 branches below have no 12-byte datetime.
			if (len == 12 && extType == -1) {
				uint32_t ns = 0;
				for (int i = 3; i >= 0; i--) ns = (ns << 8) | data[pos++];
				uint64_t sec = 0;
				for (int i = 7; i >= 0; i--) sec = (sec << 8) | data[pos++];
				if (ns >= 1000000000)
					throw asvJSONError("MsgPack ext datetime ns overflow");
				// Wire seconds are signed (two's complement): a negative timestamp
				// lands here as a huge uint64 and must round-trip through time_t.
				int64_t ssec = static_cast<int64_t>(sec);
				if (ssec > static_cast<int64_t>(std::numeric_limits<time_t>::max()) ||
				    ssec < static_cast<int64_t>(std::numeric_limits<time_t>::min()))
					throw asvJSONError("MsgPack ext datetime seconds out of time_t range");
				auto d = asvJSONValue::makeDateTime(static_cast<time_t>(ssec), ns / 1000000);
				if (!d) throw asvJSONError("MsgPack datetime alloc failed");
				return d;
			}
			if (extType == 1) {
				if (len != 12) { auto v = asvJSONValue::makeExtension(extType, data + pos, len); pos += len; if (!v) throw asvJSONError("MsgPack extension too large"); return v; }
				auto val = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
				pos += len;
				if (!val) throw asvJSONError("MsgPack objectid alloc failed");
				return val;
			}
			if (extType == 2) {
				auto re = msgpackDecodeRegex(data + pos, len);
				pos += len;
				if (!re) throw asvJSONError("MsgPack regex alloc failed");
				return re;
			}
			if (extType == 3) {
				if (len != 8) { auto v = asvJSONValue::makeExtension(extType, data + pos, len); pos += len; if (!v) throw asvJSONError("MsgPack extension too large"); return v; }
				uint64_t ts = 0;
				for (int i = 7; i >= 0; i--) ts = (ts << 8) | data[pos++];
				auto v = asvJSONValue::makeTimestamp(static_cast<int64_t>(ts));
				if (!v) throw asvJSONError("MsgPack timestamp alloc failed");
				return v;
			}
			if (extType == 4) return msgpackDecodeCustomTag(data, pos, len, depth);
			auto val = asvJSONValue::makeExtension(extType, data + pos, len);
			pos += len;
			if (!val) throw asvJSONError("MsgPack extension too large");
			return val;
		}
		case 0xC8: {
			if (pos + 2 > dataLen) throw asvJSONError("MsgPack ext16");
			uint16_t len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
			pos += 2;
			if (pos >= dataLen) throw asvJSONError("MsgPack ext16 type");
			int8_t extType = static_cast<int8_t>(data[pos++]);
			if (len > dataLen - pos) throw asvJSONError("MsgPack ext16 overflow");
			// Note: the 12-byte datetime (timestamp 96) is handled only in the
			// 0xC7 branch -- MessagePack never emits it as ext16/ext32.
			if (extType == 1) {
				if (len != 12) { auto v = asvJSONValue::makeExtension(extType, data + pos, len); pos += len; if (!v) throw asvJSONError("MsgPack extension too large"); return v; }
				auto val = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
				pos += len;
				if (!val) throw asvJSONError("MsgPack objectid alloc failed");
				return val;
			}
			if (extType == 2) {
				auto re = msgpackDecodeRegex(data + pos, len);
				pos += len;
				if (!re) throw asvJSONError("MsgPack regex alloc failed");
				return re;
			}
			if (extType == 3) {
				if (len != 8) { auto v = asvJSONValue::makeExtension(extType, data + pos, len); pos += len; if (!v) throw asvJSONError("MsgPack extension too large"); return v; }
				uint64_t ts = 0;
				for (int i = 7; i >= 0; i--) ts = (ts << 8) | data[pos++];
				auto v = asvJSONValue::makeTimestamp(static_cast<int64_t>(ts));
				if (!v) throw asvJSONError("MsgPack timestamp alloc failed");
				return v;
			}
			if (extType == 4) return msgpackDecodeCustomTag(data, pos, len, depth);
			auto val = asvJSONValue::makeExtension(extType, data + pos, len);
			pos += len;
			if (!val) throw asvJSONError("MsgPack extension too large");
			return val;
		}
		case 0xC9: {
			if (pos + 4 > dataLen) throw asvJSONError("MsgPack ext32");
			uint32_t len = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
			pos += 4;
			if (pos >= dataLen) throw asvJSONError("MsgPack ext32 type");
			int8_t extType = static_cast<int8_t>(data[pos++]);
			if (len > dataLen - pos) throw asvJSONError("MsgPack ext32 overflow");
			// Note: the 12-byte datetime (timestamp 96) is handled only in the
			// 0xC7 branch -- MessagePack never emits it as ext16/ext32.
			if (extType == 1) {
				if (len != 12) { auto v = asvJSONValue::makeExtension(extType, data + pos, len); pos += len; if (!v) throw asvJSONError("MsgPack extension too large"); return v; }
				auto val = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
				pos += len;
				if (!val) throw asvJSONError("MsgPack objectid alloc failed");
				return val;
			}
			if (extType == 2) {
				auto re = msgpackDecodeRegex(data + pos, len);
				pos += len;
				if (!re) throw asvJSONError("MsgPack regex alloc failed");
				return re;
			}
			if (extType == 3) {
				// 32-bit wraparound-safe: accumulate unsigned like 0xC7/0xC8/0xD7.
				if (len != 8) { auto v = asvJSONValue::makeExtension(extType, data + pos, len); pos += len; if (!v) throw asvJSONError("MsgPack extension too large"); return v; }
				uint64_t ts = 0;
				for (int i = 7; i >= 0; i--) ts = (ts << 8) | data[pos++];
				auto v = asvJSONValue::makeTimestamp(static_cast<int64_t>(ts));
				if (!v) throw asvJSONError("MsgPack timestamp alloc failed");
				return v;
			}
			if (extType == 4) return msgpackDecodeCustomTag(data, pos, len, depth);
			auto val = asvJSONValue::makeExtension(extType, data + pos, len);
			pos += len;
			if (!val) throw asvJSONError("MsgPack extension too large");
			return val;
		}
		case 0xCA: {
			if (pos + 4 > dataLen) throw asvJSONError("MsgPack float32");
			uint32_t fval = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
			pos += 4;
			float f; memcpy(&f, &fval, sizeof(f));
			auto v = asvJSONValue::makeDouble(f);
			if (v) v->is_float32 = true;
			return v;
		}
		case 0xCB: {
			if (pos + 8 > dataLen) throw asvJSONError("MsgPack float64");
			uint64_t dval = 0;
			for (int i = 0; i < 8; i++) dval = (dval << 8) | data[pos + i];
			pos += 8;
			double d; memcpy(&d, &dval, sizeof(d));
			return asvJSONValue::makeDouble(d);
		}
		case 0xCC: { if (pos >= dataLen) throw asvJSONError("MsgPack uint8"); return asvJSONValue::makeInt(data[pos++]); }
		case 0xCD: { if (pos + 2 > dataLen) throw asvJSONError("MsgPack uint16"); uint32_t v = (static_cast<uint32_t>(data[pos]) << 8) | data[pos + 1]; pos += 2; return asvJSONValue::makeInt(v); }
		case 0xCE: { if (pos + 4 > dataLen) throw asvJSONError("MsgPack uint32"); uint32_t v = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3]; pos += 4; return asvJSONValue::makeInt(v); }
		case 0xCF: { if (pos + 8 > dataLen) throw asvJSONError("MsgPack uint64"); uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | data[pos + i]; pos += 8; if (v > 0x7FFFFFFFFFFFFFFFULL) throw asvJSONError("MsgPack uint64 out of range"); return asvJSONValue::makeInt(static_cast<int64_t>(v)); }
		case 0xD0: { if (pos >= dataLen) throw asvJSONError("MsgPack int8"); return asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int8_t>(data[pos++]))); }
		case 0xD1: { if (pos + 2 > dataLen) throw asvJSONError("MsgPack int16"); int16_t v = static_cast<int16_t>((static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1]); pos += 2; return asvJSONValue::makeInt(v); }
		case 0xD2: { if (pos + 4 > dataLen) throw asvJSONError("MsgPack int32"); uint32_t v = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3]; pos += 4; return asvJSONValue::makeInt(static_cast<int32_t>(v)); }
		case 0xD3: { if (pos + 8 > dataLen) throw asvJSONError("MsgPack int64"); uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | data[pos + i]; pos += 8; return asvJSONValue::makeInt(static_cast<int64_t>(v)); }
		case 0xD4: { if (pos >= dataLen) throw asvJSONError("MsgPack fixext1"); int8_t et = static_cast<int8_t>(data[pos++]); if (pos >= dataLen) throw asvJSONError("MsgPack fixext1 overflow"); if (et == 2) { auto re = msgpackDecodeRegex(data + pos, 1); pos += 1; if (!re) throw asvJSONError("MsgPack regex alloc failed"); return re; } if (et == 4) return msgpackDecodeCustomTag(data, pos, 1, depth); auto v = asvJSONValue::makeExtension(et, data + pos, 1); pos += 1; if (!v) throw asvJSONError("MsgPack extension alloc failed"); return v; }
		case 0xD5: { if (pos >= dataLen) throw asvJSONError("MsgPack fixext2"); int8_t et = static_cast<int8_t>(data[pos++]); if (pos + 2 > dataLen) throw asvJSONError("MsgPack fixext2 overflow"); if (et == 2) { auto re = msgpackDecodeRegex(data + pos, 2); pos += 2; if (!re) throw asvJSONError("MsgPack regex alloc failed"); return re; } if (et == 4) return msgpackDecodeCustomTag(data, pos, 2, depth); auto v = asvJSONValue::makeExtension(et, data + pos, 2); pos += 2; if (!v) throw asvJSONError("MsgPack extension alloc failed"); return v; }
		case 0xD6: {
			if (pos >= dataLen) throw asvJSONError("MsgPack fixext4"); int8_t et = static_cast<int8_t>(data[pos++]);
			if (pos + 4 > dataLen) throw asvJSONError("MsgPack fixext4 overflow");
			if (et == -1) {
				// Timestamp 32
				uint32_t t = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
				pos += 4;
				if (static_cast<int64_t>(t) > static_cast<int64_t>(std::numeric_limits<time_t>::max())) throw asvJSONError("MsgPack fixext4 datetime seconds out of time_t range");
				auto d = asvJSONValue::makeDateTime(static_cast<time_t>(t), 0);
				if (!d) throw asvJSONError("MsgPack datetime alloc failed");
				return d;
			}
			if (et == 2) { auto re = msgpackDecodeRegex(data + pos, 4); pos += 4; if (!re) throw asvJSONError("MsgPack regex alloc failed"); return re; }
			if (et == 4) return msgpackDecodeCustomTag(data, pos, 4, depth);
			auto v = asvJSONValue::makeExtension(et, data + pos, 4);
			pos += 4;
			if (!v) throw asvJSONError("MsgPack extension alloc failed");
			return v;
		}
		case 0xD7: {
			if (pos >= dataLen) throw asvJSONError("MsgPack fixext8");
			int8_t et = static_cast<int8_t>(data[pos++]);
			if (pos + 8 > dataLen) throw asvJSONError("MsgPack fixext8 overflow");
			if (et == 3) { 
				uint64_t ts = 0; 
				for (int i = 7; i >= 0; i--) ts = (ts << 8) | data[pos++]; 
				auto v = asvJSONValue::makeTimestamp(static_cast<int64_t>(ts)); 
				if (!v) throw asvJSONError("MsgPack timestamp alloc failed"); 
				return v; 
			} 
			if (et == -1) { 
				uint64_t data64 = 0; 
				for (int i = 0; i < 8; i++) data64 = (data64 << 8) | data[pos + i]; 
				pos += 8; 
				uint64_t sec = data64 & 0x3FFFFFFFFLL; 
				auto ns = static_cast<uint32_t>((data64 >> 34) & 0x3FFFFFFF); 
				if (ns >= 1000000000) throw asvJSONError("MsgPack fixext8 datetime ns overflow");
				if (static_cast<int64_t>(sec) > static_cast<int64_t>(std::numeric_limits<time_t>::max())) throw asvJSONError("MsgPack fixext8 datetime seconds out of time_t range");
				auto d = asvJSONValue::makeDateTime(static_cast<time_t>(sec), ns / 1000000);
				if (!d) throw asvJSONError("MsgPack datetime alloc failed"); 
				return d; 
			} 
			if (et == 2) { auto re = msgpackDecodeRegex(data + pos, 8); pos += 8; if (!re) throw asvJSONError("MsgPack regex alloc failed"); return re; }
			if (et == 4) return msgpackDecodeCustomTag(data, pos, 8, depth);
			auto v = asvJSONValue::makeExtension(et, data + pos, 8); 
			pos += 8; 
			if (!v) throw asvJSONError("MsgPack extension alloc failed"); 
			return v; 
		}
		case 0xD8: { if (pos >= dataLen) throw asvJSONError("MsgPack fixext16"); int8_t et = static_cast<int8_t>(data[pos++]); if (pos + 16 > dataLen) throw asvJSONError("MsgPack fixext16 overflow"); if (et == 2) { auto re = msgpackDecodeRegex(data + pos, 16); pos += 16; if (!re) throw asvJSONError("MsgPack regex alloc failed"); return re; } if (et == 4) return msgpackDecodeCustomTag(data, pos, 16, depth); auto v = asvJSONValue::makeExtension(et, data + pos, 16); pos += 16; if (!v) throw asvJSONError("MsgPack extension alloc failed"); return v; }
		case 0xD9: {
			if (pos >= dataLen) throw asvJSONError("MsgPack str8");
			uint8_t slen = data[pos++];
			if (slen > dataLen - pos) throw asvJSONError("MsgPack str8 overflow");
			if (!isValidUTF8(data + pos, static_cast<size_t>(slen))) throw asvJSONError("MsgPack invalid UTF-8 in string");
			auto val = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), slen);
			pos += slen;
			if (!val) throw asvJSONError("MsgPack string too long");
			return val;
		}
		case 0xDA: {
			if (pos + 2 > dataLen) throw asvJSONError("MsgPack str16");
			uint16_t slen = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
			pos += 2;
			if (slen > dataLen - pos) throw asvJSONError("MsgPack str16 overflow");
			if (!isValidUTF8(data + pos, static_cast<size_t>(slen))) throw asvJSONError("MsgPack invalid UTF-8 in string");
			auto val = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), slen);
			pos += slen;
			if (!val) throw asvJSONError("MsgPack string too long");
			return val;
		}
		case 0xDB: {
			if (pos + 4 > dataLen) throw asvJSONError("MsgPack str32");
			uint32_t slen = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
			pos += 4;
			if (slen > dataLen - pos) throw asvJSONError("MsgPack str32 overflow");
			if (!isValidUTF8(data + pos, static_cast<size_t>(slen))) throw asvJSONError("MsgPack invalid UTF-8 in string");
			auto val = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), slen);
			pos += slen;
			if (!val) throw asvJSONError("MsgPack string too long");
			return val;
		}
		case 0xDC: {
			if (pos + 2 > dataLen) throw asvJSONError("MsgPack array16");
			uint16_t n = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
			pos += 2;
			auto arr = asvJSONValue::makeArray();
			if (!arr) throw asvJSONError("MsgPack array alloc failed");
			arr->arr->reserve(n);
			for (uint16_t i = 0; i < n; i++) arr->arr->push_back(parseMessagePack(data, pos, dataLen, depth + 1));
			return arr;
		}
		case 0xDD: {
			if (pos + 4 > dataLen) throw asvJSONError("MsgPack array32");
			uint32_t n = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
			pos += 4;
			if (n > asvJSONValue::MAX_ARRAY_SIZE) throw asvJSONError("MsgPack array too large");
			auto arr = asvJSONValue::makeArray();
			if (!arr) throw asvJSONError("MsgPack array alloc failed");
			arr->arr->reserve(n);
			for (uint32_t i = 0; i < n; i++) arr->arr->push_back(parseMessagePack(data, pos, dataLen, depth + 1));
			return arr;
		}
		case 0xDE: {
			if (pos + 2 > dataLen) throw asvJSONError("MsgPack map16");
			uint16_t n = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
			pos += 2;
			auto obj = asvJSONValue::makeObject();
			if (!obj) throw asvJSONError("MsgPack map alloc failed");
			for (uint16_t i = 0; i < n; i++) {
				auto k = parseMessagePack(data, pos, dataLen, depth + 1);
				if (!k || k->type != asvJSONValue::STRING) throw asvJSONError("MsgPack non-string key");
				auto v = parseMessagePack(data, pos, dataLen, depth + 1);
				if (!v) throw asvJSONError("MsgPack map value failed");
				obj->obj->insert_or_assign(k->str_data, std::move(v)); // last wins (duplicate keys), same convention as the JSON parser
			}
			return obj;
		}
		case 0xDF: {
			if (pos + 4 > dataLen) throw asvJSONError("MsgPack map32");
			uint32_t n = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
			pos += 4;
			if (n > asvJSONValue::MAX_OBJECT_SIZE) throw asvJSONError("MsgPack map too large");
			auto obj = asvJSONValue::makeObject();
			if (!obj) throw asvJSONError("MsgPack map alloc failed");
			for (uint32_t i = 0; i < n; i++) {
				auto k = parseMessagePack(data, pos, dataLen, depth + 1);
				if (!k || k->type != asvJSONValue::STRING) throw asvJSONError("MsgPack non-string key");
				auto v = parseMessagePack(data, pos, dataLen, depth + 1);
				if (!v) throw asvJSONError("MsgPack map value failed");
				obj->obj->insert_or_assign(k->str_data, std::move(v)); // last wins (duplicate keys), same convention as the JSON parser
			}
			return obj;
		}
		default: throw asvJSONError("Unknown MsgPack tag");
	}
}

// Decodes an ext type 4 custom-tag body: a msgpack-encoded name string followed
// by the msgpack-encoded annotated value (nil when the annotation has none).
// Recursion is scoped to the ext body (subEnd), so sibling bytes can never be
// consumed; a malformed body falls back to a plain EXTENSION holding the raw
// bytes -- never a partial decode, and never data loss.
inline std::unique_ptr<asvJSONValue> msgpackDecodeCustomTag(const uint8_t* data, size_t& pos, size_t len, size_t depth) {
	size_t subEnd = pos + len;
	size_t base = pos;
	try {
		auto name = parseMessagePack(data, pos, subEnd, depth + 1);
		if (name && name->type == asvJSONValue::STRING) {
			auto val = parseMessagePack(data, pos, subEnd, depth + 1);
			if (val && pos == subEnd) {
				auto custom = std::make_unique<asvJSONValue>();
				custom->type = asvJSONValue::CUSTOM_TAG;
				custom->str_data = std::move(name->str_data);
				custom->custom_value = std::move(val);
				return custom;
			}
		}
	} catch (...) {
		// Malformed layout, bad allocation, oversized body, or depth guard.
	}
	pos = subEnd;
	auto ext = asvJSONValue::makeExtension(4, data + base, len);
	if (!ext) throw asvJSONError("MsgPack extension alloc failed");
	return ext;
}

} // namespace asvJSONInternal
