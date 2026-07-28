#pragma once

/// @file protocol_factory.h
/// @brief 协议工厂 — URL scheme → IProtocolHandler 的路由与创建.
///
/// ==========================================================================
/// 设计
/// ==========================================================================
///
///   App 传入 URL → detectScheme(url) 提取 scheme
///                → registry[scheme]() 创建对应 Protocol 实例
///
///   registerProtocol("file", [] { return make_unique<FileProtocol>(); });
///   auto proto = ProtocolFactory::createProtocol("/path/to/video.mp4");
///   // → detectScheme 返回 "file" → registry["file"]() → FileProtocol
///
/// ==========================================================================
/// 注册方式
/// ==========================================================================
///
///   1. 手动注册: ProtocolFactory::registerProtocol("http", factory);
///   2. 自动注册: 调用 registerBuiltins() — 注册所有内置协议.
///
/// Thread safety:  registerProtocol 非线程安全（应在 main 启动时调用）。
///                 createProtocol 不修改 registry，与 registerProtocol 并发安全
///                 但要确保 register 全部完成后才调用 create.

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "source/protocol/i_protocol_handler.h"

namespace player {

using ProtocolFactoryFn = std::function<std::unique_ptr<IProtocolHandler>()>;

class ProtocolFactory {
public:
  ProtocolFactory() = delete;

  /// 注册一个协议。通常在 main() 或初始化阶段完成。
  /// @return true 总是成功（覆盖已有注册）
  static bool registerProtocol(const std::string& scheme, ProtocolFactoryFn factory);

  /// 根据 URL 创建对应的协议处理器。
  /// @return 对应的 IProtocolHandler 实例, 无匹配时返回 nullptr.
  static std::unique_ptr<IProtocolHandler> createProtocol(const std::string& url);

  /// 从 URL 提取 scheme（"://" 之前的部分）。
  /// 无 scheme 时返回 "file"。
  static std::string detectScheme(const std::string& url);

  /// 一次性注册所有内置协议（FileProtocol 等）。
  /// 其他协议（HTTP/RTMP...）在各自 .cpp 中通过静态初始化自动注册。
  static void registerBuiltins();

private:
  static std::unordered_map<std::string, ProtocolFactoryFn>& registry();
};

} // namespace player
