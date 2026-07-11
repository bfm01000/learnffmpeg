// MediaCodecDecoder.java
// Android MediaCodec H.264 解码器 (Surface 零拷贝输出) — 独立可用
// 详见 Doc/Android/03-MediaCodec硬解码实战.md

package com.example.media;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Surface;
import java.nio.ByteBuffer;

public class MediaCodecDecoder {

    public interface Callback {
        void onFormatReady(int width, int height);
    }

    private MediaCodec codec;
    private HandlerThread thread;
    private Handler handler;
    private Callback callback;
    private Surface outputSurface;

    public MediaCodecDecoder(Surface surface, Callback cb) {
        outputSurface = surface; callback = cb;
        thread = new HandlerThread("Decoder"); thread.start();
        handler = new Handler(thread.getLooper());
    }

    /** sps/pps: 纯 NALU 数据，无起始码 */
    public void start(byte[] sps, byte[] pps, int width, int height) {
        try {
            MediaCodecInfo info = selectHardwareCodec(MediaFormat.MIMETYPE_VIDEO_AVC);
            codec = MediaCodec.createByCodecName(info.getName());

            MediaFormat fmt = MediaFormat.createVideoFormat(
                MediaFormat.MIMETYPE_VIDEO_AVC, width, height);
            fmt.setByteBuffer("csd-0", ByteBuffer.wrap(sps)); // ★ SPS
            fmt.setByteBuffer("csd-1", ByteBuffer.wrap(pps)); // ★ PPS

            codec.configure(fmt, outputSurface, null, 0);
            codec.start();
            handler.post(this::decodeLoop);
        } catch (Exception e) { e.printStackTrace(); }
    }

    private MediaCodecInfo selectHardwareCodec(String mime) {
        MediaCodecList list = new MediaCodecList(MediaCodecList.ALL_CODECS);
        for (MediaCodecInfo info : list.getCodecInfos()) {
            if (info.isEncoder()) continue;
            for (String t : info.getSupportedTypes()) {
                if (t.equalsIgnoreCase(mime) && info.isHardwareAccelerated())
                    return info;
            }
        }
        return list.getCodecInfos()[0];
    }

    /** 喂入 Annex-B H.264 帧 */
    public void decode(byte[] annexB, long ptsUs, boolean isKeyFrame) {
        if (codec == null) return;
        int idx = codec.dequeueInputBuffer(10000);
        if (idx < 0) return;
        ByteBuffer buf = codec.getInputBuffer(idx);
        if (buf == null) return;
        buf.clear(); buf.put(annexB);
        int flags = isKeyFrame ? MediaCodec.BUFFER_FLAG_SYNC_FRAME : 0;
        codec.queueInputBuffer(idx, 0, annexB.length, ptsUs, flags);
    }

    private void decodeLoop() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        while (codec != null) {
            int idx = codec.dequeueOutputBuffer(info, 10000);
            if (idx >= 0) {
                // ★ Surface 模式: releaseOutputBuffer(idx, true) 直接渲染
                codec.releaseOutputBuffer(idx, true);
            } else if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                MediaFormat fmt = codec.getOutputFormat();
                int w = fmt.getInteger(MediaFormat.KEY_WIDTH);
                int h = fmt.getInteger(MediaFormat.KEY_HEIGHT);
                if (callback != null) callback.onFormatReady(w, h);
            }
        }
    }

    public void stop() {
        if (codec != null) { codec.stop(); codec.release(); codec = null; }
        thread.quitSafely();
    }
}
