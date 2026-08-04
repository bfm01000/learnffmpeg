# libwebrtc Checkout 与官方示例构建记录

## 当前结果

native WebRTC 构建链路已经打通。

已完成：

1. WSL 通过 Clash Allow LAN 访问 Google infra。
2. `depot_tools` 可用。
3. `fetch --nohooks --no-history webrtc` 完成 WebRTC 浅历史 checkout。
4. `gclient sync --nohooks --no-history` 可重复执行。
5. `gclient runhooks` 成功。
6. `gn gen out/Default` 成功。
7. 官方 `peerconnection_server` 编译成功。
8. 官方 `peerconnection_client` 编译成功。

## 目录

WebRTC checkout 位于：

```text
/home/bfm01000/workspace/third_party/webrtc-checkout/src
```

构建目录：

```text
/home/bfm01000/workspace/third_party/webrtc-checkout/src/out/Default
```

生成的官方示例：

```text
out/Default/peerconnection_server
out/Default/peerconnection_client
```

## 验证结果

```text
peerconnection_server: 7.8M
peerconnection_client: 131M
```

`peerconnection_server --help` 输出：

```text
peerconnection_server: Example usage: ./peerconnection_server --port=8888
```

## 复现脚本

同步源码和 hooks：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
./scripts/sync_webrtc_checkout.sh
```

构建官方示例：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
./scripts/build_webrtc_examples.sh
```

## 手动命令

如果要手动执行：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
source scripts/use_webrtc_proxy.sh
cd /home/bfm01000/workspace/third_party/webrtc-checkout
fetch --nohooks --no-history webrtc
GCLIENT_SUPPRESS_GIT_VERSION_WARNING=1 gclient sync --nohooks --no-history
GCLIENT_SUPPRESS_GIT_VERSION_WARNING=1 gclient runhooks
cd src
gn gen out/Default --args='is_debug=true rtc_include_tests=false treat_warnings_as_errors=false'
autoninja -C out/Default peerconnection_server
autoninja -C out/Default peerconnection_client
```

## 关键参数说明

`--no-history`：减少 git 历史下载量，适合学习和构建 demo。

`--nohooks`：先只同步源码，后面手动 `gclient runhooks`，便于分阶段排查。

`rtc_include_tests=false`：减少测试目标，降低构建范围。

`treat_warnings_as_errors=false`：避免非核心 warning 阻塞本地实验。

## 过程问题

### 代理问题

一开始 Windows Clash 只监听 `127.0.0.1:7897`，WSL 无法访问。打开 Allow LAN 后，WSL 通过默认网关 `172.20.208.1:7897` 访问成功。

### fetch 超时问题

第一次完整 fetch 超时。之后改为：

```bash
fetch --nohooks --no-history webrtc
```

下载仍然较久，但最终 checkout 落地。中途 Codex 工具超时后，后台进程继续运行，后续通过进程和日志确认完成。

### hooks 时间较久

`gclient runhooks` 耗时约 16 分钟，主要下载 sysroot、resources、vpython 和工具链依赖。

## 下一步

下一步可以正式进入本项目 native sender 设计到代码的过渡：

1. 阅读官方 `examples/peerconnection/client/conductor.cc`、`peer_connection_client.cc`。
2. 提取 PeerConnectionFactory、PeerConnection、VideoTrackSource、signaling 的最小调用路径。
3. 新建 `native/libwebrtc_sender` 的接口骨架。
4. 第一版先做 synthetic I420 frame sender。