#pragma once

#include "api/player.h"
#include "api/player_config.h"
#include "api/player_types.h"
#include "control/state_machine/state_machine.h"
#include "control/av_sync_engine/av_sync_engine.h"
#include "control/seek_handler/seek_handler.h"
#include "control/playlist_manager/playlist_manager.h"
#include "core/clock/clock_manager.h"
#include "core/event/event_bus.h"
#include "core/queue/packet_queue.h"
#include "core/queue/frame_queue.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward declarations for modules not yet created
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;

namespace player {

/// @brief 媒体源接口
class IMediaSource {
public:
    virtual ~IMediaSource() = default;
    // TODO: Define open/close/read/seek interface
};

/// @brief 视频解码器接口
class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;
    // TODO: Define decode/flush/drain interface
};

/// @brief 音频解码器接口
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;
    // TODO: Define decode/flush/drain interface
};

/// @brief OpenGL 视频渲染器
class OpenGLRenderer {
public:
    virtual ~OpenGLRenderer() = default;
    // TODO: Define render/swap/present interface
};

/// @brief SDL2 音频渲染器
class SDL2AudioRenderer {
public:
    virtual ~SDL2AudioRenderer() = default;
    // TODO: Define play/pause/render interface
};

/// @brief 中央播放器控制器 —— 使用 Facade 模式封装所有子模块
///
/// PlayerController 是 IPlayer 的具体实现，协调以下模块工作：
///   - Demux / Decode / Render 流水线
///   - 状态机、同步、Seek、播放列表
///   - 四组线程：解复用、视频解码、音频解码、视频渲染
class PlayerController : public IPlayer {
public:
    PlayerController();
    explicit PlayerController(const PlayerConfig& config);
    ~PlayerController() override;

    // ── IPlayer 接口（线程安全） ──────────────────────────────────────────

    int  open(const char* url) override;
    int  open(const char* url, const PlayerConfig& config) override;

    int  play()  override;
    int  pause() override;
    int  stop()  override;

    int  seek(int64_t position_ms) override;

    int  setSpeed(double speed) override;
    int  setVolume(float volume) override;
    int  setLoop(bool loop) override;

    PlayerState getState()    const override;
    int64_t     getPosition() const override;
    int64_t     getDuration() const override;
    bool        isPlaying()   const override;
    bool        isSeeking()   const override;

    void setCallback(IPlayerCallback* callback) override;

    // ── 线程工作函数 ──────────────────────────────────────────────────────

    /// @brief 解复用线程：av_read_frame -> 路由到对应 PacketQueue
    void runDemuxLoop();

    /// @brief 视频解码线程：读取视频 PacketQueue -> 解码 -> 推入 FrameQueue
    void runVideoDecodeLoop();

    /// @brief 音频解码线程：读取音频 PacketQueue -> 解码 -> 推入 FrameQueue
    void runAudioDecodeLoop();

    /// @brief 视频渲染线程：从 FrameQueue 取帧 -> 音视频同步 -> 渲染 -> Swap
    void runVideoRenderLoop();

    // ── Seek 完成处理 ─────────────────────────────────────────────────────

    /// @brief Seek 完成后重置解码器状态并继续播放
    void handleSeekComplete();

private:
    // ── 初始化 ────────────────────────────────────────────────────────────

    /// @brief 初始化播放器流水线
    int init(const PlayerConfig& config);

    /// @brief 创建各子模块实例
    int createPipeline(const PlayerConfig& config);

    /// @brief 启动所有线程
    void startThreads();

    /// @brief 停止所有线程
    void stopThreads();

    /// @brief 清理所有资源
    void teardown();

    // ── 内部辅助 ──────────────────────────────────────────────────────────

    /// @brief 发射事件到回调
    void notifyEvent(const PlayerEvent& event);

    /// @brief 更新状态机
    bool changeState(EventType event);

    // ── 子模块（唯一所有权） ──────────────────────────────────────────────

    std::unique_ptr<IMediaSource>      source_;
    std::unique_ptr<VideoDecoder>      video_decoder_;
    std::unique_ptr<AudioDecoder>      audio_decoder_;
    std::unique_ptr<OpenGLRenderer>    video_renderer_;
    std::unique_ptr<SDL2AudioRenderer> audio_renderer_;

    // ── 控制模块 ──────────────────────────────────────────────────────────

    StateMachine    state_machine_;
    ClockManager    clock_manager_;
    AVSyncEngine    av_sync_engine_;
    SeekHandler     seek_handler_;
    PlaylistManager playlist_manager_;
    EventBus        event_bus_;

    // ── 配置 ──────────────────────────────────────────────────────────────

    PlayerConfig            config_;
    IPlayerCallback*        callback_;   // 回调接口（外部拥有）
    mutable std::mutex      callback_mutex_;

    // ── 队列 ──────────────────────────────────────────────────────────────

    using PacketQueueType = PacketQueue<std::shared_ptr<struct AVPacket>>;
    using FrameQueueType  = FrameQueue<std::shared_ptr<Frame>>;

    std::shared_ptr<PacketQueueType> video_pkt_queue_;
    std::shared_ptr<PacketQueueType> audio_pkt_queue_;
    std::shared_ptr<FrameQueueType>  video_frm_queue_;
    std::shared_ptr<FrameQueueType>  audio_frm_queue_;

    // ── FFmpeg 上下文 ─────────────────────────────────────────────────────

    AVFormatContext* fmt_ctx_;
    AVCodecContext*  video_codec_ctx_;
    AVCodecContext*  audio_codec_ctx_;
    int              video_stream_idx_;
    int              audio_stream_idx_;

    // ── 线程 ──────────────────────────────────────────────────────────────

    std::unique_ptr<std::thread> demux_thread_;
    std::unique_ptr<std::thread> video_decode_thread_;
    std::unique_ptr<std::thread> audio_decode_thread_;
    std::unique_ptr<std::thread> video_render_thread_;

    std::atomic<bool> abort_requested_;

    // ── 流信息 ────────────────────────────────────────────────────────────

    int64_t duration_ms_;
};

} // namespace player
