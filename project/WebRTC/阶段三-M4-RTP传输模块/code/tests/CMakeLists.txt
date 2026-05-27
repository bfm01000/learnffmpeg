include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(my_rtp_module_tests
    rtp_packet_test.cc
    h264_packetizer_test.cc
    h264_depacketizer_test.cc
)
target_link_libraries(my_rtp_module_tests
    PRIVATE
        my_rtp_module
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(my_rtp_module_tests)
