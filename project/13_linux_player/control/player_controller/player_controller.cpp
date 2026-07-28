#include "player_controller.h"

#include "core/event/event_types.h"
#include "core/thread/thread_utils.h"

namespace player {

// ══════════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ══════════════════════════════════════════════════════════════════════════════

PlayerController::PlayerController()
    : av_sync_engine_(clock_manager_)
    , seek_handler_(SeekHandler::Dependencies{})
    , callback_(nullptr)
    , fmt_ctx_(nullptr)
    , video_codec_ctx_(nullptr)
    , audio_codec_ctx_(nullptr)
    , video_stream_idx_(-1)
    , audio_stream_idx_(-1)
    , abort_requested_(false)
    , duration_ms_(0)
{
    // TODO: Register state machine transitions
    // state_machine_.registerTransition({EventType::..., PlayerState::Idle, PlayerState::Loading, ...});
    //
    // TODO: Initialize seek handler dependencies
    // seek_handler_ = SeekHandler({...});
}

PlayerController::PlayerController(const PlayerConfig& config)
    : PlayerController()
{
    config_ = config;
}

PlayerController::~PlayerController()
{
    stop();
    teardown();
}

// ══════════════════════════════════════════════════════════════════════════════
// IPlayer — Lifecycle
// ══════════════════════════════════════════════════════════════════════════════

int PlayerController::open(const char* url)
{
    // TODO: Implement full pipeline initialization
    //   1. Call init(config_) if not already initialized
    //   2. Transit state to Loading
    //   3. Open the media source (source_->open(url))
    //   4. Find best video/audio streams
    //   5. Create decoders for each stream
    //   6. Start demux thread
    //   7. Transit to Ready state
    return -1;
}

int PlayerController::open(const char* url, const PlayerConfig& config)
{
    config_ = config;
    return open(url);
}

int PlayerController::play()
{
    // TODO:
    //   1. Check state is Ready or Paused
    //   2. Resume audio renderer
    //   3. Start video render thread if not running
    //   4. Transit to Playing state
    //   5. Notify callback onPlay()
    return -1;
}

int PlayerController::pause()
{
    // TODO:
    //   1. Check state is Playing
    //   2. Pause clock
    //   3. Pause audio renderer
    //   4. Transit to Paused state
    //   5. Notify callback onPause()
    return -1;
}

int PlayerController::stop()
{
    // TODO:
    //   1. Abort all queues
    //   2. Stop all threads
    //   3. Reset decoders and renderers
    //   4. Close media source
    //   5. Transit to Idle state
    //   6. Notify callback onStopped()
    return -1;
}

int PlayerController::seek(int64_t position_ms)
{
    // TODO:
    //   1. Delegate to seek_handler_.seekTo(position_ms)
    //   2. Handle seek complete (re-establish state)
    //   3. Notify callback onSeekComplete()
    return -1;
}

int PlayerController::setSpeed(double speed)
{
    // TODO:
    //   1. Clamp speed to valid range (0.5x ~ 2.0x)
    //   2. Update AVSyncEngine sync params
    //   3. Update clock speed
    //   4. Notify callback
    return -1;
}

int PlayerController::setVolume(float volume)
{
    // TODO:
    //   1. Clamp volume to [0.0, 1.0]
    //   2. Forward to audio renderer
    return -1;
}

int PlayerController::setLoop(bool loop)
{
    playlist_manager_.setLoop(loop ? LoopMode::LoopAll : LoopMode::NoLoop);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// IPlayer — Query
// ══════════════════════════════════════════════════════════════════════════════

PlayerState PlayerController::getState() const
{
    return state_machine_.getState();
}

int64_t PlayerController::getPosition() const
{
    // TODO: Return current master clock position converted to milliseconds
    return 0;
}

int64_t PlayerController::getDuration() const
{
    return duration_ms_;
}

bool PlayerController::isPlaying() const
{
    return state_machine_.getState() == PlayerState::Playing;
}

bool PlayerController::isSeeking() const
{
    return seek_handler_.isSeeking();
}

void PlayerController::setCallback(IPlayerCallback* callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = callback;
}

// ══════════════════════════════════════════════════════════════════════════════
// Thread Loops
// ══════════════════════════════════════════════════════════════════════════════

void PlayerController::runDemuxLoop()
{
    setThreadName("demux_loop");

    // TODO: Implement demux loop
    //   while (!abort_requested_) {
    //       AVPacket pkt;
    //       int ret = av_read_frame(fmt_ctx_, &pkt);
    //       if (ret < 0) { handle_eos_or_error(); break; }
    //       Route to video_pkt_queue_ or audio_pkt_queue_ based on stream index
    //   }
}

void PlayerController::runVideoDecodeLoop()
{
    setThreadName("vdec_loop");

    // TODO: Implement video decode loop
    //   while (!abort_requested_) {
    //       Pop from video_pkt_queue_
    //       avcodec_send_packet / avcodec_receive_frame
    //       Push decoded Frame into video_frm_queue_
    //   }
}

void PlayerController::runAudioDecodeLoop()
{
    setThreadName("adec_loop");

    // TODO: Implement audio decode loop
    //   while (!abort_requested_) {
    //       Pop from audio_pkt_queue_
    //       avcodec_send_packet / avcodec_receive_frame
    //       Push decoded Frame into audio_frm_queue_
    //   }
}

void PlayerController::runVideoRenderLoop()
{
    setThreadName("vrender_loop");

    // TODO: Implement video render loop
    //   while (!abort_requested_) {
    //       Peek frame from video_frm_queue_
    //       Call av_sync_engine_.syncVideo(frame) -> decide Render/Sleep/Drop
    //       If Render: video_renderer_->render(frame)
    //       Advance frame queue
    //   }
}

void PlayerController::handleSeekComplete()
{
    // TODO:
    //   1. Reset decoder serials
    //   2. Restart decode loops
    //   3. Restore Playing or Paused state
    //   4. Notify callback onSeekComplete()
}

// ══════════════════════════════════════════════════════════════════════════════
// Internal
// ══════════════════════════════════════════════════════════════════════════════

int PlayerController::init(const PlayerConfig& config)
{
    // TODO: Perform one-time initialization:
    //   - Validate config
    //   - Initialize SDL2 (if needed)
    //   - Initialize OpenGL context (if needed)
    //   - Create packet/frame queues with configured sizes
    //   - Register default state machine transitions
    return 0;
}

int PlayerController::createPipeline(const PlayerConfig& config)
{
    // TODO:
    //   source_ = std::make_unique<IMediaSource>(...);
    //   video_decoder_ = std::make_unique<VideoDecoder>(...);
    //   audio_decoder_ = std::make_unique<AudioDecoder>(...);
    //   video_renderer_ = std::make_unique<OpenGLRenderer>(...);
    //   audio_renderer_ = std::make_unique<SDL2AudioRenderer>(...);
    return 0;
}

void PlayerController::startThreads()
{
    // TODO: Create and start all processing threads
    // demux_thread_ = std::make_unique<std::thread>(&PlayerController::runDemuxLoop, this);
    // video_decode_thread_ = std::make_unique<std::thread>(&PlayerController::runVideoDecodeLoop, this);
    // audio_decode_thread_ = std::make_unique<std::thread>(&PlayerController::runAudioDecodeLoop, this);
    // video_render_thread_ = std::make_unique<std::thread>(&PlayerController::runVideoRenderLoop, this);
}

void PlayerController::stopThreads()
{
    abort_requested_.store(true);

    // Abort queues to unblock waiting threads
    if (video_pkt_queue_) video_pkt_queue_->abort();
    if (audio_pkt_queue_) audio_pkt_queue_->abort();

    // TODO: Join all threads
    // if (demux_thread_ && demux_thread_->joinable()) demux_thread_->join();
    // if (video_decode_thread_ && video_decode_thread_->joinable()) video_decode_thread_->join();
    // if (audio_decode_thread_ && audio_decode_thread_->joinable()) audio_decode_thread_->join();
    // if (video_render_thread_ && video_render_thread_->joinable()) video_render_thread_->join();
}

void PlayerController::teardown()
{
    // TODO: Release all resources
    //   - avformat_close_input(&fmt_ctx_)
    //   - avcodec_free_context(&video_codec_ctx_)
    //   - avcodec_free_context(&audio_codec_ctx_)
    //   - Reset all unique_ptrs
}

void PlayerController::notifyEvent(const PlayerEvent& event)
{
    event_bus_.emit(event);
    event_bus_.dispatch();
}

bool PlayerController::changeState(EventType event)
{
    return state_machine_.transit(event);
}

} // namespace player
