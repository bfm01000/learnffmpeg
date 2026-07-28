/// @file protocol_factory.cpp
/// @brief ProtocolFactory — URL scheme 检测 + 协议实例创建.

#include "source/protocol/protocol_factory.h"
#include "source/protocol/file_protocol/file_protocol.h"

namespace player {

// ── Registry ──────────────────────────────────────────────────────────────

std::unordered_map<std::string, ProtocolFactoryFn>& ProtocolFactory::registry() {
  // C++11 保证函数局部静态变量初始化的线程安全
  static std::unordered_map<std::string, ProtocolFactoryFn> s_registry;
  return s_registry;
}

// ── 注册 ──────────────────────────────────────────────────────────────────

bool ProtocolFactory::registerProtocol(const std::string& scheme,
                                       ProtocolFactoryFn factory) {
  if (scheme.empty() || !factory) return false;
  registry()[scheme] = std::move(factory);
  return true;
}

void ProtocolFactory::registerBuiltins() {
  // FileProtocol — 处理 "file://" 和裸路径
  registerProtocol("file", [] {
    return std::make_unique<FileProtocol>();
  });

  // 其他协议在各自 .cpp 中通过类似方式注册:
  // HttpProtocol:  registerProtocol("http",  ...)
  //                registerProtocol("https", ...)
  // RtmpProtocol:  registerProtocol("rtmp",  ...)
  // RtspProtocol:  registerProtocol("rtsp",  ...)
}

// ── Scheme 检测 ──────────────────────────────────────────────────────────

std::string ProtocolFactory::detectScheme(const std::string& url) {
  if (url.empty()) return {};

  auto pos = url.find("://");
  if (pos == std::string::npos) {
    // 无 scheme → 默认本地文件
    return "file";
  }
  return url.substr(0, pos);
}

// ── 协议创建 ──────────────────────────────────────────────────────────────

std::unique_ptr<IProtocolHandler> ProtocolFactory::createProtocol(const std::string& url) {
  std::string scheme = detectScheme(url);
  if (scheme.empty()) return nullptr;

  auto& reg = registry();
  auto it = reg.find(scheme);
  if (it == reg.end()) return nullptr;

  auto proto = it->second();   // 调用工厂函数
  if (proto && !proto->canHandle(url.c_str())) {
    // 工厂创建成功但协议自己判断无法处理（如某些边缘 URL 格式）
    return nullptr;
  }
  return proto;
}

} // namespace player
