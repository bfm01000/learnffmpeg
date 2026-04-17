#pragma once

#include <string>

namespace media_core {

enum class StatusCode {
    kOk = 0,
    kInvalidArg,
    kInternal,
    kNotFound,
    kFfmpegError
};

struct Status {
    StatusCode code = StatusCode::kOk;
    int ffmpeg_error = 0;
    std::string message;

    // 表示操作成功，无错误
    static Status Ok() { return {}; }
    // 表示传递了无效的参数（例如空指针、不合法的数值等）
    static Status InvalidArg(const std::string &msg) { return {StatusCode::kInvalidArg, 0, msg}; }
    // 表示内部逻辑错误或系统级错误（例如内存分配失败、状态机异常等）
    static Status Internal(const std::string &msg) { return {StatusCode::kInternal, 0, msg}; }
    // 表示未找到请求的资源（例如文件不存在、找不到对应的音视频流或解码器等）
    static Status NotFound(const std::string &msg) { return {StatusCode::kNotFound, 0, msg}; }
    // 表示 FFmpeg 底层调用返回的错误，封装了具体的 FFmpeg 错误码以便排查
    static Status Ffmpeg(const std::string &msg, int err) { return {StatusCode::kFfmpegError, err, msg}; }

    bool ok() const { return code == StatusCode::kOk; }
};

}  // namespace media_core
