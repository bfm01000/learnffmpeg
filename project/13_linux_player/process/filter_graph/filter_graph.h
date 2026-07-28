#pragma once

#include <memory>
#include <string>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace player {

class FilterGraph {
public:
    FilterGraph();
    ~FilterGraph();

    FilterGraph(const FilterGraph&) = delete;
    FilterGraph& operator=(const FilterGraph&) = delete;
    FilterGraph(FilterGraph&&) = delete;
    FilterGraph& operator=(FilterGraph&&) = delete;

    /// @brief Initialize the filter graph with a filter description and codec context.
    /// @param filters_desc  Filter graph description string (e.g. "scale=iw:ih").
    /// @param dec_ctx       AVCodecContext providing stream parameters.
    /// @return 0 on success, negative AVERROR on failure.
    int init(const std::string& filters_desc, AVCodecContext* dec_ctx);

    /// @brief Push a decoded frame into the filter graph.
    /// @param frame  The input AVFrame to process.
    /// @return 0 on success, negative AVERROR on failure.
    int pushFrame(AVFrame* frame);

    /// @brief Pull a processed frame from the filter graph.
    /// @return A reference-counted AVFrame pointer, or nullptr on error / EAGAIN.
    AVFrame* pullFrame();

    /// @brief Close and release all filter graph resources.
    void close();

private:
    AVFilterGraph*      graph_{nullptr};
    AVFilterContext*    buffer_src_{nullptr};
    AVFilterContext*    buffer_sink_{nullptr};
};

} // namespace player
