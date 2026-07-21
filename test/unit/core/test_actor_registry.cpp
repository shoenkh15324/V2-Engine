#include <gtest/gtest.h>
#include "core/actor_system/runtime/actor_registry.hpp"
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
    EXPECT_TRUE(reg.findByName("sensor").valid());
    EXPECT_EQ(reg.findByName("sensor").get(), &actor);
}

TEST(ActorRegistry, AddAndFindById){
    ActorRegistry reg;
    TestActor actor("sensor", 42);
    reg.add(&actor);
    EXPECT_TRUE(reg.findById(42).valid());
    EXPECT_EQ(reg.findById(42).get(), &actor);
}

TEST(ActorRegistry, FindNonExistent){
    ActorRegistry reg;
    EXPECT_FALSE(reg.findByName("nope").valid());
    EXPECT_FALSE(reg.findById(999).valid());
}

TEST(ActorRegistry, Remove){
    ActorRegistry reg;
    TestActor actor("sensor", 1);
    reg.add(&actor);
    reg.remove(&actor);
    EXPECT_FALSE(reg.findByName("sensor").valid());
    EXPECT_FALSE(reg.findById(1).valid());
}

TEST(ActorRegistry, MultipleActors){
    ActorRegistry reg;
    TestActor a1("sensor", 1);
    TestActor a2("actuator", 2);
    TestActor a3("controller", 3);

    reg.add(&a1);
    reg.add(&a2);
    reg.add(&a3);

    EXPECT_EQ(reg.findByName("sensor").get(), &a1);
    EXPECT_EQ(reg.findByName("actuator").get(), &a2);
    EXPECT_EQ(reg.findByName("controller").get(), &a3);
    EXPECT_EQ(reg.findById(1).get(), &a1);
    EXPECT_EQ(reg.findById(2).get(), &a2);
    EXPECT_EQ(reg.findById(3).get(), &a3);
}

TEST(ActorRegistry, Clear){
    ActorRegistry reg;
    TestActor a1("sensor", 1);
    TestActor a2("actuator", 2);

    reg.add(&a1);
    reg.add(&a2);
    reg.clear();

    EXPECT_FALSE(reg.findByName("sensor").valid());
    EXPECT_FALSE(reg.findById(2).valid());
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

    reg.forEachActor([&](ActorHandle){ found++; });
    EXPECT_EQ(found, 100);
}
