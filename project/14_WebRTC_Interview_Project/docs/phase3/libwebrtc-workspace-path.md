# libwebrtc workspace 路径说明

## 当前做法

WebRTC checkout 已经通过项目内路径暴露：

```text
/home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project/third_party/webrtc-checkout
```

它是一个符号链接，指向真实 checkout：

```text
/home/bfm01000/workspace/third_party/webrtc-checkout
```

## 为什么使用链接而不是物理移动

WebRTC checkout 当前约 16G，并且已经完成：

- `gclient runhooks`
- `gn gen out/Default`
- `peerconnection_server` 构建
- `peerconnection_client` 构建

如果直接物理移动目录，可能导致部分生成文件、日志、后续脚本路径和缓存状态需要重新校准。使用项目内 symlink 可以达到“从项目目录访问源码”的目的，同时保留已经验证过的 checkout 状态。

## 使用方式

进入 WebRTC 源码：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project/third_party/webrtc-checkout/src
```

运行官方 server：

```bash
./out/Default/peerconnection_server --port=8888
```

运行官方 client：

```bash
./out/Default/peerconnection_client --server=localhost --port=8888
```

项目脚本也已经改成优先使用这个项目内路径：

```bash
./scripts/sync_webrtc_checkout.sh
./scripts/build_webrtc_examples.sh
```

## 后续 native sender 路径

后续如果要把我们的 sender 放入 WebRTC GN 工程，建议放在：

```text
third_party/webrtc-checkout/src/examples/low_latency_sender
```

项目内源码草稿仍放在：

```text
native/libwebrtc_sender
```