# FFmpeg 与 CMake 学习问答记录

## 1. `project(transcoder CXX)` 里的 `CXX` 是什么

`CXX` 是 CMake 对 C++ 语言的内部标识，不是随便写的字符串。

- `C` 表示 C 语言
- `CXX` 表示 C++
- 这个命名和工具链里的 `CC`、`CXX` 环境变量习惯一致

写了 `CXX` 之后，CMake 才会启用 C++ 编译器检测，以及 `CMAKE_CXX_STANDARD` 这类变量。

## 2. `CMAKE_EXPORT_COMPILE_COMMANDS` 是什么

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

这句会让 CMake 额外生成一个 `compile_commands.json` 文件。

这个文件本质上是一份编译数据库，里面记录每个源文件真实的编译命令，比如：

- 用哪个编译器
- 传了哪些 `-I` 头文件目录
- 传了哪些 `-D` 宏定义
- 使用了哪个 C++ 标准

它主要给 `clangd`、Cursor、VSCode 等工具使用，帮助它们正确补全、跳转和诊断。

## 3. `find_package(PkgConfig REQUIRED)` 是什么

```cmake
find_package(PkgConfig REQUIRED)
```

这句不是在找 FFmpeg，而是在找 `pkg-config` 这个工具。

作用是：

- 让 CMake 加载 `PkgConfig` 模块
- 确认系统里存在 `pkg-config`
- 这样后面才能使用 `pkg_check_modules(...)`

如果不写，后面调用 `pkg_check_modules(...)` 时通常会报：

```text
Unknown CMake command "pkg_check_modules"
```

## 4. `REQUIRED` 是什么意思

`REQUIRED` 表示“这是强依赖，找不到就立刻失败”。

比如：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec)
```

含义分别是：

- 找不到 `pkg-config`，配置直接失败
- 找不到 FFmpeg 相关模块，配置直接失败

## 5. `pkg_check_modules(...)` 里的 `FFMPEG` 是什么

```cmake
pkg_check_modules(
  FFMPEG REQUIRED
  libavformat
  libavcodec
  libavutil
)
```

这里的 `FFMPEG` 不是库名，而是“结果变量前缀”。

执行后，CMake 会生成一组变量，例如：

- `FFMPEG_INCLUDE_DIRS`
- `FFMPEG_LIBRARY_DIRS`
- `FFMPEG_LIBRARIES`
- `FFMPEG_CFLAGS_OTHER`

所以后面才能这样写：

```cmake
target_include_directories(app PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(app PRIVATE ${FFMPEG_LIBRARIES})
```

## 6. 为什么 `iostream`、`string` 不需要单独查找

因为它们属于 C++ 标准库。

只要你的项目已经启用了 C++：

```cmake
project(app CXX)
```

并且使用 C++ 编译器编译 `.cpp` 文件，标准库通常由编译器工具链自动处理，不需要像 FFmpeg、OpenGL 这种第三方库那样单独 `find_package()`。

## 7. 为什么 OpenGL 常写成 `find_package(OpenGL REQUIRED)`，FFmpeg 却常用 `pkg-config`

两者差别不在于“谁更高级”，而在于“这个库在当前系统上如何被发现”。

### OpenGL 常见写法

```cmake
find_package(OpenGL REQUIRED)
target_link_libraries(app PRIVATE OpenGL::GL)
```

这通常是 CMake 自带的查找模块，返回的是一个现成的 imported target。

### FFmpeg 常见写法

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil)
target_include_directories(app PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(app PRIVATE ${FFMPEG_LIBRARIES})
```

这是借助 `pkg-config` 查到一组变量，再手动挂到 target 上。

总结：

- OpenGL 常走“CMake 原生 target 风格”
- FFmpeg 在 Linux 上常走“pkg-config 变量风格”

## 8. 为什么公司里更常看到 `target_link_libraries`

因为 `target_link_libraries(...)` 是最终表达“这个目标依赖哪些库”的核心命令。

即使前面用了：

- `find_package(...)`
- `pkg-config`
- 自定义公司 CMake 模块

最后通常还是会落到：

```cmake
target_link_libraries(...)
```

而很多大项目会把“找库”的动作统一封装到上层脚本或公共模块里，所以你在业务模块里更容易只看到 `target_link_libraries(...)`。

## 9. 为什么 C++ 里包含 FFmpeg 头文件要加 `extern "C"`

因为 FFmpeg 是 C 库，而你的 `main.cpp` 是按 C++ 编译的。

如果不写：

```cpp
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
```

那么 C++ 编译器会按 C++ 方式修饰符号名，而动态库里导出的却是 C 风格符号，最终就会链接失败，出现 `undefined reference`。

## 10. 为什么 `AVPacket` 要手动 `av_packet_unref()`，但 `AVFrame` 看起来会自动清空

关键区别是：

- `AVPacket` 在这里是输入数据
- `AVFrame` 在 `avcodec_receive_frame()` 里是输出容器

### `AVPacket`

```cpp
av_read_frame(fmt_ctx, pkt);
avcodec_send_packet(codec_ctx, pkt);
av_packet_unref(pkt);
```

`av_read_frame()` 会把压缩数据包填到 `pkt` 里，这份包数据的生命周期仍然由调用者管理。

`avcodec_send_packet()` 只是读取它，不接管所有权，所以你必须自己 `av_packet_unref(pkt)`。

### `AVFrame`

```cpp
avcodec_receive_frame(codec_ctx, frame);
```

这里的 `frame` 是一个输出槽位。FFmpeg 在往里面放新帧之前，会先清理旧内容，因此看起来像“自动释放旧帧数据”。

## 11. 为什么保存成 JPEG 需要重新编码

因为 `AVFrame` 不是 JPEG 文件，它只是解码后的原始像素数据。

也就是说：

- `AVFrame` = 原始图像帧（如 YUV、RGB）
- `.jpg` = JPEG 编码后的压缩文件

如果要生成真正可打开的 JPEG 文件，就必须：

1. 先把 `AVFrame` 转成 JPEG 编码器支持的像素格式
2. 再交给 MJPEG 编码器编码
3. 最后把编码后的 `AVPacket` 写入文件

## 12. 为什么 PPM 更容易保存

PPM 尤其是 `P6` 格式比较简单，本质上是：

- 一个很短的文件头
- 后面直接跟 RGB 原始像素数据

所以只要把源帧转换成 `RGB24`，就可以直接写文件，不需要额外做 JPEG/PNG 这种压缩编码。

## 13. `save_frame_as_ppm()` 里 `av_frame_make_writable()` 是什么含义

典型流程是：

1. `av_frame_alloc()` 只分配 `AVFrame` 结构体这个“壳子”
2. `av_frame_get_buffer()` 才给这个壳子分配真正的像素缓冲区
3. `av_frame_make_writable()` 在真正写像素之前，确保这块缓冲区当前可安全写入

为什么还要做第 3 步：

- 后面的 `sws_scale()` 会把转换后的 RGB 像素写进 `rgb_frame->data`
- 如果缓冲区是共享的、不可直接写，FFmpeg 会在必要时帮你准备一份新的可写缓冲区

所以这一步可以理解成“写前确认”和“必要时写时复制”。

## 14. 当前这类“视频抽帧存图”的完整数据流

### 抽到原始视频帧

```text
输入文件 -> av_read_frame -> AVPacket -> avcodec_send_packet
       -> avcodec_receive_frame -> AVFrame(原始像素)
```

### 保存成 PPM

```text
AVFrame(原始像素) -> sws_scale 转 RGB24 -> 写 PPM 文件头 + 写 RGB 数据
```

### 保存成 JPEG

```text
AVFrame(原始像素) -> sws_scale 转编码器支持的像素格式
                 -> avcodec_send_frame(MJPEG encoder)
                 -> avcodec_receive_packet
                 -> 写 .jpg 文件
```

## 15. 一个学习建议

学习 FFmpeg 时，很多 API 都可以从“壳子、缓冲区、所有权”三个角度理解：

- 壳子是谁分配的
- 真正的数据缓冲区是谁分配的
- 数据所有权和释放责任属于谁

这能帮助理解：

- `alloc`
- `get_buffer`
- `unref`
- `free`
- `make_writable`

这些 API 到底各自负责什么。
