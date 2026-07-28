add_library(core OBJECT
  # queue
  queue/packet_queue.h
  queue/packet_queue.cpp
  queue/frame_queue.h
  queue/frame_queue.cpp
  # clock
  clock/clock.h
  clock/clock.cpp
  clock/clock_manager.h
  clock/clock_manager.cpp
  # event
  event/event_bus.h
  event/event_bus.cpp
  event/event_types.h
  # thread
  thread/thread_pool.h
  thread/thread_pool.cpp
  thread/thread_utils.h
  thread/thread_utils.cpp
  # memory
  memory/memory_pool.h
  memory/memory_pool.cpp
  memory/frame.h
  memory/frame.cpp
  # plugin
  plugin/plugin_manager.h
  plugin/plugin_manager.cpp
  plugin/i_plugin.h
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(core PUBLIC Threads::Threads)
