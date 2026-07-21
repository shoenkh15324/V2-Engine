#include <gtest/gtest.h>
#include "core/actor_system/runtime/scheduler.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/actor_system/runtime/actor_registry.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <memory>

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
    auto actor = std::make_unique<TestActor>("t", 1);
    ActorRuntime rt(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);
    int id = sched.addTimer(&rt, Tick{}, 100, false);
    EXPECT_GT(id, 0);
}

TEST(Scheduler, Cancel){
    Scheduler sched;
    auto actor = std::make_unique<TestActor>("t", 1);
    ActorRuntime rt(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);
    int id = sched.addTimer(&rt, Tick{}, 100, false);
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
