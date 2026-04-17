#pragma once

#include <string>

namespace my_media_core {

// 枚举类：定义状态码
// 注意：这里的 'k' 前缀是 C++ 中非常经典的命名规范（如 Google C++ Style Guide）。
// 'k' 代表 "Konstant"（常量的德语拼写，为了避免和 C 语言的 const 关键字混淆）。
// 它用来明确告诉阅读代码的人：这是一个编译期常量（Constant）或枚举值（Enum Value）。
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
};

}  // namespace my_media_core