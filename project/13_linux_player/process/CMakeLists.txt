add_library(process OBJECT
  # ── Filter Graph ───────────────────────────────────
  filter_graph/filter_graph.h
  filter_graph/filter_graph.cpp

  # ── Resampler ──────────────────────────────────────
  resampler/audio_resampler.h
  resampler/audio_resampler.cpp

  # ── Color Converter ────────────────────────────────
  color_converter/color_converter.h
  color_converter/color_converter.cpp
)

target_include_directories(process PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(process PUBLIC core api ${FFMPEG_LIBRARIES})
