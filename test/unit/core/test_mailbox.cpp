#include <gtest/gtest.h>
#include "core/actor_system/runtime/mailbox.hpp"
#include <vector>
#include <thread>
#include <memory>
#include <atomic>

// Construction

TEST(Mailbox, Create){
    Mailbox<int> mb(16);
    EXPECT_EQ(mb.capacity(), 16);
    EXPECT_EQ(mb.count(), 0);
    EXPECT_TRUE(mb.empty());
}

TEST(Mailbox, CapacityOne){
    Mailbox<int> mb(1);
    EXPECT_EQ(mb.capacity(), 1);
    EXPECT_TRUE(mb.empty());
}

// Basic Push / Pop

TEST(Mailbox, PushPopOrder){
    Mailbox<int> mb(8);
    EXPECT_TRUE(mb.push(1));
    EXPECT_TRUE(mb.push(2));
    mb.push(3);

    int val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_FALSE(mb.pop(val));
}

TEST(Mailbox, PushReturnValue){
    Mailbox<int> mb(4);
    EXPECT_TRUE(mb.push(10));
    EXPECT_TRUE(mb.push(20));
    EXPECT_TRUE(mb.push(30));

    int val;
    mb.pop(val);
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(mb.push(40));
}

TEST(Mailbox, PopUntilEmpty){
    Mailbox<int> mb(4);
    mb.push(1);
    mb.push(2);

    int val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_TRUE(mb.pop(val));
    EXPECT_FALSE(mb.pop(val));
    EXPECT_TRUE(mb.empty());
}

// Full / Overflow

TEST(Mailbox, PushToFull){
    Mailbox<int> mb(3);
    mb.push(1);
    mb.push(2);
    mb.push(3);
    EXPECT_EQ(mb.count(), 3);
    EXPECT_FALSE(mb.push(4)); // full -> false
    EXPECT_EQ(mb.count(), 3); // count unchanged

    int val;
    mb.pop(val);
    EXPECT_EQ(val, 1);
}

// Wrap-Around

TEST(Mailbox, PushPopWrapAround){
    Mailbox<int> mb(4);
    mb.push(1);
    mb.push(2);
    mb.push(3);
    mb.push(4); // head=0, tail=0, full

    int val;
    mb.pop(val); // pop 1 → head=1
    mb.pop(val); // pop 2 → head=2
    mb.push(5); // tail=0 (wrap)
    mb.push(6); // tail=1
    mb.pop(val); // 3
    EXPECT_EQ(val, 3);
    mb.pop(val); // 4
    EXPECT_EQ(val, 4);
    mb.pop(val); // 5
    EXPECT_EQ(val, 5);
    mb.pop(val); // 6
    EXPECT_EQ(val, 6);
    EXPECT_TRUE(mb.empty());
}

TEST(Mailbox, RepeatedPartialPushPop){
    Mailbox<int> mb(4);
    for(int i = 0; i < 100; i++){
        EXPECT_TRUE(mb.push(i));
        int val;
        EXPECT_TRUE(mb.pop(val));
        EXPECT_EQ(val, i);
    }
}

// Move-Only Types

TEST(Mailbox, PushPopUniquePtr){
    Mailbox<std::unique_ptr<int>> mb(4);
    mb.push(std::make_unique<int>(42));
    mb.push(std::make_unique<int>(100));

    std::unique_ptr<int> val;
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(*val, 42);
    EXPECT_TRUE(mb.pop(val));
    EXPECT_EQ(*val, 100);
    EXPECT_FALSE(mb.pop(val));
}

// Multithreaded

TEST(Mailbox, SingleProducerSingleConsumer){
    Mailbox<int> mb(8);
    constexpr int N = 1000;

    std::thread producer([&](){
        for(int i = 0; i < N; i++){
            while(!mb.push(i)){
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

TEST(Mailbox, MultipleProducersConsumers){
    Mailbox<int> mb(16);
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
                while(!mb.push(val)){
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
