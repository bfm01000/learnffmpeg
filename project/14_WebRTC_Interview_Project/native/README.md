# Native Modules

Phase 3 native experiments for LowLatency WebRTC Lab.

## Build

```bash
cd native
cmake -S . -B build
cmake --build build
```

## V4L2 Capture

List camera formats:

```bash
./build/v4l2_capture/v4l2_capture --device /dev/video0 --list
```

Capture one frame:

```bash
./build/v4l2_capture/v4l2_capture --device /dev/video0 --format mjpeg --size 640x480 --output ../captures/native-camera.jpg
```

In the current WSL USB/IP camera environment, MJPEG capture is verified. YUYV format enumeration and S_FMT may work, but frame dequeue can time out depending on the device path and USB transport behavior.


## FFmpeg Probe

Probe packet timestamps and GOP:

```bash
./build/ffmpeg_probe/ffmpeg_probe --input ../../11_FrameIndex_Extraction_Demo/test_short_gop.mp4 --packets 40
```

The verified sample reports H.264 video, `time_base=1/15360`, 30fps, and GOP interval of 15 video packets.

Default media sample for probe experiments:

```bash
/home/bfm01000/workspace/video_downloads/FRXXZ.mp4
```

```bash
./build/ffmpeg_probe/ffmpeg_probe --input /home/bfm01000/workspace/video_downloads/FRXXZ.mp4 --packets 40
```
## H.264 RTP Packetizer

Generate an Annex-B H.264 sample from the unified media file:

```bash
./build/ffmpeg_probe/ffmpeg_probe --input /home/bfm01000/workspace/video_downloads/FRXXZ.mp4 --packets 80 --annexb-output ../captures/frxxz-first80.h264
```

Packetize the Annex-B sample into RTP packet metadata:

```bash
./build/h264_rtp_packetizer/h264_rtp_packetizer --input ../captures/frxxz-first80.h264 --output ../captures/frxxz-first80-rtp.csv --max-payload 1200 --fps 25
```

This demo outputs CSV instead of sending UDP packets. The purpose is to make H.264 over RTP concepts visible: NALU type, sequence number, RTP timestamp, marker bit, single NALU packetization, and FU-A fragmentation.
