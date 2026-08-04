# Dev Log 020 - libwebrtc Checkout 与官方示例构建成功

## 本次目标

在用户打开 Clash Allow LAN 后，继续完成 WebRTC native 构建环境准备，并验证官方 peerconnection 示例可以编译。

## 做了什么

1. 运行 `scripts/test_webrtc_network.sh`，确认 WSL 能通过 `172.20.208.1:7897` 代理访问 WebRTC 相关 Google 域名。
2. 创建 `/home/bfm01000/workspace/third_party/webrtc-checkout`。
3. 执行 `fetch --nohooks --no-history webrtc`。
4. 执行 `gclient sync --nohooks --no-history` 确认同步状态。
5. 执行 `gclient runhooks` 下载 sysroot、resources、vpython 和工具链依赖。
6. 执行 `gn gen out/Default --args='is_debug=true rtc_include_tests=false treat_warnings_as_errors=false'`。
7. 编译官方 `peerconnection_server`。
8. 编译官方 `peerconnection_client`。
9. 新增 `scripts/sync_webrtc_checkout.sh` 和 `scripts/build_webrtc_examples.sh` 固化复现流程。
10. 新增 `docs/phase3/libwebrtc-checkout-build.md` 记录构建结果。

## 验证结果

```text
GN gen: success
peerconnection_server: build success, 7.8M
peerconnection_client: build success, 131M
```

`peerconnection_server --help` 可运行，输出示例：

```text
./peerconnection_server --port=8888
```

## 遇到的问题

### fetch 时间超过工具超时

`fetch --nohooks --no-history webrtc` 在 Codex 工具超时后仍有后台进程继续运行。通过 `ps`、`du -sh` 和日志确认它仍在同步，最终 checkout 增长到约 13G。

解决方式：不立即删除目录，先检查进程和日志；等后台进程结束后，再执行 `gclient sync --nohooks --no-history` 确认一致性。

### hooks 下载耗时较长

`gclient runhooks` 耗时约 16 分钟，主要下载 sysroot、WebRTC resources、vpython、LLVM/Rust 等依赖。

解决方式：保持代理环境，单独运行 hooks，并记录日志。

## 技术取舍

使用 `--no-history`，因为当前目标是构建和学习 native sender，不需要完整历史。使用 `rtc_include_tests=false` 减少目标规模，避免无关测试扩大构建时间。

## 面试可讲点

这一步证明我能处理大型 C++ 工程接入：不是只写业务代码，而是能把源码获取、代理、depot_tools、gclient、hooks、GN、Ninja 和官方示例构建全部打通。后续写 native sender 时，如果出问题，可以明确区分是环境问题还是业务代码问题。

## 下一步

阅读官方 peerconnection client 示例，提炼 native sender 最小代码结构，然后在项目中实现 synthetic I420 frame -> libwebrtc PeerConnection -> browser receiver 的最小 Demo。