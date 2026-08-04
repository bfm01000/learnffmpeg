# Dev Log 019 - WSL 代理阻塞 libwebrtc fetch

## 本次目标

用户手动执行 bootstrap 后，继续检查 libwebrtc 构建链路是否可以进入 `fetch webrtc`。

## 做了什么

1. 重新运行 `scripts/check_libwebrtc_env.sh`。
2. 确认 `ninja`、`clang`、`clang++` 已安装成功。
3. 确认 `depot_tools` 已通过镜像下载到 `/home/bfm01000/workspace/third_party/depot_tools`。
4. 更新环境检查脚本，让它自动把 `depot_tools` 加入 PATH。
5. 确认 `fetch`、`gclient`、`gn` 命令已经能被找到。
6. 测试 `chrome-infra-packages.appspot.com` 和 `commondatastorage.googleapis.com`，仍然 TLS 失败。
7. 读取 Windows 代理配置，发现代理为 `127.0.0.1:7897`。
8. 检查端口监听，确认代理进程是 `clash-meta.exe`，但只监听 Windows localhost。
9. 新增 WSL 代理辅助脚本和网络测试脚本。

## 当前状态

已具备：

```text
ninja
clang
clang++
depot_tools/fetch
depot_tools/gclient
depot_tools/gn
```

仍缺失或未完成：

```text
depot_tools 的 CIPD/vpython bootstrap
WebRTC checkout
```

## 遇到的问题

`depot_tools` 首次运行会访问：

```text
chrome-infra-packages.appspot.com
commondatastorage.googleapis.com
```

当前 WSL 直连这些域名失败。Windows 有 Clash 代理，但监听地址是：

```text
127.0.0.1:7897
```

WSL 通过默认网关访问代理端口超时，因为代理没有开启 Allow LAN 或没有监听 `0.0.0.0`。

## 解决方案

需要在 Clash 中打开：

```text
Allow LAN / 允许局域网连接
```

然后在 WSL 中运行：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
./scripts/test_webrtc_network.sh
```

网络通后再继续：

```bash
source scripts/use_webrtc_proxy.sh
cd /home/bfm01000/workspace/third_party/webrtc-checkout
fetch --nohooks webrtc
gclient sync
```

## 技术取舍

没有强行运行 `fetch webrtc`。当前瓶颈不是代码，而是 Google infra 网络访问。继续运行只会产生重复失败日志。

## 面试可讲点

大型 native WebRTC 工程接入时，构建系统和依赖源本身就是工程风险。这里把系统工具、depot_tools、CIPD、WebRTC checkout、官方示例和项目代码分层处理，能体现排查复杂依赖链路的能力。