#include <gtest/gtest.h>
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/common/util/return.hpp"
#include "core/common/config/platform_config.h"
#include <thread>
#include <atomic>
#include <chrono>

#if V2_PLATFORM_LINUX
    #include <unistd.h>
#endif

namespace{

struct TestContext{};

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

    TestContext ctx;
    d.dispatch(reinterpret_cast<ActorContext*>(&ctx));
    ActorContext* acquired = d.acquire();
    EXPECT_EQ(acquired, reinterpret_cast<ActorContext*>(&ctx));

    d.stop();
}

TEST(Dispatcher, DispatchDuplicate){
    Dispatcher d(1);
    d.start();

    TestContext ctx;
    d.dispatch(reinterpret_cast<ActorContext*>(&ctx));
    d.dispatch(reinterpret_cast<ActorContext*>(&ctx));

    ActorContext* a1 = d.acquire();
    EXPECT_EQ(a1, reinterpret_cast<ActorContext*>(&ctx));

    d.stop();
    ActorContext* a2 = d.acquire();
    EXPECT_EQ(a2, nullptr);
}

TEST(Dispatcher, DispatchFifo){
    Dispatcher d(1);
    d.start();

    TestContext a, b, c;
    d.dispatch(reinterpret_cast<ActorContext*>(&a));
    d.dispatch(reinterpret_cast<ActorContext*>(&b));
    d.dispatch(reinterpret_cast<ActorContext*>(&c));

    EXPECT_EQ(d.acquire(), reinterpret_cast<ActorContext*>(&a));
    EXPECT_EQ(d.acquire(), reinterpret_cast<ActorContext*>(&b));
    EXPECT_EQ(d.acquire(), reinterpret_cast<ActorContext*>(&c));

    d.stop();
}

TEST(Dispatcher, AcquireOnEmpty){
    Dispatcher d(1);
    d.start();

    TestContext ctx;
    std::thread t([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        d.dispatch(reinterpret_cast<ActorContext*>(&ctx));
    });

    ActorContext* acquired = d.acquire();
    EXPECT_EQ(acquired, reinterpret_cast<ActorContext*>(&ctx));

    t.join();
    d.stop();
}

TEST(Dispatcher, AcquireAfterStop){
    Dispatcher d(1);
    d.start();
    d.stop();

    // stop() releases semaphore, but queue is empty -> returns nullptr
    ActorContext* acquired = d.acquire();
    EXPECT_EQ(acquired, nullptr);
}

TEST(Dispatcher, StopReleasesWorkers){
    Dispatcher d(3);
    d.start();
    d.stop();

    // stop() with workerCount=3 releases semaphore 3 times
    EXPECT_EQ(d.acquire(), nullptr);
    EXPECT_EQ(d.acquire(), nullptr);
    EXPECT_EQ(d.acquire(), nullptr);
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

    ::write(fds[1], "x", 1);
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
    t.join(); // should not hang
}

#endif
