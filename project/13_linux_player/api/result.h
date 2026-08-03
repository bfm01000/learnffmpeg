#pragma once

/// @file result.h
/// @brief Result<T> — 类似 Rust Result / C++23 std::expected 的错误处理类型.
///
/// 用法:
///   Result<void> r = player.play();
///   if (!r.isOk()) { LOG_ERROR("play failed: %s", r.error().message); }
///
///   Result<double> pos = player.getPosition();
///   if (pos.isOk()) { printf("pos=%.1f\n", pos.value()); }

#include "api/player_types.h"

#include <cstring>
#include <string>
#include <variant>

namespace player {

/// @brief 错误详情
struct Error {
    ErrorCode code    = ErrorCode::Ok;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string msg = "") : code(c), message(std::move(msg)) {}

    bool isOk() const { return code == ErrorCode::Ok; }
};

/// @brief Result<T> — 成功时包含 T 类型的值，失败时包含 Error.
///
/// 对 void 类型提供特化版本 Result<void>（只含 Error）
template <typename T>
class [[nodiscard]] Result {
public:
    // ── 构造 ────────────────────────────────────────────────────────────

    Result() : data_(T{}) {}
    Result(T value) : data_(std::move(value)) {}
    Result(ErrorCode code, std::string msg = "") : data_(Error(code, std::move(msg))) {}
    Result(Error err) : data_(std::move(err)) {}

    // ── 查询 ────────────────────────────────────────────────────────────

    bool isOk()  const { return std::holds_alternative<T>(data_); }
    bool isErr() const { return std::holds_alternative<Error>(data_); }

    /// 获取成功值（失败时抛 std::bad_variant_access）
    const T& value() const { return std::get<T>(data_); }
    T&       value()       { return std::get<T>(data_); }

    /// 获取成功值，失败时返回默认值
    T valueOr(T defaultVal) const {
        return isOk() ? std::get<T>(data_) : std::move(defaultVal);
    }

    /// 获取错误（成功时抛异常）
    const Error& error() const { return std::get<Error>(data_); }

    /// 隐式转 bool — 可用 if (result) 判断成功
    explicit operator bool() const { return isOk(); }

private:
    std::variant<T, Error> data_;
};

/// @brief Result<void> 特化 — 只包含 Error，无值类型
template <>
class [[nodiscard]] Result<void> {
public:
    Result() : error_(ErrorCode::Ok) {}
    Result(ErrorCode code, std::string msg = "") : error_(code, std::move(msg)) {}
    Result(Error err) : error_(std::move(err)) {}

    bool isOk()  const { return error_.isOk(); }
    bool isErr() const { return !error_.isOk(); }
    const Error& error() const { return error_; }
    explicit operator bool() const { return isOk(); }

    /// 快捷工厂
    static Result<void> ok() { return Result<void>(); }

private:
    Error error_;
};

} // namespace player
