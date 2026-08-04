#include "examples/low_latency_sender/websocket_signaling_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

namespace {

std::string MaskingKey() {
  std::array<char, 4> key{};
  std::random_device rd;
  for (char& c : key) c = static_cast<char>(rd() & 0xff);
  return std::string(key.data(), key.size());
}

bool WriteAll(int fd, const void* data, size_t size) {
  const char* bytes = static_cast<const char*>(data);
  size_t sent = 0;
  while (sent < size) {
    ssize_t n = send(fd, bytes + sent, size - sent, 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

WebSocketSignalingClient::WebSocketSignalingClient(std::string host, int port, std::string room)
    : host_(std::move(host)), port_(port), room_(std::move(room)) {}

WebSocketSignalingClient::~WebSocketSignalingClient() { Close(); }

void WebSocketSignalingClient::SetMessageCallback(MessageCallback callback) {
  on_message_ = std::move(callback);
}

bool WebSocketSignalingClient::Connect() {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* result = nullptr;
  const std::string port_text = std::to_string(port_);
  if (getaddrinfo(host_.c_str(), port_text.c_str(), &hints, &result) != 0) {
    std::cerr << "getaddrinfo failed\n";
    return false;
  }

  for (addrinfo* ai = result; ai; ai = ai->ai_next) {
    fd_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd_ < 0) continue;
    if (connect(fd_, ai->ai_addr, ai->ai_addrlen) == 0) break;
    close(fd_);
    fd_ = -1;
  }
  freeaddrinfo(result);

  if (fd_ < 0) {
    std::cerr << "connect signaling server failed\n";
    return false;
  }
  if (!SendHandshake() || !ReadHandshakeResponse()) {
    std::cerr << "websocket handshake failed\n";
    Close();
    return false;
  }

  running_ = true;
  read_thread_ = std::thread([this] { ReadLoop(); });
  return SendText("{\"type\":\"join\",\"room\":\"" + room_ + "\",\"role\":\"native-sender\"}");
}

void WebSocketSignalingClient::Close() {
  running_ = false;
  if (fd_ >= 0) {
    shutdown(fd_, SHUT_RDWR);
    close(fd_);
    fd_ = -1;
  }
  if (read_thread_.joinable()) read_thread_.join();
}

bool WebSocketSignalingClient::SendHandshake() {
  std::ostringstream request;
  request << "GET / HTTP/1.1\r\n"
          << "Host: " << host_ << ":" << port_ << "\r\n"
          << "Upgrade: websocket\r\n"
          << "Connection: Upgrade\r\n"
          << "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
          << "Sec-WebSocket-Version: 13\r\n\r\n";
  const std::string text = request.str();
  return WriteAll(fd_, text.data(), text.size());
}

bool WebSocketSignalingClient::ReadHandshakeResponse() {
  std::string response;
  char c = 0;
  while (response.find("\r\n\r\n") == std::string::npos) {
    if (recv(fd_, &c, 1, 0) != 1) return false;
    response.push_back(c);
    if (response.size() > 8192) return false;
  }
  return response.find("101") != std::string::npos;
}

bool WebSocketSignalingClient::SendText(const std::string& text) {
  if (fd_ < 0) return false;
  if (text.size() >= 65536) {
    std::cerr << "websocket payload too large for this demo\n";
    return false;
  }

  std::vector<unsigned char> frame;
  frame.push_back(0x81);
  if (text.size() < 126) {
    frame.push_back(static_cast<unsigned char>(0x80 | text.size()));
  } else {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<unsigned char>((text.size() >> 8) & 0xff));
    frame.push_back(static_cast<unsigned char>(text.size() & 0xff));
  }

  const std::string key = MaskingKey();
  frame.insert(frame.end(), key.begin(), key.end());
  for (size_t i = 0; i < text.size(); ++i) {
    frame.push_back(static_cast<unsigned char>(text[i] ^ key[i % 4]));
  }
  return WriteAll(fd_, frame.data(), frame.size());
}

bool WebSocketSignalingClient::ReadExact(void* data, size_t size) {
  char* out = static_cast<char*>(data);
  size_t read_size = 0;
  while (read_size < size) {
    ssize_t n = recv(fd_, out + read_size, size - read_size, 0);
    if (n <= 0) return false;
    read_size += static_cast<size_t>(n);
  }
  return true;
}

bool WebSocketSignalingClient::ReadFrame(std::string* message) {
  unsigned char header[2]{};
  if (!ReadExact(header, 2)) return false;
  const int opcode = header[0] & 0x0f;
  const bool masked = (header[1] & 0x80) != 0;
  uint64_t length = header[1] & 0x7f;
  if (length == 126) {
    unsigned char ext[2]{};
    if (!ReadExact(ext, 2)) return false;
    length = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
  } else if (length == 127) {
    return false;
  }

  std::array<unsigned char, 4> mask{};
  if (masked && !ReadExact(mask.data(), mask.size())) return false;

  std::string payload(length, '\0');
  if (length > 0 && !ReadExact(payload.data(), static_cast<size_t>(length))) return false;
  if (masked) {
    for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
  }
  if (opcode == 0x8) return false;
  if (opcode != 0x1) return true;
  *message = std::move(payload);
  return true;
}

void WebSocketSignalingClient::ReadLoop() {
  while (running_) {
    std::string message;
    if (!ReadFrame(&message)) break;
    if (!message.empty() && on_message_) on_message_(message);
  }
  running_ = false;
}