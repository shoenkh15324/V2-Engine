#include <gtest/gtest.h>
#include "core/actor_system/runtime/actor_registry.hpp"
#include "core/actor_system/actor/actor.hpp"
#include <memory>
#include <thread>
#include <vector>
#include <atomic>

namespace{

class TestActor : public Actor{
public:
    TestActor(const std::string& name, uint64_t id) : Actor(name, id){}

    int open() override{
        opened = true;
        return 0;
    }
    int close() override{
        opened = false;
        return 0;
    }
    void handle(const Message&) override{
        handled = true;
    }

    bool opened = false;
    bool handled = false;
};

} // namespace

TEST(ActorRegistry, AddAndFindByName){
    ActorRegistry reg;
    TestActor actor("sensor", 1);
    reg.add(&actor);
    EXPECT_EQ(reg.findByName("sensor"), &actor);
}

TEST(ActorRegistry, AddAndFindById){
    ActorRegistry reg;
    TestActor actor("sensor", 42);
    reg.add(&actor);
    EXPECT_EQ(reg.findById(42), &actor);
}

TEST(ActorRegistry, FindNonExistent){
    ActorRegistry reg;
    EXPECT_EQ(reg.findByName("nope"), nullptr);
    EXPECT_EQ(reg.findById(999), nullptr);
}

TEST(ActorRegistry, Remove){
    ActorRegistry reg;
    TestActor actor("sensor", 1);
    reg.add(&actor);
    reg.remove(&actor);
    EXPECT_EQ(reg.findByName("sensor"), nullptr);
    EXPECT_EQ(reg.findById(1), nullptr);
}

TEST(ActorRegistry, MultipleActors){
    ActorRegistry reg;
    TestActor a1("sensor", 1);
    TestActor a2("actuator", 2);
    TestActor a3("controller", 3);

    reg.add(&a1);
    reg.add(&a2);
    reg.add(&a3);

    EXPECT_EQ(reg.findByName("sensor"), &a1);
    EXPECT_EQ(reg.findByName("actuator"), &a2);
    EXPECT_EQ(reg.findByName("controller"), &a3);
    EXPECT_EQ(reg.findById(1), &a1);
    EXPECT_EQ(reg.findById(2), &a2);
    EXPECT_EQ(reg.findById(3), &a3);
}

TEST(ActorRegistry, Clear){
    ActorRegistry reg;
    TestActor a1("sensor", 1);
    TestActor a2("actuator", 2);

    reg.add(&a1);
    reg.add(&a2);
    reg.clear();

    EXPECT_EQ(reg.findByName("sensor"), nullptr);
    EXPECT_EQ(reg.findById(2), nullptr);
}

TEST(ActorRegistry, ForEachActor){
    ActorRegistry reg;
    TestActor a1("sensor", 1);
    TestActor a2("actuator", 2);
    TestActor a3("controller", 3);

    reg.add(&a1);
    reg.add(&a2);
    reg.add(&a3);

    int count = 0;
    reg.forEachActor([&](Actor*){ count++; });
    EXPECT_EQ(count, 3);
}

TEST(ActorRegistry, EnableActor){
    ActorRegistry reg;
    TestActor actor("sensor", 1);
    reg.add(&actor);
    EXPECT_EQ(reg.enableActor("sensor"), 0);
    EXPECT_TRUE(actor.opened);
}

TEST(ActorRegistry, DisableActor){
    ActorRegistry reg;
    TestActor actor("sensor", 1);
    reg.add(&actor);
    reg.enableActor("sensor");
    EXPECT_EQ(reg.disableActor("sensor"), 0);
    EXPECT_FALSE(actor.opened);
}

TEST(ActorRegistry, DisableEssentialActor){
    ActorRegistry reg;
    TestActor actor("sensor", 1);
    actor.setEssential(true);
    reg.add(&actor);
    reg.enableActor("sensor");
    EXPECT_TRUE(actor.opened);
    EXPECT_EQ(reg.disableActor("sensor"), -2);
    EXPECT_TRUE(actor.opened);
}

TEST(ActorRegistry, EnableUnknown){
    ActorRegistry reg;
    EXPECT_EQ(reg.enableActor("nope"), -1);
    EXPECT_EQ(reg.disableActor("nope"), -1);
}

TEST(ActorRegistry, ThreadSafety){
    ActorRegistry reg;
    std::vector<std::unique_ptr<TestActor>> actors;
    for(int i = 0; i < 100; i++){
        actors.push_back(std::make_unique<TestActor>("actor" + std::to_string(i), i));
    }

    std::atomic<int> found{0};
    std::thread t1([&](){
        for(int i = 0; i < 50; i++) reg.add(actors[i].get());
    });
    std::thread t2([&](){
        for(int i = 50; i < 100; i++) reg.add(actors[i].get());
    });
    t1.join();
    t2.join();

    reg.forEachActor([&](Actor*){ found++; });
    EXPECT_EQ(found, 100);
}
