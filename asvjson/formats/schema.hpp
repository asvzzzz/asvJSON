#pragma once
// JSON Schema validation for asvJSON++
// Supports draft-07 core validation keywords:
//   type, const, enum, allOf, anyOf, oneOf, not,
//   if/then/else, $ref (local),
//   string: minLength, maxLength, pattern,
//   number: minimum, maximum, exclusiveMinimum, exclusiveMaximum, multipleOf,
//   array:  minItems, maxItems, uniqueItems, items, contains,
//   object: required, properties, patternProperties, additionalProperties,
//           minProperties, maxProperties, propertyNames, dependentRequired, dependencies.
//
// "format" is a pure annotation in draft-07: it carries no assertion, so it is
// accepted anywhere and never checked against the instance -- there is no format
// validation behind the scenes. Only the keyword's own shape is asserted (it
// must be a string). "contentMediaType"/"contentEncoding" and the
// "definitions"/"$schema"/"$id" metadata keywords are likewise unasserted.
// $ref is local only: it resolves against the root schema through
// RFC 6901 JSON Pointer, so "#", "#/defs/x" and array tokens such as
// "#/allOf/0" all work, but remote/relative references (other documents,
// external files) are not resolved.
//
// validate() never throws: an escaping std::exception (std::bad_alloc from the
// caches, std::regex_error from a pattern whose *match* runs out of stack) is
// converted into a false return with a "Schema validation failed: ..." message.
// A pathological `pattern`/`patternProperties` regex over attacker-controlled
// text is CPU-expensive (std::regex backtracks): MSVC enforces a pre-set match
// complexity limit and regex_search throws std::regex_error(error_complexity)
// after roughly 1-2 s, which the fence turns into a false return, so validate()
// neither hangs nor crashes there. libstdc++/libc++ have no such cap, so the
// wall-clock bound is toolchain-specific: treat the SCHEMA (its patterns) as
// trusted input even though instances may be hostile.
// A malformed subschema is likewise reported rather than swallowed, including
// inside anyOf/oneOf/not/if/contains -- those keywords abort on the first broken
// branch instead of counting it as a branch the instance failed to match. A
// malformed subschema that the instance never reaches (an unused `properties`
// entry, a `then` that never fires) is not detected, since only schemas actually
// visited are structurally checked.
//
// `pattern` and `patternProperties` keys are compiled at shape-check time, so a
// regex that cannot compile is a malformed schema even when the branch would
// never be exercised. A $ref that is not a string, does not resolve (a dangling
// local pointer), or does not land on a schema is a malformed branch too.
// Circular $ref is the one exception: it re-applies the same schema to the same
// instance with no progress, an unsatisfiable condition that legitimately
// matches nothing, so inside a fork it reads as "branch did not match" rather
// than "schema is broken".
//
// Recursion depth is capped (MAX_VALIDATION_DEPTH): parsed schemas/instances
// are bounded by MAX_NESTING_DEPTH, but programmatically built trees are not,
// and an unbounded descent overflowed the stack. A pathological product of
// deeply nested schema keywords and deeply nested data can overflow it too --
// hitting the cap fails validation as a schema-internal error (forks abort on
// it), never a crash.
//
// Deliberate strictness (draft-07 states a SHOULD, not a MUST, for these):
//   - An EMPTY `enum`, `type`, `allOf`, `anyOf` or `oneOf` is a malformed
//     schema. An empty `enum`/`type`/`anyOf`/`oneOf` matches nothing and an
//     empty `allOf` matches everything, so all five are degenerate.
//   - Duplicate `enum`/`type`/`required` entries are malformed (the arrays
//     SHOULD be unique; `1` and `1.0` are the same enum entry).
//   - An unknown `type` name is malformed instead of compiling to a schema
//     that rejects every value.
//   - The draft-04 boolean `exclusiveMinimum`/`exclusiveMaximum` is only
//     accepted together with the `minimum`/`maximum` it modifies.
//   - An empty `required` stays a legal no-op.

#include "../core.hpp"
#include <cstring>
#include <limits>
#include <memory>
#include <cmath>
#include <functional>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace asvJSONInternal {

class SchemaValidator {
public:
  // Never throws. Everything below runs inside a fence because a schema (or an
  // instance) can drive the validator into a std::exception that no local guard
  // covers -- the sharpest case is std::regex_search: getCompiled() only
  // guards std::regex *construction*, while the search itself can still fail,
  // and MSVC reports a stack/memory exhaustion during the match as
  // std::regex_error(error_stack) from regex_search. Any such escape would
  // break the bool-returning contract of validate() and of asvJSON::validate().
  bool validate(const asvJSONValue* value, const asvJSONValue* schema) {
    error_.clear();
    path_ = "";
    shapeFailed_ = false; // a validator may be reused for a second document
    try {
      return validateImpl(value, schema);
    } catch (const std::exception& e) {
      return failInternal(std::string("Schema validation failed: ") + e.what());
    }
  }

  bool validate(const asvJSONValue* value, const asvJSONValue* schema, std::string& errorOut) {
    bool ok = validate(value, schema);
    errorOut = error_;
    return ok;
  }

  [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
  const asvJSONValue* rootSchema_ = nullptr;
  std::string error_;
  std::string path_;
  // Set when this validator rejected the INSTANCE only because a schema object
  // it walked is malformed (validateKeywordShapes or a non-string $ref). The
  // boolean answers "failed?" only; the message lives in error_.
  //
  // It exists because anyOf/oneOf/not/if/contains fork a sub-validator, and a
  // forked sub-validator's error is thrown away by design: those keywords treat
  // a branch failure as "this instance does not match that branch" and move on.
  // That reading is wrong for a malformed subschema, which is not a property of
  // the instance at all. Without this flag a schema error inside a branch is
  // silently swallowed and the parent reports a nonsense outcome for the
  // instance instead: `{"if":{"enum":5}}` makes every instance take the `else`
  // path (the broken `if` reads as "condition did not hold"), `{"not":{"enum":5}}`
  // accepts every instance (a failing `not` reads as "did not match", so the
  // negation succeeds), and `{"contains":{"enum":5}}` blames the array for a
  // schema that never compiled.
  bool shapeFailed_ = false;
  // The set of (value, schema) pairs currently being resolved through a $ref.
  // Shared by every sub-validator forked by anyOf/oneOf/not/if/contains (and
  // copied into them), so cycle detection is exact across the WHOLE
  // validation. A self-/mutually-referential schema (`{"$ref":"#"}`,
  // `"a":{"$ref":"#/definitions/a"}`, or a cycle reached again from inside an
  // anyOf/oneOf/not/if branch) would otherwise recurse forever through
  // validateNode and overflow the stack.
  //
  // This keyed guard is strictly more precise than a depth counter: a
  // legitimate recursive schema consumes one distinct (value, schema) pair per
  // data level (bounded by the parsers' MAX_NESTING_DEPTH = 48, so the stack
  // can never grow past it), while an actual loop must revisit the same value
  // against the same schema and is caught on the first repetition. A plain
  // depth cap, by contrast, would spuriously reject deep (but acyclic) data
  // and needed the magic 48.
  struct RefKey {
    const asvJSONValue* value;
    const asvJSONValue* schema;
    bool operator==(const RefKey& o) const { return value == o.value && schema == o.schema; }
  };
  struct RefKeyHash {
    size_t operator()(const RefKey& k) const {
      return std::hash<const asvJSONValue*>()(k.value) ^
             (std::hash<const asvJSONValue*>()(k.schema) << 1);
    }
  };
  using RefStack = std::unordered_set<RefKey, RefKeyHash>;
  std::shared_ptr<RefStack> refStack_;
  // Compiled-regex cache shared across the whole validation so repeated
  // `pattern` / `patternProperties` evaluations compile each unique regex once
  // per validate() call instead of once per string node / per object.
  struct PatternCache {
    std::unordered_map<std::string, std::regex> ok;
    std::unordered_set<std::string> bad;
  };
  std::shared_ptr<PatternCache> patternCache_;
  // Cached keyword-shape checks. The shape of a schema is static, so the
  // structural validation in validateKeywordShapes runs once per schema node
  // (instead of once per value validated against it). Schemas are shared
  // between every sub-validator (like patternCache_), so an anyOf over a large
  // document pays one O(K) pass instead of one per branch per value.
  using ShapeCache = std::unordered_map<const asvJSONValue*, bool>;
  std::shared_ptr<ShapeCache> shapeCache_;
  // Cached local $ref resolution: the target of a given ref string depends
  // only on rootSchema_, so a frequently-hit pointer (e.g. a recursive schema
  // on a large document) is walked once instead of once per reference.
  struct RefCacheEntry {
    const asvJSONValue* target = nullptr;
    bool failed = false;
  };
  using RefCache = std::unordered_map<std::string, RefCacheEntry>;
  std::shared_ptr<RefCache> refCache_;
  // Reusable buffer for propertyNames validation: validateNode may push its
  // address into refStack_ when a $ref is encountered. Keeping this on the
  // validator's lifetime (rather than as a stack-local in the loop) avoids a
  // dangling pointer if an early-return or refactor ever changes erase timing.
  asvJSONValue propertyNameVal_;

  // The body of validate(), called from inside its exception fence.
  // An empty document (root == nullptr) is treated as null: it validates against
  // the schema as a null value would. This matches the draft-07 semantics where
  // an absent instance is equivalent to JSON null - so {"type":"object"} fails
  // on an empty document, while true-schema or {} succeeds.
  // Hard cap on the validateNode recursion depth (per validation, continuing
  // across anyOf/oneOf/not/if/contains forks). Parsed inputs sit near the bottom
  // of it: data nests at most MAX_NESTING_DEPTH = 48 levels, and a recursive
  // schema consumes ~1-3 keyword levels per data level (a `properties` descent
  // is one, an `allOf`+`items` pair is two), so a 48x3 product wants ~144.
  // The cap is a STACK budget, not a semantic one, and the frame size varies by
  // keyword shape and by optimisation level - measured on MSVC x64 with the
  // default 1 MB main-thread stack, counting nested validateNode levels:
  //   properties / additionalProperties:  crash at ~250 (/Od), ~900 (/O2)
  //   allOf / anyOf / not / items:        crash at ~350 (/Od), ~600 (/O2)
  // The old 512 sat ABOVE the unoptimized thresholds, so the guard never fired
  // in time and a deep programmatic (raw-API) schema or instance crashed the
  // process (0xC00000FD) instead of failing validation. 160 reports the case
  // with ~90 levels of margin in the worst measured shape and still admits the
  // 48x3 product of parser-built inputs. Only programmatically built trees can
  // nest deeper than the parsers allow; hitting the cap fails validation
  // through failInternal (shapeFailed_, so fork keywords abort on it).
  static constexpr int MAX_VALIDATION_DEPTH = 160;
  int nodeDepth_ = 0;

  // valuesEqual()/identityHash() walk two trees in lockstep and recurse per
  // container level. Parsed trees are capped at MAX_NESTING_DEPTH, but a
  // programmatically built instance (or `enum`/`const` entry) is not, and the
  // unbounded recursion overflowed the stack (~20000 levels crashed in both
  // /Od and /O2). Deeper than this the two operands compare NOT equal, which
  // is the conservative answer: a `const`/`enum` member check reports "not in
  // list" and a uniqueness check stays silent, never a crash.
  static constexpr int MAX_VALUE_COMPARE_DEPTH = 128;

  bool validateImpl(const asvJSONValue* value, const asvJSONValue* schema) {
    if (!schema) return true;
    static const asvJSONValue kNull; // default-constructed node is NULL_VAL
    nodeDepth_ = 0; // a reused validator must not inherit a stale depth
    rootSchema_ = schema;
    refStack_ = std::make_shared<RefStack>();
    patternCache_ = std::make_shared<PatternCache>();
    shapeCache_ = std::make_shared<ShapeCache>();
    refCache_ = std::make_shared<RefCache>();
    return validateNode(value ? value : &kNull, schema);
  }

  // Records the error of a fork that failed on a malformed subschema rather than
  // on the instance. Returns true when the caller must abort with this error.
  //
  // The sub-validator's message already carries the instance path (its setError
  // prefixed it and it was seeded with our path_), so it is adopted verbatim;
  // routing it through our own setError would print the path twice.
  bool adoptShapeError(const SchemaValidator& sub) {
    if (!sub.shapeFailed_) return false;
    error_ = sub.error_;
    shapeFailed_ = true;
    return true;
  }

  // Builds the throwaway sub-validator that anyOf/oneOf/not/if/contains fork
  // off. Copies everything a forked validation shares (the root schema, the
  // instance path, and the caches that would otherwise have to be re-seeded
  // per branch); each caller does the same job by hand in its own loop.
  SchemaValidator makeFork() const {
    SchemaValidator sub;
    sub.rootSchema_ = rootSchema_;
    sub.path_ = path_;
    sub.refStack_ = refStack_;
    sub.patternCache_ = patternCache_;
    sub.shapeCache_ = shapeCache_;
    sub.refCache_ = refCache_;
    sub.nodeDepth_ = nodeDepth_; // the depth budget continues across forks
    return sub;
  }

  // Stores the message for an escaping std::exception and reports failure. The
  // nested guard covers the handler itself: building the message allocates, so a
  // bad_alloc caught by validate() could otherwise escape again from in here and
  // break the very contract the fence exists to keep.
  bool failInternal(const std::string& msg) noexcept {
    try {
      error_ = msg;
    } catch (...) {
      // Leave whatever error_ holds rather than propagating a second throw.
    }
    shapeFailed_ = true;
    return false;
  }

  // A schema value is either a boolean (true/false schema) or an object.
  static bool isSchema(const asvJSONValue* v) {
    return v && (v->type == asvJSONValue::OBJECT || v->type == asvJSONValue::BOOL_VAL);
  }

  void setError(const std::string& msg) {
    if (!error_.empty()) return;
    error_ = path_.empty() ? msg : (path_ + ": " + msg);
  }

  void pushPath(const std::string& seg) {
    // RFC 6901 JSON Pointer: ~ -> ~0, / -> ~1. Both must always be escaped
    // regardless of other characters present; `"` has no special meaning in
    // JSON Pointer tokens so it is emitted as-is.
    std::string escaped;
    escaped.reserve(seg.size() + 2);
    for (char c : seg) {
      if (c == '~') escaped += "~0";
      else if (c == '/') escaped += "~1";
      else escaped += c;
    }
    path_ += "/" + escaped;
  }

  void pushIndexPath(size_t idx) {
    path_ += "/" + std::to_string(idx);
  }


  static const asvJSONValue* schemaGet(const asvJSONValue* s, const std::string& key) {
    if (!s || s->type != asvJSONValue::OBJECT || !s->obj) return nullptr;
    auto it = s->obj->find(key);
    return it != s->obj->end() ? it->second.get() : nullptr;
  }

  static bool schemaHas(const asvJSONValue* s, const std::string& key) {
    return schemaGet(s, key) != nullptr;
  }

  static bool isBoolTrue(const asvJSONValue* s) {
    return s && s->type == asvJSONValue::BOOL_VAL && s->flag;
  }

  static bool isBoolFalse(const asvJSONValue* s) {
    return s && s->type == asvJSONValue::BOOL_VAL && !s->flag;
  }

  // Percent-decodes a single URI-fragment token (%XX -> byte). Bare '%' and
  // malformed escapes are left as-is.
  static std::string percentDecode(std::string_view seg) {
    std::string out;
    out.reserve(seg.size());
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    for (size_t i = 0; i < seg.size(); i++) {
      if (seg[i] == '%' && i + 2 < seg.size()) {
        int hi = hex(seg[i + 1]);
        int lo = hex(seg[i + 2]);
        if (hi >= 0 && lo >= 0) { out += static_cast<char>((hi << 4) | lo); i += 2; }
        else out += seg[i];
      } else {
        out += seg[i];
      }
    }
    return out;
  }

  // Decodes one JSON Pointer reference token: URI percent-escaping first, then
  // the ~0 (-> ~) / ~1 (-> /) escapes.
  static std::string decodePointerToken(std::string_view seg) {
    std::string decoded = percentDecode(seg);
    std::string out;
    out.reserve(decoded.size());
    for (size_t i = 0; i < decoded.size(); i++) {
      if (decoded[i] == '~' && i + 1 < decoded.size()) {
        if (decoded[i + 1] == '0') { out += '~'; i++; }
        else if (decoded[i + 1] == '1') { out += '/'; i++; }
        else out += decoded[i];
      } else {
        out += decoded[i];
      }
    }
    return out;
  }

  // RFC 6901 array index: %x30 / ( %x31-39 *(%x30-39) ) -- digits only, and no
  // leading zeros, so "0" is valid but "01", "-1" and "" are not. Accumulates with
  // an early bail on the array size, which also makes the arithmetic overflow-safe
  // (std::stoul would throw std::out_of_range on a long token).
  static bool parseArrayIndex(std::string_view tok, size_t size, size_t& idx) {
    if (tok.empty() || (tok.size() > 1 && tok[0] == '0')) return false;
    size_t n = 0;
    for (char c : tok) {
      if (c < '0' || c > '9') return false;
      // Guard against overflow before the multiplication: if n > (SIZE_MAX - d)/10
      // then n*10+d would wrap. The size bound makes this unreachable in practice,
      // but the check is cheap and closes the theoretical gap.
      int d = static_cast<int>(c - '0');
      if (n > (SIZE_MAX - static_cast<size_t>(d)) / 10) return false;
      n = n * 10 + static_cast<size_t>(d);
      if (n >= size) return false; // any further digit only grows it
    }
    idx = n;
    return true;
  }

  const asvJSONValue* resolveRef(std::string_view ref) const {
    if (ref.empty() || ref[0] != '#') return nullptr;
    ref.remove_prefix(1);
    const std::string cacheKey(ref.data(), ref.size());
    if (refCache_) {
      auto ci = refCache_->find(cacheKey);
      if (ci != refCache_->end()) return ci->second.failed ? nullptr : ci->second.target;
    }
    const asvJSONValue* cur = nullptr;
    bool failed = true;
    if (ref.empty()) {
      cur = rootSchema_;       // "#" -- whole-schema identity ref
      failed = false;
    } else if (ref[0] == '/') {
      ref.remove_prefix(1);
      cur = rootSchema_;
      failed = false;
      // JSON Pointer reference tokens are the pieces between '/' (the first may
      // be an empty string, correctly resolving to the object key ""). An OBJECT
      // maps the token to a member; an ARRAY indexes it, so pointers into
      // subschema arrays ("#/allOf/0", "#/anyOf/1") resolve as well.
      while (true) {
        auto slash = ref.find('/');
        std::string_view seg = (slash == std::string_view::npos) ? ref : ref.substr(0, slash);
        if (!cur) { failed = true; break; }
        if (cur->type == asvJSONValue::OBJECT && cur->obj) {
          auto it = cur->obj->find(decodePointerToken(seg));
          if (it == cur->obj->end()) { failed = true; break; }
          cur = it->second.get();
        } else if (cur->type == asvJSONValue::ARRAY && cur->arr) {
          size_t idx = 0;
          if (!parseArrayIndex(decodePointerToken(seg), cur->arr->size(), idx)) { failed = true; break; }
          cur = (*cur->arr)[idx].get();
        } else { failed = true; break; }
        if (slash == std::string_view::npos) break;
        ref.remove_prefix(slash + 1);
      }
    }
    if (refCache_) (*refCache_)[cacheKey] = {cur, failed};
    return failed ? nullptr : cur;
  }

  // Exact value equality (draft-07 `const`/`enum`/`uniqueItems` semantics).
  // `depth` bounds the lockstep walk: past MAX_VALUE_COMPARE_DEPTH the answer
  // is "not equal" (see the constant). Callers from the parser side never get
  // there - both operands nest at most MAX_NESTING_DEPTH deep.
  static bool valuesEqual(const asvJSONValue* a, const asvJSONValue* b, int depth = 0) {
    if (!a || !b) return a == b;
    if (depth > MAX_VALUE_COMPARE_DEPTH) return false;
    if (a->type != b->type) {
      if ((a->type == asvJSONValue::INT || a->type == asvJSONValue::DOUBLE) &&
          (b->type == asvJSONValue::INT || b->type == asvJSONValue::DOUBLE)) {
        // NaN would otherwise fall into the INT/DOUBLE mix and compareNumbers
        // (via compareIntDouble) maps it to "equal" -- the whole point of the
        // `if (b != b) return 0;` ordering quirk. An IEEE NaN is unordered and
        // not equal to any number, itself included, so reject it here instead.
        if ((a->type == asvJSONValue::DOUBLE && a->dbl != a->dbl) ||
            (b->type == asvJSONValue::DOUBLE && b->dbl != b->dbl))
          return false;
        return compareNumbers(a, b) == 0;
      }
      return false;
    }
    switch (a->type) {
      case asvJSONValue::NULL_VAL: return true;
      case asvJSONValue::BOOL_VAL: return a->flag == b->flag;
      case asvJSONValue::INT: return a->num == b->num;
      // Same-type DOUBLE is handled by the switch; NaN != NaN is already the
      // IEEE result of the == below, so no extra guard needed here.
      case asvJSONValue::DOUBLE: return a->dbl == b->dbl;
      case asvJSONValue::STRING: return a->str_data == b->str_data;
      case asvJSONValue::DATETIME: return a->timestamp == b->timestamp &&
                                          a->datetime_ms == b->datetime_ms &&
                                          a->tz_offset_min == b->tz_offset_min;
      case asvJSONValue::BINARY: return a->bin_data == b->bin_data;
      case asvJSONValue::OBJECTID: return a->str_data == b->str_data;
      case asvJSONValue::REGEX: return a->str_data == b->str_data;
      case asvJSONValue::TIMESTAMP: return a->num == b->num;
      case asvJSONValue::EXTENSION: return a->ext_type == b->ext_type && a->bin_data == b->bin_data;
      case asvJSONValue::CUSTOM_TAG:
        if (a->str_data != b->str_data) return false;
        if (static_cast<bool>(a->custom_value) != static_cast<bool>(b->custom_value)) return false;
        if (a->custom_value) return valuesEqual(a->custom_value.get(), b->custom_value.get(), depth + 1);
        return true;
      case asvJSONValue::ARRAY: {
        if (!a->arr || !b->arr) return a->arr == b->arr;
        if (a->arr->size() != b->arr->size()) return false;
        for (size_t i = 0; i < a->arr->size(); i++)
          if (!valuesEqual((*a->arr)[i].get(), (*b->arr)[i].get(), depth + 1)) return false;
        return true;
      }
      case asvJSONValue::OBJECT: {
        if (!a->obj || !b->obj) return a->obj == b->obj;
        if (a->obj->size() != b->obj->size()) return false;
        for (auto& kv : *a->obj) {
          auto it = b->obj->find(kv.first);
          if (it == b->obj->end()) return false;
          if (!valuesEqual(kv.second.get(), it->second.get(), depth + 1)) return false;
        }
        return true;
      }
      default: return false;
    }
  }

  // Bucket key for the two uniqueness checks (`enum` entries, `uniqueItems`
  // items). It is NOT an equality test -- valuesEqual() stays the exact
  // relation -- only a way to stop comparing every pair: an O(n^2) scan over
  // valuesEqual is a CPU-DoS vector on an attacker-supplied schema (50k enum
  // entries measured 30 s of pure shape checking, 200k measured 494 s).
  // The one property that matters: values valuesEqual() calls equal MUST hash
  // the same, so a bucket can never hide a duplicate. Numbers are therefore
  // canonicalised (INT 1 and DOUBLE 1.0 share one key, which is exactly the
  // documented "same enum entry" rule), object members are folded
  // commutatively because the object compare is order-insensitive, array
  // elements positionally. A collision costs one extra exact compare, never a
  // wrong verdict. Recursion is capped like valuesEqual(); a capped subtree
  // hashes to a fixed marker, which only merges buckets.
  static uint64_t identityHash(const asvJSONValue* v, int depth) {
    if (!v) return 0;
    if (depth > MAX_VALUE_COMPARE_DEPTH) return 0xC0FFEE1ull;
    uint64_t h = 1469598103934665603ull; // FNV-1a, 64-bit
    auto mix = [&h](uint64_t x) {
      for (int i = 0; i < 8; i++) { h ^= (x >> (i * 8)) & 0xFFull; h *= 1099511628211ull; }
    };
    auto fnv = [](const auto& s) { // std::string or std::vector<uint8_t>
      uint64_t x = 1469598103934665603ull;
      for (unsigned char c : s) { x ^= c; x *= 1099511628211ull; }
      return x;
    };
    if (v->type == asvJSONValue::INT || v->type == asvJSONValue::DOUBLE) {
      // One marker for both spellings, then the canonical value: an exactly
      // integral DOUBLE must hash like the INT holding the same number.
      mix(0x4E554Dull); // "NUM"
      if (v->type == asvJSONValue::INT) { mix(static_cast<uint64_t>(v->num)); return h; }
      double d = v->dbl;
      if (d != d) mix(0x4E614Eull); // NaN: unordered, never equal to anything
      else if (d == 0.0) mix(0);    // folds +0.0 and -0.0
      else if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
        int64_t i = static_cast<int64_t>(d);
        if (static_cast<double>(i) == d) { mix(static_cast<uint64_t>(i)); return h; }
        uint64_t bits = 0; // in range but not exactly integral
        std::memcpy(&bits, &d, sizeof(bits));
        mix(bits);
      } else {
        uint64_t bits = 0; // outside int64: exact bit pattern
        std::memcpy(&bits, &d, sizeof(bits));
        mix(bits);
      }
      return h;
    }
    mix(static_cast<uint64_t>(v->type));
    switch (v->type) {
      case asvJSONValue::NULL_VAL: break;
      case asvJSONValue::BOOL_VAL: mix(v->flag ? 1ull : 0ull); break;
      case asvJSONValue::STRING:
      case asvJSONValue::OBJECTID:
      case asvJSONValue::REGEX: mix(fnv(v->str_data)); break;
      case asvJSONValue::TIMESTAMP: mix(static_cast<uint64_t>(v->num)); break;
      case asvJSONValue::DATETIME:
        mix(static_cast<uint64_t>(static_cast<int64_t>(v->timestamp)));
        mix(static_cast<uint64_t>(v->datetime_ms));
        mix(static_cast<uint64_t>(v->tz_offset_min));
        break;
      case asvJSONValue::BINARY: mix(fnv(v->bin_data)); break;
      case asvJSONValue::EXTENSION:
        mix(static_cast<uint64_t>(static_cast<int64_t>(v->ext_type)));
        mix(fnv(v->bin_data));
        break;
      case asvJSONValue::CUSTOM_TAG:
        mix(fnv(v->str_data));
        mix(identityHash(v->custom_value.get(), depth + 1));
        break;
      case asvJSONValue::ARRAY: {
        if (!v->arr) break;
        mix(v->arr->size());
        for (const auto& e : *v->arr) mix(identityHash(e.get(), depth + 1)); // positional
        break;
      }
      case asvJSONValue::OBJECT: {
        if (!v->obj) break;
        mix(v->obj->size());
        for (const auto& kv : *v->obj) {
          // Commutative fold: valuesEqual() matches members by name, so the
          // key order of the map must not change the key.
          uint64_t k = fnv(kv.first);
          uint64_t s = identityHash(kv.second.get(), depth + 1);
          s ^= s >> 33; s *= 0xff51afd7ed558ccdull; s ^= s >> 29;
          h += k + s;
        }
        break;
      }
      default: break;
    }
    return h;
  }

  // Shared driver for the two uniqueness checks (`enum` entries, `uniqueItems`
  // items). Buckets by identityHash and confirms inside the bucket with
  // valuesEqual, so the verdict is exactly the pairwise O(n^2) scan's, at
  // ~O(n) when the values are distinct. Reports the first duplicate pair.
  static bool findDuplicatePair(const std::vector<std::unique_ptr<asvJSONValue>>& v,
                                size_t& firstIdx, size_t& secondIdx) {
    static constexpr size_t kNil = static_cast<size_t>(-1);
    const size_t n = v.size();
    std::unordered_map<uint64_t, size_t> head; // bucket -> last index in it
    head.reserve(n);
    std::vector<size_t> chain(n, kNil);        // threads the rest of a bucket
    for (size_t i = 0; i < n; i++) {
      uint64_t key = identityHash(v[i].get(), 0);
      auto bucket = head.find(key);
      for (size_t k = bucket == head.end() ? kNil : bucket->second; k != kNil; k = chain[k])
        if (valuesEqual(v[k].get(), v[i].get())) { firstIdx = k; secondIdx = i; return true; }
      if (bucket == head.end()) head.emplace(key, i);
      else { chain[i] = bucket->second; bucket->second = i; }
    }
    return false;
  }

  static std::string valueTypeName(const asvJSONValue* v) {
    if (!v) return "null";
    switch (v->type) {
      case asvJSONValue::NULL_VAL: return "null";
      case asvJSONValue::BOOL_VAL: return "boolean";
      case asvJSONValue::INT: return "integer";
      case asvJSONValue::DOUBLE: return "number";
      case asvJSONValue::STRING: return "string";
      case asvJSONValue::ARRAY: return "array";
      case asvJSONValue::OBJECT: return "object";
      // Special types map to their JSON-serializable counterpart so error
      // messages are informative rather than "unknown".
      case asvJSONValue::DATETIME: return "string";
      case asvJSONValue::TIMESTAMP: return "integer";
      case asvJSONValue::BINARY: return "string";
      case asvJSONValue::OBJECTID: return "object";
      case asvJSONValue::REGEX: return "object";
      case asvJSONValue::EXTENSION: return "object";
      case asvJSONValue::CUSTOM_TAG: return "object";
      default: return "unknown";
    }
  }

  static bool isNumberType(const asvJSONValue* v) {
    return v && (v->type == asvJSONValue::INT || v->type == asvJSONValue::DOUBLE);
  }

  static double getNumberValue(const asvJSONValue* v) {
    if (!v) return 0;
    if (v->type == asvJSONValue::INT) return static_cast<double>(v->num);
    if (v->type == asvJSONValue::DOUBLE) return v->dbl;
    return 0;
  }

  // Error-message rendering of a number: int64 is printed exactly (the double
  // conversion above would corrupt values like 9223372036854775807 in the
  // diagnostic); doubles go through the same formatter as JSON serialization so
  // the message reads like the document does. std::to_string(double) would print
  // six fixed decimals ("3.140000") and, worse, expand a large magnitude into
  // every digit it has -- a 1e300 bound produces a 300-digit error message.
  static std::string numberToStr(const asvJSONValue* v) {
    if (!v) return "0";
    if (v->type == asvJSONValue::INT) return std::to_string(v->num);
    std::string out;
    fmtDoubleVal(getNumberValue(v), out);
    return out;
  }

  // Exact cross-type int64/double ordering. Avoids the precision loss of
  // converting a wide int64 to double (e.g. 2^63-1 vs the double 2^63), which
  // would otherwise make distinct numbers compare equal.
  static int compareIntDouble(int64_t a, double b) {
    if (b != b) return 0; // NaN: unordered, treat as equal
    if (b == std::numeric_limits<double>::infinity()) return -1;
    if (b == -std::numeric_limits<double>::infinity()) return 1;
    if (!(b >= -9223372036854775808.0 && b < 9223372036854775808.0)) return b < 0 ? 1 : -1;
    int64_t ib = static_cast<int64_t>(b);
    if (a < ib) return -1;
    if (a > ib) return 1;
    if (static_cast<double>(ib) == b) return 0;  // b is exactly a
    return b < 0 ? 1 : -1;                          // a is trunc(b), fractional b is strictly larger/smaller
  }

  // Exact numeric ordering for two JSON numbers; INT/INT pairs never go
  // through double.
  static int compareNumbers(const asvJSONValue* a, const asvJSONValue* b) {
    if (a->type == asvJSONValue::INT && b->type == asvJSONValue::INT) {
      if (a->num < b->num) return -1;
      if (a->num > b->num) return 1;
      return 0;
    }
    if (a->type == asvJSONValue::INT) return compareIntDouble(a->num, b->dbl);
    if (b->type == asvJSONValue::INT) return -compareIntDouble(b->num, a->dbl);
    if (a->dbl < b->dbl) return -1;
    if (a->dbl > b->dbl) return 1;
    return 0;
  }

  // Unicode code-point count of a UTF-8 string, robust for malformed input:
  // stray continuation bytes count as one character each, and a lead byte whose
  // sequence runs past the end of the string is counted as one character with
  // len forced to 1, so the scan resumes on the next byte rather than skipping
  // it -- a truncated tail may therefore count as more than one character
  // ("\xE0\x80" counts 2: the lead, then the stray continuation). Only the
  // count matters here, not the boundaries.
  static int64_t utf8Length(const std::string& s) {
    int64_t n = 0;
    size_t i = 0;
    while (i < s.size()) {
      auto c = static_cast<unsigned char>(s[i]);
      if (c < 0x80) { n++; i++; }
      else if (c < 0xC0) { n++; i++; } // stray continuation byte
      else {
        size_t len = (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        if (i + len > s.size()) len = 1; // truncated tail: count once, stop
        n++;
        i += len;
      }
    }
    return n;
  }

  bool schemaPassesTypeCheck(const asvJSONValue* value, const asvJSONValue* typeNode) const {
    if (!typeNode) return true;
    if (typeNode->type == asvJSONValue::STRING) {
      return checkSingleType(value, typeNode->str_data);
    }
    if (typeNode->type == asvJSONValue::ARRAY && typeNode->arr) {
      for (const auto& t : *typeNode->arr) {
        if (t->type == asvJSONValue::STRING && checkSingleType(value, t->str_data))
          return true;
      }
      return false;
    }
    return true;
  }

  static bool checkSingleType(const asvJSONValue* v, std::string_view t) {
    // Custom types map to their closest JSON Schema analogue per the asvJSON++
    // convention (see core.hpp type documentation). This keeps schema validation
    // usable without requiring a dedicated "binary"/"datetime" keyword.
    switch (v->type) {
      case asvJSONValue::NULL_VAL: return t == "null";
      case asvJSONValue::BOOL_VAL: return t == "boolean";
      case asvJSONValue::INT: return t == "integer" || t == "number";
      case asvJSONValue::DOUBLE:
        if (t == "number") return true;
        if (t == "integer") return std::isfinite(v->dbl) && std::trunc(v->dbl) == v->dbl;
        return false;
      case asvJSONValue::STRING: return t == "string";
      case asvJSONValue::ARRAY: return t == "array";
      case asvJSONValue::OBJECT: return t == "object";
      // Every custom type below emits its own JSON shape, so a schema written
      // against the serialized form has to accept that shape. DATETIME is the
      // one type that serializes to a plain JSON string; the rest serialize to
      // the extended-JSON conventions ({$oid}, {$regex}, {$customTag},
      // {$binary}, {$timestamp}) and therefore look like objects to anything
      // reading the text -- including a user reading their own output and
      // writing {"type":"object"} for it.
      //
      // Both spellings are accepted rather than only the serialized one. Picking
      // just the serialized shape would retroactively reject schemas that pass
      // today ({"type":"string"} for an ObjectId, {"type":"integer"} for a
      // Timestamp, and the pure-JSON mirror form {"type":["string","object"]}
      // that a shared schema already has to write to cover BINARY); accepting
      // both only widens the set of instances a schema admits.
      case asvJSONValue::DATETIME: return t == "string";
      case asvJSONValue::OBJECTID:
      case asvJSONValue::REGEX:
      case asvJSONValue::CUSTOM_TAG: return t == "string" || t == "object";
      // Timestamp stores epoch seconds as int64 ({"$timestamp":{"t":N}} in its
      // serialized form).
      case asvJSONValue::TIMESTAMP: return t == "integer" || t == "number" || t == "object";
      // Binary data can appear as a base64 string or (when decoded) an object;
      // accept both for schema compatibility.
      case asvJSONValue::BINARY: return t == "string" || t == "object";
      // Extension payload has no single canonical JSON Schema type; it
      // serializes as a $binary object carrying its ext_type in subType.
      case asvJSONValue::EXTENSION: return t == "object";
      default: return false;
    }
  }

  // draft-07 6.1.1: "The value of this keyword MUST be either a string or an
  // array. If it is an array, elements of the array MUST be strings." and the
  // strings MUST be one of the seven type names the spec defines. An unknown
  // name ("strng") can never match any instance, so rather than let it reject
  // every value with a baffling "Expected type 'strng', got 'integer'" it is
  // reported as the schema mistake it is.
  static bool isKnownTypeName(std::string_view t) {
    return t == "null" || t == "boolean" || t == "object" || t == "array" ||
           t == "number" || t == "string" || t == "integer";
  }

  // Returns a compiled regex for `pat` from the validator-wide cache. On a
  // bad pattern sets valid=false (the pattern is remembered so later lookups
  // are fast) and returns nullptr without touching error_.
  const std::regex* getCompiled(const std::string& pat, bool& valid) {
    if (!patternCache_) patternCache_ = std::make_shared<PatternCache>();
    auto it = patternCache_->ok.find(pat);
    if (it != patternCache_->ok.end()) { valid = true; return &it->second; }
    if (patternCache_->bad.count(pat) != 0) { valid = false; return nullptr; }
    try {
      auto res = patternCache_->ok.emplace(pat, std::regex(pat));
      valid = true;
      return &res.first->second;
    } catch (const std::regex_error&) {
      patternCache_->bad.insert(pat);
      valid = false;
      return nullptr;
    } catch (const std::bad_alloc&) {
      // A pathological pattern (e.g. an exploding ECMAScript construct) can
      // exhaust memory while std::regex builds its NFA table. Remember it as
      // bad so we do not retry, and fail closed.
      patternCache_->bad.insert(pat);
      valid = false;
      return nullptr;
    }
  }

  // Rejects keyword values whose shape is invalid per draft-07 (e.g. a
  // non-array `enum`, a negative `minLength`, a non-numeric `minimum`).
  // Malformed schemas fail validation instead of being silently ignored.
  bool validateKeywordShapes(const asvJSONValue* schema) {
    if (auto r = schemaGet(schema, "$ref")) {
      if (r->type != asvJSONValue::STRING) {
        setError("Invalid schema: '$ref' must be a string");
        return false;
      }
    }
    if (auto t = schemaGet(schema, "type")) {
      bool okShape = t->type == asvJSONValue::STRING;
      if (t->type == asvJSONValue::ARRAY && t->arr) {
        okShape = true;
        for (const auto& e : *t->arr)
          if (e->type != asvJSONValue::STRING) { okShape = false; break; }
      }
      if (!okShape) {
        setError("Invalid schema: 'type' must be a string or an array of strings");
        return false;
      }
      if (t->type == asvJSONValue::STRING && !isKnownTypeName(t->str_data)) {
        setError("Invalid schema: unknown type name '" + t->str_data + "'");
        return false;
      }
      if (t->type == asvJSONValue::ARRAY && t->arr) {
        for (const auto& e : *t->arr)
          if (!isKnownTypeName(e->str_data)) {
            setError("Invalid schema: unknown type name '" + e->str_data + "'");
            return false;
          }
      }
      // draft-07: the array "SHOULD be unique" and, as with `enum`, an empty
      // list matches nothing at all -- it also produces the useless diagnostic
      // "Expected type [], got 'string'". Same deliberate strictness as the
      // empty `enum` above: a schema that can never validate anything is a
      // mistake worth reporting instead of silently rejecting every instance.
      if (t->type == asvJSONValue::ARRAY && t->arr) {
        if (t->arr->empty()) {
          setError("Invalid schema: 'type' should have at least one element");
          return false;
        }
        // O(n) hash dedup (the same shape `required` uses below): the pairwise
        // scan was quadratic in an attacker-supplied array, and every entry
        // clears the known-name check, so {"type":["string", ... x 200k]} was
        // minutes of pure shape checking.
        std::unordered_set<std::string> seen;
        seen.reserve(t->arr->size());
        bool dupType = false;
        for (const auto& e : *t->arr)
          if (!seen.insert(e->str_data).second) { dupType = true; break; }
        if (dupType) {
          setError("Invalid schema: 'type' entries must be unique");
          return false;
        }
      }
    }
    if (auto e = schemaGet(schema, "enum")) {
      if (e->type != asvJSONValue::ARRAY || !e->arr) {
        setError("Invalid schema: 'enum' must be an array");
        return false;
      }
      // draft-07: "The value of this keyword MUST be an array. This array SHOULD
      // have at least one element." An empty enum matches nothing at all, so it
      // rejects every instance -- a schema that can never validate anything is
      // almost certainly a mistake. Note this is a SHOULD, not a MUST: the
      // instance-level rejection ("Value not in enum") is already correct on its
      // own, and this is a deliberate extra strictness for a degenerate schema.
      if (e->arr->empty()) {
        setError("Invalid schema: 'enum' should have at least one element");
        return false;
      }
      // draft-07: "Elements in the array SHOULD be unique" -- same deliberate
      // strictness as the `type` array and `required` above. Duplicate entries
      // are redundant with equal values (1 and 1.0 compare equal via
      // valuesEqual, the same equality the membership check uses) and hide a
      // copy-paste slip; a schema that repeats itself is rejected.
      // Bucketed rather than pairwise: the O(n^2) scan over valuesEqual was a
      // CPU-DoS on a schema the parser accepts (50k entries measured 30 s,
      // 200k measured 494 s of shape checking alone).
      size_t dupA = 0, dupB = 0;
      if (findDuplicatePair(*e->arr, dupA, dupB)) {
        setError("Invalid schema: 'enum' entries must be unique");
        return false;
      }
    }
    if (auto r = schemaGet(schema, "required")) {
      if (r->type != asvJSONValue::ARRAY || !r->arr) {
        setError("Invalid schema: 'required' must be an array of strings");
        return false;
      }
      for (const auto& e : *r->arr)
        if (e->type != asvJSONValue::STRING) {
          setError("Invalid schema: 'required' entries must be strings");
          return false;
        }
      // draft-07: the array "SHOULD be unique". Repeating a name is harmless
      // (the requirement it states is still checked once), but it is never
      // written on purpose and hides a copy-paste error in the schema.
      // An empty array stays legal -- it is a no-op, unlike an empty `enum`.
      // O(n) hash-based dedup check instead of O(n^2).
      {
        std::unordered_set<std::string> seen;
        seen.reserve(r->arr->size());
        bool dup = false;
        for (const auto& e : *r->arr) {
          if (!seen.insert(e->str_data).second) { dup = true; break; }
        }
        if (dup) {
          setError("Invalid schema: 'required' entries must be unique");
          return false;
        }
      }
    }
    if (auto u = schemaGet(schema, "uniqueItems")) {
      if (u->type != asvJSONValue::BOOL_VAL) {
        setError("Invalid schema: 'uniqueItems' must be a boolean");
        return false;
      }
    }
    static const char* const nonNegInts[] = {"minLength", "maxLength", "minItems", "maxItems", "minProperties", "maxProperties"};
    for (const char* k : nonNegInts) {
      if (auto n = schemaGet(schema, k)) {
        if (n->type != asvJSONValue::INT || n->num < 0) {
          setError(std::string("Invalid schema: '") + k + "' must be a non-negative integer");
          return false;
        }
      }
    }
    static const char* const numbers[] = {"minimum", "maximum", "multipleOf"};
    for (const char* k : numbers) {
      if (auto n = schemaGet(schema, k)) {
        if (!isNumberType(n)) {
          setError(std::string("Invalid schema: '") + k + "' must be a number");
          return false;
        }
      }
    }
    // exclusiveMinimum/exclusiveMaximum take the draft-07 numeric form or the
    // draft-04 boolean (legacy) form, which is a modifier of the plain bound it
    // is named after.
    struct ExclusiveForm { const char* keyword; const char* partner; };
    static const ExclusiveForm exclusives[] = {
      {"exclusiveMinimum", "minimum"}, {"exclusiveMaximum", "maximum"}};
    for (const auto& ex : exclusives) {
      if (auto n = schemaGet(schema, ex.keyword)) {
        if (n->type != asvJSONValue::BOOL_VAL && !isNumberType(n)) {
          setError(std::string("Invalid schema: '") + ex.keyword + "' must be a number or boolean");
          return false;
        }
        // The boolean form needs its bound beside it: `exclusiveMinimum: true`
        // tightens `minimum`, so on its own there is nothing to tighten and the
        // strictness the author asked for would pass unnoticed. Same reason an
        // empty `enum` is rejected.
        if (n->type == asvJSONValue::BOOL_VAL && !schemaGet(schema, ex.partner)) {
          setError(std::string("Invalid schema: '") + ex.keyword + "' requires '" +
                   ex.partner + "'");
          return false;
        }
      }
    }
    if (auto m = schemaGet(schema, "multipleOf")) {
      if (isNumberType(m) && getNumberValue(m) <= 0) {
        setError("Invalid schema: 'multipleOf' must be greater than 0");
        return false;
      }
    }
    if (auto p = schemaGet(schema, "pattern")) {
      if (p->type != asvJSONValue::STRING) {
        setError("Invalid schema: 'pattern' must be a string");
        return false;
      }
      // Compile here, at shape-check time, so a bad regex is a malformed schema
      // reported like any other (and cached once per schema node) instead of
      // failing lazily at first application -- a lazy failure inside a fork
      // keyword (anyOf/oneOf/not/if/contains) would read as "branch did not
      // match" and be swallowed by adoptShapeError. The runtime compile below
      // still guards the regex_search itself.
      bool validPattern = false;
      getCompiled(p->str_data, validPattern);
      if (!validPattern) {
        setError("Invalid schema: 'pattern' is not a valid regex: " + p->str_data);
        return false;
      }
    }

    // allOf / anyOf / oneOf -- non-empty arrays of schemas.
    static const char* const ofArrays[] = {"allOf", "anyOf", "oneOf"};
    for (const char* k : ofArrays) {
      if (auto n = schemaGet(schema, k)) {
        if (n->type != asvJSONValue::ARRAY || !n->arr || n->arr->empty()) {
          setError(std::string("Invalid schema: '") + k + "' must be a non-empty array of schemas");
          return false;
        }
        for (const auto& e : *n->arr)
          if (!isSchema(e.get())) {
            setError(std::string("Invalid schema: '") + k + "' entries must be schemas");
            return false;
          }
      }
    }
    // not / if / then / else / contains / propertyNames -- single schemas.
    static const char* const singleSchemas[] = {"not", "if", "then", "else", "contains", "propertyNames"};
    for (const char* k : singleSchemas) {
      if (auto n = schemaGet(schema, k)) {
        if (!isSchema(n)) {
          setError(std::string("Invalid schema: '") + k + "' must be a schema");
          return false;
        }
      }
    }
    // items -- a schema, or an (possibly empty) array of schemas (tuple form).
    if (auto it = schemaGet(schema, "items")) {
      bool ok = isSchema(it);
      if (it->type == asvJSONValue::ARRAY && it->arr) {
        ok = true;
        for (const auto& e : *it->arr)
          if (!isSchema(e.get())) { ok = false; break; }
      }
      if (!ok) {
        setError("Invalid schema: 'items' must be a schema or an array of schemas");
        return false;
      }
    }
    if (auto ai = schemaGet(schema, "additionalItems")) {
      if (!isSchema(ai)) {
        setError("Invalid schema: 'additionalItems' must be a schema or boolean");
        return false;
      }
    }
    if (auto ap = schemaGet(schema, "additionalProperties")) {
      if (!isSchema(ap)) {
        setError("Invalid schema: 'additionalProperties' must be a schema or boolean");
        return false;
      }
    }
    // properties / patternProperties / definitions / $defs -- objects of schemas.
    // ($defs is 2019-09+, aliased to draft-07 `definitions` for $ref targets.)
    static const char* const schemaMaps[] = {"properties", "patternProperties", "definitions", "$defs"};
    for (const char* k : schemaMaps) {
      if (auto n = schemaGet(schema, k)) {
        if (n->type != asvJSONValue::OBJECT || !n->obj) {
          setError(std::string("Invalid schema: '") + k + "' must be an object");
          return false;
        }
        for (const auto& [key, val] : *n->obj) {
          if (!isSchema(val.get())) {
            setError(std::string("Invalid schema: '") + k + "' values must be schemas");
            return false;
          }
        }
      }
    }
    // patternProperties KEYS are regexes. Compile them here too (see the
    // `pattern` case): an invalid key is a malformed schema, reported at
    // shape-check time even when no object instance ever reaches this node.
    if (auto pp = schemaGet(schema, "patternProperties")) {
      if (pp->type == asvJSONValue::OBJECT && pp->obj) {
        for (const auto& [key, _] : *pp->obj) {
          bool validPattern = false;
          getCompiled(key, validPattern);
          if (!validPattern) {
            setError("Invalid schema: 'patternProperties' key is not a valid regex: " + key);
            return false;
          }
        }
      }
    }
    // dependencies -- object; each value is a schema or an array of strings.
    if (auto d = schemaGet(schema, "dependencies")) {
      if (d->type != asvJSONValue::OBJECT) {
        setError("Invalid schema: 'dependencies' must be an object");
        return false;
      }
      if (d->obj) {
        for (const auto& [k, v] : *d->obj) {
          bool ok = isSchema(v.get());
          if (v->type == asvJSONValue::ARRAY && v->arr) {
            ok = true;
            for (const auto& e : *v->arr)
              if (e->type != asvJSONValue::STRING) { ok = false; break; }
          }
          if (!ok) {
            setError("Invalid schema: 'dependencies' entries must be schemas or arrays of strings");
            return false;
          }
        }
      }
    }
    // dependentRequired -- object of arrays of strings.
    if (auto dr = schemaGet(schema, "dependentRequired")) {
      if (dr->type != asvJSONValue::OBJECT) {
        setError("Invalid schema: 'dependentRequired' must be an object");
        return false;
      }
      if (dr->obj) {
        for (const auto& [k, v] : *dr->obj) {
          bool ok = v->type == asvJSONValue::ARRAY && v->arr;
          if (ok) {
            for (const auto& e : *v->arr)
              if (e->type != asvJSONValue::STRING) { ok = false; break; }
          }
          if (!ok) {
            setError("Invalid schema: 'dependentRequired' entries must be arrays of strings");
            return false;
          }
        }
      }
    }
    if (auto f = schemaGet(schema, "format")) {
      if (f->type != asvJSONValue::STRING) {
        setError("Invalid schema: 'format' must be a string");
        return false;
      }
    }
    return true;
  }

  // Cached wrapper around validateKeywordShapes: the shape of a schema object
  // is static, so each node's structural check runs once per validate() call
  // instead of once per value that passes through it. Since the cache is
  // shared with forked sub-validators, an anyOf/oneOf/not branch on the same
  // schema costs no revalidation. A cached failure is re-run only to
  // regenerate this validator's own error message (failures abort validation
  // and execute once, so the repetition is negligible).
  //
  // A false result also raises shapeFailed_, so a caller that treats the return
  // value as merely "the instance did not match" (anyOf/oneOf/not/if/contains)
  // can tell a malformed schema apart from a genuine mismatch.
  bool shapesOk(const asvJSONValue* schema) {
    if (shapeCache_) {
      auto it = shapeCache_->find(schema);
      if (it != shapeCache_->end()) {
        if (it->second) return true;
        (void)validateKeywordShapes(schema); // rebuild error_ for this validator
        shapeFailed_ = true;
        return false;
      }
    }
    bool ok = validateKeywordShapes(schema);
    if (shapeCache_) (*shapeCache_)[schema] = ok;
    if (!ok) shapeFailed_ = true;
    return ok;
  }

  bool validateNode(const asvJSONValue* value, const asvJSONValue* schema) {
    // Depth guard wrapper: every recursion edge (keyword nesting, $ref hop,
    // data descent) goes through here, and so do the forked sub-validators
    // (makeFork carries nodeDepth_ over). failInternal sets shapeFailed_:
    // an abandoned check is not an instance verdict.
    if (nodeDepth_ >= MAX_VALIDATION_DEPTH)
      return failInternal("Schema validation nesting too deep");
    ++nodeDepth_;
    const bool ok = validateNodeDeep(value, schema);
    --nodeDepth_;
    return ok;
  }

  bool validateNodeDeep(const asvJSONValue* value, const asvJSONValue* schema) {
    if (isBoolTrue(schema)) return true;
    if (isBoolFalse(schema)) { setError("False schema rejects all values"); return false; }
    if (!schema || schema->type != asvJSONValue::OBJECT) return true;

    if (auto refNode = schemaGet(schema, "$ref")) {
      if (refNode->type != asvJSONValue::STRING) {
        setError("Invalid schema: '$ref' must be a string");
        shapeFailed_ = true; // malformed schema, not an instance mismatch
        return false;
      }
      auto resolved = resolveRef(refNode->str_data);
      if (!resolved) {
        // A dangling local reference is a schema typo, not a verdict about the
        // instance (same class as $ref landing on a non-schema -- both are
        // shapeFailed_ so fork keywords abort on them instead of reading the
        // branch as "did not match").
        setError("Unresolved $ref: " + refNode->str_data);
        shapeFailed_ = true; // malformed schema, not an instance mismatch
        return false;
      }
      if (!isSchema(resolved)) {
        // $ref must land on a schema instance (boolean or object). A pointer
        // into bare data (`{"definitions":{"x":"bar"},"$ref":"#/definitions/x"}`)
        // is a malformed schema, and passing it through would silently validate
        // everything ("not an object -> true"), contradicting the header's
        // "malformed subschema is reported, not swallowed".
        setError("Invalid schema: '$ref' does not resolve to a schema: " + refNode->str_data);
        shapeFailed_ = true; // malformed schema, not an instance mismatch
        return false;
      }
      RefKey key{value, resolved};
      if (refStack_ && refStack_->count(key) != 0) {
        // Deliberately NOT shapeFailed_: a circular $ref re-applies the same
        // schema to the same instance with no progress, so it is an
        // unsatisfiable condition that matches nothing rather than a malformed
        // schema. Inside a fork it legitimately reads as "branch did not match"
        // (see the header for the full distinction).
        setError("Circular $ref detected at: " + refNode->str_data);
        return false;
      }
      if (refStack_) refStack_->insert(key);
      bool ok = validateNode(value, resolved);
      if (refStack_) refStack_->erase(key);
      return ok;
    }

    if (!shapesOk(schema)) return false;

    const asvJSONValue* typeNode = schemaGet(schema, "type");
    if (!schemaPassesTypeCheck(value, typeNode)) {
      std::string expected;
      if (typeNode->type == asvJSONValue::STRING) {
        expected = "'" + std::string(typeNode->str_data) + "'";
      } else if (typeNode->type == asvJSONValue::ARRAY && typeNode->arr) {
        expected = "[";
        for (size_t i = 0; i < typeNode->arr->size(); i++) {
          if (i > 0) expected += ", ";
          expected += "'" + std::string((*typeNode->arr)[i]->str_data) + "'";
        }
        expected += "]";
      }
      setError("Expected type " + expected + ", got '" + valueTypeName(value) + "'");
      return false;
    }

    if (schemaHas(schema, "const")) {
      auto constVal = schemaGet(schema, "const");
      if (!valuesEqual(value, constVal)) {
        setError("Value does not match const");
        return false;
      }
    }

    if (schemaHas(schema, "enum")) {
      auto enumNode = schemaGet(schema, "enum");
      if (enumNode && enumNode->type == asvJSONValue::ARRAY && enumNode->arr) {
        bool found = false;
        for (const auto& ev : *enumNode->arr) {
          if (valuesEqual(value, ev.get())) { found = true; break; }
        }
        if (!found) { setError("Value not in enum"); return false; }
      }
    }

    if (!validateComposition(value, schema)) return false;

    if (!validateString(value, schema)) return false;
    if (!validateNumber(value, schema)) return false;
    if (!validateArray(value, schema)) return false;
    if (!validateObject(value, schema)) return false;

    return true;
  }

  bool validateComposition(const asvJSONValue* value, const asvJSONValue* schema) {
    if (schemaHas(schema, "allOf")) {
      auto node = schemaGet(schema, "allOf");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        for (size_t i = 0; i < node->arr->size(); i++) {
          if (!validateNode(value, (*node->arr)[i].get())) return false;
        }
      }
    }

    if (schemaHas(schema, "anyOf")) {
      auto node = schemaGet(schema, "anyOf");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        bool anyMatched = false;
        for (const auto& sub : *node->arr) {
          SchemaValidator subVal = makeFork();
          if (subVal.validateNode(value, sub.get())) { anyMatched = true; break; }
          // A malformed branch is not "this branch did not match" -- abort,
          // otherwise the schema error disappears behind the anyOf verdict.
          if (adoptShapeError(subVal)) return false;
        }
        if (!anyMatched) {
          setError("Does not match any schema in anyOf");
          return false;
        }
        // fall through to oneOf / not / if — these are independent assertions
      }
    }

    if (schemaHas(schema, "oneOf")) {
      auto node = schemaGet(schema, "oneOf");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        int matches = 0;
        for (const auto& sub : *node->arr) {
          SchemaValidator subVal = makeFork();
          if (subVal.validateNode(value, sub.get())) matches++;
          if (adoptShapeError(subVal)) return false;
        }
        if (matches != 1) {
          setError(matches == 0 ? "Does not match any schema in oneOf" : "Matches more than one schema in oneOf");
          return false;
        }
      }
    }

    if (schemaHas(schema, "not")) {
      auto node = schemaGet(schema, "not");
      if (node) {
        SchemaValidator subVal = makeFork();
        if (subVal.validateNode(value, node)) {
          setError("Matches the 'not' schema");
          return false;
        }
        // The branch failing is the success condition here, so a malformed
        // subschema would otherwise negate into "accepts everything".
        if (adoptShapeError(subVal)) return false;
      }
    }

    if (schemaHas(schema, "if")) {
      auto ifNode = schemaGet(schema, "if");
      if (ifNode) {
        SchemaValidator ifVal = makeFork();
        bool cond = ifVal.validateNode(value, ifNode);
        // A malformed condition reads as a false one and would steer the
        // instance into the else branch as if the author had asked for that.
        if (adoptShapeError(ifVal)) return false;
        if (cond && schemaHas(schema, "then")) {
          return validateNode(value, schemaGet(schema, "then"));
        }
        if (!cond && schemaHas(schema, "else")) {
          return validateNode(value, schemaGet(schema, "else"));
        }
      }
    }

    return true;
  }

  bool validateString(const asvJSONValue* value, const asvJSONValue* schema) {
    if (value->type != asvJSONValue::STRING) return true;
    const std::string& s = value->str_data;

    if (schemaHas(schema, "minLength")) {
      auto node = schemaGet(schema, "minLength");
      if (node && node->type == asvJSONValue::INT) {
        int64_t actualLen = utf8Length(s);
        if (actualLen < node->num) {
          setError("String length " + std::to_string(actualLen) + " is less than minLength " + std::to_string(node->num));
          return false;
        }
      }
    }

    if (schemaHas(schema, "maxLength")) {
      auto node = schemaGet(schema, "maxLength");
      if (node && node->type == asvJSONValue::INT) {
        int64_t actualLen = utf8Length(s);
        if (actualLen > node->num) {
          setError("String length " + std::to_string(actualLen) + " is greater than maxLength " + std::to_string(node->num));
          return false;
        }
      }
    }

    if (schemaHas(schema, "pattern")) {
      auto node = schemaGet(schema, "pattern");
      if (node && node->type == asvJSONValue::STRING) {
        bool validPattern = false;
        const std::regex* re = getCompiled(node->str_data, validPattern);
        if (!validPattern) {
          setError("Invalid regex pattern '" + node->str_data + "'");
          return false;
        }
        if (re && !std::regex_search(s, *re)) {
          setError("String does not match pattern '" + node->str_data + "'");
          return false;
        }
      }
    }

    return true;
  }

  bool validateNumber(const asvJSONValue* value, const asvJSONValue* schema) {
    if (!isNumberType(value)) return true;

    // exclusiveMinimum/exclusiveMaximum accept both the draft-07 numeric form
    // and the draft-04 boolean (legacy) form that makes `minimum`/`maximum`
    // strict. The draft-07 numeric form is an independent strict bound that
    // does NOT replace its inclusive sibling: `{"minimum":5,"exclusiveMinimum":3}`
    // must reject everything below 5 (the plain bound) and at 3 or below (the
    // strict one), so both checks are applied separately. Overwriting `minimum`
    // with the exclusive value (as earlier versions did) silently dropped the
    // inclusive bound when the two coexisted.
    const asvJSONValue* minNode = schemaGet(schema, "minimum");
    const asvJSONValue* maxNode = schemaGet(schema, "maximum");
    bool strictMin = false;
    bool strictMax = false;
    const asvJSONValue* exMinNode = nullptr; // draft-07 numeric exclusive form
    const asvJSONValue* exMaxNode = nullptr;
    if (auto exMin = schemaGet(schema, "exclusiveMinimum")) {
      if (exMin->type == asvJSONValue::BOOL_VAL) {
        strictMin = exMin->flag;
      } else if (isNumberType(exMin)) {
        exMinNode = exMin;
      }
    }
    if (auto exMax = schemaGet(schema, "exclusiveMaximum")) {
      if (exMax->type == asvJSONValue::BOOL_VAL) {
        strictMax = exMax->flag;
      } else if (isNumberType(exMax)) {
        exMaxNode = exMax;
      }
    }

    // A NaN instance is IEEE-unordered: comparing it against a bound via
    // compareNumbers funnels through compareIntDouble, whose `if (b != b)
    // return 0;` quirk maps NaN to "equal", so inclusive bounds would pass a
    // value that satisfies nothing. Reject it whenever a numeric bound exists.
    const bool valueNaN = value->type == asvJSONValue::DOUBLE && value->dbl != value->dbl;
    const bool anyBound = (minNode && isNumberType(minNode)) || (maxNode && isNumberType(maxNode)) ||
                          exMinNode || exMaxNode;
    if (valueNaN && anyBound) {
      setError("Value NaN is not comparable to the numeric bounds");
      return false;
    }

    if (minNode && isNumberType(minNode) &&
        (strictMin ? compareNumbers(value, minNode) <= 0 : compareNumbers(value, minNode) < 0)) {
      if (strictMin) {
        setError("Value " + numberToStr(value) + " is not greater than exclusiveMinimum " + numberToStr(minNode));
      } else {
        setError("Value " + numberToStr(value) + " is less than minimum " + numberToStr(minNode));
      }
      return false;
    }

    if (exMinNode && compareNumbers(value, exMinNode) <= 0) {
      setError("Value " + numberToStr(value) + " is not greater than exclusiveMinimum " + numberToStr(exMinNode));
      return false;
    }

    if (maxNode && isNumberType(maxNode) &&
        (strictMax ? compareNumbers(value, maxNode) >= 0 : compareNumbers(value, maxNode) > 0)) {
      if (strictMax) {
        setError("Value " + numberToStr(value) + " is not less than exclusiveMaximum " + numberToStr(maxNode));
      } else {
        setError("Value " + numberToStr(value) + " is greater than maximum " + numberToStr(maxNode));
      }
      return false;
    }

    if (exMaxNode && compareNumbers(value, exMaxNode) >= 0) {
      setError("Value " + numberToStr(value) + " is not less than exclusiveMaximum " + numberToStr(exMaxNode));
      return false;
    }

    if (schemaHas(schema, "multipleOf")) {
      auto node = schemaGet(schema, "multipleOf");
      if (node && isNumberType(node) && getNumberValue(node) > 0) {
        bool isMultiple = false;
        if (value->type == asvJSONValue::INT && node->type == asvJSONValue::INT) {
          // Exact integer arithmetic: no double conversion, so values at the
          // full int64 range are checked precisely.
          isMultiple = (value->num % node->num == 0);
        } else {
          double val = getNumberValue(value);
          double mof = getNumberValue(node);
          // Round-based check: val/mof should be an integer. Using round avoids
          // the asymmetric tolerance of the fmod approach and handles cases like
          // 0.1 + 0.2 - 0.3 == multipleOf(0.1) correctly.
          double ratio = val / mof;
          isMultiple = std::fabs(ratio - std::round(ratio)) * std::fabs(mof) <= 1e-9 * (1.0 + std::fabs(val));
        }
        if (!isMultiple) {
          setError("Value " + numberToStr(value) + " is not a multiple of " + numberToStr(node));
          return false;
        }
      }
    }

    return true;
  }

  bool validateArray(const asvJSONValue* value, const asvJSONValue* schema) {
    if (value->type != asvJSONValue::ARRAY) return true;
    const auto& items = value->arr;
    size_t count = items ? items->size() : 0;

    if (schemaHas(schema, "minItems")) {
      auto node = schemaGet(schema, "minItems");
      if (node && node->type == asvJSONValue::INT && count < static_cast<size_t>(node->num)) {
        setError("Array has " + std::to_string(count) + " items, minItems is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "maxItems")) {
      auto node = schemaGet(schema, "maxItems");
      if (node && node->type == asvJSONValue::INT && count > static_cast<size_t>(node->num)) {
        setError("Array has " + std::to_string(count) + " items, maxItems is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "uniqueItems")) {
      auto node = schemaGet(schema, "uniqueItems");
      if (node && node->type == asvJSONValue::BOOL_VAL && node->flag) {
        // Fast path: arrays of scalars use a hash set of canonical text keys;
        // anything else goes through the bucketed deep compare below.
        bool allScalars = true;
        for (size_t i = 0; i < count; i++) {
          auto t = (*items)[i]->type;
          if (t != asvJSONValue::STRING && t != asvJSONValue::INT &&
              t != asvJSONValue::DOUBLE && t != asvJSONValue::BOOL_VAL &&
              t != asvJSONValue::NULL_VAL) { allScalars = false; break; }
          if (t == asvJSONValue::DOUBLE && (*items)[i]->dbl != (*items)[i]->dbl) { allScalars = false; break; } // NaN: keep deep compare
        }
        if (allScalars) {
          std::unordered_map<std::string, size_t> first;
          first.reserve(count);
          for (size_t i = 0; i < count; i++) {
            std::string token;
            const auto& nodePtr = (*items)[i];
            switch (nodePtr->type) {
              case asvJSONValue::NULL_VAL: token = "n"; break;
              case asvJSONValue::BOOL_VAL: token = nodePtr->flag ? "b1" : "b0"; break;
              case asvJSONValue::INT: token = "i" + std::to_string(nodePtr->num); break;
              case asvJSONValue::DOUBLE: {
                double d = nodePtr->dbl;
                if (d == 0.0) { token = "i0"; break; } // normalizes +0.0 / -0.0
                if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
                  int64_t id = static_cast<int64_t>(d);
                  if (static_cast<double>(id) == d) { token = "i" + std::to_string(id); break; } // exactly integral: same value type as INT
                }
                uint64_t bits = 0;
                std::memcpy(&bits, &d, sizeof(bits));
                token = "d" + std::to_string(bits);
                break;
              }
              case asvJSONValue::STRING:
                token.reserve(nodePtr->str_data.size() + 1);
                token += 's';
                token += nodePtr->str_data;
                break;
              default: break;
            }
            auto found = first.find(token);
            if (found != first.end()) {
              setError("Array items at indices " + std::to_string(found->second) + " and " + std::to_string(i) + " are not unique");
              return false;
            }
            first.emplace(std::move(token), i);
          }
        } else {
          // Container items: bucketed like the `enum` check. The pairwise scan
          // was quadratic in an instance the attacker controls -- 50k nested
          // arrays under a plain `{"uniqueItems":true}`.
          size_t dupA = 0, dupB = 0;
          if (findDuplicatePair(*items, dupA, dupB)) {
            setError("Array items at indices " + std::to_string(dupA) + " and " + std::to_string(dupB) + " are not unique");
            return false;
          }
        }
      }
    }

    if (schemaHas(schema, "contains")) {
      auto node = schemaGet(schema, "contains");
      if (node && items) {
        bool found = false;
        // Build the probing validator once; only its path changes per item.
        // The path is seeded from ours then indexed per item via pushIndexPath
        // (the same mechanism the array branches use), so a failure message for
        // `contains` reads "<parent>/<i>: ..." just like an items/properties
        // verdict instead of the old hand-rolled string concatenation.
        SchemaValidator subVal = makeFork();
        for (size_t i = 0; i < count; i++) {
          subVal.path_ = path_;
          subVal.pushIndexPath(i);
          subVal.error_.clear();
          if (subVal.validateNode((*items)[i].get(), node)) { found = true; break; }
          // Blaming the array for a subschema that never compiled is worse
          // than saying so, so stop at the first malformed item.
          if (adoptShapeError(subVal)) return false;
        }
        if (!found) {
          setError("Array does not contain a matching item for 'contains' schema");
          return false;
        }
      }
    }

    const asvJSONValue* itemsSchema = schemaGet(schema, "items");
    if (items && itemsSchema) {
      if (itemsSchema->type == asvJSONValue::ARRAY && itemsSchema->arr) {
        // Hoisted out of the loop: schemaHas/schemaGet are O(log n) map lookups.
        const asvJSONValue* additionalItemsSchema = schemaGet(schema, "additionalItems");
        for (size_t i = 0; i < count; i++) {
          const asvJSONValue* itemSchema = nullptr;
          if (i < itemsSchema->arr->size()) {
            itemSchema = (*itemsSchema->arr)[i].get();
          } else if (additionalItemsSchema) {
            if (isBoolFalse(additionalItemsSchema)) {
              std::string savedPath = path_;
              pushIndexPath(i);
              setError("Additional items not allowed");
              path_ = savedPath;
              return false;
            }
            itemSchema = additionalItemsSchema;
          }
          if (itemSchema && !isBoolTrue(itemSchema)) {
            std::string oldPath = path_;
            pushIndexPath(i);
            if (!validateNode((*items)[i].get(), itemSchema)) { path_ = oldPath; return false; }
            path_ = oldPath;
          }
        }
      } else {
        for (size_t i = 0; i < count; i++) {
          std::string oldPath = path_;
          pushIndexPath(i);
          if (!validateNode((*items)[i].get(), itemsSchema)) { path_ = oldPath; return false; }
          path_ = oldPath;
        }
      }
    }

    return true;
  }

  bool validateObject(const asvJSONValue* value, const asvJSONValue* schema) {
    if (value->type != asvJSONValue::OBJECT) return true;
    const auto& obj = value->obj;
    size_t propCount = obj ? obj->size() : 0;

    if (schemaHas(schema, "minProperties")) {
      auto node = schemaGet(schema, "minProperties");
      if (node && node->type == asvJSONValue::INT && propCount < static_cast<size_t>(node->num)) {
        setError("Object has " + std::to_string(propCount) + " properties, minProperties is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "maxProperties")) {
      auto node = schemaGet(schema, "maxProperties");
      if (node && node->type == asvJSONValue::INT && propCount > static_cast<size_t>(node->num)) {
        setError("Object has " + std::to_string(propCount) + " properties, maxProperties is " + std::to_string(node->num));
        return false;
      }
    }

    if (schemaHas(schema, "required")) {
      auto node = schemaGet(schema, "required");
      if (node && node->type == asvJSONValue::ARRAY && node->arr) {
        for (const auto& req : *node->arr) {
          if (req->type == asvJSONValue::STRING) {
            if (!obj || obj->find(req->str_data) == obj->end()) {
              setError("Missing required property '" + req->str_data + "'");
              return false;
            }
          }
        }
      }
    }

    if (obj) {
      const asvJSONValue* propsSchema = schemaGet(schema, "properties");
      const asvJSONValue* patternPropsSchema = schemaGet(schema, "patternProperties");
      const asvJSONValue* additionalPropsSchema = schemaGet(schema, "additionalProperties");

      std::unordered_set<std::string> matchedByPattern;

      if (patternPropsSchema && patternPropsSchema->type == asvJSONValue::OBJECT && patternPropsSchema->obj) {
        struct CompiledPattern {
          std::regex re;
          const asvJSONValue* schema;
        };
        std::vector<CompiledPattern> compiledPatterns;
        compiledPatterns.reserve(patternPropsSchema->obj->size());
        for (auto& patKV : *patternPropsSchema->obj) {
          bool validPattern = false;
          const std::regex* re = getCompiled(patKV.first, validPattern);
          if (!validPattern) {
            setError("Invalid regex pattern in patternProperties: '" + patKV.first + "'");
            return false;
          }
          compiledPatterns.push_back({*re, patKV.second.get()});
        }
        for (auto& kv : *obj) {
          for (const auto& cp : compiledPatterns) {
            if (std::regex_search(kv.first, cp.re)) {
              matchedByPattern.insert(kv.first);
              std::string oldPath = path_;
              pushPath(kv.first);
              if (!validateNode(kv.second.get(), cp.schema)) { path_ = oldPath; return false; }
              path_ = oldPath;
            }
          }
        }
      }

      if (propsSchema && propsSchema->type == asvJSONValue::OBJECT && propsSchema->obj) {
        for (auto& kv : *obj) {
          auto it = propsSchema->obj->find(kv.first);
          if (it != propsSchema->obj->end()) {
            std::string oldPath = path_;
            pushPath(kv.first);
            if (!validateNode(kv.second.get(), it->second.get())) { path_ = oldPath; return false; }
            path_ = oldPath;
          }
        }
      }

      if (additionalPropsSchema) {
        if (isBoolFalse(additionalPropsSchema)) {
          for (auto& kv : *obj) {
            bool known = matchedByPattern.count(kv.first) > 0;
            if (!known && propsSchema && propsSchema->type == asvJSONValue::OBJECT && propsSchema->obj) {
              known = propsSchema->obj->count(kv.first) > 0;
            }
            if (!known) {
              // Report against the offending property, like the properties /
              // patternProperties / propertyNames branches do. setError()
              // already prefixes path_, so the name appears only once.
              std::string oldPath = path_;
              pushPath(kv.first);
              setError("additional property not allowed");
              path_ = oldPath;
              return false;
            }
          }
        } else if (additionalPropsSchema->type == asvJSONValue::OBJECT) {
          for (auto& kv : *obj) {
            bool known = matchedByPattern.count(kv.first) > 0;
            if (!known && propsSchema && propsSchema->type == asvJSONValue::OBJECT && propsSchema->obj) {
              known = propsSchema->obj->count(kv.first) > 0;
            }
            if (!known) {
              std::string oldPath = path_;
              pushPath(kv.first);
              if (!validateNode(kv.second.get(), additionalPropsSchema)) { path_ = oldPath; return false; }
              path_ = oldPath;
            }
          }
        }
      }

      if (schemaHas(schema, "propertyNames")) {
        auto node = schemaGet(schema, "propertyNames");
        if (node && obj) {
          for (auto& kv : *obj) {
            propertyNameVal_.type = asvJSONValue::STRING;
            propertyNameVal_.str_data = kv.first;
            // Push the offending property name into the path exactly like
            // additionalProperties does, so the inner message names it instead
            // of a generic "Property name 'x' is invalid" on top of the real
            // cause (setError is idempotent, so that outer call was dead code).
            std::string oldPath = path_;
            pushPath(kv.first);
            bool ok = validateNode(&propertyNameVal_, node);
            path_ = oldPath;
            if (!ok) return false;
          }
        }
      }

      if (schemaHas(schema, "dependentRequired")) {
        auto node = schemaGet(schema, "dependentRequired");
        if (node && node->type == asvJSONValue::OBJECT && node->obj && obj) {
          for (auto& depKV : *node->obj) {
            if (obj->count(depKV.first) > 0 && depKV.second->type == asvJSONValue::ARRAY && depKV.second->arr) {
              for (const auto& req : *depKV.second->arr) {
                if (req->type == asvJSONValue::STRING && obj->count(req->str_data) == 0) {
                  setError("Property '" + depKV.first + "' requires property '" + req->str_data + "'");
                  return false;
                }
              }
            }
          }
        }
      }

      if (schemaHas(schema, "dependencies")) {
        auto node = schemaGet(schema, "dependencies");
        if (node && node->type == asvJSONValue::OBJECT && node->obj && obj) {
          for (auto& depKV : *node->obj) {
            if (obj->count(depKV.first) > 0) {
              if (depKV.second->type == asvJSONValue::ARRAY && depKV.second->arr) {
                for (const auto& req : *depKV.second->arr) {
                  if (req->type == asvJSONValue::STRING && obj->count(req->str_data) == 0) {
                    setError("Property '" + depKV.first + "' requires property '" + req->str_data + "'");
                    return false;
                  }
                }
              } else if (isSchema(depKV.second.get())) {
                // isSchema covers both OBJECT schemas and boolean schemas
                // (true = always-valid, false = reject any value that has this key).
                // validateNode already dispatches bool schemas at its top.
                if (!validateNode(value, depKV.second.get())) return false;
              }
            }
          }
        }
      }
    }

    return true;
  }
};

inline bool asvJSON::validate(const asvJSON& schema, std::string& error) const {
  SchemaValidator v;
  return v.validate(root.get(), schema.root.get(), error);
}

inline bool asvJSON::validate(const asvJSON& schema) const {
  SchemaValidator v;
  return v.validate(root.get(), schema.root.get());
}

} // namespace asvJSONInternal
