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
//   - empty ARRAY        -> std::vector<NullType> (placeholder; edit the type).
//   - INT/DOUBLE/BOOL    -> int/double/bool (intType configurable).
//   - STRING             -> stringType; identifier strings in an enum context
//                           (see emitEnums / emitEnumMaps / enumPaths) become
//                           an enum type.
//   - NULL_VAL           -> nullType.
//   - DATETIME/BINARY    -> datetimeType/binaryType; other specials -> string.
//   - root {Name: {...}} -> `struct Name` (unwrapRootSingleKey).
//
// Comments are preserved as `//` / `/* */` lines and same-line trails.

#include "../core.hpp"

#include <cctype>
#include <cmath>
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
  // is worse than leaving the plural alone. A regular plural never ends in one
  // of these, so the rule costs nothing.
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

inline void cppEmitComments(std::string& out, const std::vector<Comment>& list,
                            int indent, const CppWriteOptions& o) {
  if (!o.emitComments) return;
  const std::string pad(static_cast<size_t>(indent < 0 ? 0 : indent) *
                            static_cast<size_t>(o.indentWidth), ' ');
  for (const auto& c : list) {
    out += pad;
    if (c.block) { out += "/*"; out += c.text; out += "*/\n"; }
    else { out += "//"; out += c.text; out += '\n'; }
  }
}

inline void cppEmitTrailing(std::string& out, const asvJSONValue* v, const CppWriteOptions& o) {
  if (!o.emitComments || !v || !v->comments) return;
  for (const auto& c : v->comments->trailing) {
    out += ' ';
    if (c.block) { out += "/*"; out += c.text; out += "*/"; }
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
  std::string enumNameHint(const std::string& path, const std::string& hint) const {
    for (const auto& p : opts_.enumNames) if (pathMatch(path, p.first)) return p.second;
    (void)hint;
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

    std::string name = enumNameHint(path, hint);
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
    suffixOut += "[" + std::to_string(n) + "]";
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
    std::string suffix;
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
        // Recursive reference: break the cycle with a pointer.
        // objTypeCache_ was populated by registerStruct before child processing,
        // so cac (checked above) already covers this case -- we only reach here
        // if the cache hit failed for some reason; fall back to deriving a name.
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
    out += t.enumName;
    if (enumQualified()) out += "::";
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
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool cppRdIdentChar(char c) {
  return cppRdIdentStart(c) || (c >= '0' && c <= '9');
}

inline std::string cppRdStripNumSuffix(std::string s) {
  while (!s.empty()) {
    char c = s.back();
    if (c == 'f' || c == 'F' || c == 'u' || c == 'U' || c == 'l' || c == 'L') s.pop_back();
    else break;
  }
  return s;
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
      if (expr[ep] == '0' &&
          (ep + 1 >= expr.size() || !std::isalnum((unsigned char)expr[ep + 1]))) { ep++; return false; }
      if (expr[ep] == '1' &&
          (ep + 1 >= expr.size() || !std::isalnum((unsigned char)expr[ep + 1]))) { ep++; return true; }
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
          std::string nm = logical.substr(k);
          while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\t' || nm.back() == '\r'))
            nm.pop_back();
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
                  functionLike = (t2 < logical.size());
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
              rtoks = cppRdLex(repl, nullptr);
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
      while (i < n && in[i] != '\n') i++;
      if (!skipTok) {
        Tok t; t.kind = Tok::Comment; t.line = line; t.block = false;
        t.text.assign(in.data() + s, i - s);
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
            case '0': val += '\0'; break;
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
              val += static_cast<char>(hv);
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
      if (!skipTok) {
        Tok t; t.kind = Tok::Ident; t.line = line;
        t.text.assign(in.data() + s, i - s);
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

// Interpret a scalar initializer as a single literal: strips balanced outer
// parentheses and a leading unary sign, then reads one number/string/char/
// bool/null literal. Returns nullptr when the tokens are not a literal.
inline std::unique_ptr<asvJSONValue> cppRdScalarLiteral(const std::vector<Tok>& init,
                                                        bool wantDouble) {
  std::vector<const Tok*> v;
  v.reserve(init.size());
  for (const Tok& tk : init) if (tk.kind != Tok::Comment) v.push_back(&tk);
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
        return asvJSONValue::makeDouble(neg ? -a : a - b);
      } catch (...) { return nullptr; }
    }
    try {
      unsigned long long a = std::stoull(sa, nullptr, 0);
      unsigned long long b = std::stoull(sb, nullptr, 0);
      unsigned long long r = (neg ? (0ull - a) : a) - b;
      return asvJSONValue::makeInt(static_cast<int64_t>(r));
    } catch (...) { return nullptr; }
  }
  if (v.size() != 1) return nullptr;
  const Tok* tk = v[0];
  if (tk->kind == Tok::Str || tk->kind == Tok::Char)
    return asvJSONValue::makeString(std::string(tk->text));
  if (tk->kind == Tok::Number) {
    std::string s = cppRdStripNumSuffix(tk->text);
    bool flt = wantDouble;
    for (char c : s) if (c == '.' || c == 'e' || c == 'E') flt = true;
    if (neg) s.insert(s.begin(), '-');
    if (flt) {
      try { return asvJSONValue::makeDouble(std::stod(s)); }
      catch (...) { return asvJSONValue::makeString(cppRdStripNumSuffix(tk->text)); }
    }
    // Boundary constant: -9223372036854775808 is valid but not a literal.
    if (s == "-9223372036854775808")
      return asvJSONValue::makeInt((std::numeric_limits<long long>::min)());
    try {
      std::string::size_type pos = 0;
      long long v = std::stoll(s, &pos, 0);
      if (pos == s.size()) return asvJSONValue::makeInt(static_cast<int64_t>(v));
    } catch (const std::out_of_range&) {}
    catch (...) {}
    // Overflow or a partially-consumed literal ("08"): preserve the value as a
    // string instead of silently zeroing it.
    return asvJSONValue::makeString(cppRdStripNumSuffix(tk->text));
  }
  if (tk->kind == Tok::Ident) {
    if (tk->text == "true") return asvJSONValue::makeBool(true);
    if (tk->text == "false") return asvJSONValue::makeBool(false);
    if (tk->text == "nullptr" || tk->text == "NULL") return asvJSONValue::makeNull();
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
};

struct AliasDef {
  std::string name;
  TypeRef type;
  int order = 0;
};

inline bool cppRdIsQual(const std::string& s) {
  return s == "const" || s == "volatile" || s == "static" || s == "constexpr" ||
         s == "consteval" || s == "constinit" || s == "mutable" || s == "inline" ||
         s == "extern" || s == "register" || s == "typename" || s == "struct" ||
         s == "class" || s == "enum" || s == "friend" ||
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
  try { return std::stoll(cppRdStripNumSuffix(s)); } catch (...) { return -1; }
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
      ty.kind = TypeRef::Array;
      if (!groups.empty()) ty.elem = std::make_shared<TypeRef>(cppRdParseTypeV(v, groups[0].first, groups[0].second));
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
           (v[et - 1]->text == "*" || v[et - 1]->text == "&" || v[et - 1]->text == "&&"))
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
inline bool cppRdIsTemplateOpen(const std::vector<Tok>& t, size_t k, size_t limit) {
  int d = 0;
  const size_t end = (limit < t.size()) ? limit : t.size();
  for (size_t j = k; j < end; j++) {
    if (t[j].kind != Tok::Punct) continue;
    const std::string& s = t[j].text;
    if (s == "<") d++;
    else if (s == ">") { d--; if (d == 0) return true; if (d < 0) return false; }
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
      else if (tk.text == "<") { if (cppRdIsTemplateOpen(body, i, body.size())) ad++; }
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
      if (opts.includeComments && t[p].text.find("Generated by asvJSON++") == std::string::npos)
        pending.push_back(Comment{t[p].text, t[p].block});
      p++;
    }
  }

  std::vector<Comment> takePending() {
    std::vector<Comment> r = std::move(pending);
    pending.clear();
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
          (t[p].text == "struct" || t[p].text == "class" || t[p].text == "enum")))
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
  void parseAll() {
    while (!eof()) {
      skipTrivia();
      if (eof()) break;
      if (cur().kind == Tok::Ident) {
        const std::string s = cur().text;
        if (s == "template") { skipTemplateParams(); continue; }
        if ((s == "struct" || s == "class") && looksLikeNestedDef()) {
          for (auto& f : parseStructDecl()) fileScope.push_back(std::move(f));
          continue;
        }
        if (s == "enum" && looksLikeNestedDef()) { parseEnumDecl(); continue; }
        if (s == "typedef") { parseTypedef(); continue; }
        if (s == "using") { parseUsing(); continue; }
        if (s == "namespace") {
          adv(); skipTrivia();
          while (!eof() && !isP("{") && !isP(";")) adv();
          if (isP("{")) skipBalanced("{", "}");
          continue;
        }
      }
      if (isP("{")) { skipBalanced("{", "}"); continue; }
      if (isP(";")) { adv(); continue; }
      adv();
    }
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

  std::vector<FieldDef> parseStructDecl() {
    std::vector<Comment> lead = takePending();
    adv(); // struct / class
    skipTrivia();
    // Skip any bracket attributes: [[nodiscard]], __attribute__((...)), etc.
    while (isP("[")) skipBracketAttr();
    skipTrivia();
    std::string name;
    if (cur().kind == Tok::Ident) { name = cur().text; adv(); }
    skipTrivia();
    if (isP(":")) { skipBases(); skipTrivia(); }
    if (isP(";")) { adv(); return {}; } // forward declaration
    if (!isP("{")) { skipToSemi(); return {}; }
    StructDef def;
    def.name = name;
    def.leading = std::move(lead);
    return parseStructBody(def);
  }

  std::vector<FieldDef> parseStructBody(StructDef& def) {
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
          for (auto& tk : trail) if (tk.kind == Tok::Ident) { def.name = tk.text; break; }
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
            if (eq2 != static_cast<size_t>(-1)) {
              f.hasInit = true;
              for (size_t k = eq2 + 1; k < se; k++) f.init.push_back(trail[k]);
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
          adv(); skipTrivia();
          if (isP(":")) adv();
          else p--; // not a specifier: rewind so parseMember sees the ident
          continue;
        }
        if (s == "template") { skipTemplateParams(); continue; }
        if (s == "typedef") { parseTypedef(); continue; }
        if (s == "using") { parseUsing(); continue; }
        if (s == "friend" || s == "static_assert") { skipToSemi(); continue; }
        if (looksLikeNestedDef()) {
          if (s == "enum") parseEnumDecl();
          else {
            auto flds = parseStructDecl();
            for (auto& f : flds) def.fields.push_back(std::move(f));
          }
          continue;
        }
      }
      parseMember(def);
    }
    return {};
  }

  void parseMember(StructDef& def) {
    std::vector<Comment> lead = takePending();
    const size_t npos = static_cast<size_t>(-1);
    size_t start = p, i = p;
    int paren = 0, ang = 0, brk = 0, brace = 0;
    bool sawEq = false, sawParen = false, methodBody = false;
    size_t end = npos;
    std::vector<size_t> commas;
    bool parsedAny = false;
    while (i < t.size()) {
      const Tok& tk = t[i];
      if (tk.kind == Tok::End) break;
      if (tk.kind == Tok::Comment) { i++; continue; }
      if (tk.kind == Tok::Punct) {
        const std::string& s = tk.text;
        if (s == "(") { if (paren == 0 && ang == 0 && brk == 0 && brace == 0) sawParen = true; paren++; }
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
              while (i < t.size() && d > 0) {
                if (t[i].kind == Tok::Punct && t[i].text == "{") d++;
                else if (t[i].kind == Tok::Punct && t[i].text == "}") d--;
                i++;
              }
              methodBody = true;
              while (t[i].kind == Tok::Comment) i++;
              if (t[i].kind == Tok::Punct && t[i].text == ";") i++;
              break;
            }
          } else brace++;
        }
        else if (s == "}") { if (brace > 0) brace--; else break; }
        else if (s == "," && paren == 0 && ang == 0 && brk == 0 && brace == 0) {
          if (commas.empty() || commas.back() != i) commas.push_back(i);
        }
        else if (s == ";" && paren == 0 && ang == 0 && brk == 0 && brace == 0) { end = i; break; }
        else if (s == "=" && paren == 0 && ang == 0 && brk == 0 && brace == 0 && !sawEq) { sawEq = true; }
      }
      i++;
    }
    if (methodBody) { p = i; return; }
    if (sawParen && !sawEq) { p = (end == npos) ? i : end + 1; return; }
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
      size_t seq = npos;
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
          else if (s2 == "{") Br++;
          else if (s2 == "}") { if (Br > 0) Br--; }
          else if (s2 == "=" && P == 0 && A == 0 && B == 0 && Br == 0) { seq = k; break; }
        }
      }
      size_t lim = (seq == npos) ? se : seq;
      size_t nameI = npos;
      for (size_t k = lim; k > sb; k--)
        if (t[k - 1].kind == Tok::Ident) { nameI = k - 1; break; }
      if (nameI == npos) {
        if (opts.strict) throw asvJSONError("cpp: cannot parse member declaration");
        continue;
      }
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
      size_t suf = nameI + 1;
      while (suf < lim) {
        if (t[suf].kind == Tok::Punct && t[suf].text == "[") {
          size_t j = suf + 1;
          std::string num;
          while (j < lim && !(t[j].kind == Tok::Punct && t[j].text == "]")) {
            if (t[j].kind == Tok::Number) num = t[j].text;
            j++;
          }
          TypeRef a;
          a.kind = TypeRef::Array;
          a.elem = std::make_shared<TypeRef>(f.type);
          if (!num.empty()) {
            try { a.arrayLen = static_cast<size_t>(std::stoull(cppRdStripNumSuffix(num))); a.hasLen = true; } catch (...) {}
          }
          f.type = a;
          suf = (j < lim) ? j + 1 : lim;
        } else {
          suf++;
        }
      }
      if (seq != npos) {
        f.hasInit = true;
        for (size_t k = seq + 1; k < se; k++) f.init.push_back(t[k]);
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

  void parseEnumDecl() {
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
    if (isP(";")) { adv(); return; }
    if (!isP("{")) { skipToSemi(); return; }
    adv(); // '{'
    EnumDef def;
    def.name = name;
    while (!eof() && !isP("}")) {
      skipTrivia();
      if (isP("}")) break;
      if (isP(",")) { adv(); continue; }
      if (cur().kind == Tok::Ident) {
        def.members.push_back(cur().text);
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
    if (isP("}")) adv();
    skipTrivia();
    if (def.name.empty() && cur().kind == Tok::Ident) { def.name = cur().text; adv(); }
    while (!eof() && !isP(";")) adv();
    if (isP(";")) adv();
    if (!def.name.empty()) enums.push_back(std::move(def));
  }

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

  void parseTypedef() {
    takePending();
    adv(); // typedef
    skipTrivia();
    if (isKw("struct") || isKw("class")) { parseStructDecl(); return; }
    if (isKw("enum")) { parseEnumDecl(); return; }
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
  void resolve(TypeRef& ty) const {
    if (ty.kind != TypeRef::Unknown) return;
    if (findStruct(ty.name)) { ty.kind = TypeRef::Struct; return; }
    if (findEnum(ty.name)) { ty.kind = TypeRef::Enum; return; }
    ty.kind = TypeRef::Scalar;
    ty.scalar = cppRdClassify(ty.name);
  }

  static std::unique_ptr<asvJSONValue> mkStr(std::string s) {
    return asvJSONValue::makeString(std::move(s));
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
      try { return asvJSONValue::makeDouble(std::stod(s)); } catch (...) { return asvJSONValue::makeString(cppRdStripNumSuffix(raw)); }
    }
    if (s == "-9223372036854775808")
      return asvJSONValue::makeInt((std::numeric_limits<long long>::min)());
    try {
      std::string::size_type pos = 0;
      long long v = std::stoll(s, &pos, 0);
      if (pos == s.size()) return asvJSONValue::makeInt(static_cast<int64_t>(v));
    } catch (const std::out_of_range&) {}
    catch (...) {}
    return asvJSONValue::makeString(cppRdStripNumSuffix(raw));
  }

  std::unique_ptr<asvJSONValue> materialize(const TypeRef& t,
                                            const std::vector<Tok>& init,
                                            bool hasInit, int depth) {
    if (depth > static_cast<int>(asvJSONValue::MAX_NESTING_DEPTH))
      throw asvJSONError("cpp: nesting too deep");
    TypeRef rt = t;
    resolve(rt);

    bool grp = hasInit && cppRdBraceGroup(init);
    std::vector<std::vector<Tok>> groups;
    if (grp) groups = cppRdInitGroups(init);
    bool onlyEmptyGroup = groups.size() == 1 && groups[0].empty();

    if (rt.kind == TypeRef::Struct) {
      auto o = asvJSONValue::makeObject();
      const StructDef* sd = findStruct(rt.name);
      if (sd) {
        for (size_t idx = 0; idx < sd->fields.size(); idx++) {
          const FieldDef& f = sd->fields[idx];
          std::vector<Tok> fi;
          bool hi = false;
          if (grp && idx < groups.size() && !groups[idx].empty()) { fi = groups[idx]; hi = true; }
          else { fi = f.init; hi = f.hasInit; }
          auto child = materialize(f.type, fi, hi, depth + 1);
          if (!child) continue;
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
      if (hasInit && !grp) {
        std::string v = cppRdLastIdent(init);
        if (!v.empty()) return asvJSONValue::makeString(std::move(v));
      }
      return asvJSONValue::makeNull();
    }

    if (rt.kind == TypeRef::Array) {
      auto a = asvJSONValue::makeArray();
      TypeRef elem;
      if (rt.elem) { elem = *rt.elem; resolve(elem); }
      const EnumDef* ed = (elem.kind == TypeRef::Enum) ? findEnum(elem.name) : nullptr;
      if (grp && !onlyEmptyGroup) {
        for (auto& g : groups)
          a->arr->push_back(materialize(elem, g, !g.empty(), depth + 1));
      } else if (rt.hasLen) {
        if (!asvJSONValue::checkArraySize(rt.arrayLen)) throw asvJSONError("CPP array too large");
        for (size_t k = 0; k < rt.arrayLen; k++) {
          if (ed && ed->members.size() == rt.arrayLen)
            a->arr->push_back(mkStr(ed->members[k]));
          else
            a->arr->push_back(materialize(elem, std::vector<Tok>(), false, depth + 1));
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
                return asvJSONValue::makeBool(v->text == "true");
            }
            if (at.kind == TypeRef::Enum && v->kind == Tok::Ident)
              return asvJSONValue::makeString(cppRdLastIdent(init));
          }
        }
      }
      if (grp && groups.size() == 1)
        return materialize(rt.alts.empty() ? TypeRef{} : *rt.alts[0], groups[0], !groups[0].empty(), depth + 1);
      return materialize(rt.alts.empty() ? TypeRef{} : *rt.alts[0], std::vector<Tok>(), false, depth + 1);
    }

    if (rt.kind == TypeRef::Optional) {
      if (!hasInit) return asvJSONValue::makeNull();
      const Tok* v = cppRdFirstVal(init);
      if (v && v->kind == Tok::Ident &&
          (v->text == "nullopt" || v->text == "NULL" || v->text == "nullptr"))
        return asvJSONValue::makeNull();
      return materialize(rt.elem ? *rt.elem : TypeRef{}, init, hasInit, depth + 1);
    }

    if (rt.kind == TypeRef::Map) {
      // `std::map<K, V> m = {{k1, v1}, {k2, v2}};` -- the initializer groups
      // were computed but never looked at, so every map decoded as {} and its
      // contents vanished. Each group is one braced {key, value} pair (the
      // only shape a map initializer can take, since the comma between key and
      // value must sit inside the inner braces).
      auto o = asvJSONValue::makeObject();
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
          o->obj->emplace(std::move(name), std::move(val));
        }
      }
      return o;
    }

    // scalar / unresolved
    if (hasInit && !grp) {
      if (auto lit = cppRdScalarLiteral(init, rt.scalar == TypeRef::SDouble)) return lit;
      const Tok* v = cppRdFirstVal(init);
      if (v && v->kind == Tok::Ident) {
        std::string id = cppRdLastIdent(init);
        if (!id.empty()) return asvJSONValue::makeString(std::move(id));
      }
    }
    switch (rt.scalar) {
      case TypeRef::SBool: return asvJSONValue::makeBool(false);
      case TypeRef::SDouble: return asvJSONValue::makeDouble(0.0);
      case TypeRef::SString: return asvJSONValue::makeString(std::string());
      case TypeRef::SNull: return asvJSONValue::makeNull();
      case TypeRef::SInt: return asvJSONValue::makeInt(0);
      default: return asvJSONValue::makeNull();
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
      out->obj->emplace(f.name, std::move(val));
    }
  }

  std::unique_ptr<asvJSONValue> buildRoot() {
    if (!aliases.empty()) {
      // First-declared (lowest order) alias wins - not the last one seen.
      const AliasDef* best = &aliases[0];
      for (auto& a : aliases) if (a.order < best->order) best = &a;
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
      for (auto* s : list) {
        TypeRef st;
        st.kind = TypeRef::Struct;
        st.name = s->name;
        auto val = materialize(st, std::vector<Tok>(), false, 0);
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
        TypeRef st;
        st.kind = TypeRef::Struct;
        st.name = s->name;
        auto val = materialize(st, std::vector<Tok>(), false, 0);
        if (val && opts.includeComments && (!s->leading.empty() || !s->closing.empty())) {
          val->comments.reset(new Comments());
          val->comments->leading = s->leading;
          val->comments->closing = s->closing;
        }
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
    std::vector<const StructDef*> list = {&structs[0]};
    std::unique_ptr<asvJSONValue> out = wrap(list);
    addFileScopeInst(out, list);
    return out;
  }
};

} // namespace cppread

inline bool asvJSON::fromCpp(std::string_view input, const CppReadOptions& opts) {
  formatType = FormatType::Unknown;
  if (input.empty()) { lastError = "empty input"; return false; }
  dirty_ = false;
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
    if (!val) { lastError = "cpp: no type declarations found"; return false; }
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
