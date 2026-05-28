# macOS 环境准备清单（WebRTC 项目）

> 在进入阶段三模块下钻之前，把开发环境一次性搭好。
> 平台：**macOS（端到端开发，含 libwebrtc 编译 + 信令服务端 + B 层 CMake 项目）**
> 目标版本：**libwebrtc m120**（2023-12 release，对应 Chromium 120）
>
> **建议投入**：1-2 天，编译 libwebrtc 走预编译替代方案可以压到半天。
> **关键策略**：能用预编译产物就不要自己编译——自己编译只是为了"完整体验过流程"，工程上没必要。

---

## 目录

1. [系统硬件要求](#1-系统硬件要求)
2. [基础工具链](#2-基础工具链)
3. [libwebrtc 获取方案（两个路径选一）](#3-libwebrtc-获取方案两个路径选一)
4. [信令服务端依赖](#4-信令服务端依赖)
5. [coturn 部署（Docker）](#5-coturn-部署docker)
6. [B 层 CMake 项目骨架](#6-b-层-cmake-项目骨架)
7. [常见报错排查](#7-常见报错排查)
8. [环境就绪自检清单](#8-环境就绪自检清单)

---

## 1. 系统硬件要求

| 项 | 最低 | 推荐 |
|----|------|------|
| **macOS 版本** | 12 (Monterey) | **14 (Sonoma) / 15 (Sequoia)** |
| **CPU** | Intel x86_64 / Apple Silicon (M1+) | M2/M3 Pro 及以上 |
| **内存** | 16 GB | **32 GB**（libwebrtc 链接阶段单进程吃 8-12 GB）|
| **磁盘可用空间** | **50 GB**（libwebrtc 源码 + 编译产物）| 80 GB |
| **Xcode** | 14.x | **15.x**（m120 在新版 SDK 上更稳）|
| **Xcode CLT** | 必装：`xcode-select --install` | 同左 |

### 验证当前环境

```bash
sw_vers                       # 看 macOS 版本
xcode-select -p               # 看 Xcode 路径，要返回非空
xcodebuild -version           # 看 Xcode 版本
df -h ~                       # 看 home 目录剩余空间
sysctl hw.memsize             # 看物理内存（字节）
sysctl -n machdep.cpu.brand_string  # 看 CPU 型号
```

---

## 2. 基础工具链

```bash
# Homebrew（macOS 包管理器，没装的先装）
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 项目要用的工具
brew install cmake ninja python@3.11 git wget pkg-config
brew install nlohmann-json     # B 层 / 信令的 JSON 库
brew install spdlog            # 日志库
brew install googletest        # 单元测试框架

# 验证
cmake --version    # ≥ 3.20
ninja --version    # ≥ 1.10
python3 --version  # ≥ 3.10
```

### Python 注意点

libwebrtc 的 depot_tools 默认调 `python3`，**Apple 自带 python3 是 2.x 兼容版**，必须确保 `which python3` 指向 Homebrew 安装的版本：

```bash
which python3
# 期望输出：/opt/homebrew/bin/python3（M 系列）或 /usr/local/bin/python3（Intel）
# 如果是 /usr/bin/python3，要在 ~/.zshrc 加：
# export PATH="/opt/homebrew/bin:$PATH"
```

---

## 3. libwebrtc 获取方案（两个路径选一）

### 路径 A：预编译产物（推荐，省 1-2 天）

**适用场景**：你的目标是"学习架构 + 重写 B 层模块"，不是"成为 libwebrtc 编译工程师"。直接用预编译产物，跳过整套 GN/Ninja 体系。

#### 选项 A1：CocoaPods GoogleWebRTC（最简单，停在 M114）

```ruby
# Podfile
platform :osx, '12.0'
target 'MyWebRtcDemo' do
  pod 'GoogleWebRTC', '~> 1.1.31999'
end
```

```bash
pod install
```

**优点**：5 分钟搞定、官方维护过、有 Objective-C 桥接头。  
**缺点**：版本停在 M114（2023-05），m120 的新特性看不到。**但对学习项目完全够用**。

#### 选项 A2：webrtc-build 第三方预编译（m120/m121 都有）

GitHub 上 **shiguredo/webrtc-build** 仓库提供日本声网团队（時雨堂）持续维护的 libwebrtc 预编译产物：

```bash
# 下载 macOS arm64 的 m120 build
wget https://github.com/shiguredo/webrtc-build/releases/download/m120.6099.1.3/webrtc.macos_arm64.tar.gz
tar -xzf webrtc.macos_arm64.tar.gz
# 解压后得到 libwebrtc.a 和 include/ 头文件目录
```

**优点**：版本新（m120+）、Apple Silicon 原生 arm64。  
**缺点**：第三方维护，偶有更新滞后；要自己处理头文件路径。

### 路径 B：自己编译 libwebrtc（仅当路径 A 不可行）

> ⚠️ **强烈不推荐普通学习项目走这条路**。仅在你明确想"完整体验 Chromium 编译流程"时再考虑。预计耗时 1-2 天 + 1 次失败。

#### 步骤 1：安装 depot_tools

```bash
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git ~/depot_tools

# 加到 PATH（~/.zshrc）
echo 'export PATH="$HOME/depot_tools:$PATH"' >> ~/.zshrc
source ~/.zshrc

# 验证
which gn fetch ninja  # 应该都在 depot_tools 目录下
```

#### 步骤 2：拉取 libwebrtc 源码

```bash
mkdir -p ~/webrtc-checkout && cd ~/webrtc-checkout
fetch --nohooks webrtc           # 拉源码，约 8-15 GB
gclient sync                     # 拉依赖，再 8-15 GB
cd src
git checkout branch-heads/6099   # m120 的分支号
gclient sync                     # 切分支后再 sync 一次
```

#### 步骤 3：配置构建参数

```bash
cd ~/webrtc-checkout/src

# Apple Silicon (M1/M2/M3) 配置
gn gen out/Default --args='target_os="mac" target_cpu="arm64" is_debug=false rtc_include_tests=false is_component_build=false rtc_use_h264=true ffmpeg_branding="Chrome"'

# Intel Mac 配置
# gn gen out/Default --args='target_os="mac" target_cpu="x64" is_debug=false rtc_include_tests=false'
```

**参数解释**：
- `is_debug=false`：Release 模式，编译产物小、跑得快
- `rtc_include_tests=false`：不编 libwebrtc 自带测试（省 30% 时间）
- `is_component_build=false`：编成单个静态库，方便链接
- `rtc_use_h264=true` + `ffmpeg_branding="Chrome"`：启用 H264 支持（默认关闭，专利原因）

#### 步骤 4：编译

```bash
ninja -C out/Default
# 在 M2 Pro 上约 2-3 小时；Intel i7 约 3-5 小时
```

#### 步骤 5：取出产物

```bash
# 静态库
ls out/Default/obj/libwebrtc.a    # 约 200-400 MB

# 头文件（保持源码目录结构）
# 头文件分散在 src/ 各处，需要自己整理：
mkdir -p ~/webrtc-installed/include
rsync -a --include='*/' --include='*.h' --exclude='*' \
    api modules pc rtc_base ~/webrtc-installed/include/
cp out/Default/obj/libwebrtc.a ~/webrtc-installed/lib/
```

---

## 4. 信令服务端依赖

信令服务跑在本地（开发期）或部署到云服务器（生产），用 C++17 + WebSocket。

### 选项 1：uWebSockets（推荐，轻量 ~5k 行）

```bash
brew install uwebsockets
# 或源码安装
git clone https://github.com/uNetworking/uWebSockets.git ~/libs/uWebSockets
cd ~/libs/uWebSockets && make examples
```

### 选项 2：Boost.Beast（重量级、稳定）

```bash
brew install boost
# Boost.Beast 在 boost::beast 命名空间下，header-only
```

**推荐 uWebSockets**——更小、API 更现代，适合学习项目。

### 信令依赖快速验证

```bash
# 写一个最小 WebSocket Echo Server，验证 uWebSockets 能跑
cat > /tmp/echo_server.cc << 'EOF'
#include <App.h>
int main() {
    uWS::App().ws<int>("/*", {
        .message = [](auto* webSocket, std::string_view receivedMessage, uWS::OpCode opcode) {
            webSocket->send(receivedMessage, opcode);
        }
    }).listen(9001, [](auto* listenSocket) {
        if (listenSocket) std::cout << "Listening on 9001\n";
    }).run();
}
EOF
# 编译略（需要链接 uSockets + zlib + ssl）
```

---

## 5. coturn 部署（Docker）

coturn 是开源的 STUN/TURN 服务器，做 NAT 穿透 + 中继兜底。

### 安装 Docker Desktop

```bash
brew install --cask docker
open /Applications/Docker.app  # 启动 Docker
```

### 一键启动 coturn

```bash
# 拉镜像
docker pull coturn/coturn:latest

# 创建本地配置文件
mkdir -p ~/coturn-config
cat > ~/coturn-config/turnserver.conf << 'EOF'
listening-port=3478
fingerprint
lt-cred-mech
user=demo:demopass
realm=local.test
total-quota=100
bps-capacity=0
stale-nonce=600
no-stdout-log
log-file=/var/log/turnserver.log
no-tls
no-dtls
EOF

# 启动容器
docker run -d --name coturn \
    -p 3478:3478 -p 3478:3478/udp \
    -v ~/coturn-config/turnserver.conf:/etc/turnserver.conf \
    coturn/coturn:latest \
    -c /etc/turnserver.conf

# 验证
docker logs coturn  # 应看到 "TLS listener opened" 类似日志
```

### 客户端配置 ICE Server

```cpp
// libwebrtc 配置
webrtc::PeerConnectionInterface::IceServer iceServer;
iceServer.urls.push_back("stun:localhost:3478");
iceServer.urls.push_back("turn:localhost:3478?transport=udp");
iceServer.username = "demo";
iceServer.password = "demopass";
```

---

## 6. B 层 CMake 项目骨架

B 层的两个模块（RTP 打包器 + Jitter Buffer）用 **独立的 CMake 项目**——跨平台、易于单测、可单独 push GitHub。

### 目录结构

```
WebRTC-B-Modules/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── rtp_packetizer.h
│   ├── rtp_depacketizer.h
│   └── jitter_buffer.h
├── src/
│   ├── rtp_packetizer.cc
│   ├── rtp_depacketizer.cc
│   ├── h264_fu_a_packetizer.cc
│   └── jitter_buffer.cc
├── tests/
│   ├── CMakeLists.txt
│   ├── rtp_packetizer_test.cc
│   └── jitter_buffer_test.cc
└── benchmark/
    └── jitter_buffer_benchmark.cc
```

### 顶层 CMakeLists.txt（示例骨架）

```cmake
cmake_minimum_required(VERSION 3.20)
project(WebRtcBModules CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 编译选项
add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

# B 层算法库
add_library(webrtc_b_modules STATIC
    src/rtp_packetizer.cc
    src/rtp_depacketizer.cc
    src/h264_fu_a_packetizer.cc
    src/jitter_buffer.cc
)
target_include_directories(webrtc_b_modules PUBLIC include)

# 单元测试
enable_testing()
add_subdirectory(tests)
```

### 验证 CMake 项目可 build

```bash
mkdir -p ~/projects/webrtc-b-modules/build
cd ~/projects/webrtc-b-modules/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .
ctest --output-on-failure
```

---

## 7. 常见报错排查

### 错误 1：`xcrun: error: invalid active developer path`

```
xcrun: error: invalid active developer path (/Library/Developer/CommandLineTools), 
missing xcrun at: /Library/Developer/CommandLineTools/usr/bin/xcrun
```

**原因**：Xcode 升级后命令行工具失效。  
**解决**：`xcode-select --install`，等弹窗安装。

### 错误 2：libwebrtc 编译报 `'string_view' is unavailable: introduced in macOS 10.14`

**原因**：deployment target 设置过低。  
**解决**：在 `gn gen` 参数加 `mac_deployment_target="12.0"`。

### 错误 3：CocoaPods 安装 GoogleWebRTC 卡住

**原因**：Pods 镜像源在国内访问慢。  
**解决**：换清华镜像源：
```bash
gem sources --remove https://rubygems.org/
gem sources --add https://mirrors.tuna.tsinghua.edu.cn/rubygems/
```

### 错误 4：Apple Silicon 下找不到 brew 包

**原因**：终端跑在 Rosetta 下。  
**解决**：检查 `arch`：应输出 `arm64`，否则关闭 Terminal 的"使用 Rosetta 打开"。

### 错误 5：CMake 找不到 GoogleTest

```
Could not find a package configuration file provided by "GTest"
```

**解决**：
```bash
brew install googletest
# 或在 CMakeLists.txt 用 FetchContent
include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
FetchContent_MakeAvailable(googletest)
```

### 错误 6：Docker 启动 coturn 后客户端连不上

**原因**：macOS 防火墙拦截 / Docker 端口映射没生效。  
**排查**：
```bash
nc -uvz localhost 3478           # 测 UDP 端口
docker port coturn               # 看实际映射
# 如果 nc 失败，关闭 macOS 防火墙再试（系统设置 → 网络 → 防火墙）
```

---

## 8. 环境就绪自检清单

完成下面所有项后，才算环境准备好、可以进阶段三：

### 基础工具
- [ ] `xcodebuild -version` 输出 14+ 或 15+
- [ ] `cmake --version` ≥ 3.20
- [ ] `ninja --version` ≥ 1.10
- [ ] `python3 --version` ≥ 3.10
- [ ] `which python3` 指向 Homebrew 路径
- [ ] `brew list` 含 `nlohmann-json` / `spdlog` / `googletest`

### libwebrtc
- [ ] 已选定方案（A1 CocoaPods / A2 webrtc-build / B 自编）
- [ ] `libwebrtc.a` 存在且大小合理（200-400 MB）
- [ ] 头文件目录结构正确（`api/` / `modules/` / `rtc_base/`）
- [ ] 能写一个 5 行的 hello world include `<api/peer_connection_interface.h>` 并编译通过

### 信令依赖
- [ ] uWebSockets 安装完成（或源码 build 通过）
- [ ] echo server demo 在 9001 端口能跑通

### coturn
- [ ] `docker ps` 看到 coturn 容器在运行
- [ ] `nc -uvz localhost 3478` 成功
- [ ] `docker logs coturn` 无 fatal 级别错误

### B 层 CMake 项目
- [ ] 项目骨架目录建好
- [ ] `cmake .. && cmake --build .` 一次成功（即使源文件还是空）
- [ ] `ctest` 能运行（即使没测试用例）

### 编辑器/IDE
- [ ] VS Code 或 CLion 装好，能打开 B 层项目
- [ ] `clangd` 能解析 `compile_commands.json` 提供智能提示
- [ ] 调试器配置好（lldb），能在 main 函数打断点

---

## 总结建议

**最快路径**（半天）：
1. 上午：装 Homebrew + 各种 brew 包 + Xcode CLT
2. 中午：CocoaPods 装 GoogleWebRTC（路径 A1），跳过编译
3. 下午：Docker 跑 coturn、写 hello world include 验证
4. 晚上：搭 B 层 CMake 骨架 + 跑通空测试

**完整路径**（1-2 天）：在最快路径基础上，自己跑一遍 libwebrtc 编译（路径 B），知道一遍流程后续问题排查更顺手。

---

## 结束语

环境清单完成。请你按照清单**至少完成"基础工具 + libwebrtc 路径 A + B 层 CMake 项目骨架"**这三块，然后告诉我：

1. **libwebrtc 你选了路径 A1（CocoaPods）/ A2（webrtc-build）/ B（自编）哪个**？
2. **碰到了哪些清单里没列的报错**？需要我补充进"常见报错排查"。
3. **是否进入阶段三 M4 RTP 传输模块下钻**？

如果你想边搭环境边进 M4（环境搭建过程中我可以同步讲解 RTP 原理），告诉我"一起走"，我会平行推进。
