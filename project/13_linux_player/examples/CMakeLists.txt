# ── Simple Player (OpenGL window) ────────────────────────────────────────────
add_executable(simple_player
  simple_player/main.cpp
)
target_link_libraries(simple_player PRIVATE player_sdk)

# ── Offscreen Player (no window, frame callback) ─────────────────────────────
add_executable(offscreen_player
  offscreen_player/main.cpp
)
target_link_libraries(offscreen_player PRIVATE player_sdk)

# ── OpenGL Demo (raw GL rendering with shader effects) ───────────────────────
add_executable(opengl_player
  opengl_player/main.cpp
)
target_link_libraries(opengl_player PRIVATE player_sdk)

add_executable(audio_player
  audio_player/main.cpp
)
target_link_libraries(audio_player PRIVATE player_sdk)

add_executable(full_player
  full_player/main.cpp
)
target_link_libraries(full_player PRIVATE player_sdk)

add_executable(minimal_sdl
  minimal_sdl/main.cpp
)
target_link_libraries(minimal_sdl PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES} Threads::Threads)
target_include_directories(minimal_sdl PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})

add_executable(minimal_audio
  minimal_audio/main.cpp
)
target_link_libraries(minimal_audio PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES} Threads::Threads)
target_include_directories(minimal_audio PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})

add_executable(minimal_av
  minimal_av/main.cpp
)
target_link_libraries(minimal_av PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES} Threads::Threads)
target_include_directories(minimal_av PRIVATE ${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
