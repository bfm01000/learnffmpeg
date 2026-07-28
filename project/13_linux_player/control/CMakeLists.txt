add_library(control OBJECT
  # ── Player Controller ──────────────────────────────
  player_controller/player_controller.h
  player_controller/player_controller.cpp

  # ── State Machine ──────────────────────────────────
  state_machine/state_machine.h
  state_machine/state_machine.cpp

  # ── AV Sync ────────────────────────────────────────
  av_sync_engine/av_sync_engine.h
  av_sync_engine/av_sync_engine.cpp

  # ── Seek Handler ───────────────────────────────────
  seek_handler/seek_handler.h
  seek_handler/seek_handler.cpp

  # ── Playlist Manager ───────────────────────────────
  playlist_manager/playlist_manager.h
  playlist_manager/playlist_manager.cpp
)

target_include_directories(control PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(control PUBLIC core source decode process render api)
