#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include "core/common/util/return.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/runtime/mailbox/mailbox.hpp"
#include "core/actor_system/runtime/dispatcher/worker.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "service/tick/tick_messages.hpp"

namespace{

class TestActor : public Actor {
public:
    using Actor::Actor;
    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message&) override {}
};

class CountingActor : public Actor {
public:
    using Actor::Actor;
    std::atomic<size_t> handled{0};
    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message&) override { handled.fetch_add(1, std::memory_order_relaxed); }
};

} // namespace

// Construction

TEST(WorkDispatcher, Create){
    WorkDispatcher d(1);
}

TEST(WorkDispatcher, CreateDestroy){
    WorkDispatcher d(1);
    d.start();
    d.stop();
}

// Ready Queue

TEST(WorkDispatcher, DispatchAcquire){
    WorkDispatcher d(1);
    d.start();

    auto actor = std::make_unique<TestActor>("test", 1);
    auto mailbox = std::make_unique<Mailbox>(16);
    ActorRuntime ctx(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    d.dispatch(&ctx);
    ActorRuntime* acquired = d.acquire(0);
    EXPECT_EQ(acquired, &ctx);

    d.stop();
}

TEST(WorkDispatcher, DispatchFifo){
    WorkDispatcher d(1);
    d.start();

    auto a1 = std::make_unique<TestActor>("a", 1);
    auto m1 = std::make_unique<Mailbox>(16);
    ActorRuntime ctx1(std::move(a1), std::move(m1), &d, nullptr, nullptr);

    auto a2 = std::make_unique<TestActor>("b", 2);
    auto m2 = std::make_unique<Mailbox>(16);
    ActorRuntime ctx2(std::move(a2), std::move(m2), &d, nullptr, nullptr);

    auto a3 = std::make_unique<TestActor>("c", 3);
    auto m3 = std::make_unique<Mailbox>(16);
    ActorRuntime ctx3(std::move(a3), std::move(m3), &d, nullptr, nullptr);

    d.dispatch(&ctx1);
    d.dispatch(&ctx2);
    d.dispatch(&ctx3);

    EXPECT_EQ(d.acquire(0), &ctx1);
    EXPECT_EQ(d.acquire(0), &ctx2);
    EXPECT_EQ(d.acquire(0), &ctx3);

    d.stop();
}

TEST(WorkDispatcher, AcquireOnEmpty){
    WorkDispatcher d(1);
    d.start();

    auto actor = std::make_unique<TestActor>("test", 1);
    auto mailbox = std::make_unique<Mailbox>(16);
    ActorRuntime ctx(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    std::thread t([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        d.dispatch(&ctx);
    });

    ActorRuntime* acquired = d.acquire(0);
    EXPECT_EQ(acquired, &ctx);

    t.join();
    d.stop();
}

TEST(WorkDispatcher, AcquireAfterStop){
    WorkDispatcher d(1);
    d.start();
    d.stop();

    ActorRuntime* acquired = d.acquire(0);
    EXPECT_EQ(acquired, nullptr);
}

TEST(WorkDispatcher, StopReleasesWorkers){
    WorkDispatcher d(3);
    d.start();
    d.stop();

    EXPECT_EQ(d.acquire(0), nullptr);
    EXPECT_EQ(d.acquire(1), nullptr);
    EXPECT_EQ(d.acquire(2), nullptr);
}

TEST(WorkDispatcher, LoadAwareDispatchSpreadsOverflow){
    WorkDispatcher d(2, WorkDispatcher::kDefaultQueueCapacity, 1);
    d.start();

    auto a0 = std::make_unique<TestActor>("hot0", 0);
    auto m0 = std::make_unique<Mailbox>(16);
    ActorRuntime rt0(std::move(a0), std::move(m0), &d, nullptr, nullptr);

    auto a1 = std::make_unique<TestActor>("hot1", 2);
    auto m1 = std::make_unique<Mailbox>(16);
    ActorRuntime rt1(std::move(a1), std::move(m1), &d, nullptr, nullptr);

    ASSERT_TRUE(d.dispatch(&rt0));
    ASSERT_TRUE(d.dispatch(&rt1));
    ASSERT_EQ(d.acquire(0), &rt0);
    ASSERT_EQ(d.acquire(1), &rt1);

    d.stop();
}

TEST(WorkDispatcher, LoadAwareKeepsHomeBelowWatermark){
    WorkDispatcher d(3, WorkDispatcher::kDefaultQueueCapacity, 5);
    d.start();

    std::vector<std::unique_ptr<ActorRuntime>> runtimes;
    for(uint64_t i = 0; i < 5; i++){
        auto actor = std::make_unique<TestActor>("home" + std::to_string(i), i * 3);
        auto mailbox = std::make_unique<Mailbox>(16);
        runtimes.push_back(std::make_unique<ActorRuntime>(std::move(actor), std::move(mailbox), &d, nullptr, nullptr));
        ASSERT_TRUE(d.dispatch(runtimes.back().get()));
    }
    for(int i = 0; i < 5; i++){
        EXPECT_EQ(d.acquire(0), runtimes[static_cast<size_t>(i)].get());
    }

    d.stop();
}

TEST(WorkDispatcher, SingleEntryGuardDedups){
    V2_METRICS()->setEnabled(true);
    V2_METRICS()->reset();

    WorkDispatcher d(2);
    d.start();

    auto actor = std::make_unique<CountingActor>("hot", 0);
    auto* counter = actor.get();
    auto mailbox = std::make_unique<Mailbox>(100000);
    ActorRuntime rt(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    const size_t M = 10000;
    for(size_t i = 0; i < M; i++){
        rt.enqueue(Message::make(Tick{}));
    }

    bool more = false;
    rt.run(-1, &more);
    EXPECT_FALSE(more);

    auto snap = V2_METRICS()->snapshot().dispatcher;
    EXPECT_EQ(snap.dispatchCount, M);
    EXPECT_EQ(snap.deduplicated, M - 1);
    EXPECT_EQ(snap.dispatchCount - snap.deduplicated, 1);
    EXPECT_EQ(rt.mailboxCount(), 0);
    EXPECT_EQ(counter->handled.load(), M);

    d.stop();
    V2_METRICS()->setEnabled(false);
}

TEST(WorkDispatcher, SingleEntryGuardConcurrentNoLoss){
    V2_METRICS()->setEnabled(true);
    V2_METRICS()->reset();

    WorkDispatcher d(4);
    d.start();

    auto actor = std::make_unique<CountingActor>("hot", 0);
    auto* counter = actor.get();
    auto mailbox = std::make_unique<Mailbox>(1000000);
    ActorRuntime rt(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    Worker w0(&d, 0, 32), w1(&d, 1, 32), w2(&d, 2, 32), w3(&d, 3, 32);
    w0.start();
    w1.start();
    w2.start();
    w3.start();

    const size_t T = 8, N = 5000;
    std::vector<std::thread> producers;
    for(size_t t = 0; t < T; t++){
        producers.emplace_back([&](){
            for(size_t i = 0; i < N; i++){
                rt.enqueue(Message::make(Tick{}));
            }
        });
    }
    for(auto& th : producers){
        th.join();
    }

    const size_t total = T * N;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(((counter->handled.load() < total) || (d.pendingWork() != 0)) && (std::chrono::steady_clock::now() < deadline)){
        std::this_thread::yield();
    }

    d.beginDrain();
    w0.stop();
    w1.stop();
    w2.stop();
    w3.stop();

    EXPECT_EQ(counter->handled.load(), total);
    EXPECT_EQ(rt.mailboxCount(), 0);
    auto snap = V2_METRICS()->snapshot().dispatcher;
    EXPECT_GT(snap.deduplicated, 0);

    V2_METRICS()->setEnabled(false);
}

TEST(WorkDispatcher, LostWakeupStress){
    WorkDispatcher d(2);
    d.start();

    auto actor = std::make_unique<CountingActor>("hot", 0);
    auto* counter = actor.get();
    auto mailbox = std::make_unique<Mailbox>(100000);
    ActorRuntime rt(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    const size_t N = 50000;
    std::thread producer([&](){
        for(size_t i = 0; i < N; i++){
            rt.enqueue(Message::make(Tick{}));
        }
    });

    while(counter->handled.load(std::memory_order_acquire) < N){
        ActorRuntime* t = d.acquire(0);
        if(t) t->run(1);
    }
    producer.join();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while((d.pendingWork() != 0) && (std::chrono::steady_clock::now() < deadline)){
        ActorRuntime* t = d.acquire(0);
        if(t){
            t->run(-1);
        }else{
            std::this_thread::yield();
        }
    }

    EXPECT_EQ(counter->handled.load(), N);
    EXPECT_EQ(rt.mailboxCount(), 0u);
    EXPECT_EQ(d.pendingWork(), 0u);

    d.stop();
}

TEST(WorkDispatcher, FinalizeRetiresExactlyOnce){
    WorkDispatcher d(1);
    d.start();

    auto actor = std::make_unique<CountingActor>("solo", 7);
    auto* counter = actor.get();
    auto mailbox = std::make_unique<Mailbox>(64);
    ActorRuntime rt(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    for(size_t i = 0; i < 10; i++){
        rt.enqueue(Message::make(Tick{}));
    }
    ASSERT_EQ(d.pendingWork(), 1u);

    ActorRuntime* t = d.acquire(0);
    ASSERT_EQ(t, &rt);
    t->run(-1);

    EXPECT_EQ(counter->handled.load(), 10u);
    EXPECT_EQ(rt.mailboxCount(), 0u);
    EXPECT_EQ(d.pendingWork(), 0u);

    d.stop();
}
