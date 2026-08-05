# AudioTrack 与 AudioRecord 详解：Android 音频采集与播放

## 0. 本篇定位

- 面试复习：先掌握 `AudioRecord`/`AudioTrack` 的缓冲区、回调线程、采样率/声道/采样格式，以及低延迟场景为什么要关注 AAudio/OpenSL ES。
- 深入学习：重点看音频焦点、路由、蓝牙延迟、AEC/NS/AGC 和 underrun/overrun 排查。
- 工程落点：音频模块真正考验的是实时线程纪律、时钟稳定性和与视频时间戳的同步，而不是单纯会打开麦克风。
> **适用方向**：Android 实时音频（直播/RTC/VoIP）
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 8 分钟｜全文 25 分钟
> **关联文档**：[[00-Android音视频开发全景导读]] · [[06-端到端采集编码推流管线]]

---

## 一、Android 音频 API 选型

| API | 级别 | 延迟 | 适合场景 |
|-----|------|------|---------|
| **AudioTrack/AudioRecord** | Java | ~20-50ms | 大多数场景，API 简单 |
| **AAudio** (API 26+) | NDK C | ~5-10ms | 低延迟 RTC/乐器/游戏，**推荐** |
| **OpenSL ES** | NDK C | ~10-30ms | 跨平台（Android 4.1+），API 繁琐 |
| **MediaPlayer** | Java | 高 | 本地文件播放 |
| **AudioTrack.Builder** (API 21+) | Java | ~20-50ms | 新 API，比旧 AudioTrack 更清晰 |

---

## 二、面试速记

| # | 考点 | 一句话答案 |
|---|------|-----------|
| 1 | AudioRecord 怎么采 PCM | new AudioRecord → startRecording → read(byte[]) → 16bit PCM |
| 2 | AudioTrack 怎么播 PCM | new AudioTrack → play → write(byte[]) → 16bit PCM → 扬声器 |
| 3 | 怎么算出 buffer size | `AudioRecord.getMinBufferSize(sr,ch,fmt)` — 返回的是最小大小，实际用 2-4 倍 |
| 4 | 延迟来自哪里 | buffer 大小 + 音频 HAL 延迟 + 蓝牙额外延迟(50-200ms) |
| 5 | AAudio 为什么延迟更低 | 绕过 AudioFlinger，直接和 HAL 通信，支持 exclusive/mmap 模式 |
| 6 | 音频线程要注意什么 | 不要阻塞、不要加锁、不要 alloc，和 iOS AudioUnit 同样的硬实时要求 |
| 7 | 怎么处理 AudioFocus | AudioManager.requestAudioFocus → 电话/其他 App 抢占时回调 → 暂停/降低音量 |
| 8 | 蓝牙延迟怎么看 | SCO(HFP) 8kHz 高延迟，A2DP 高音质高延迟，BLE Audio(LC3) 低延迟 |

---

## 三、核心 Demo

### 3.1 音频采集（AudioRecord）

```java
// AudioCapture.java
package com.example.audio;

import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Handler;
import android.os.HandlerThread;

public class AudioCapture {
    private static final int SAMPLE_RATE = 48000;      // 48kHz
    private static final int CHANNEL = AudioFormat.CHANNEL_IN_MONO;
    private static final int FORMAT = AudioFormat.ENCODING_PCM_16BIT;

    private AudioRecord recorder;
    private int bufferSize;
    private HandlerThread audioThread;
    private Handler audioHandler;

    public interface AudioCallback {
        void onPCMData(byte[] pcm, int length);
    }

    public AudioCapture(AudioCallback callback) {
        // ★ 计算最小 buffer 大小，实际用 2 倍
        int minBuffer = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL, FORMAT);
        bufferSize = Math.max(minBuffer * 2, 2048);  // 至少 2KB

        recorder = new AudioRecord.Builder()
            .setAudioSource(MediaRecorder.AudioSource.VOICE_COMMUNICATION)
            // ★ VOICE_COMMUNICATION: 系统会做回声消除/降噪（走音频 DSP）
            .setAudioFormat(new AudioFormat.Builder()
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(CHANNEL)
                .setEncoding(FORMAT)
                .build())
            .setBufferSizeInBytes(bufferSize)
            .build();

        audioThread = new HandlerThread("AudioCapture");
        audioThread.start();
        audioHandler = new Handler(audioThread.getLooper());

        // 启动采集循环
        audioHandler.post(() -> {
            recorder.startRecording();
            byte[] buffer = new byte[bufferSize];
            while (recorder.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) {
                int bytesRead = recorder.read(buffer, 0, buffer.length);
                if (bytesRead > 0) {
                    callback.onPCMData(buffer, bytesRead);
                    // ★ 这里不要做耗时操作：直接 dispatch 到其他线程编码/推流
                }
            }
        });
    }

    public void stop() {
        recorder.stop();
        recorder.release();
        audioThread.quitSafely();
    }
}
```

### 3.2 音频播放（AudioTrack）

```java
// AudioPlayer.java
package com.example.audio;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;

public class AudioPlayer {
    private static final int SAMPLE_RATE = 48000;
    private AudioTrack track;
    private int bufferSize;

    public AudioPlayer() {
        int minBuffer = AudioTrack.getMinBufferSize(SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        bufferSize = Math.max(minBuffer * 2, 4096);

        track = new AudioTrack.Builder()
            .setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                // ★ VOICE_COMMUNICATION: 走通话音量（不是媒体音量）
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build())
            .setAudioFormat(new AudioFormat.Builder()
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .build())
            .setBufferSizeInBytes(bufferSize)
            .setTransferMode(AudioTrack.MODE_STREAM)  // ★ 流模式：不断 write
            // 低延迟可设: .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
            .build();

        track.play();
    }

    /** 播一帧 PCM */
    public int playPCM(byte[] pcm, int offset, int size) {
        return track.write(pcm, offset, size);
        // ★ write 会阻塞直到 buffer 有足够空间，不要在 UI 线程调
    }

    public void stop() {
        track.stop();
        track.release();
    }
}
```

### 3.3 AAudio（API 26+ NDK，更低延迟）

```cpp
// aa_audio.cpp — AAudio NDK 示例
#include <aaudio/AAudio.h>

AAudioStream *stream = nullptr;
AAudioStreamBuilder *builder = nullptr;

AAudio_createStreamBuilder(&builder);
AAudioStreamBuilder_setDeviceId(builder, AAUDIO_UNSPECIFIED);
AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
AAudioStreamBuilder_setSampleRate(builder, 48000);
AAudioStreamBuilder_setChannelCount(builder, 1);
AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
// ★ LOW_LATENCY: 走 FastMixer 通路，延迟 ~5-10ms

AAudioStreamBuilder_openStream(builder, &stream);
AAudioStream_requestStart(stream);

// 写音频
AAudioStream_write(stream, pcmData, numFrames, timeoutNs);
```

---

## 四、AudioManager / AudioFocus

```java
AudioManager audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

// 请求焦点（直播/RTC）
AudioAttributes attrs = new AudioAttributes.Builder()
    .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
    .build();
AudioFocusRequest request = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
    .setAudioAttributes(attrs)
    .setOnAudioFocusChangeListener(focusChange -> {
        switch (focusChange) {
            case AudioManager.AUDIOFOCUS_LOSS:
                // 永久失去焦点 → 停止播放
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                // 暂时失去（如通知音）→ 暂停
                break;
            case AudioManager.AUDIOFOCUS_GAIN:
                // 重新获得焦点 → 恢复
                break;
        }
    })
    .build();
audioManager.requestAudioFocus(request);
```

---

## 五、常见坑

### 坑 1：音频线程阻塞导致爆音

AudioRecord.read() 和 AudioTrack.write() 都会阻塞。如果在同一线程做 AAC 编码/网络发送，延迟累加导致 buffer 空了 → 爆音。**对策**：音频线程只做 read/write，数据通过无锁队列传给编码/发送线程。

### 坑 2：buffer size 太小导致频繁回调

`getMinBufferSize` 返回的是硬件的最小要求，实际用 2-4 倍。太小 → 每 1-2ms 回调一次 → CPU 被中断淹没。

### 坑 3：蓝牙延迟差异巨大

- SCO（通话蓝牙）：8kHz 采样率、高压缩、延迟 ~50-100ms
- A2DP（音乐蓝牙）：高音质但延迟 ~100-250ms
- BLE Audio with LC3 codec：低延迟 ~20-40ms（Android 13+）
- 开发时注意**实际输出设备**，蓝牙模式下 AudioTrack.write 可能返回比预期慢

### 坑 4：音频源选错导致没有回声消除

选 `VOICE_COMMUNICATION` → 系统启动音频 DSP 做 AEC/AGC/NS。选 `MIC` 或 `DEFAULT` → 只有裸麦克风数据，RTC 场景会有回声。

---

## 一句话总结
> Android 音频 = AudioRecord(AudioSource.VOICE_COMMUNICATION, 48kHz, 16bit, mono) 采 PCM → AudioTrack 播 PCM。低延迟场景用 AAudio(API 26+) 或 PERFORMANCE_MODE_LOW_LATENCY。音频线程不能阻塞，用无锁队列解耦。记得处理 AudioFocus 和蓝牙延迟。

## 关联文档
- [[06-端到端采集编码推流管线]] — 音频在推流管线中的位置
- [[../ffmpeg/18-FFmpeg音频编解码详解]] — PCM → AAC 编码
