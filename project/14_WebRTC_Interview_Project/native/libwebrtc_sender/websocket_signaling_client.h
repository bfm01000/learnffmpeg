#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class WebSocketSignalingClient {
 public:
  using MessageCallback = std::function<void(const std::string&)>;

  WebSocketSignalingClient(std::string host, int port, std::string room);
  ~WebSocketSignalingClient();

  bool Connect();
  void Close();
  bool SendText(const std::string& text);
  void SetMessageCallback(MessageCallback callback);

  const std::string& room() const { return room_; }

 private:
  bool SendHandshake();
  bool ReadHandshakeResponse();
  void ReadLoop();
  bool ReadExact(void* data, size_t size);
  bool ReadFrame(std::string* message);

  std::string host_;
  int port_ = 0;
  std::string room_;
  int fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread read_thread_;
  MessageCallback on_message_;
};