#include <gtest/gtest.h>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/actor_system/runtime/actor_registry.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include "core/common/util/return.hpp"
#include <memory>

namespace{

class TestActor : public Actor{
public:
    TestActor(const std::string& name, uint64_t id) : Actor(name, id){}

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message&) override { handled = true; }

    bool handled = false;
};

struct TestScheduler : IScheduler{
    TestScheduler() = default;

    int addTimer(IActorRuntime* target, Message, uint64_t delayMs, bool repeating) override{
        lastTarget = target;
        lastDelayMs = delayMs;
        lastRepeating = repeating;
        addedCount++;
        return nextId++;
    }

    void cancel(int timerId) override{
        cancelledIds.push_back(timerId);
        cancelCount++;
    }

    IActorRuntime* lastTarget = nullptr;
    uint64_t lastDelayMs = 0;
    bool lastRepeating = false;
    int nextId = 1;
    int addedCount = 0;
    int cancelCount = 0;
    std::vector<int> cancelledIds;
};

} // namespace

// Construction

TEST(Actor, Create){
    TestActor a("sensor", 1);
    EXPECT_EQ(a.name(), "sensor");
    EXPECT_EQ(a.id(), 1);
    EXPECT_EQ(a.getState(), Closed);
}

TEST(Actor, CreateDefaultId){
    TestActor a("no_id", static_cast<uint64_t>(-1));
    EXPECT_EQ(a.name(), "no_id");
}

TEST(Actor, Essential){
    TestActor a("essentail", 1);
    EXPECT_FALSE(a.isEssential());
    a.setEssential(true);
    EXPECT_TRUE(a.isEssential());
}

// Messaging

TEST(Actor, SendMsgByName){
    ActorRegistry reg;

    auto targetActor = std::make_unique<TestActor>("target", 2);
    auto* target = targetActor.get();
    auto targetCtx = std::make_unique<ActorRuntime>(std::move(targetActor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, &reg);
    reg.add(target);
    auto senderActor = std::make_unique<TestActor>("sender", 1);
    auto* sender = senderActor.get();
    auto senderCtx = std::make_unique<ActorRuntime>(std::move(senderActor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, &reg);
    reg.add(sender);

    sender->sendMsg("target", Tick{});
    EXPECT_EQ(target->mailboxCount(), 1);
}

TEST(Actor, SendMsgById){
    ActorRegistry reg;

    auto targetActor = std::make_unique<TestActor>("target", 42);
    auto* target = targetActor.get();
    auto targetCtx = std::make_unique<ActorRuntime>(std::move(targetActor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, &reg);
    reg.add(target);
    auto senderActor = std::make_unique<TestActor>("sender", 1);
    auto* sender = senderActor.get();
    auto senderCtx = std::make_unique<ActorRuntime>(std::move(senderActor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, &reg);
    reg.add(sender);

    sender->sendMsg(uint64_t(42), Tick{});
    EXPECT_EQ(target->mailboxCount(), 1);
}

TEST(Actor, SendMsgUnknown){
    ActorRegistry reg;

    auto senderActor = std::make_unique<TestActor>("sender", 1);
    auto* sender = senderActor.get();
    auto senderCtx = std::make_unique<ActorRuntime>(std::move(senderActor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, &reg);
    reg.add(sender);

    sender->sendMsg("nobody", Tick{}); // should not crash
}

TEST(Actor, ReceiveMsg){
    auto actor = std::make_unique<TestActor>("a", 1);
    auto* a = actor.get();
    auto ctx = std::make_unique<ActorRuntime>(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);

    EXPECT_EQ(a->mailboxCount(), 0);
    a->receiveMsg(Tick{});
    EXPECT_EQ(a->mailboxCount(), 1);
    a->receiveMsg(Tick{});
    EXPECT_EQ(a->mailboxCount(), 2);
}

// Timer

TEST(Actor, StartTimer){
    TestScheduler sched;
    auto actor = std::make_unique<TestActor>("a", 1);
    auto* a = actor.get();
    auto ctx = std::make_unique<ActorRuntime>(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);

    int id = a->startTimer(Tick{}, 100, false);
    EXPECT_GT(id, 0);
    EXPECT_EQ(sched.addedCount, 1);
    EXPECT_EQ(sched.lastTarget, static_cast<IActorRuntime*>(ctx.get()));
    EXPECT_EQ(sched.lastDelayMs, 100);
    EXPECT_EQ(sched.lastRepeating, false);
    EXPECT_EQ(a->timerCount(), 1);
}

TEST(Actor, StartTimerRepeating){
    TestScheduler sched;
    auto actor = std::make_unique<TestActor>("a", 1);
    auto* a = actor.get();
    auto ctx = std::make_unique<ActorRuntime>(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);

    int id = a->startTimer(Tick{}, 100, true);
    EXPECT_GT(id, 0);
    EXPECT_EQ(sched.lastDelayMs, 100);
    EXPECT_EQ(sched.lastRepeating, true);
}

TEST(Actor, StartTimerNoScheduler){
    auto actor = std::make_unique<TestActor>("a", 1);
    auto* a = actor.get();
    auto ctx = std::make_unique<ActorRuntime>(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);

    int id = a->startTimer(Tick{}, 100, false);
    EXPECT_EQ(id, Fail);
}

TEST(Actor, CancelTimer){
    TestScheduler sched;
    auto actor = std::make_unique<TestActor>("a", 1);
    auto* a = actor.get();
    auto ctx = std::make_unique<ActorRuntime>(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);

    int id = a->startTimer(Tick{}, 100, false);
    EXPECT_EQ(a->timerCount(), 1);

    a->cancelTimer(id);
    EXPECT_EQ(sched.cancelCount, 1);
    ASSERT_EQ(sched.cancelledIds.size(), 1);
    EXPECT_EQ(sched.cancelledIds[0], id);
    EXPECT_EQ(a->timerCount(), 0);
}

TEST(Actor, DestructorCancelsTimers){
    TestScheduler sched;
    {
        auto actor = std::make_unique<TestActor>("a", 1);
        auto ctx = std::make_unique<ActorRuntime>(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, &sched, nullptr);
        auto* a = static_cast<TestActor*>(ctx->actor());

        a->startTimer(Tick{}, 100, false);
        a->startTimer(Tick{}, 200, true);
        EXPECT_EQ(a->timerCount(), 2);
        EXPECT_EQ(sched.addedCount, 2);
    } // ctx destroyed → Actor destroyed → ~Actor cancels all timers
    EXPECT_EQ(sched.cancelCount, 2);
}
