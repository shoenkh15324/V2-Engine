#include <gtest/gtest.h>
#include "core/actor_system/runtime/dispatcher/io/event_loop_epoll.hpp"
#include "core/common/util/return.hpp"
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>

TEST(EventLoopEpoll, Create){
    EventLoopEpoll io;
}

TEST(EventLoopEpoll, CreateDestroy){
    EventLoopEpoll io;
    io.start();
    io.stop();
}

TEST(EventLoopEpoll, Subscribe){
    EventLoopEpoll io;
    io.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    EXPECT_EQ(io.subscribe(fds[0], [](){}), Ok);

    ::close(fds[0]);
    ::close(fds[1]);
    io.stop();
}

TEST(EventLoopEpoll, SubscribeDuplicate){
    EventLoopEpoll io;
    io.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    EXPECT_EQ(io.subscribe(fds[0], [](){}), Ok);
    EXPECT_EQ(io.subscribe(fds[0], [](){}), Fail);

    ::close(fds[0]);
    ::close(fds[1]);
    io.stop();
}

TEST(EventLoopEpoll, Unsubscribe){
    EventLoopEpoll io;
    io.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    io.subscribe(fds[0], [](){});
    EXPECT_EQ(io.unsubscribe(fds[0]), Ok);

    ::close(fds[0]);
    ::close(fds[1]);
    io.stop();
}

TEST(EventLoopEpoll, RunFiresHandler){
    EventLoopEpoll io;
    io.start();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    std::atomic<bool> fired{false};
    EXPECT_EQ(io.subscribe(fds[0], [&](){ fired = true; }), Ok);

    std::thread t([&](){ io.run(); });

    auto _ = ::write(fds[1], "x", 1);
    (void)_;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(fired);

    io.stop();
    t.join();
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(EventLoopEpoll, StopStopsRun){
    EventLoopEpoll io;
    io.start();

    std::thread t([&](){ io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    io.stop();
    t.join();
}
