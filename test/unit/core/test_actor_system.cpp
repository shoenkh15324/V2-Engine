#include <gtest/gtest.h>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor_system.hpp"

namespace{

class TestActor : public Actor{
public:
    using Actor::Actor;

    int open() override{ state_ = Opened; return 0; }
    int close() override{ state_ = Closed; return 0; }
    void handle(const Message&) override{}
};

} // namespace

TEST(ActorSystem, Create){
    ActorSystem sys(1);
}

TEST(ActorSystem, CreateActor){
    ActorSystem sys(1);
    auto* a = sys.createActor<TestActor>("sensor", 64);
    EXPECT_EQ(a->name(), "sensor");
    EXPECT_TRUE(a->getState() == Closed); // not opened yet
}

TEST(ActorSystem, CreateActorMailboxSize){
    ActorSystem sys(1);
    auto* a = sys.createActor<TestActor>("sensor", 128);
    EXPECT_EQ(a->mailboxCapacity(), 128);
}

TEST(ActorSystem, CreateActorAutoId){
    ActorSystem sys(1);
    auto* a0 = sys.createActor<TestActor>("a", 64);
    auto* a1 = sys.createActor<TestActor>("b", 64);
    EXPECT_EQ(a0->id(), 0);
    EXPECT_EQ(a1->id(), 1);
}

TEST(ActorSystem, StartStop){
    ActorSystem sys(1);
    sys.createActor<TestActor>("sensor", 64);
    sys.start();
    sys.stop();
}
