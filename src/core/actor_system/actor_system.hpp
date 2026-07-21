#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include "core/actor_system/runtime/actor_registry.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/actor_system/runtime/dispatcher/io/event_loop_epoll.hpp"
#include "core/actor_system/runtime/scheduler.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/debug.hpp"
#include "core/perf/metrics/metrics.hpp"

class Worker;

class ActorSystem{
public:
    explicit ActorSystem(int numWorkers, int maxBatch = 32, int epollMaxEvents = 64, int epollWaitTimeoutMs = 1000);
    ~ActorSystem();
    
    ActorSystem(const ActorSystem&) = delete;
    ActorSystem& operator=(const ActorSystem&) = delete;
    ActorSystem(ActorSystem&&) = delete;
    ActorSystem& operator=(ActorSystem&&) = delete;

    template<typename T, typename ... Args>
    T* createActor(const std::string& name, size_t mailboxSize = 512, Args&& ... args){
        return createActorImpl<T>(name, mailboxSize, std::forward<Args>(args)...);
    }

    void start();
    void stop();
    void run();
    void requestStop();

    int enableActor(const std::string& name);
    int disableActor(const std::string& name);

private:
    template <typename T, typename ... Args>
    T* createActorImpl(const std::string& name, size_t mailboxSize, Args&& ... args){
        V2_ASSERT((std::is_base_of_v<Actor, T>), "T must derive from Actor");
        uint64_t id = nextActorId_++;
        auto actor = std::make_unique<T>(name, id, std::forward<Args>(args)...);
        auto mailbox = std::make_unique<LockFreeMpscQueue<Message>>(mailboxSize);
        auto actorRuntime = std::make_unique<ActorRuntime>(std::move(actor), std::move(mailbox), &workDispatcher_, &scheduler_, &actorRegistry_, &eventLoop_);
        T* ptr = static_cast<T*>(actorRuntime->actor());
        actorRegistry_.add(ptr);
        actorRuntimes_.push_back(std::move(actorRuntime));
        Metrics::registerActor(id);
        V2_LOG_INFO("Create actor / name: %s, id: %d, mailbox: %d", name.c_str(), id, mailboxSize);
        return ptr;
    }

    WorkDispatcher workDispatcher_;
    EventLoopEpoll eventLoop_;
    Scheduler scheduler_;
    ActorRegistry actorRegistry_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::unique_ptr<ActorRuntime>> actorRuntimes_;
    std::atomic<uint64_t> nextActorId_{0};
};
