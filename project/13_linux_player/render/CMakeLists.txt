add_library(render OBJECT
  # ── 接口 ───────────────────────────────────────────
  i_renderer.h

  # ── SDL2 Audio ─────────────────────────────────────
  audio/sdl2_renderer/sdl2_audio_renderer.h
  audio/sdl2_renderer/sdl2_audio_renderer.cpp
  audio/sdl2_renderer/audio_ring_buffer.h
  audio/sdl2_renderer/audio_ring_buffer.cpp

  # ── Subtitle Overlay ───────────────────────────────
  subtitle/overlay_renderer/subtitle_renderer.h
  subtitle/overlay_renderer/subtitle_renderer.cpp
)

# OpenGL renderer — only if GLFW is available
if(GLFW_FOUND)
  target_sources(render PRIVATE
    video/opengl_renderer/opengl_renderer.h
    video/opengl_renderer/opengl_renderer.cpp
    video/opengl_renderer/gl_context.h
    video/opengl_renderer/gl_context.cpp
    video/opengl_renderer/shader_program.h
    video/opengl_renderer/shader_program.cpp
    video/opengl_renderer/texture_manager.h
    video/opengl_renderer/texture_manager.cpp
  )
endif()

target_include_directories(render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(render PUBLIC core api ${SDL2_LIBRARIES})

if(GLFW_FOUND)
  target_link_libraries(render PUBLIC OpenGL::GL ${GLFW_LIBRARIES})
endif()
