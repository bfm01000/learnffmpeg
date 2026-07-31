/// Minimal SDL2 audio player — single-file, single-threaded.
/// Purpose: verify SDL2 audio output works before building complex pipelines.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
}

// ── Ring buffer (lock-free SPSC) ─────────────────────────────────────
struct RingBuffer {
    static constexpr size_t kSize = 65536; // must be power of 2
    uint8_t  buf[kSize];
    std::atomic<size_t> wPos{0};
    std::atomic<size_t> rPos{0};

    size_t writable() const {
        size_t w = wPos.load(std::memory_order_acquire);
        size_t r = rPos.load(std::memory_order_acquire);
        return kSize - (w - r);
    }
    size_t readable() const {
        size_t w = wPos.load(std::memory_order_acquire);
        size_t r = rPos.load(std::memory_order_acquire);
        return w - r;
    }
    void write(const uint8_t* src, size_t len) {
        size_t w = wPos.load(std::memory_order_relaxed);
        size_t off = w & (kSize - 1);
        size_t n1 = kSize - off;
        if (n1 > len) n1 = len;
        memcpy(buf + off, src, n1);
        if (n1 < len) memcpy(buf, src + n1, len - n1);
        wPos.store(w + len, std::memory_order_release);
    }
    size_t read(uint8_t* dst, size_t len) {
        size_t avail = readable();
        if (len > avail) len = avail;
        if (len == 0) return 0;
        size_t r = rPos.load(std::memory_order_relaxed);
        size_t off = r & (kSize - 1);
        size_t n1 = kSize - off;
        if (n1 > len) n1 = len;
        memcpy(dst, buf + off, n1);
        if (n1 < len) memcpy(dst + n1, buf, len - n1);
        rPos.store(r + len, std::memory_order_release);
        return len;
    }
};

// ── Globals ──────────────────────────────────────────────────────────
static RingBuffer          g_ring;
static std::atomic<bool>   g_running{true};
static std::atomic<double> g_clock{0.0};

void onSig(int) { g_running.store(false); }

// ── SDL audio callback ───────────────────────────────────────────────
void sdlCallback(void*, Uint8* stream, int len) {
    size_t read = g_ring.read(stream, static_cast<size_t>(len));
    if (read < static_cast<size_t>(len)) {
        memset(stream + read, 0, static_cast<size_t>(len) - read);
    }
    // Advance audio clock: 16-bit stereo → bytes/4 = samples
    double elapsed = static_cast<double>(len) / (4.0 * 44100.0);
    g_clock.store(g_clock.load(std::memory_order_acquire) + elapsed,
                  std::memory_order_release);
}

// ── main ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }
    signal(SIGINT, onSig); signal(SIGTERM, onSig);

    // ── 1. Open file + find audio stream ─────────────────────────────
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, argv[1], nullptr, nullptr) < 0) {
        fprintf(stderr, "Cannot open: %s\n", argv[1]); return 1;
    }
    avformat_find_stream_info(fmt, nullptr);

    int aidx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            aidx = (int)i; break;
        }
    }
    if (aidx < 0) { fprintf(stderr, "No audio stream\n"); return 1; }

    AVCodecParameters* par = fmt->streams[aidx]->codecpar;
    printf("Audio: codec=%s rate=%d ch=%d fmt=%d\n",
           avcodec_get_name(par->codec_id), par->sample_rate,
           par->ch_layout.nb_channels, (int)par->format);

    // ── 2. Open decoder ──────────────────────────────────────────────
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, par);
    avcodec_open2(ctx, codec, nullptr);

    // ── 3. Setup resampler → S16 stereo 44100 ────────────────────────
    SwrContext* swr = nullptr;
    AVChannelLayout outChLayout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &outChLayout, AV_SAMPLE_FMT_S16, 44100,
                        &par->ch_layout, (AVSampleFormat)par->format,
                        par->sample_rate, 0, nullptr);
    swr_init(swr);
    printf("Resampler: in=%dHz/%dch → out=44100Hz/2ch/S16\n",
           par->sample_rate, par->ch_layout.nb_channels);

    // ── 4. Open SDL audio device ─────────────────────────────────────
    SDL_Init(SDL_INIT_AUDIO);
    SDL_AudioSpec desired;
    SDL_zero(desired);
    desired.freq     = 44100;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 1024;
    desired.callback = sdlCallback;
    SDL_AudioSpec obtained;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &desired,
        &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (dev == 0) {
        fprintf(stderr, "SDL audio open failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("SDL audio: opened freq=%d fmt=%d ch=%d\n",
           (int)obtained.freq, (int)obtained.format, (int)obtained.channels);
    SDL_PauseAudioDevice(dev, 0); // start playback

    // ── 5. Decode loop ───────────────────────────────────────────────
    AVPacket* pkt = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    int frameCount = 0;

    while (g_running.load(std::memory_order_acquire)) {
        int ret = av_read_frame(fmt, pkt);
        if (ret < 0) { printf("\nEOF after %d audio frames\n", frameCount); break; }
        if (pkt->stream_index != aidx) { av_packet_unref(pkt); continue; }

        avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);

        while (true) {
            ret = avcodec_receive_frame(ctx, frame);
            if (ret == AVERROR(EAGAIN)) break;
            if (ret < 0) goto done;

            // Resample → S16 stereo
            uint8_t* outData = nullptr;
            int outSamples = swr_get_out_samples(swr, frame->nb_samples);
            av_samples_alloc(&outData, nullptr, 2, outSamples,
                             AV_SAMPLE_FMT_S16, 0);
            int converted = swr_convert(swr, &outData, outSamples,
                                        (const uint8_t**)frame->data,
                                        frame->nb_samples);
            if (converted > 0) {
                int bytes = converted * 2 * 2; // samples * channels * bytes_per_sample
                // Wait for ring buffer space
                while (g_ring.writable() < static_cast<size_t>(bytes)) {
                    SDL_Delay(1);
                }
                g_ring.write(outData, static_cast<size_t>(bytes));
                frameCount++;
            }
            av_freep(&outData);
            av_frame_unref(frame);
        }
    }

done:
    printf("Decoded %d audio frames, clock=%.1fs\n",
           frameCount, g_clock.load());

    // ── 6. Drain + cleanup ───────────────────────────────────────────
    SDL_Delay(500); // let ring buffer drain
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    printf("Done.\n");
    return 0;
}
