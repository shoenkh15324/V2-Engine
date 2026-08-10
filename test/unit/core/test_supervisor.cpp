#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include "core/actor_system/runtime/supervisor/supervisor.hpp"
#include "core/actor_system/runtime/supervisor/dead_letter_queue.hpp"
#include "core/actor_system/runtime/supervisor/i_supervised.hpp"
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/core_messages.hpp"
#include "service/tick/tick_messages.hpp"
#include "core/actor_system/runtime/mailbox/mailbox.hpp"

namespace{

class LifecycleActor : public Actor{
public:
    LifecycleActor(const std::string& name, uint64_t id) : Actor(name, id){}

    int open() override { state_ = Opened; openCount++; return 0; }
    int close() override { state_ = Closed; closeCount++; return 0; }
    void handle(const Message&) override {}

    int openCount = 0;
    int closeCount = 0;
};

class MockSupervised : public ISupervised{
public:
    uint64_t id = 0;
    std::string name = "mock";
    std::atomic<int> restartCountVal{0};
    std::atomic<int> tryCalls{0};
    std::atomic<int> shutdownCalls{0};
    bool tryResult = true;
    std::vector<Message> pending;

    bool tryRestart(const std::string&, int maxRestarts) override {
        int prev = restartCountVal.load(std::memory_order_relaxed);
        while(true){
            if(prev >= maxRestarts) return false;
            if(restartCountVal.compare_exchange_weak(prev, prev + 1, std::memory_order_relaxed)) break;
        }
        tryCalls.fetch_add(1, std::memory_order_relaxed);
        return tryResult;
    }

    void shutdown() override {
        shutdownCalls.fetch_add(1, std::memory_order_relaxed);
    }

    bool popMessage(Message& msg) override {
        if(pending.empty()) return false;
        msg = std::move(pending.front());
        pending.erase(pending.begin());
        return true;
    }

    int restartCount() const override { return restartCountVal.load(std::memory_order_relaxed); }
    uint64_t actorId() const override { return id; }
    const std::string& actorName() const override { return name; }
};

std::unique_ptr<Supervisor> makeSupervisor(DeadLetterQueue& dlq, RestartStrategy def = RestartStrategy::OneForOne){
    auto sup = std::make_unique<Supervisor>(dlq);
    sup->setDefaultStrategy(def);
    return sup;
}

} // namespace

TEST(Supervisor, DefaultOneForOneRestarts){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq);
    MockSupervised mock;

    sup->onActorFailed(&mock, Message::make(Tick{}), "boom");

    EXPECT_EQ(sup->totalFailures(), 1);
    EXPECT_EQ(sup->totalRestarts(), 1);
    EXPECT_EQ(mock.restartCount(), 1);
    EXPECT_EQ(mock.shutdownCalls, 0);
}

TEST(Supervisor, PolicyOverridePerActor){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForOne);
    MockSupervised a, b;
    a.id = 1;
    b.id = 2;

    sup->setStrategy(1, RestartStrategy::None);
    sup->onActorFailed(&a, Message::make(Tick{}), "boom");
    sup->onActorFailed(&b, Message::make(Tick{}), "boom");

    EXPECT_EQ(a.shutdownCalls, 1);
    EXPECT_EQ(b.shutdownCalls, 0);
    EXPECT_EQ(sup->totalRestarts(), 1);
}

TEST(Supervisor, RemovePolicyFallsBackToDefault){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForOne);
    MockSupervised mock;
    mock.id = 1;

    sup->setStrategy(1, RestartStrategy::None);
    sup->removePolicy(1);
    sup->onActorFailed(&mock, Message::make(Tick{}), "boom");

    EXPECT_EQ(mock.shutdownCalls, 0);
    EXPECT_EQ(sup->totalRestarts(), 1);
}

TEST(Supervisor, NoneStrategyShutsDown){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::None);
    MockSupervised mock;

    sup->onActorFailed(&mock, Message::make(Tick{}), "boom");

    EXPECT_EQ(mock.shutdownCalls, 1);
    EXPECT_EQ(sup->totalRestarts(), 0);
    EXPECT_EQ(sup->totalFailures(), 1);
}

TEST(Supervisor, MaxRestartsBoundary){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForOne);
    sup->setMaxRestarts(5);
    MockSupervised mock;

    for(int i = 0; i < 6; i++){
        sup->onActorFailed(&mock, Message::make(Tick{}), "boom");
    }

    EXPECT_EQ(sup->totalRestarts(), 5);
    EXPECT_EQ(mock.restartCount(), 5);
}

TEST(Supervisor, ConcurrentMaxRestartsBoundary){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForOne);
    sup->setMaxRestarts(50);
    MockSupervised mock;

    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    for(int t = 0; t < kThreads; t++){
        threads.emplace_back([&]{
            for(int i = 0; i < 50; i++){
                sup->onActorFailed(&mock, Message::make(Tick{}), "boom");
            }
        });
    }
    for(auto& th : threads) th.join();

    EXPECT_EQ(sup->totalRestarts(), 50);
    EXPECT_EQ(mock.restartCount(), 50);
}

TEST(Supervisor, OneForAllBroadcastCount){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForAll);
    sup->setRestartAll([]() -> int { return 3; });
    MockSupervised mock;

    sup->onActorFailed(&mock, Message::make(Tick{}), "boom");

    EXPECT_EQ(sup->totalRestarts(), 3);
    EXPECT_EQ(sup->oneForAllBroadcasts(), 1);
    EXPECT_EQ(mock.shutdownCalls, 0);
}

TEST(Supervisor, OneForAllThrowingCallback){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForAll);
    sup->setRestartAll([]() -> int { throw std::runtime_error("cb"); });
    MockSupervised mock;

    EXPECT_NO_THROW(sup->onActorFailed(&mock, Message::make(Tick{}), "boom"));

    EXPECT_EQ(sup->totalRestarts(), 0);
    EXPECT_EQ(sup->oneForAllBroadcasts(), 1);
}

TEST(Supervisor, DeadLetterDrainsFailedAndPending){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq);
    MockSupervised mock;
    mock.pending.push_back(Message::make(Tick{}));
    mock.pending.push_back(Message::make(Tick{}));

    sup->onActorFailed(&mock, Message::make(Tick{}), "boom");

    EXPECT_EQ(sup->deadLetterCount(), 3);

    DeadLetter out;
    int ticks = 0;
    while(dlq.pop(out)){
        EXPECT_EQ(out.msg.id(), MessageId::Tick);
        ticks++;
    }
    EXPECT_EQ(ticks, 3);
}

TEST(Supervisor, ActorRestartRequestRestartsOpenedActor){
    auto actor = std::make_unique<LifecycleActor>("a", 1);
    auto* a = actor.get();
    a->open();
    ActorRuntime rt(std::move(actor), std::make_unique<Mailbox>(64), nullptr, nullptr, nullptr);

    ActorRestartRequest req;
    req.reason = "one-for-all";
    rt.enqueue(Message::make(std::move(req)));
    rt.run(-1);

    EXPECT_EQ(a->openCount, 2);
    EXPECT_EQ(a->closeCount, 1);
}

TEST(Supervisor, ActorRestartRequestSkipsClosedActor){
    auto actor = std::make_unique<LifecycleActor>("a", 1);
    auto* a = actor.get();
    ActorRuntime rt(std::move(actor), std::make_unique<Mailbox>(64), nullptr, nullptr, nullptr);

    ActorRestartRequest req;
    req.reason = "one-for-all";
    rt.enqueue(Message::make(std::move(req)));
    rt.run(-1);

    EXPECT_EQ(a->openCount, 0);
    EXPECT_EQ(a->closeCount, 0);
}

TEST(Supervisor, OneForOneExceededMaxRestartsShutdown){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForOne);
    sup->setMaxRestarts(2);
    MockSupervised mock;

    for(int i = 0; i < 3; i++){
        sup->onActorFailed(&mock, Message::make(Tick{}), "boom");
    }

    EXPECT_EQ(sup->totalRestarts(), 2);
    EXPECT_EQ(mock.restartCount(), 2);
    EXPECT_EQ(mock.shutdownCalls, 1);
}

TEST(Supervisor, OneForAllExceededMaxRestartsShutdown){
    DeadLetterQueue dlq;
    auto sup = makeSupervisor(dlq, RestartStrategy::OneForAll);
    sup->setMaxRestarts(2);
    sup->setRestartAll([]() -> int { return 1; });
    MockSupervised mock;

    for(int i = 0; i < 3; i++){
        sup->onActorFailed(&mock, Message::make(Tick{}), "boom");
    }

    EXPECT_EQ(sup->oneForAllBroadcasts(), 2);  // 2회까지만 브로드캐스트
    EXPECT_EQ(sup->totalRestarts(), 2);
    EXPECT_EQ(mock.shutdownCalls, 1);
}

TEST(Supervisor, ShutdownRuntimeStopsProcessing){
    auto actor = std::make_unique<LifecycleActor>("a", 1);
    auto* a = actor.get();
    a->open();
    ActorRuntime rt(std::move(actor), std::make_unique<Mailbox>(64), nullptr, nullptr, nullptr);

    rt.shutdown();
    rt.enqueue(Message::make(Tick{}));
    int processed = rt.run(-1);

    EXPECT_EQ(processed, 0);
    EXPECT_EQ(a->getState(), Closed);
}
