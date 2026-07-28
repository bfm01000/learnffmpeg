#include "filter_graph.h"

namespace player {

FilterGraph::FilterGraph() = default;

FilterGraph::~FilterGraph() {
    close();
}

int FilterGraph::init(const std::string& filters_desc, AVCodecContext* dec_ctx) {
    // TODO: Create AVFilterGraph, allocate buffer_src_ and buffer_sink_,
    //       parse filters_desc, configure inputs/outputs based on dec_ctx,
    //       and link the graph.
    return 0;
}

int FilterGraph::pushFrame(AVFrame* frame) {
    // TODO: Push frame into buffer_src_ via av_buffersrc_add_frame_flags().
    return 0;
}

AVFrame* FilterGraph::pullFrame() {
    // TODO: Pull a processed frame from buffer_sink_ via av_buffersink_get_frame().
    return nullptr;
}

void FilterGraph::close() {
    // TODO: Free graph_ with avfilter_graph_free(), nullify all pointers.
}

} // namespace player
