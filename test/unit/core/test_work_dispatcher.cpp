#include <gtest/gtest.h>
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

namespace{

class TestActor : public Actor{
public:
    using Actor::Actor;
    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message&) override {}
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
    auto mailbox = std::make_unique<LockFreeMpscQueue<Message>>(16);
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
    auto m1 = std::make_unique<LockFreeMpscQueue<Message>>(16);
    ActorRuntime ctx1(std::move(a1), std::move(m1), &d, nullptr, nullptr);

    auto a2 = std::make_unique<TestActor>("b", 2);
    auto m2 = std::make_unique<LockFreeMpscQueue<Message>>(16);
    ActorRuntime ctx2(std::move(a2), std::move(m2), &d, nullptr, nullptr);

    auto a3 = std::make_unique<TestActor>("c", 3);
    auto m3 = std::make_unique<LockFreeMpscQueue<Message>>(16);
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
    auto mailbox = std::make_unique<LockFreeMpscQueue<Message>>(16);
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
