# Dev Log 022 - WebRTC checkout 项目内入口

## 本次目标

用户希望把 WebRTC 源码放到 workspace 路径下。当前真实 checkout 已经位于 workspace 根目录 `/home/bfm01000/workspace/third_party/webrtc-checkout`，本次补充项目内入口，方便从当前项目访问。

## 做了什么

1. 创建项目内目录 `third_party/`。
2. 创建符号链接：

```text
learnffmpeg/project/14_WebRTC_Interview_Project/third_party/webrtc-checkout
  -> /home/bfm01000/workspace/third_party/webrtc-checkout
```

3. 验证项目内路径可以访问官方示例二进制。
4. 更新 `scripts/sync_webrtc_checkout.sh`，优先使用项目内 `third_party/webrtc-checkout`。
5. 更新 `scripts/build_webrtc_examples.sh`，优先使用项目内 `third_party/webrtc-checkout/src`。
6. 新增 `third_party/README.md` 和 `docs/phase3/libwebrtc-workspace-path.md`。

## 验证结果

项目内路径可访问：

```text
third_party/webrtc-checkout/src/out/Default/peerconnection_server
third_party/webrtc-checkout/src/out/Default/peerconnection_client
```

两个二进制仍然存在：

```text
peerconnection_server: 7.8M
peerconnection_client: 131M
```

## 为什么没有物理移动

WebRTC checkout 当前约 16G，并且已经完成 hooks、GN 生成和官方示例构建。直接移动可能引入路径缓存和构建状态问题。

使用符号链接能满足从项目目录访问源码的需求，同时避免破坏已验证的 checkout。

## 下一步

后续实现 `low_latency_sender` 时，可以把实际 GN target 放到：

```text
third_party/webrtc-checkout/src/examples/low_latency_sender
```

项目内 `native/libwebrtc_sender` 保留源码草稿、模板和设计说明。