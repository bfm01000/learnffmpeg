# Dev Log 017 - libwebrtc 环境检查

## 本次目标

继续推进 native libwebrtc sender，但在真正实现前先检查本机 WSL 环境是否具备构建 libwebrtc 的条件。

## 做了什么

1. 新增 `scripts/check_libwebrtc_env.sh`，用于重复检查 native WebRTC 构建环境。
2. 运行脚本检查 `git`、`python3`、`cmake`、`ninja`、`gn`、`fetch`、`gclient`、`clang`、`gcc/g++` 等工具。
3. 检查 workspace 磁盘空间。
4. 检查是否已有本地 WebRTC checkout。
5. 新增 `docs/phase3/native-signaling-protocol.md`，记录当前浏览器 WebRTC 页面和 Node.js signaling server 的兼容方式。
6. 新增 `docs/phase3/libwebrtc-env-check.md`，记录环境检查结果和后续 bootstrap 路线。
7. 新增 `native/libwebrtc_sender/README.md`，保留 native sender 目录入口，但暂不写依赖 libwebrtc 的 C++ 代码。

## 真实检查结果

当前已具备：

```text
git / python3 / cmake / node / npm / gcc / g++
```

当前缺失：

```text
ninja / gn / fetch / gclient / clang / clang++ / WebRTC checkout
```

磁盘空间充足：

```text
/dev/sdd 1007G, available 947G
```

## 遇到的问题

### PowerShell 写入 shell 脚本时产生 BOM

第一次用 PowerShell `Set-Content -Encoding UTF8` 写入 `check_libwebrtc_env.sh`，WSL 执行时出现：

```text
#!/usr/bin/env: No such file or directory
```

原因是 Windows PowerShell 的 UTF-8 写入带 BOM，bash shebang 前面多了不可见字节。

解决方式：使用 .NET `System.Text.UTF8Encoding(false)` 以 UTF-8 无 BOM 写入脚本。

### 跨 PowerShell / WSL / bash 的内联命令容易被转义破坏

环境检查命令里包含 `$`、`{}`、`|`、引号时，PowerShell 和 WSL 默认 shell 会相互影响。

解决方式：把复杂检查固化成脚本，之后只运行脚本文件。

## 技术取舍

没有直接开始下载和编译 WebRTC。

原因：WebRTC 官方 checkout 体积较大，官方文档标注 Linux 约 6.4GB，且需要 `depot_tools`、`gclient sync`、`gn`、`ninja` 等工具链。直接开始可能把大量时间消耗在网络和构建环境上。

当前更合理的步骤是：先记录缺失项和 bootstrap 路线，等确认允许安装和下载后，再进入实际构建。

## 面试可讲点

这一步可以体现工程判断：我没有把“编译 libwebrtc 很复杂”包装成业务难点，而是先把工具链、源码 checkout、官方示例、自己的 sender 分层处理。这样后续遇到问题时，可以判断到底是环境问题、官方示例问题，还是自己业务代码的问题。

## 下一步

需要用户确认是否允许安装和下载依赖。推荐命令方向：

```bash
sudo apt update
sudo apt install -y ninja-build clang
mkdir -p /home/bfm01000/workspace/third_party
cd /home/bfm01000/workspace/third_party
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH=/home/bfm01000/workspace/third_party/depot_tools:$PATH
mkdir -p webrtc-checkout
cd webrtc-checkout
fetch --nohooks webrtc
gclient sync
```

完成后优先编译官方 `peerconnection_client` / `peerconnection_server`，再开始本项目 native sender。