#include <gtest/gtest.h>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
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
        std::visit(overloaded{
            [this](const Tick&){ tickCount++; },
            [](const auto&){}
        }, msg);
    }

    int openCount = 0;
    int closeCount = 0;
    std::atomic<int> tickCount{0};
};

} // namespace

TEST(ActorSystemIntegration, FullLifeCycle){
    ActorSystem sys(1);
    auto* a = sys.createActor<TestActor>("actor_a", 64);
    EXPECT_EQ(a->getState(), Closed);

    sys.start();
    EXPECT_EQ(a->getState(), Opened);
    EXPECT_EQ(a->openCount, 1);

    // sendMsg: actor_a -> actor_a (self)
    // dispatcher dispatch → worker acquire → ActorContext::run → handle(Tick)
    a->sendMsg("actor_a", Tick{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(a->tickCount, 1);

    sys.stop();
    EXPECT_EQ(a->getState(), Closed);
    EXPECT_EQ(a->closeCount, 1);
}

TEST(ActorSystemIntegration, SendBetweenActors){
    ActorSystem sys(2);

    auto* a = sys.createActor<TestActor>("sender", 64);
    auto* b = sys.createActor<TestActor>("receiver", 64);

    sys.start();

    a->sendMsg("receiver", Tick{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(b->tickCount, 1);
    EXPECT_EQ(a->tickCount, 0);

    sys.stop();
}

TEST(ActorSystemIntegration, MultipleMessages){
    ActorSystem sys(2);

    auto* a = sys.createActor<TestActor>("a", 64);
    auto* b = sys.createActor<TestActor>("b", 64);

    sys.start();
    for(int i = 0; i < 10; i++){
        a->sendMsg("b", Tick{});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(b->tickCount, 10);

    sys.stop();
}

TEST(ActorSystemIntegration, OpenCloseOnce){
    ActorSystem sys(1);
    auto* a = sys.createActor<TestActor>("a", 64);

    sys.start();
    EXPECT_EQ(a->openCount, 1);

    sys.stop();
    EXPECT_EQ(a->closeCount, 1);

    // restart
    sys.start();
    EXPECT_EQ(a->openCount, 2);

    sys.stop();
    EXPECT_EQ(a->closeCount, 2);
}
