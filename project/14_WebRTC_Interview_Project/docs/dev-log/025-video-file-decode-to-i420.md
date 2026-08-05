# 025 - 统一视频素材解码到 I420

## 本次做了什么

- 新增 `native/video_file_decode` 工具。
- 使用 FFmpeg 读取用户指定的统一素材：`/home/bfm01000/workspace/video_downloads/FRXXZ.mp4`。
- 完成 MP4 解封装、H.264 解码、`sws_scale` 转 I420。
- 输出 raw I420 文件和帧时间戳 CSV。
- 生成首帧 JPG 预览：`artifacts/frxxz-first-frame.jpg`。

## 遇到的问题

1. 一开始清理 warning 时，误把 PowerShell 的反引号换行写成了 C++ 源码里的字面量 `` `n``。
2. raw I420 本身不能直接查看，需要再用 FFmpeg 指定 `rawvideo + yuv420p + size` 才能转成 JPG。

## 怎么解决

- 重新查看源码行号，删除误插入的 `` `n`` 行。
- 用下面命令验证 I420 是否真的可读：

```bash
ffmpeg -f rawvideo -pix_fmt yuv420p -s 320x180 \
  -i /tmp/frxxz-first-frame.i420 \
  -frames:v 1 /tmp/frxxz-first-frame.jpg -y
```

## 验证结果

```text
codec      : h264
source     : 852x480 pix_fmt=yuv420p
i420       : 320x180, 86400 bytes/frame
frames     : 10
```

CSV 前几帧：

```text
0,0.000000,320,180,i420,86400
1,0.040000,320,180,i420,86400
2,0.080000,320,180,i420,86400
```

说明素材帧间隔约 40ms，也就是 25fps。

## 面试讲法

可以把这一步讲成“真实媒体输入进入 WebRTC 前的标准化”。我没有直接把 MP4 硬塞进 PeerConnection，而是先把媒体文件解码成 WebRTC 友好的 I420 帧，并把 PTS 导出。这样后面接入 native sender 时，可以分别讨论解码、像素格式转换、时间戳驱动、WebRTC source 推帧，而不是把所有复杂度混在一起。

## 下一步

把这个工具里的解码逻辑抽成 `VideoFileFrameReader`，再接入 `low_latency_sender --source file --file /home/bfm01000/workspace/video_downloads/FRXXZ.mp4`。