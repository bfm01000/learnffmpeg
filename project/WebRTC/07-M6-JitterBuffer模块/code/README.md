# M6 · 视频 Jitter Buffer · B 层代码

> 阶段三 M6 Jitter Buffer 模块的 B 层重写实现。**不依赖 libwebrtc**，纯 C++17 + GoogleTest。
> 配套主文档：`../阶段三-M6-JitterBuffer模块.md`。

## 目录结构

```
code/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── jitter_buffer.h          # 公共接口（IJitterBuffer + 数据结构）
│   ├── packet_buffer.h          # 包级环形缓冲 + 帧完整性判定
│   └── jitter_estimator.h       # EWMA 抗抖估计
├── src/
│   ├── packet_buffer.cc         # 环形 buffer + 拼帧（SeqNum 连续 + Marker）
│   ├── jitter_estimator.cc      # EWMA 公式 + 目标延迟（3σ + decode + render）
│   └── jitter_buffer.cc         # 总入口：丢包检测 + 关键帧请求 + 渲染调度
└── tests/
    ├── CMakeLists.txt           # FetchContent 拉 GoogleTest
    ├── jitter_estimator_test.cc # 7 个测试点
    ├── packet_buffer_test.cc    # 11 个测试点
    └── jitter_buffer_test.cc    # 9 个测试点
```

## 编译与运行

```bash
cd 阶段三-M6-JitterBuffer模块/code
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j && ctest --output-on-failure
```

期望输出：**27 个测试全部 PASSED**。

## 核心算法位置

| 算法 | 文件 | 函数 |
|------|------|------|
| 环形 buffer SeqNum 索引 + wraparound | `src/packet_buffer.cc` | `PacketBuffer::InsertPacket` |
| 帧完整性判定（首包 + 连续 SeqNum + Marker）| `src/packet_buffer.cc` | `PacketBuffer::TryFindFrameStartingAt` |
| 增量扫描已完整帧 | `src/packet_buffer.cc` | `PacketBuffer::ExtractCompletedFrames` |
| EWMA 抗抖更新 | `src/jitter_estimator.cc` | `JitterEstimator::OnFrameReceived` |
| 目标延迟 = 3σ + decode + render | `src/jitter_estimator.cc` | `JitterEstimator::GetTargetDelayMs` |
| 丢包检测 + NACK 反馈 | `src/jitter_buffer.cc` | `JitterBufferImpl::InsertPacket` |
| 关键帧请求触发（连续丢包 ≥ 10）| `src/jitter_buffer.cc` | 同上 |
| 渲染时刻调度 | `src/jitter_buffer.cc` | 同上 |

## 实现范围 vs libwebrtc 的差异

| 维度 | 本实现 | libwebrtc |
|------|--------|----------|
| 抗抖算法 | EWMA（单状态变量）| Kalman（双状态变量）|
| 参考帧链分析 | 不做 | FrameBuffer 完整 DAG 判定 |
| 重传优先级 | 一视同仁 | keyframe 范围内 SeqNum 高优先级 |
| 音频支持 | 无 | NetEQ（独立的 10k+ 行模块）|
| 线程模型 | 单线程 | network/decoder 双线程同步 |
| 总代码量 | ~530 行实现 + ~430 行测试 | 2000+ 行实现 + 大量测试 |

**核心数据通路（环形 buffer + 完整性判定 + EWMA + 渲染调度）和 libwebrtc 等价**——稳态网络下行为接近。

## 已知简化（面试可主动声明）

1. **EWMA 替代 Kalman**：突发抖动场景收敛速度慢 5-10 倍，稳态准确度接近。
2. **不做参考帧链分析**：默认所有完整帧都可解，由解码器 `ReferenceFrameMissing` 回调兜底。
3. **不做 NACK 延迟批处理**：本模块只检测丢包并立刻上报，"等一个 RTT 再发"由 NACK 模块（独立的下游模块）实现。
4. **单线程假设**：调用方应保证 `InsertPacket` 和 `PopNextCompletedFrame` 在同一线程；libwebrtc 用条件变量做跨线程同步。
5. **没有 MaxWaitingTime 上限**：renderTimeMs 直接返回不做"缓冲过深加速吐"的逃生通道。

## 下一步（V2 阶段）

- [ ] 写 Adapter 把本模块替换 libwebrtc 的 `PacketBuffer` + `FrameBuffer`
- [ ] 模拟丢包/抖动测试：用 tc/netem 在 macOS 上模拟 1%/5% 丢包 + 50/100ms 抖动
- [ ] 性能 benchmark：对比 EWMA vs libwebrtc 的 Kalman，画 buffer 深度变化曲线
