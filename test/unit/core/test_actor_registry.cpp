#include <gtest/gtest.h>
#include "core/actor_system/actor/actor_registry.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
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
    EXPECT_TRUE(reg.findHandleByName("sensor").valid());
    EXPECT_EQ(reg.findActorByName("sensor"), &actor);
}

TEST(ActorRegistry, AddAndFindById){
    ActorRegistry reg;
    TestActor actor("sensor", 42);
    reg.add(&actor);
    EXPECT_TRUE(reg.findHandleById(42).valid());
    EXPECT_EQ(reg.findActorById(42), &actor);
}

TEST(ActorRegistry, FindNonExistent){
    ActorRegistry reg;
    EXPECT_FALSE(reg.findHandleByName("nope").valid());
    EXPECT_FALSE(reg.findHandleById(999).valid());
    EXPECT_EQ(reg.findActorByName("nope"), nullptr);
    EXPECT_EQ(reg.findActorById(999), nullptr);
}

TEST(ActorRegistry, Remove){
    ActorRegistry reg;
    TestActor actor("sensor", 1);
    reg.add(&actor);
    reg.remove(&actor);
    EXPECT_FALSE(reg.findHandleByName("sensor").valid());
    EXPECT_FALSE(reg.findHandleById(1).valid());
    EXPECT_EQ(reg.findActorByName("sensor"), nullptr);
    EXPECT_EQ(reg.findActorById(1), nullptr);
}

TEST(ActorRegistry, MultipleActors){
    ActorRegistry reg;
    TestActor a1("sensor", 1);
    TestActor a2("actuator", 2);
    TestActor a3("controller", 3);

    reg.add(&a1);
    reg.add(&a2);
    reg.add(&a3);

    EXPECT_EQ(reg.findActorByName("sensor"), &a1);
    EXPECT_EQ(reg.findActorByName("actuator"), &a2);
    EXPECT_EQ(reg.findActorByName("controller"), &a3);
    EXPECT_EQ(reg.findActorById(1), &a1);
    EXPECT_EQ(reg.findActorById(2), &a2);
    EXPECT_EQ(reg.findActorById(3), &a3);
}

TEST(ActorRegistry, Clear){
    ActorRegistry reg;
    TestActor a1("sensor", 1);
    TestActor a2("actuator", 2);

    reg.add(&a1);
    reg.add(&a2);
    reg.clear();

    EXPECT_FALSE(reg.findHandleByName("sensor").valid());
    EXPECT_FALSE(reg.findHandleById(2).valid());
    EXPECT_EQ(reg.findActorByName("sensor"), nullptr);
    EXPECT_EQ(reg.findActorById(2), nullptr);
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
    reg.forEachActor([&](ActorHandle){ count++; });
    EXPECT_EQ(count, 3);
}

TEST(ActorRegistry, ThreadSafety){
    ActorRegistry reg;
    std::vector<std::unique_ptr<TestActor>> actors;
    for(int i = 0; i < 100; i++){
        actors.push_back(std::make_unique<TestActor>("actor" + std::to_string(i), i));
        reg.add(actors.back().get());
    }

    std::atomic<int> found{0};
    std::thread t1([&](){
        reg.forEachActor([&](ActorHandle){ found++; });
    });
    std::thread t2([&](){
        reg.forEachActor([&](ActorHandle){ found++; });
    });
    t1.join();
    t2.join();

    EXPECT_EQ(found, 200);
}
