#include <gtest/gtest.h>
#include <memory>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/runtime/mailbox/mailbox.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"

namespace{

class TestActor : public Actor {
public:
    using Actor::Actor;
    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message&) override {}
};

} // namespace

TEST(BackpressureTest, DispatchFailureAddsToPending){
    WorkDispatcher d({.workerCount = 1, .queueCapacity = 2});
    d.start();

    auto a1 = std::make_unique<TestActor>("a1", 1);
    auto a2 = std::make_unique<TestActor>("a2", 2);
    auto a3 = std::make_unique<TestActor>("a3", 3);

    auto m1 = std::make_unique<Mailbox>(64);
    auto m2 = std::make_unique<Mailbox>(64);
    auto m3 = std::make_unique<Mailbox>(64);

    ActorRuntime rt1(std::move(a1), std::move(m1), {.workDispatcher = &d});
    ActorRuntime rt2(std::move(a2), std::move(m2), {.workDispatcher = &d});
    ActorRuntime rt3(std::move(a3), std::move(m3), {.workDispatcher = &d});

    EXPECT_TRUE(d.dispatch(&rt1));
    EXPECT_TRUE(d.dispatch(&rt2));
    EXPECT_FALSE(d.dispatch(&rt3));

    ActorRuntime* popped = d.acquire(0);
    EXPECT_NE(popped, nullptr);

    d.drainPendedActor();

    ActorRuntime* acquired = d.acquire(0);
    EXPECT_NE(acquired, nullptr);

    d.stop();
}

TEST(BackpressureTest, PendingClearedOnStop){
    WorkDispatcher d({.workerCount = 1, .queueCapacity = 2});
    d.start();

    auto a1 = std::make_unique<TestActor>("a1", 1);
    auto a2 = std::make_unique<TestActor>("a2", 2);
    auto a3 = std::make_unique<TestActor>("a3", 3);

    auto m1 = std::make_unique<Mailbox>(64);
    auto m2 = std::make_unique<Mailbox>(64);
    auto m3 = std::make_unique<Mailbox>(64);

    ActorRuntime rt1(std::move(a1), std::move(m1), {.workDispatcher = &d});
    ActorRuntime rt2(std::move(a2), std::move(m2), {.workDispatcher = &d});
    ActorRuntime rt3(std::move(a3), std::move(m3), {.workDispatcher = &d});

    d.dispatch(&rt1);
    d.dispatch(&rt2);
    EXPECT_FALSE(d.dispatch(&rt3));

    d.acquire(0);
    d.acquire(0);

    d.stop();

    d.start();
    auto a4 = std::make_unique<TestActor>("a4", 4);
    auto m4 = std::make_unique<Mailbox>(64);
    ActorRuntime rt4(std::move(a4), std::move(m4), {.workDispatcher = &d});
    EXPECT_TRUE(d.dispatch(&rt4));

    d.stop();
}

TEST(BackpressureTest, DrainPendingRetriesUntilQueueSlotAvailable){
    WorkDispatcher d({.workerCount = 1, .queueCapacity = 2});
    d.start();

    auto a1 = std::make_unique<TestActor>("a1", 1);
    auto a2 = std::make_unique<TestActor>("a2", 2);
    auto a3 = std::make_unique<TestActor>("a3", 3);

    auto m1 = std::make_unique<Mailbox>(64);
    auto m2 = std::make_unique<Mailbox>(64);
    auto m3 = std::make_unique<Mailbox>(64);

    ActorRuntime rt1(std::move(a1), std::move(m1), {.workDispatcher = &d});
    ActorRuntime rt2(std::move(a2), std::move(m2), {.workDispatcher = &d});
    ActorRuntime rt3(std::move(a3), std::move(m3), {.workDispatcher = &d});

    d.dispatch(&rt1);
    d.dispatch(&rt2);
    d.dispatch(&rt3);

    ActorRuntime* consumed = d.acquire(0);
    EXPECT_EQ(consumed, &rt1);

    d.drainPendedActor();

    ActorRuntime* acquired = d.acquire(0);
    EXPECT_NE(acquired, nullptr);

    d.stop();
}

TEST(BackpressureTest, DrainPendingWithMultipleWorkers){
    WorkDispatcher d({.workerCount = 2, .queueCapacity = 2});
    d.start();

    auto a1 = std::make_unique<TestActor>("a1", 1);
    auto a2 = std::make_unique<TestActor>("a2", 2);
    auto a3 = std::make_unique<TestActor>("a3", 3);
    auto a4 = std::make_unique<TestActor>("a4", 4);
    auto a5 = std::make_unique<TestActor>("a5", 5);

    auto m1 = std::make_unique<Mailbox>(64);
    auto m2 = std::make_unique<Mailbox>(64);
    auto m3 = std::make_unique<Mailbox>(64);
    auto m4 = std::make_unique<Mailbox>(64);
    auto m5 = std::make_unique<Mailbox>(64);

    ActorRuntime rt1(std::move(a1), std::move(m1), {.workDispatcher = &d});
    ActorRuntime rt2(std::move(a2), std::move(m2), {.workDispatcher = &d});
    ActorRuntime rt3(std::move(a3), std::move(m3), {.workDispatcher = &d});
    ActorRuntime rt4(std::move(a4), std::move(m4), {.workDispatcher = &d});
    ActorRuntime rt5(std::move(a5), std::move(m5), {.workDispatcher = &d});

    d.dispatch(&rt1);
    d.dispatch(&rt2);
    d.dispatch(&rt3);
    d.dispatch(&rt4);
    EXPECT_FALSE(d.dispatch(&rt5));

    ActorRuntime* w0 = d.acquire(0);
    ActorRuntime* w1 = d.acquire(1);
    EXPECT_NE(w0, nullptr);
    EXPECT_NE(w1, nullptr);

    d.drainPendedActor();

    ActorRuntime* extra = d.acquire(0);
    if(!extra) extra = d.acquire(1);
    EXPECT_NE(extra, nullptr);

    d.stop();
}
