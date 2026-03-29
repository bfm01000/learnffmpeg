cmake_minimum_required(VERSION 3.16)

# 定义项目名称为 transcoder，项目语言为 C++
# 这里的 CXX 是 CMake 对 C++ 的内部语言标识，不是随便写的字符串
# 它和常见工具链环境变量 CC / CXX 的命名习惯一致
# 写上 CXX 后，CMake 才会启用 C++ 编译器检测、C++ 规则以及 CMAKE_CXX_STANDARD 这类变量
project(transcoder CXX)

# 指定使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)

# 强制要求编译器必须支持 C++17，不能自动降级
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 导出 compile_commands.json，方便 clangd、Cursor 等工具做跳转和补全
# 这个文件本质上是一份“编译数据库”，记录每个源文件真实的编译命令
# 里面通常会包含编译器路径、头文件搜索路径(-I)、宏定义(-D)、编译标准等信息
# 有了它，IDE/静态分析工具才能按和真实编译一致的方式理解代码，减少误报和报红
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 pkg-config 工具；FFmpeg 常通过它暴露头文件和库信息
find_package(PkgConfig REQUIRED)

# 通过 pkg-config 查找 FFmpeg 相关模块
# FFMPEG 是这一组查询结果的变量前缀，后面会得到：
# - FFMPEG_INCLUDE_DIRS
# - FFMPEG_LIBRARY_DIRS
# - FFMPEG_LIBRARIES
# - FFMPEG_CFLAGS_OTHER
pkg_check_modules(
  FFMPEG REQUIRED
  libavformat    # 封装格式处理，如 mp4/ts/flv 的读写
  libavcodec     # 编解码核心库
  libavutil      # 通用工具库，如 AVFrame / AVPacket / 错误处理
  libswresample  # 音频重采样与格式转换
  libswscale     # 图像缩放与像素格式转换
)

# 生成一个名为 transcoder 的可执行文件，源文件为 src/main.cpp
add_executable(transcoder src/main.cpp)

# 为 transcoder 添加 FFmpeg 头文件搜索路径
# PRIVATE 表示这些设置只对当前目标生效
target_include_directories(transcoder PRIVATE ${FFMPEG_INCLUDE_DIRS})

# 为 transcoder 添加 FFmpeg 的库搜索目录
target_link_directories(transcoder PRIVATE ${FFMPEG_LIBRARY_DIRS})

# 链接 FFmpeg 相关库
target_link_libraries(transcoder PRIVATE ${FFMPEG_LIBRARIES})

# 添加 pkg-config 返回的其他编译选项
target_compile_options(transcoder PRIVATE ${FFMPEG_CFLAGS_OTHER})
