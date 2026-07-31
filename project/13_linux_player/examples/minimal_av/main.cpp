/// Minimal audio+video SDL player — single threaded, no SDK.
/// Purpose: verify SDL2 audio+video work together before debugging the pipeline.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
}

// ── Ring buffer (same as minimal_audio) ──────────────────────────────
struct RingBuffer {
    static constexpr size_t kSize = 65536;
    uint8_t  buf[kSize];
    std::atomic<size_t> wPos{0};
    std::atomic<size_t> rPos{0};
    size_t writable() const { return kSize - (wPos.load(std::memory_order_acquire) - rPos.load(std::memory_order_acquire)); }
    void write(const uint8_t* src, size_t len) {
        size_t w = wPos.load(std::memory_order_relaxed);
        size_t off = w & (kSize - 1), n1 = kSize - off;
        if (n1 > len) n1 = len;
        memcpy(buf + off, src, n1);
        if (n1 < len) memcpy(buf, src + n1, len - n1);
        wPos.store(w + len, std::memory_order_release);
    }
    size_t read(uint8_t* dst, size_t len) {
        size_t avail = wPos.load(std::memory_order_acquire) - rPos.load(std::memory_order_acquire);
        if (len > avail) len = avail;
        if (len == 0) return 0;
        size_t r = rPos.load(std::memory_order_relaxed);
        size_t off = r & (kSize - 1), n1 = kSize - off;
        if (n1 > len) n1 = len;
        memcpy(dst, buf + off, n1);
        if (n1 < len) memcpy(dst + n1, buf, len - n1);
        rPos.store(r + len, std::memory_order_release);
        return len;
    }
};

static RingBuffer g_ring;
static volatile bool g_running = true;
void onSig(int) { g_running = false; }

void audioCallback(void*, Uint8* stream, int len) {
    size_t read = g_ring.read(stream, (size_t)len);
    if (read < (size_t)len) memset(stream + read, 0, (size_t)len - read);
}

int main(int argc, char* argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }
    signal(SIGINT, onSig); signal(SIGTERM, onSig);

    // ── 1. Open file ────────────────────────────────────────────────
    AVFormatContext* fmt = nullptr;
    avformat_open_input(&fmt, argv[1], nullptr, nullptr);
    avformat_find_stream_info(fmt, nullptr);

    int aidx = -1, vidx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        int t = fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_AUDIO && aidx < 0) aidx = (int)i;
        if (t == AVMEDIA_TYPE_VIDEO && vidx < 0) vidx = (int)i;
    }
    printf("Streams: audio=%d video=%d\n", aidx, vidx);

    // ── 2. Init codecs ──────────────────────────────────────────────
    AVCodecContext *aCtx = nullptr, *vCtx = nullptr;
    SwrContext* swr = nullptr;

    if (aidx >= 0) {
        auto* p = fmt->streams[aidx]->codecpar;
        aCtx = avcodec_alloc_context3(avcodec_find_decoder(p->codec_id));
        avcodec_parameters_to_context(aCtx, p);
        avcodec_open2(aCtx, avcodec_find_decoder(p->codec_id), nullptr);
        AVChannelLayout outCh = AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&swr, &outCh, AV_SAMPLE_FMT_S16, 44100,
                            &p->ch_layout, (AVSampleFormat)p->format, p->sample_rate, 0, nullptr);
        swr_init(swr);
        printf("Audio: %dHz %dch\n", p->sample_rate, p->ch_layout.nb_channels);
    }
    if (vidx >= 0) {
        auto* p = fmt->streams[vidx]->codecpar;
        vCtx = avcodec_alloc_context3(avcodec_find_decoder(p->codec_id));
        avcodec_parameters_to_context(vCtx, p);
        avcodec_open2(vCtx, avcodec_find_decoder(p->codec_id), nullptr);
        printf("Video: %dx%d\n", p->width, p->height);
    }

    // ── 3. SDL init — audio + video TOGETHER ────────────────────────
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);

    // Open audio device
    SDL_AudioDeviceID dev = 0;
    if (aidx >= 0) {
        SDL_AudioSpec desired;
        SDL_zero(desired);
        desired.freq = 44100; desired.format = AUDIO_S16SYS;
        desired.channels = 2; desired.samples = 1024;
        desired.callback = audioCallback;
        dev = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, SDL_AUDIO_ALLOW_ANY_CHANGE);
        printf("Audio device: %d (%s)\n", (int)dev, dev ? "OK" : SDL_GetError());
        if (dev) SDL_PauseAudioDevice(dev, 0);
    }

    // Create video window
    SDL_Window* win = nullptr;
    SDL_Renderer* rend = nullptr;
    SDL_Texture* tex = nullptr;
    int vw = 0, vh = 0;
    if (vidx >= 0) {
        vw = fmt->streams[vidx]->codecpar->width;
        vh = fmt->streams[vidx]->codecpar->height;
        win = SDL_CreateWindow("minimal_av", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               vw, vh, SDL_WINDOW_RESIZABLE);
        rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        printf("Video window: %p renderer: %p\n", (void*)win, (void*)rend);
    }

    // ── 4. Decode + play loop ───────────────────────────────────────
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int aFrames = 0, vFrames = 0;
    using namespace std::chrono;
    auto nextVTime = steady_clock::now();
    double frameDur = 0.04; // 25fps default

    if (vidx >= 0) {
        AVRational fr = fmt->streams[vidx]->r_frame_rate;
        if (fr.num > 0 && fr.den > 0) frameDur = av_q2d(av_inv_q(fr));
    }

    while (g_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) { if (ev.type == SDL_QUIT) g_running = false; }

        int ret = av_read_frame(fmt, pkt);
        if (ret < 0) { printf("\nEOF: a=%d v=%d\n", aFrames, vFrames); break; }

        if (pkt->stream_index == aidx && aCtx) {
            avcodec_send_packet(aCtx, pkt);
            while (true) {
                ret = avcodec_receive_frame(aCtx, frame);
                if (ret == AVERROR(EAGAIN)) break;
                if (ret < 0) goto out;

                uint8_t* outData = nullptr;
                int outSamp = swr_get_out_samples(swr, frame->nb_samples);
                av_samples_alloc(&outData, nullptr, 2, outSamp, AV_SAMPLE_FMT_S16, 0);
                int conv = swr_convert(swr, &outData, outSamp,
                                       (const uint8_t**)frame->data, frame->nb_samples);
                if (conv > 0) {
                    int bytes = conv * 2 * 2;
                    while (g_ring.writable() < (size_t)bytes) SDL_Delay(1);
                    g_ring.write(outData, (size_t)bytes);
                    aFrames++;
                }
                av_freep(&outData);
                av_frame_unref(frame);
            }
        } else if (pkt->stream_index == vidx && vCtx) {
            avcodec_send_packet(vCtx, pkt);
            while (true) {
                ret = avcodec_receive_frame(vCtx, frame);
                if (ret == AVERROR(EAGAIN)) break;
                if (ret < 0) goto out;

                // Create texture on first frame or resize
                if (!tex || frame->width != vw || frame->height != vh) {
                    if (tex) SDL_DestroyTexture(tex);
                    vw = frame->width; vh = frame->height;
                    tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_IYUV,
                                            SDL_TEXTUREACCESS_STREAMING, vw, vh);
                }
                SDL_UpdateYUVTexture(tex, nullptr,
                    frame->data[0], frame->linesize[0],
                    frame->data[1], frame->linesize[1],
                    frame->data[2], frame->linesize[2]);
                SDL_RenderClear(rend);
                SDL_RenderCopy(rend, tex, nullptr, nullptr);
                SDL_RenderPresent(rend);

                // Frame pacing
                auto fd = microseconds((long long)(frameDur * 1'000'000));
                nextVTime += fd;
                auto now = steady_clock::now();
                if (nextVTime > now) std::this_thread::sleep_for(nextVTime - now);
                else nextVTime = now;
                vFrames++;

                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

out:
    printf("Done: audio=%d frames, video=%d frames\n", aFrames, vFrames);

    // Cleanup
    if (dev) { SDL_Delay(300); SDL_CloseAudioDevice(dev); }
    if (tex) SDL_DestroyTexture(tex);
    if (rend) SDL_DestroyRenderer(rend);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (swr) swr_free(&swr);
    if (aCtx) avcodec_free_context(&aCtx);
    if (vCtx) avcodec_free_context(&vCtx);
    avformat_close_input(&fmt);
    return 0;
}
