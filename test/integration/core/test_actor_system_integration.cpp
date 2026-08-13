#include <gtest/gtest.h>
#include <memory>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "infra/platform/linux/event_loop_epoll.hpp"
#include "service/tick/tick_messages.hpp"
#include <atomic>
#include <thread>
#include <chrono>

namespace{

class TestActor : public Actor{
public:
    TestActor(const std::string& name, uint64_t id) : Actor(name, id){}

    int open() override{
        state_ = Opened;
        openCount++;
        return 0;
    }

    int close() override{
        state_ = Closed;
        closeCount++;
        return 0;
    }

    void handle(const Message& msg) override{
        dispatch(*this, msg, std::tuple<Tick>{});
    }
    void handle(const Tick&){ tickCount++; }

    int openCount = 0;
    int closeCount = 0;
    std::atomic<int> tickCount{0};
};

static std::unique_ptr<ActorSystem> makeSystem(int numWorkers){
    auto loop = std::make_unique<EventLoopEpoll>(64, 1000);
    return createDefaultActorSystem({numWorkers, 32}, std::move(loop));
}

} // namespace

TEST(ActorSystemIntegration, FullLifeCycle){
    auto sys = makeSystem(1);
    auto* a = sys->createActor<TestActor>("actor_a", 64);
    EXPECT_EQ(a->getState(), Closed);

    sys->start();
    EXPECT_EQ(a->getState(), Opened);
    EXPECT_EQ(a->openCount, 1);

    // sendMsg: actor_a -> actor_a (self)
    // dispatcher dispatch → worker acquire → ActorRuntime::run → handle(Tick)
    a->sendMsg("actor_a", Message::make(Tick{}));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(a->tickCount, 1);

    sys->stop();
    EXPECT_EQ(a->getState(), Closed);
    EXPECT_EQ(a->closeCount, 1);
}

TEST(ActorSystemIntegration, SendBetweenActors){
    auto sys = makeSystem(2);

    auto* a = sys->createActor<TestActor>("sender", 64);
    auto* b = sys->createActor<TestActor>("receiver", 64);

    sys->start();

    a->sendMsg("receiver", Message::make(Tick{}));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(b->tickCount, 1);
    EXPECT_EQ(a->tickCount, 0);

    sys->stop();
}

TEST(ActorSystemIntegration, MultipleMessages){
    auto sys = makeSystem(2);

    auto* a = sys->createActor<TestActor>("a", 64);
    auto* b = sys->createActor<TestActor>("b", 64);

    sys->start();
    for(int i = 0; i < 10; i++){
        a->sendMsg("b", Message::make(Tick{}));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(b->tickCount, 10);

    sys->stop();
}

TEST(ActorSystemIntegration, OpenCloseOnce){
    auto sys = makeSystem(1);
    auto* a = sys->createActor<TestActor>("a", 64);

    sys->start();
    EXPECT_EQ(a->openCount, 1);

    sys->stop();
    EXPECT_EQ(a->closeCount, 1);

    // restart
    sys->start();
    EXPECT_EQ(a->openCount, 2);

    sys->stop();
    EXPECT_EQ(a->closeCount, 2);
}

TEST(ActorSystemIntegration, DrainProcessesPendingMessages){
    auto sys = makeSystem(2);
    auto* a = sys->createActor<TestActor>("drain_actor", 256);
    sys->start();
    constexpr int N = 100;
    for(int i = 0; i < N; i++){
        a->sendMsg("drain_actor", Message::make(Tick{}));
    }
    sys->stop();
    EXPECT_EQ(a->tickCount, N);
}
