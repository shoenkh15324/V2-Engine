#include <gtest/gtest.h>
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include <vector>
#include <thread>
#include <memory>
#include <atomic>

// ── Single-thread ──

TEST(MpscQueue, Create){
    LockFreeMpscQueue<int> mb(16);
    EXPECT_EQ(mb.capacity(), 16);
    EXPECT_EQ(mb.count(), 0);
    EXPECT_TRUE(mb.empty());
}

TEST(MpscQueue, CapacityOne){
    LockFreeMpscQueue<int> mb(1);
    EXPECT_EQ(mb.capacity(), 1);
    EXPECT_TRUE(mb.empty());
}

TEST(MpscQueue, PushPopOrder){
    LockFreeMpscQueue<int> mb(8);
    EXPECT_TRUE(mb.push(1));
    EXPECT_TRUE(mb.push(2));
    EXPECT_TRUE(mb.push(3));

    int val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_FALSE(mb.pop(val));
}

TEST(MpscQueue, PushReturnValue){
    LockFreeMpscQueue<int> mb(4);
    EXPECT_TRUE(mb.push(10));
    EXPECT_TRUE(mb.push(20));
    EXPECT_TRUE(mb.push(30));

    int val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(mb.push(40));
}

TEST(MpscQueue, PopUntilEmpty){
    LockFreeMpscQueue<int> mb(4);
    EXPECT_TRUE(mb.push(1));
    EXPECT_TRUE(mb.push(2));

    int val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_TRUE(mb.pop(val));
    EXPECT_FALSE(mb.pop(val));
    EXPECT_TRUE(mb.empty());
}

TEST(MpscQueue, PushToFull){
    LockFreeMpscQueue<int> mb(3);
    EXPECT_TRUE(mb.push(1));
    EXPECT_TRUE(mb.push(2));
    EXPECT_TRUE(mb.push(3));
    EXPECT_EQ(mb.count(), 3);
    EXPECT_FALSE(mb.push(4));
    EXPECT_EQ(mb.count(), 3);

    int val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 1);
}

TEST(MpscQueue, PushPopWrapAround){
    LockFreeMpscQueue<int> mb(4);
    EXPECT_TRUE(mb.push(1));
    EXPECT_TRUE(mb.push(2));
    EXPECT_TRUE(mb.push(3));
    EXPECT_TRUE(mb.push(4));

    int val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(mb.push(5));
    EXPECT_TRUE(mb.push(6));
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 4);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 5);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 6);
    EXPECT_TRUE(mb.empty());
}

TEST(MpscQueue, RepeatedPartialPushPop){
    LockFreeMpscQueue<int> mb(4);
    for(int i = 0; i < 100; i++){
        EXPECT_TRUE(mb.push(std::move(i)));
        int val;
        EXPECT_TRUE(mb.pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST(MpscQueue, PushPopUniquePtr){
    LockFreeMpscQueue<std::unique_ptr<int>> mb(4);
    EXPECT_TRUE(mb.push(std::make_unique<int>(42)));
    EXPECT_TRUE(mb.push(std::make_unique<int>(100)));

    std::unique_ptr<int> val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(*val, 42);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(*val, 100);
    EXPECT_FALSE(mb.pop(val));
}

// ── Multithreaded: SPSC ──

TEST(MpscQueueMT, SPSC){
    LockFreeMpscQueue<int> mb(8);
    constexpr int N = 1000;

    std::thread producer([&](){
        for(int i = 0; i < N; i++){
            while(!mb.push(std::move(i))){
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&](){
        int prev = -1;
        for(int i = 0; i < N; i++){
            int val;
            while(!mb.pop(val)){
                std::this_thread::yield();
            }
            EXPECT_EQ(val, prev + 1);
            prev = val;
        }
    });

    producer.join();
    consumer.join();
}

// ── Multithreaded: MPSC sum 검증 ──

TEST(MpscQueueMT, MPSCSum){
    LockFreeMpscQueue<int> mb(16);
    constexpr int PRODUCERS = 2;
    constexpr int PER_PRODUCER = 500;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;
    std::atomic<int> consumed{0};
    std::atomic<int> totalSum{0};
    std::vector<std::thread> producers;

    for(int p = 0; p < PRODUCERS; p++){
        producers.emplace_back([&, p](){
            for(int i = 0; i < PER_PRODUCER; i++){
                int val = p * PER_PRODUCER + i;
                while(!mb.push(std::move(val))){
                    std::this_thread::yield();
                }
            }
        });
    }

    std::thread consumer([&](){
        while(consumed < TOTAL){
            int val;
            if(mb.pop(val)){
                totalSum.fetch_add(val);
                consumed.fetch_add(1);
            }else{
                std::this_thread::yield();
            }
        }
    });

    for(auto& t : producers){
        t.join();
    }
    consumer.join();

    EXPECT_EQ(consumed, TOTAL);
    int expectedSum = TOTAL * (TOTAL - 1) / 2;
    EXPECT_EQ(totalSum.load(), expectedSum);
}
