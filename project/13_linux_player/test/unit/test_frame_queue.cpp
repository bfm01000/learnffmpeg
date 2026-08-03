#include "core/queue/frame_queue.h"
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace player {
namespace test {

using IntQueue = FrameQueue<std::shared_ptr<int>>;

TEST(FrameQueueTest, PushAndPeek)
{
    IntQueue q(3);
    auto a = std::make_shared<int>(10);
    auto b = std::make_shared<int>(20);

    EXPECT_TRUE(q.pushFrame(a));
    EXPECT_TRUE(q.pushFrame(b));
    EXPECT_EQ(q.size(), 2);

    std::shared_ptr<int> out;
    EXPECT_TRUE(q.peekFrame(out));
    EXPECT_EQ(*out, 10);
}

TEST(FrameQueueTest, NextFrameAdvances)
{
    IntQueue q(3);
    q.pushFrame(std::make_shared<int>(10));
    q.pushFrame(std::make_shared<int>(20));

    std::shared_ptr<int> out;
    q.peekFrame(out);
    EXPECT_EQ(*out, 10);
    q.nextFrame();

    EXPECT_TRUE(q.peekFrame(out));
    EXPECT_EQ(*out, 20);
    EXPECT_EQ(q.numRemaining(), 1);
}

TEST(FrameQueueTest, CapacityBlocksPush)
{
    IntQueue q(2);
    EXPECT_TRUE(q.pushFrame(std::make_shared<int>(1)));
    EXPECT_TRUE(q.pushFrame(std::make_shared<int>(2)));
    EXPECT_FALSE(q.pushFrame(std::make_shared<int>(3))); // full
    EXPECT_EQ(q.size(), 2);
}

TEST(FrameQueueTest, NextFrameFreesCapacity)
{
    IntQueue q(2);
    q.pushFrame(std::make_shared<int>(1));
    q.pushFrame(std::make_shared<int>(2));

    std::shared_ptr<int> out;
    q.peekFrame(out);   // peek frame 1
    q.nextFrame();      // consume frame 1 — shrink triggers (readIndex > 0)

    // After shrink, queue should have 1 frame and accept new pushes
    EXPECT_TRUE(q.pushFrame(std::make_shared<int>(3)));
    EXPECT_EQ(q.size(), 2); // frame 2 + frame 3
}

TEST(FrameQueueTest, PrevFrame)
{
    // With immediate shrink per frame, prev only works within same batch
    IntQueue q(5);
    q.pushFrame(std::make_shared<int>(10));
    q.pushFrame(std::make_shared<int>(20));

    // Don't advance. Peek frame 10 first
    std::shared_ptr<int> out;
    q.peekFrame(out);
    EXPECT_EQ(*out, 10);
    // At start, prevFrame does nothing
    EXPECT_FALSE(q.prevFrame());
    q.peekFrame(out);
    EXPECT_EQ(*out, 10); // still at 10
}

TEST(FrameQueueTest, FlushClearsAll)
{
    IntQueue q(3);
    q.pushFrame(std::make_shared<int>(1));
    q.pushFrame(std::make_shared<int>(2));
    q.flush();

    EXPECT_EQ(q.size(), 0);
    EXPECT_TRUE(q.empty());
    std::shared_ptr<int> out;
    EXPECT_FALSE(q.peekFrame(out));
}

TEST(FrameQueueTest, ShrinkAfterEveryNext)
{
    // Verify that shrink fires after every nextFrame() call (readIndex > 0),
    // not just when readIndex > capacity/2.
    IntQueue q(5);
    for (int i = 0; i < 5; ++i) q.pushFrame(std::make_shared<int>(i));

    std::shared_ptr<int> out;
    q.peekFrame(out); q.nextFrame(); // consume frame 0
    EXPECT_EQ(q.size(), 4);           // should have shrunk: only 4 remaining
    EXPECT_TRUE(q.pushFrame(std::make_shared<int>(99))); // should have space
    EXPECT_EQ(q.size(), 5);
}

TEST(FrameQueueTest, MultiThreadPushPop)
{
    // Verify producer-consumer pattern works with blocking push
    IntQueue q(3);
    std::atomic<bool> producerDone{false};
    std::atomic<int> sum{0};

    // Producer: fills queue then blocks, pushes 10 items total
    std::thread producer([&]() {
        for (int i = 1; i <= 10; ++i) {
            while (!q.pushFrame(std::make_shared<int>(i))) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        producerDone.store(true);
    });

    // Consumer: peeks and consumes at slower pace
    std::thread consumer([&]() {
        int count = 0;
        while (count < 10) {
            std::shared_ptr<int> out;
            if (q.peekFrame(out)) {
                sum.fetch_add(*out);
                q.nextFrame();
                ++count;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    producer.join();
    consumer.join();

    EXPECT_TRUE(producerDone.load());
    EXPECT_EQ(sum.load(), 55); // 1+2+...+10 = 55
}

} // namespace test
} // namespace player
