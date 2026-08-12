#pragma once
// BSON serialization/parsing for asvJSON++

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
						char idx[16];
						int n = snprintf(idx, sizeof(idx), "%zu", i);
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
			int64_t ms = static_cast<int64_t>(timestamp) * 1000 + datetime_ms;
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
				out.insert(out.end(), str_data.begin(), str_data.end());
				for (size_t i = str_data.size(); i < 12; i++) out.push_back(0);
			}
			break;
		}
		default: out.push_back(0x0A); break;
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
	if (docSize < 5 || docStart + docSize > dataLen) throw asvJSONError("BSON invalid document size");
	uint32_t docEnd = docStart + docSize - 1;
	auto obj = asvJSONValue::makeObject();
	if (!obj) throw asvJSONError("BSON alloc failed");
	while (pos < docEnd) {
		if (pos >= dataLen) throw asvJSONError("BSON unexpected end");
		uint8_t elemType = data[pos++];
		size_t keyStart = pos;
		while (pos < docEnd && data[pos] != 0) pos++;
		if (pos >= docEnd) throw asvJSONError("BSON missing key null");
		std::string key(reinterpret_cast<const char*>(data + keyStart), pos - keyStart);
		pos++;
		if (pos > docEnd) throw asvJSONError("BSON element past end");
		if (key.size() > asvJSONValue::MAX_STRING_LEN) throw asvJSONError("BSON key too long");
		switch (elemType) {
			case 0x01: { if (pos + 8 > dataLen) throw asvJSONError("BSON double: unexpected end"); double d; readLE64Double(data, pos, d); obj->obj->emplace(key, asvJSONValue::makeDouble(d)); break; }
			case 0x02: {
				if (pos + 4 > dataLen) throw asvJSONError("BSON string");
				uint32_t slen; readLE32(data, pos, slen);
				if (slen < 1 || pos + slen > dataLen) throw asvJSONError("BSON string invalid");
				auto val = asvJSONValue::makeString(reinterpret_cast<const char*>(data + pos), slen - 1);
				pos += slen;
				if (!val) throw asvJSONError("BSON string too long");
				obj->obj->emplace(key, std::move(val));
				break;
			}
			case 0x03: {
				auto subObj = parseBSON(data, pos, dataLen, depth + 1);
				obj->obj->emplace(key, std::move(subObj));
				break;
			}
			case 0x04: {
				auto subArr = parseBSON(data, pos, dataLen, depth + 1);
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
				}
				obj->obj->emplace(key, std::move(subArr));
				break;
			}
			case 0x05: {
				if (pos + 4 > dataLen) throw asvJSONError("BSON binary");
				uint32_t blen; readLE32(data, pos, blen);
				if (pos >= dataLen) throw asvJSONError("BSON binary subtype");
				uint8_t subtype = data[pos++];
				if (pos + blen > dataLen) throw asvJSONError("BSON binary overflow");
				if (subtype == 0x05 || subtype == 0) {
					auto val = asvJSONValue::makeBinary(data + pos, blen);
					pos += blen;
					if (!val) throw asvJSONError("BSON binary too large");
					obj->obj->emplace(key, std::move(val));
				} else if (subtype == 0x03) {
					if (blen >= 4) {
						if (pos + 4 > dataLen) throw asvJSONError("BSON binary subtype 0x03 truncated");
						uint32_t oldLen; readLE32(data, pos, oldLen);
						if (pos + oldLen > dataLen) throw asvJSONError("BSON binary subtype 0x03 overflow");
						auto val = asvJSONValue::makeBinary(data + pos, oldLen);
						pos += oldLen;
						if (!val) throw asvJSONError("BSON binary too large");
						obj->obj->emplace(key, std::move(val));
					} else {
						auto val = asvJSONValue::makeBinary(data + pos, blen);
						pos += blen;
						if (!val) throw asvJSONError("BSON binary too large");
						obj->obj->emplace(key, std::move(val));
					}
				} else {
					auto val = asvJSONValue::makeExtension(static_cast<int8_t>(subtype), data + pos, blen);
					pos += blen;
					if (!val) throw asvJSONError("BSON extension too large");
					obj->obj->emplace(key, std::move(val));
				}
				break;
			}
			case 0x06: obj->obj->emplace(key, asvJSONValue::makeNull()); break;
			case 0x07: {
				if (pos + 12 > dataLen) throw asvJSONError("BSON objectid");
				auto val = asvJSONValue::makeObjectId(std::string_view(reinterpret_cast<const char*>(data + pos), 12));
				pos += 12;
				obj->obj->emplace(key, std::move(val));
				break;
			}
			case 0x08: {
				if (pos >= dataLen) throw asvJSONError("BSON bool");
				obj->obj->emplace(key, asvJSONValue::makeBool(data[pos++] != 0));
				break;
			}
			case 0x09: {
				if (pos + 8 > dataLen) throw asvJSONError("BSON datetime");
				uint64_t ms = 0;
				for (int i = 0; i < 8; i++) ms |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
				pos += 8;
				time_t ts = static_cast<time_t>(ms / 1000);
				int ms_rem = static_cast<int>(ms % 1000);
				obj->obj->emplace(key, asvJSONValue::makeDateTime(ts, ms_rem));
				break;
			}
			case 0x0A: obj->obj->emplace(key, asvJSONValue::makeNull()); break;
			case 0x0B: {
				size_t patStart = pos;
				while (pos < docEnd && data[pos] != 0) pos++;
				std::string pattern(reinterpret_cast<const char*>(data + patStart), pos - patStart);
				pos++;
				size_t optStart = pos;
				while (pos < docEnd && data[pos] != 0) pos++;
				std::string options(reinterpret_cast<const char*>(data + optStart), pos - optStart);
				pos++;
				auto val = asvJSONValue::makeRegex(pattern.c_str(), options.c_str());
				if (!val) throw asvJSONError("BSON regex invalid or too large");
				obj->obj->emplace(key, std::move(val));
				break;
			}
			case 0x10: {
				if (pos + 4 > dataLen) throw asvJSONError("BSON int32");
				uint32_t v; readLE32(data, pos, v);
				obj->obj->emplace(key, asvJSONValue::makeInt(static_cast<int64_t>(static_cast<int32_t>(v))));
				break;
			}
			case 0x11: {
				if (pos + 8 > dataLen) throw asvJSONError("BSON timestamp");
				uint32_t ts; readLE32(data, pos, ts);
				uint32_t inc; readLE32(data, pos, inc);
				(void)inc;
				obj->obj->emplace(key, asvJSONValue::makeTimestamp(static_cast<int64_t>(ts)));
				break;
			}
			case 0x12: {
				if (pos + 8 > dataLen) throw asvJSONError("BSON int64");
				uint64_t v = 0;
				for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
				pos += 8;
				obj->obj->emplace(key, asvJSONValue::makeInt(static_cast<int64_t>(v)));
				break;
			}
			case 0x13: {
				if (pos + 16 > dataLen) throw asvJSONError("BSON decimal128: unexpected end");
				pos += 16;
				// Decimal128 is a 128-bit IEEE-754 decimal float that cannot be
				// represented losslessly by this library; fail rather than silently
				// replacing the value with null.
				throw asvJSONError("BSON decimal128 is not supported");
			}
			default: throw asvJSONError(std::string("BSON unknown element type: ") + std::to_string(elemType));
		}
	}
	if (pos < dataLen && data[pos] == 0) pos++;
	return obj;
}

} // namespace asvJSONInternal
