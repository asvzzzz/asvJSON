#pragma once
// JSON SAX / Streaming parser for asvJSON++
// Supports:
//   - SAX event-based parsing with callbacks (JsonSaxParser)
//   - Chunk-based incremental/streaming parsing (JsonStreamParser)
//   - SAX-to-DOM conversion adapter (JsonSaxToDom)
//   - Comments (//, /* */, #), NaN/Infinity, same options as DOM parser
//   - Resource limits: MAX_NESTING_DEPTH, MAX_STRING_LEN, MAX_ARRAY_SIZE, MAX_OBJECT_SIZE

#include "../core.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <algorithm>
#include <limits>

namespace asvJSONInternal {

// ============================================================================
// Callback interface for SAX parsing
// ============================================================================
// Each callback returns void. If the handler needs to abort parsing,
// it should throw an exception (which will be caught and reported as an error).
// The string_view parameters point into data valid only during the callback;
// the handler must copy if it needs the data beyond the callback scope.
// onError, in particular, MUST NOT throw: it is invoked both inside the
// parsers' internal try/catch (where a throw is "delivered" a second time by
// the same path) and on early-return validation paths that bypass it (where a
// throw would escape parse()/feed() entirely). Handlers must treat onError as
// a nothrow notification point.

struct JsonSaxHandler {
  virtual ~JsonSaxHandler() = default;
  virtual void onStartObject() {}
  virtual void onEndObject() {}
  virtual void onStartArray() {}
  virtual void onEndArray() {}
  virtual void onKey(std::string_view key) {}
  virtual void onStringValue(std::string_view value) {}
  virtual void onIntegerValue(int64_t value) {}
  virtual void onDoubleValue(double value) {}
  virtual void onBoolValue(bool value) {}
  virtual void onNullValue() {}
  virtual void onError(std::string_view message) {}
};

// ============================================================================
// JsonSaxParser -- stateless single-pass SAX parser
// ============================================================================
// Requires the entire input in memory. Uses ~0 extra memory beyond the input.
// For gigabyte files, use JsonStreamParser instead.

class JsonSaxParser {
public:
  bool allowNaNInfinity = false;
  bool validateUTF8 = false;
  bool allowComments = true;

  bool parse(std::string_view input, JsonSaxHandler& handler) {
    handler_ = &handler;
    input_ = input;
    pos_ = 0;
    depth_ = 0;
    lastError_.clear();
    std::fill(std::begin(cntObject_), std::end(cntObject_), size_t(0));
    std::fill(std::begin(cntArray_), std::end(cntArray_), size_t(0));

    if (input_.empty()) { handler_->onError("empty input"); return false; }

    // Strip UTF-8 BOM
    if (input_.size() >= 3 &&
        static_cast<unsigned char>(input_[0]) == 0xEF &&
        static_cast<unsigned char>(input_[1]) == 0xBB &&
        static_cast<unsigned char>(input_[2]) == 0xBF)
      input_.remove_prefix(3);

    if (validateUTF8 && !isValidUTF8(reinterpret_cast<const uint8_t*>(input_.data()), input_.size())) {
      lastError_ = "Invalid UTF-8 in input";
      handler_->onError(lastError_);
      return false;
    }

    try {
      if (!parseValue()) return false;
      skip();
      if (pos_ != input_.size()) {
        lastError_ = "Trailing chars after value";
        handler_->onError(lastError_);
        return false;
      }
      return true;
    } catch (const std::exception& e) {
      // Single delivery point: error handlers are notified here (and at the
      // early-return paths above), never twice.
      lastError_ = e.what();
      handler_->onError(lastError_);
      return false;
    }
  }

  std::string_view getLastError() const { return lastError_; }

private:

  std::string_view input_;
  size_t pos_ = 0;
  int depth_ = 0;
  JsonSaxHandler* handler_ = nullptr;
  std::string lastError_;
  // Per-container element/key counters (indexed by depth_) for the
  // MAX_ARRAY_SIZE / MAX_OBJECT_SIZE limits in the stateless parser.
  size_t cntObject_[asvJSONValue::MAX_NESTING_DEPTH + 2] = {};
  size_t cntArray_[asvJSONValue::MAX_NESTING_DEPTH + 2] = {};

  char cur() const { return pos_ < input_.size() ? input_[pos_] : 0; }
  void advance() { pos_++; }

  bool isIdentChar(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
  }

  void skip() {
    while (pos_ < input_.size()) {
      char ch = input_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { pos_++; continue; }
      if (allowComments && ch == '/' && pos_ + 1 < input_.size()) {
        if (input_[pos_ + 1] == '/') {
          pos_ += 2;
          while (pos_ < input_.size() && input_[pos_] != '\n' && input_[pos_] != '\r') pos_++;
          continue;
        }
        if (input_[pos_ + 1] == '*') {
          pos_ += 2;
          while (pos_ + 1 < input_.size() && !(input_[pos_] == '*' && input_[pos_ + 1] == '/')) pos_++;
          if (pos_ + 1 >= input_.size()) throw std::runtime_error("Unclosed multiline comment");
          pos_ += 2;
          continue;
        }
      }
      if (allowComments && ch == '#') {
        while (pos_ < input_.size() && input_[pos_] != '\n' && input_[pos_] != '\r') pos_++;
        continue;
      }
      break;
    }
  }

  bool parseValue() {
    skip();
    char c = cur();
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') return parseStringValue();
    if (c == 't' || c == 'f') return parseBool();
    if (c == 'n') return parseNull();
    if (allowNaNInfinity && c == 'N') return parseNaN();
    if (allowNaNInfinity && c == 'I') return parseInfinity(false);
    if (allowNaNInfinity && c == '-' && pos_ + 1 < input_.size() && input_[pos_ + 1] == 'I')
      return parseInfinity(true);
    if (allowNaNInfinity && c == '-' && pos_ + 1 < input_.size() && input_[pos_ + 1] == 'N')
      return parseNaN(true);
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
    throw std::runtime_error(std::string("Unexpected: '") + c + "'");
  }

  bool parseObject() {
    if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) throw std::runtime_error("Maximum nesting depth exceeded");
    depth_++;
    cntObject_[depth_] = 0; // fresh container on this level: size limits are per-container, not per-level
    handler_->onStartObject();
    advance(); // consume '{'
    skip();
    if (cur() == '}') { advance(); depth_--; handler_->onEndObject(); return true; }
    while (true) {
      skip();
      // A trailing comma ({"a":1,}) is accepted, matching the DOM parser; a
      // leading or duplicate comma ({,}, {"a":1,,}) still fails below.
      if (cur() == '}') { advance(); depth_--; handler_->onEndObject(); return true; }
      if (cur() != '"') throw std::runtime_error("Expected string key");
      // Parse key
      {
        std::string raw = scanStringRaw();
        std::string key = unescapeJsonString(raw, true);
        handler_->onKey(key);
      }
      skip();
      if (cur() != ':') throw std::runtime_error("Expected ':'");
      advance();
      if (!parseValue()) return false;
      if (++cntObject_[depth_] > asvJSONValue::MAX_OBJECT_SIZE)
        throw std::runtime_error("Object too large");
      skip();
      if (cur() == '}') { advance(); depth_--; handler_->onEndObject(); return true; }
      if (cur() == ',') { advance(); continue; }
      throw std::runtime_error("Expected ',' or '}'");
    }
  }

  bool parseArray() {
    if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) throw std::runtime_error("Maximum nesting depth exceeded");
    depth_++;
    cntArray_[depth_] = 0; // fresh container on this level: size limits are per-container, not per-level
    handler_->onStartArray();
    advance(); // consume '['
    skip();
    if (cur() == ']') { advance(); depth_--; handler_->onEndArray(); return true; }
    while (true) {
      skip();
      // A trailing comma ([1,]) is accepted, matching the DOM parser; a leading
      // or duplicate comma ([,], [1,,2]) still fails inside parseValue().
      if (cur() == ']') { advance(); depth_--; handler_->onEndArray(); return true; }
      if (!parseValue()) return false;
      if (++cntArray_[depth_] > asvJSONValue::MAX_ARRAY_SIZE)
        throw std::runtime_error("Array too large");
      skip();
      if (cur() == ']') { advance(); depth_--; handler_->onEndArray(); return true; }
      if (cur() == ',') { advance(); continue; }
      throw std::runtime_error("Expected ',' or ']'");
    }
  }

  bool parseStringValue() {
    std::string raw = scanStringRaw();
    // Strings are always reported unescaped, even when the raw text happens to
    // look like a datetime (SAX has no datetime event; the SAX contract is that
    // callbacks receive decoded bytes, so there is no raw early-exit path).
    std::string unescaped;
    try { unescaped = unescapeJsonString(raw, true); }
    catch (const std::runtime_error&) { throw std::runtime_error("Invalid escape in string"); }
    handler_->onStringValue(unescaped);
    return true;
  }

  std::string scanStringRaw() {
    if (cur() != '"') throw std::runtime_error("Expected string");
    size_t start = ++pos_;
    while (pos_ < input_.size()) {
      char c = input_[pos_];
      if (c == '"') break;
      if (c == '\\') { if (pos_ + 1 >= input_.size()) throw std::runtime_error("Unclosed escape"); pos_ += 2; }
      else if (static_cast<unsigned char>(c) < 0x20) { throw std::runtime_error("Control character in string"); }
      else pos_++;
    }
    if (pos_ >= input_.size() || input_[pos_] != '"') throw std::runtime_error("Unclosed string");
    size_t len = pos_ - start;
    if (!asvJSONValue::checkStringLen(len)) throw std::runtime_error("String too long");
    advance(); // consume closing quote
    return std::string(input_.substr(start, len));
  }

  bool parseNumber() {
    size_t start = pos_;
    if (cur() == '-') advance();
    size_t intStart = pos_;
    while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') advance();
    size_t intLen = pos_ - intStart;
    if (intLen == 0) throw std::runtime_error("Invalid number");
    if (intLen > 1 && input_[intStart] == '0') throw std::runtime_error("Leading zero");

    bool isDouble = false;
    if (cur() == '.') {
      isDouble = true; advance();
      if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9')
        throw std::runtime_error("Digit required after decimal point");
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') advance();
    }
    if (cur() == 'e' || cur() == 'E') {
      isDouble = true; advance();
      if (cur() == '+' || cur() == '-') advance();
      if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9')
        throw std::runtime_error("Digit required after exponent");
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') advance();
    }

    size_t numLen = pos_ - start;
    const char* buf = input_.data() + start;

    if (isDouble) {
      double d = 0;
      bool bad = true;
    #if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
      auto [ptr, ec] = std::from_chars(buf, buf + numLen, d);
      if (ec == std::errc() && ptr == buf + numLen) {
        bad = false;
      } else if (ec == std::errc() && ptr != buf + numLen) {
        throw std::runtime_error("Invalid number");
      } else {
        // Overflow (e.g. 1e999) and underflow (e.g. 1e-999) both report
        // result_out_of_range, and whether from_chars writes +/-inf into `d`
        // there is not guaranteed by the standard -- so don't read `d` to tell
        // the two apart. strtod is required to set the value: +/-inf on
        // overflow, a tiny finite (possibly zero) on underflow. It is also what
        // the fallback branch below uses, so overflow is honored exactly as it
        // is there and underflow stays rejected.
        std::string numStr(buf, numLen);
        errno = 0;
        char* end = nullptr;
        d = std::strtod(numStr.c_str(), &end);
        if (end != numStr.c_str() + numStr.size()) throw std::runtime_error("Invalid number");
        if (allowNaNInfinity && std::isinf(d)) bad = false;
        else throw std::runtime_error("Invalid number: out of range");
      }
    #else
      // Copy to local string for null-terminated strtod (input_ may be string_view)
      std::string numStr(buf, numLen);
      char* endptr;
      errno = 0;
      d = std::strtod(numStr.c_str(), &endptr);
      if (errno == ERANGE) {
        if (allowNaNInfinity && std::isinf(d)) { bad = false; }
        else throw std::runtime_error("Invalid number: out of range");
      } else if (endptr != numStr.c_str() + numLen) {
        throw std::runtime_error("Invalid number");
      } else {
        bad = false;
      }
    #endif
      if (!bad && !allowNaNInfinity && (std::isnan(d) || std::isinf(d)))
        throw std::runtime_error("NaN or Infinity not allowed");
      if (bad) throw std::runtime_error("Invalid number: out of range");
      handler_->onDoubleValue(d);
    } else {
      long long l = 0;
      auto [ptr, ec] = std::from_chars(buf, buf + numLen, l);
      if (ec != std::errc() || ptr != buf + numLen) {
        // int64 overflow. Documented divergence: the DOM parser preserves such a
        // literal as a STRING with raw_number (exact text round-trip), while the
        // SAX JSON parser has no raw-number event and degrades to a double --
        // both are valid numbers, but the DOM keeps the literal verbatim. This
        // is intentional: SAX consumers get a numeric value, not a raw string.
        std::string numStr(buf, numLen);
        errno = 0;
        char* end = nullptr;
        double d = std::strtod(numStr.c_str(), &end);
        if (end != numStr.c_str() + numLen) throw std::runtime_error("Invalid number");
        if ((std::isnan(d) || std::isinf(d)) && !allowNaNInfinity)
          throw std::runtime_error("NaN or Infinity not allowed");
        handler_->onDoubleValue(d);
      } else {
        handler_->onIntegerValue(l);
      }
    }
    return true;
  }

  bool parseBool() {
    if (cur() == 't') {
      const char exp[] = "true";
      for (int i = 0; i < 4; i++) {
        if (cur() != exp[i]) throw std::runtime_error("Invalid boolean");
        advance();
      }
      if (pos_ < input_.size() && isIdentChar(cur())) throw std::runtime_error("Invalid boolean");
      handler_->onBoolValue(true);
      return true;
    }
    if (cur() == 'f') {
      const char exp[] = "false";
      for (int i = 0; i < 5; i++) {
        if (cur() != exp[i]) throw std::runtime_error("Invalid boolean");
        advance();
      }
      if (pos_ < input_.size() && isIdentChar(cur())) throw std::runtime_error("Invalid boolean");
      handler_->onBoolValue(false);
      return true;
    }
    throw std::runtime_error("Expected boolean");
  }

  bool parseNull() {
    const char exp[] = "null";
    for (int i = 0; i < 4; i++) {
      if (cur() != exp[i]) throw std::runtime_error("Invalid null");
      advance();
    }
    if (pos_ < input_.size() && isIdentChar(cur())) throw std::runtime_error("Invalid null");
    handler_->onNullValue();
    return true;
  }

  bool parseNaN(bool negative = false) {
    if (negative) advance(); // skip '-'
    if (input_.compare(pos_, 3, "NaN") != 0) throw std::runtime_error("Expected NaN");
    pos_ += 3;
    if (pos_ < input_.size() && isIdentChar(cur())) throw std::runtime_error("Invalid NaN");
    double v = std::numeric_limits<double>::quiet_NaN();
    if (negative) v = -v;
    handler_->onDoubleValue(v);
    return true;
  }

  bool parseInfinity(bool negative) {
    if (negative) advance(); // skip '-'
    if (input_.compare(pos_, 8, "Infinity") != 0) throw std::runtime_error("Expected Infinity");
    pos_ += 8;
    if (pos_ < input_.size() && isIdentChar(cur())) throw std::runtime_error("Invalid Infinity");
    double val = std::numeric_limits<double>::infinity();
    if (negative) val = -val;
    handler_->onDoubleValue(val);
    return true;
  }
};

// ============================================================================
// JsonStreamParser -- chunk-based streaming SAX parser
// ============================================================================
// Processes input incrementally. Feed chunks via feed(), call finish() when done.
// Individual values (strings, numbers) are buffered internally and emitted
// complete. The document as a whole can be arbitrarily large.

class JsonStreamParser {
public:
  bool allowNaNInfinity = false;
  bool allowComments = true;
  bool validateUTF8 = false;

  explicit JsonStreamParser(JsonSaxHandler& handler)
    : handler_(handler) {}

  // Feed a chunk of input data. Returns false on parse error.
  bool feed(std::string_view chunk) {
    if (error_) return false;
    // An empty chunk is always a no-op (e.g. a read() returning 0); it must not
    // be mistaken for "data after complete value".
    if (chunk.empty()) return true;
    if (done_) { setError("Data after complete value"); return false; }
    buffer_.append(chunk.data(), chunk.size());
    needMore_ = false; // new data may resolve a paused token

    // Strip a leading UTF-8 BOM (possibly split across chunk boundaries).
    if (bomPending_) {
      static const unsigned char BOM[3] = { 0xEF, 0xBB, 0xBF };
      size_t n = 0;
      while (n < 3 && n < buffer_.size() && static_cast<unsigned char>(buffer_[n]) == BOM[n]) n++;
      if (n == 3) {
        buffer_.erase(0, 3);
        utf8ScanPos_ = (utf8ScanPos_ > 3) ? utf8ScanPos_ - 3 : 0;
        bomPending_ = false;
      } else if (n == buffer_.size() && n < 3) {
        return true; // partial BOM prefix; wait for more bytes
      } else {
        bomPending_ = false; // not a BOM after all
      }
    }

    // Incremental structural UTF-8 validation over the newly appended region.
    // Runs over the raw bytes, so it covers strings, keys, literals, comments and
    // inter-token whitespace alike. Lead bytes and continuation bytes are checked
    // here, and the range constraints that rule out overlong forms, surrogates and
    // code points above U+10FFFF are checked on the first continuation byte --
    // the same rules isValidUTF8 applies, so the stream parser accepts exactly
    // what the DOM parser accepts when validateUTF8 is set. The DOM parser runs
    // isValidUTF8 over the whole input up front; here the check is spread across
    // feeds, so a sequence is only judged once its bytes have all arrived.
    if (validateUTF8) {
      for (size_t i = utf8ScanPos_; i < buffer_.size(); i++) {
        uint8_t b = static_cast<uint8_t>(buffer_[i]);
        if (utf8Need_ == 0) {
          if (b < 0x80) continue;
          if (b <= 0xC1 || b >= 0xF5) { setError("Invalid UTF-8 in input"); return false; }
          if ((b & 0xE0) == 0xC0) { utf8Need_ = 1; utf8Lead_ = b; }
          else if ((b & 0xF0) == 0xE0) { utf8Need_ = 2; utf8Lead_ = b; }
          else if ((b & 0xF8) == 0xF0) { utf8Need_ = 3; utf8Lead_ = b; }
          else { setError("Invalid UTF-8 in input"); return false; }
        } else {
          if ((b & 0xC0) != 0x80) { setError("Invalid UTF-8 in input"); return false; }
          if (utf8Lead_ != 0) {
            const bool bad = (utf8Lead_ == 0xE0 && b < 0xA0) ||  // overlong 3-byte
                             (utf8Lead_ == 0xED && b >= 0xA0) || // U+D800..DFFF surrogate
                             (utf8Lead_ == 0xF0 && b < 0x90) ||  // overlong 4-byte
                             (utf8Lead_ == 0xF4 && b > 0x8F);   // > U+10FFFF
            utf8Lead_ = 0;
            if (bad) { setError("Invalid UTF-8 in input"); return false; }
          }
          utf8Need_--;
        }
      }
      utf8ScanPos_ = buffer_.size();
    }

    return process();
  }

  // Signal end of input. Returns false if input is incomplete.
  bool finish() {
    if (error_) return false;
    if (done_) return true;

    // Handle pending tokens at end of input
    switch (state_) {
      case State::DONE:
        break;
      case State::STR_VALUE:
      case State::STR_KEY:
        lastError_ = "Unterminated string"; error_ = true; handler_.onError(lastError_); return false;
      case State::NUM:
        // emitPendingNumber already calls afterValue() internally on success,
        // so a second call here would double-advance the container counter.
        if (!emitPendingNumber()) return false;
        break;
      case State::LIT_TRUE:
      case State::LIT_FALSE:
      case State::LIT_NULL:
        // A LIT_* state at EOF always means the literal was truncated; there is
        // no further input, so the token is invalid.
        lastError_ = "Invalid literal"; error_ = true; handler_.onError(lastError_); return false;
      case State::LINE_COMMENT:
        // Resume the state the comment interrupted (e.g. waiting for a value)
        // so incomplete input such as `{"a": //` is still detected.
        state_ = commentRetState_;
        break;
      case State::BLOCK_COMMENT:
        lastError_ = "Unterminated block comment"; error_ = true; handler_.onError(lastError_); return false;
      default:
        break;
    }

    // A value completed during the pend-stage above (e.g. a number flushed at
    // EOF) transitions straight into the done state.
    if (state_ == State::DONE) {
      done_ = true;
      return true;
    }

    // Check UTF-8 BEFORE accepting a completed top-level value: a multibyte
    // sequence may have been cut off by EOF even while the value (or a trailing
    // comment holding those bytes) looked complete. The DOM parser rejects such
    // input on entry; here it is rejected at the end of input.
    if (validateUTF8 && utf8Need_ != 0) {
      // A multibyte sequence was cut off at end of input.
      lastError_ = "Invalid UTF-8 in input"; error_ = true; handler_.onError(lastError_); return false;
    }

    if (state_ == State::TAIL) {
      // A '/' waiting for its second character at EOF never opened a comment:
      // it is trailing data after the completed value ("Trailing chars" in the
      // DOM/JSON parsers), not harmless whitespace.
      if (pendingComment()) {
        lastError_ = "Data after complete value"; error_ = true; handler_.onError(lastError_); return false;
      }
      done_ = true;
      return true;
    }

    if (depth_ > 0 || state_ != State::TOP) {
      // Still inside a container / mid-token at end of input: incomplete. The
      // previous nested check was dead code -- it demanded state_ == TOP &&
      // depth_ == 0, the exact opposite of this branch's precondition; reaching
      // the end means the input never produced a complete document.
      lastError_ = "Incomplete input"; error_ = true; handler_.onError(lastError_); return false;
    }

    // state_ == State::TOP && depth_ == 0 now. A real value must have been
    // produced; empty input and comment-only input are rejected, matching the
    // DOM parser and JsonSaxParser.
    if (!hadValue_) {
      lastError_ = "Empty input"; error_ = true; handler_.onError(lastError_); return false;
    }

    state_ = State::DONE;
    done_ = true;
    return true;
  }

  std::string_view getLastError() const { return lastError_; }

private:
  enum class State {
    TOP,           // expecting a value at top level
    OBJ_KEY,       // inside object, expecting key or }
    OBJ_COLON,     // after key, expecting :
    OBJ_VALUE,     // after :, expecting value
    ARR_VALUE,     // inside array, expecting value or ]
    STR_VALUE,     // inside string value
    STR_KEY,       // inside string key
    NUM,           // inside number
    LIT_TRUE,      // inside "true"
    LIT_FALSE,     // inside "false"
    LIT_NULL,      // inside "null"
    LINE_COMMENT,  // inside // comment
    BLOCK_COMMENT, // inside /* */ comment
    TAIL,          // complete top-level value; only whitespace/comments may follow
    DONE,
    ERROR
  };

  JsonSaxHandler& handler_;
  std::string buffer_;
  size_t pos_ = 0;
  State state_ = State::TOP;
  int depth_ = 0;
  bool hadValue_ = false;
  // Bound on the accumulated number token (tokenBuffer_ + in-buffer tail) so a
  // pathological number cannot grow tokenBuffer_ without limit (DoS guard).
  static constexpr size_t MAX_NUMBER_LEN = 4096;
  size_t tokenStart_ = 0;   // start of current token in buffer_
  std::string tokenBuffer_; // accumulated partial token data across chunks
  std::string lastError_;
  bool done_ = false;
  bool error_ = false;
  bool strEscape_ = false;  // last string char captured was a backslash awaiting its escaped target
  size_t litIdx_ = 0;       // number of literal chars validated so far (LIT_* states)
  bool needMore_ = false;   // true when processing paused without consuming (awaiting more input)
  bool bomPending_ = true;  // leading bytes might be a UTF-8 BOM
  int utf8Need_ = 0;        // remaining continuation bytes of an in-flight UTF-8 sequence
  uint8_t utf8Lead_ = 0;    // its lead byte, non-zero until the first continuation is seen
  size_t utf8ScanPos_ = 0;  // end of the buffer region already UTF-8 validated

  // Container stack for tracking context; each frame also tracks whether a
  // value/key has been produced since the last separator and how many members
  // the container holds (for the MAX_ARRAY_SIZE / MAX_OBJECT_SIZE limits).
  enum class Container { OBJ, ARR };
  struct Frame {
    Container kind;
    size_t count = 0;
    bool hasVal = false;
  };
  std::vector<Frame> containerStack_;

  // State to resume once a comment ends. Set before entering a comment so that
  // e.g. `{"a": /*c*/ 1}` resumes value parsing instead of assuming a key.
  State commentRetState_ = State::TOP;

  void setError(const std::string& msg) {
    lastError_ = msg; error_ = true;
    // Contract says onError MUST NOT throw, but wrap defensively: if a user-
    // supplied handler does throw, catch and swallow so feed()/finish() don't
    // propagate an exception through the error path.
    try { handler_.onError(lastError_); } catch (...) {}
  }

  // True when the next unconsumed char is a '/' sitting at the very end of the
  // buffer (the rest of a `//` or `/*` comment may arrive in a later chunk).
  bool pendingComment() const {
    return allowComments && pos_ < buffer_.size() && buffer_[pos_] == '/' && pos_ + 1 >= buffer_.size();
  }

  static bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
  }

  bool isValueStart(char c) {
    return c == '"' || c == '{' || c == '[' || c == 't' || c == 'f' || c == 'n' ||
           c == '-' || (c >= '0' && c <= '9') ||
           (c == 'N' || c == 'I') && allowNaNInfinity;
  }

  // Strict JSON number grammar: [-] int [. frac] [(e|E)[+|-] digits]. Rejects
  // leading zeros, a missing fractional/exponent digit and other non-JSON
  // accumulations the permissive scan could otherwise swallow.
  static bool validJsonNumber(const char* b, size_t n) {
    size_t i = 0;
    if (i < n && b[i] == '-') i++;
    if (i >= n) return false;
    if (b[i] == '0') i++;
    else if (b[i] >= '1' && b[i] <= '9') { while (i < n && b[i] >= '0' && b[i] <= '9') i++; }
    else return false;
    if (i < n && b[i] == '.') {
      i++;
      if (i >= n || b[i] < '0' || b[i] > '9') return false;
      while (i < n && b[i] >= '0' && b[i] <= '9') i++;
    }
    if (i < n && (b[i] == 'e' || b[i] == 'E')) {
      i++;
      if (i < n && (b[i] == '+' || b[i] == '-')) i++;
      if (i >= n || b[i] < '0' || b[i] > '9') return false;
      while (i < n && b[i] >= '0' && b[i] <= '9') i++;
    }
    return i == n;
  }

  // Compact buffer when it grows large and we're at a safe point
  void compact() {
    if (pos_ > 0 && pos_ <= buffer_.size() &&
        state_ != State::STR_VALUE && state_ != State::STR_KEY && state_ != State::NUM &&
        state_ != State::LIT_TRUE && state_ != State::LIT_FALSE && state_ != State::LIT_NULL) {
      size_t consumed = pos_;
      buffer_.erase(0, consumed);
      pos_ = 0;
      tokenStart_ = 0; // keep in sync — future states relying on it need a clean slate
      // Bytes [0, consumed) were already UTF-8 validated and are now gone; keep
      // the scan pointer aligned with the remaining (already validated) region.
      utf8ScanPos_ = (utf8ScanPos_ >= consumed) ? utf8ScanPos_ - consumed : 0;
    }
  }

  bool process() {
    while (pos_ < buffer_.size() && !done_ && !error_ && !needMore_) {
      switch (state_) {
        case State::TOP:
        case State::OBJ_VALUE:
        case State::ARR_VALUE:
          if (!processValueState()) return false;
          break;
        case State::OBJ_KEY:
          if (!processObjKey()) return false;
          break;
        case State::OBJ_COLON:
          if (!processObjColon()) return false;
          break;
        case State::STR_VALUE:
        case State::STR_KEY:
          if (!processString()) return false;
          break;
        case State::NUM:
          if (!processNumber()) return false;
          break;
        case State::LIT_TRUE:
        case State::LIT_FALSE:
        case State::LIT_NULL:
          if (!processLiteral()) return false;
          break;
        case State::LINE_COMMENT:
          if (!processLineComment()) return false;
          break;
        case State::BLOCK_COMMENT:
          if (!processBlockComment()) return false;
          break;
        case State::TAIL:
          if (!processTrailing()) return false;
          break;
        default:
          return true;
      }
    }
    if (!error_ && !done_) compact();
    return !error_;
  }

  void skipWsAndComments() {
    while (pos_ < buffer_.size()) {
      char ch = buffer_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { pos_++; continue; }
      if (allowComments && ch == '/' && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == '/') {
        commentRetState_ = state_;
        pos_ += 2;
        state_ = State::LINE_COMMENT;
        return;
      }
      if (allowComments && ch == '/' && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == '*') {
        commentRetState_ = state_;
        pos_ += 2;
        state_ = State::BLOCK_COMMENT;
        return;
      }
      if (allowComments && ch == '#') {
        commentRetState_ = state_;
        pos_++;
        state_ = State::LINE_COMMENT;
        return;
      }
      break;
    }
  }

  bool processValueState() {
    skipWsAndComments();
    if (state_ == State::LINE_COMMENT || state_ == State::BLOCK_COMMENT) return true;
    if (pos_ >= buffer_.size()) return true; // need more data
    if (pendingComment()) { needMore_ = true; return true; } // '/' awaiting a second '/' or '*'; need more data
    if (validateUTF8 && utf8Need_ != 0) { needMore_ = true; return true; } // truncated multibyte at the tail

    char c = buffer_[pos_];

    // Enforce array separators: an element must be preceded by ',' or ']', and
    // a ',' must be preceded by an element. A trailing comma before ']' is
    // accepted (DOM parity); a leading or duplicate comma is rejected.
    if (state_ == State::ARR_VALUE && !containerStack_.empty() &&
        containerStack_.back().kind == Container::ARR) {
      if (c == ',') {
        if (!containerStack_.back().hasVal) { setError("Unexpected ','"); return false; }
        advance();
        containerStack_.back().hasVal = false;
        return true;
      }
      if (c != ']' && isValueStart(c) && containerStack_.back().hasVal) {
        setError("Expected ',' or ']'");
        return false;
      }
    }

    // A value is mandatory after ':'; a closing bracket there means the value
    // is missing (the DOM parser rejects this too).
    if (state_ == State::OBJ_VALUE) {
      if (c == '}' || c == ']') { setError("Value expected after ':'"); return false; }
    }

    if (c == '{') {
      if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) { setError("Maximum nesting depth exceeded"); return false; }
      depth_++;
      containerStack_.push_back({Container::OBJ, 0, false});
      handler_.onStartObject();
      advance();
      state_ = State::OBJ_KEY;
      return true;
    }
    if (c == '}') {
      if (containerStack_.empty() || containerStack_.back().kind != Container::OBJ) { setError("Unexpected '}'"); return false; }
      containerStack_.pop_back();
      depth_--;
      handler_.onEndObject();
      advance();
      return afterValue();
    }
    if (c == '[') {
      if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) { setError("Maximum nesting depth exceeded"); return false; }
      depth_++;
      containerStack_.push_back({Container::ARR, 0, false});
      handler_.onStartArray();
      advance();
      state_ = State::ARR_VALUE;
      return true;
    }
    if (c == ']') {
      if (containerStack_.empty() || containerStack_.back().kind != Container::ARR) { setError("Unexpected ']'"); return false; }
      containerStack_.pop_back();
      depth_--;
      handler_.onEndArray();
      advance();
      return afterValue();
    }
    if (c == '"') {
      tokenBuffer_.clear();
      tokenStart_ = pos_ + 1; // past opening quote
      advance();
      state_ = State::STR_VALUE;
      return processString();
    }
    if (c == 't') { tokenBuffer_.clear(); tokenStart_ = pos_; litIdx_ = 0; state_ = State::LIT_TRUE; advance(); return processLiteral(); }
    if (c == 'f') { tokenBuffer_.clear(); tokenStart_ = pos_; litIdx_ = 0; state_ = State::LIT_FALSE; advance(); return processLiteral(); }
    if (c == 'n') { tokenBuffer_.clear(); tokenStart_ = pos_; litIdx_ = 0; state_ = State::LIT_NULL; advance(); return processLiteral(); }

    // NaN/Infinity may be split across chunks; only match when the whole token
    // is present, otherwise wait for more input. Ident-char continuation
    // (NaN5, Infinityx) is rejected.
    if (allowNaNInfinity && c == 'N') {
      if (buffer_.compare(pos_, 3, "NaN") == 0) {
        pos_ += 3;
        if (pos_ < buffer_.size() && isIdentChar(buffer_[pos_])) { setError("Invalid NaN literal"); return false; }
        handler_.onDoubleValue(std::numeric_limits<double>::quiet_NaN());
        return afterValue();
      }
      if (buffer_.size() - pos_ < 3) { needMore_ = true; return true; }
      setError("Invalid NaN literal");
      return false;
    }
    if (allowNaNInfinity && c == 'I') {
      if (buffer_.compare(pos_, 8, "Infinity") == 0) {
        pos_ += 8;
        if (pos_ < buffer_.size() && isIdentChar(buffer_[pos_])) { setError("Invalid Infinity literal"); return false; }
        handler_.onDoubleValue(std::numeric_limits<double>::infinity());
        return afterValue();
      }
      if (buffer_.size() - pos_ < 8) { needMore_ = true; return true; }
      setError("Invalid Infinity literal");
      return false;
    }
    if (c == '-') {
      if (allowNaNInfinity && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == 'I') {
        if (buffer_.compare(pos_ + 1, 8, "Infinity") == 0) {
          pos_ += 9;
          if (pos_ < buffer_.size() && isIdentChar(buffer_[pos_])) { setError("Invalid -Infinity literal"); return false; }
          handler_.onDoubleValue(-std::numeric_limits<double>::infinity());
          return afterValue();
        }
        if (buffer_.size() - pos_ < 9) { needMore_ = true; return true; }
        setError("Invalid -Infinity literal");
        return false;
      }
      if (allowNaNInfinity && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == 'N') {
        if (buffer_.compare(pos_ + 1, 3, "NaN") == 0) {
          pos_ += 4;
          if (pos_ < buffer_.size() && isIdentChar(buffer_[pos_])) { setError("Invalid -NaN literal"); return false; }
          handler_.onDoubleValue(std::numeric_limits<double>::quiet_NaN());
          return afterValue();
        }
        if (buffer_.size() - pos_ < 4) { needMore_ = true; return true; }
        setError("Invalid -NaN literal");
        return false;
      }
      if (allowNaNInfinity && pos_ + 1 >= buffer_.size()) {
        // A lone '-' at the end of the buffer may be the start of '-Infinity'.
        needMore_ = true;
        return true;
      }
      tokenBuffer_.clear();
      tokenStart_ = pos_;
      state_ = State::NUM;
      return processNumber();
    }
    if (c >= '0' && c <= '9') {
      tokenBuffer_.clear();
      tokenStart_ = pos_;
      state_ = State::NUM;
      return processNumber();
    }
    setError(std::string("Unexpected: '") + c + "'");
    return false;
  }

  bool processObjKey() {
    skipWsAndComments();
    if (state_ == State::LINE_COMMENT || state_ == State::BLOCK_COMMENT) return true;
    if (pos_ >= buffer_.size()) return true;
    if (pendingComment()) { needMore_ = true; return true; } // '/' awaiting a second '/' or '*'; need more data
    if (validateUTF8 && utf8Need_ != 0) { needMore_ = true; return true; } // truncated multibyte at the tail
    char c = buffer_[pos_];
    Frame& f = containerStack_.back(); // an OBJ frame is always on top here
    if (c == '}') {
      containerStack_.pop_back();
      depth_--;
      handler_.onEndObject();
      advance();
      return afterValue();
    }
    if (c == ',') {
      // Trailing comma is accepted (a key may follow it or '}' closes the
      // object); a leading or duplicate comma is rejected.
      if (!f.hasVal) { setError("Unexpected ','"); return false; }
      advance();
      f.hasVal = false;
      return true;
    }
    if (c == '"') {
      if (f.hasVal) { setError("Expected ',' or '}'"); return false; }
      tokenBuffer_.clear();
      tokenStart_ = pos_ + 1;
      advance();
      state_ = State::STR_KEY;
      return processString();
    }
    setError(std::string("Expected '\"' or '}', got '") + c + "'");
    return false;
  }

  bool processObjColon() {
    skipWsAndComments();
    if (state_ == State::LINE_COMMENT || state_ == State::BLOCK_COMMENT) return true;
    if (pos_ >= buffer_.size()) return true;
    if (pendingComment()) { needMore_ = true; return true; } // '/' awaiting a second '/' or '*'; need more data
    if (validateUTF8 && utf8Need_ != 0) { needMore_ = true; return true; } // truncated multibyte at the tail
    if (buffer_[pos_] == ':') {
      advance();
      state_ = State::OBJ_VALUE;
      return processValueState();
    }
    setError(std::string("Expected ':', got '") + buffer_[pos_] + "'");
    return false;
  }

  bool processString() {
    // Scan from pos_ for closing quote
    while (pos_ < buffer_.size()) {
      if (strEscape_) {
        // The previous chunk ended on a backslash; the current char is its
        // escaped target and must not be treated as a closing quote/control.
        pos_++;
        strEscape_ = false;
        continue;
      }
      char c = buffer_[pos_];
      if (c == '"') {
        // Complete string found
        std::string raw = tokenBuffer_;
        raw.append(buffer_, tokenStart_, pos_ - tokenStart_);
        advance(); // past closing quote

        if (validateUTF8 && !isValidUTF8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size())) {
          setError("Invalid UTF-8 in input");
          return false;
        }
        if (state_ == State::STR_KEY) {
          if (raw.size() > asvJSONValue::MAX_STRING_LEN) {
            setError("String too long");
            return false;
          }
          std::string key;
          try { key = unescapeJsonString(raw, true); }
          catch (...) { setError("Invalid escape in key"); return false; }
          Frame& f = containerStack_.back();
          if (++f.count > asvJSONValue::MAX_OBJECT_SIZE) {
            setError("Object too large");
            return false;
          }
          f.hasVal = true;
          handler_.onKey(key);
          state_ = State::OBJ_COLON;
        } else {
          // Strings are always reported unescaped, even if the raw text looks
          // like a datetime (no raw early-exit; SAX delivers decoded bytes).
          if (raw.size() > asvJSONValue::MAX_STRING_LEN) {
            setError("String too long");
            return false;
          }
          try {
            std::string unescaped = unescapeJsonString(raw, true);
            handler_.onStringValue(unescaped);
          } catch (...) {
            setError("Invalid escape in string");
            return false;
          }
          return afterValue();
        }
        tokenBuffer_.clear();
        return true;
      }
      if (c == '\\') {
        if (pos_ + 1 >= buffer_.size()) {
          // Backslash at end of buffer -- capture up to and including it; the
          // escaped target arrives in a later chunk and is consumed via strEscape_.
          tokenBuffer_.append(buffer_, tokenStart_, pos_ - tokenStart_ + 1);
          pos_++;
          tokenStart_ = pos_;
          strEscape_ = true;
          return true; // need more data
        }
        pos_ += 2; // skip escaped char
      } else if (static_cast<unsigned char>(c) < 0x20) {
        setError("Control character in string");
        return false;
      } else {
        pos_++;
      }
    }
    // End of buffer without finding closing quote -- save partial data
    tokenBuffer_.append(buffer_, tokenStart_, buffer_.size() - tokenStart_);
    tokenStart_ = buffer_.size();
    pos_ = buffer_.size();
    if (tokenBuffer_.size() > asvJSONValue::MAX_STRING_LEN) {
      setError("String too long");
      return false;
    }
    return true; // need more data
  }

  bool processNumber() {
    // Scan from pos_ for end of number
    while (pos_ < buffer_.size()) {
      char c = buffer_[pos_];
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
        pos_++;
      } else {
        // End of number
        return emitPendingNumber();
      }
    }
    // End of buffer -- might need more data (e.g., exponent)
    // Save partial and wait -- but bounds-check the accumulated size so a
    // number split across many chunks cannot grow tokenBuffer_ without limit.
    {
      size_t total = tokenBuffer_.size() + (buffer_.size() - tokenStart_);
      if (total > MAX_NUMBER_LEN) {
        setError("Number too large"); return false;
      }
      tokenBuffer_.append(buffer_, tokenStart_, buffer_.size() - tokenStart_);
      tokenStart_ = buffer_.size();
      pos_ = buffer_.size();
    }
    return true; // need more data
  }

  bool emitPendingNumber() {
    // Bound the accumulated number token so a pathological number split across
    // many chunks cannot grow memory without limit.
    size_t tailLen = (tokenStart_ <= pos_ && tokenStart_ <= buffer_.size()) ? (pos_ - tokenStart_) : 0;
    if (tokenBuffer_.size() + tailLen > MAX_NUMBER_LEN) {
      setError("Number too large"); return false;
    }
    std::string numStr;
    if (!tokenBuffer_.empty()) {
      numStr = tokenBuffer_;
      numStr.append(buffer_, tokenStart_, pos_ - tokenStart_);
    } else {
      numStr.assign(buffer_, tokenStart_, pos_ - tokenStart_);
    }

    const char* buf = numStr.data();
    size_t len = numStr.size();

    // The permissive scan may have accumulated non-JSON garbage (leading zeros,
    // missing exponent digits, embedded signs); enforce the strict grammar.
    if (!validJsonNumber(buf, len)) {
      setError("Invalid number");
      return false;
    }

    bool isDouble = false;
    for (char c : numStr) {
      if (c == '.' || c == 'e' || c == 'E') { isDouble = true; break; }
    }

    if (isDouble) {
      double d = 0;
    #if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && (!defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 11)
      auto [ptr, ec] = std::from_chars(buf, buf + len, d);
      if (ec == std::errc() && ptr == buf + len) {
        // ok
      } else if (ec == std::errc() && ptr != buf + len) {
        // A grammatically valid number the formatter did not fully consume:
        // fall back to strtod, verifying it consumed everything.
        errno = 0;
        char* end = nullptr;
        d = std::strtod(numStr.c_str(), &end);
        if (errno == ERANGE) { setError("Invalid number: out of range"); return false; }
        if (end != numStr.c_str() + numStr.size()) { setError("Invalid number"); return false; }
      } else if (ec == std::errc::result_out_of_range) {
        // Overflow (e.g. 1e999) and underflow (e.g. 1e-999) both report
        // result_out_of_range, and whether from_chars writes +/-inf into `d`
        // there is not guaranteed by the standard -- so don't read `d` to tell
        // the two apart. strtod is required to set the value: +/-inf on
        // overflow, a tiny finite (possibly zero) on underflow. It is also
        // what the fallback branch and the DOM parser use, so overflow is
        // honored exactly as they honor it and underflow stays rejected.
        errno = 0;
        char* end = nullptr;
        d = std::strtod(numStr.c_str(), &end);
        if (end != numStr.c_str() + numStr.size()) { setError("Invalid number"); return false; }
        if (!(allowNaNInfinity && std::isinf(d))) { setError("Invalid number: out of range"); return false; }
      } else {
        setError("Invalid number: out of range");
        return false;
      }
    #else
      errno = 0;
      d = std::strtod(numStr.c_str(), nullptr);
      if (errno == ERANGE) {
        if (!(allowNaNInfinity && std::isinf(d))) { setError("Invalid number: out of range"); return false; }
      }
    #endif
      if (!allowNaNInfinity && (std::isnan(d) || std::isinf(d))) {
        setError("NaN or Infinity not allowed");
        return false;
      }
      tokenBuffer_.clear();
      handler_.onDoubleValue(d);
    } else {
      long long l = 0;
      auto [ptr, ec] = std::from_chars(buf, buf + len, l);
      if (ec != std::errc() || ptr != buf + len) {
        // int64 overflow: same documented divergence as the stateless parser --
        // the DOM parser keeps the literal as a raw_number string, SAX emits it
        // as a double (a JSON number either way, with type conversion).
        tokenBuffer_.clear();
        errno = 0;
        char* end = nullptr;
        double d = std::strtod(numStr.c_str(), &end);
        if (errno == ERANGE || end != numStr.c_str() + numStr.size()) {
          setError("Invalid number: out of range");
          return false;
        }
        if (!allowNaNInfinity && (std::isnan(d) || std::isinf(d))) {
          setError("NaN or Infinity not allowed");
          return false;
        }
        handler_.onDoubleValue(d);
      } else {
        tokenBuffer_.clear();
        handler_.onIntegerValue(l);
      }
    }
    return afterValue();
  }

  bool processLiteral() {
    size_t len = (state_ == State::LIT_TRUE) ? 4 : (state_ == State::LIT_FALSE) ? 5 : 4;
    const char* lit = (state_ == State::LIT_TRUE) ? "true" : (state_ == State::LIT_FALSE) ? "false" : "null";
    // tokenStart_ points at the first literal char; litIdx_ counts chars already
    // validated. buffer_ is never compacted while in a LIT_* state (see
    // compact()), so the token always lives intact starting at tokenStart_.
    while (litIdx_ < len) {
      size_t idx = tokenStart_ + litIdx_;
      if (idx >= buffer_.size()) break; // need more data
      if (buffer_[idx] != lit[litIdx_]) { setError("Invalid literal"); return false; }
      litIdx_++;
    }
    pos_ = tokenStart_ + litIdx_; // scan pointer derived from validated progress
    if (litIdx_ < len) return true; // need more data
    if (state_ == State::LIT_TRUE) handler_.onBoolValue(true);
    else if (state_ == State::LIT_FALSE) handler_.onBoolValue(false);
    else handler_.onNullValue();
    // Consistent with JsonSaxParser: reject a literal followed by an ident
    // char (e.g. "truey" must error, not silently parse as "true").
    if (pos_ < buffer_.size() && isIdentChar(buffer_[pos_])) {
      setError("Invalid literal"); return false;
    }
    return afterValue();
  }

  // After a complete top-level value only whitespace/comments may follow. Any
  // other byte (e.g. a second value) is trailing data after the value.
  bool processTrailing() {
    while (true) {
      skipWsAndComments();
      if (state_ == State::LINE_COMMENT || state_ == State::BLOCK_COMMENT) return true;
      if (pos_ >= buffer_.size()) return true; // need more data
      if (pendingComment()) { needMore_ = true; return true; } // '/' awaiting a comment start
      setError("Data after complete value"); return false;
    }
  }

  bool processLineComment() {
    while (pos_ < buffer_.size()) {
      if (buffer_[pos_] == '\n') {
        pos_++;
        state_ = commentRetState_;
        return true;
      }
      pos_++;
    }
    return true; // consumed to end of buffer, wait for more
  }

  bool processBlockComment() {
    while (pos_ + 1 < buffer_.size()) {
      if (buffer_[pos_] == '*' && buffer_[pos_ + 1] == '/') {
        pos_ += 2;
        state_ = commentRetState_;
        return true;
      }
      pos_++;
    }
    // At most one byte is left unexamined. If it is '*', it may be the first
    // half of a '*/' whose '/' arrives in the next chunk, so it must stay in the
    // buffer: consuming it made a comment split at that boundary swallow the
    // rest of the document ("/* abc *" + "/ 42" -> unterminated comment, and
    // "/*abc*" + "/def*/" absorbed "def"). Pause so the next feed pairs it up.
    if (pos_ < buffer_.size()) {
      if (buffer_[pos_] == '*') { needMore_ = true; return true; }
      pos_++;
    }
    return true; // need more data
  }

  void advance() { pos_++; }

  // Called after a value is emitted (top-level, in object after colon, or in array)
  bool afterValue() {
    hadValue_ = true;
    if (containerStack_.empty()) {
      // A complete top-level value: do NOT set done_ here -- the remaining
      // space in this chunk must still be scanned. processTrailing() below
      // accepts only whitespace/comments until the end of input, so trailing
      // garbage inside the same chunk is rejected ("Data after complete value").
      state_ = State::TAIL;
      return true;
    }
    Frame& f = containerStack_.back();
    f.hasVal = true;
    if (f.kind == Container::ARR) {
      if (++f.count > asvJSONValue::MAX_ARRAY_SIZE) {
        setError("Array too large");
        return false;
      }
      state_ = State::ARR_VALUE; // after value in array, expect , or ]
    } else {
      state_ = State::OBJ_KEY; // after value in object, expect , or }
    }
    return true;
  }
};

// ============================================================================
// JsonSaxToDom -- builds a DOM tree from SAX events
// ============================================================================
// Use with either JsonSaxParser or JsonStreamParser to build an asvJSONValue tree.

class JsonSaxToDom : public JsonSaxHandler {
public:
  std::unique_ptr<asvJSONValue> getResult() {
    if (fatal_) return nullptr;
    return std::move(root_);
  }

  void onStartObject() override {
    if (fatal_) return;
    auto obj = asvJSONValue::makeObject();
    if (!obj) { fatal_ = true; errorMsg_ = "SAX-to-DOM allocation failed"; return; }
    pushContainer(std::move(obj));
  }

  void onEndObject() override {
    if (fatal_) return;
    popContainer();
  }

  void onStartArray() override {
    if (fatal_) return;
    auto arr = asvJSONValue::makeArray();
    if (!arr) { fatal_ = true; errorMsg_ = "SAX-to-DOM allocation failed"; return; }
    if (arr->arr) arr->arr->reserve(16);
    pushContainer(std::move(arr));
  }

  void onEndArray() override {
    if (fatal_) return;
    popContainer();
  }

  void onKey(std::string_view key) override {
    if (fatal_) return;
    currentKey_ = std::string(key);
  }

  void onStringValue(std::string_view value) override {
    if (fatal_) return;
    // An empty string_view may carry data() == nullptr; makeString requires a
    // valid pointer even for a zero-length value.
    auto v = asvJSONValue::makeString(value.empty() ? "" : value.data(), value.size());
    if (!v) { fatal_ = true; errorMsg_ = "SAX-to-DOM allocation failed"; return; }
    if (!addValue(std::move(v))) return;
  }

  void onIntegerValue(int64_t value) override {
    if (fatal_) return;
    auto v = asvJSONValue::makeInt(value);
    if (!v) { fatal_ = true; errorMsg_ = "SAX-to-DOM allocation failed"; return; }
    if (!addValue(std::move(v))) return;
  }

  void onDoubleValue(double value) override {
    if (fatal_) return;
    auto v = asvJSONValue::makeDouble(value);
    if (!v) { fatal_ = true; errorMsg_ = "SAX-to-DOM allocation failed"; return; }
    if (!addValue(std::move(v))) return;
  }

  void onBoolValue(bool value) override {
    if (fatal_) return;
    auto v = asvJSONValue::makeBool(value);
    if (!v) { fatal_ = true; errorMsg_ = "SAX-to-DOM allocation failed"; return; }
    if (!addValue(std::move(v))) return;
  }

  void onNullValue() override {
    if (fatal_) return;
    auto v = asvJSONValue::makeNull();
    if (!v) { fatal_ = true; errorMsg_ = "SAX-to-DOM allocation failed"; return; }
    if (!addValue(std::move(v))) return;
  }

  void onError(std::string_view message) override {
    if (fatal_) return;
    fatal_ = true;
    errorMsg_ = std::string(message);
  }

  std::string_view getError() const { return errorMsg_; }

private:
  std::unique_ptr<asvJSONValue> root_;
  std::vector<asvJSONValue*> stack_; // raw pointers (root_ owns)
  std::string currentKey_;
  std::string errorMsg_;
  bool fatal_ = false;

  void pushContainer(std::unique_ptr<asvJSONValue> node) {
    if (!node) return;
    asvJSONValue* raw = node.get();
    if (stack_.empty()) {
      root_ = std::move(node);
    } else if (!addToCurrent(std::move(node))) {
      return; // node destroyed in mismatch, raw would be dangling
    }
    stack_.push_back(raw);
  }

  // An unbalanced event stream (a stray onEndObject) is a caller bug, not a
  // parse error: there is no input left to reject it against, so the extra
  // end event is a no-op rather than a diagnostic. Well-formed input always
  // balances, so the guard only fires for such a stream.
  void popContainer() {
    if (!stack_.empty()) stack_.pop_back();
  }

  bool addValue(std::unique_ptr<asvJSONValue> val) {
    if (stack_.empty()) {
      root_ = std::move(val);
      return true;
    }
    return addToCurrent(std::move(val));
  }

  bool addToCurrent(std::unique_ptr<asvJSONValue> val) {
    asvJSONValue* parent = stack_.back();
    if (parent->type == asvJSONValue::OBJECT && parent->obj) {
      (*parent->obj)[currentKey_] = std::move(val);
      return true;
    }
    if (parent->type == asvJSONValue::ARRAY && parent->arr) {
      parent->arr->push_back(std::move(val));
      return true;
    }
    errorMsg_ = "SAX value emitted outside object/array context";
    fatal_ = true;
    return false;
  }
};

} // namespace asvJSONInternal

using asvJSONInternal::JsonSaxHandler;
using asvJSONInternal::JsonSaxParser;
using asvJSONInternal::JsonStreamParser;
using asvJSONInternal::JsonSaxToDom;
