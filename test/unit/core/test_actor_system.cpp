#include <gtest/gtest.h>
#include <memory>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "infra/platform/linux/event_loop_epoll.hpp"

namespace{

class TestActor : public Actor{
public:
    using Actor::Actor;

    int open() override{ state_ = Opened; return 0; }
    int close() override{ state_ = Closed; return 0; }
    void handle(const Message&) override{}
};

static std::unique_ptr<ActorSystem> makeSystem(int numWorkers){
    auto loop = std::make_unique<EventLoopEpoll>(64, 1000);
    return std::make_unique<ActorSystem>(numWorkers, 32, std::move(loop), nullptr);
}

} // namespace

TEST(ActorSystem, Create){
    auto sys = makeSystem(1);
}

TEST(ActorSystem, CreateActor){
    auto sys = makeSystem(1);
    auto* a = sys->createActor<TestActor>("sensor", 64);
    EXPECT_EQ(a->name(), "sensor");
    EXPECT_TRUE(a->getState() == Closed); // not opened yet
}

TEST(ActorSystem, CreateActorMailboxSize){
    auto sys = makeSystem(1);
    auto* a = sys->createActor<TestActor>("sensor", 128);
    EXPECT_EQ(a->mailboxCapacity(), 128);
}

TEST(ActorSystem, CreateActorAutoId){
    auto sys = makeSystem(1);
    auto* a0 = sys->createActor<TestActor>("a", 64);
    auto* a1 = sys->createActor<TestActor>("b", 64);
    EXPECT_EQ(a0->id(), 0);
    EXPECT_EQ(a1->id(), 1);
}

TEST(ActorSystem, StartStop){
    auto sys = makeSystem(1);
    sys->createActor<TestActor>("sensor", 64);
    sys->start();
    sys->stop();
}
