#include <gtest/gtest.h>
#include "core/common/timer/timer.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <set>
#include <chrono>

struct CtxBool{ std::atomic<bool>* v; };
struct CtxCount{ std::atomic<int>* v; };
struct CtxFireOnce{ std::atomic<bool>* fired; std::atomic<int>* id; };
struct CtxOrder{ std::vector<int>* order; int val; };

static void noOp(int, void*){}
static void setTrue(int, void* ctx){ *static_cast<CtxBool*>(ctx)->v = true; }
static void incCount(int, void* ctx){ ++*static_cast<CtxCount*>(ctx)->v; }
static void fireOnceCb(int id, void* ctx){
    auto* c = static_cast<CtxFireOnce*>(ctx);
    *c->fired = true;
    *c->id = id;
}
static void pushOrder(int, void* ctx){
    auto* c = static_cast<CtxOrder*>(ctx);
    c->order->push_back(c->val);
}

TEST(Timer, Create){
    Timer t;
}

TEST(Timer, CreateDestroy){
    Timer t;
    t.start();
    t.stop();
}

TEST(Timer, AddSingle){
    Timer t;
    int id = t.add(100, false, noOp, nullptr);
    EXPECT_GT(id, 0);
}

TEST(Timer, AddMultiple){
    Timer t;
    std::set<int> ids;
    for(int i = 0; i < 10; i++){
        ids.insert(t.add(100, false, noOp, nullptr));
    }
    EXPECT_EQ(ids.size(), 10);
}

TEST(Timer, CancelBeforeFire){
    Timer t;
    std::atomic<bool> fired{false};
    CtxBool ctx{&fired};
    int id = t.add(10, false, setTrue, &ctx);
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
    CtxCount ctx{&count};
    t.add(10, true, incCount, &ctx);
    t.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_EQ(count, 0);
}

TEST(Timer, FireOnce){
    Timer t;
    std::atomic<bool> fired{false};
    std::atomic<int> firedId{0};
    CtxFireOnce ctx{&fired, &firedId};
    int id = t.add(10, false, fireOnceCb, &ctx);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_TRUE(fired);
    EXPECT_EQ(firedId, id);
}

TEST(Timer, FireMultiple){
    Timer t;
    std::atomic<int> count{0};
    CtxCount ctx{&count};
    t.add(10, false, incCount, &ctx);
    t.add(10, false, incCount, &ctx);
    t.add(10, false, incCount, &ctx);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_EQ(count, 3);
}

TEST(Timer, FireRepeating){
    Timer t;
    std::atomic<int> count{0};
    CtxCount ctx{&count};
    t.add(10, true, incCount, &ctx);
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
    CtxOrder ctx3{&order, 3};
    CtxOrder ctx1{&order, 1};
    CtxOrder ctx2{&order, 2};
    t.add(30, false, pushOrder, &ctx3);
    t.add(10, false, pushOrder, &ctx1);
    t.add(20, false, pushOrder, &ctx2);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    t.handleTimerEvent();

    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(Timer, ZeroDelay){
    Timer t;
    std::atomic<bool> fired{false};
    CtxBool ctx{&fired};
    t.add(0, false, setTrue, &ctx);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.handleTimerEvent();
    EXPECT_TRUE(fired);
}

TEST(Timer, StopPreventsFire){
    Timer t;
    std::atomic<bool> fired{false};
    CtxBool ctx{&fired};
    t.add(10, false, setTrue, &ctx);
    t.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.handleTimerEvent();
    EXPECT_FALSE(fired);
}
