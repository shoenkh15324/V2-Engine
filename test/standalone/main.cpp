#include "mock_event_loop.hpp"
#include <atomic>
#include <memory>
#include <cstdio>
#include <thread>
#include "core/common/time/sleep.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/actor_system/messages/message.hpp"
#include "core/actor_system/messages/system_messages.hpp"

class SmokeActor : public Actor {
public:
    SmokeActor(const std::string& name, uint64_t id) : Actor(name, id) {}
    int open() override { return Ok; }
    int close() override { return Ok; }
    void handle(const Message& msg) override {
        if(msg.id() == MessageId::SignalNotify) handled_.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<int> handled_{0};
};

int main(){
    auto loop = std::make_unique<MockEventLoop>();
    auto sys = createDefaultActorSystem({1, 32}, std::move(loop));   // timer=null → core portable Timer
    auto* a = sys->createActor<SmokeActor>("smoke", 64);
    sys->start();

    std::thread t([&]{ sys->run(); });

    a->sendMsg("smoke", Message::make(SignalNotify{1}));            // 메시징
    a->startTimer(Message::make(SignalNotify{2}), 20, true);        // 타이머 (반복)
    Sleep::sleepMs(100);

    sys->requestStop();
    t.join();

    int n = a->handled_.load(std::memory_order_relaxed);
    std::printf("smoke handled: %d\n", n);                          // 예상: 1(직접) + ~4(타이머)
    return (n >= 3) ? 0 : 1;
}
