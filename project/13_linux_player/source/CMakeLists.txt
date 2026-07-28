add_library(source OBJECT
  # ── 接口 ───────────────────────────────────────────
  protocol/i_protocol_handler.h
  protocol/protocol_factory.h
  protocol/protocol_factory.cpp
  demuxer/i_media_source.h
  demuxer/ffmpeg_demuxer.h
  demuxer/ffmpeg_demuxer.cpp
  demuxer/stream_info.h

  # ── File ───────────────────────────────────────────
  protocol/file_protocol/file_protocol.h
  protocol/file_protocol/file_protocol.cpp

  # ── HTTP ───────────────────────────────────────────
  protocol/http_protocol/http_protocol.h
  protocol/http_protocol/http_protocol.cpp

  # ── RTMP ───────────────────────────────────────────
  $<$<BOOL:${ENABLE_PLUGIN_RTMP}>:protocol/rtmp_protocol/rtmp_protocol.h>
  $<$<BOOL:${ENABLE_PLUGIN_RTMP}>:protocol/rtmp_protocol/rtmp_protocol.cpp>

  # ── RTSP ───────────────────────────────────────────
  $<$<BOOL:${ENABLE_PLUGIN_RTSP}>:protocol/rtsp_protocol/rtsp_protocol.h>
  $<$<BOOL:${ENABLE_PLUGIN_RTSP}>:protocol/rtsp_protocol/rtsp_protocol.cpp>

  # ── HLS ────────────────────────────────────────────
  $<$<BOOL:${ENABLE_PLUGIN_HLS}>:protocol/hls_protocol/hls_protocol.h>
  $<$<BOOL:${ENABLE_PLUGIN_HLS}>:protocol/hls_protocol/hls_protocol.cpp>
)

target_include_directories(source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(source PUBLIC core api ${FFMPEG_LIBRARIES})
