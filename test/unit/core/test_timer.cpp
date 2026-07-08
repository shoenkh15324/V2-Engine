#include <gtest/gtest.h>
#include "core/common/time/timer.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <set>
#include <chrono>

// Construction

TEST(Timer, Create){
    Timer t;
#if V2_PLATFORM_LINUX
    EXPECT_GE(t.fd(), 0);
#endif
}

TEST(Timer, CreateAndDestroy){
    Timer t;
    t.start();
    t.stop();
}

// Add / Cancel / Clear

TEST(Timer, AddSingle){
    Timer t;
    int id = t.add(100, false, [](int){});
    EXPECT_GT(id, 0);
}

TEST(Timer, AddMultiple){
    Timer t;
    std::set<int> ids;
    for(int i = 0; i < 10; i++){
        ids.insert(t.add(100, false, [](int){}));
    }
    EXPECT_EQ(ids.size(), 10);
}

TEST(Timer, CancelBeforeFire){
    Timer t;
    std::atomic<bool> fired{false};
    int id = t.add(10, false, [&](int){ fired = true; });
    t.cancel(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_FALSE(fired);
}

TEST(Timer, CancelInvalidId){
    Timer t;
    t.cancel(999);
}

TEST(Timer, Clear){
    Timer t;
    std::atomic<int> count{0};
    t.add(10, true, [&](int){ count++; });
    t.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_EQ(count, 0);
}

// Fire

TEST(Timer, FireOnce){
    Timer t;
    std::atomic<bool> fired{false};
    std::atomic<int> firedId{0};
    int id = t.add(10, false, [&](int tid){
        fired = true;
        firedId = tid;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_TRUE(fired);
    EXPECT_EQ(firedId, id);
}

TEST(Timer, FireMultiple){
    Timer t;
    std::atomic<int> count{0};
    t.add(10, false, [&](int){ count++; });
    t.add(10, false, [&](int){ count++; });
    t.add(10, false, [&](int){ count++; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_EQ(count, 3);
}

TEST(Timer, FireRepeating){
    Timer t;
    std::atomic<int> count{0};
    t.add(10, true, [&](int){ count++; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_EQ(count, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_EQ(count, 2);
}

TEST(Timer, FireOrder){
    Timer t;
    std::vector<int> order;
    t.add(30, false, [&](int){ order.push_back(3); });
    t.add(10, false, [&](int){ order.push_back(1); });
    t.add(20, false, [&](int){ order.push_back(2); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    t.handleTimerEvent();

    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// Edge Caeses

TEST(Timer, ZeroDelay){
    Timer t;
    std::atomic<bool> fired{false};
    t.add(0, false, [&](int){ fired = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.handleTimerEvent();
    EXPECT_TRUE(fired);
}

// Stop

TEST(Timer, StopPreventsFire){
    Timer t;
    std::atomic<bool> fired{false};
    t.add(10, false, [&](int){ fired = true; });
    t.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_FALSE(fired);
}
