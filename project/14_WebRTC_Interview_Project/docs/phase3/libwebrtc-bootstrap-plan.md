# libwebrtc Bootstrap 计划

## 当前状态

已经尝试开始 bootstrap，但遇到两个外部环境问题：

1. `sudo apt update && sudo apt install ...` 卡在 sudo 密码输入。Codex 当前工具会等待命令结束，不能交互式输入密码。
2. `git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git` 失败，错误为 TLS handshake 中断。

因此当前没有继续拉取完整 WebRTC 源码。

## 已新增脚本

```bash
scripts/bootstrap_libwebrtc_env.sh
```

建议你在 WSL 交互式终端中运行：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
./scripts/bootstrap_libwebrtc_env.sh
```

这个脚本会做：

1. `sudo -v`，让你先输入 sudo 密码。
2. 安装 `ninja-build`、`clang`、`ca-certificates`、`curl`、`git`、`python3`。
3. 克隆 `depot_tools` 到 `/home/bfm01000/workspace/third_party/depot_tools`。
4. 提示后续 `fetch --nohooks webrtc` 和 `gclient sync` 命令。

## 网络问题

本次直接克隆官方 depot_tools 失败：

```text
fatal: unable to access 'https://chromium.googlesource.com/chromium/tools/depot_tools.git/':
gnutls_handshake() failed: The TLS connection was non-properly terminated.
```

这通常和网络、代理、证书或访问 Google 源有关。

可以先在 WSL 中单独验证：

```bash
curl -I https://chromium.googlesource.com/chromium/tools/depot_tools.git
```

如果你使用代理，需要确认 WSL 里也配置了：

```bash
export http_proxy=http://127.0.0.1:端口
export https_proxy=http://127.0.0.1:端口
git config --global http.proxy http://127.0.0.1:端口
git config --global https.proxy http://127.0.0.1:端口
```

端口要替换成你本机代理实际端口。

## 成功后的下一步

当 `scripts/check_libwebrtc_env.sh` 显示以下工具可用后：

```text
ninja
gn
fetch
gclient
clang
clang++
```

再继续：

```bash
cd /home/bfm01000/workspace/third_party/webrtc-checkout
fetch --nohooks webrtc
gclient sync
```

源码准备好后，优先编译官方 peerconnection 示例，再开始本项目 `native/libwebrtc_sender`。