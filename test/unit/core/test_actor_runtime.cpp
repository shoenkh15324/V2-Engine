#include <gtest/gtest.h>
#include "core/common/util/return.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/actor_system/runtime/actor_registry.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <memory>

namespace{

class TestActor : public Actor{
public:
    TestActor(const std::string& name, uint64_t id) : Actor(name, id){}

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message&) override { handleCount++; }

    int handleCount = 0;
};

} // namespace

// Construction

TEST(ActorRuntime, Create){
    auto actor = std::make_unique<TestActor>("a", 1);
    auto* a = actor.get();
    ActorRuntime ctx(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);

    EXPECT_EQ(ctx.actor(), a);
    EXPECT_EQ(ctx.mailboxCount(), 0);
    EXPECT_EQ(ctx.mailboxCapacity(), 64);
}

TEST(ActorRuntime, CreateWithRegistry){
    ActorRegistry reg;
    auto actor = std::make_unique<TestActor>("a", 1);
    auto* a = actor.get();
    ActorRuntime ctx(std::move(actor), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, &reg);
    reg.add(a);
    EXPECT_EQ(reg.findByName("a"), a);
    EXPECT_EQ(reg.findById(1), a);
}

// Enqueue

TEST(ActorRuntime, Enqueue){
    ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);
    ctx.enqueue(Tick{});
    EXPECT_EQ(ctx.mailboxCount(), 1);
}

TEST(ActorRuntime, EnqueueMultiple){
    ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);
    ctx.enqueue(Tick{});
    ctx.enqueue(Tick{});
    ctx.enqueue(Tick{});
    EXPECT_EQ(ctx.mailboxCount(), 3);
}

// Run

TEST(ActorRuntime, RunEmpty){
    ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);
    auto* a = static_cast<TestActor*>(ctx.actor());
    ctx.run(-1);
    EXPECT_EQ(a->handleCount, 0);
}

TEST(ActorRuntime, RunSingle){
    ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);
    auto* a = static_cast<TestActor*>(ctx.actor());
    ctx.enqueue(Tick{});
    ctx.run(-1);
    EXPECT_EQ(a->handleCount, 1);
    EXPECT_EQ(ctx.mailboxCount(), 0);
}

TEST(ActorRuntime, RunMultiple){
    ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, nullptr);
    auto* a = static_cast<TestActor*>(ctx.actor());
    ctx.enqueue(Tick{});
    ctx.enqueue(Tick{});
    ctx.enqueue(Tick{});
    ctx.run(-1);
    EXPECT_EQ(a->handleCount, 3);
}

TEST(ActorRuntime, RunMaxBatch){
    WorkDispatcher d(1);
    ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), &d, nullptr, nullptr);
    auto* a = static_cast<TestActor*>(ctx.actor());

    for(int i = 0; i < 5; i++){
        ctx.enqueue(Tick{});
    }
    ctx.run(2);
    EXPECT_EQ(a->handleCount, 2);
    EXPECT_EQ(ctx.mailboxCount(), 3);
}

TEST(ActorRuntime, RunAll){
    WorkDispatcher d(1);
    ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), &d, nullptr, nullptr);
    auto* a = static_cast<TestActor*>(ctx.actor());

    for(int i = 0; i < 5; i++){
        ctx.enqueue(Tick{});
    }
    ctx.run(-1);
    EXPECT_EQ(a->handleCount, 5);
    EXPECT_EQ(ctx.mailboxCount(), 0);
}

// Destructor

TEST(ActorRuntime, DestructorRemovesFromRegistry){
    ActorRegistry reg;
    {
        ActorRuntime ctx(std::make_unique<TestActor>("a", 1), std::make_unique<LockFreeMpscQueue<Message>>(64), nullptr, nullptr, &reg);
        reg.add(ctx.actor());
        EXPECT_NE(reg.findByName("a"), nullptr);
    }
    EXPECT_EQ(reg.findByName("a"), nullptr);
}
