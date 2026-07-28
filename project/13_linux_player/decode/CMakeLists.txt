add_library(decode OBJECT
  # ── 接口 ───────────────────────────────────────────
  i_decoder.h
  decode_utils.h
  decode_utils.cpp

  # ── Video ──────────────────────────────────────────
  video_decoder/video_decoder.h
  video_decoder/video_decoder.cpp
  video_decoder/hw_accel.h
  video_decoder/hw_accel_vaapi.cpp
  video_decoder/hw_accel_vdpau.cpp
  video_decoder/hw_accel_cuda.cpp

  # ── Audio ──────────────────────────────────────────
  audio_decoder/audio_decoder.h
  audio_decoder/audio_decoder.cpp

  # ── Subtitle ───────────────────────────────────────
  subtitle_decoder/subtitle_decoder.h
  subtitle_decoder/subtitle_decoder.cpp
)

target_include_directories(decode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(decode PUBLIC core api ${FFMPEG_LIBRARIES})

# HWAccel compile definitions
if(ENABLE_HWACCEL_VAAPI)
  target_compile_definitions(decode PUBLIC PLAYER_HWACCEL_VAAPI=1)
endif()
if(ENABLE_HWACCEL_VDPAU)
  target_compile_definitions(decode PUBLIC PLAYER_HWACCEL_VDPAU=1)
endif()
if(ENABLE_HWACCEL_CUDA)
  target_compile_definitions(decode PUBLIC PLAYER_HWACCEL_CUDA=1)
endif()
