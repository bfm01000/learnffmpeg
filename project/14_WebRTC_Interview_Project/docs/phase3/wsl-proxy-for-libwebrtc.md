# WSL 访问 libwebrtc 依赖源的代理配置

## 当前问题

`depot_tools` 已经通过镜像下载到：

```text
/home/bfm01000/workspace/third_party/depot_tools
```

但运行 `fetch` / `gclient` 前，`depot_tools` 需要访问 Google infra 依赖，例如：

```text
chrome-infra-packages.appspot.com
commondatastorage.googleapis.com
chromium.googlesource.com
```

当前 WSL 直连这些域名会出现 TLS 错误：

```text
OpenSSL SSL_connect: SSL_ERROR_SYSCALL
```

Windows 上检测到系统代理：

```text
127.0.0.1:7897
process: clash-meta.exe
```

但该代理只监听 Windows `127.0.0.1`，没有监听 `0.0.0.0`，因此 WSL 不能通过 `172.20.208.1:7897` 访问它。

## 需要用户操作

在 Clash / clash-meta 中打开：

```text
Allow LAN / 允许局域网连接
```

或把 mixed-port/http-port 监听地址改成：

```text
0.0.0.0:7897
```

然后确认 Windows 防火墙允许 WSL 子网访问该端口。

## 项目脚本

已新增：

```bash
scripts/use_webrtc_proxy.sh
scripts/test_webrtc_network.sh
```

打开 Allow LAN 后，在 WSL 里运行：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
./scripts/test_webrtc_network.sh
```

如果看到 `chrome-infra-packages.appspot.com`、`commondatastorage.googleapis.com` 返回 HTTP 响应，就说明可以继续 `fetch webrtc`。

## 后续命令

网络通过后：

```bash
source scripts/use_webrtc_proxy.sh
cd /home/bfm01000/workspace/third_party
mkdir -p webrtc-checkout
cd webrtc-checkout
fetch --nohooks webrtc
gclient sync
```

## 备注

`use_webrtc_proxy.sh` 会自动读取 WSL 默认网关作为 Windows host IP：

```bash
ip route | awk '/default/ {print $3; exit}'
```

当前检测到默认网关是：

```text
172.20.208.1
```