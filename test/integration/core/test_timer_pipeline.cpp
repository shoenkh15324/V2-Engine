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

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message& msg) override {
        if(msg.id() == MessageId::Tick) tickCount++;
    }

    std::atomic<int> tickCount{0};
};

static std::unique_ptr<ActorSystem> makeSystem(int numWorkers){
    auto loop = std::make_unique<EventLoopEpoll>(64, 1000);
    return createDefaultActorSystem({numWorkers, 32}, std::move(loop));
}

} // namespace

TEST(TimerPipeline, SendMsgAfter){
    auto sys = makeSystem(1);
    auto* a = sys->createActor<TestActor>("a", 64);
    auto* b = sys->createActor<TestActor>("b", 64);
    sys->start();

    std::thread t([&](){ sys->run(); });

    a->sendMsgAfter("b", Message::make(Tick{}), 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(b->tickCount, 1);

    sys->requestStop();
    t.join();
}

TEST(TimerPipeline, SendMsgAfterMultiple){
    auto sys = makeSystem(2);
    auto* a = sys->createActor<TestActor>("a", 64);
    auto* b = sys->createActor<TestActor>("b", 64);
    sys->start();

    std::thread t([&](){ sys->run(); });

    a->sendMsgAfter("b", Message::make(Tick{}), 10);
    a->sendMsgAfter("b", Message::make(Tick{}), 20);
    a->sendMsgAfter("b", Message::make(Tick{}), 30);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(b->tickCount, 3);

    sys->requestStop();
    t.join();
}

TEST(TimerPipeline, StartTimerRepeating){
    auto sys = makeSystem(1);
    auto* a = sys->createActor<TestActor>("a", 64);
    sys->start();

    std::thread t([&](){ sys->run(); });

    a->startTimer(Message::make(Tick{}), 20, true);

    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    EXPECT_GE(a->tickCount, 3);

    sys->requestStop();
    t.join();
}

TEST(TimerPipeline, CancelTimer){
    auto sys = makeSystem(1);
    auto* a = sys->createActor<TestActor>("a", 64);
    sys->start();

    std::thread t([&](){ sys->run(); });

    int id = a->startTimer(Message::make(Tick{}), 20, true);
    a->cancelTimer(id);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(a->tickCount, 0);

    sys->requestStop();
    t.join();
}
