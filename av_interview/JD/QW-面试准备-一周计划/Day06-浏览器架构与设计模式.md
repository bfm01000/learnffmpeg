# Day 6 · 浏览器架构与设计模式

**目标**：满足 JD「了解 Chromium/CEF」的**面试级概念**（内推说不强制经验）；补齐工厂 + 生产者消费者理论。  
**建议时长**：4～5 小时

---

## 上午块（2.5h）· 浏览器内核扫盲

### 1. 必会：一张架构图（能手绘）

```
用户输入 URL
    ↓
Browser Process（网络、Tab、权限）
    ↓ IPC
Renderer Process（Blink：HTML/CSS → Layout → Paint）
    ↓
Compositor / GPU Process（合成、上屏）
    ↓
OS 显示
```

### 2. 必会名词（每个用一句话解释）

| 名词 | 一句话 |
|------|--------|
| Chromium | 开源浏览器工程，多进程 + Blink |
| Blink | 渲染引擎：布局、绘制 |
| WebKit | Safari  lineage，概念上与 Blink 同源 |
| CEF | Chromium Embedded Framework，App 内嵌网页 |
| Electron | Node + Chromium，桌面跨平台容器 |
| PDFium | Chromium 系 PDF 渲染库 |
| Skia | 2D 图形库，Chromium/Flutter 绘制后端 |
| 沙箱 | Renderer 受限，降低漏洞危害 |

### 3. 阅读资源（选 1，共 1～1.5h）

- 官方 high-level：[Chromium multi-process architecture](https://www.chromium.org/developers/design-documents/multi-process-architecture/)
- 或中文笔记：搜索「Chromium 多进程架构」选一篇 20min 读完即可
- **不要**今天开始编译 Chromium（性价比低）

### 4. 与自身经历的对照话术（写下来）

| 浏览器概念 | 你的类似经验 |
|------------|--------------|
| 多进程隔离 | SDK 进程内多线程 + 解码/渲染/网络分工 |
| 跨进程/层桥接 | JNI / OC ↔ C++ |
| 渲染管线 | OpenGL ES 外部纹理 → Surface → 编码器 |
| 性能指标 | 帧率/延时/发热 ↔ 首屏/内存/流畅度（方法论同：先测量） |

---

## 下午块（1.5h）· 设计模式（对齐 补强.md）

### 工厂模式

- **定义**：创建逻辑与使用逻辑分离
- **你的例子**：按平台/编码格式创建 `Decoder` / `Encoder` / `Muxer` 实例；JNI 只暴露工厂接口
- **面试一句**：避免 if-else 散落，新增编码器类型只改工厂注册

### 生产者-消费者模式

- **定义**：解耦生产与消费速率
- **你的例子**：Day3 映射表三条
- **浏览器里**：网络线程生产资源 → 主线程/合成线程消费

### 自检：各用 1 分钟口述，不卡壳

---

## 晚间块（1h）· 算法 + 概念

### 刷题 2 题（轻量）

| 题目 | 说明 |
|------|------|
| [20. 有效的括号](https://leetcode.cn/problems/valid-parentheses/) | 栈，10min 热身 |
| [141. 环形链表](https://leetcode.cn/problems/linked-list-cycle/) | 快慢指针复习 |

### 转型叙事定稿（20min）

熟读 `自我介绍与项目话术模板.md`，填入个人数据。

---

## 今日产出物

- [ ] 手绘/architecture 图 1 张（拍照存手机）
- [ ] 工厂 + 生产者消费者各 1 分钟口述录音
- [ ] 算法 +2，全周累计 ≥14
