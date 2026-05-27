include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(my_jitter_buffer_tests
    packet_buffer_test.cc
    jitter_estimator_test.cc
    jitter_buffer_test.cc
)
target_link_libraries(my_jitter_buffer_tests
    PRIVATE
        my_jitter_buffer
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(my_jitter_buffer_tests)
