# Dev Log 018 - libwebrtc Bootstrap 尝试

## 本次目标

用户确认可以继续后，开始准备 native libwebrtc 构建环境。

## 做了什么

1. 尝试通过 apt 安装 `ninja-build` 和 `clang`。
2. 检查发现命令卡在 sudo 密码等待。
3. 清理挂起的 sudo / apt 相关进程，避免后续包管理器被阻塞。
4. 尝试克隆 Chromium 官方 `depot_tools`。
5. 第一次普通 clone 失败。
6. 第二次 `--depth 1` 浅克隆仍然失败。
7. 新增 `scripts/bootstrap_libwebrtc_env.sh`，用于用户在交互式 WSL 终端中手动执行 bootstrap。
8. 新增 `docs/phase3/libwebrtc-bootstrap-plan.md`，记录环境问题、命令和后续路线。

## 遇到的问题

### sudo 需要交互式密码

`sudo apt update` 进程卡住，原因是 Codex 工具无法输入用户的 sudo 密码。

解决方式：停止挂起进程，把安装命令沉淀到脚本，让用户在 WSL 终端中交互式执行。

### depot_tools 克隆失败

错误：

```text
gnutls_handshake() failed: The TLS connection was non-properly terminated
```

普通 clone 和浅 clone 都失败，说明更可能是 WSL 网络、代理、证书或访问 Google 源的问题。

## 技术取舍

没有继续执行 `fetch webrtc`。

原因：当前连 `depot_tools` 都没有成功下载，直接 fetch 完整 WebRTC 只会把问题扩大。先解决工具链和网络访问是更可靠的工程路径。

## 面试可讲点

这一步体现的是 native 大型工程接入前的环境分层能力：先确认系统工具，再确认 depot_tools，再确认官方示例，最后才写自己的业务 sender。这样能避免把环境问题、构建系统问题和业务代码问题混在一起。

## 下一步

用户需要在 WSL 终端运行：

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project
./scripts/bootstrap_libwebrtc_env.sh
```

如果 depot_tools 仍然因为 TLS 失败，需要先配置 WSL 代理或网络，然后重新运行脚本。