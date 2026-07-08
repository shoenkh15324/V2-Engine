#include <gtest/gtest.h>
#include "core/actor_system/runtime/scheduler.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/tick_messages.hpp"

namespace{

class TestActor : public Actor{
public:
    TestActor(const std::string& name, uint64_t id) : Actor(name, id){}

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message&) override {}
};

} // namespace

TEST(Scheduler, Create){
    Scheduler sched;
}

TEST(Scheduler, AddTimer){
    Scheduler sched;
    TestActor a("t", 1);
    int id = sched.addTimer(&a, Tick{}, 100, false);
    EXPECT_GT(id, 0);
}

TEST(Scheduler, Cancel){
    Scheduler sched;
    TestActor a("t", 1);
    int id = sched.addTimer(&a, Tick{}, 100, false);
    EXPECT_GT(id, 0);
    sched.cancel(id);
}

TEST(Scheduler, CancelInvalidId){
    Scheduler sched;
    sched.cancel(999); // no crash
}

TEST(Scheduler, StartStop){
    Scheduler sched;
    sched.start();
    sched.stop();
}
