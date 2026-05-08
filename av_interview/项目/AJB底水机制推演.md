
void CameraVideoQueue::QueueSampleGroup3(const sp<VideoSampleGroup> &sampleGroup) {
    /**
     * QueueSampleGroup3 设计目标：
     * 1) 不影响旧版 QueueSampleGroup/QueueSampleGroup2 的行为；
     * 2) 以“低延时优先”为实时预览策略；
     * 3) 用快升慢降(Asymmetric EMA)平滑抖动，避免首帧延时被长期继承；
     * 4) 在积压(backpressure)时主动追赶，而不是继续等待导致延时滚大。
     *
     * 时序模型说明：
     * - sampleTimeMs: 帧的媒体时间(PTS)；
     * - nowMs: 本地 monotonic 时间；
     * - queue3BasePtsMs_/queue3BaseMonoMs_: 一组基准点，用于将 PTS 映射到本地时间轴；
     * - expectedMonoMs: 当前帧“理论应渲染时刻”；
     * - observedDelayMs: now - expected，代表当前链路总延迟(编码/传输/解码/调度)。
     *
     * 关键策略：
     * - Fast-up: 观测延迟变大时快速抬高目标缓冲，优先稳定；
     * - Slow-down: 观测延迟变小时缓慢下调目标缓冲，逐步回收延时；
     * - Deadband: 小误差不调，避免目标值在小范围内抖动；
     * - Backpressure: 当队列有积压时，等待时间上限压到 2ms，优先追赶实时性；
     * - Drop late frame: 在积压且严重迟到时丢帧，防止“越播越慢”；
     * - Resync: 遇到 PTS 跳变或异常超早帧，重建时间轴基线。
     */
    {
        // 全局退出保护：若队列已取消，直接返回，避免无意义调度。
        std::unique_lock<std::mutex> lock(mutex_);
        if(quit_) {
            return;
        }
    }

    auto pending = ++pendingTask3_;
    if(pending > gQueueSize) {
        LOG(ERROR) << "camera video queue3 pending too much( " << pending << ")";
    }

    dispatch_.AddAsyncTask([=] {
        // pendingLeft 表示此任务开始执行时，队列里还积压了多少同类任务。
        // >0 时说明下游来不及，需进入追赶策略。
        auto pendingLeft = --pendingTask3_;
        double sampleTimeMs = sampleGroup->GetMediaTimeMs();
        double nowMs = videoClock_->GetCurrentTimeMs();
        double waitTimeMs = 0.0;
        bool dropFrame = false;

        {
            // queue3 的状态全部由独立互斥锁保护，确保该策略与旧流程解耦。
            std::unique_lock<std::mutex> lock(queue3Mutex_);
            if(!queue3Started_) {
                // 首帧初始化：建立 PTS->monotonic 的映射基准。
                queue3Started_ = true;
                queue3BasePtsMs_ = sampleTimeMs;
                queue3BaseMonoMs_ = nowMs;
                // 初始目标缓冲以 smoothDelta 为下限，首帧直接渲染，缩短起播首屏时间。
                queue3TargetBufferMs_ = std::max(0.0, smoothDeltaMs_);
                queue3LastPtsMs_ = sampleTimeMs;
                queue3WarmupFrames_ = 1;
                waitTimeMs = 0.0;
            } else {
                double ptsGapMs = sampleTimeMs - queue3LastPtsMs_;
                // 1.当帧时间不再连续，则重新建立时间基线
                if(ptsGapMs < -5.0 || ptsGapMs > 2000.0) {
                    // PTS 跳变（回退/大步进）通常意味着时间轴不再连续：立即重建基线。
                    queue3BasePtsMs_ = sampleTimeMs;
                    queue3BaseMonoMs_ = nowMs;
                    queue3TargetBufferMs_ = std::max(0.0, smoothDeltaMs_);
                    queue3WarmupFrames_ = 1;
                }

                // 2.理论mono渲染时间，将媒体时间映射到本地时间轴，得到该帧理论渲染时刻。
                double expectedMonoMs = queue3BaseMonoMs_ + (sampleTimeMs - queue3BasePtsMs_);
                // 观测延迟：用于驱动 AJB 的目标缓冲更新。
                double observedDelayMs = nowMs - expectedMonoMs;

                if(queue3WarmupFrames_ == 1 && queue3TargetBufferMs_ <= 0.0) {
                    // 启动窗口可用首批观测值“拉起”目标缓冲，避免初始化过小带来的连环迟到。
                    queue3TargetBufferMs_ = std::max(0.0, observedDelayMs);
                }

                constexpr double kDeadbandMs = 2.0;
                double errMs = observedDelayMs - queue3TargetBufferMs_;
                if(std::abs(errMs) > kDeadbandMs) {
                    if(errMs > 0.0) {
                        // Fast-up：抖动/延迟上升时快速跟随，降低卡顿风险。
                        queue3TargetBufferMs_ = 0.8 * observedDelayMs + 0.2 * queue3TargetBufferMs_;
                    } else {
                        // Slow-down：网络恢复时慢速回收延迟，避免目标值振荡。
                        // warmup 阶段略激进(0.2)以便更快摆脱首帧高延迟，稳定后切 0.1。
                        double alpha = queue3WarmupFrames_ < 10 ? 0.2 : 0.1;
                        queue3TargetBufferMs_ = alpha * observedDelayMs + (1.0 - alpha) * queue3TargetBufferMs_;
                    }
                }

                // 限幅：防止目标缓冲无限变大或过小导致频繁迟到。
                double minBufferMs = std::min(frameIntervalMs_, 10.0);
                double maxBufferMs = std::max(frameIntervalMs_ * 3.0, 120.0);
                queue3TargetBufferMs_ = std::max(minBufferMs, std::min(queue3TargetBufferMs_, maxBufferMs));

                // 最终截止时间 = 理论时刻 + 目标缓冲 - smoothDelta(人为前置一点点以降低体感延时)。
                double deadlineMs = expectedMonoMs + queue3TargetBufferMs_ - smoothDeltaMs_;
                waitTimeMs = deadlineMs - nowMs;

                if(pendingLeft > 0) {
                    // 背压追赶：有积压就减少等待，优先“追平实时”，而不是追求绝对平滑。
                    waitTimeMs = std::min(waitTimeMs, 2.0);
                }

                double dropThresholdMs = std::max(frameIntervalMs_ * 1.2, 40.0);
                if(waitTimeMs < -dropThresholdMs && pendingLeft > 0) {
                    // 严重迟到且有积压：丢掉该帧让系统快速回到低延时工作点。
                    dropFrame = true;
                }

                if(waitTimeMs > 250.0) {
                    // 超长等待通常是异常早帧或时间轴漂移：强制重建基线，避免“挂起式等待”。
                    queue3BasePtsMs_ = sampleTimeMs;
                    queue3BaseMonoMs_ = nowMs;
                    queue3TargetBufferMs_ = std::max(minBufferMs, smoothDeltaMs_);
                    waitTimeMs = 0.0;
                }

                // 更新上一帧状态，供下一帧做连续性判断。
                queue3LastPtsMs_ = sampleTimeMs;
                queue3WarmupFrames_++;
            }
        }

        if(dropFrame) {
            if(isDebug()) {
                LOG(INFO) << std::fixed << "queue3 drop late frame sampleTimeMs " << sampleTimeMs
                          << " pendingLeft " << pendingLeft;
            }
            return;
        }

        // renderAtOnce_ 打开时直接渲染；否则按策略等待到截止时间再渲染。
        if(!renderAtOnce_ && waitTimeMs > 0.0) {
            if(!Wait(waitTimeMs)) {
                return;
            }
        }
        RenderSample(sampleGroup);
    });
}