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
