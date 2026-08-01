#include <gtest/gtest.h>
#include "core/actor_system/runtime/scheduler.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/actor_system/runtime/actor_registry.hpp"
#include "core/actor_system/runtime/dispatcher/io/event_loop_epoll.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
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
    ActorRuntime rt(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);
    int id = sched.addTimer(&rt, Message::make(Tick{}), 100, false);
    EXPECT_GT(id, 0);
}

TEST(Scheduler, Cancel){
    Scheduler sched;
    auto actor = std::make_unique<TestActor>("t", 1);
    ActorRuntime rt(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);
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
    EventLoopEpoll loop;
    loop.start();
    std::thread loopThread([&loop]{ loop.run(); });

    Scheduler sched;
    sched.start(&loop);

    auto actor = std::make_unique<TestActor>("stress", 1);
    ActorRuntime rt(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(4096), nullptr, &sched, nullptr);

    constexpr int kThreads = 4;
    constexpr int kIters = 5000;
    std::vector<std::thread> workers;
    std::atomic<bool> keepFiring{true};

    // 반복 타이머가 event loop 스레드에서 계속 fire되는 동안 add/cancel 반복
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
    loop.stop();
    loopThread.join();
}
