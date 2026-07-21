#include <gtest/gtest.h>
#include "core/actor_system/runtime/dispatcher/dispatcher.hpp"
#include "core/actor_system/actor/i_actor_runtime.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/common/util/return.hpp"
#include "core/common/config/platform_config.h"
#include "core/actor_system/messages/tick_messages.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

#if V2_PLATFORM_LINUX
    #include <unistd.h>
#endif

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

TEST(Dispatcher, Create){
    Dispatcher d(1);
}

TEST(Dispatcher, CreateDestroy){
    Dispatcher d(1);
    d.start();
    d.stop();
}

// Ready Queue

TEST(Dispatcher, DispatchAcquire){
    Dispatcher d(1);
    d.start();

    auto actor = std::make_unique<TestActor>("test", 1);
    auto mailbox = std::make_unique<LockFreeMpscQueue<Message>>(16);
    ActorContext ctx(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    d.dispatch(&ctx);
    ActorContext* acquired = d.acquire(0);
    EXPECT_EQ(acquired, &ctx);

    d.stop();
}

TEST(Dispatcher, DispatchFifo){
    Dispatcher d(1);
    d.start();

    auto a1 = std::make_unique<TestActor>("a", 1);
    auto m1 = std::make_unique<LockFreeMpscQueue<Message>>(16);
    ActorContext ctx1(std::move(a1), std::move(m1), &d, nullptr, nullptr);

    auto a2 = std::make_unique<TestActor>("b", 2);
    auto m2 = std::make_unique<LockFreeMpscQueue<Message>>(16);
    ActorContext ctx2(std::move(a2), std::move(m2), &d, nullptr, nullptr);

    auto a3 = std::make_unique<TestActor>("c", 3);
    auto m3 = std::make_unique<LockFreeMpscQueue<Message>>(16);
    ActorContext ctx3(std::move(a3), std::move(m3), &d, nullptr, nullptr);

    d.dispatch(&ctx1);
    d.dispatch(&ctx2);
    d.dispatch(&ctx3);

    EXPECT_EQ(d.acquire(0), &ctx1);
    EXPECT_EQ(d.acquire(0), &ctx2);
    EXPECT_EQ(d.acquire(0), &ctx3);

    d.stop();
}

TEST(Dispatcher, AcquireOnEmpty){
    Dispatcher d(1);
    d.start();

    auto actor = std::make_unique<TestActor>("test", 1);
    auto mailbox = std::make_unique<LockFreeMpscQueue<Message>>(16);
    ActorContext ctx(std::move(actor), std::move(mailbox), &d, nullptr, nullptr);

    std::thread t([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        d.dispatch(&ctx);
    });

    ActorContext* acquired = d.acquire(0);
    EXPECT_EQ(acquired, &ctx);

    t.join();
    d.stop();
}

TEST(Dispatcher, AcquireAfterStop){
    Dispatcher d(1);
    d.start();
    d.stop();

    ActorContext* acquired = d.acquire(0);
    EXPECT_EQ(acquired, nullptr);
}

TEST(Dispatcher, StopReleasesWorkers){
    Dispatcher d(3);
    d.start();
    d.stop();

    EXPECT_EQ(d.acquire(0), nullptr);
    EXPECT_EQ(d.acquire(1), nullptr);
    EXPECT_EQ(d.acquire(2), nullptr);
}

// Epoll

#if V2_PLATFORM_LINUX

TEST(Dispatcher, Subscribe){
    Dispatcher d(1);
    d.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    EXPECT_EQ(d.subscribe(fds[0], [](){}), Ok);

    ::close(fds[0]);
    ::close(fds[1]);
    d.stop();
}

TEST(Dispatcher, SubscribeDuplicate){
    Dispatcher d(1);
    d.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    EXPECT_EQ(d.subscribe(fds[0], [](){}), Ok);
    EXPECT_EQ(d.subscribe(fds[0], [](){}), Fail);

    ::close(fds[0]);
    ::close(fds[1]);
    d.stop();
}

TEST(Dispatcher, Unsubscribe){
    Dispatcher d(1);
    d.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    d.subscribe(fds[0], [](){});
    EXPECT_EQ(d.unsubscribe(fds[0]), Ok);

    ::close(fds[0]);
    ::close(fds[1]);
    d.stop();
}

TEST(Dispatcher, RunFiresHandler){
    Dispatcher d(1);
    d.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    std::atomic<bool> fired{false};
    EXPECT_EQ(d.subscribe(fds[0], [&](){ fired = true; }), Ok);

    std::thread t([&](){ d.run(); });

    auto _ = ::write(fds[1], "x", 1);
    (void)_;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(fired);

    d.stop();
    t.join();
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(Dispatcher, StopStopsRun){
    Dispatcher d(1);
    d.start();

    std::thread t([&](){ d.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    d.stop();
    t.join();
}

#endif
