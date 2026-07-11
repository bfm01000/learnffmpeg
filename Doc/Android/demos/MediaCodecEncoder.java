// MediaCodecEncoder.java
// Android MediaCodec H.264 实时编码器 — 独立可用
// minSdkVersion: 21 (KEY_LATENCY needs 26)
// 详见 Doc/Android/02-MediaCodec硬编码实战.md

package com.example.media;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import java.nio.ByteBuffer;

public class MediaCodecEncoder {

    public interface Callback {
        void onEncoded(ByteBuffer data, int offset, int size,
                       long ptsUs, int flags, boolean isKeyFrame);
    }

    private MediaCodec codec;
    private HandlerThread thread;
    private Handler handler;
    private Callback callback;
    private int width, height, fps, bitrate;
    private boolean forceKeyFrame;

    public MediaCodecEncoder(int w, int h, int f, int br, Callback cb) {
        width = w; height = h; fps = f; bitrate = br; callback = cb;
        thread = new HandlerThread("Encoder"); thread.start();
        handler = new Handler(thread.getLooper());
    }

    public void start() {
        try {
            String mime = MediaFormat.MIMETYPE_VIDEO_AVC;
            MediaCodecInfo info = selectHardwareCodec(mime);
            codec = MediaCodec.createByCodecName(info.getName());

            MediaFormat fmt = MediaFormat.createVideoFormat(mime, width, height);
            fmt.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
            fmt.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
            fmt.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
            fmt.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, fps * 2);

            // 实时编码属性
            if (info.getCapabilitiesForType(mime).isBitrateModeSupported(
                    MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR)) {
                fmt.setInteger(MediaFormat.KEY_BITRATE_MODE,
                    MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR);
            }
            if (android.os.Build.VERSION.SDK_INT >= 26) {
                fmt.setInteger(MediaFormat.KEY_LATENCY, 1);
            }
            fmt.setInteger(MediaFormat.KEY_PROFILE,
                MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline);

            codec.configure(fmt, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();
            handler.post(this::encodeLoop);
        } catch (Exception e) { e.printStackTrace(); }
    }

    private MediaCodecInfo selectHardwareCodec(String mime) {
        MediaCodecList list = new MediaCodecList(MediaCodecList.ALL_CODECS);
        for (MediaCodecInfo info : list.getCodecInfos()) {
            if (!info.isEncoder()) continue;
            for (String t : info.getSupportedTypes()) {
                if (t.equalsIgnoreCase(mime) && info.isHardwareAccelerated())
                    return info;
            }
        }
        return list.getCodecInfos()[0]; // fallback
    }

    public void encode(byte[] yuv, long ptsUs) {
        if (codec == null) return;
        int idx = codec.dequeueInputBuffer(10000);
        if (idx < 0) return;
        ByteBuffer buf = codec.getInputBuffer(idx);
        if (buf == null) return;
        buf.clear();
        buf.put(yuv);
        if (forceKeyFrame) {
            Bundle p = new Bundle();
            p.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
            codec.setParameters(p);
            forceKeyFrame = false;
        }
        codec.queueInputBuffer(idx, 0, yuv.length, ptsUs, 0);
    }

    private void encodeLoop() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        while (codec != null) {
            int idx = codec.dequeueOutputBuffer(info, 10000);
            if (idx >= 0) {
                ByteBuffer buf = codec.getOutputBuffer(idx);
                if (info.size > 0 && buf != null && callback != null) {
                    boolean kf = (info.flags & MediaCodec.BUFFER_FLAG_SYNC_FRAME) != 0;
                    callback.onEncoded(buf, info.offset, info.size,
                        info.presentationTimeUs, info.flags, kf);
                }
                codec.releaseOutputBuffer(idx, false);
            } else if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                // Format ready – SPS/PPS available via codec.getOutputFormat()
            }
        }
    }

    public void setBitrate(int br) {
        if (codec != null) {
            Bundle p = new Bundle();
            p.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, br);
            codec.setParameters(p);
        }
    }

    public void requestKeyFrame() { forceKeyFrame = true; }

    public void stop() {
        if (codec != null) { codec.stop(); codec.release(); codec = null; }
        thread.quitSafely();
    }
}
