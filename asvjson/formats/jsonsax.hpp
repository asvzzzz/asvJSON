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
          if (pos_ + 1 >= input_.size()) { handler_->onError("Unclosed multiline comment"); throw std::runtime_error("Unclosed multiline comment"); }
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
    std::string msg = std::string("Unexpected: '") + c + "'";
    handler_->onError(msg);
    throw std::runtime_error(msg);
    return false;
  }

  bool parseObject() {
    if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) {
      handler_->onError("Maximum nesting depth exceeded");
      throw std::runtime_error("Maximum nesting depth exceeded");
    }
    depth_++;
    handler_->onStartObject();
    advance(); // consume '{'
    skip();
    if (cur() == '}') { advance(); depth_--; handler_->onEndObject(); return true; }
    while (true) {
      skip();
      if (cur() != '"') { handler_->onError("Expected string key"); throw std::runtime_error("Expected string key"); }
      // Parse key
      {
        std::string raw = scanStringRaw();
        std::string key = unescapeJsonString(raw, true);
        handler_->onKey(key);
      }
      skip();
      if (cur() != ':') { handler_->onError("Expected ':'"); throw std::runtime_error("Expected ':'"); }
      advance();
      if (!parseValue()) return false;
      skip();
      if (cur() == '}') { advance(); depth_--; handler_->onEndObject(); return true; }
      if (cur() == ',') { advance(); continue; }
      handler_->onError("Expected ',' or '}'");
      throw std::runtime_error("Expected ',' or '}'");
    }
  }

  bool parseArray() {
    if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) {
      handler_->onError("Maximum nesting depth exceeded");
      throw std::runtime_error("Maximum nesting depth exceeded");
    }
    depth_++;
    handler_->onStartArray();
    advance(); // consume '['
    skip();
    if (cur() == ']') { advance(); depth_--; handler_->onEndArray(); return true; }
    while (true) {
      if (!parseValue()) return false;
      skip();
      if (cur() == ']') { advance(); depth_--; handler_->onEndArray(); return true; }
      if (cur() == ',') { advance(); continue; }
      handler_->onError("Expected ',' or ']'");
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
    catch (const std::runtime_error&) { handler_->onError("Invalid escape in string"); throw std::runtime_error("Invalid escape in string"); }
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
    if (intLen == 0) { handler_->onError("Invalid number"); throw std::runtime_error("Invalid number"); }
    if (intLen > 1 && input_[intStart] == '0') { handler_->onError("Leading zero"); throw std::runtime_error("Leading zero"); }

    bool isDouble = false;
    if (cur() == '.') {
      isDouble = true; advance();
      if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') {
        handler_->onError("Digit required after decimal point");
        throw std::runtime_error("Digit required after decimal point");
      }
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') advance();
    }
    if (cur() == 'e' || cur() == 'E') {
      isDouble = true; advance();
      if (cur() == '+' || cur() == '-') advance();
      if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') {
        handler_->onError("Digit required after exponent");
        throw std::runtime_error("Digit required after exponent");
      }
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') advance();
    }

    size_t numLen = pos_ - start;
    const char* buf = input_.data() + start;

    if (isDouble) {
      double d = 0;
      auto [ptr, ec] = std::from_chars(buf, buf + numLen, d);
      if (ec != std::errc() || ptr != buf + numLen) {
        // Try strtod as fallback
        char saved = buf[numLen];
        char* mutableBuf = const_cast<char*>(buf);
        mutableBuf[numLen] = '\0';
        d = std::strtod(buf, nullptr);
        mutableBuf[numLen] = saved;
      }
      if (!allowNaNInfinity && (std::isnan(d) || std::isinf(d))) {
        handler_->onError("NaN or Infinity not allowed");
        throw std::runtime_error("NaN or Infinity not allowed");
      }
      handler_->onDoubleValue(d);
    } else {
      long long l = 0;
      auto [ptr, ec] = std::from_chars(buf, buf + numLen, l);
      if (ec != std::errc() || ptr != buf + numLen) {
        // Too large for int64 -- emit as double if possible, else as raw string
        char saved = buf[numLen];
        char* mutableBuf = const_cast<char*>(buf);
        mutableBuf[numLen] = '\0';
        double d = std::strtod(buf, nullptr);
        mutableBuf[numLen] = saved;
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
        if (cur() != exp[i]) { handler_->onError("Invalid boolean"); throw std::runtime_error("Invalid boolean"); }
        advance();
      }
      if (pos_ < input_.size() && isIdentChar(cur())) { handler_->onError("Invalid boolean"); throw std::runtime_error("Invalid boolean"); }
      handler_->onBoolValue(true);
      return true;
    }
    if (cur() == 'f') {
      const char exp[] = "false";
      for (int i = 0; i < 5; i++) {
        if (cur() != exp[i]) { handler_->onError("Invalid boolean"); throw std::runtime_error("Invalid boolean"); }
        advance();
      }
      if (pos_ < input_.size() && isIdentChar(cur())) { handler_->onError("Invalid boolean"); throw std::runtime_error("Invalid boolean"); }
      handler_->onBoolValue(false);
      return true;
    }
    handler_->onError("Expected boolean");
    throw std::runtime_error("Expected boolean");
  }

  bool parseNull() {
    const char exp[] = "null";
    for (int i = 0; i < 4; i++) {
      if (cur() != exp[i]) { handler_->onError("Invalid null"); throw std::runtime_error("Invalid null"); }
      advance();
    }
    if (pos_ < input_.size() && isIdentChar(cur())) { handler_->onError("Invalid null"); throw std::runtime_error("Invalid null"); }
    handler_->onNullValue();
    return true;
  }

  bool parseNaN() {
    if (input_.compare(pos_, 3, "NaN") != 0) { handler_->onError("Expected NaN"); throw std::runtime_error("Expected NaN"); }
    pos_ += 3;
    if (pos_ < input_.size() && isIdentChar(cur())) { handler_->onError("Invalid NaN"); throw std::runtime_error("Invalid NaN"); }
    handler_->onDoubleValue(std::numeric_limits<double>::quiet_NaN());
    return true;
  }

  bool parseInfinity(bool negative) {
    if (negative) advance(); // skip '-'
    if (input_.compare(pos_, 8, "Infinity") != 0) { handler_->onError("Expected Infinity"); throw std::runtime_error("Expected Infinity"); }
    pos_ += 8;
    if (pos_ < input_.size() && isIdentChar(cur())) { handler_->onError("Invalid Infinity"); throw std::runtime_error("Invalid Infinity"); }
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

  explicit JsonStreamParser(JsonSaxHandler& handler)
    : handler_(handler) {}

  // Feed a chunk of input data. Returns false on parse error.
  bool feed(std::string_view chunk) {
    if (error_) return false;
    if (done_) { lastError_ = "Data after complete value"; error_ = true; handler_.onError(lastError_); return false; }
    buffer_.append(chunk.data(), chunk.size());
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
        // Emit the pending number
        if (!emitPendingNumber()) return false;
        afterValue();
        break;
      case State::LIT_TRUE:
        if (tokenBuffer_ == "tru") { handler_.onBoolValue(true); afterValue(); }
        else { lastError_ = "Invalid literal"; error_ = true; handler_.onError(lastError_); return false; }
        break;
      case State::LIT_FALSE:
        if (tokenBuffer_ == "fals") { handler_.onBoolValue(false); afterValue(); }
        else { lastError_ = "Invalid literal"; error_ = true; handler_.onError(lastError_); return false; }
        break;
      case State::LIT_NULL:
        if (tokenBuffer_ == "nul") { handler_.onNullValue(); afterValue(); }
        else { lastError_ = "Invalid literal"; error_ = true; handler_.onError(lastError_); return false; }
        break;
      case State::LINE_COMMENT:
        // Comment consumed to end of input -- OK
        state_ = State::TOP;
        break;
      case State::BLOCK_COMMENT:
        lastError_ = "Unterminated block comment"; error_ = true; handler_.onError(lastError_); return false;
      default:
        if (state_ == State::TOP && depth_ == 0 && !hadValue_) {
          // Empty input is OK only if nothing was parsed
          if (buffer_.empty() || (pos_ >= buffer_.size() && tokenBuffer_.empty())) break;
        }
        if (depth_ > 0 || state_ != State::TOP) {
          lastError_ = "Incomplete input"; error_ = true; handler_.onError(lastError_); return false;
        }
        break;
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

  // Container stack for tracking context
  enum class Container { OBJ, ARR };
  std::vector<Container> containerStack_;

  void setError(const std::string& msg) {
    lastError_ = msg; error_ = true; handler_.onError(lastError_);
  }

  // Compact buffer when it grows large and we're at a safe point
  void compact() {
    if (pos_ > 0 && pos_ <= buffer_.size() && state_ != State::STR_VALUE && state_ != State::STR_KEY && state_ != State::NUM) {
      buffer_.erase(0, pos_);
      pos_ = 0;
    }
  }

  bool process() {
    while (pos_ < buffer_.size() && !done_ && !error_) {
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
        pos_ += 2;
        state_ = State::LINE_COMMENT;
        return;
      }
      if (allowComments && ch == '/' && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == '*') {
        pos_ += 2;
        state_ = State::BLOCK_COMMENT;
        return;
      }
      if (allowComments && ch == '#') {
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

    char c = buffer_[pos_];
    if (c == ',' && state_ == State::ARR_VALUE) { advance(); skipWsAndComments(); if (state_ == State::LINE_COMMENT || state_ == State::BLOCK_COMMENT || pos_ >= buffer_.size()) return true; c = buffer_[pos_]; }
    if (c == '{') {
      if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) { setError("Maximum nesting depth exceeded"); return false; }
      depth_++;
      containerStack_.push_back(Container::OBJ);
      handler_.onStartObject();
      advance();
      state_ = State::OBJ_KEY;
      return true;
    }
    if (c == '}') {
      if (containerStack_.empty() || containerStack_.back() != Container::OBJ) { setError("Unexpected '}'"); return false; }
      containerStack_.pop_back();
      depth_--;
      handler_.onEndObject();
      advance();
      afterValue();
      return true;
    }
    if (c == '[') {
      if (depth_ >= asvJSONValue::MAX_NESTING_DEPTH) { setError("Maximum nesting depth exceeded"); return false; }
      depth_++;
      containerStack_.push_back(Container::ARR);
      handler_.onStartArray();
      advance();
      state_ = State::ARR_VALUE;
      return true;
    }
    if (c == ']') {
      if (containerStack_.empty() || containerStack_.back() != Container::ARR) { setError("Unexpected ']'"); return false; }
      containerStack_.pop_back();
      depth_--;
      handler_.onEndArray();
      advance();
      afterValue();
      return true;
    }
    if (c == '"') {
      tokenBuffer_.clear();
      tokenStart_ = pos_ + 1; // past opening quote
      advance();
      if (!containerStack_.empty() && containerStack_.back() == Container::OBJ && state_ == State::OBJ_VALUE) {
        // This shouldn't happen -- OBJ_VALUE should go through the colon path
        // But if we're in ARR_VALUE or TOP with a string, it's a value
      }
      // Determine if this is a key (next state should be OBJ_COLON if in OBJ context)
      state_ = (state_ == State::OBJ_VALUE) ? State::STR_VALUE : State::STR_VALUE;
      // Actually: if we're at top-level or in array or after colon in object, it's a value
      state_ = State::STR_VALUE;
      return processString();
    }
    if (c == 't') { tokenBuffer_.clear(); tokenStart_ = pos_; state_ = State::LIT_TRUE; advance(); return processLiteral(); }
    if (c == 'f') { tokenBuffer_.clear(); tokenStart_ = pos_; state_ = State::LIT_FALSE; advance(); return processLiteral(); }
    if (c == 'n') { tokenBuffer_.clear(); tokenStart_ = pos_; state_ = State::LIT_NULL; advance(); return processLiteral(); }
    if (allowNaNInfinity && c == 'N') {
      if (buffer_.compare(pos_, 3, "NaN") == 0) {
        pos_ += 3;
        handler_.onDoubleValue(std::numeric_limits<double>::quiet_NaN());
        afterValue();
        return true;
      }
    }
    if (allowNaNInfinity && c == 'I') {
      if (buffer_.compare(pos_, 8, "Infinity") == 0) {
        pos_ += 8;
        handler_.onDoubleValue(std::numeric_limits<double>::infinity());
        afterValue();
        return true;
      }
    }
    if (allowNaNInfinity && c == '-' && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == 'I') {
      if (buffer_.compare(pos_ + 1, 8, "Infinity") == 0) {
        pos_ += 9;
        handler_.onDoubleValue(-std::numeric_limits<double>::infinity());
        afterValue();
        return true;
      }
    }
    if (allowNaNInfinity && c == '-' && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == 'N') {
      if (buffer_.compare(pos_ + 1, 3, "NaN") == 0) {
        pos_ += 4;
        handler_.onDoubleValue(std::numeric_limits<double>::quiet_NaN());
        afterValue();
        return true;
      }
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
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
    char c = buffer_[pos_];
    if (c == ',') { advance(); skipWsAndComments(); if (state_ == State::LINE_COMMENT || state_ == State::BLOCK_COMMENT || pos_ >= buffer_.size()) return true; c = buffer_[pos_]; }
    if (c == '}') {
      containerStack_.pop_back();
      depth_--;
      handler_.onEndObject();
      advance();
      afterValue();
      return true;
    }
    if (c == '"') {
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
      char c = buffer_[pos_];
      if (c == '"') {
        // Complete string found
        std::string raw = tokenBuffer_;
        raw.append(buffer_, tokenStart_, pos_ - tokenStart_);
        advance(); // past closing quote

        if (state_ == State::STR_KEY) {
          handler_.onKey(raw);
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
            try {
              std::string unescaped = unescapeJsonString(raw, true);
              handler_.onStringValue(unescaped);
            } catch (...) {
              setError("Invalid escape in string");
              return false;
            }
          }
          afterValue();
        }
        tokenBuffer_.clear();
        return true;
      }
      if (c == '\\') {
        if (pos_ + 1 >= buffer_.size()) {
          // Escape at end of buffer -- need more data
          tokenBuffer_.append(buffer_, tokenStart_, pos_ - tokenStart_ + 1);
          pos_++;
          tokenStart_ = pos_;
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

    bool isDouble = false;
    for (char c : numStr) {
      if (c == '.' || c == 'e' || c == 'E') { isDouble = true; break; }
    }

    const char* buf = numStr.data();
    size_t len = numStr.size();

    if (isDouble) {
      double d = 0;
      auto [ptr, ec] = std::from_chars(buf, buf + len, d);
      if (ec != std::errc() || ptr != buf + len) {
        char saved = buf[len];
        char* mb = const_cast<char*>(buf);
        mb[len] = '\0';
        d = std::strtod(buf, nullptr);
        mb[len] = saved;
      }
      if (!allowNaNInfinity && (std::isnan(d) || std::isinf(d))) {
        setError("NaN or Infinity not allowed");
        return false;
      }
      handler_.onDoubleValue(d);
    } else {
      long long l = 0;
      auto [ptr, ec] = std::from_chars(buf, buf + len, l);
      if (ec != std::errc() || ptr != buf + len) {
        char saved = buf[len];
        char* mb = const_cast<char*>(buf);
        mb[len] = '\0';
        double d = std::strtod(buf, nullptr);
        mb[len] = saved;
        handler_.onDoubleValue(d);
      } else {
        handler_.onIntegerValue(l);
      }
    }
    tokenBuffer_.clear();
    afterValue();
    return true;
  }

  bool processLiteral() {
    static const char* literals[] = { "true", "false", "null" };
    size_t expectedLens[] = { 4, 5, 4 };
    int idx = (state_ == State::LIT_TRUE) ? 0 : (state_ == State::LIT_FALSE) ? 1 : 2;
    const char* expected = literals[idx];
    size_t expectedLen = expectedLens[idx];

    // Rebuild what we have so far
    std::string current = tokenBuffer_;
    current.append(buffer_, tokenStart_, pos_ - tokenStart_);

    // Check if we've consumed more from buffer since tokenStart_
    // Actually tokenStart_ was set when we entered the literal state
    // and pos_ has been advancing. tokenBuffer_ holds data from previous chunks.
    // But we also need to re-read from tokenStart_ in current buffer...
    // Let me reconsider: when we enter LIT state, tokenStart_ = pos_ (before advance)
    // and we advanced pos_. So we need to gather from tokenStart_ to pos_.
    // But if we've been accumulating, tokenBuffer_ has old data.

    // Simpler approach: scan what we have vs what we need
    if (current.size() > expectedLen) {
      setError("Invalid literal");
      return false;
    }

    for (size_t i = 0; i < current.size(); i++) {
      if (current[i] != expected[i]) {
        setError("Invalid literal");
        return false;
      }
    }

    if (current.size() == expectedLen) {
      // Complete literal
      if (idx == 0) handler_.onBoolValue(true);
      else if (idx == 1) handler_.onBoolValue(false);
      else handler_.onNullValue();
      tokenBuffer_.clear();
      afterValue();
      return true;
    }

    // Incomplete -- need more data
    // Scan more from buffer if available
    while (pos_ < buffer_.size() && current.size() < expectedLen) {
      current += buffer_[pos_];
      pos_++;
    }

    if (current.size() > expectedLen) {
      setError("Invalid literal");
      return false;
    }

    if (current.size() == expectedLen) {
      for (size_t i = 0; i < expectedLen; i++) {
        if (current[i] != expected[i]) {
          setError("Invalid literal");
          return false;
        }
      }
      if (idx == 0) handler_.onBoolValue(true);
      else if (idx == 1) handler_.onBoolValue(false);
      else handler_.onNullValue();
      tokenBuffer_.clear();
      afterValue();
      return true;
    }

    tokenBuffer_ = current;
    return true; // need more data
  }

  bool processLineComment() {
    while (pos_ < buffer_.size()) {
      if (buffer_[pos_] == '\n') {
        pos_++;
        state_ = State::TOP; // Return to whatever state we were in
        // Determine correct state from container stack
        if (!containerStack_.empty()) {
          if (containerStack_.back() == Container::OBJ) state_ = State::OBJ_KEY;
          else state_ = State::ARR_VALUE;
        }
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
        state_ = State::TOP;
        if (!containerStack_.empty()) {
          if (containerStack_.back() == Container::OBJ) state_ = State::OBJ_KEY;
          else state_ = State::ARR_VALUE;
        }
        return true;
      }
      pos_++;
    }
    if (pos_ < buffer_.size()) pos_++; // skip last char if odd
    return true; // need more data
  }

  void advance() { pos_++; }

  // Called after a value is emitted (top-level, in object after colon, or in array)
  void afterValue() {
    hadValue_ = true;
    if (containerStack_.empty()) {
      state_ = State::DONE;
      done_ = true;
      return;
    }
    Container top = containerStack_.back();
    if (top == Container::OBJ) {
      state_ = State::OBJ_KEY; // after value in object, expect , or }
    } else {
      state_ = State::ARR_VALUE; // after value in array, expect , or ]
    }
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
