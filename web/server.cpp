/* asvJSON++ Converter — standalone cross-platform HTTP server
   Serves static frontend and handles /api/convert POST.
   No external dependencies — uses BSD sockets (POSIX/WinSock).
*/

// Include asvJSON before Windows headers to avoid TRUE/FALSE macro conflicts
#include "../asvjson/asvjson.hpp"

/* ---- Platform abstraction ---- */
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  // Undef macros that conflict with enum values in tron.hpp/goon.hpp/etc.
  #ifdef TRUE
    #undef TRUE
  #endif
  #ifdef FALSE
    #undef FALSE
  #endif
  #ifdef NUL
    #undef NUL
  #endif
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET socket_t;
  #define SOCK_CLOSE(fd)  closesocket(fd)
  #define SOCK_ERR        SOCKET_ERROR
  #define SOCK_INVALID    INVALID_SOCKET
  #define SOCK_ERRNO      WSAGetLastError()
  #define SOCK_NONBLOCK(fd) \
    { u_long mode = 1; ioctlsocket(fd, FIONBIO, &mode); }
  static bool sockInit() {
    WSADATA wsa; return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
  }
  static void sockCleanup() { WSACleanup(); }
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <dirent.h>
  #include <sys/stat.h>
  typedef int socket_t;
  #define SOCK_CLOSE(fd)  ::close(fd)
  #define SOCK_ERR        (-1)
  #define SOCK_INVALID    (-1)
  #define SOCK_ERRNO      errno
  #define SOCK_NONBLOCK(fd) \
    { int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK); }
  static bool sockInit() { return true; }
  static void sockCleanup() {}
#endif

#include <iostream>
#include <fstream>
#include <atomic>
#include <ctime>
#include <chrono>

/* ---- Logging ---- */
struct LogInfo {
  std::string fromFmt;
  std::string toFmt;
  double durationMs = 0;
  bool success = false;
  std::string error;
};
static void logRequest(const std::string& clientIp, const std::string& raw, const LogInfo* info = nullptr) {
#ifdef _WIN32
  ::CreateDirectoryA("logs", NULL);
#else
  ::mkdir("logs", 0755);
#endif
  std::time_t now = std::time(nullptr);
  struct tm t;
#ifdef _WIN32
  localtime_s(&t, &now);
#else
  localtime_r(&now, &t);
#endif
  char fname[64];
  std::strftime(fname, sizeof(fname), "logs/%Y%m%d_%H%M%S", &t);
  static std::atomic<int> seq{0};
  int n = seq++;
  char suffix[16];
  std::snprintf(suffix, sizeof(suffix), "_%04d.log", n % 10000);
  std::string path = std::string(fname) + suffix;
  std::ofstream log(path.c_str());
  if (log) {
    log << "IP: " << clientIp << "\n";
    if (info && !info->fromFmt.empty()) {
      log << "From: " << info->fromFmt << " -> To: " << info->toFmt << "\n";
      log << "Duration: " << info->durationMs << " ms\n";
      log << "Status: " << (info->success ? "OK" : "ERROR") << "\n";
      if (!info->error.empty()) log << "Error: " << info->error << "\n";
    }
    log << "---\n";
    log << raw;
  }
}

/* ---- Base64 ---- */
static std::string base64Encode(const std::vector<uint8_t>& buf) {
  static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (auto c : buf) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) { out.push_back(b64[(val >> valb) & 0x3F]); valb -= 6; }
  }
  if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

static std::vector<uint8_t> base64Decode(const std::string& in) {
  static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int T[256]; std::memset(T, -1, sizeof(T));
  for (int i = 0; i < 64; i++) T[(int)b64[i]] = i;
  std::vector<uint8_t> out;
  int val = 0, valb = -8;
  for (auto c : in) {
    if (T[(int)c] == -1) continue;
    val = (val << 6) + T[(int)c];
    valb += 6;
    if (valb >= 0) { out.push_back(uint8_t((val >> valb) & 0xFF)); valb -= 8; }
  }
  return out;
}

/* ---- Strip binary preamble ---- */
static std::string stripBinaryPreamble(const std::string& s) {
  if (s.find("[Binary data") == 0) {
    auto n = s.find("\n\n");
    if (n != std::string::npos) return s.substr(n + 2);
  }
  return s;
}

/* ---- JSON helpers ---- */
static std::string jsonEscape(const std::string& s) {
  std::string out; out.reserve(s.size() + 8); out += '"';
  for (auto c : s) {
    switch (c) {
      case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break; case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break; case '\f': out += "\\f"; break;
      case '\b': out += "\\b"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          out += buf;
        } else { out += c; }
    }
  }
  out += '"'; return out;
}

static std::string jsonMsg(bool ok, const std::string& data, bool binary, const std::string& err) {
  if (ok) return "{\"success\":true,\"data\":" + jsonEscape(data) + ",\"binary\":" + (binary?"true":"false") + "}";
  return "{\"success\":false,\"error\":" + jsonEscape(err) + "}";
}

/* ---- MIME types ---- */
static std::string mimeType(const std::string& path) {
  auto ext = path.substr(path.rfind('.') + 1);
  if (ext == "html") return "text/html; charset=utf-8";
  if (ext == "css")  return "text/css; charset=utf-8";
  if (ext == "js")   return "application/javascript; charset=utf-8";
  if (ext == "json") return "application/json; charset=utf-8";
  if (ext == "png")  return "image/png";
  if (ext == "svg")  return "image/svg+xml";
  if (ext == "ico")  return "image/x-icon";
  if (ext == "woff2") return "font/woff2";
  if (ext == "woff") return "font/woff";
  return "application/octet-stream";
}

/* ---- URL decode ---- */
static std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      unsigned int c = 0;
      if (std::sscanf(s.data() + i + 1, "%2x", &c) != 1) { out += '%'; continue; }
      out += char(c); i += 2;
    } else if (s[i] == '+') { out += ' '; }
    else { out += s[i]; }
  }
  return out;
}

/* ---- Path sanitize ---- */
static bool isSafePath(const std::string& path) {
  if (path.empty() || path[0] != '/') return false;
  if (path.find("..") != std::string::npos) return false;
  if (path.find("//") != std::string::npos) return false;
  if (path.find(':') != std::string::npos) return false;
  if (path.find('\\') != std::string::npos) return false;
  if (path.size() > 4096) return false;
  return true;
}

/* ---- Conversion logic (inline from convert.cpp) ---- */
static std::string doConvert(const std::string& fromFmt, const std::string& toFmt,
                             const std::string& textData, const std::vector<uint8_t>& binData,
                             bool binaryInput, bool& outBinary, bool pretty = true)
{
  try {
    asvJSON j;

    if (fromFmt.empty() || fromFmt == "JSON") {
      if (!j.parse(textData)) return jsonMsg(false, "", false, j.getLastError());
    } else {
      // For binary formats with text input, decode base64
      std::vector<uint8_t> resolvedBin = binData;
      if (!binaryInput && (fromFmt == "MessagePack" || fromFmt == "BSON" || fromFmt == "CBOR" || fromFmt == "Protobuf")) {
        resolvedBin = base64Decode(stripBinaryPreamble(textData));
      }

      bool ok = false;
      if (fromFmt == "JSON5") ok = j.fromJSON5(textData);
      else if (fromFmt == "XML") ok = j.fromXML(textData);
      else if (fromFmt == "YAML") ok = j.fromYAML(textData);
      else if (fromFmt == "CSV") ok = j.fromCSV(textData);
      else if (fromFmt == "TOML") ok = j.fromTOML(textData);
      else if (fromFmt == "INI") ok = j.fromINI(textData);
      else if (fromFmt == "UDE") ok = j.fromUDE(textData);
      else if (fromFmt == "JSONLines") ok = j.fromJSONLines(textData);
      else if (fromFmt == "Sexpr") ok = j.fromSexpr(textData);
      else if (fromFmt == "TOON") ok = j.fromTOON(textData);
      else if (fromFmt == "TRON") ok = j.fromTRON(textData);
      else if (fromFmt == "GOON") ok = j.fromGOON(textData);
      else if (fromFmt == "ProtobufText") ok = j.fromProtobufText(textData);
      else if (fromFmt == "MessagePack") ok = j.fromMessagePack(resolvedBin.data(), resolvedBin.size());
      else if (fromFmt == "BSON") ok = j.fromBSON(resolvedBin.data(), resolvedBin.size());
      else if (fromFmt == "CBOR") ok = j.fromCBOR(resolvedBin.data(), resolvedBin.size());
      else if (fromFmt == "Protobuf") ok = j.fromProtobuf(resolvedBin.data(), resolvedBin.size());
      else return jsonMsg(false, "", false, "Unknown input format: " + fromFmt);
      if (!ok) return jsonMsg(false, "", false, j.getLastError());
    }

    outBinary = (toFmt == "MessagePack" || toFmt == "BSON" || toFmt == "CBOR" || toFmt == "Protobuf");

    if (outBinary) {
      std::vector<uint8_t> binOut;
      std::string strOut;
      if (toFmt == "MessagePack") binOut = j.toMessagePack();
      else if (toFmt == "CBOR") binOut = j.toCBOR();
      else if (toFmt == "BSON") strOut = j.toBSON();
      else if (toFmt == "Protobuf") binOut = j.toProtobuf();

      if (toFmt == "BSON") {
        std::vector<uint8_t> vec(strOut.begin(), strOut.end());
        return jsonMsg(true, base64Encode(vec), true, "");
      }
      return jsonMsg(true, base64Encode(binOut), true, "");
    }

    std::string result;
    if (toFmt.empty() || toFmt == "JSON") result = pretty ? j.serialize(true) : j.serialize();
    else if (toFmt == "JSON5") result = pretty ? j.toJSON5(true) : j.toJSON5(false);
    else if (toFmt == "XML") result = j.toXML();
    else if (toFmt == "YAML") result = j.toYAML();
    else if (toFmt == "CSV") result = j.toCSV();
    else if (toFmt == "TOML") result = j.toTOML();
    else if (toFmt == "INI") result = j.toINI();
    else if (toFmt == "UDE") result = j.toUDE();
    else if (toFmt == "JSONLines") result = j.toJSONLines();
    else if (toFmt == "Sexpr") result = j.toSexpr();
    else if (toFmt == "TOON") result = j.toTOON();
    else if (toFmt == "TRON") result = j.toTRON();
    else if (toFmt == "GOON") result = j.toGOON();
    else if (toFmt == "ProtobufText") result = j.toProtobufText();
    else return jsonMsg(false, "", false, "Unknown output format: " + toFmt);

    return jsonMsg(true, result, false, "");
  }
  catch (const std::exception& e) {
    return jsonMsg(false, "", false, e.what());
  }
}

/* ---- HTTP Server ---- */
class Server {
public:
  Server(int port, const std::string& publicDir)
    : port_(port), publicDir_(publicDir), sock_(SOCK_INVALID) {}

  bool start() {
    if (!sockInit()) { std::cerr << "Failed to init sockets\n"; return false; }

    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == SOCK_INVALID) { std::cerr << "socket() failed\n"; return false; }

    int opt = 1;
#ifdef _WIN32
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif



    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) == SOCK_ERR) {
      std::cerr << "bind() failed on port " << port_ << "\n"; return false;
    }

    if (::listen(sock_, 16) == SOCK_ERR) {
      std::cerr << "listen() failed\n"; return false;
    }

    std::cout << "Listening on http://0.0.0.0:" << port_ << "\n";
    std::cout << "Public dir: " << publicDir_ << "\n";
    return true;
  }

  void run() {
    while (true) {
      struct sockaddr_in client;
      socklen_t clientLen = sizeof(client);
      socket_t clientSock = ::accept(sock_, (struct sockaddr*)&client, &clientLen);
      if (clientSock == SOCK_INVALID) {
        if (SOCK_ERRNO == EINTR) continue;
        std::cerr << "accept() failed\n"; break;
      }
      char ipStr[64];
      std::snprintf(ipStr, sizeof(ipStr), "%s", inet_ntoa(client.sin_addr));
      handleClient(clientSock, ipStr);
      SOCK_CLOSE(clientSock);
    }
  }

  ~Server() {
    if (sock_ != SOCK_INVALID) SOCK_CLOSE(sock_);
    sockCleanup();
  }

private:
  int port_;
  std::string publicDir_;
  socket_t sock_;

  /* ---- Recv all (blocking with timeout, size limit) ---- */
  std::string recvAll(socket_t fd) {
    std::string buf;
    char tmp[4096];
    const size_t MAX_BODY = 16 * 1024 * 1024; // 16 MB

    while (true) {
      int r = ::recv(fd, tmp, sizeof(tmp), 0);
      if (r > 0) {
        buf.append(tmp, r);
        if (buf.size() > MAX_BODY) return buf; // too large, return what we have
        if (buf.size() < 4) continue;

        // Check if we have full headers
        auto hl = buf.find("\r\n\r\n");
        if (hl == std::string::npos) continue;

        // Figure expected body length
        size_t headerEnd = hl + 4;
        size_t bodyHave = buf.size() - headerEnd;

        // Try Content-Length (case-insensitive)
        size_t clPos = std::string::npos;
        auto clCandidate = buf.find("Content-Length:");
        if (clCandidate == std::string::npos) clCandidate = buf.find("content-length:");
        if (clCandidate == std::string::npos) {
          // Check for Transfer-Encoding: chunked
          if (buf.find("Transfer-Encoding: chunked") != std::string::npos ||
              buf.find("transfer-encoding: chunked") != std::string::npos) {
            // For simplicity, assume end of chunked stream (connection close)
            // A full implementation would parse chunk sizes
            return buf;
          }
          // No body indicator — assume done
          return buf;
        }

        // Parse Content-Length value (skip past "Content-Length:" / "content-length:")
        size_t colonPos = buf.find(':', clCandidate);
        if (colonPos == std::string::npos) return buf;
        clPos = buf.find_first_of("0123456789", colonPos + 1);
        if (clPos == std::string::npos) return buf; // malformed, return what we have

        size_t bodyLen = 0;
        try {
          bodyLen = std::stoul(buf.substr(clPos));
        } catch (...) {
          return buf; // invalid Content-Length value
        }

        if (bodyLen > MAX_BODY) return buf;

        if (bodyHave >= bodyLen) return buf; // got full body
      } else if (r == 0) {
        return buf; // connection closed
      } else {
        return buf; // error
      }
    }
  }

  /* ---- HTTP response ---- */
  void sendResponse(socket_t fd, int status, const std::string& statusText,
                    const std::string& contentType,
                    const std::string& body,
                    const std::string& extraHeaders = "") {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " " << statusText << "\r\n"
         << "Content-Type: " << contentType << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
         << "Access-Control-Allow-Headers: Content-Type\r\n"
         << extraHeaders
         << "\r\n"
         << body;
    std::string respStr = resp.str();
    auto sent = ::send(fd, respStr.data(), (int)respStr.size(), 0);
    (void)sent;
  }

  /* ---- Static file serving ---- */
  void serveFile(socket_t fd, const std::string& path) {
    std::string filePath = publicDir_ + path;
    if (path == "/" || path.empty()) filePath = publicDir_ + "/index.html";

    std::ifstream file(filePath.c_str(), std::ios::binary);
    if (!file) {
      std::string msg = "<h1>404 Not Found</h1>";
      sendResponse(fd, 404, "Not Found", "text/html", msg);
      return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Cache control for static assets
    std::string cacheHdr;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
      std::string ext = path.substr(dot);
      if (ext == ".png" || ext == ".svg" || ext == ".woff2") {
        cacheHdr = "Cache-Control: public, max-age=86400\r\n";
      }
    }

    sendResponse(fd, 200, "OK", mimeType(filePath), content, cacheHdr);
  }

  /* ---- API handler ---- */
  void handleAPI(socket_t fd, const std::string& body, const std::string& clientIp, const std::string& raw) {
    asvJSON req;
    if (!req.parse(body)) {
      LogInfo li;
      li.fromFmt = "?"; li.toFmt = "?"; li.success = false; li.error = "Invalid JSON";
      logRequest(clientIp, raw, &li);
      sendResponse(fd, 400, "Bad Request", "application/json",
                   jsonMsg(false, "", false, "Invalid JSON"));
      return;
    }

    std::string fromFmt = req.getString("from");
    std::string toFmt = req.getString("to");
    bool binaryInput = req.getBool("binary");
    bool pretty = req.optBool("pretty", true);
    std::string textData;
    std::vector<uint8_t> binData;

    if (binaryInput) {
      std::string b64 = stripBinaryPreamble(req.getString("data"));
      binData = base64Decode(b64);
    } else {
      textData = req.getString("data");
    }

    auto t0 = std::chrono::steady_clock::now();
    bool outBinary = false;
    std::string result = doConvert(fromFmt, toFmt, textData, binData, binaryInput, outBinary, pretty);
    auto t1 = std::chrono::steady_clock::now();
    double durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Check if result indicates error
    bool success = true;
    std::string error;
    {
      asvJSON check;
      if (check.parse(result)) {
        success = check.getBool("ok");
        if (!success) error = check.getString("error");
      }
    }

    LogInfo li;
    li.fromFmt = fromFmt;
    li.toFmt = toFmt;
    li.durationMs = durationMs;
    li.success = success;
    li.error = error;
    logRequest(clientIp, raw, &li);

    sendResponse(fd, 200, "OK", "application/json; charset=utf-8", result);
  }

  /* ---- Request dispatch ---- */
  void handleClient(socket_t fd, const std::string& clientIp) {
    // Set receive timeout
#ifdef _WIN32
    DWORD rcvto = 10000;
    DWORD sndto = 10000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvto, sizeof(rcvto));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sndto, sizeof(sndto));
#else
    struct timeval tv = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    std::string raw = recvAll(fd);
    if (raw.empty()) return;

    // Log all requests
    logRequest(clientIp, raw);

    // Parse request line
    auto firstLine = raw.substr(0, raw.find("\r\n"));
    auto methodEnd = firstLine.find(' ');
    if (methodEnd == std::string::npos) return;
    std::string method = firstLine.substr(0, methodEnd);

    auto pathEnd = firstLine.find(' ', methodEnd + 1);
    if (pathEnd == std::string::npos) return;
    std::string path = urlDecode(firstLine.substr(methodEnd + 1, pathEnd - methodEnd - 1));

    // Extract body
    auto hdrEnd = raw.find("\r\n\r\n");
    std::string body;
    if (hdrEnd != std::string::npos) {
      body = raw.substr(hdrEnd + 4);
    }

    // CORS preflight
    if (method == "OPTIONS") {
      sendResponse(fd, 204, "No Content", "", "");
      return;
    }

    if (method == "GET") {
      if (!isSafePath(path)) {
        sendResponse(fd, 403, "Forbidden", "text/html", "<h1>403 Forbidden</h1>");
        return;
      }
      serveFile(fd, path);
    }
    else if (method == "POST" && path == "/api/convert") {
      handleAPI(fd, body, clientIp, raw);
    }
    else {
      sendResponse(fd, 405, "Method Not Allowed", "text/html", "<h1>405 Method Not Allowed</h1>");
    }
  }
};

int main(int argc, char* argv[]) {
  int port = 3001;
  if (argc >= 2) port = std::atoi(argv[1]);
  if (port <= 0 || port > 65535) port = 3001;

  // Determine public directory: look for ./public/ relative to executable
  std::string exeDir;
  if (argc >= 3) {
    exeDir = argv[2];
  } else {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, sizeof(buf));
    exeDir = buf;
    auto pos = exeDir.rfind('\\');
    if (pos != std::string::npos) exeDir = exeDir.substr(0, pos);
#else
    exeDir = ".";
#endif
  }
  std::string publicDir = exeDir + "/public";

  Server srv(port, publicDir);
  if (!srv.start()) return 1;
  srv.run();
  return 0;
}
