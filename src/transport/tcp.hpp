#pragma once
// Internal framed TCP transport over Winsock. NOT installed as public API.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include "communication_planner/protocol/protocol.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace communication_planner::transport {

inline bool libraryInit() {
  WSADATA d;
  return WSAStartup(MAKEWORD(2, 2), &d) == 0;
}
inline void libraryCleanup() { WSACleanup(); }
inline void lastErrorHint(std::string& out) {
  char buf[256] = {0};
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, WSAGetLastError(), 0, buf, 256, nullptr);
  out = buf;
}

inline SOCKET connectTo(const std::string& host, unsigned short port, std::string& err) {
  addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  const std::string portStr = std::to_string(port);
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) { err = "getaddrinfo failed"; return INVALID_SOCKET; }
  SOCKET s = INVALID_SOCKET;
  for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
    s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) break;
    closesocket(s); s = INVALID_SOCKET;
  }
  freeaddrinfo(res);
  if (s == INVALID_SOCKET) { lastErrorHint(err); }
  return s;
}

class Listener {
 public:
  bool listenOn(unsigned short port, std::string& err) {
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) { lastErrorHint(err); return false; }
    int yes = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(listener_, (sockaddr*)&addr, sizeof addr) == SOCKET_ERROR) { lastErrorHint(err); closesocket(listener_); listener_=INVALID_SOCKET; return false; }
    if (listen(listener_, 16) == SOCKET_ERROR) { lastErrorHint(err); closesocket(listener_); listener_=INVALID_SOCKET; return false; }
    return true;
  }
  unsigned short port() const {
    sockaddr_in a{}; int len = sizeof a;
    if (getsockname(listener_, (sockaddr*)&a, &len) == 0) return ntohs(a.sin_port);
    return 0;
  }
  SOCKET accept(std::string& err) {
    sockaddr_in a{}; int len = sizeof a;
    SOCKET s = ::accept(listener_, (sockaddr*)&a, &len);
    if (s == INVALID_SOCKET) lastErrorHint(err);
    return s;
  }
  void close() { if (listener_ != INVALID_SOCKET) { closesocket(listener_); listener_ = INVALID_SOCKET; } }
  bool valid() const { return listener_ != INVALID_SOCKET; }
 private:
  SOCKET listener_{INVALID_SOCKET};
};

inline bool readExact(SOCKET s, std::uint8_t* buf, std::size_t n, long timeoutMs) {
  DWORD t = (timeoutMs < 0) ? 0 : (DWORD)timeoutMs;
  if (timeoutMs >= 0) { setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t); }
  std::size_t got = 0;
  while (got < n) {
    int r = recv(s, (char*)(buf + got), (int)(n - got), 0);
    if (r == 0) return false;
    if (r == SOCKET_ERROR) return false;
    got += (std::size_t)r;
  }
  return true;
}

inline bool writeAll(SOCKET s, const std::uint8_t* buf, std::size_t n) {
  std::size_t sent = 0;
  while (sent < n) {
    int r = send(s, (const char*)(buf + sent), (int)(n - sent), 0);
    if (r == SOCKET_ERROR) return false;
    sent += (std::size_t)r;
  }
  return true;
}

// Send one framed message: 4-byte big/little length prefix + frame bytes.
inline bool sendFrame(SOCKET s, const Frame& f, std::string& err) {
  std::vector<std::byte> bytes;
  ProtocolError e = encodeFrame(f, bytes);
  if (!e.ok()) { err = e.detail; return false; }
  std::uint32_t len = (std::uint32_t)bytes.size();
  std::uint8_t hdr[4] = { (std::uint8_t)(len & 0xff), (std::uint8_t)((len >> 8) & 0xff),
                          (std::uint8_t)((len >> 16) & 0xff), (std::uint8_t)((len >> 24) & 0xff) };
  if (!writeAll(s, hdr, 4)) { err = "write length failed"; return false; }
  if (!writeAll(s, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())) { err = "write frame failed"; return false; }
  return true;
}

// Receive one framed message. Returns false on short read / decode error.
inline bool recvFrame(SOCKET s, Frame& f, std::string& err, long timeoutMs) {
  std::uint8_t hdr[4];
  if (!readExact(s, hdr, 4, timeoutMs)) { err = "read length failed"; return false; }
  std::uint32_t len = (std::uint32_t)hdr[0] | ((std::uint32_t)hdr[1] << 8) | ((std::uint32_t)hdr[2] << 16) | ((std::uint32_t)hdr[3] << 24);
  if (len == 0 || len > kMaxFramePayload) { err = "bad frame length"; return false; }
  std::vector<std::byte> buf(len);
  if (!readExact(s, reinterpret_cast<std::uint8_t*>(buf.data()), len, timeoutMs)) { err = "read frame failed"; return false; }
  ProtocolError e = decodeFrame(buf.data(), buf.size(), f);
  if (!e.ok()) { err = e.detail; return false; }
  return true;
}

inline void closeSock(SOCKET& s) { if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; } }

}  // namespace communication_planner::transport
