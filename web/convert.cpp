#include "../asvjson/asvjson.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

static std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  out += '"';
  for (auto c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\f': out += "\\f"; break;
      case '\b': out += "\\b"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
  return out;
}

static std::string jsonError(const std::string& msg) {
  return "{\"success\":false,\"error\":" + jsonEscape(msg) + "}";
}

static std::string jsonSuccess(const std::string& data, bool binary) {
  return "{\"success\":true,\"data\":" + jsonEscape(data) + ",\"binary\":" + (binary ? "true" : "false") + "}";
}

static std::string base64Encode(const std::vector<uint8_t>& buf) {
  static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : buf) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(b64[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

static std::vector<uint8_t> base64Decode(const std::string& in) {
  std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++) T[b64[i]] = i;
  std::vector<uint8_t> out;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (T[c] == -1) continue;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  return out;
}

int main(int argc, char** argv) {
  bool rawMode = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--raw") == 0) { rawMode = true; continue; }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      std::cout
        << "asvJSON++ Format Converter v1.25.5\n"
           "Usage: '{\"from\":\"YAML\",\"to\":\"JSON\",\"data\":\"key: val\"}' | convert.exe  (PowerShell)\n"
           "       echo {\"from\":\"YAML\",\"to\":\"JSON\",\"data\":\"key: val\"} | convert.exe  (cmd)\n"
           "       convert.exe --raw < input.txt  (raw output, no JSON wrapper)\n"
           "\n"
           "Reads a JSON request from stdin, writes JSON response to stdout.\n"
"Request:  {\"from\":\"<fmt>\",\"to\":\"<fmt>\",\"data\":\"...\",\"binary\":false}\n"
            "Response: {\"success\":true,\"data\":\"...\",\"binary\":false}\n"
           "\n"
           "  --raw    print converted content directly, no JSON wrapper\n"
           "\n"
"Formats: JSON, JSON5, XML, YAML, CSV, TOML, INI, Dotenv, JavaProp, Plist, Bencode, UBJSON, UDE, JSONLines,\n"
            "         Sexpr, RON, TOON, TRON, GOON, ProtobufText, MessagePack, BSON, CBOR, Protobuf\n"
            "Outputs: ... plus CPP (C++ structs/enums) and CPP_C (C structs/enums)\n"
           "         (empty from=JSON, to=JSON)\n";
      return 0;
    }
  }

  std::string line;
  if (!std::getline(std::cin, line)) {
    std::cout << "Usage: convert.exe [-h|--help] [--raw]\n"
              << "       '{\"from\":\"YAML\",\"to\":\"JSON\",\"data\":\"key: val\"}' | convert.exe\n"
              << "       echo {\"from\":\"YAML\",\"to\":\"JSON\",\"data\":\"key: val\"} | convert.exe\n";
    return 1;
  }

  asvJSON req;
  if (!req.parse(line)) {
    if (rawMode) { std::cerr << "Error: Invalid request JSON\n"; }
    else { std::cout << jsonError("Invalid request JSON"); }
    return 1;
  }

  std::string fromFmt = req.getString("from");
  std::string toFmt = req.getString("to");
  bool binaryInput = req.getBool("binary");

  try {
    asvJSON j;

    if (fromFmt.empty() || fromFmt == "JSON") {
      std::string text = req.getString("data");
      if (!j.parse(text)) {
        if (rawMode) { std::cerr << "Error: " << j.getLastError() << "\n"; }
        else { std::cout << jsonError(j.getLastError()); }
        return 1;
      }
    } else {
      std::string text;
      std::vector<uint8_t> bin;

      auto stripPreamble = [](std::string s) -> std::string {
        if (s.find("[Binary data") == 0) {
          auto n = s.find("\n\n");
          if (n != std::string::npos) s = s.substr(n + 2);
        }
        return s;
      };

      if (binaryInput) {
        std::string b64 = stripPreamble(req.getString("data"));
        bin = base64Decode(b64);
      } else {
        text = req.getString("data");
        // For binary formats, decode base64 text to binary
        if (fromFmt == "MessagePack" || fromFmt == "BSON" || fromFmt == "CBOR" || fromFmt == "Protobuf" || fromFmt == "UBJSON") {
          bin = base64Decode(stripPreamble(text));
        }
      }

      bool ok = false;
      if (fromFmt.empty()) {
        ok = j.fromAny(text);
      } else if (fromFmt == "JSON5") ok = j.fromJSON5(text);
      else if (fromFmt == "XML") ok = j.fromXML(text);
      else if (fromFmt == "YAML") ok = j.fromYAML(text);
      else if (fromFmt == "CSV") ok = j.fromCSV(text);
      else if (fromFmt == "TOML") ok = j.fromTOML(text);
      else if (fromFmt == "INI") ok = j.fromINI(text);
      else if (fromFmt == "Dotenv") ok = j.fromDotenv(text);
      else if (fromFmt == "JavaProp") ok = j.fromJavaProp(text);
      else if (fromFmt == "Plist") ok = j.fromPlist(text);
      else if (fromFmt == "Bencode") {
        if (binaryInput) {
          ok = j.fromBencode(std::string_view(reinterpret_cast<const char*>(bin.data()), bin.size()));
        } else {
          ok = j.fromBencode(text);
        }
      }
      else if (fromFmt == "UDE") ok = j.fromUDE(text);
      else if (fromFmt == "JSONLines") ok = j.fromJSONLines(text);
      else if (fromFmt == "Sexpr") ok = j.fromSexpr(text);
      else if (fromFmt == "RON") ok = j.fromRON(text);
      else if (fromFmt == "TOON") ok = j.fromTOON(text);
      else if (fromFmt == "TRON") ok = j.fromTRON(text);
      else if (fromFmt == "GOON") ok = j.fromGOON(text);
      else if (fromFmt == "ProtobufText") ok = j.fromProtobufText(text);
      else if (fromFmt == "MessagePack") { ok = j.fromMessagePack(bin.data(), bin.size()); }
      else if (fromFmt == "BSON") { ok = j.fromBSON(bin.data(), bin.size()); }
      else if (fromFmt == "CBOR") { ok = j.fromCBOR(bin.data(), bin.size()); }
      else if (fromFmt == "Protobuf") { ok = j.fromProtobuf(bin.data(), bin.size()); }
      else if (fromFmt == "UBJSON") { ok = j.fromUBJSON(bin.data(), bin.size()); }
      else if (fromFmt == "CPP" || fromFmt == "CPP_C") ok = j.fromCpp(text);
      else { std::cout << jsonError("Unknown input format: " + fromFmt); return 1; }

      if (!ok) {
        if (rawMode) { std::cerr << "Error: " << j.getLastError() << "\n"; }
        else { std::cout << jsonError(j.getLastError()); }
        return 1;
      }
    }

    // Serialize output
    bool binaryOutput = (toFmt == "MessagePack" || toFmt == "BSON" || toFmt == "CBOR" || toFmt == "Protobuf" || toFmt == "UBJSON");

    if (binaryOutput) {
      std::vector<uint8_t> binOut;
      if (toFmt == "MessagePack") binOut = j.toMessagePack();
      else if (toFmt == "CBOR") binOut = j.toCBOR();
      else if (toFmt == "BSON") binOut = j.toBSON();
      else if (toFmt == "Protobuf") binOut = j.toProtobuf();
      else if (toFmt == "UBJSON") binOut = j.toUBJSON();

      std::cout << jsonSuccess(base64Encode(binOut), true);
    } else {
      std::string result;
      if (toFmt.empty() || toFmt == "JSON") result = req.optBool("pretty", true) ? j.serialize(true) : j.serialize();
      else if (toFmt == "JSON5") result = req.optBool("pretty", true) ? j.toJSON5(true) : j.toJSON5(false);
      else if (toFmt == "XML") result = j.toXML();
      else if (toFmt == "YAML") result = j.toYAML(req.optBool("pretty", true));
      else if (toFmt == "CSV") result = j.toCSV();
      else if (toFmt == "TOML") result = j.toTOML(req.optBool("pretty", true));
      else if (toFmt == "INI") result = j.toINI(req.optBool("pretty", true));
      else if (toFmt == "Dotenv") result = j.toDotenv();
      else if (toFmt == "JavaProp") result = j.toJavaProp();
      else if (toFmt == "Plist") result = j.toPlist();
      else if (toFmt == "Bencode") result = j.toBencode();
      else if (toFmt == "UDE") result = j.toUDE();
      else if (toFmt == "JSONLines") result = j.toJSONLines();
      else if (toFmt == "Sexpr") result = j.toSexpr();
      else if (toFmt == "RON") result = j.toRON(req.optBool("pretty", true));
      else if (toFmt == "TOON") result = j.toTOON();
      else if (toFmt == "TRON") result = j.toTRON();
      else if (toFmt == "GOON") result = j.toGOON();
      else if (toFmt == "ProtobufText") result = j.toProtobufText();
      else if (toFmt == "CPP") result = j.toCpp();
      else if (toFmt == "CPP_C") {
        CppWriteOptions co;
        co.style = CppWriteOptions::Style::C;
        result = j.toCpp(co);
      }
      else { std::cout << jsonError("Unknown output format: " + toFmt); return 1; }

      if (rawMode) { std::cout << result; }
      else { std::cout << jsonSuccess(result, false); }
    }

  } catch (const std::exception& e) {
    std::cout << jsonError(e.what());
    return 1;
  }

  return 0;
}
