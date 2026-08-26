#include <gtest/gtest.h>
#include "core/actor_system/runtime/scheduler/scheduler.hpp"
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"
#include "core/actor_system/actor/actor_registry.hpp"
#include "core/actor_system/runtime/mailbox/mailbox.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "service/tick/tick_messages.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

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
    ActorRuntime rt(std::move(actor), std::make_unique<Mailbox>(64), {.scheduler = &sched});
    int id = sched.addTimer(&rt, Message::make(Tick{}), 100, false);
    EXPECT_GT(id, 0);
}

TEST(Scheduler, Cancel){
    Scheduler sched;
    auto actor = std::make_unique<TestActor>("t", 1);
    ActorRuntime rt(std::move(actor), std::make_unique<Mailbox>(64), {.scheduler = &sched});
    int id = sched.addTimer(&rt, Message::make(Tick{}), 100, false);
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

TEST(Scheduler, ConcurrentAddCancelDuringFire){
    Scheduler sched;
    sched.start();

    auto actor = std::make_unique<TestActor>("stress", 1);
    ActorRuntime rt(std::move(actor), std::make_unique<Mailbox>(4096), {.scheduler = &sched});

    constexpr int kThreads = 4;
    constexpr int kIters = 5000;
    std::vector<std::thread> workers;
    std::atomic<bool> keepFiring{true};

    // 반복 타이머가 타이머 스레드에서 계속 fire되는 동안 add/cancel 반복
    std::thread fireThread([&]{
        while(keepFiring.load()){
            int id = sched.addTimer(&rt, Message::make(Tick{}), 1, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            sched.cancel(id);
        }
    });

    for(int t = 0; t < kThreads; ++t){
        workers.emplace_back([&]{
            for(int i = 0; i < kIters; ++i){
                int id = sched.addTimer(&rt, Message::make(Tick{}), 1, false);
                sched.cancel(id);
            }
        });
    }

    for(auto& w : workers) w.join();
    keepFiring.store(false);
    fireThread.join();

    sched.stop();
}
