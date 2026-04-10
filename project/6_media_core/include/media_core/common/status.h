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

    static Status Ok() { return {}; }
    static Status InvalidArg(const std::string &msg) { return {StatusCode::kInvalidArg, 0, msg}; }
    static Status Internal(const std::string &msg) { return {StatusCode::kInternal, 0, msg}; }
    static Status NotFound(const std::string &msg) { return {StatusCode::kNotFound, 0, msg}; }
    static Status Ffmpeg(const std::string &msg, int err) { return {StatusCode::kFfmpegError, err, msg}; }

    bool ok() const { return code == StatusCode::kOk; }
};

}  // namespace media_core
