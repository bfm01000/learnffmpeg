/// Minimal SDL2 video player with frame pacing.
/// Single-threaded. Verify SDL YUV rendering pipeline works.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>
}

int main(int argc, char *argv[]) {
  if (argc < 2) { fprintf(stderr, "Usage: %s <video>\n", argv[0]); return 1; }

  // ── 1. Open file ─────────────────────────────────────────────────
  AVFormatContext *fmt = nullptr;
  if (avformat_open_input(&fmt, argv[1], nullptr, nullptr) < 0) {
    fprintf(stderr, "Cannot open: %s\n", argv[1]); return 1;
  }
  avformat_find_stream_info(fmt, nullptr);

  int vidx = -1;
  for (unsigned i = 0; i < fmt->nb_streams; i++) {
    if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      vidx = (int)i; break;
    }
  }
  if (vidx < 0) { fprintf(stderr, "No video stream\n"); return 1; }

  AVCodecParameters *par = fmt->streams[vidx]->codecpar;
  printf("Video: %dx%d codec=%d\n", par->width, par->height, par->codec_id);

  // ── 2. Open decoder ──────────────────────────────────────────────
  const AVCodec *codec = avcodec_find_decoder(par->codec_id);
  AVCodecContext *ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx, par);
  avcodec_open2(ctx, codec, nullptr);

  // Frame duration for pacing
  AVRational fr = fmt->streams[vidx]->r_frame_rate;
  double frameDuration = (fr.num > 0 && fr.den > 0)
      ? av_q2d(av_inv_q(fr))
      : 0.04;
  printf("Frame duration: %.3fs (%.1f fps)\n", frameDuration, 1.0/frameDuration);

  // ── 3. SDL window (main thread) ──────────────────────────────────
  SDL_Init(SDL_INIT_VIDEO);
  int w = par->width, h = par->height;
  SDL_Window *win = SDL_CreateWindow("minimal", SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_RESIZABLE);
  SDL_Renderer *rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  SDL_Texture *tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_IYUV,
      SDL_TEXTUREACCESS_STREAMING, w, h);
  printf("SDL: window=%p renderer=%p texture=%p\n", (void*)win, (void*)rend, (void*)tex);

  // ── 4. Decode + render loop (main thread) ────────────────────────
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  int running = 1, frameCount = 0;
  using namespace std::chrono;
  auto nextFrameTime = steady_clock::now();

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { if (ev.type == SDL_QUIT) running = 0; }

    int ret = av_read_frame(fmt, pkt);
    if (ret < 0) { printf("\nEOF after %d frames\n", frameCount); break; }
    if (pkt->stream_index != vidx) { av_packet_unref(pkt); continue; }

    avcodec_send_packet(ctx, pkt);
    av_packet_unref(pkt);

    ret = avcodec_receive_frame(ctx, frame);
    if (ret == AVERROR(EAGAIN)) continue;
    if (ret < 0) break;

    frameCount++;
    if (frameCount <= 3 || frameCount % 50 == 0) {
      printf("Frame %d: pts=%lld w=%d h=%d fmt=%d\n",
          frameCount, (long long)frame->pts,
          frame->width, frame->height, frame->format);
    }

    SDL_UpdateYUVTexture(tex, nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1],
        frame->data[2], frame->linesize[2]);

    SDL_RenderClear(rend);
    SDL_RenderCopy(rend, tex, nullptr, nullptr);
    SDL_RenderPresent(rend);

    // Frame pacing
    auto frameDurUs = microseconds(static_cast<long long>(frameDuration * 1'000'000.0));
    nextFrameTime += frameDurUs;
    auto now = steady_clock::now();
    if (nextFrameTime > now) {
      std::this_thread::sleep_for(nextFrameTime - now);
    } else {
      nextFrameTime = now;
    }
  }

  // ── 5. Cleanup ──────────────────────────────────────────────────
  av_frame_free(&frame);
  av_packet_free(&pkt);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(rend);
  SDL_DestroyWindow(win);
  SDL_Quit();
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  return 0;
}
