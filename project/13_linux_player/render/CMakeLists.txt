add_library(render OBJECT
  i_renderer.h

  # SDL2 Audio
  audio/sdl2_renderer/sdl2_audio_renderer.h
  audio/sdl2_renderer/sdl2_audio_renderer.cpp
  audio/sdl2_renderer/audio_ring_buffer.h
  audio/sdl2_renderer/audio_ring_buffer.cpp

  # SDL2 Video
  video/opengl_renderer/sdl_video_renderer.h
  video/opengl_renderer/sdl_video_renderer.cpp

  # Subtitle
  subtitle/overlay_renderer/subtitle_renderer.h
  subtitle/overlay_renderer/subtitle_renderer.cpp
)

target_include_directories(render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(render PUBLIC core api ${SDL2_LIBRARIES})
