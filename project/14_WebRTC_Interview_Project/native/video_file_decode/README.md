# video_file_decode

`video_file_decode` 是 native sender 接入真实视频文件前的解码验证工具。

它做的事情很明确：

1. 用 FFmpeg 打开 MP4。
2. 找到视频流并创建 decoder。
3. 解码视频帧。
4. 用 `libswscale` 转成 I420/YUV420P。
5. 输出 raw `.i420` 和每帧时间戳 CSV。

## 构建

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project/native
cmake -S . -B build
cmake --build build --target video_file_decode -j 8
```

## 使用统一素材测试

```bash
native/build/video_file_decode/video_file_decode \
  --input /home/bfm01000/workspace/video_downloads/FRXXZ.mp4 \
  --frames 10 \
  --width 320 \
  --height 180 \
  --output /tmp/frxxz-320x180-10f.i420 \
  --csv /tmp/frxxz-320x180-10f.csv
```

验证输出：

```text
codec      : h264
source     : 852x480 pix_fmt=yuv420p
i420       : 320x180, 86400 bytes/frame
frames     : 10
```

320x180 I420 每帧大小：

```text
320 * 180 * 3 / 2 = 86400 bytes
```

## 首帧预览

I420 是 raw video，不能直接用普通图片查看。可以用 FFmpeg 转出一张 JPG：

```bash
ffmpeg -f rawvideo -pix_fmt yuv420p -s 320x180 \
  -i /tmp/frxxz-first-frame.i420 \
  -frames:v 1 artifacts/frxxz-first-frame.jpg -y
```

## 为什么这一步重要

WebRTC 原生视频发送最终需要喂 `webrtc::VideoFrame`。常用路径是把外部输入统一成 I420，然后创建 `I420Buffer` 或相关 buffer，再交给自定义 `VideoTrackSourceInterface`。这个工具先证明：统一素材可以被稳定解码、缩放并转成 I420，下一步再把这段逻辑封装成 sender 内部的视频文件 source。