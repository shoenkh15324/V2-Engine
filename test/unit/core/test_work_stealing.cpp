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

namespace {

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

TEST(WorkStealing, MovesTokenToIdleWorker){
    WorkDispatcher d({.workerCount = 2, .highWatermark = 1});
    d.start();

    auto actor = std::make_unique<TestActor>("hot", 0);
    auto mailbox = std::make_unique<Mailbox>(16);
    ActorRuntime rt(std::move(actor), std::move(mailbox), {.workDispatcher = &d});
    ASSERT_TRUE(d.dispatch(&rt));

    ActorRuntime* stolen = d.acquire(1);
    EXPECT_EQ(stolen, &rt);
    d.stop();
}

TEST(WorkStealing, UsesBusyIntervalFirst){
    WorkDispatcher d({.workerCount = 2, .highWatermark = 1, .busyStealIntervalUs = 1, .idleStealIntervalUs = 500});
    d.start();

    auto actor = std::make_unique<TestActor>("hot", 0);
    auto mailbox = std::make_unique<Mailbox>(16);
    ActorRuntime rt(std::move(actor), std::move(mailbox), {.workDispatcher = &d});
    ASSERT_TRUE(d.dispatch(&rt));

    auto begin = std::chrono::steady_clock::now();
    ActorRuntime* stolen = d.acquire(1);
    auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_EQ(stolen, &rt);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
    d.stop();
}

TEST(WorkStealing, BacksOffWhenIdle){
    V2_METRICS()->setEnabled(true);
    V2_METRICS()->reset();

    WorkDispatcher d({.workerCount = 2, .highWatermark = 1, .busyStealIntervalUs = 1, .idleStealIntervalUs = 100000});
    d.start();

    std::thread t([&](){
        d.acquire(0);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(650));
    d.stop();
    t.join();

    auto failCount = V2_METRICS()->snapshot().dispatcher.stealFailCount;
    EXPECT_GE(failCount, 2);
    EXPECT_LE(failCount, 8);
    V2_METRICS()->setEnabled(false);
}

TEST(WorkStealing, KeepsSingleEntryGuardConcurrent){
    V2_METRICS()->setEnabled(true);
    V2_METRICS()->reset();

    WorkDispatcher d({.workerCount = 4, .highWatermark = 1000000, .busyStealIntervalUs = 1, .idleStealIntervalUs = 10});
    d.start();

    auto actor = std::make_unique<CountingActor>("hot", 0);
    auto* counter = actor.get();
    auto mailbox = std::make_unique<Mailbox>(1000000);
    ActorRuntime rt(std::move(actor), std::move(mailbox), {.workDispatcher = &d});

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
    EXPECT_GT(snap.stealCount, 0);
    V2_METRICS()->setEnabled(false);
}

TEST(WorkStealing, CountsMetrics){
    V2_METRICS()->setEnabled(true);
    V2_METRICS()->reset();

    WorkDispatcher d({.workerCount = 2});
    d.start();

    std::vector<std::unique_ptr<ActorRuntime>> runtimes;
    for(size_t i = 0; i < 3; i++){
        auto actor = std::make_unique<TestActor>("a" + std::to_string(i), i * 2);
        auto mailbox = std::make_unique<Mailbox>(16);
        ActorRuntimeDeps deps{.workDispatcher = &d};
        runtimes.push_back(std::unique_ptr<ActorRuntime>(new ActorRuntime(std::move(actor), std::move(mailbox), deps)));
        ASSERT_TRUE(d.dispatch(runtimes.back().get()));
    }

    for(size_t i = 0; i < 3; i++){
        ActorRuntime* stolen = d.acquire(1);
        EXPECT_NE(stolen, nullptr);
    }

    auto snap = V2_METRICS()->snapshot().dispatcher;
    EXPECT_EQ(snap.stealCount, 3);
    EXPECT_EQ(snap.stealFailCount, 0);
    d.stop();
    V2_METRICS()->setEnabled(false);
}
