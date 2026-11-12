#pragma once
// C++ struct/enum code generation and decoding for asvJSON++.
//
// `toCpp()` infers C++ type declarations (structs, enums, arrays) from a parsed
// value tree and renders them as source text. `fromCpp()` parses a pragmatic
// subset of C/C++ type declarations -- as produced by toCpp() or hand-written
// -- back into a value tree (best-effort round-trip; see CppReadOptions).
//
// Inference rules (see CppWriteOptions for the switches):
//   - OBJECT             -> struct; one field per key, named from the key.
//   - ARRAY of one type  -> std::array<T,N> (useStdArray) / std::vector<T>;
//                           C style -> `T name[N]`.
//   - ARRAY of objects   -> std::vector<Struct> (struct named from the field).
//   - ARRAY of mixed     -> std::vector<std::variant<...>>.
//   - empty ARRAY        -> std::vector<std::monostate> (placeholder from
//                           <variant>; edit the type).
//   - INT/DOUBLE/BOOL    -> int/double/bool (intType configurable).
//   - STRING             -> stringType; identifier strings in an enum context
//                           (see emitEnums / emitEnumMaps / enumPaths) become
//                           an enum type.
//   - NULL_VAL           -> nullType.
//   - DATETIME/BINARY    -> datetimeType/binaryType; other specials -> string.
//   - root {Name: {...}} -> `struct Name` (unwrapRootSingleKey).
//
// Comments are preserved as `//` / `/* */` lines and same-line trails. The
// comments of the ENUMERATORS are read as well (they are attached to the
// variant values where the enum type is used), so a toCpp() -> fromCpp() trip
// keeps them. A block comment whose text contains a `*/` terminator is emitted
// with the terminator broken up (`* /`) - otherwise the comment would end
// early and the rest of the text would become code.
//
// Known limitations:
//   - Paren-init for struct types (`Foo f(1, 2);`) maps positional args to
//     fields in order; the values are parsed but only scalar literals survive.
//     Complex expressions fall back to field defaults.
//   - Container paren-init (`std::vector<int> v(10);`) emits an empty array -
//     the size argument is not interpreted (container semantics differ: vector
//     takes count, string takes count+char, map ignores). Use brace-init or
//     explicit element listing for predictable output.
//   - A `union` is read like a `struct` (its members become the fields); the
//     alternatives share one storage in C++, which the value tree cannot
//     express.
//   - The type name of an ANONYMOUS enum is synthesized (`Enum1`, `Enum2`, ...)
//     - the enum has no tag of its own.
//   - A comment that follows the LAST enumerator (inside the braces) is
//     attached to that enumerator as its trailing comment - the enum is a
//     type, it has no node of its own to hold a "closing" comment.

#include "../core.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace asvJSONInternal {

// ---------------------------------------------------------------------------
// Identifier helpers
// ---------------------------------------------------------------------------

inline bool cppIsKeyword(std::string_view s) {
  static const char* const kw[] = {
    "alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool",
    "break","case","catch","char","char16_t","char32_t","class","compl",
    "concept","const","consteval","constexpr","constinit","const_cast",
    "continue","co_await","co_return","co_yield","decltype","default","delete",
    "do","double","dynamic_cast","else","enum","explicit","export","extern",
    "false","float","for","friend","goto","if","inline","int","long","mutable",
    "namespace","new","noexcept","not","not_eq","nullptr","operator","or",
    "or_eq","private","protected","public","register","reinterpret_cast",
    "requires","return","short","signed","sizeof","static","static_assert",
    "static_cast","struct","switch","template","this","thread_local","throw",
    "true","try","typedef","typeid","typename","union","unsigned","using",
    "virtual","void","volatile","wchar_t","while","xor","xor_eq",
    "int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t","uint32_t",
    "uint64_t","size_t","time_t","ssize_t",
    "final","import","module","override"
  };
  for (const char* k : kw) if (s == k) return true;
  return false;
}

inline bool cppIdentStart(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool cppIdentChar(unsigned char c) {
  return cppIdentStart(c) || (c >= '0' && c <= '9');
}

// Sanitize arbitrary text into a valid C++ identifier (case preserved).
inline std::string cppIdent(std::string_view raw, const char* fallback = "field") {
  std::string out;
  out.reserve(raw.size() + 1);
  for (char ch : raw) {
    unsigned char c = static_cast<unsigned char>(ch);
    out += cppIdentChar(c) ? ch : '_';
  }
  if (out.empty()) out = fallback;
  if (!cppIdentStart(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
  if (cppIsKeyword(out)) out += '_';
  return out;
}

// Turn a key into a PascalCase type name.
inline std::string cppPascal(std::string_view raw, const char* fallback = "Type") {
  std::string out;
  bool up = true;
  for (char ch : raw) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (cppIdentChar(c) && c != '_') {
      if (up && c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
      out += static_cast<char>(c);
      up = false;
    } else {
      up = true;
    }
  }
  if (out.empty()) out = fallback;
  if (!cppIdentStart(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
  if (cppIsKeyword(out)) out += '_';
  return out;
}

// Naive singularization for element type names ("items" -> "item").
inline std::string cppSingular(std::string w) {
  static const char* const keep[] = {
    "series", "species", "status", "data", "analysis", "basis", "crisis",
    "news", "means", "index", "matrix", "vertex", "appendix", "alias",
    "bias", "yes", "always", "this", "its", "less", "cross", "address",
    "process", "access", "class", "glass", "mass", "boss", "gas", "bus"
  };
  std::string_view sv(w);
  auto ends = [&](std::string_view suf) {
    return sv.size() >= suf.size() &&
           sv.compare(sv.size() - suf.size(), suf.size(), suf) == 0;
  };
  for (const char* k : keep) if (w == k) return w;
  // Endings that are already singular: -sis (analysis), -us (bus), -is (basis),
  // -as (gas), -os (atlas) and a doubled final -s (address). Dropping the last
  // 's' from these invents a word that does not exist ("bus" -> "bu"), which
  // is worse than leaving the plural alone. The trade-off is real in both
  // directions: a regular plural ending in one of these keeps its plural name
  // ("areas" -> "Areas", "ideas" -> "Ideas"), which is cosmetic only -- the
  // generated type still compiles and round-trips. Guessing which -as word is
  // already singular is not decidable from the spelling, so the endings rule
  // wins over an open-ended keep-list.
  if (ends("sis") || ends("us") || ends("is") || ends("as") || ends("os")) return w;
  if (w.size() > 1 && w.back() == 's' && w[w.size() - 2] == 's') return w;
  if (ends("ies")) return w.substr(0, w.size() - 3) + "y";       // cities -> city
  if (ends("sses")) return w.substr(0, w.size() - 2);            // classes -> class
  if (ends("ches") || ends("shes") || ends("xes") || ends("zes"))
    return w.substr(0, w.size() - 2);                            // boxes -> box
  // Anything else: drop a single trailing 's'. "-ses" is left alone
  // ("houses" -> "house" is the common case; "buses" -> "buse" is the
  // unavoidable casualty of English not being reversible).
  if (w.size() > 1 && w.back() == 's' && w[w.size() - 2] != 's') w.pop_back();
  return w;
}

// A bare identifier token (an enum-variant candidate).
inline bool cppIdentToken(std::string_view s) {
  if (s.empty() || s.size() > 64) return false;
  if (!cppIdentStart(static_cast<unsigned char>(s[0]))) return false;
  for (char ch : s) if (!cppIdentChar(static_cast<unsigned char>(ch))) return false;
  return true;
}

// ---------------------------------------------------------------------------
// Type descriptor produced by inference
// ---------------------------------------------------------------------------

struct CppType {
  enum Kind { Scalar, Struct, Enum, Array, Variant };
  Kind kind = Scalar;
  std::string type;        // full C++ type spelling
  std::string suffix;      // C-style array suffix, e.g. "[2]" / "[2][3]"
  std::string structName;  // Struct/Variant primary struct name
  std::string enumName;    // Enum kind (type name)
  std::vector<CppType> alternatives; // Variant alternatives
  std::shared_ptr<CppType> elem;     // Array element (owned copy)
  bool hasElem = false;
};

// ---------------------------------------------------------------------------
// Comment emission
// ---------------------------------------------------------------------------

// A block comment's text must not contain the `*/` terminator: it would end
// the comment early and the rest of the text would become code, so the
// generated file would not compile. The terminator is broken up (`* /`); the
// text is otherwise kept intact.
inline std::string cppEscapeBlockComment(const std::string& text) {
  if (text.find("*/") == std::string::npos) return text;
  std::string out;
  out.reserve(text.size() + 4);
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
      out += "* /";
      i++;
      continue;
    }
    out += text[i];
  }
  return out;
}

inline void cppEmitComments(std::string& out, const std::vector<Comment>& list,
                            int indent, const CppWriteOptions& o) {
  if (!o.emitComments) return;
  const std::string pad(static_cast<size_t>(indent < 0 ? 0 : indent) *
                            static_cast<size_t>(o.indentWidth), ' ');
  for (const auto& c : list) {
    out += pad;
    if (c.block) { out += "/*"; out += cppEscapeBlockComment(c.text); out += "*/\n"; }
    else { out += "//"; out += c.text; out += '\n'; }
  }
}

inline void cppEmitTrailing(std::string& out, const asvJSONValue* v, const CppWriteOptions& o) {
  if (!o.emitComments || !v || !v->comments) return;
  for (const auto& c : v->comments->trailing) {
    out += ' ';
    if (c.block) { out += "/*"; out += cppEscapeBlockComment(c.text); out += "*/"; }
    else { out += "//"; out += c.text; }
  }
}

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------

class CppGenerator {
public:
  explicit CppGenerator(const CppWriteOptions& o) : opts_(o) {}

  std::string run(const asvJSONValue* root);

private:
  struct Field {
    std::string name;
    std::string type;
    std::string suffix;
    std::string init;
    const asvJSONValue* src = nullptr;
  };
  struct StructDef {
    std::string name;
    std::vector<Field> fields;
    const asvJSONValue* src = nullptr;
    const asvJSONValue* commentSrc = nullptr;
    std::vector<std::string> deps; // struct names referenced by value
    bool registered = false;
  };
  struct EnumDef {
    std::string name;
    std::vector<std::pair<std::string, const asvJSONValue*>> values;
    std::string sig;
  };

  bool isC() const { return opts_.style == CppWriteOptions::Style::C; }
  bool enumQualified() const { return !isC() && opts_.useEnumClass; }
  std::string effString() const { return isC() ? "const char*" : opts_.stringType; }
  std::string effBinary() const { return isC() ? "unsigned char*" : opts_.binaryType; }
  std::string effDateTime() const { return isC() ? "time_t" : opts_.datetimeType; }
  std::string effNull() const { return isC() ? "void*" : opts_.nullType; }
  std::string enumTypeName(const std::string& n) const { return isC() ? ("enum " + n) : n; }

  static bool pathMatch(const std::string& path, const std::string& hint) {
    if (path == hint) return true;
    std::string last = path;
    size_t dot = last.rfind('.');
    if (dot != std::string::npos) last = last.substr(dot + 1);
    return last == hint;
  }
  bool forcedEnum(const std::string& path) const {
    if (opts_.emitEnumMaps) return true;
    for (const auto& h : opts_.enumPaths) if (pathMatch(path, h)) return true;
    return false;
  }
  std::string enumNameHint(const std::string& path) const {
    for (const auto& p : opts_.enumNames) if (pathMatch(path, p.first)) return p.second;
    return std::string();
  }

  std::string uniqueName(const std::string& base) {
    if (usedNames_.insert(base).second) return base;
    for (int i = 2; i < 10000; i++) {
      std::string cand = base + std::to_string(i);
      if (usedNames_.insert(cand).second) return cand;
    }
    throw asvJSONError("cpp: could not allocate a unique type name for '" + base + "'");
  }

  std::string registerEnum(const std::vector<std::string>& rawValues,
                           const std::string& path, const std::string& hint,
                           const std::vector<const asvJSONValue*>& srcs, bool singular) {
    std::string sig;
    for (const auto& v : rawValues) { sig += std::to_string(v.size()); sig += ':'; sig += v; sig += ';'; }
    auto it = enumBySig_.find(sig);
    if (it != enumBySig_.end()) return enums_[it->second].name;

    std::string name = enumNameHint(path);
    if (name.empty()) {
      std::string base = hint.empty() ? opts_.rootName : hint;
      if (singular) base = cppSingular(base);
      name = cppPascal(base, "Enum") + (isC() ? "" : "");
    }
    name = uniqueName(name);
    EnumDef e;
    e.name = name;
    e.sig = sig;
    for (size_t i = 0; i < rawValues.size(); i++) {
      std::string en = cppIdent(rawValues[i], "Value");
      e.values.emplace_back(en, i < srcs.size() ? srcs[i] : nullptr);
    }
    enumBySig_[sig] = enums_.size();
    enums_.push_back(std::move(e));
    return name;
  }

  bool enumMapLike(const asvJSONValue* v) const {
    if (v->type != asvJSONValue::OBJECT || !v->obj || v->obj->empty()) return false;
    std::set<std::string_view> seen;
    for (const auto& [k, c] : *v->obj) {
      (void)k;
      if (!c || c->type != asvJSONValue::STRING) return false;
      if (!cppIdentToken(c->str_data)) return false;
      if (!seen.insert(std::string_view(c->str_data)).second) return false;
    }
    return true;
  }

  bool enumArrayLike(const asvJSONValue* v) const {
    if (v->type != asvJSONValue::ARRAY || !v->arr || v->arr->empty()) return false;
    std::set<std::string_view> seen;
    for (const auto& c : *v->arr) {
      if (!c || c->type != asvJSONValue::STRING) return false;
      if (!cppIdentToken(c->str_data)) return false;
      if (!seen.insert(std::string_view(c->str_data)).second) return false;
    }
    return true;
  }

  CppType inferType(const asvJSONValue* v, const std::string& path,
                    const std::string& hint, const asvJSONValue* commentSrc = nullptr);
  CppType inferArray(const asvJSONValue* v, const std::string& path,
                     const std::string& hint);
  std::string registerStruct(const asvJSONValue* v, const std::string& path,
                             const std::string& hint, const asvJSONValue* commentSrc = nullptr);
  std::string containerOf(const std::string& elem, size_t n, std::string& suffixOut);

  void valueLiteral(const asvJSONValue* v, const CppType& t, std::string& out) const;
  static std::string cppIntLiteral(long long n);
  void renderStruct(const StructDef& s, std::string& out) const;
  void renderEnum(const EnumDef& e, std::string& out) const;
  void topoVisit(size_t idx);
  size_t findStruct(const std::string& name) const;

  const CppWriteOptions& opts_;
  std::vector<StructDef> structs_;
  std::vector<EnumDef> enums_;
  std::map<std::string, size_t> enumBySig_;
  std::set<std::string> usedNames_;
  std::unordered_set<const void*> busyObjects_;
  std::unordered_map<const asvJSONValue*, CppType> objTypeCache_;
  std::unordered_map<std::string, size_t> structIndex_;
  std::vector<std::string> order_;
  bool usedVariant_ = false;
  bool usedUniquePtr_ = false;
};

// Scope guard: removes an object pointer from busyObjects_ on scope exit, so an
// exception (e.g. std::bad_alloc) never leaves the object "stuck" as busy.
class CppGeneratorBusyGuard {
  std::unordered_set<const void*>& set_;
  const void* key_;
public:
  CppGeneratorBusyGuard(std::unordered_set<const void*>& set, const void* key)
    : set_(set), key_(key) {}
  CppGeneratorBusyGuard(const CppGeneratorBusyGuard&) = delete;
  CppGeneratorBusyGuard& operator=(const CppGeneratorBusyGuard&) = delete;
  ~CppGeneratorBusyGuard() { set_.erase(key_); }
};

inline std::string CppGenerator::containerOf(const std::string& elem, size_t n,
                                             std::string& suffixOut) {
  if (isC()) {
    // The dimension is PREPENDED: `suffixOut` may already carry the inner
    // dimensions of a nested array (`int m[2][3]` - the outer count comes
    // first). Appending reversed the order, so a 2x3 array came out as 3x2.
    suffixOut = "[" + std::to_string(n) + "]" + suffixOut;
    return elem;
  }
  if (opts_.useStdArray) return "std::array<" + elem + ", " + std::to_string(n) + ">";
  return "std::vector<" + elem + ">";
}

inline size_t CppGenerator::findStruct(const std::string& name) const {
  auto it = structIndex_.find(name);
  if (it != structIndex_.end()) return it->second;
  return static_cast<size_t>(-1);
}

inline std::string CppGenerator::registerStruct(const asvJSONValue* v,
                                                const std::string& path,
                                                const std::string& hint,
                                                const asvJSONValue* commentSrc) {
  // Fast-path: if this value was already registered (e.g. via a prior inferType
  // call that populated objTypeCache_), reuse the same name rather than deriving
  // a second uniqueName() and creating a dangling reference.
  auto cac = objTypeCache_.find(v);
  if (cac != objTypeCache_.end()) return cac->second.structName;

  std::string base = hint.empty() ? opts_.rootName : hint;
  std::string name = uniqueName(cppPascal(base, opts_.rootName.c_str()));

  // Register in cache BEFORE processing children so that any recursive
  // inferType() call on this same object (via busyObjects_ hit) resolves to the
  // exact same name. Without this, a cycle like root -> child -> root would
  // emit std::unique_ptr<Root2> while the struct is actually named Root.
  CppType cached;
  cached.kind = CppType::Struct;
  cached.structName = name;
  cached.type = name;
  objTypeCache_.emplace(v, cached);

  StructDef def;
  def.name = name;
  def.src = v;
  def.commentSrc = commentSrc ? commentSrc : v;

  bool isEnumMap = enumMapLike(v) && forcedEnum(path);
  if (isEnumMap) {
    std::vector<std::string> vals;
    std::vector<const asvJSONValue*> srcs;
    for (const auto& [k, c] : *v->obj) {
      (void)k;
      vals.push_back(c->str_data);
      srcs.push_back(c.get());
    }
    std::string en = registerEnum(vals, path, hint, srcs, /*singular=*/false);
    for (const auto& [k, c] : *v->obj) {
      Field f;
      f.name = cppIdent(k);
      f.type = enumTypeName(en);
      f.src = c.get();
      if (opts_.emitDefaults) f.init = (enumQualified() ? en + "::" : "") + cppIdent(c->str_data, "Value");
      def.fields.push_back(std::move(f));
    }
  } else {
    for (const auto& [k, c] : *v->obj) {
      Field f;
      f.name = cppIdent(k);
      std::string childPath = path.empty() ? std::string(k) : path + "." + std::string(k);
      CppType ct = inferType(c.get(), childPath, std::string(k));
      f.type = ct.type;
      f.suffix = ct.suffix;
      f.src = c.get();
      if (opts_.emitDefaults) {
        std::string lit;
        valueLiteral(c.get(), ct, lit);
        f.init = lit;
      }
      // Record by-value struct dependencies for ordering.
      if (ct.kind == CppType::Struct && !ct.structName.empty()) def.deps.push_back(ct.structName);
      else if (ct.kind == CppType::Array && ct.hasElem && ct.elem && ct.elem->kind == CppType::Struct)
        def.deps.push_back(ct.elem->structName);
      else if (ct.kind == CppType::Variant)
        for (const auto& a : ct.alternatives)
          if (a.kind == CppType::Struct) def.deps.push_back(a.structName);
      def.fields.push_back(std::move(f));
    }
  }
  structs_.push_back(std::move(def));
  return name;
}

inline CppType CppGenerator::inferArray(const asvJSONValue* v,
                                        const std::string& path,
                                        const std::string& hint) {
  CppType t;
  t.kind = CppType::Array;
  const size_t n = v->arr ? v->arr->size() : 0;

  // Empty array: unknown element type, emit a documented placeholder.
  if (n == 0) {
    CppType elem;
    elem.kind = CppType::Scalar;
    if (isC()) {
      // void* keeps the declaration compilable; the note makes the gap visible.
      elem.type = "void*";
      t.type = "void* /* TODO: type */";
    } else {
      elem.type = "std::monostate";
      t.type = "std::vector<std::monostate>";
      usedVariant_ = true;  // std::monostate comes from <variant>
    }
    t.elem = std::make_shared<CppType>(elem);
    t.hasElem = true;
    return t;
  }

  // Array of identifier strings -> enum.
  if (opts_.emitEnums && enumArrayLike(v)) {
    std::vector<std::string> vals;
    std::vector<const asvJSONValue*> srcs;
    for (const auto& c : *v->arr) { vals.push_back(c->str_data); srcs.push_back(c.get()); }
    std::string en = registerEnum(vals, path, hint, srcs, /*singular=*/true);
    CppType elem;
    elem.kind = CppType::Enum;
    elem.type = enumTypeName(en);
    elem.enumName = en;
    std::string suffix;
    t.type = containerOf(enumTypeName(en), n, suffix);
    t.suffix = suffix;
    t.elem = std::make_shared<CppType>(elem);
    t.hasElem = true;
    return t;
  }

  // Array of objects sharing one key set -> std::vector<Struct>.
  bool allObj = true;
  for (const auto& c : *v->arr) if (!c || c->type != asvJSONValue::OBJECT) { allObj = false; break; }
  if (allObj) {
    // Union of keys so mixed record shapes still compile.
    std::vector<std::string> keys;
    std::set<std::string> keySet;
    for (const auto& c : *v->arr)
      for (const auto& [k, cc] : *c->obj) if (keySet.insert(k).second) keys.push_back(k);
    std::string sname = uniqueName(cppPascal(cppSingular(hint.empty() ? opts_.rootName : hint), "Item"));
    StructDef def;
    def.name = sname;
    def.src = v->arr->front().get();
    def.commentSrc = def.src;
    for (const auto& k : keys) {
      const asvJSONValue* sample = nullptr;
      for (const auto& c : *v->arr) { auto* f = c->get(k); if (f) { sample = f; break; } }
      Field f;
      f.name = cppIdent(k);
      std::string childPath = path.empty() ? k : path + "." + k;
      CppType ct = sample ? inferType(sample, childPath, k)
                          : CppType{};
      f.type = ct.type;
      f.suffix = ct.suffix;
      f.src = sample;
      if (opts_.emitDefaults && sample) { std::string lit; valueLiteral(sample, ct, lit); f.init = lit; }
      if (ct.kind == CppType::Struct && !ct.structName.empty()) def.deps.push_back(ct.structName);
      def.fields.push_back(std::move(f));
    }
    structs_.push_back(std::move(def));
    CppType elem;
    elem.kind = CppType::Struct;
    elem.type = sname;
    elem.structName = sname;
    // Reuse the same if all keys identical, else still one union struct.
    std::string suffix;
    t.type = containerOf(sname, n, suffix);
    t.suffix = suffix;
    t.elem = std::make_shared<CppType>(elem);
    t.hasElem = true;
    return t;
  }

  // Homogeneous element type -> container; mixed -> vector<variant<...>>.
  CppType first = inferType(v->arr->front().get(), path, hint);
  bool same = true;
  for (size_t i = 1; i < n && same; i++) {
    CppType cur = inferType(v->arr->at(i).get(), path, hint);
    if (cur.type != first.type || cur.suffix != first.suffix) same = false;
  }
  if (same) {
    // A nested array keeps its inner dimensions in first.suffix (C style:
    // `int m[2][3]`). Seed the suffix with them - a fresh empty string lost
    // the inner `[3]` and the array was emitted as `int m[2]`.
    std::string suffix = first.suffix;
    t.type = containerOf(first.type, n, suffix);
    t.suffix = suffix;
    t.elem = std::make_shared<CppType>(first);
    t.hasElem = true;
    return t;
  }

  // Mixed: std::vector<std::variant<...>> (C style falls back to void*).
  if (isC()) {
    t.type = "void*";
    return t;
  }
  usedVariant_ = true;
  std::vector<CppType> alts;
  for (const auto& c : *v->arr) {
    CppType cur = inferType(c.get(), path, hint);
    bool found = false;
    for (const auto& a : alts) if (a.type == cur.type) { found = true; break; }
    if (!found) alts.push_back(cur);
  }
  std::string vt = "std::variant<";
  for (size_t i = 0; i < alts.size(); i++) { if (i) vt += ", "; vt += alts[i].type; }
  vt += ">";
  t.kind = CppType::Variant;
  t.type = "std::vector<" + vt + ">";
  t.alternatives = std::move(alts);
  return t;
}

inline CppType CppGenerator::inferType(const asvJSONValue* v, const std::string& path,
                                       const std::string& hint, const asvJSONValue* commentSrc) {
  CppType t;
  if (!v) { t.type = effNull(); return t; }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: t.type = effNull(); return t;
    case asvJSONValue::BOOL_VAL: t.type = "bool"; return t;
    case asvJSONValue::INT: t.type = opts_.intType; return t;
    case asvJSONValue::DOUBLE: t.type = "double"; return t;
    case asvJSONValue::STRING: t.type = effString(); return t;
    case asvJSONValue::DATETIME: t.type = effDateTime(); return t;
    case asvJSONValue::BINARY: t.type = effBinary(); return t;
    case asvJSONValue::OBJECTID:
    case asvJSONValue::REGEX:
    case asvJSONValue::TIMESTAMP:
    case asvJSONValue::EXTENSION:
    case asvJSONValue::CUSTOM_TAG: t.type = effString(); return t;
    case asvJSONValue::ARRAY: return inferArray(v, path, hint);
    case asvJSONValue::OBJECT: {
      auto cac = objTypeCache_.find(v);
      if (cac != objTypeCache_.end()) return cac->second;
      const void* key = static_cast<const void*>(v);
      if (busyObjects_.count(key)) {
        // Unreachable for a well-formed tree: registerStruct() caches this same
        // object BEFORE it descends into children, so a re-entrant call for the
        // same pointer returns at the `cac` lookup above. asvJSONValue owns its
        // children outright (unique_ptr), so no descendant can be the ancestor's
        // own pointer. Kept as a cheap backstop for a future container type that
        // could alias (shared_ptr/weak_ptr children) -- better a pointer-typed
        // member than unbounded recursion.
        usedUniquePtr_ = true;
        t.kind = CppType::Struct;
        t.structName = cppPascal(hint.empty() ? opts_.rootName : hint, "Node");
        t.type = isC() ? (t.structName + "*") : ("std::unique_ptr<" + t.structName + ">");
        return t;
      }
      busyObjects_.insert(key);
      CppGeneratorBusyGuard guard(busyObjects_, key);
      std::string name = registerStruct(v, path, hint, commentSrc);
      t.kind = CppType::Struct;
      t.structName = name;
      t.type = name;
      objTypeCache_.emplace(v, t);
      return t;
    }
    default: t.type = effString(); return t;
  }
}

inline void CppGenerator::valueLiteral(const asvJSONValue* v, const CppType& t,
                                       std::string& out) const {
  if (!v) { out += "{}"; return; }
  if (t.kind == CppType::Enum) {
    if (enumQualified()) { out += t.enumName; out += "::"; }
    out += cppIdent(v->str_data, "Value");
    return;
  }
  if (t.kind == CppType::Struct || t.kind == CppType::Variant) { out += "{}"; return; }
  if (t.kind == CppType::Array) {
    if (t.hasElem && t.elem &&
        (t.elem->kind == CppType::Struct || t.elem->kind == CppType::Variant)) {
      out += "{}";
      return;
    }
    out += '{';
    if (v->type == asvJSONValue::ARRAY && v->arr && t.hasElem) {
      bool first = true;
      for (const auto& e : *v->arr) {
        if (!first) out += ", ";
        first = false;
        valueLiteral(e.get(), *t.elem, out);
      }
    }
    out += '}';
    return;
  }
  switch (v->type) {
    case asvJSONValue::NULL_VAL: out += isC() ? "0" : "nullptr"; break;
    case asvJSONValue::BOOL_VAL: out += v->flag ? "true" : "false"; break;
    case asvJSONValue::INT:
      out += cppIntLiteral(static_cast<long long>(v->num));
      break;
    case asvJSONValue::DOUBLE: {
      double d = v->dbl;
      if (std::isnan(d)) { out += "(0.0 / 0.0)"; break; }
      if (std::isinf(d)) { out += (d < 0 ? "(-1.0 / 0.0)" : "(1.0 / 0.0)"); break; }
      std::string s; fmtDoubleVal(d, s);
      if (s.find_first_of(".eE") == std::string::npos) s += ".0";
      out += s;
      break;
    }
    case asvJSONValue::STRING:
    case asvJSONValue::DATETIME:
    case asvJSONValue::BINARY:
    case asvJSONValue::OBJECTID:
    case asvJSONValue::REGEX:
    case asvJSONValue::TIMESTAMP:
    case asvJSONValue::EXTENSION:
    case asvJSONValue::CUSTOM_TAG:
      if (v->type == asvJSONValue::DATETIME) { out += cppIntLiteral(static_cast<long long>(v->timestamp)); break; }
      if (v->type == asvJSONValue::BINARY || v->type == asvJSONValue::EXTENSION) { out += "{}"; break; }
      out += '"';
      appendJsonEscaped(out, v->str_data);
      out += '"';
      break;
    default: out += "{}"; break;
  }
}

// Render an integer constant that always compiles (INT64_MIN would otherwise
// produce -9223372036854775808, which is not a valid long long literal).
inline std::string CppGenerator::cppIntLiteral(long long n) {
  if (n == (std::numeric_limits<long long>::min)()) return "(-9223372036854775807LL - 1)";
  return std::to_string(n);
}

inline void CppGenerator::renderStruct(const StructDef& s, std::string& out) const {
  const asvJSONValue* cs = s.commentSrc ? s.commentSrc : s.src;
  if (cs && cs->comments) cppEmitComments(out, cs->comments->leading, 0, opts_);
  out += isC() ? "typedef struct " : "struct ";
  out += s.name;
  out += " {";
  if (cs) cppEmitTrailing(out, cs, opts_);
  out += '\n';
  for (const auto& f : s.fields) {
    if (f.src && f.src->comments) cppEmitComments(out, f.src->comments->leading, 1, opts_);
    out += std::string(static_cast<size_t>(opts_.indentWidth), ' ');
    out += f.type;
    out += ' ';
    out += f.name;
    out += f.suffix;
    if (!f.init.empty() && !isC()) { out += " = "; out += f.init; }
    out += ';';
    if (f.src) cppEmitTrailing(out, f.src, opts_);
    out += '\n';
  }
  if (cs && cs->comments) cppEmitComments(out, cs->comments->closing, 1, opts_);
  out += '}';
  if (isC()) { out += ' '; out += s.name; out += ';'; }
  else out += ';';
  out += '\n';
}

inline void CppGenerator::renderEnum(const EnumDef& e, std::string& out) const {
  if (isC()) out += "enum ";
  else if (opts_.useEnumClass) out += "enum class ";
  else out += "enum ";
  out += e.name;
  out += " {\n";
  for (size_t i = 0; i < e.values.size(); i++) {
    const auto& [en, src] = e.values[i];
    if (src && src->comments && opts_.emitComments)
      cppEmitComments(out, src->comments->leading, 1, opts_);
    out += std::string(static_cast<size_t>(opts_.indentWidth), ' ');
    out += en;
    out += ',';
    if (src) cppEmitTrailing(out, src, opts_);
    out += '\n';
  }
  out += "};\n";
}

inline void CppGenerator::topoVisit(size_t idx) {
  if (idx == static_cast<size_t>(-1)) return;
  StructDef& s = structs_[idx];
  if (s.registered) return;
  s.registered = true;
  for (const auto& d : s.deps) topoVisit(findStruct(d));
  order_.push_back(s.name);
}

inline std::string CppGenerator::run(const asvJSONValue* root) {
  if (!root) return std::string();
  CppType top;
  bool topIsStruct = false;
  std::string topName;

  if (root->type == asvJSONValue::OBJECT && root->obj) {
    if (opts_.unwrapRootSingleKey && root->obj->size() == 1) {
      const auto& kv = *root->obj->begin();
      const std::string& key = kv.first;
      const asvJSONValue* val = kv.second.get();
      if (val && val->type == asvJSONValue::OBJECT) {
        topName = registerStruct(val, key, key, root);
        top.kind = CppType::Struct;
        top.type = topName;
        top.structName = topName;
        topIsStruct = true;
      } else if (val && val->type == asvJSONValue::ARRAY) {
        top = inferType(val, key, key);
        topName = uniqueName(cppPascal(key, opts_.rootName.c_str()));
      } else {
        topName = registerStruct(root, "", opts_.rootName);
        topIsStruct = true;
      }
    } else {
      topName = registerStruct(root, "", opts_.rootName);
      topIsStruct = true;
    }
  } else {
    top = inferType(root, "", opts_.rootName);
    topName = uniqueName(cppPascal(opts_.rootName, "Document"));
  }

  for (size_t i = 0; i < structs_.size(); i++) structIndex_[structs_[i].name] = i;
  for (size_t i = 0; i < structs_.size(); i++) topoVisit(i);

  std::string out;
  if (opts_.emitIncludes) {
    out += "// Generated by asvJSON++ toCpp()\n";
    if (isC()) {
      out += "#include <stdbool.h>\n";
      out += "#include <stdint.h>\n";
      out += "#include <time.h>\n\n";
    } else {
      out += "#include <array>\n";
      out += "#include <cstdint>\n";
      out += "#include <ctime>\n";
      out += "#include <string>\n";
      if (usedVariant_) out += "#include <variant>\n";
      if (usedUniquePtr_) out += "#include <memory>\n";
      out += "#include <vector>\n\n";
    }
  }
  if (opts_.forwardDeclare && !structs_.empty()) {
    for (const auto& s : structs_) {
      if (isC()) out += "typedef struct " + s.name + " " + s.name + ";\n";
      else out += "struct " + s.name + ";\n";
    }
    out += '\n';
  }
  for (const auto& e : enums_) renderEnum(e, out);
  if (!enums_.empty()) out += '\n';
  for (const auto& name : order_) {
    size_t idx = findStruct(name);
    if (idx != static_cast<size_t>(-1)) renderStruct(structs_[idx], out);
  }
  if (!topIsStruct) {
    out += isC() ? "typedef " : "using ";
    if (isC()) { out += top.type; out += ' '; out += topName; out += top.suffix; out += ';'; }
    else { out += topName; out += " = "; out += top.type; out += top.suffix; out += ';'; }
    out += '\n';
  }
  if (root->comments && opts_.emitComments)
    cppEmitComments(out, root->comments->footing, 0, opts_);
  return out;
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

inline std::string asvJSON::toCpp(const CppWriteOptions& opts) const {
  if (!root) return std::string();
  CppGenerator gen(opts);
  return gen.run(root.get());
}

inline void asvJSONValue::toCpp(std::string& out, const CppWriteOptions& opts) const {
  CppGenerator gen(opts);
  out += gen.run(this);
}

// ---------------------------------------------------------------------------
// fromCpp() -- pragmatic C/C++ declaration reader
// ---------------------------------------------------------------------------

namespace cppread {

struct Tok {
  enum Kind { Ident, Number, Str, Char, Punct, Comment, End } kind = End;
  std::string text;   // decoded text for Str/Char; raw spelling otherwise
  int line = 0;
  bool block = false; // Comment: true for /* ... */
};

inline bool cppRdIdentStart(char c) {
  // A byte >= 0x80 starts a UTF-8 identifier (C++11 allows universal
  // character names and extended characters).
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         static_cast<unsigned char>(c) >= 0x80;
}
inline bool cppRdIdentChar(char c) {
  // A byte >= 0x80 is part of a UTF-8 identifier (C++11 allows universal
  // character names and extended characters). Without it the scan stopped at
  // the first byte of a non-ASCII name, and `int e;` was decoded as a field
  // named "int" with the real name lost.
  return cppRdIdentStart(c) || (c >= '0' && c <= '9') ||
         static_cast<unsigned char>(c) >= 0x80;
}

inline std::string cppRdStripNumSuffix(std::string s) {
  // In a hex literal a trailing `f`/`F` is a DIGIT, not the float suffix:
  // stripping it turned `0xFF` into `0x`, which decoded as the string "0x".
  // The check is a plain prefix test -- the loop may have already removed a
  // real suffix (`0xFFu` -> `0xFF`, then the trailing F is kept).
  const bool hex = s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  while (!s.empty()) {
    char c = s.back();
    if ((c == 'f' || c == 'F') && hex) break;
    if (c == 'f' || c == 'F' || c == 'u' || c == 'U' || c == 'l' || c == 'L') s.pop_back();
    else break;
  }
  return s;
}

// Parse a C++ integer literal into the value it denotes: C++14 digit separators
// (`1'000'000`) are removed, the `0b`/`0B` binary prefix is honored, and a
// decimal suffix (`1u`, `2L`) is stripped. Returns false when the text is not a
// valid integer (bad digit, trailing junk such as `08`, or out-of-range), so the
// caller can fall back to preserving the literal spelling as a string.
inline bool cppRdParseIntLiteral(const std::string& raw, long long& out) {
  // Remove C++14 digit separators, then strip any suffix (hex-aware via
  // cppRdStripNumSuffix, so `0xFF` keeps its trailing F digit).
  std::string s = cppRdStripNumSuffix(raw);
  std::string t;
  t.reserve(s.size());
  for (char c : s) if (c != '\'') t += c;
  s = std::move(t);
  if (s.empty()) return false;
  size_t p = 0;
  bool neg = false;
  if (s[0] == '+' || s[0] == '-') { neg = (s[0] == '-'); p = 1; }
  // Binary literal (C++14). strtoll does not accept `0b`, so decode it here.
  if (p + 1 < s.size() && s[p] == '0' && (s[p + 1] == 'b' || s[p + 1] == 'B')) {
    unsigned long long v = 0;
    bool any = false;
    for (size_t j = p + 2; j < s.size(); j++) {
      char c = s[j];
      if (c != '0' && c != '1') return false;
      any = true;
      v = (v << 1) | static_cast<unsigned long long>(c - '0');
    }
    if (!any) return false;
    if (neg) {
      out = (v == (1ull << 63)) ? (std::numeric_limits<long long>::min)()
                                : -static_cast<long long>(v);
    } else {
      if (v > static_cast<unsigned long long>((std::numeric_limits<long long>::max)())) return false;
      out = static_cast<long long>(v);
    }
    return true;
  }
  errno = 0;
  const char* b = s.c_str();
  char* endp = nullptr;
  long long v = std::strtoll(b, &endp, 0);
  if (endp == b || *endp != '\0' || errno == ERANGE) return false;
  out = v;
  return true;
}

// Parse exactly four hex digits (for \uXXXX); returns 0xFFFFFFFF on failure.
inline uint32_t cppRdHex4(std::string_view s) {
  if (s.size() < 4) return 0xFFFFFFFFu;
  uint32_t v = 0;
  for (size_t k = 0; k < 4; k++) {
    char c = s[k];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
    else return 0xFFFFFFFFu;
  }
  return v;
}

inline uint32_t cppRdHex8(std::string_view s) {
  if (s.size() < 8) return 0xFFFFFFFFu;
  uint32_t v = 0;
  for (size_t k = 0; k < 8; k++) {
    char c = s[k];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
    else return 0xFFFFFFFFu;
  }
  // Beyond U+10FFFF, or a surrogate written as \U -- both have no UTF-8 form.
  if (v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)) return 0xFFFFFFFFu;
  return v;
}

// Append a Unicode code point as UTF-8 (for decoded \uXXXX escapes).
inline void cppRdUtf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7F) {
    out += static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Remove comments, keeping string/char literals byte for byte. A #define
// replacement list is formed AFTER comments are deleted, so the macro body is
// its text minus every comment -- truncating at the first `/*`/`//` instead
// would throw away everything after it (`#define MAX /* cap */ 100` became an
// EMPTY body, and `#define U /* c */ "http://x"` lost the string too). Cutting
// also cannot report an unterminated `/*`: it is simply dropped, which is what
// translation phase 3 does. Each comment leaves one space so tokens it
// separated do not merge; a `//` keeps its terminating newline, because a
// multi-line `\` continuation lives inside the same logical line.
inline std::string cppRdStripComments(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  char quote = 0;
  for (size_t i = 0; i < s.size(); i++) {
    const char c = s[i];
    if (quote) {
      out += c;
      if (c == '\\' && i + 1 < s.size()) { out += s[++i]; continue; }
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') { quote = c; out += c; continue; }
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      while (i < s.size() && s[i] != '\n') i++;
      out += ' ';
      if (i < s.size()) out += s[i]; // the '\n' itself
      continue;
    }
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
      if (i + 1 < s.size()) i++; // consume the '/', the for-step takes the rest
      out += ' ';
      continue;
    }
    out += c;
  }
  return out;
}

inline std::vector<Tok> cppRdLex(std::string_view in,
                                 std::map<std::string, std::vector<Tok>>* macros = nullptr) {
  std::vector<Tok> out;
  int line = 1;
  size_t i = 0, n = in.size();
  // Conditional-compilation stack: {branch active, branch already taken}.
  std::vector<std::pair<bool, bool>> cond;
  // True when an enclosing #if/#ifdef/#elif branch is inactive (preprocessor
  // content below it is skipped and #define/#undef there are not honored).
  auto condSkipped = [&]() {
    for (auto& cc : cond) if (!cc.first) return true;
    return false;
  };
  auto condParentActive = [&]() {
    for (size_t ci = 0; ci + 1 < cond.size(); ci++) if (!cond[ci].first) return false;
    return true;
  };
  // Pragmatic #if expression: "0" -> false, "1" -> true, `defined(X)` /
  // `!defined(X)` by the macros seen so far, anything else -> true
  // (conservative: never silently drops live code).
  auto condEval = [&](const std::string& logical, size_t cs) -> bool {
    size_t k = cs;
    while (k < logical.size() && (logical[k] == ' ' || logical[k] == '\t')) k++;
    const std::string expr = logical.substr(k);
    size_t ep = 0;
    // Minimal boolean expression evaluator for #if: `defined X` / `defined(X)`,
    // numeric 0/1 literals, `!`, `&&`, `||`, parentheses. Unknown terms are
    // treated as true (conservative - never silently drops live code).
    std::function<bool()> prim, term, tri;
    prim = [&]() -> bool {
      while (ep < expr.size() && expr[ep] == ' ') ep++;
      if (ep >= expr.size()) return true;
      if (expr[ep] == '(') {
        ep++;
        bool v = tri();
        while (ep < expr.size() && expr[ep] == ' ') ep++;
        if (ep < expr.size() && expr[ep] == ')') ep++;
        return v;
      }
      if (expr[ep] == '!') { ep++; return !prim(); }
      if (expr.compare(ep, 7, "defined") == 0) {
        ep += 7;
        while (ep < expr.size() && expr[ep] == ' ') ep++;
        bool paren = false;
        if (ep < expr.size() && expr[ep] == '(') { paren = true; ep++; }
        while (ep < expr.size() && expr[ep] == ' ') ep++;
        size_t s = ep;
        while (ep < expr.size() && (std::isalnum((unsigned char)expr[ep]) || expr[ep] == '_')) ep++;
        std::string nm = expr.substr(s, ep - s);
        if (paren) {
          while (ep < expr.size() && expr[ep] == ' ') ep++;
          if (ep < expr.size() && expr[ep] == ')') ep++;
        }
        return macros && macros->count(nm) > 0;
      }
      if (std::isdigit((unsigned char)expr[ep])) {
        // Parse the whole integer constant, so `#if 00` is false like `#if 0`
        // instead of falling through to "unknown -> true". C++ suffixes and the
        // 0x/0b base prefixes are recognized; a value is true iff any digit is
        // non-zero (a pure-zero literal of any width is false).
        size_t s = ep;
        while (ep < expr.size() &&
               (std::isalnum((unsigned char)expr[ep]) || expr[ep] == '\'')) ep++;
        const std::string num = expr.substr(s, ep - s);
        size_t d = (num.size() >= 2 && num[0] == '0' &&
                    (num[1] == 'x' || num[1] == 'X' || num[1] == 'b' || num[1] == 'B'))
                     ? 2 : 0;
        for (size_t q = d; q < num.size(); q++) {
          const char ch = num[q];
          if ((ch >= '1' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) return true;
        }
        return false;
      }
      // Unknown token: consume and stay true.
      while (ep < expr.size() && (std::isalnum((unsigned char)expr[ep]) || expr[ep] == '_')) ep++;
      return true;
    };
    term = [&]() -> bool {
      bool v = prim();
      while (true) {
        while (ep < expr.size() && expr[ep] == ' ') ep++;
        if (expr.compare(ep, 2, "&&") == 0) { ep += 2; v = prim() && v; }
        else break;
      }
      return v;
    };
    tri = [&]() -> bool {
      bool v = term();
      while (true) {
        while (ep < expr.size() && expr[ep] == ' ') ep++;
        if (expr.compare(ep, 2, "||") == 0) { ep += 2; v = term() || v; }
        else break;
      }
      return v;
    };
    return tri();
  };
  while (i < n) {
    char c = in[i];
    if (c == '\n') { line++; i++; continue; }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { i++; continue; }
    if (c == '#') {
      // Preprocessor directive: consume the logical line (honoring `\`
      // continuations), track conditional-compilation state, and capture
      // object-like `#define NAME repl` macros only in active branches.
      // Function-like macros (`#define F(x) ...`, with or without a space
      // before the paren) are intentionally not captured -- expanding them
      // would require argument substitution, which is out of scope for this
      // pragmatic reader. They appear as bare identifiers in the token stream.
      size_t j = i;
      std::string logical;
      while (j < n) {
        size_t e = j;
        while (e < n && in[e] != '\n') e++;
        size_t k = e;
        if (k > j && in[k - 1] == '\r') k--;
        bool cont = (k > j && in[k - 1] == '\\');
        size_t segEnd = cont ? k - 1 : e;
        logical.append(in.data() + j, segEnd - j);
        if (cont && e < n) { j = e + 1; line++; continue; }
        j = e;
        break;
      }
      i = j;
      if (macros) {
        size_t q = 0;
        while (q < logical.size() && (logical[q] == ' ' || logical[q] == '\t')) q++;
        if (q < logical.size() && logical[q] == '#') q++;
        while (q < logical.size() && (logical[q] == ' ' || logical[q] == '\t')) q++;
        size_t ds = q;
        while (q < logical.size() && cppRdIdentChar(logical[q])) q++;
        const std::string w = logical.substr(ds, q - ds);
        if (w == "if") {
          bool t = condEval(logical, q);
          bool parentA = !condSkipped();
          bool act = t && parentA;
          cond.push_back({act, act});
        } else if (w == "ifdef" || w == "ifndef") {
          size_t k = q;
          while (k < logical.size() && (logical[k] == ' ' || logical[k] == '\t')) k++;
          // The macro name ends at the first character that cannot be part of
          // an identifier. Taking the rest of the line made `#ifdef FOO //
          // comment` look for a macro named "FOO // comment" - never defined,
          // so the whole branch was skipped and `#ifndef` was always active.
          // `#undef` already scanned the identifier, so the two directives
          // disagreed.
          size_t ne = k;
          while (ne < logical.size() && cppRdIdentChar(logical[ne])) ne++;
          std::string nm = logical.substr(k, ne - k);
          bool d = macros->count(nm) > 0;
          bool t = (w == "ifdef") ? d : !d;
          bool parentA = !condSkipped();
          bool act = t && parentA;
          cond.push_back({act, act});
        } else if (w == "else") {
          if (!cond.empty()) {
            bool parentA = condParentActive();
            bool newA = parentA && !cond.back().second;
            cond.back().second = cond.back().second || newA;
            cond.back().first = newA;
          }
        } else if (w == "elif") {
          if (!cond.empty()) {
            bool parentA = condParentActive();
            bool newA = parentA && !cond.back().second && condEval(logical, q);
            cond.back().second = cond.back().second || newA;
            cond.back().first = newA;
          }
        } else if (w == "endif") {
          if (!cond.empty()) cond.pop_back();
        } else if (w == "define") {
          if (!condSkipped()) {
            while (q < logical.size() && (logical[q] == ' ' || logical[q] == '\t')) q++;
            size_t ns = q;
            while (q < logical.size() && cppRdIdentChar(logical[q])) q++;
            std::string name = logical.substr(ns, q - ns);
            // Function-like detection. Strict C requires '(' to be adjacent to
            // the name, so `#define F (x) ...` is object-like there and expands
            // to the token soup `(x) ...`, which corrupts every use site
            // (`int v = F;` reads as `int v = (x) ...;`). We also accept the
            // spaced spelling as function-like, but only when something trails
            // the parameter list: that is what distinguishes a sloppy
            // `#define F (x) 1` from `#define WIDTH (800)`, whose parenthesized
            // value is the whole replacement and must stay object-like.
            // Adjacent `(` with no trailing content (e.g. `#define ASSERT(x)`)
            // is still function-like: the parameter list is present, it just
            // has an empty body - common for assert stubs.
            bool functionLike = false;
            {
              size_t r = q;
              while (r < logical.size() && (logical[r] == ' ' || logical[r] == '\t')) r++;
              if (r < logical.size() && logical[r] == '(') {
                int pd = 0;
                size_t close = r;
                for (size_t k = r; k < logical.size(); k++) {
                  if (logical[k] == '(') pd++;
                  else if (logical[k] == ')') { pd--; if (pd == 0) { close = k; break; } }
                }
                if (close < logical.size()) {
                  size_t t2 = close + 1;
                  while (t2 < logical.size() && (logical[t2] == ' ' || logical[t2] == '\t' ||
                                                 logical[t2] == '\r')) t2++;
                  // Adjacent `(` (no whitespace between name and paren) is always
                  // function-like, even with an empty body - common for assert stubs.
                  bool adjacent = (r == q);
                  functionLike = adjacent || (t2 < logical.size());
                }
              }
            }
            if (!name.empty() && !functionLike) {
            std::vector<Tok> rtoks;
            while (q < logical.size() && (logical[q] == ' ' || logical[q] == '\t')) q++;
            std::string repl = logical.substr(q);
            while (!repl.empty() &&
                   (repl.back() == ' ' || repl.back() == '\t' || repl.back() == '\r'))
              repl.pop_back();
            if (!repl.empty()) {
              // Strip comments from the replacement list: a `// note` or `/* doc */` around
              // the value is not part of the macro body and must not be
              // re-emitted at every expansion site (where it could attach to a
              // field). Removal, not truncation -- the value may follow it.
              rtoks = cppRdLex(cppRdStripComments(repl), nullptr);
              if (!rtoks.empty() && rtoks.back().kind == Tok::End) rtoks.pop_back();
            }
            (*macros)[name] = std::move(rtoks);
          }
          }
        } else if (w == "undef") {
          if (!condSkipped()) {
            while (q < logical.size() && (logical[q] == ' ' || logical[q] == '\t')) q++;
            size_t ns = q;
            while (q < logical.size() && cppRdIdentChar(logical[q])) q++;
            std::string nm = logical.substr(ns, q - ns);
            if (!nm.empty()) macros->erase(nm);
          }
        }
      }
      continue;
    }
    bool skipTok = condSkipped();
    if (c == '/' && i + 1 < n && in[i + 1] == '/') {
      i += 2;
      size_t s = i;
      int startLine = line;
      // C++ translation phase 2 strips `\` + newline before tokenization, so a
      // line continuation inside a line comment joins the next line into the
      // same comment text. Mirror that behavior.
      while (i < n) {
        // C++ translation phase 1 maps \r\n → \n before phase 2, so both \n
        // and \r\n after a backslash are line continuations.
        if (in[i] == '\\') {
          if (i + 1 < n && in[i + 1] == '\n') { i += 2; line++; continue; }
          if (i + 2 < n && in[i + 1] == '\r' && in[i + 2] == '\n') { i += 3; line++; continue; }
        }
        if (in[i] == '\n') break;
        i++;
      }
      if (!skipTok) {
        Tok t; t.kind = Tok::Comment; t.line = startLine; t.block = false;
        // C++ phase 2 strips `\` + newline in comments too, so the token text
        // should match what the compiler sees — without the backslash and line
        // break. Build it explicitly rather than slicing raw bytes.
        std::string txt;
        for (size_t k = s; k < i; ) {
          if (in[k] == '\\' && k + 1 < i && in[k + 1] == '\n') { k += 2; continue; }
          if (k + 2 <= i && in[k] == '\\' && in[k + 1] == '\r' && in[k + 2] == '\n') { k += 3; continue; }
          txt += in[k++];
        }
        t.text = std::move(txt);
        out.push_back(std::move(t));
      }
      continue;
    }
    if (c == '/' && i + 1 < n && in[i + 1] == '*') {
      i += 2;
      size_t s = i;
      int sl = line;
      while (i + 1 < n && !(in[i] == '*' && in[i + 1] == '/')) { if (in[i] == '\n') line++; i++; }
      if (i + 1 >= n) throw asvJSONError("cpp: unterminated block comment at line " + std::to_string(sl));
      if (!skipTok) {
        Tok t; t.kind = Tok::Comment; t.line = sl; t.block = true;
        t.text.assign(in.data() + s, i - s);
        out.push_back(std::move(t));
      }
      i += 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      char q = c;
      int sl = line;
      i++;
      std::string val;
      while (i < n && in[i] != q) {
        if (in[i] == '\\' && i + 1 < n) {
          char e = in[i + 1];
          i += 2;
          // A backslash before a newline is a line splice: the newline itself
          // is not part of the literal and the string continues on the next
          // line. Advance `line` so error messages stay accurate, and append
          // nothing to the value (the C++ translation phase deletes the pair).
          if (e == '\n') { line++; continue; }
          if (e == '\r') { line++; if (i < n && in[i] == '\n') i++; continue; }
          switch (e) {
            case 'n': val += '\n'; break;
            case 't': val += '\t'; break;
            case 'r': val += '\r'; break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
              // Octal escape: up to three digits (\0 is NUL, \012 is LF, \101
              // is 'A'). Only a bare `case '0'` existed, so \012 decoded as NUL
              // followed by the literal "12" and \101 as "\1" + "01".
              int oct = e - '0', ocnt = 1;
              while (ocnt < 3 && i < n && in[i] >= '0' && in[i] <= '7') {
                oct = oct * 8 + (in[i] - '0'); i++; ocnt++;
              }
              val += static_cast<char>(oct);
              break;
            }
            case '\\': val += '\\'; break;
            case '\'': val += '\''; break;
            case '"': val += '"'; break;
            case 'x': {
              int hv = 0, cnt = 0;
              while (cnt < 2 && i < n) {
                char hc = in[i];
                int d = -1;
                if (hc >= '0' && hc <= '9') d = hc - '0';
                else if (hc >= 'a' && hc <= 'f') d = hc - 'a' + 10;
                else if (hc >= 'A' && hc <= 'F') d = hc - 'A' + 10;
                if (d < 0) break;
                hv = hv * 16 + d; i++; cnt++;
              }
              // \x with zero hex digits is invalid C++ ("\\x" at EOF or "\\xg").
              // cnt == 1 is valid (\x1 = \x01). Append only when at least one
              // digit was consumed, otherwise the silent NUL would corrupt data.
              if (cnt == 0) throw asvJSONError("cpp: invalid \\x escape at line " + std::to_string(sl));
              val += static_cast<char>(hv);
              break;
            }
            case 'U': {
              uint32_t u8 = (i + 8 <= n) ? cppRdHex8(std::string_view(in.data() + i, 8)) : 0xFFFFFFFFu;
              if (u8 == 0xFFFFFFFFu)
                throw asvJSONError("cpp: invalid \\U escape at line " + std::to_string(sl));
              i += 8;
              cppRdUtf8(val, u8);
              break;
            }
            case 'u': {
              uint32_t u1 = (i + 4 <= n) ? cppRdHex4(std::string_view(in.data() + i, 4)) : 0xFFFFFFFFu;
              if (u1 == 0xFFFFFFFFu)
                throw asvJSONError("cpp: invalid \\u escape at line " + std::to_string(sl));
              i += 4;
              uint32_t cp = u1;
              if (u1 >= 0xD800 && u1 <= 0xDBFF) {
                if (i + 6 <= n && in[i] == '\\' && in[i + 1] == 'u') {
                  uint32_t u2 = cppRdHex4(std::string_view(in.data() + i + 2, 4));
                  if (u2 != 0xFFFFFFFFu && u2 >= 0xDC00 && u2 <= 0xDFFF) {
                    cp = 0x10000 + ((u1 - 0xD800) << 10) + (u2 - 0xDC00);
                    i += 6;
                  } else cp = 0xFFFD;
                } else cp = 0xFFFD;
              } else if (u1 >= 0xDC00 && u1 <= 0xDFFF) {
                cp = 0xFFFD;
              }
              cppRdUtf8(val, cp);
              break;
            }
            default: val += e; break;
          }
        } else {
          if (in[i] == '\n') line++;
          val += in[i++];
        }
      }
      if (i >= n) throw asvJSONError("cpp: unterminated string literal at line " + std::to_string(sl));
      i++; // closing quote
      if (!skipTok) {
        Tok t; t.kind = (q == '"' ? Tok::Str : Tok::Char); t.line = sl; t.text = std::move(val);
        out.push_back(std::move(t));
      }
      continue;
    }
    if ((c >= '0' && c <= '9') ||
        (c == '.' && i + 1 < n && in[i + 1] >= '0' && in[i + 1] <= '9')) {
      size_t s = i;
      while (i < n && (cppRdIdentChar(in[i]) || in[i] == '.' ||
             (in[i] == '\'' && i + 1 < n && cppRdIdentChar(in[i + 1])) ||
             ((in[i] == '+' || in[i] == '-') && i > s && (in[i - 1] == 'e' || in[i - 1] == 'E'))))
        i++;
      if (!skipTok) {
        Tok t; t.kind = Tok::Number; t.line = line;
        t.text.assign(in.data() + s, i - s);
        out.push_back(std::move(t));
      }
      continue;
    }
    if (cppRdIdentStart(c)) {
      size_t s = i;
      while (i < n && cppRdIdentChar(in[i])) i++;
      std::string ident(in.data() + s, i - s);
      // Raw string literal: R"delimiter(... )delimiter" (C++11). The form is
      // R"d chars )d" - the opening paren comes AFTER the quote, so the
      // delimiter is read first and the body ends at ')' + delimiter + '"'.
      // The body is taken verbatim (no escape processing) and may span lines.
      if (ident == "R" && i < n && in[i] == '"') {
        size_t sl = line;
        size_t j = i + 1;
        std::string delim;
        // The delimiter is at most 16 chars and may not contain a space, a
        // paren or a backslash (C++11 [lex.string]).
        while (j < n && delim.size() < 16 && in[j] != '(' && in[j] != ')' &&
               in[j] != '\\' && !std::isspace(static_cast<unsigned char>(in[j]))) {
          delim += in[j++];
        }
        if (j < n && in[j] == '(') {
          j++; // '('
          std::string val;
          bool closed = false;
          while (j < n) {
            if (in[j] == ')') {
              size_t k = j + 1;
              bool delimMatch = true;
              for (char dc : delim) {
                if (k >= n || in[k] != dc) { delimMatch = false; break; }
                k++;
              }
              if (delimMatch && k < n && in[k] == '"') {
                j = k + 1;
                closed = true;
                break;
              }
            }
            if (in[j] == '\n') line++;
            val += in[j++];
          }
          if (!closed)
            throw asvJSONError("cpp: unterminated raw string literal at line " + std::to_string(sl));
          i = j;
          if (!skipTok) {
            Tok t; t.kind = Tok::Str; t.line = sl; t.text = std::move(val);
            out.push_back(std::move(t));
          }
          continue;
        }
        // Not a raw string after all (e.g. R"( is a macro call): fall through
        // and emit the identifier as usual.
      }
      if (!skipTok) {
        Tok t; t.kind = Tok::Ident; t.line = line;
        t.text = std::move(ident);
        out.push_back(std::move(t));
      }
      continue;
    }
    if (!skipTok) {
      Tok t; t.kind = Tok::Punct; t.line = line;
      if (c == ':' && i + 1 < n && in[i + 1] == ':') { t.text = "::"; i += 2; }
      else { t.text.assign(1, c); i++; }
      out.push_back(std::move(t));
    } else {
      i++;
    }
  }
  // NOTE: the string/char literal prefixes (L, u8, u, U) are NOT stripped here.
  // A global Ident+literal merge would eat a legitimate identifier that happens
  // to be followed by a string token (an initializer list element `{u, "x"}`).
  // The prefix is removed where a literal is actually expected - in
  // cppRdProcessStringLiterals, which runs on the initializer of one member.
  Tok e; e.kind = Tok::End; e.line = line;
  out.push_back(std::move(e));
  return out;
}

// Expand object-like macros captured from `#define` directives across a token
// stream. `active` guards against recursive (self-referential) macros.
inline std::vector<Tok> cppRdExpandMacros(const std::vector<Tok>& in,
                                          const std::map<std::string, std::vector<Tok>>& macros,
                                          std::set<std::string>& active, int depth) {
  if (depth > 32) return in;
  std::vector<Tok> out;
  out.reserve(in.size());
  for (const Tok& tk : in) {
    if (tk.kind == Tok::Ident) {
      auto it = macros.find(tk.text);
      if (it != macros.end() && !active.count(tk.text)) {
        active.insert(tk.text);
        std::vector<Tok> rep = cppRdExpandMacros(it->second, macros, active, depth + 1);
        active.erase(tk.text);
        for (Tok& r : rep) out.push_back(std::move(r));
        continue;
      }
    }
    out.push_back(tk);
  }
  return out;
}

// Null-node guards for the direct tree decoder. make* is a nothrow `new` that
// returns nullptr on OOM (and makeString above MAX_STRING_LEN), while fromCpp
// builds the tree itself, so a null must become a reported error instead of
// being returned / pushed into an array. A source literal larger than
// MAX_STRING_LEN is reachable without OOM; the rest is defense in depth.
inline std::unique_ptr<asvJSONValue> cppRdNonNull(std::unique_ptr<asvJSONValue> v) {
  if (!v) throw asvJSONError("cpp: out of memory");
  return v;
}

inline std::unique_ptr<asvJSONValue> cppRdString(std::string s) {
  if (!asvJSONValue::checkStringLen(s.size()))
    throw asvJSONError("cpp: string too long");
  auto v = asvJSONValue::makeString(std::move(s));
  if (!v) throw asvJSONError("cpp: out of memory");
  return v;
}

// Concatenate adjacent string literal tokens. C++ concatenates them into one
// value: "a" "b" -> "ab". Also strips string/char literal prefixes (L, u8, u, U).
// Note: R/r raw string literals are handled in the lexer directly.
inline std::vector<Tok> cppRdProcessStringLiterals(std::vector<Tok> init) {
  // First, strip prefixes: merge prefix+literal pairs
  {
    std::vector<Tok> result;
    result.reserve(init.size());
    for (size_t k = 0; k < init.size(); ) {
      const Tok& tk = init[k];
      bool isPrefix = false;
      if (tk.kind == Tok::Ident && (tk.text == "L" || tk.text == "u8" || tk.text == "u" ||
              tk.text == "U")) {
        if (k + 1 < init.size() && (init[k + 1].kind == Tok::Str || init[k + 1].kind == Tok::Char)) {
          isPrefix = true;
        }
      }
      if (isPrefix) {
        result.push_back(init[k + 1]);
        k += 2;
      } else {
        result.push_back(tk);
        k++;
      }
    }
    init = std::move(result);
  }
  // Then, concatenate adjacent string literals
  {
    std::vector<Tok> out;
    out.reserve(init.size());
    for (size_t i = 0; i < init.size(); ) {
      if (init[i].kind == Tok::Str && i + 1 < init.size() && init[i + 1].kind == Tok::Str) {
        std::string combined = init[i].text;
        size_t lastIdx = i;
        while (i + 1 < init.size() && init[i + 1].kind == Tok::Str) {
          i++;
          combined += init[i].text;
          lastIdx = i;
        }
        out.push_back(init[lastIdx]);
        out.back().text = std::move(combined);
      } else {
        out.push_back(init[i]);
      }
      i++;
    }
    init = std::move(out);
  }
  return init;
}

// Interpret a scalar initializer as a single literal: strips balanced outer
// parentheses and a leading unary sign, then reads one number/string/char/
// bool/null literal. Returns nullptr when the tokens are not a literal.
inline std::unique_ptr<asvJSONValue> cppRdScalarLiteral(const std::vector<Tok>& init,
                                                         bool wantDouble) {
  // Process string literals: strip prefixes and concatenate adjacent strings.
  std::vector<Tok> processed = cppRdProcessStringLiterals(init);
  std::vector<const Tok*> v;
  v.reserve(processed.size());
  for (const Tok& tk : processed) if (tk.kind != Tok::Comment) v.push_back(&tk);
  while (v.size() >= 2 && v.front()->kind == Tok::Punct && v.front()->text == "(" &&
         v.back()->kind == Tok::Punct && v.back()->text == ")") {
    int d = 0;
    size_t match = v.size();
    for (size_t k = 0; k < v.size(); k++) {
      if (v[k]->kind != Tok::Punct) continue;
      if (v[k]->text == "(") d++;
      else if (v[k]->text == ")") { d--; if (d == 0) { match = k; break; } }
    }
    if (match != v.size() - 1) break;
    v.erase(v.begin());
    v.pop_back();
  }
  bool neg = false;
  while (!v.empty() && v.front()->kind == Tok::Punct &&
         (v.front()->text == "-" || v.front()->text == "+")) {
    if (v.front()->text == "-") neg = !neg;
    v.erase(v.begin());
  }
  // A functional cast or constructor call: `std::string("hello")`, `char('a')`,
  // `int(5)`, `static_cast<int>(7)`. The paren-strip loop above only fires when
  // the initializer *starts* with '(', so these fell through to the caller's
  // identifier fallback and lost the value (`std::string("hello")` decoded as
  // the string "string"). Peel the trailing call argument list and re-read the
  // literal inside it. The prefix must look like a qualified type or cast name
  // (identifiers, `::`, and a template argument list) so that an expression
  // like `a * (b)` or `(x) + (y)` is left alone.
  if (v.size() >= 3 && v.back()->kind == Tok::Punct && v.back()->text == ")") {
    int d = 0;
    size_t open = v.size();
    for (size_t k = v.size(); k-- > 0;) {
      if (v[k]->kind != Tok::Punct) continue;
      if (v[k]->text == ")") d++;
      else if (v[k]->text == "(") { d--; if (d == 0) { open = k; break; } }
    }
    bool castName = open > 0 && open < v.size() - 1;
    for (size_t k = 0; castName && k < open; k++) {
      if (v[k]->kind == Tok::Ident) continue;
      if (v[k]->kind == Tok::Punct && v[k]->text == "::") continue;
      if (v[k]->kind == Tok::Punct && v[k]->text == "<") {
        int ad = 0;
        for (size_t j = k; j < open; j++) {
          if (v[j]->kind != Tok::Punct) continue;
          if (v[j]->text == "<") ad++;
          else if (v[j]->text == ">") { ad--; if (ad == 0) { k = j; break; } }
        }
        continue;
      }
      castName = false;
    }
    if (castName) {
      std::vector<Tok> inner;
      for (size_t k = open + 1; k + 1 < v.size(); k++) inner.push_back(*v[k]);
      if (!inner.empty())
        if (auto lit = cppRdScalarLiteral(inner, wantDouble)) return lit;
    }
  }
  if (v.size() == 3 && v[0]->kind == Tok::Number && v[1]->kind == Tok::Punct &&
      v[1]->text == "-" && v[2]->kind == Tok::Number) {
    // `(-9223372036854775807LL - 1)` - the toCpp INT64_MIN form. After paren
    // and unary-sign stripping the tokens are NUMBER `-` NUMBER.
    std::string sa = cppRdStripNumSuffix(v[0]->text);
    std::string sb = cppRdStripNumSuffix(v[2]->text);
    bool flt = wantDouble;
    for (char c : sa) if (c == '.' || c == 'e' || c == 'E') flt = true;
    for (char c : sb) if (c == '.' || c == 'e' || c == 'E') flt = true;
    if (flt) {
      try {
        double a = std::stod(sa), b = std::stod(sb);
        return cppRdNonNull(asvJSONValue::makeDouble(neg ? -a : a - b));
      } catch (const asvJSONError&) { throw; } catch (...) { return nullptr; }
    }
    try {
      unsigned long long a = std::stoull(sa, nullptr, 0);
      unsigned long long b = std::stoull(sb, nullptr, 0);
      unsigned long long r = (neg ? (0ull - a) : a) - b;
      return cppRdNonNull(asvJSONValue::makeInt(static_cast<int64_t>(r)));
    } catch (const asvJSONError&) { throw; } catch (...) { return nullptr; }
  }
  if (v.size() != 1) return nullptr;
  const Tok* tk = v[0];
  if (tk->kind == Tok::Str || tk->kind == Tok::Char)
    return cppRdString(std::string(tk->text));
  if (tk->kind == Tok::Number) {
    std::string s = cppRdStripNumSuffix(tk->text);
    bool flt = wantDouble;
    for (char c : s) if (c == '.' || c == 'e' || c == 'E') flt = true;
    if (neg) s.insert(s.begin(), '-');
    if (flt) {
      try { return cppRdNonNull(asvJSONValue::makeDouble(std::stod(s))); }
      catch (const asvJSONError&) { throw; }
      catch (...) { return cppRdString(cppRdStripNumSuffix(tk->text)); }
    }
    // Boundary constant: -9223372036854775808 is valid but not a literal.
    if (s == "-9223372036854775808")
      return cppRdNonNull(asvJSONValue::makeInt((std::numeric_limits<long long>::min)()));
    long long iv = 0;
    if (cppRdParseIntLiteral(s, iv)) return cppRdNonNull(asvJSONValue::makeInt(static_cast<int64_t>(iv)));
    // Overflow or a partially-consumed literal ("08"): preserve the value as a
    // string instead of silently zeroing it.
    return cppRdString(cppRdStripNumSuffix(tk->text));
  }
  if (tk->kind == Tok::Ident) {
    if (tk->text == "true") return cppRdNonNull(asvJSONValue::makeBool(true));
    if (tk->text == "false") return cppRdNonNull(asvJSONValue::makeBool(false));
    if (tk->text == "nullptr" || tk->text == "NULL") return cppRdNonNull(asvJSONValue::makeNull());
  }
  return nullptr;
}

struct TypeRef {
  enum Kind { Scalar, Struct, Enum, Array, Variant, Optional, Map, Unknown } kind = Unknown;
  enum ScalarKind { SNone, SInt, SDouble, SBool, SString, SNull } scalar = SNone;
  std::string name;  // scalar spelling or named type (last identifier)
  std::shared_ptr<TypeRef> elem;
  std::shared_ptr<TypeRef> value;  // map value type
  std::vector<std::shared_ptr<TypeRef>> alts; // variant alternatives
  size_t arrayLen = 0;
  bool hasLen = false;
};

struct FieldDef {
  std::string name;
  TypeRef type;
  std::vector<Tok> init;
  bool hasInit = false;
  std::vector<Comment> leading;
  std::vector<Comment> trailing;
};

struct StructDef {
  std::string name;
  std::vector<FieldDef> fields;
  std::vector<Comment> leading;
  std::vector<Comment> closing;
};

struct EnumDef {
  std::string name;
  std::vector<std::string> members;
  // The comments of the enumerators (parallel to `members`). toCpp() emits
  // them (`//first value` above an enumerator, a same-line trail after it),
  // so the reader has to keep them or the round-trip loses them.
  std::vector<std::vector<Comment>> memberLeading;
  std::vector<std::vector<Comment>> memberTrailing;
};

struct AliasDef {
  std::string name;
  TypeRef type;
  int order = 0;
  // A typedef name for a tagged type (`typedef struct Foo Bar;`). It resolves
  // references to `Bar`, but never drives the root on its own -- otherwise a
  // translation unit with real struct definitions would decode to just the
  // aliased type and lose the struct wrapper.
  bool weak = false;
};

inline bool cppRdIsQual(const std::string& s) {
  return s == "const" || s == "volatile" || s == "static" || s == "constexpr" ||
         s == "consteval" || s == "constinit" || s == "mutable" || s == "inline" ||
         s == "extern" || s == "register" || s == "typename" || s == "struct" ||
         s == "class" || s == "enum" || s == "union" || s == "friend" ||
         s == "virtual" || s == "thread_local";
}

inline TypeRef::ScalarKind cppRdClassify(const std::string& n) {
  if (n == "bool") return TypeRef::SBool;
  if (n == "double" || n == "float") return TypeRef::SDouble;
  if (n == "char" || n == "wchar_t" || n == "char16_t" || n == "char32_t" ||
      n == "string" || n == "string_view" || n == "basic_string")
    return TypeRef::SString;
  if (n == "int" || n == "long" || n == "short" || n == "unsigned" || n == "signed" ||
      n == "size_t" || n == "ssize_t" || n == "time_t" || n == "int8_t" ||
      n == "int16_t" || n == "int32_t" || n == "int64_t" || n == "uint8_t" ||
      n == "uint16_t" || n == "uint32_t" || n == "uint64_t" || n == "intptr_t" ||
      n == "uintptr_t")
    return TypeRef::SInt;
  if (n == "void" || n == "nullptr_t") return TypeRef::SNull;
  return TypeRef::SNone;
}

// Classify a whole type region by the identifiers it contains, so multi-word
// types classify correctly (`signed char` -> int, `unsigned long long` -> int).
// Note: `unsigned char` and `signed char` both set anyInt=true (via "unsigned"
// / "signed") and fall through to SInt. This matches the numeric range of
// those types but means a hand-written `unsigned char flags;` round-trips as
// INT rather than STRING/BINARY -- toCpp() never emits those raw types anyway.
inline TypeRef::ScalarKind cppRdClassifyRegion(const std::vector<const Tok*>& v,
                                                size_t b, size_t e) {
  bool anyInt = false, anyFloat = false, anyChar = false, anyBool = false;
  bool anyStr = false, anyVoid = false;
  for (size_t k = b; k < e; k++) {
    if (v[k]->kind != Tok::Ident) continue;
    const std::string& s = v[k]->text;
    if (s == "bool") anyBool = true;
    else if (s == "float" || s == "double") anyFloat = true;
    else if (s == "char" || s == "wchar_t" || s == "char16_t" || s == "char32_t") anyChar = true;
    else if (s == "string" || s == "string_view" || s == "basic_string") anyStr = true;
    else if (s == "void" || s == "nullptr_t") anyVoid = true;
    else if (s == "int" || s == "long" || s == "short" ||
             s == "signed" || s == "unsigned" || s == "size_t" || s == "ssize_t" ||
             s == "time_t" || s == "int8_t" || s == "int16_t" || s == "int32_t" ||
             s == "int64_t" || s == "uint8_t" || s == "uint16_t" || s == "uint32_t" ||
             s == "uint64_t" || s == "intptr_t" || s == "uintptr_t")
      anyInt = true;
  }
  if (anyBool) return TypeRef::SBool;
  if (anyFloat) return TypeRef::SDouble;
  if (anyInt) return TypeRef::SInt;
  if (anyChar) return TypeRef::SString;
  if (anyStr) return TypeRef::SString;
  if (anyVoid) return TypeRef::SNull;
  return TypeRef::SNone;
}

inline long long cppRdIntToks(const std::vector<const Tok*>& v, size_t b, size_t e) {
  std::string s;
  for (size_t k = b; k < e; k++)
    if (v[k]->kind == Tok::Number) s = v[k]->text;
  if (s.empty()) return -1;
  long long iv = 0;
  return cppRdParseIntLiteral(s, iv) ? iv : -1;
}

// Forward declaration for template recursion.
inline TypeRef cppRdParseTypeV(const std::vector<const Tok*>& v, size_t b, size_t e) {
  TypeRef ty;
  size_t i = b;
  while (i < e && v[i]->kind == Tok::Ident && cppRdIsQual(v[i]->text)) i++;
  if (i + 3 < e && v[i]->kind == Tok::Ident && v[i]->text == "std" &&
      v[i + 1]->kind == Tok::Punct && v[i + 1]->text == "::" &&
      v[i + 2]->kind == Tok::Ident &&
      v[i + 3]->kind == Tok::Punct && v[i + 3]->text == "<") {
    const std::string tn = v[i + 2]->text;
    size_t j = i + 4;
    int d = 1;
    size_t close = e;
    std::vector<std::pair<size_t, size_t>> groups;
    size_t gs = j;
    for (; j < e; j++) {
      if (v[j]->kind != Tok::Punct) continue;
      if (v[j]->text == "<") d++;
      else if (v[j]->text == ">") { d--; if (d == 0) { groups.push_back({gs, j}); close = j; break; } }
      else if (d == 1 && v[j]->text == ",") { groups.push_back({gs, j}); gs = j + 1; }
    }
    if (close == e) { ty.kind = TypeRef::Unknown; ty.name = "std::" + tn; return ty; }
    if (tn == "vector" || tn == "list" || tn == "deque" || tn == "set" || tn == "unordered_set") {
      ty.kind = TypeRef::Array;
      if (!groups.empty()) ty.elem = std::make_shared<TypeRef>(cppRdParseTypeV(v, groups[0].first, groups[0].second));
    } else if (tn == "array") {
      ty.kind = TypeRef::Array;
      if (!groups.empty()) ty.elem = std::make_shared<TypeRef>(cppRdParseTypeV(v, groups[0].first, groups[0].second));
      if (groups.size() >= 2) {
        long long c = cppRdIntToks(v, groups[1].first, groups[1].second);
        if (c >= 0) { ty.arrayLen = static_cast<size_t>(c); ty.hasLen = true; }
      }
    } else if (tn == "variant") {
      ty.kind = TypeRef::Variant;
      for (auto& g : groups) ty.alts.push_back(std::make_shared<TypeRef>(cppRdParseTypeV(v, g.first, g.second)));
    } else if (tn == "optional") {
      ty.kind = TypeRef::Optional;
      if (!groups.empty()) ty.elem = std::make_shared<TypeRef>(cppRdParseTypeV(v, groups[0].first, groups[0].second));
    } else if (tn == "unique_ptr" || tn == "shared_ptr") {
      // Pointer wrappers melt to their pointee so recursive structs (which
      // toCpp() emits as std::unique_ptr<T>) round-trip as values.
      if (!groups.empty()) ty = cppRdParseTypeV(v, groups[0].first, groups[0].second);
      else { ty.kind = TypeRef::Unknown; ty.name = "std::" + tn; }
    } else if (tn == "map" || tn == "unordered_map") {
      ty.kind = TypeRef::Map;
      if (!groups.empty()) ty.elem = std::make_shared<TypeRef>(cppRdParseTypeV(v, groups[0].first, groups[0].second));
      if (groups.size() >= 2) ty.value = std::make_shared<TypeRef>(cppRdParseTypeV(v, groups[1].first, groups[1].second));
    } else if (tn == "pair") {
      // A pair is a fixed 2-element aggregate, not a homogeneous sequence, and
      // keeping only T1 as the element type dropped the second member of
      // `std::pair<int, std::string>`. Use BOTH as alternatives instead, so an
      // initializer still resolves group by group (`p{1,"a"}` -> [1,"a"]), and
      // pin the length at 2 so an uninitialized pair keeps its arity.
      // NOT a Variant node: that decodes `p{1,"a"}` to the first alternative
      // alone (1), losing "a" and the pair shape.
      ty.kind = TypeRef::Array;
      if (groups.size() == 2) {
        ty.arrayLen = 2;
        ty.hasLen = true;
        TypeRef pv;
        pv.kind = TypeRef::Variant;
        for (size_t gi = 0; gi < 2; gi++)
          pv.alts.push_back(std::make_shared<TypeRef>(
            cppRdParseTypeV(v, groups[gi].first, groups[gi].second)));
        ty.elem = std::make_shared<TypeRef>(pv);
      } else if (!groups.empty()) {
        ty.elem = std::make_shared<TypeRef>(cppRdParseTypeV(v, groups[0].first, groups[0].second));
      }
    } else {
      ty.kind = TypeRef::Unknown;
      ty.name = "std::" + tn;
    }
    return ty;
  }
  // Strip leading and trailing qualifiers, in any order. The leading strip is
  // above; the trailing one has to look past pointer/reference tokens, because
  // `Foo const&` (a common C++ style) puts `&` last: a strip that only looks at
  // the final token leaves the range as [Foo, const, &], and the name loop then
  // picks up the *last* identifier in it, which is "const" -- the field is then
  // reported as a scalar of unknown type and materializes to null, silently
  // dropping the value. So alternate: drop trailing quals, then drop trailing
  // `*`/`&`/`&&`, until neither applies.
  size_t et = e;
  for (;;) {
    size_t before = et;
    while (et > i && v[et - 1]->kind == Tok::Ident && cppRdIsQual(v[et - 1]->text)) et--;
    while (et > i && v[et - 1]->kind == Tok::Punct &&
           (v[et - 1]->text == "*" || v[et - 1]->text == "&"))
      et--;
    if (et == before) break;
  }
  std::string name;
  for (size_t k = i; k < et; k++)
    if (v[k]->kind == Tok::Ident) name = v[k]->text;
  ty.name = name;
  TypeRef::ScalarKind sk = cppRdClassifyRegion(v, i, et);
  if (sk != TypeRef::SNone) { ty.kind = TypeRef::Scalar; ty.scalar = sk; }
  else { ty.kind = TypeRef::Unknown; }
  return ty;
}

inline TypeRef cppRdParseType(const std::vector<Tok>& t, size_t b, size_t e) {
  std::vector<const Tok*> v;
  for (size_t k = b; k < e; k++)
    if (t[k].kind != Tok::Comment) v.push_back(&t[k]);
  return cppRdParseTypeV(v, 0, v.size());
}

// Tell a template-argument list apart from a less-than comparison. Real C++
// needs a full parser; the pragmatic rule here is that an angle list must be
// closed by a matching '>' before the current statement ends (a top-level ';',
// '{' or '}'). `std::vector<int> v;` passes, `1 < 2, 3` and `a < b;` do not.
// Misreading a comparison as a template unbalances every depth counter built
// on top of it: the ';' that terminates the declaration stops being
// recognized, so a member swallows the rest of the file.
inline bool cppRdIsTemplateOpen(const std::vector<Tok>& t, size_t k, size_t limit,
                                bool allowComma = true) {
  int d = 0;
  const size_t end = (limit < t.size()) ? limit : t.size();
  for (size_t j = k; j < end; j++) {
    if (t[j].kind != Tok::Punct) continue;
    const std::string& s = t[j].text;
    if (s == "<") d++;
    else if (s == ">") { d--; if (d == 0) return true; if (d < 0) return false; }
    // When splitting an initializer, a comma at angle depth means this was a
    // comparison (`{a < b, c > d}`), not a template argument list: no template
    // opens, so the comma still separates elements. Callers that scan a whole
    // declaration keep the default and let `std::map<int, string>` through.
    else if (s == "," && !allowComma && d >= 1) return false;
    else if (s == ";" || s == "{" || s == "}") return false;
  }
  return false;
}

inline bool cppRdBraceGroup(const std::vector<Tok>& init) {
  size_t b = 0, e = init.size();
  while (b < e && init[b].kind == Tok::Comment) b++;
  while (e > b && init[e - 1].kind == Tok::Comment) e--;
  return e > b && init[b].kind == Tok::Punct && init[b].text == "{" &&
         init[e - 1].kind == Tok::Punct && init[e - 1].text == "}";
}

inline bool cppRdParenGroup(const std::vector<Tok>& init) {
  size_t b = 0, e = init.size();
  while (b < e && init[b].kind == Tok::Comment) b++;
  while (e > b && init[e - 1].kind == Tok::Comment) e--;
  return e > b && init[b].kind == Tok::Punct && init[b].text == "(" &&
         init[e - 1].kind == Tok::Punct && init[e - 1].text == ")";
}

// Split a brace-group body at top-level commas. Only ( ) [ ] { } depth is
// tracked; angle brackets go through cppRdIsTemplateOpen, so a comparison in
// an initializer (`{ 1 < 2, 3 }`) no longer swallows the separator while a
// real template expression still protects its own commas. Comments are kept
// with the group they were written in.
inline std::vector<std::vector<Tok>> cppRdSplitTop(const std::vector<Tok>& body) {
  std::vector<std::vector<Tok>> out;
  std::vector<Tok> cur;
  int pd = 0, ad = 0, bd = 0, brd = 0;
  for (size_t i = 0; i < body.size(); i++) {
    const Tok& tk = body[i];
    if (tk.kind == Tok::Comment) { if (!cur.empty()) cur.push_back(tk); continue; }
    if (tk.kind == Tok::Punct) {
      if (tk.text == "(") pd++;
      else if (tk.text == ")") { if (pd > 0) pd--; }
      else if (tk.text == "<") { if (cppRdIsTemplateOpen(body, i, body.size(), false)) ad++; }
      else if (tk.text == ">") { if (ad > 0) ad--; }
      else if (tk.text == "[") brd++;
      else if (tk.text == "]") { if (brd > 0) brd--; }
      else if (tk.text == "{") bd++;
      else if (tk.text == "}") { if (bd > 0) bd--; }
      else if (tk.text == "," && pd == 0 && ad == 0 && bd == 0 && brd == 0) {
        out.push_back(cur); cur.clear(); continue;
      }
    }
    cur.push_back(tk);
  }
  bool empty = true;
  for (auto& x : cur) if (x.kind != Tok::Comment) { empty = false; break; }
  if (!empty || out.empty()) out.push_back(cur);
  return out;
}

inline std::vector<std::vector<Tok>> cppRdInitGroups(const std::vector<Tok>& init) {
  std::vector<std::vector<Tok>> out;
  size_t b = 0, e = init.size();
  while (b < e && init[b].kind == Tok::Comment) b++;
  if (b >= e || !(init[b].kind == Tok::Punct && init[b].text == "{")) return out;
  int d = 0;
  size_t close = e;
  for (size_t i = b; i < e; i++) {
    if (init[i].kind != Tok::Punct) continue;
    if (init[i].text == "{") d++;
    else if (init[i].text == "}") { d--; if (d == 0) { close = i; break; } }
  }
  if (close == e) return out;
  std::vector<Tok> body(init.begin() + static_cast<long>(b) + 1, init.begin() + static_cast<long>(close));
  out = cppRdSplitTop(body);
  return out;
}

inline std::string cppRdLastIdent(const std::vector<Tok>& toks) {
  std::string r;
  for (auto& tk : toks) {
    if (tk.kind != Tok::Ident) continue;
    if (cppIsKeyword(tk.text) && tk.text != "true" && tk.text != "false" && tk.text != "nullptr")
      continue;
    r = tk.text;
  }
  return r;
}

inline const Tok* cppRdFirstVal(const std::vector<Tok>& toks) {
  for (auto& tk : toks) { if (tk.kind == Tok::Comment) continue; return &tk; }
  return nullptr;
}

struct Reader {
  const std::vector<Tok>& t;
  size_t p = 0;
  const CppReadOptions& opts;
  std::vector<StructDef> structs;
  std::vector<EnumDef> enums;
  std::vector<AliasDef> aliases;
  // Variables declared alongside a file-scope struct: `struct Foo {...} a, b;`
  std::vector<FieldDef> fileScope;
  std::vector<Comment> pending;
  // The source line of every pending comment (parallel to `pending`).
  std::vector<int> pendingLines;
  int order = 0;

  Reader(const std::vector<Tok>& toks, const CppReadOptions& o) : t(toks), opts(o) {}

  // --- token helpers ---
  const Tok& cur() const { return t[p]; }
  bool eof() const { return t[p].kind == Tok::End; }
  bool isP(const char* s) const { return t[p].kind == Tok::Punct && t[p].text == s; }
  bool isKw(const char* s) const { return t[p].kind == Tok::Ident && t[p].text == s; }
  void adv() { if (t[p].kind != Tok::End) p++; }

  void skipTrivia() {
    while (t[p].kind == Tok::Comment) {
      if (opts.includeComments && t[p].text.find("Generated by asvJSON++") == std::string::npos) {
        pending.push_back(Comment{t[p].text, t[p].block});
        pendingLines.push_back(t[p].line);
      }
      p++;
    }
  }

  std::vector<Comment> takePending() {
    std::vector<Comment> r = std::move(pending);
    pending.clear();
    pendingLines.clear();
    return r;
  }

  // Take the pending comments together with their source lines. The line
  // tells a comment that TRAILS the previous enumerator (it follows that
  // enumerator's comma) from one that LEADS the next one.
  std::vector<Comment> takePendingLines(std::vector<int>& lines) {
    std::vector<Comment> r = std::move(pending);
    lines = std::move(pendingLines);
    pending.clear();
    pendingLines.clear();
    return r;
  }

  void skipBalanced(const char* o, const char* c) {
    adv();
    int d = 1;
    while (!eof() && d > 0) {
      if (isP(o)) d++;
      else if (isP(c)) d--;
      adv();
    }
  }

  void skipToSemi() {
    while (!eof() && !isP(";")) {
      if (isP("{")) { skipBalanced("{", "}"); continue; }
      adv();
    }
    if (isP(";")) adv();
  }

  void skipTemplateParams() {
    adv(); // template
    skipTrivia();
    if (isP("<")) skipBalanced("<", ">");
  }

  void skipBases() {
    adv(); // ':'
    int angle = 0, paren = 0;
    while (!eof()) {
      if (isP("<")) angle++;
      else if (isP(">")) { if (angle > 0) angle--; }
      else if (isP("(")) paren++;
      else if (isP(")")) { if (paren > 0) paren--; }
      else if (angle == 0 && paren == 0 && (isP("{") || isP(";") || isP("="))) return;
      adv();
    }
  }

  bool looksLikeNestedDef() const {
    if (!(t[p].kind == Tok::Ident &&
          (t[p].text == "struct" || t[p].text == "class" || t[p].text == "enum" ||
           t[p].text == "union")))
      return false;
    size_t q = p + 1;
    int ang = 0;
    while (q < t.size()) {
      const Tok& tk = t[q];
      if (tk.kind == Tok::Comment || tk.kind == Tok::End) { q++; if (tk.kind == Tok::End) return false; continue; }
      if (tk.kind == Tok::Punct) {
        if (tk.text == "<") ang++;
        else if (tk.text == ">") { if (ang > 0) ang--; }
        else if (ang == 0) {
          if (tk.text == "{") return true;
          if (tk.text == ";" || tk.text == "=" || tk.text == "(") return false;
        }
      }
      q++;
    }
    return false;
  }

  const StructDef* findStruct(const std::string& n) const {
    for (auto& s : structs) if (s.name == n) return &s;
    return nullptr;
  }
  const EnumDef* findEnum(const std::string& n) const {
    for (auto& e : enums) if (e.name == n) return &e;
    return nullptr;
  }

  void registerStruct(StructDef def) {
    if (def.name.empty()) return;
    for (auto& s : structs) {
      if (s.name == def.name) {
        if (!def.fields.empty()) s = std::move(def);
        return;
      }
    }
    structs.push_back(std::move(def));
  }

  // --- parsing ---
  void parseAll(int depth = 0) {
    if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
      throw asvJSONError("cpp: namespace nesting too deep");
    while (!eof()) {
      skipTrivia();
      if (eof()) break;
      if (cur().kind == Tok::Ident) {
        const std::string s = cur().text;
        if (s == "template") { skipTemplateParams(); continue; }
        if ((s == "struct" || s == "class" || s == "union") && looksLikeNestedDef()) {
          for (auto& f : parseStructDecl()) fileScope.push_back(std::move(f));
          continue;
        }
        if (s == "enum" && looksLikeNestedDef()) {
          parseEnumDecl();
          // A file-scope variable of an enum type (`enum E { A, B } var;`) is a
          // declaration like any other.
          if (!enumDeclVar_.empty()) {
            FieldDef ef;
            ef.name = enumDeclVar_;
            TypeRef et;
            et.kind = TypeRef::Enum;
            et.name = enumDeclName_;
            ef.type = et;
            // The comments between the closing brace and the declarator lead
            // the declarator.
            ef.leading = takePending();
            ef.hasInit = enumDeclHasInit_;
            ef.init = enumDeclInit_;
            fileScope.push_back(std::move(ef));
          }
          // NOTE: a standalone enum is a TYPE declaration, not a value - its
          // variants materialize where the type is used (`std::array<E,3> v`
          // -> the variant names). So no root member is created here: the
          // enum-map mechanism (see the "modes" case in testFromCpp) owns that.
          continue;
        }
        if (s == "typedef") { parseTypedef(); continue; }
        if (s == "using") { parseUsing(); continue; }
        if (s == "namespace") {
          adv(); skipTrivia();
          // Skip an optional nested-name (`namespace a::b`) up to the body.
          while (!eof() && !isP("{") && !isP(";")) adv();
          if (isP("{")) parseSubScope(depth);
          continue;
        }
        if (s == "extern") {
          // `extern "C" { ... }` linkage blocks: recurse so their declarations
          // are not discarded. A plain `extern` declaration has no `{`.
          const size_t save = p;
          adv(); skipTrivia();
          if (cur().kind == Tok::Str) { adv(); skipTrivia(); }
          if (isP("{")) parseSubScope(depth);
          else p = save + 1;
          continue;
        }
      }
      if (isP("{")) { skipBalanced("{", "}"); continue; }
      if (isP(";")) { adv(); continue; }
      // A file-scope variable declaration (`int g = 5;`, `std::string s = "x";`,
      // `int g[3] = {1,2,3};`) is a declaration too. The top level used to skip
      // such tokens one by one, so a file with only globals decoded to "no type
      // declarations found" and the values were lost.
      if (cppRdLooksLikeFileScopeDecl()) {
        StructDef tmp;
        const size_t save = p;
        parseMember(tmp);
        if (p == save) { adv(); continue; }  // no progress -> skip one token
        for (auto& f : tmp.fields) fileScope.push_back(std::move(f));
        continue;
      }
      adv();
    }
  }

  // True when the tokens at the current position look like a file-scope
  // variable declaration: a type, a declarator name and then `=`, `;`, `[` or
  // `,`. A leading C++ keyword means it is a type declaration or a statement,
  // not a variable.
  bool cppRdLooksLikeFileScopeDecl() {
    if (cur().kind != Tok::Ident) return false;
    // A leading declaration/statement keyword means this is a type
    // declaration or a statement, not a variable. A TYPE keyword (int,
    // double, ...) is fine - that is exactly a variable declaration.
    static const char* declKws[] = {
      "struct", "class", "enum", "typedef", "using", "namespace", "template",
      "extern", "friend", "public", "private", "protected", "static_assert",
      "operator", "return", "if", "else", "for", "while", "do", "switch",
      "case", "break", "continue", "goto", "try", "catch", "throw", "delete",
      "new", "sizeof", "alignof", "decltype", "typename"
    };
    for (auto* kw : declKws) if (cur().text == kw) return false;

    // A qualified type name: `ident :: ident` (with an optional template
    // argument list) - `std::string s = "x";`.
    {
      size_t q = p + 1;
      while (q < t.size() && t[q].kind == Tok::Comment) q++;
      if (q + 1 < t.size() && t[q].kind == Tok::Punct && t[q].text == "::" &&
          t[q + 1].kind == Tok::Ident) {
        size_t e = q + 2;
        if (e < t.size() && t[e].kind == Tok::Punct && t[e].text == "<") {
          int d = 0;
          size_t r = e;
          for (; r < t.size(); ++r) {
            if (t[r].kind != Tok::Punct) continue;
            if (t[r].text == "<") d++;
            else if (t[r].text == ">") { d--; if (d == 0) { r++; break; } }
          }
          if (r >= t.size()) return false;
          e = r;
        }
        while (e < t.size() && t[e].kind == Tok::Comment) e++;
        if (e >= t.size() || t[e].kind != Tok::Ident) return false;
        e++;
        while (e < t.size() && t[e].kind == Tok::Comment) e++;
        if (e >= t.size() || t[e].kind != Tok::Punct) return false;
        return t[e].text == "=" || t[e].text == ";" || t[e].text == "[" || t[e].text == ",";
      }
    }

    // A plain type: one or more type keywords - `unsigned long long x = 1;`.
    static const char* typeKws[] = {
      "int", "char", "long", "short", "signed", "unsigned", "float", "double",
      "bool", "void", "size_t", "wchar_t", "char16_t", "char32_t"
    };
    size_t k = p;
    size_t kwCount = 0;
    while (k < t.size()) {
      while (k < t.size() && t[k].kind == Tok::Comment) k++;
      if (k >= t.size() || t[k].kind != Tok::Ident) break;
      bool isKw = false;
      for (auto* kw : typeKws) if (t[k].text == kw) { isKw = true; break; }
      if (!isKw) break;
      kwCount++;
      k++;
    }
    if (kwCount == 0) return false;
    while (k < t.size() && t[k].kind == Tok::Comment) k++;
    if (k >= t.size() || t[k].kind != Tok::Ident) return false;
    k++;
    while (k < t.size() && t[k].kind == Tok::Comment) k++;
    if (k >= t.size() || t[k].kind != Tok::Punct) return false;
    return t[k].text == "=" || t[k].text == ";" || t[k].text == "[" || t[k].text == ",";
  }

  // Parse the `{ ... }` scope starting at the current `{` with a fresh reader
  // and fold its declarations into this one. Namespace and `extern "C"` bodies
  // used to be skipped wholesale, silently dropping every type they held.
  void parseSubScope(int depth) {
    const size_t open = p;
    int d = 0;
    size_t close = t.size();
    for (size_t k = open; k < t.size(); k++) {
      if (t[k].kind != Tok::Punct) continue;
      if (t[k].text == "{") d++;
      else if (t[k].text == "}") { d--; if (d == 0) { close = k; break; } }
    }
    if (close == t.size()) { skipBalanced("{", "}"); return; } // unterminated
    std::vector<Tok> sub(t.begin() + static_cast<std::ptrdiff_t>(open) + 1,
                         t.begin() + static_cast<std::ptrdiff_t>(close));
    sub.push_back(Tok()); // End sentinel (Tok's default kind is End)
    Reader inner(sub, opts);
    inner.parseAll(depth + 1);
    mergeFrom(inner);
    p = close + 1;
  }

  void mergeFrom(Reader& other) {
    for (auto& s : other.structs) registerStruct(std::move(s));
    for (auto& e : other.enums) {
      bool dup = false;
      for (auto& x : enums) if (x.name == e.name) { dup = true; break; }
      if (!dup) enums.push_back(std::move(e));
    }
    for (auto& a : other.aliases) { a.order = order++; aliases.push_back(std::move(a)); }
    for (auto& f : other.fileScope) fileScope.push_back(std::move(f));
  }

  // Returns the "extra declarators" declared after the closing brace
  // (e.g. `struct Foo { ... } a, b;` -> fields a and b).
  // Skip C++20-style bracket attributes ([...] or [[...]]) that may appear
  // between 'struct'/'class' and the identifier (e.g. `struct [[nodiscard]] Foo`).
  void skipBracketAttr() {
    if (!isP("[")) return;
    adv(); // '['
    int d = 1;
    while (!eof() && d > 0) {
      if (isP("[")) d++;
      else if (isP("]")) d--;
      adv();
    }
  }

  std::vector<FieldDef> parseStructDecl(int depth = 0) {
    // depth starts at 0 for the outermost struct. checkNestingDepth uses depth
    // starting at 1 (parse() increments first), so depth >= MAX means we're
    // about to create the (MAX+1)-th container — reject. Allows exactly MAX
    // levels (depth 0..MAX-1).
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
      throw asvJSONError("cpp: struct nesting too deep");
    std::vector<Comment> lead = takePending();
    adv(); // struct / class
    skipTrivia();
    // Skip any bracket attributes: [[nodiscard]], __attribute__((...)), etc.
    while (isP("[")) skipBracketAttr();
    skipTrivia();
    std::string name;
    if (cur().kind == Tok::Ident) { name = cur().text; adv(); }
    skipTrivia();
    // `final` closes a class head (C++11). It is an ordinary identifier token,
    // so without this the `{` check below missed and skipToSemi() ate the whole
    // body -- `struct Foo final { int x; };` decoded to a null member, and a
    // `struct Document final { ... }` root vanished with "no type declarations".
    if (isKw("final")) { adv(); skipTrivia(); }
    if (isP(":")) { skipBases(); skipTrivia(); }
    if (isP(";")) { adv(); return {}; } // forward declaration
    if (!isP("{")) { skipToSemi(); return {}; }
    StructDef def;
    def.name = name;
    def.leading = std::move(lead);
    // depth here is the caller's depth; the body is at the same level (the
    // struct declaration itself counts as one container, checked above). The
    // nested struct inside the body gets depth + 1. This keeps the increment
    // at 1 per nesting level, matching parse() semantics.
    return parseStructBody(def, depth);
  }

  std::vector<FieldDef> parseStructBody(StructDef& def, int depth = 0) {
    adv(); // '{'
    while (!eof()) {
      skipTrivia();
      if (isP("}")) {
        def.closing = takePending();
        adv();
        std::vector<Tok> trail;
        int d2 = 0;
        while (!eof()) {
          if (cur().kind == Tok::Punct) {
            const std::string& s2 = cur().text;
            if (s2 == "{" || s2 == "(" || s2 == "[") d2++;
            else if (s2 == "}" || s2 == ")" || s2 == "]") { if (d2 == 0) break; d2--; }
            else if (s2 == ";" && d2 == 0) break;
          }
          trail.push_back(cur());
          adv();
        }
        if (isP(";")) adv();
        std::vector<FieldDef> extras;
        if (def.name.empty()) {
          // `struct { ... } Foo, Bar;` or `typedef struct { ... } Foo, Bar;` -
          // the first Ident becomes the tag name, subsequent Idents are extra
          // declarators (weak aliases when coming through a typedef). Collect
          // them so callers that know the context can register them.
          bool gotName = false;
          for (auto& tk : trail) {
            if (tk.kind == Tok::Ident) {
              if (!gotName) { def.name = tk.text; gotName = true; }
              else {
                FieldDef f;
                f.name = tk.text;
                f.type.kind = TypeRef::Unknown;
                f.type.name = def.name;
                extras.push_back(std::move(f));
              }
            }
          }
        } else {
          // `struct Foo { ... } a, b, c = {};` - one declarator per
          // top-level comma-separated segment.
          std::vector<std::pair<size_t, size_t>> segs;
          size_t segStart = 0, dP = 0, dA = 0, dB = 0, dBr = 0;
          for (size_t k = 0; k < trail.size(); k++) {
            if (trail[k].kind != Tok::Punct) continue;
            const std::string& s2 = trail[k].text;
          if (s2 == "(") dP++;
              else if (s2 == ")") { if (dP > 0) dP--; }
              else if (s2 == "<") { if (cppRdIsTemplateOpen(trail, k, trail.size())) dA++; }
              else if (s2 == ">") { if (dA > 0) dA--; }
            else if (s2 == "[") dBr++;
            else if (s2 == "]") { if (dBr > 0) dBr--; }
            else if (s2 == "{") dB++;
            else if (s2 == "}") { if (dB > 0) dB--; }
            else if (s2 == "," && dP == 0 && dA == 0 && dB == 0 && dBr == 0) {
              segs.push_back({segStart, k}); segStart = k + 1;
            }
          }
          segs.push_back({segStart, trail.size()});
          for (auto& [sb, se] : segs) {
            if (sb >= se) continue;
            size_t eq2 = static_cast<size_t>(-1);
            for (size_t k = sb; k < se; k++)
              if (trail[k].kind == Tok::Punct && trail[k].text == "=") { eq2 = k; break; }
            size_t nameI = static_cast<size_t>(-1);
            size_t lim = (eq2 == static_cast<size_t>(-1)) ? se : eq2;
            for (size_t k = lim; k > sb; k--)
              if (trail[k - 1].kind == Tok::Ident) { nameI = k - 1; break; }
            if (nameI == static_cast<size_t>(-1)) continue;
            FieldDef f;
            f.name = trail[nameI].text;
            f.type.kind = TypeRef::Unknown;
            f.type.name = def.name;
            // Parse the array suffix `[N]` (and `[N][M]`) after the name,
            // mirroring parseMember. The dimensions are collected in order and
            // the type is built from the inside out, so `int m[2][3]` is an
            // array of 2 rows of 3 - before, the second suffix overwrote the
            // first and the row count came out as 3.
            std::vector<size_t> dims;
            for (size_t k = nameI + 1; k < se; k++) {
              if (trail[k].kind == Tok::Punct && trail[k].text == "[") {
                size_t j = k + 1;
                while (j < se && trail[j].kind == Tok::Comment) j++;
                if (j < se && trail[j].kind == Tok::Punct && trail[j].text == "-")
                  throw asvJSONError("cpp: negative array size");
                std::string num;
                while (j < se && !(trail[j].kind == Tok::Punct && trail[j].text == "]")) {
                  if (trail[j].kind == Tok::Number) num = trail[j].text;
                  j++;
                }
                if (!num.empty()) {
                  try { dims.push_back(static_cast<size_t>(std::stoull(num))); } catch (...) {}
                }
                k = (j < se) ? j : se;
              } else if (trail[k].kind == Tok::Punct && trail[k].text == "=") {
                f.hasInit = true;
                for (size_t m = k + 1; m < se; m++) f.init.push_back(trail[m]);
                break;
              }
            }
            if (!dims.empty()) {
              TypeRef ty = f.type;
              for (size_t d = dims.size(); d-- > 0;) {
                TypeRef a;
                a.kind = TypeRef::Array;
                a.elem = std::make_shared<TypeRef>(ty);
                a.arrayLen = dims[d];
                a.hasLen = true;
                ty = a;
              }
              f.type = ty;
            }
            extras.push_back(std::move(f));
          }
        }
        registerStruct(std::move(def));
        return extras;
      }
      if (isP(";")) { adv(); continue; }
      if (cur().kind == Tok::Ident) {
        const std::string s = cur().text;
        // Treat `public`/`private`/`protected` as access-specifiers only when
        // followed by `:` -- otherwise they could be a field name in a
        // declaration like `int public;` (invalid C++, but we should not mis-parse).
        if (s == "public" || s == "private" || s == "protected") {
          const size_t save = p;
          adv(); skipTrivia();
          if (isP(":")) { adv(); continue; }
          // Not an access specifier after all (`public int x;`, or a field
          // literally named `public`). Rewind to the token start -- not by one
          // token -- so the ident AND any trivia skipped above are seen again,
          // then fall through to parseMember(). A one-token rewind followed by
          // `continue` re-examined the same ident forever and hung.
          p = save;
        }
        if (s == "template") { skipTemplateParams(); continue; }
        if (s == "typedef") { parseTypedef(); continue; }
        if (s == "using") { parseUsing(); continue; }
        if (s == "friend" || s == "static_assert") { skipToSemi(); continue; }
        if (looksLikeNestedDef()) {
          if (s == "enum") {
            enumDeclVar_.clear();
            parseEnumDecl();
            if (!enumDeclVar_.empty()) {
              // `enum E { A, B } e;` - the enum AND a member of that type.
              // The member was swallowed by the enum declaration before.
              FieldDef ef;
              ef.name = enumDeclVar_;
              TypeRef et;
              et.kind = TypeRef::Enum;
              et.name = enumDeclName_;
              ef.type = et;
              // The comments between the closing brace and the declarator lead
              // the member.
              ef.leading = takePending();
              ef.hasInit = enumDeclHasInit_;
              ef.init = enumDeclInit_;
              def.fields.push_back(std::move(ef));
            }
          }
          else {
            auto flds = parseStructDecl(depth + 1);
            for (auto& f : flds) def.fields.push_back(std::move(f));
          }
          continue;
        }
      }
      parseMember(def);
    }
    // EOF before the closing '}'. Every other exit returns from inside the loop,
    // so reaching here means the struct body was truncated: `def` was never
    // registered and all of its fields are silently lost. Report it instead of
    // dropping the declaration (an unterminated block comment behaves the same
    // way, in strict and non-strict mode alike -- a truncated header is never
    // valid C++, so there is no tree to salvage).
    throw asvJSONError("cpp: unterminated struct body for '" +
                       (def.name.empty() ? std::string("<anonymous>") : def.name) + "'");
  }

  void parseMember(StructDef& def) {
    std::vector<Comment> lead = takePending();
    const size_t npos = static_cast<size_t>(-1);
    size_t start = p, i = p;
    int paren = 0, ang = 0, brk = 0, brace = 0;
    bool sawEq = false, sawParen = false, sawFnPtr = false, methodBody = false;
    size_t eqPos = npos, end = npos;
    std::vector<size_t> commas;
    bool parsedAny = false;
    while (i < t.size()) {
      const Tok& tk = t[i];
      if (tk.kind == Tok::End) break;
      if (tk.kind == Tok::Comment) { i++; continue; }
      if (tk.kind == Tok::Ident && tk.text == "operator" &&
          paren == 0 && ang == 0 && brk == 0 && brace == 0) {
        // An operator function declaration (`bool operator==(const S&) const;`).
        // The operator symbol is lexed as separate punctuation, so the `=` of
        // `==` would otherwise look like an initializer: the member was decoded
        // as a field named "operator" with a bool value. `operator` is a keyword
        // in C++, so it can never be a field name - the whole declaration is a
        // function and is skipped like a method.
        size_t q = i + 1;
        while (q < t.size() && t[q].kind == Tok::Comment) q++;
        bool isOperator = false;
        if (q < t.size() && t[q].kind == Tok::Punct) {
          const std::string& op = t[q].text;
          isOperator = (op == "(" || op == "[") ||
                       (op.size() == 1 &&
                        std::string("=!<>+-*/%&|^~").find(op[0]) != std::string::npos);
        }
        if (isOperator) {
          size_t k = i;
          int dp = 0, db = 0, dc = 0;
          while (k < t.size() && t[k].kind != Tok::End) {
            if (t[k].kind == Tok::Punct) {
              if (t[k].text == "(") dp++;
              else if (t[k].text == ")") { if (dp > 0) dp--; }
              else if (t[k].text == "[") db++;
              else if (t[k].text == "]") { if (db > 0) db--; }
              else if (t[k].text == "{") dc++;
              else if (t[k].text == "}") { if (dc > 0) dc--; }
              else if (t[k].text == ";" && dp == 0 && db == 0 && dc == 0) { k++; break; }
            }
            k++;
          }
          p = k;
          return;
        }
      }
      if (tk.kind == Tok::Punct) {
        const std::string& s = tk.text;
        if (s == "(") {
          if (paren == 0 && ang == 0 && brk == 0 && brace == 0) {
            // A function-pointer declarator opens with `(` immediately followed
            // by `*` (`int (*fp)(int)`). Its parens must NOT mark the member as
            // a function declaration, or the whole member is dropped below as a
            // method. Everything after the declarator is then ignored by the
            // normal segment scan, which already picks the name inside `(*...)`.
            size_t q = i + 1;
            while (q < t.size() && t[q].kind == Tok::Comment) q++;
            if (q < t.size() && t[q].kind == Tok::Punct && t[q].text == "*") sawFnPtr = true;
            else if (!sawFnPtr) sawParen = true;
          }
          paren++;
        }
        else if (s == ")") { if (paren > 0) paren--; }
        else if (s == "<") { if (cppRdIsTemplateOpen(t, i, t.size())) ang++; }
        else if (s == ">") { if (ang > 0) ang--; }
        else if (s == "[") brk++;
        else if (s == "]") { if (brk > 0) brk--; }
        else if (s == "{") {
          if (paren == 0 && ang == 0 && brk == 0) {
            // A bare `{` without a preceding `=` or `(` is brace-init (e.g.
            // `Foo bar{5};`, `int arr[3]{1,2,3};`), not a method body.  Only
            // treat it as a method body when `(` was seen earlier (`sawParen`).
            if (!sawParen && !sawEq) brace++;
            else if (sawEq) brace++;
            else {
              int d = 1; i++;
              while (i < t.size() && d > 0 && t[i].kind != Tok::End) {
                if (t[i].kind == Tok::Punct && t[i].text == "{") d++;
                else if (t[i].kind == Tok::Punct && t[i].text == "}") d--;
                i++;
              }
              methodBody = true;
              while (i < t.size() && t[i].kind == Tok::Comment) i++;
              if (i < t.size() && t[i].kind == Tok::Punct && t[i].text == ";") i++;
              break;
            }
          } else brace++;
        }
        else if (s == "}") { if (brace > 0) brace--; else break; }
        else if (s == "," && paren == 0 && ang == 0 && brk == 0 && brace == 0) {
          if (commas.empty() || commas.back() != i) commas.push_back(i);
        }
        else if (s == ";" && paren == 0 && ang == 0 && brk == 0 && brace == 0) { end = i; break; }
        else if (s == "=" && paren == 0 && ang == 0 && brk == 0 && brace == 0 && !sawEq) { sawEq = true; eqPos = i; }
      }
      i++;
    }
    if (methodBody) { p = i; return; }
    // `int x(5);` / `std::string s("hello");` - direct-initialization of a
    // variable, not a method declaration. Distinguish by looking inside the
    // first paren: if it contains only literals/idents/punct (no type-like
    // keywords) and is followed by `;` or `,`, treat as data member with init.
    size_t directInitPi = npos, directInitPc = npos;
    if (sawParen && !sawEq) {
      bool looksLikeDirectInit = false;
      size_t pi = npos, pc = npos;
      {
        // The group is the FIRST top-level paren group - the parameter list of
        // a method or the argument list of a direct-init. Taking the LAST "("
        // before the first ")" picked the inner group of a function-pointer
        // parameter (`void f(void (*cb)(int));` -> the group `(* cb`), which
        // holds no type keyword and was therefore read as a direct-init: the
        // member became a bogus field named after a parameter type.
        int pdepth = 0;
        for (size_t k = start; k < t.size(); k++) {
          // Skip [[...]] attribute regions first: the parentheses of
          // `int x [[deprecated("y")]](5);` belong to the attribute, while
          // sawParen was set by the real (5) direct-init outside it.
          if (t[k].kind == Tok::Punct && t[k].text == "[" &&
              k + 1 < t.size() && t[k + 1].kind == Tok::Punct && t[k + 1].text == "[") {
            int attrDepth = 0;
            while (k < t.size()) {
              if (t[k].kind == Tok::Punct && t[k].text == "[") attrDepth++;
              else if (t[k].kind == Tok::Punct && t[k].text == "]") {
                attrDepth--;
                if (attrDepth == 0) break;
              }
              k++;
            }
            continue; // the loop's own k++ steps past the closing ']'
          }
          if (t[k].kind == Tok::Punct && t[k].text == "(") {
            if (pi == npos) { pi = k; pdepth = 1; }
            else if (pdepth > 0) pdepth++;
          }
          else if (t[k].kind == Tok::Punct && t[k].text == ")") {
            if (pi == npos) continue;
            if (pdepth > 0) {
              pdepth--;
              if (pdepth == 0) { pc = k; break; }
            }
          }
        }
        if (pi != npos && pc != npos && pc > pi + 1) {
          bool hasTypeKeyword = false;
          static const char* typeKws[] = {"int","char","float","double","bool","auto",
            "void","long","short","signed","unsigned","const","volatile","static","extern",
            "struct","class","enum","union","typedef","template","typename","namespace",
            "public","private","protected","virtual","explicit","friend","register","mutable"};
          for (size_t k = pi + 1; k < pc; k++) {
            if (t[k].kind == Tok::Ident) {
              for (auto* kw : typeKws) {
                if (t[k].text == kw) { hasTypeKeyword = true; break; }
                if (!hasTypeKeyword && t[k].text.size() > 2 &&
                    (t[k].text[0] >= 'A' && t[k].text[0] <= 'Z')) {
                  // CamelCase ident at this position is likely a type name.
                  hasTypeKeyword = true; break;
                }
              }
              if (hasTypeKeyword) break;
            }
          }
          bool endsSep = (end != npos) ||
                         (pc + 1 < t.size() && t[pc + 1].kind == Tok::Punct &&
                          (t[pc + 1].text == "," || t[pc + 1].text == ";"));
          looksLikeDirectInit = !hasTypeKeyword && endsSep;
        }
      }
      if (looksLikeDirectInit) {
        directInitPi = pi;
        directInitPc = pc;
      } else {
        p = (end == npos) ? i : end + 1; return;
      }
    }
    if (sawParen && sawEq) {
      // `void f() = default;` / `= delete;`, and pure virtual `= 0`, are
      // function declarations, not data members. Without this check the outer
      // scan saw merely "paren then equals" and decoded the member as a field
      // named after the function, initialized to "default"/"delete"/0.
      size_t q = (eqPos == npos) ? i : eqPos + 1;
      while (q < t.size() && t[q].kind == Tok::Comment) q++;
      if (q < t.size() &&
          ((t[q].kind == Tok::Ident && (t[q].text == "default" || t[q].text == "delete")) ||
           (t[q].kind == Tok::Number && t[q].text == "0"))) {
        p = (end == npos) ? i : end + 1;
        return;
      }
    }
    size_t stop = (end == npos) ? i : end;
    // Split the declaration at top-level commas into one segment per
    // declarator (`int a = 1, b = 2;` -> [a], [b]).
    std::vector<std::pair<size_t, size_t>> segs;
    if (commas.empty()) {
      segs.push_back({start, stop});
    } else {
      size_t s0 = start;
      for (size_t c : commas) { segs.push_back({s0, c}); s0 = c + 1; }
      segs.push_back({s0, stop});
    }
    TypeRef base;
    bool baseSet = false;
    for (size_t si = 0; si < segs.size(); si++) {
      const size_t sb = segs[si].first;
      const size_t se = segs[si].second;
      if (sb >= se) continue;
      size_t seq = npos, biPos = npos;
      bool bitfield = false;
      {
        int P = 0, A = 0, B = 0, Br = 0;
        for (size_t k = sb; k < se; k++) {
          if (t[k].kind != Tok::Punct) continue;
          const std::string& s2 = t[k].text;
          if (s2 == "(") P++;
          else if (s2 == ")") { if (P > 0) P--; }
          else if (s2 == "<") A++;
          else if (s2 == ">") { if (A > 0) A--; }
          else if (s2 == "[") B++;
          else if (s2 == "]") { if (B > 0) B--; }
          else if (s2 == "{") {
            // Top-level `{` with no `=` before it is brace-init, not a method
            // body (the outer scan already routed method bodies elsewhere).
            if (P == 0 && A == 0 && B == 0 && Br == 0) biPos = k;
            Br++;
          }
          else if (s2 == "}") { if (Br > 0) Br--; }
          else if (s2 == "=" && P == 0 && A == 0 && B == 0 && Br == 0) { seq = k; break; }
          // Bit-field width (`int x : 3;`). Reached only before any top-level
          // `=`, so the `:` of a ternary initializer (`v = c ? 1 : 0`, which
          // breaks on the `=` first) is never mistaken for one.
          else if (s2 == ":" && P == 0 && A == 0 && B == 0 && Br == 0) { bitfield = true; break; }
        }
      }
      size_t lim = se;
      if (seq != npos) lim = seq;
      else if (biPos != npos) lim = biPos;
      // A post-name attribute (`int x [[deprecated]];`) is lexed as ordinary
      // identifiers, so the "last identifier is the declarator name" rule below
      // picked `deprecated` and the field lost its real name. Mark every
      // `[[...]]` region in this segment first: those tokens can neither supply
      // the name nor be read as an array suffix by the suffix loop.
      std::vector<std::pair<size_t, size_t>> attrs;
      {
        size_t k = sb;
        while (k + 1 < se) {
          if (!(t[k].kind == Tok::Punct && t[k].text == "[" &&
                t[k + 1].kind == Tok::Punct && t[k + 1].text == "[")) { k++; continue; }
          const size_t start = k;
          int ad = 0;
          while (k < se) {
            if (t[k].kind == Tok::Punct && t[k].text == "[") ad++;
            else if (t[k].kind == Tok::Punct && t[k].text == "]") {
              ad--;
              if (ad == 0) { k++; break; }
            }
            k++;
          }
          attrs.emplace_back(start, k);
        }
      }
      // Function-pointer declarator (`int (*fp)(int, int)`): the name sits
      // INSIDE the parens, so the "last identifier before lim" rule below would
      // pick the last PARAMETER (or, with no parameters, fp only by luck).
      // Detect the `( *` opening and take the identifier after the `*` (an
      // optional `const`/`volatile` may sit between them: `int (*const fp)()`).
      size_t fpNameI = npos;
      bool fnPtr = false;
      {
        int P2 = 0;
        for (size_t k = sb; k < se; k++) {
          if (t[k].kind != Tok::Punct) continue;
          if (t[k].text == "(") {
            if (P2 == 0) {
              size_t m = k + 1;
              while (m < se && t[m].kind == Tok::Comment) m++;
              if (m < se && t[m].kind == Tok::Punct && t[m].text == "*") {
                fnPtr = true;
                size_t nn = m + 1;
                while (nn < se && t[nn].kind == Tok::Comment) nn++;
                while (nn < se && t[nn].kind == Tok::Ident &&
                       (t[nn].text == "const" || t[nn].text == "volatile")) {
                  nn++;
                  while (nn < se && t[nn].kind == Tok::Comment) nn++;
                }
                if (nn < se && t[nn].kind == Tok::Ident) fpNameI = nn;
                break;
              }
            }
            P2++;
          } else if (t[k].text == ")") { if (P2 > 0) P2--; }
        }
      }
      size_t nameI = npos;
      if (fnPtr && fpNameI != npos) {
        nameI = fpNameI;
      } else {
        for (size_t k = lim; k > sb; k--) {
          const bool inAttr = [&] {
            for (const auto& a : attrs)
              if (k - 1 >= a.first && k - 1 < a.second) return true;
            return false;
          }();
          if (!inAttr && t[k - 1].kind == Tok::Ident) { nameI = k - 1; break; }
        }
      }
      if (nameI == npos) {
        if (opts.strict) throw asvJSONError("cpp: cannot parse member declaration");
        continue;
      }
      // A JSON tree has no bit width, so `int x : 3;` can only ever decode to the
      // field's default. Silent in lenient mode (as before); strict mode rejects
      // it instead of quietly discarding the width.
      if (bitfield && opts.strict)
        throw asvJSONError("cpp: bit-field width is not representable in JSON ('" +
                           t[nameI].text + "')");
      FieldDef f;
      f.name = t[nameI].text;
      f.leading = (si == 0) ? std::move(lead) : std::vector<Comment>{};
      if (baseSet) {
        f.type = base;
      } else {
        f.type = cppRdParseType(t, sb, nameI);
        base = f.type;
        baseSet = true;
      }
      // Attributes are not part of the declarator, so cap the suffix scan at the
      // first one that follows the name (`[[` must not be read as an array
      // suffix). Note `lim` is where the initializer STARTS (`=` or the brace
      // group), so it bounds the suffix scan only -- never the initializer. An
      // attribute may sit on either side of the initializer
      // (`int x [[deprecated]] = 5;` and `int x = 5 [[deprecated]];` both work),
      // so the initializer range is capped by the first attribute after ITS
      // start, not after the name.
      const size_t initStart = (seq != npos) ? seq : biPos;
      size_t lim2 = lim;
      size_t seInit = se;
      for (const auto& a : attrs) {
        if (a.first > nameI && lim2 == lim) lim2 = std::min(lim, a.first);
        if (initStart != npos && a.first > initStart && seInit == se)
          seInit = std::min(se, a.first);
      }
      size_t suf = nameI + 1;
      // A function pointer's post-name region is its closing `)` and parameter
      // list -- never an array suffix.
      // The dimensions are collected in order and the type is built from the
      // inside out, so `int m[2][3]` is an array of 2 rows of 3. Before, each
      // suffix wrapped the previous type, which reversed the dimension order
      // (3 rows of 2).
      std::vector<size_t> dims;
      while (suf < lim2 && !fnPtr) {
        if (t[suf].kind == Tok::Punct && t[suf].text == "[") {
          size_t j = suf + 1;
          while (j < lim2 && t[j].kind == Tok::Comment) j++;
          if (j < lim2 && t[j].kind == Tok::Punct && t[j].text == "-")
            throw asvJSONError("cpp: negative array size");
          std::string num;
          while (j < lim2 && !(t[j].kind == Tok::Punct && t[j].text == "]")) {
            if (t[j].kind == Tok::Number) num = t[j].text;
            j++;
          }
          if (!num.empty()) {
            try { dims.push_back(static_cast<size_t>(std::stoull(cppRdStripNumSuffix(num)))); } catch (...) {}
          }
          suf = (j < lim2) ? j + 1 : lim2;
        } else {
          suf++;
        }
      }
      if (!dims.empty()) {
        TypeRef ty = f.type;
        for (size_t d = dims.size(); d-- > 0;) {
          TypeRef a;
          a.kind = TypeRef::Array;
          a.elem = std::make_shared<TypeRef>(ty);
          a.arrayLen = dims[d];
          a.hasLen = true;
          ty = a;
        }
        f.type = ty;
      }
      if (seq != npos) {
        f.hasInit = true;
        for (size_t k = seq + 1; k < seInit; k++) f.init.push_back(t[k]);
      } else if (directInitPi != npos && directInitPc != npos &&
                 directInitPi >= sb && directInitPc < se) {
        // Paren-init (`int x(5);`, `std::string s("hello");`). The paren group
        // is the initializer - capture it so cppRdScalarLiteral can strip parens
        // and extract the value. Must be checked before brace-init since a
        // direct-init cannot also have braces at this point.
        f.hasInit = true;
        for (size_t k = directInitPi; k <= directInitPc; k++) f.init.push_back(t[k]);
      } else {
        // Global paren-init didn't span this segment (e.g. second declarator in
        // `int x(5), y(6);`). Look for a local one.
        size_t lpi = npos, lpc = npos;
        for (size_t k = sb; k < se; k++) {
          // Parentheses inside a [[...]] attribute belong to the attribute,
          // not to an initializer: `int x [[deprecated("use y")]];` must not
          // decode x as the string "use y" (it has no initializer at all).
          bool inAttr = false;
          for (const auto& a : attrs) {
            if (k >= a.first && k < a.second) { inAttr = true; break; }
          }
          if (inAttr) continue;
          if (t[k].kind == Tok::Punct && t[k].text == "(") { lpi = k; }
          else if (t[k].kind == Tok::Punct && t[k].text == ")") { lpc = k; break; }
        }
        if (lpi != npos && lpc != npos && lpc > lpi + 1) {
          f.hasInit = true;
          for (size_t k = lpi; k <= lpc; k++) f.init.push_back(t[k]);
        }
      }
      if (biPos != npos) {
        // Brace-init (`Foo bar{5};`, `Point p{1,2};`, `int arr[3]{1,2,3};`).
        // The braces ARE part of f.init: materialize() locates the group with
        // cppRdBraceGroup(), exactly as for `= {1,2,3}`.
        f.hasInit = true;
        for (size_t k = biPos; k < seInit; k++) f.init.push_back(t[k]);
      }
      def.fields.push_back(std::move(f));
      parsedAny = true;
    }
    // Advance past the terminating `;` (and any same-line trailing comments).
    // Done outside the segment loop so a trailing empty/no-identifier segment
    // (`int a, ;`) can never leave p stuck and loop forever.
    if (end != npos) {
      // parsedAny is set after def.fields.push_back(...) in the loop above, so
      // a non-empty fields vector is the expected invariant -- but check explicitly
      // to avoid UB on a future refactoring that could decouple the two.
      if (parsedAny && !def.fields.empty()) {
        FieldDef& lastField = def.fields.back();
        int semiLine = t[end].line;
        size_t k = end + 1;
        while (t[k].kind == Tok::Comment && t[k].line == semiLine) {
          lastField.trailing.push_back(Comment{t[k].text, t[k].block});
          k++;
        }
        p = k;
      } else {
        p = end + 1;
      }
    } else {
      p = i;
    }
  }

  void parseEnumDecl(bool typedefCtx = false) {
    takePending();
    adv(); // enum
    skipTrivia();
    if (isKw("class") || isKw("struct")) adv();
    skipTrivia();
    std::string name;
    if (cur().kind == Tok::Ident) { name = cur().text; adv(); }
    skipTrivia();
    if (isP(":")) {
      while (!eof() && !isP("{") && !isP(";")) adv();
      skipTrivia();
    }
    enumDeclVar_.clear();
    enumDeclAliases_.clear();
    enumDeclInit_.clear();
    enumDeclHasInit_ = false;
    if (isP(";")) { adv(); return; }
    if (!isP("{")) { skipToSemi(); return; }
    adv(); // '{'
    EnumDef def;
    def.name = name;
    int lastMemberLine = 0;
    while (!eof() && !isP("}")) {
      skipTrivia();
      if (isP("}")) break;
      if (isP(",")) { adv(); continue; }
      if (cur().kind == Tok::Ident) {
        const int memberLine = cur().line;
        // The comments collected so far: those on the PREVIOUS enumerator's
        // line trail that enumerator (they follow its comma), the rest lead
        // this one.
        std::vector<int> lines;
        std::vector<Comment> pend = takePendingLines(lines);
        std::vector<Comment> lead;
        for (size_t ci = 0; ci < pend.size(); ci++) {
          if (lastMemberLine > 0 && ci < lines.size() && lines[ci] == lastMemberLine) {
            if (!def.memberTrailing.empty()) def.memberTrailing.back().push_back(pend[ci]);
            continue;
          }
          lead.push_back(pend[ci]);
        }
        if (!asvJSONValue::checkObjectSize(def.members.size() + 1))
          throw asvJSONError("cpp: too many enum members");
        def.members.push_back(cur().text);
        def.memberLeading.push_back(lead);
        def.memberTrailing.push_back(std::vector<Comment>());
        lastMemberLine = memberLine;
        adv();
        skipTrivia();
        if (isP("=")) {
          adv();
          while (!eof() && !isP(",") && !isP("}")) adv();
        }
      } else {
        adv();
      }
    }
    // The comments inside the body that follow the LAST enumerator belong to
    // it (the emitter writes a trailing comment after the comma). They must
    // not stay in `pending`: they would become the leading comments of the
    // NEXT declaration - the enum is a type, it has no node of its own.
    if (!def.memberTrailing.empty() && !pending.empty()) {
      for (const Comment& c : pending) def.memberTrailing.back().push_back(c);
      pending.clear();
      pendingLines.clear();
    }
    if (isP("}")) adv();
    skipTrivia();
    // The identifier after the body: in a typedef it is the ALIAS name(s)
    // (`typedef enum E { A, B } A1, A2;`), otherwise it is a DECLARATOR of a
    // variable of that enum type (`enum E { A, B } var;`). An ANONYMOUS enum
    // has no tag of its own, so the identifier there can only be a declarator
    // - taking it as the type name made the variable disappear from the tree.
    if (cur().kind == Tok::Ident) {
      size_t q = p + 1;
      while (q < t.size() && t[q].kind == Tok::Comment) q++;
      bool declFollows = q < t.size() && t[q].kind == Tok::Punct &&
                         (t[q].text == ";" || t[q].text == "=" || t[q].text == "[");
      if (typedefCtx) {
        // The trailing names of a typedef are ALIASES, not variables - the
        // struct-typedef path registers all of them.
        while (cur().kind == Tok::Ident) {
          enumDeclAliases_.push_back(cur().text);
          adv();
          skipTrivia();
          if (isP(",")) { adv(); skipTrivia(); continue; }
          break;
        }
      } else if (declFollows) {
        // `enum E { A, B } e;` declares a MEMBER of that enum type as well.
        // The declarator is not part of the enum definition, so it must not be
        // swallowed by the "skip to ;" below - the member was lost that way.
        enumDeclVar_ = cur().text;
        adv();
        skipTrivia();
        // The initializer of the declarator (`enum E { A, B } e = A;`).
        if (isP("=")) {
          adv();
          enumDeclHasInit_ = true;
          int depth = 0;
          while (!eof()) {
            if (t[p].kind == Tok::Punct) {
              if (t[p].text == "{" || t[p].text == "(" || t[p].text == "[") depth++;
              else if (t[p].text == "}" || t[p].text == ")" || t[p].text == "]") {
                if (depth == 0) break;
                depth--;
              } else if (depth == 0 && (t[p].text == ";" || t[p].text == ",")) break;
            }
            enumDeclInit_.push_back(cur());
            adv();
          }
        }
      }
    }
    while (!eof() && !isP(";")) adv();
    if (isP(";")) adv();
    if (def.name.empty()) {
      // An anonymous enum has no tag - synthesize a name so the type can be
      // referenced (by the variable declared with it, by a typedef alias).
      def.name = "Enum" + std::to_string(++anonEnumSeq_);
    }
    enumDeclName_ = def.name;
    enums.push_back(std::move(def));
  }

  // Set by parseEnumDecl when the enum definition is followed by declarators.
  // `enumDeclVar_` holds the name of a VARIABLE of that enum type
  // (`enum E { A, B } e;`), `enumDeclAliases_` the trailing names of a TYPEDEF
  // (`typedef enum E { A, B } A1, A2;` - they are aliases, not variables).
  // `enumDeclName_` is the name the enum was registered under.
  std::string enumDeclVar_;
  std::vector<std::string> enumDeclAliases_;
  std::string enumDeclName_;
  // The initializer of the declarator (`enum E { A, B } e = A;`).
  std::vector<Tok> enumDeclInit_;
  bool enumDeclHasInit_ = false;
  // Counter for the synthesized names of anonymous enums.
  int anonEnumSeq_ = 0;

  void parseUsing() {
    adv(); // using
    skipTrivia();
    if (isKw("namespace")) { skipToSemi(); return; }
    std::string name;
    if (cur().kind == Tok::Ident) { name = cur().text; adv(); }
    skipTrivia();
    if (!isP("=")) { skipToSemi(); return; }
    adv(); // '='
    size_t b = p;
    while (!eof() && !isP(";")) {
      if (isP("{")) { skipBalanced("{", "}"); continue; }
      adv();
    }
    size_t e = p;
    if (isP(";")) adv();
    AliasDef a;
    a.name = name;
    a.type = cppRdParseType(t, b, e);
    a.order = order++;
    if (!a.name.empty()) aliases.push_back(std::move(a));
  }

  // Register `typedef struct Foo Bar;`-style names. `weak` keeps them out of
  // root selection (a real struct definition should still wrap the output), but
  // `resolve()` still follows them so a field typed `Bar` becomes `Foo`.
  void addWeakAlias(const std::string& name, const TypeRef& type) {
    if (name.empty()) return;
    AliasDef a;
    a.name = name;
    a.type = type;
    a.order = order++;
    a.weak = true;
    aliases.push_back(std::move(a));
  }

  void parseTypedef() {
    takePending();
    adv(); // typedef
    skipTrivia();
    if (isKw("struct") || isKw("class") || isKw("enum") || isKw("union")) {
      const bool isEnum = isKw("enum");
      // Look past the tag name: `typedef struct Foo Bar;` has a second name and
      // is a forward-declared alias, while `typedef struct Foo { ... }` and
      // `typedef struct Foo;` do not.
      size_t q = p + 1;
      while (q < t.size() && t[q].kind == Tok::Comment) q++;
      std::string tagName;
      if (q < t.size() && t[q].kind == Tok::Ident) { tagName = t[q].text; q++; }
      while (q < t.size() && t[q].kind == Tok::Comment) q++;
      if (!tagName.empty() && q < t.size() && t[q].kind == Tok::Ident) {
        TypeRef tagged;
        tagged.kind = TypeRef::Unknown;
        tagged.name = tagName;
        while (q < t.size() && t[q].kind == Tok::Ident) {
          addWeakAlias(t[q].text, tagged);
          q++;
          while (q < t.size() && t[q].kind == Tok::Comment) q++;
          if (q < t.size() && t[q].kind == Tok::Punct && t[q].text == ",") {
            q++;
            while (q < t.size() && t[q].kind == Tok::Comment) q++;
          } else break;
        }
        skipToSemi();
        return;
      }
      if (isEnum) {
        // `typedef enum E { A, B } Alias;` - the trailing names are ALIASES,
        // not variables. They were not registered, so `Alias x;` did not
        // resolve and the member decoded to null.
        parseEnumDecl(/*typedefCtx=*/true);
        for (const std::string& al : enumDeclAliases_) {
          TypeRef et;
          et.kind = TypeRef::Enum;
          et.name = enumDeclName_;
          addWeakAlias(al, et);
        }
        return;
      }
      std::vector<FieldDef> extras = parseStructDecl();
      // `typedef struct Foo { ... } Bar;` - the trailing names are aliases, not
      // variables (a typedef declares a type). Register each so `Bar` resolves.
      for (auto& f : extras) addWeakAlias(f.name, f.type);
      return;
    }
    size_t b = p;
    while (!eof() && !isP(";")) {
      if (isP("{")) { skipBalanced("{", "}"); continue; }
      adv();
    }
    size_t e = p;
    if (isP(";")) adv();
    size_t nameIdx = static_cast<size_t>(-1);
    for (size_t k = e; k > b; k--)
      if (t[k - 1].kind == Tok::Ident) { nameIdx = k - 1; break; }
    if (nameIdx == static_cast<size_t>(-1) || nameIdx == b) return;
    AliasDef a;
    a.name = t[nameIdx].text;
    a.type = cppRdParseType(t, b, nameIdx);
    a.order = order++;
    aliases.push_back(std::move(a));
  }

  // --- materialization ---
  void resolve(TypeRef& ty, int depth = 0) const {
    if (ty.kind != TypeRef::Unknown) return;
    if (findStruct(ty.name)) { ty.kind = TypeRef::Struct; return; }
    if (findEnum(ty.name)) { ty.kind = TypeRef::Enum; return; }
    // Follow a typedef name (`typedef struct Foo Bar;`) to its underlying type,
    // so a field declared `Bar` resolves like `Foo`. The depth cap stops a
    // malformed alias chain (`typedef A B; typedef B A;`) from recursing forever.
    if (depth < 32) {
      for (const auto& a : aliases) {
        if (a.name != ty.name) continue;
        ty = a.type;
        if (ty.kind == TypeRef::Unknown) resolve(ty, depth + 1);
        return;
      }
    }
    ty.kind = TypeRef::Scalar;
    ty.scalar = cppRdClassify(ty.name);
  }

  static std::unique_ptr<asvJSONValue> mkStr(std::string s) {
    return cppRdString(std::move(s));
  }

  // A map key has to become a JSON object key, so the parsed key value is
  // rendered back to text. Strings keep their exact bytes, numbers and bools
  // use their literal spelling, and anything else falls back to compact JSON
  // (so a struct-valued key round-trips as its JSON form).
  static std::string mapKeyText(const asvJSONValue* v) {
    if (!v) return {};
    switch (v->type) {
      case asvJSONValue::STRING:   return v->str_data;
      case asvJSONValue::INT:      return std::to_string(v->num);
      case asvJSONValue::BOOL_VAL: return v->flag ? "true" : "false";
      case asvJSONValue::NULL_VAL: return "null";
      default: break;
    }
    std::string s;
    v->serialize(s);
    return s;
  }

  std::unique_ptr<asvJSONValue> numberValue(const std::string& raw, bool asDouble) {
    std::string s = cppRdStripNumSuffix(raw);
    bool flt = asDouble;
    for (char c : s) if (c == '.' || c == 'e' || c == 'E') flt = true;
    if (flt) {
      try { return cppRdNonNull(asvJSONValue::makeDouble(std::stod(s))); }
      catch (const asvJSONError&) { throw; }
      catch (...) { return cppRdString(cppRdStripNumSuffix(raw)); }
    }
    if (s == "-9223372036854775808")
      return cppRdNonNull(asvJSONValue::makeInt((std::numeric_limits<long long>::min)()));
    long long iv = 0;
    if (cppRdParseIntLiteral(s, iv)) return cppRdNonNull(asvJSONValue::makeInt(static_cast<int64_t>(iv)));
    return cppRdString(cppRdStripNumSuffix(raw));
  }

  // The value of an enumerator: its name plus the comments that were read for
  // it. toCpp() emits them, so they have to survive the round-trip.
  static std::unique_ptr<asvJSONValue> enumVariant(const EnumDef* ed, size_t i) {
    if (!ed || i >= ed->members.size()) throw asvJSONError("cpp: bad enumerator index");
    auto sv = cppRdString(ed->members[i]);
    if (!sv) throw asvJSONError("cpp: out of memory");
    std::vector<Comment> lead, trail;
    if (i < ed->memberLeading.size()) lead = ed->memberLeading[i];
    if (i < ed->memberTrailing.size()) trail = ed->memberTrailing[i];
    if (!lead.empty() || !trail.empty()) {
      auto* c = new Comments();
      c->leading = std::move(lead);
      c->trailing = std::move(trail);
      sv->comments.reset(c);
    }
    return sv;
  }

  std::unique_ptr<asvJSONValue> materialize(const TypeRef& t,
                                            const std::vector<Tok>& init,
                                            bool hasInit, int depth) {
    // depth starts at 0 for root scalar; each container increments by 1.
    // checkNestingDepth uses depth starting at 1 (parse() increments first),
    // so depth >= MAX means we're about to create the (MAX+1)-th container —
    // reject. Allows exactly MAX containers (depth 0..MAX-1).
    if (depth >= static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
      throw asvJSONError("cpp: nesting too deep");
    TypeRef rt = t;
    resolve(rt);

    bool grp = hasInit && cppRdBraceGroup(init);
    std::vector<std::vector<Tok>> groups;
    if (grp) groups = cppRdInitGroups(init);
    bool onlyEmptyGroup = groups.size() == 1 && groups[0].empty();

    if (rt.kind == TypeRef::Struct) {
      auto o = asvJSONValue::makeObject();
      if (!o || !o->obj) throw asvJSONError("cpp: out of memory");
      const StructDef* sd = findStruct(rt.name);
      // Paren-init (`Foo f(1, 2);`) - positional args mapped to fields in order.
      bool pgrp = hasInit && cppRdParenGroup(init);
      std::vector<std::vector<Tok>> pgroups;
      if (pgrp) pgroups = cppRdSplitTop({init.begin() + 1, init.end() - 1});
      if (sd) {
        for (size_t idx = 0; idx < sd->fields.size(); idx++) {
          const FieldDef& f = sd->fields[idx];
          std::vector<Tok> fi;
          bool hi = false;
          if (grp && idx < groups.size() && !groups[idx].empty()) { fi = groups[idx]; hi = true; }
          else if (pgrp && idx < pgroups.size() && !pgroups[idx].empty()) { fi = pgroups[idx]; hi = true; }
          else { fi = f.init; hi = f.hasInit; }
          auto child = materialize(f.type, fi, hi, depth + 1);
          if (!child) throw asvJSONError("cpp: out of memory");
          if (opts.includeComments && (!f.leading.empty() || !f.trailing.empty())) {
            child->comments.reset(new Comments());
            child->comments->leading = f.leading;
            child->comments->trailing = f.trailing;
          }
          o->obj->emplace(f.name, std::move(child));
        }
      }
      return o;
    }

    if (rt.kind == TypeRef::Enum) {
      if (hasInit) {
        // An initializer names ONE variant (`e = A;`, `e = {A};`) - the value
        // is that variant's name, not the whole variant list.
        std::string v;
        if (!grp) v = cppRdLastIdent(init);
        else if (!groups.empty()) v = cppRdLastIdent(groups[0]);
        if (!v.empty()) return cppRdString(std::move(v));
      }
      // Without an initializer the value is the enum's VARIANT LIST - the same
      // shape toCpp() emits an enum for (an array of identifier strings), so
      // `enum E { A, B } e;` keeps the member `e` with the value ["A","B"] and
      // a standalone `enum E { A, B };` becomes {"E":["A","B"]}.
      const EnumDef* ed = findEnum(rt.name);
      if (ed && !ed->members.empty()) {
        auto a = asvJSONValue::makeArray();
        if (!a || !a->arr) throw asvJSONError("cpp: out of memory");
        for (size_t i = 0; i < ed->members.size(); i++)
          a->arr->push_back(enumVariant(ed, i));
        return a;
      }
      return cppRdNonNull(asvJSONValue::makeNull());
    }

    if (rt.kind == TypeRef::Array) {
      auto a = asvJSONValue::makeArray();
      if (!a || !a->arr) throw asvJSONError("cpp: out of memory");
      TypeRef elem;
      if (rt.elem) { elem = *rt.elem; resolve(elem); }
      const EnumDef* ed = (elem.kind == TypeRef::Enum) ? findEnum(elem.name) : nullptr;
      if (grp && !onlyEmptyGroup) {
        for (auto& g : groups) {
          if (!asvJSONValue::checkArraySize(a->arr->size() + 1))
            throw asvJSONError("CPP array too large");
          auto elemVal = materialize(elem, g, !g.empty(), depth + 1);
          if (!elemVal) throw asvJSONError("cpp: out of memory");
          a->arr->push_back(std::move(elemVal));
        }
      } else if (rt.hasLen) {
        if (!asvJSONValue::checkArraySize(rt.arrayLen)) throw asvJSONError("CPP array too large");
        for (size_t k = 0; k < rt.arrayLen; k++) {
          if (ed && ed->members.size() == rt.arrayLen)
            a->arr->push_back(enumVariant(ed, k));
          else {
            auto elemVal = materialize(elem, std::vector<Tok>(), false, depth + 1);
            if (!elemVal) throw asvJSONError("cpp: out of memory");
            a->arr->push_back(std::move(elemVal));
          }
        }
      }
      return a;
    }

    if (rt.kind == TypeRef::Variant) {
      if (hasInit && !grp) {
        const Tok* v = cppRdFirstVal(init);
        if (v) {
          for (auto& alt : rt.alts) {
            TypeRef at = *alt;
            resolve(at);
            if (at.kind == TypeRef::Scalar) {
              if (v->kind == Tok::Str && at.scalar == TypeRef::SString)
                return mkStr(v->text);
              if (v->kind == Tok::Number && (at.scalar == TypeRef::SInt || at.scalar == TypeRef::SDouble))
                return numberValue(v->text, at.scalar == TypeRef::SDouble);
              if ((v->text == "true" || v->text == "false") && at.scalar == TypeRef::SBool)
                return cppRdNonNull(asvJSONValue::makeBool(v->text == "true"));
            }
            if (at.kind == TypeRef::Enum && v->kind == Tok::Ident)
              return cppRdString(cppRdLastIdent(init));
          }
        }
      }
      if (grp && groups.size() == 1)
        return materialize(rt.alts.empty() ? TypeRef{} : *rt.alts[0], groups[0], !groups[0].empty(), depth + 1);
      return materialize(rt.alts.empty() ? TypeRef{} : *rt.alts[0], std::vector<Tok>(), false, depth + 1);
    }

    if (rt.kind == TypeRef::Optional) {
      if (!hasInit) return cppRdNonNull(asvJSONValue::makeNull());
      const Tok* v = cppRdFirstVal(init);
      if (v && v->kind == Tok::Ident &&
          (v->text == "nullopt" || v->text == "NULL" || v->text == "nullptr"))
        return cppRdNonNull(asvJSONValue::makeNull());
      return materialize(rt.elem ? *rt.elem : TypeRef{}, init, hasInit, depth + 1);
    }

    if (rt.kind == TypeRef::Map) {
      // `std::map<K, V> m = {{k1, v1}, {k2, v2}};` -- the initializer groups
      // were computed but never looked at, so every map decoded as {} and its
      // contents vanished. Each group is one braced {key, value} pair (the
      // only shape a map initializer can take, since the comma between key and
      // value must sit inside the inner braces).
      auto o = asvJSONValue::makeObject();
      if (!o || !o->obj) throw asvJSONError("cpp: out of memory");
      if (grp && !onlyEmptyGroup) {
        TypeRef kt, vt;
        if (rt.elem) { kt = *rt.elem; resolve(kt); }
        if (rt.value) { vt = *rt.value; resolve(vt); }
        for (auto& g : groups) {
          auto parts = cppRdInitGroups(g);
          if (parts.size() != 2) continue;  // not a {key, value} pair
          auto key = materialize(kt, parts[0], !parts[0].empty(), depth + 1);
          std::string name = mapKeyText(key.get());
          if (name.empty()) continue;
          auto val = materialize(vt, parts[1], !parts[1].empty(), depth + 1);
          if (!val) throw asvJSONError("cpp: out of memory");
          o->obj->emplace(std::move(name), std::move(val));
        }
      }
      return o;
    }

    // scalar / unresolved
    if (hasInit) {
      const std::vector<Tok>* litSrc = nullptr;
      if (grp) {
        // A brace group after a scalar declarator (`int a{1};`, `bool b{true};`)
        // is a plain scalar initializer, not an aggregate: without this the
        // group skipped the literal reader entirely and fell through to the
        // default below, so `int a{1}` decoded as 0. An empty group (`int a{}`)
        // is just the type default. Restricted to a resolved scalar kind, so an
        // unresolved (forward referenced) type keeps its existing behaviour.
        if (rt.kind == TypeRef::Scalar && groups.size() == 1 && !groups[0].empty())
          litSrc = &groups[0];
        // `int a{1, 2}` (aggregate-init of a scalar) named one field but held
        // several values; there is no JSON for it. The literal reader would skip
        // the group entirely and silently emit the type default (0). Strict mode
        // refuses it instead (lenient mode keeps the default, as before).
        else if (rt.kind == TypeRef::Scalar && groups.size() != 1 && opts.strict)
          throw asvJSONError("cpp: braced initializer for scalar has multiple elements "
                             "(not representable in JSON)");
      } else {
        litSrc = &init;
      }
      if (litSrc) {
        if (auto lit = cppRdScalarLiteral(*litSrc, rt.scalar == TypeRef::SDouble)) return lit;
        const Tok* v = cppRdFirstVal(*litSrc);
        if (v && v->kind == Tok::Ident) {
          std::string id = cppRdLastIdent(*litSrc);
          if (!id.empty()) return cppRdString(std::move(id));
        }
        // An initializer this reader cannot evaluate (`1 | 2`, `1 << 4`,
        // `sizeof(int)`, `1 ? 2 : 3`) would fall through to the switch below
        // and silently become 0/false -- a wrong value, not just a missing one.
        // Lenient mode keeps that default (unchanged); strict mode refuses the
        // declaration instead of inventing a value.
        if (opts.strict)
          throw asvJSONError("cpp: initializer expression is not representable in JSON");
      }
    }
    switch (rt.scalar) {
      case TypeRef::SBool: return cppRdNonNull(asvJSONValue::makeBool(false));
      case TypeRef::SDouble: return cppRdNonNull(asvJSONValue::makeDouble(0.0));
      case TypeRef::SString: return cppRdString(std::string());
      case TypeRef::SNull: return cppRdNonNull(asvJSONValue::makeNull());
      case TypeRef::SInt: return cppRdNonNull(asvJSONValue::makeInt(0));
      default: return cppRdNonNull(asvJSONValue::makeNull());
    }
  }

  void collectRefs(const TypeRef& t, std::set<std::string>& out) const {
    if (t.kind == TypeRef::Struct) out.insert(t.name);
    else if (t.kind == TypeRef::Unknown && findStruct(t.name)) out.insert(t.name);
    if (t.elem) collectRefs(*t.elem, out);
    if (t.value) collectRefs(*t.value, out);
    for (auto& a : t.alts) if (a) collectRefs(*a, out);
  }

  // `struct Foo { ... } a, b;` declares real variables of type Foo at file
  // scope. Those declarators were parsed and then thrown away, so a single
  // translation unit decoded to {"Foo": {...}} and the variables it actually
  // defines were invisible. Add them to the root -- but only when their type is
  // not already reachable from the chosen roots, so the common
  // `struct Inner {...} ia, ib;` + `struct Document { Inner a, b; };` case
  // (where Inner is already represented inside Document) keeps its output.
  void addFileScopeInst(const std::unique_ptr<asvJSONValue>& out,
                        const std::vector<const StructDef*>& chosen) {
    if (fileScope.empty() || !out || out->type != asvJSONValue::OBJECT) return;
    std::set<std::string> live;
    for (auto* s : chosen)
      for (auto& f : s->fields) collectRefs(f.type, live);
    for (auto& f : fileScope) {
      if (f.name.empty()) continue;
      if (live.count(f.type.name)) continue;
      auto val = materialize(f.type, f.init, f.hasInit, 0);
      if (!val) throw asvJSONError("cpp: out of memory");
      // The comments of a file-scope declarator are attached to its value, as
      // they are for a struct member.
      if (opts.includeComments && (!f.leading.empty() || !f.trailing.empty())) {
        val->comments.reset(new Comments());
        val->comments->leading = f.leading;
        val->comments->trailing = f.trailing;
      }
      out->obj->emplace(f.name, std::move(val));
    }
  }

  std::unique_ptr<asvJSONValue> buildRoot() {
    if (!aliases.empty()) {
      // First-declared (lowest order) alias wins - not the last one seen. Weak
      // aliases (typedef names for tagged types) never drive the root: with a
      // real struct definition present the struct wrapper must be kept, and
      // alone they would only resolve to a forward-declared, bodiless type.
      const AliasDef* best = nullptr;
      for (auto& a : aliases) {
        if (a.weak) continue;
        if (!best || a.order < best->order) best = &a;
      }
      // A strong alias drives the root ONLY when the file declares no struct of
      // its own. With real struct definitions present the alias is just a
      // type name, and letting it drive the root dropped every struct in the
      // file (`using Alias = int;` + `struct S { Alias a; };` decoded to the
      // scalar 0 instead of {"S":{"a":0}}).
      if (best && structs.empty() && fileScope.empty())
        return materialize(best->type, std::vector<Tok>(), false, 0);
    }
    if (structs.empty() && fileScope.empty()) return nullptr;

    std::set<std::string> referenced;
    for (auto& s : structs)
      for (auto& f : s.fields) collectRefs(f.type, referenced);

    std::vector<const StructDef*> roots;
    for (auto& s : structs) if (!referenced.count(s.name)) roots.push_back(&s);

    auto wrap = [&](const std::vector<const StructDef*>& list) {
      auto o = asvJSONValue::makeObject();
      if (!o || !o->obj) throw asvJSONError("cpp: out of memory");
      for (auto* s : list) {
        TypeRef st;
        st.kind = TypeRef::Struct;
        st.name = s->name;
        auto val = materialize(st, std::vector<Tok>(), false, 0);
        if (!val) throw asvJSONError("cpp: out of memory");
        if (opts.includeComments && val && (!s->leading.empty() || !s->closing.empty())) {
          val->comments.reset(new Comments());
          val->comments->leading = s->leading;
          val->comments->closing = s->closing;
        }
        o->obj->emplace(s->name, std::move(val));
      }
      return o;
    };

    if (roots.size() == 1) {
      const StructDef* s = roots[0];
      if (opts.unwrapRootStruct && s->name == opts.rootName) {
        // Unwrap to the root struct's own fields, then merge any file-scope
        // instance (`struct Document { int x; } doc;`). This is needed even
        // though unwrapping runs only when `roots` holds a single struct: `live`
        // collects the types of the root's FIELDS, not the root's own name, so a
        // declarator of the root type itself is not in it and would be dropped.
        TypeRef st;
        st.kind = TypeRef::Struct;
        st.name = s->name;
        auto val = materialize(st, std::vector<Tok>(), false, 0);
        if (val && opts.includeComments && (!s->leading.empty() || !s->closing.empty())) {
          val->comments.reset(new Comments());
          val->comments->leading = s->leading;
          val->comments->closing = s->closing;
        }
        std::vector<const StructDef*> list = {s};
        addFileScopeInst(val, list);
        return val;
      }
      std::vector<const StructDef*> list = {s};
      std::unique_ptr<asvJSONValue> out = wrap(list);
      addFileScopeInst(out, list);
      return out;
    }
    if (!roots.empty()) {
      std::unique_ptr<asvJSONValue> out = wrap(roots);
      addFileScopeInst(out, roots);
      return out;
    }
    if (structs.empty()) {
      // Only file-scope variables (`int g = 5;`): the root is the object of
      // those variables. The fallback below assumed a struct exists and read
      // structs[0] out of bounds.
      std::unique_ptr<asvJSONValue> out = asvJSONValue::makeObject();
      if (!out || !out->obj) throw asvJSONError("cpp: out of memory");
      addFileScopeInst(out, std::vector<const StructDef*>{});
      return out;
    }
    std::vector<const StructDef*> list = {&structs[0]};
    std::unique_ptr<asvJSONValue> out = wrap(list);
    addFileScopeInst(out, list);
    return out;
  }
};

} // namespace cppread

// Input size limit — symmetric with other from* parsers.
inline constexpr size_t MAX_CPP_INPUT = 32u * 1024u * 1024u;

inline bool asvJSON::fromCpp(std::string_view input, const CppReadOptions& opts) {
  lastError.clear();
  root = nullptr;
  dirty_ = false;
  formatType = FormatType::Unknown;
  // Every other text decoder strips a leading UTF-8 BOM; a C++ header saved by
  // a Windows editor often has one. The lexer treats a byte >= 0x80 as an
  // identifier character, so the BOM used to glue itself to the first token
  // ("\xEF\xBB\xBFstruct") and the file decoded as "no type declarations found".
  if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xEF &&
      static_cast<unsigned char>(input[1]) == 0xBB && static_cast<unsigned char>(input[2]) == 0xBF)
    input.remove_prefix(3);
  if (input.empty()) { lastError = "empty input"; return false; }
  if (input.size() > MAX_CPP_INPUT) { lastError = "CPP input exceeds 32 MB limit"; return false; }
  try {
    std::map<std::string, std::vector<cppread::Tok>> macros;
    std::vector<cppread::Tok> toks = cppread::cppRdLex(input, &macros);
    if (!macros.empty()) {
      std::set<std::string> active;
      toks = cppread::cppRdExpandMacros(toks, macros, active, 0);
    }
    cppread::Reader rd(toks, opts);
    rd.parseAll();
    std::unique_ptr<asvJSONValue> val = rd.buildRoot();
    if (!val) { lastError = "cpp: no type declarations found"; root = nullptr; return false; }
    root = std::move(val);
    dirty_ = false;
    formatType = FormatType::CPP;
    return true;
  } catch (const asvJSONError& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  } catch (const std::exception& e) {
    lastError = e.what();
    root = nullptr;
    return false;
  }
}

} // namespace asvJSONInternal
