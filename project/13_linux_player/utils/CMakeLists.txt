add_library(utils OBJECT
  # ── Logger ─────────────────────────────────────────
  logger/logger.h
  logger/logger.cpp

  # ── Config ─────────────────────────────────────────
  config/config_manager.h
  config/config_manager.cpp

  # ── Perf Probe ─────────────────────────────────────
  perf_probe/perf_probe.h
  perf_probe/perf_probe.cpp
)

target_include_directories(utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(utils PUBLIC core)
