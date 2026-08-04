# libwebrtc Native 环境检查报告

## 检查时间

2026-08-04

## 当前结论

当前 WSL 环境可以继续做普通 C++ / CMake / FFmpeg 实验，但还不能直接构建 native libwebrtc。

已具备：

- `git`
- `python3`
- `cmake`
- `node`
- `npm`
- `gcc / g++`
- workspace 磁盘空间充足，约 947G 可用

缺失：

- `ninja`
- `gn`
- `fetch`
- `gclient`
- `clang / clang++`
- 本地 WebRTC checkout

## 检查脚本

已新增：

```bash
scripts/check_libwebrtc_env.sh
```

运行方式：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
./scripts/check_libwebrtc_env.sh
```

## 本次真实输出摘要

```text
git          /usr/bin/git
python3      /usr/bin/python3
cmake        /usr/bin/cmake
ninja        MISSING
gn           MISSING
fetch        MISSING
gclient      MISSING
node         /usr/bin/node
npm          /usr/bin/npm
clang        MISSING
clang++      MISSING
gcc          /usr/bin/gcc
g++          /usr/bin/g++

workspace disk:
/dev/sdd       1007G  9.3G  947G   1% /
```

## 官方构建路线

WebRTC 官方 native development 文档说明：

1. 需要先安装 prerequisite software。
2. 桌面端通常创建 `webrtc-checkout` 目录。
3. 使用 `fetch --nohooks webrtc` 获取源码。
4. 使用 `gclient sync` 同步依赖。
5. 使用 `gn gen out/Default` 生成 Ninja 工程。
6. 使用 `ninja -C out/Default` 编译。

官方文档还提示 WebRTC checkout 体积较大，Linux 约 6.4GB。实际同步还会受网络、代理、Google 源访问和依赖下载影响。

参考：

- https://webrtc.github.io/webrtc-org/native-code/development/
- https://webrtc.github.io/webrtc-org/native-code/native-apis/

## 推荐下一步

不要直接把完整 WebRTC 源码拉到当前项目目录里。建议使用 workspace 下独立目录：

```text
/home/bfm01000/workspace/third_party/webrtc-checkout
```

准备顺序：

1. 安装小工具：`ninja-build`、`clang`。
2. 拉取 `depot_tools`。
3. 把 `depot_tools` 加入 PATH。
4. 在独立目录执行 `fetch --nohooks webrtc`。
5. 执行 `gclient sync`。
6. 先编译官方 `peerconnection_client` / `peerconnection_server` 示例。
7. 再接入本项目 `native/libwebrtc_sender`。

## 为什么先跑官方示例

libwebrtc 的难点很大一部分在构建系统和平台依赖。先跑官方 `peerconnection_client` 可以把问题分层：

- 如果官方示例都编不过，先解决工具链问题。
- 如果官方示例能跑，再开始写自己的 sender。
- 这样不会把构建问题误判成业务代码问题。

## 与当前项目的连接

当前项目已有浏览器 WebRTC 页面和 WebSocket signaling server。native sender 的第一版可以复用同一个 room 协议，浏览器先加入房间，native 后加入并作为 answerer。

这样 `server.js` 和 `public/app.js` 暂时都不需要改动。