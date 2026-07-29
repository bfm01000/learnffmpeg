# ── GoogleTest ────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# ── Common test library ──────────────────────────────────────────────────────
add_library(test_common STATIC
  mock/mock_demuxer.h
  mock/mock_demuxer.cpp
  mock/mock_decoder.h
  mock/mock_decoder.cpp
  mock/mock_renderer.h
  mock/mock_renderer.cpp
)
target_link_libraries(test_common PUBLIC player_sdk gtest gmock)

# ── Unit tests ───────────────────────────────────────────────────────────────
function(add_unit_test name)
  add_executable(${name} unit/${name}.cpp)
  target_link_libraries(${name} PRIVATE test_common gtest_main)
  gtest_discover_tests(${name} TEST_PREFIX "unit/")
endfunction()

add_unit_test(test_packet_queue)
add_unit_test(test_frame_queue)
add_unit_test(test_file_protocol)
add_unit_test(test_protocol_factory)
add_unit_test(test_clock)
add_unit_test(test_event_bus)
add_unit_test(test_state_machine)
add_unit_test(test_av_sync)
add_unit_test(test_demuxer)
add_unit_test(test_video_decoder)
add_unit_test(test_audio_decoder)
add_unit_test(test_seek_handler)

# ── Integration tests ────────────────────────────────────────────────────────
function(add_integration_test name)
  add_executable(${name} integration/${name}.cpp)
  target_link_libraries(${name} PRIVATE test_common gtest_main)
  gtest_discover_tests(${name} TEST_PREFIX "integration/")
endfunction()

add_integration_test(test_full_pipeline)
add_integration_test(test_playback_local_mp4)
add_integration_test(test_playback_network_http)
add_integration_test(test_seek_accuracy)
add_integration_test(test_audio_video_sync)
