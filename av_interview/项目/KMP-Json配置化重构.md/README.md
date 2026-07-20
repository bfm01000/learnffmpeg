# KMP SDK 重构项目文档

Vertical Industry Camera SDK 的 Kotlin Multiplatform 重构相关资料。

## 文档索引

| 文档 | 说明 |
|------|------|
| [自述.md](./自述.md) | 重构背景、踩坑、核心需求 |
| [AI驱动的大规模KMP-SDK重构实践-技术白皮书.md](./AI驱动的大规模KMP-SDK重构实践-技术白皮书.md) | 完整技术白皮书：背景、架构、AI Workflow、迁移实践 |
| [预备-KMP-Json配置化重构.md](./预备-KMP-Json配置化重构.md) | 面试准备稿：六层架构、亮点与不足 |
| [CameraParam-方案比较与取舍.md](./CameraParam-方案比较与取舍.md) | CameraParam 三种实现路径的 trade-off 分析 |
| [已知不足与后续改进.md](./已知不足与后续改进.md) | listener 线程安全 + CameraSystem ISP 问题的成因与改造方案 |

## 快速定位

- 想了解整体项目 → 读白皮书
- 想准备面试口述 → 读预备稿 + 白皮书第 10 节
- 想深入「为什么选 Template Method」→ 读 CameraParam 方案比较
- 想讲「主动暴露不足」→ 读已知不足与后续改进
