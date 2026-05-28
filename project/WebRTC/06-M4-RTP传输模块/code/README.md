# M4 · RTP 传输模块 · B 层代码

> 阶段三 M4 RTP 模块的 B 层重写实现。**不依赖 libwebrtc**，纯 C++17 + GoogleTest，跨平台。
> 配套主文档：`../06-M4-RTP传输模块.md`。

## 目录结构

```
code/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── rtp_packet.h              # RTP 包数据结构
│   ├── rtp_packetizer.h          # IRtpPacketizer 接口 + H264Packetizer
│   └── rtp_depacketizer.h        # IRtpDepacketizer 接口 + H264Depacketizer
├── src/
│   ├── rtp_packet.cc             # 序列化/解析（手动字节序）
│   ├── h264_packetizer.cc        # Single NALU + FU-A 分片打包
│   └── h264_depacketizer.cc      # Single NALU + FU-A 重组解包
└── tests/
    ├── CMakeLists.txt            # 用 FetchContent 自动拉 GoogleTest
    ├── rtp_packet_test.cc        # 12 个测试点
    ├── h264_packetizer_test.cc   # 9 个测试点
    └── h264_depacketizer_test.cc # 8 个测试点
```

## 编译与运行

### 前置依赖

只需要：
- CMake ≥ 3.20
- C++17 编译器（clang 14+ / gcc 11+）
- 网络（首次构建会用 FetchContent 拉 GoogleTest 源码包）

**不需要** libwebrtc、不需要外部 GoogleTest 安装。

### 命令

```bash
# 进入代码目录
cd 06-M4-RTP传输模块/code

# 配置 + 构建
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j

# 跑全部单元测试
ctest --output-on-failure

# 或者直接跑可执行（更详细的 GoogleTest 输出）
./tests/my_rtp_module_tests
```

期望输出：**29 个测试全部 PASSED**。

## 实现范围 vs libwebrtc 的差异

| 维度 | 本实现 | libwebrtc |
|------|--------|----------|
| 支持的编码 | H264 | H264 / VP8 / VP9 / AV1 |
| H264 打包模式 | Single NALU + FU-A | Single + STAP-A + FU-A |
| 扩展头 | 不支持（X 必须为 0）| RFC 8285 完整支持 |
| CSRC | 不支持（CC 必须为 0）| 支持 |
| 加密 | 无 | SRTP 完整集成 |
| RTX 重传 | 无 | 完整实现（独立 SSRC）|
| Pacer 节流 | 无 | 完整实现 |
| 总代码量 | ~590 行实现 + ~380 行测试 | 1500+ 行实现 + 大量测试 |

**核心打包/解包路径（Single NALU / FU-A）的字节输出与 libwebrtc 字节对齐**——下一步集成时会用 libwebrtc 的 `RtpPacketizerH264` 跑相同输入做字节级对比。

## 关键算法位置（便于阅读）

| 算法 | 文件 | 函数 |
|------|------|------|
| Annex-B 起始码扫描 | `src/h264_packetizer.cc` | `FindNextStartCodePayloadOffset` |
| FU-A 分片规划（均衡分配）| `src/h264_packetizer.cc` | `H264Packetizer::PlanFuAForNalu` |
| FU Indicator/Header 拼装 | `src/h264_packetizer.cc` | `H264Packetizer::FillFuAPacket` |
| FU-A 重组 + NALU 头还原 | `src/h264_depacketizer.cc` | `H264Depacketizer::HandleFuAPacket` |
| RTP 头大端序列化 | `src/rtp_packet.cc` | `RtpPacket::Serialize` |

## 已知简化（面试可主动声明）

1. **不支持 STAP-A**：阶段二讨论时确认 STAP-A 工程价值低（FU-A 已能覆盖所有大包场景），SPS+PPS 用两个 Single NALU 替代效果等同。
2. **不支持扩展头**：transport-cc / abs-send-time 等扩展暂不解析，在 V1 集成阶段如需要再加。
3. **PopCompletedNalu 用 `vector::erase`**：FIFO 出队 O(n)，生产环境应换 `std::deque`；学习项目下不影响测试。
4. **未做 emulation prevention 字节去除**：假设上游编码器输出已经处理过。

## 下一步（V1 阶段）

- [ ] 写 Adapter 把本实现注入 libwebrtc 的 `RtpPacketizer` 接口
- [ ] 端到端字节对齐验证（自实现的输出 vs libwebrtc 输出 byte-for-byte 相等）
- [ ] 1v1 通话联调（验证接收端能正常解码 + 显示）
