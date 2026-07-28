#pragma once

/// @file file_protocol.h
/// @brief 本地文件协议处理器。IProtocolHandler 的最简实现。
///
/// Thread safety:  非线程安全。每个实例由单个 Demux Thread 独占使用。
///
/// Ownership:      实例由 ProtocolFactory 创建, 调用者通过 unique_ptr 持有.
///
/// Lifecycle:      ProtocolFactory::createProtocol(url) → open(url) → read/seek → close()
///
/// Schemes:        {"file", ""} — "" 表示无 scheme 的裸路径（/path/to/file）

#include "source/protocol/i_protocol_handler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace player {

class FileProtocol : public IProtocolHandler {
public:
  FileProtocol();
  ~FileProtocol() override;

  // ── IProtocolHandler ──────────────────────────────────────────────────

  bool     canHandle(const char* url)              override;
  int      open(const char* url)                   override;
  int      read(uint8_t* buf, int size)            override;
  int64_t  seek(int64_t pos, int whence)           override;
  int      close()                                 override;
  std::vector<std::string> getSchemes() const      override;

private:
  /// 去掉 "file://" 前缀, 返回纯文件路径. 同时处理 percent-encoding.
  static std::string stripScheme_(const char* url);

  int         m_fd = -1;
  int64_t     m_fileSize = 0;
  std::string m_url;
};

} // namespace player
