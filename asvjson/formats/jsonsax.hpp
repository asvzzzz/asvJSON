#pragma once
// JSON SAX / Streaming parser for asvJSON++
// Supports:
//   - SAX event-based parsing with callbacks (JsonSaxParser)
//   - Chunk-based incremental/streaming parsing (JsonStreamParser)
//   - SAX-to-DOM conversion adapter (JsonSaxToDom)
//   - Comments (//, /* */, #), NaN/Infinity, same options as DOM parser
//   - Resource limits: MAX_NESTING_DEPTH, MAX_STRING_LEN

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

namespace asvJSONInternal {

// ============================================================================
// Callback interface for SAX parsing
// ============================================================================
// Each callback returns void. If the handler needs to abort parsing,
// it should throw an exception (which will be caught and reported as an error).
// The string_view parameters point into data valid only during the callback;
// the handler must copy if it needs the data beyond the callback scope.

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
      return parseNaN();
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
    throw std::runtime_error(std::string("Unexpected: '") + c + "'");
  }

  bool parseObject() {
    if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) throw std::runtime_error("Maximum nesting depth exceeded");
    depth_++;
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
      skip();
      if (cur() == '}') { advance(); depth_--; handler_->onEndObject(); return true; }
      if (cur() == ',') { advance(); continue; }
      throw std::runtime_error("Expected ',' or '}'");
    }
  }

  bool parseArray() {
    if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) throw std::runtime_error("Maximum nesting depth exceeded");
    depth_++;
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
      skip();
      if (cur() == ']') { advance(); depth_--; handler_->onEndArray(); return true; }
      if (cur() == ',') { advance(); continue; }
      throw std::runtime_error("Expected ',' or ']'");
    }
  }

  bool parseStringValue() {
    std::string raw = scanStringRaw();
    // DateTime heuristic
    if (raw.size() >= 20 && raw[4] == '-' && raw[7] == '-' && raw[10] == 'T') {
      time_t ts; int ms = 0;
      if (tryParseDateTime(raw, ts, &ms)) {
        // Emit as string (SAX doesn't have a datetime event)
        handler_->onStringValue(raw);
        return true;
      }
    }
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
        // Overflow (e.g. 1e999): from_chars sets d to +/-inf. Honor allowNaNInfinity.
        // Underflow (e.g. 1e-999) leaves d finite/0 -> still rejected.
        if (ec == std::errc::result_out_of_range && allowNaNInfinity && std::isinf(d)) bad = false;
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
        // Too large for int64 -- emit as double if possible, else as raw string
        std::string numStr(buf, numLen);
        double d = std::strtod(numStr.c_str(), nullptr);
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

  bool parseNaN() {
    if (input_.compare(pos_, 3, "NaN") != 0) throw std::runtime_error("Expected NaN");
    pos_ += 3;
    if (pos_ < input_.size() && isIdentChar(cur())) throw std::runtime_error("Invalid NaN");
    handler_->onDoubleValue(std::numeric_limits<double>::quiet_NaN());
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
    if (done_) { lastError_ = "Data after complete value"; error_ = true; handler_.onError(lastError_); return false; }
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
    if (validateUTF8) {
      for (size_t i = utf8ScanPos_; i < buffer_.size(); i++) {
        uint8_t b = static_cast<uint8_t>(buffer_[i]);
        if (utf8Need_ == 0) {
          if (b < 0x80) continue;
          if ((b & 0xE0) == 0xC0) utf8Need_ = 1;
          else if ((b & 0xF0) == 0xE0) utf8Need_ = 2;
          else if ((b & 0xF8) == 0xF0) utf8Need_ = 3;
          else { setError("Invalid UTF-8 in input"); return false; }
        } else {
          if ((b & 0xC0) != 0x80) { setError("Invalid UTF-8 in input"); return false; }
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
        if (!emitPendingNumber()) return false;
        if (!afterValue()) return false;
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

    if (validateUTF8 && utf8Need_ != 0) {
      // A multibyte sequence was cut off at end of input.
      lastError_ = "Invalid UTF-8 in input"; error_ = true; handler_.onError(lastError_); return false;
    }

    if (depth_ > 0 || state_ != State::TOP) {
      // Empty input is the only content-less TOP allowed; a trailing line
      // comment that was not followed by a newline is harmless.
      if (!(state_ == State::TOP && depth_ == 0 && !hadValue_ &&
            (buffer_.empty() || (pos_ >= buffer_.size() && tokenBuffer_.empty())))) {
        lastError_ = "Incomplete input"; error_ = true; handler_.onError(lastError_); return false;
      }
    }
    state_ = State::DONE;
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
    DONE,
    ERROR
  };

  JsonSaxHandler& handler_;
  std::string buffer_;
  size_t pos_ = 0;
  State state_ = State::TOP;
  int depth_ = 0;
  bool hadValue_ = false;
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
    lastError_ = msg; error_ = true; handler_.onError(lastError_);
  }

  // True when the next unconsumed char is a '/' sitting at the very end of the
  // buffer (the rest of a `//` or `/*` comment may arrive in a later chunk).
  bool pendingComment() const {
    return allowComments && pos_ < buffer_.size() && buffer_[pos_] == '/' && pos_ + 1 >= buffer_.size();
  }

  static bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
  }

  static bool isValueStart(char c) {
    return c == '"' || c == '{' || c == '[' || c == 't' || c == 'f' || c == 'n' ||
           c == '-' || (c >= '0' && c <= '9') || c == 'N' || c == 'I';
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
          // DateTime heuristic
          bool emitted = false;
          if (raw.size() >= 20 && raw[4] == '-' && raw[7] == '-' && raw[10] == 'T') {
            time_t ts; int ms = 0;
            if (tryParseDateTime(raw, ts, &ms)) {
              handler_.onStringValue(raw);
              emitted = true;
            }
          }
          if (!emitted) {
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
    // Save partial and wait
    tokenBuffer_.append(buffer_, tokenStart_, buffer_.size() - tokenStart_);
    tokenStart_ = buffer_.size();
    pos_ = buffer_.size();
    return true; // need more data
  }

  bool emitPendingNumber() {
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
        // fall back to strtod.
        errno = 0;
        d = std::strtod(numStr.c_str(), nullptr);
        if (errno == ERANGE) { setError("Invalid number: out of range"); return false; }
      } else if (ec == std::errc::result_out_of_range && allowNaNInfinity && std::isinf(d)) {
        // Overflow honoring allowNaNInfinity, matching the DOM parser.
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
        tokenBuffer_.clear();
        double d = std::strtod(numStr.c_str(), nullptr);
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
    return afterValue();
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
    if (pos_ < buffer_.size()) pos_++; // skip last char if odd
    return true; // need more data
  }

  void advance() { pos_++; }

  // Called after a value is emitted (top-level, in object after colon, or in array)
  bool afterValue() {
    hadValue_ = true;
    if (containerStack_.empty()) {
      state_ = State::DONE;
      done_ = true;
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
  std::unique_ptr<asvJSONValue> getResult() { return std::move(root_); }

  void onStartObject() override {
    auto obj = asvJSONValue::makeObject();
    pushContainer(std::move(obj));
  }

  void onEndObject() override {
    popContainer();
  }

  void onStartArray() override {
    auto arr = asvJSONValue::makeArray();
    arr->arr->reserve(16);
    pushContainer(std::move(arr));
  }

  void onEndArray() override {
    popContainer();
  }

  void onKey(std::string_view key) override {
    currentKey_ = std::string(key);
  }

  void onStringValue(std::string_view value) override {
    auto v = asvJSONValue::makeString(value.data(), value.size());
    addValue(std::move(v));
  }

  void onIntegerValue(int64_t value) override {
    auto v = asvJSONValue::makeInt(value);
    addValue(std::move(v));
  }

  void onDoubleValue(double value) override {
    auto v = asvJSONValue::makeDouble(value);
    addValue(std::move(v));
  }

  void onBoolValue(bool value) override {
    auto v = asvJSONValue::makeBool(value);
    addValue(std::move(v));
  }

  void onNullValue() override {
    auto v = asvJSONValue::makeNull();
    addValue(std::move(v));
  }

  void onError(std::string_view message) override {
    errorMsg_ = std::string(message);
  }

  std::string_view getError() const { return errorMsg_; }

private:
  std::unique_ptr<asvJSONValue> root_;
  std::vector<asvJSONValue*> stack_; // raw pointers (root_ owns)
  std::string currentKey_;
  std::string errorMsg_;

  void pushContainer(std::unique_ptr<asvJSONValue> node) {
    asvJSONValue* raw = node.get();
    if (stack_.empty()) {
      root_ = std::move(node);
    } else {
      addToCurrent(std::move(node));
    }
    stack_.push_back(raw);
  }

  void popContainer() {
    if (!stack_.empty()) stack_.pop_back();
  }

  void addValue(std::unique_ptr<asvJSONValue> val) {
    if (stack_.empty()) {
      root_ = std::move(val);
      return;
    }
    addToCurrent(std::move(val));
  }

  void addToCurrent(std::unique_ptr<asvJSONValue> val) {
    asvJSONValue* parent = stack_.back();
    if (parent->type == asvJSONValue::OBJECT && parent->obj) {
      (*parent->obj)[currentKey_] = std::move(val);
    } else if (parent->type == asvJSONValue::ARRAY && parent->arr) {
      parent->arr->push_back(std::move(val));
    }
  }
};

} // namespace asvJSONInternal

using asvJSONInternal::JsonSaxHandler;
using asvJSONInternal::JsonSaxParser;
using asvJSONInternal::JsonStreamParser;
using asvJSONInternal::JsonSaxToDom;
