# H.264 视频压缩核心参数详解：从入门到进阶

---

## 💡 面试速记卡

**Q：H.264 中决定压缩率和延迟的核心参数有哪些？**

> **答法模板**：
> "H.264 的压缩效果主要由三个维度的参数决定：
> 1. **Profile（配置级别）**：决定算法复杂度。实时低延时场景必须用 **Baseline**（无 B 帧，解编快）；高清点播用 **High**（算法复杂，压缩率最高）。
> 2. **Preset（速度预设）**：决定 CPU 算力与压缩率的兑换关系。从 `ultrafast` 到 `veryslow`，越慢压缩率越高。实时场景通常选 `ultrafast` 并配合 `tune=zerolatency` 关闭内部缓冲。
> 3. **码率控制（CRF / Bitrate）**：直接决定画质和文件大小。点播常用 CRF 控制恒定质量，直播/实时流常用 CBR/VBR 控制带宽上限。"
> 



在视频开发和面试中，我们经常会听到“压缩率高/低”的说法。很多初学者会误以为 H.264 中有一个单一的参数叫做 `High` 或 `Low` 来控制压缩率。

实际上，H.264 的压缩效果是由**画质（码率）**、**算法复杂度（Profile）**和**编码耗时（Preset）**这三个维度的参数共同决定的。

本文将由浅入深，带你彻底搞懂 H.264 压缩中的核心参数，以及它们在真实业务（如低延时预览、离线存储）中的应用。

---

## 第一层（入门）：决定画质与文件大小的“旋钮” —— 码率控制

这是最直观影响“压缩率”的参数。你想把视频压得多小，主要看这里。

### 1. 绝对控制：Bitrate（目标码率）
直接告诉编码器：“我希望这个视频每秒钟的数据量是多少”。
- **CBR (Constant Bitrate, 固定码率)**：无论画面是静止还是剧烈运动，码率死死卡在一个固定值。
  - *场景*：极其严格的网络传输（如早期直播），防止突发流量冲爆带宽。
- **VBR (Variable Bitrate, 可变码率)**：静止画面用低码率，运动画面用高码率，平均下来接近目标值。
  - *场景*：大多数流媒体点播。

### 2. 相对控制：CRF (Constant Rate Factor, 恒定质量因子)
这是 x264 编码器最常用的模式。你不指定具体码率，而是告诉编码器“我想要什么样的视觉质量”。
- **取值范围**：`0 ~ 51`
- **规则**：**数值越低，画质越好，压缩率越低（文件越大）**；数值越高，画质越差，压缩率越高（文件越小）。
- **常用甜点区**：`18 ~ 28`（默认值通常是 23）。`18` 视觉上基本无损，`28` 以上能看出明显马赛克。

---

## 第二层（进阶）：算法复杂度与兼容性的“天梯” —— Profile & Level

这就是你印象中的 `High` 和 `Low`。它规定了编码器**“允许使用多么高级/复杂的数学算法”**来压缩视频。

### 1. Profile（配置级别）
级别越高，同等画质下文件越小（压缩率越高），但对设备的**解码算力**要求也越高，且**延迟越大**。

*   **Baseline Profile (基础/低级别)**
    *   **特点**：算法最简单。**不支持 B 帧**（双向预测帧），不支持 CABAC（高级算术编码）。
    *   **压缩率**：最低。
    *   **核心优势**：**极低延迟！** 因为没有 B 帧，解码器不需要等待后面的帧就能解码当前帧。
    *   **适用场景**：车载相机实时预览、微信视频通话、安防监控、老旧低端手机。
*   **Main Profile (主流级别)**
    *   **特点**：引入了 B 帧和 CABAC 编码。
    *   **压缩率**：中等。
    *   **适用场景**：网页视频点播、标清电视广播。
*   **High Profile (高级别)**
    *   **特点**：引入了 8x8 内部预测等极其复杂的算法。
    *   **压缩率**：**最高**。
    *   **核心劣势**：编码和解码极其消耗 CPU/GPU 算力，且因为大量使用 B 帧，天然带有延迟。
    *   **适用场景**：蓝光光盘、4K/1080P 高清电影点播。

### 2. Level（等级）
如果说 Profile 限制了“算法”，那么 Level 就限制了“性能上限”（最大分辨率、最大帧率、最大码率）。
- 例如 `Level 4.1` 最高支持 1080P@30fps，`Level 5.1` 支持 4K@30fps。
- 硬件解码芯片通常会标明自己最高支持到哪个 Profile 和 Level。

---

## 第三层（高阶）：算力与时间的“交易” —— Preset & Tune

当你选定了 Profile（算法上限）和 CRF（目标画质）后，编码器内部还需要做一件事：**搜索最优解**。

### 1. Preset（编码速度预设）
它决定了编码器花多少 CPU 时间去寻找最高效的压缩方式。
- **预设档位（从快到慢）**：
  `ultrafast` -> `superfast` -> `veryfast` -> `faster` -> `fast` -> `medium` (默认) -> `slow` -> `slower` -> `veryslow`
- **核心逻辑**：
  - 选 `ultrafast`：编码器随便找个差不多的压缩方案就输出了。**结果：CPU 占用极低，速度极快，但同等画质下文件很大（压缩率低）。**
  - 选 `veryslow`：编码器穷举各种可能性，寻找最完美的像素复用方案。**结果：CPU 满载，速度极慢，但同等画质下文件压到了极致（压缩率极高）。**

### 2. Tune（场景调优）
告诉编码器你的视频是什么类型的，让它对症下药。
- `film`：真实电影（保留胶片颗粒感）。
- `animation`：动画片（大面积纯色，线条锐利）。
- **`zerolatency`（零延迟）**：**极其重要！** 强制关闭编码器内部的 lookahead（向前看）缓冲，强制不使用 B 帧。专为实时低延迟场景设计。

---

## 第四层（实战）：不同业务场景下的 FFmpeg 参数组合

理解了上面的原理，我们来看看真实工程中怎么组合这些参数。

### 场景 A：车载相机低延时预览 / 视频会议 (实时性 > 一切)
- **诉求**：绝对不能有延迟，哪怕画质稍微差一点、带宽占用稍微高一点。
- **参数选择**：`Baseline Profile` + `ultrafast` + `zerolatency`
- **FFmpeg 示例**：
  ```bash
  ffmpeg -i input.mp4 -c:v libx264 -profile:v baseline -preset ultrafast -tune zerolatency -b:v 2000k output.mp4
  ```

### 场景 B：离线高清电影压制 / 短视频存储 (压缩率 > 一切)
- **诉求**：文件尽可能小（省 CDN 流量费），画质尽可能高。不在乎压制需要花几个小时。
- **参数选择**：`High Profile` + `slow/veryslow` + `CRF 18~23`
- **FFmpeg 示例**：
  ```bash
  ffmpeg -i input.mp4 -c:v libx264 -profile:v high -preset slow -crF 18 output.mp4
  ```

---

## 第五层（代码实战）：在 C++ (FFmpeg API) 中如何设置

在实际的 C/C++ 开发中，我们通常通过 `AVCodecContext` 的成员变量，或者使用 `av_opt_set` 操作编码器的私有数据（`priv_data`）来配置这些参数。

### 1. 设置目标码率 (Bitrate)
如果使用 CBR 或 VBR，直接设置 `AVCodecContext` 的成员变量：
```cpp
// 设置目标码率为 2000 kbps (2Mbps)
codec_ctx->bit_rate = 2000000; 

// 可选：设置最大/最小码率（用于 VBR 控制波动范围）
codec_ctx->rc_max_rate = 2500000;
codec_ctx->rc_min_rate = 1500000;
// 设置码率控制缓冲区大小（通常与 max_rate 配合使用）
codec_ctx->rc_buffer_size = 2000000;
```

### 2. 设置 CRF (恒定质量)
CRF 是 x264 编码器的私有参数，需要引入 `<libavutil/opt.h>` 并通过 `av_opt_set` 设置：
```cpp
#include <libavutil/opt.h>

// 设置 CRF 为 23（数字越小画质越好，文件越大）
av_opt_set(codec_ctx->priv_data, "crf", "23", 0);
```

### 3. 设置 Profile
Profile 可以通过 `AVCodecContext` 的宏定义设置，也可以通过字符串设置：
```cpp
// 方式一：使用 FFmpeg 提供的宏定义
codec_ctx->profile = FF_PROFILE_H264_BASELINE; // 低延时选 Baseline
// codec_ctx->profile = FF_PROFILE_H264_MAIN;
// codec_ctx->profile = FF_PROFILE_H264_HIGH;

// 方式二：通过字典或 opt 设置
av_opt_set(codec_ctx->priv_data, "profile", "baseline", 0);
```

### 4. 设置 Preset 和 Tune
这两个参数也是 x264 的私有参数，必须通过 `av_opt_set` 传递：
```cpp
// 设置 Preset：牺牲压缩率换取极快的编码速度
av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);

// 设置 Tune：零延迟，强制关闭内部缓冲和 B 帧（低延时必备）
av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
```

### 5. 完整代码示例：初始化一个低延时编码器
```cpp
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <iostream>

bool InitLowLatencyEncoder(AVCodecContext*& codec_ctx) {
    // 1. 查找 H.264 编码器 (通常是 libx264)
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return false;

    // 2. 分配编码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    
    // 3. 设置基本参数 (宽高、时间基、像素格式等)
    codec_ctx->width = 1920;
    codec_ctx->height = 1080;
    codec_ctx->time_base = {1, 30};
    codec_ctx->framerate = {30, 1};
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    // 4. 设置低延时核心参数 (面试考点代码化)
    codec_ctx->profile = FF_PROFILE_H264_BASELINE;              // 禁用 B 帧
    av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0); // 极速编码，降低 CPU 耗时
    av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0); // 零延迟调优，关闭内部 lookahead
    
    // 5. 设置码率 (例如 2Mbps)
    codec_ctx->bit_rate = 2000000;

    // 6. 打开编码器
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Failed to open encoder!" << std::endl;
        return false;
    }
    return true;
}
```

