/// @file file_protocol.cpp
/// @brief FileProtocol — 本地文件读取, 基于 POSIX I/O.

#include "source/protocol/file_protocol/file_protocol.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace player {

// ── 生命周期 ────────────────────────────────────────────────────────────

FileProtocol::FileProtocol() = default;

FileProtocol::~FileProtocol() {
  close();
}

// ── IProtocolHandler 实现 ──────────────────────────────────────────────

bool FileProtocol::canHandle(const char* url) {
  if (!url || !*url) return false;

  // "file://" 显式声明
  if (strncmp(url, "file://", 7) == 0) return true;

  // 无 scheme（不含 "://"）→ 本地文件路径
  const char* sep = strstr(url, "://");
  if (!sep) return true;

  // 其他 scheme（http://, rtsp://...）→ false
  return false;
}

int FileProtocol::open(const char* url) {
  if (!url) return -EINVAL;

  // 确保之前的 fd 已关闭
  close();

  m_url = url;
  std::string path = stripScheme_(url);

  m_fd = ::open(path.c_str(), O_RDONLY);
  if (m_fd < 0) {
    return -errno;
  }

  // 获取文件大小
  struct stat st;
  if (::fstat(m_fd, &st) == 0) {
    m_fileSize = st.st_size;
  } else {
    m_fileSize = -1;
  }

  return 0;
}

int FileProtocol::read(uint8_t* buf, int size) {
  if (m_fd < 0) return -EBADF;
  if (!buf || size <= 0) return -EINVAL;

  ssize_t n = ::read(m_fd, buf, static_cast<size_t>(size));
  if (n < 0) return -errno;

  return static_cast<int>(n);   // 0 = EOF, 会被上层检测
}

int64_t FileProtocol::seek(int64_t pos, int whence) {
  if (m_fd < 0) return -EBADF;

  off64_t result = ::lseek64(m_fd, pos, whence);
  if (result == (off64_t)-1) return -errno;

  return static_cast<int64_t>(result);
}

int FileProtocol::close() {
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
  m_fileSize = 0;
  m_url.clear();
  return 0;
}

std::vector<std::string> FileProtocol::getSchemes() const {
  return {"file", ""};
}

// ── 内部工具 ────────────────────────────────────────────────────────────

std::string FileProtocol::stripScheme_(const char* url) {
  if (!url) return {};

  const char* prefix = "file://";
  if (strncmp(url, prefix, 7) == 0) {
    return std::string(url + 7);
  }

  // 处理可能存在的 "file:" 前缀（少一个斜杠）
  if (strncmp(url, "file:", 5) == 0 && strncmp(url, "file://", 7) != 0) {
    return std::string(url + 5);
  }

  return std::string(url);
}

} // namespace player
