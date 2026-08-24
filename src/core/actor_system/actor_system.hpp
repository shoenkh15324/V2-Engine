#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <cstddef>
#include <utility>
#include <type_traits>
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "core/actor_system/runtime/scheduler/i_scheduler.hpp"
#include "core/actor_system/runtime/supervisor/i_supervisor.hpp"
#include "core/actor_system/runtime/supervisor/i_dead_letter_queue.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/timer/i_timer.hpp"

class ActorSystem;
class Worker;
class ActorRuntime;

struct ActorSystemConfig {
    int numWorkers = 4;
    int maxBatch = 32;
    int busyStealIntervalUs = 200;
    int idleStealIntervalUs = 2000;
    int parkSpinNs = 3000;
    int tokenGraceNs = 5000;
    size_t defaultMailboxSize = 512;
    int dispatcherQueueCapacity = 1024;
    int dispatcherHighWatermark = 0; // 0이면 kDefaultHighWatermark 자동 계산
    int supervisorMaxRestarts = 5;
    int supervisorDefaultStrategy = 0;
    int deadLetterQueueCapacity = 128;
};

struct ActorSystemDeps {
    std::unique_ptr<IEventLoop> eventLoop;
    std::unique_ptr<IScheduler> scheduler;
    std::unique_ptr<ISupervisor> supervisor;
    std::unique_ptr<IActorRegistry> registry;
    std::unique_ptr<IWorkDispatcher> dispatcher;
    std::unique_ptr<IDeadLetterQueue> deadLetterQueue;
};

std::unique_ptr<ActorSystem> createDefaultActorSystem(
    const ActorSystemConfig& config,
    std::unique_ptr<IEventLoop> eventLoop,
    std::unique_ptr<ITimer> timer = nullptr
);

class ActorSystem {
public:
    ActorSystem(const ActorSystemConfig& config, ActorSystemDeps deps);
    ~ActorSystem();
    
    ActorSystem(const ActorSystem&) = delete;
    ActorSystem& operator=(const ActorSystem&) = delete;
    ActorSystem(ActorSystem&&) = delete;
    ActorSystem& operator=(ActorSystem&&) = delete;

    template<typename T, typename ... Args>
    T* createActor(const std::string& name, size_t mailboxSize = 0, Args&& ... args){
        static_assert(std::is_base_of_v<Actor, T>, "T must derive from Actor");
        if(mailboxSize == 0) mailboxSize = defaultMailboxSize_;
        uint64_t id = nextActorId_++;
        auto actor = std::make_unique<T>(std::move(name), id, std::forward<Args>(args)...);
        T* raw = actor.get();
        attachActor(std::move(actor), mailboxSize, id);
        return raw;
    }

    void start();
    void stop();
    void run();
    void requestStop();

private:
    // ActorRuntime + mailbox 생성은 .cpp의 attachActor()에서 (헤더에서 concrete 제거 목적)
    void attachActor(std::unique_ptr<Actor> actor, size_t mailboxSize, uint64_t id);

    std::unique_ptr<IDeadLetterQueue> deadLetterQueue_;
    std::unique_ptr<ISupervisor> supervisor_;
    std::unique_ptr<IWorkDispatcher> dispatcher_;
    std::unique_ptr<IScheduler> scheduler_;
    std::unique_ptr<IActorRegistry> registry_;
    std::unique_ptr<IEventLoop> eventLoop_;
    size_t defaultMailboxSize_ = 512;
    int maxBatch_ = 32;
    std::atomic<uint64_t> nextActorId_{0};
    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::unique_ptr<ActorRuntime>> actorRuntimes_;

};
