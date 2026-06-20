#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include "core/actor_system/actor/actor_registry.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/actor_system/runtime/scheduler.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/common/config.h"
#include "core/common/log.hpp"

class Worker;

class ActorSystem{
public:
    explicit ActorSystem(int numWorkers = 1);
    ~ActorSystem();
    ActorSystem(const ActorSystem&) = delete;
    ActorSystem& operator=(const ActorSystem&) = delete;
    ActorSystem(ActorSystem&&) = delete;
    ActorSystem& operator=(ActorSystem&&) = delete;

    template<typename T, typename ... Args>
    T* createActor(const std::string& name, size_t mailboxSize = V2_DEFAULT_MAILBOX_SIZE, Args&& ... args){
        static_assert(std::is_base_of_v<Actor, T>, "T must derive from Actor");
        auto actor = std::make_unique<T>(name, nextActorId_++, std::forward<Args>(args)...);
        auto actorCtx = std::make_unique<ActorContext>(std::move(actor), mailboxSize, &dispatcher_, &scheduler_, &actorRegistry_);
        T* ptr = static_cast<T*>(actorCtx->actor());
        actorRegistry_.add(ptr);
        actorContexts_.push_back(std::move(actorCtx));
        V2_LOG_INFO("Create %s actor / mailbox: %d", name.c_str(), mailboxSize);
        return ptr;
    }

    void start();
    void stop();
    void run();
    void requestStop();

private:
    Dispatcher dispatcher_;
    Scheduler scheduler_;
    ActorRegistry actorRegistry_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::unique_ptr<ActorContext>> actorContexts_;
    std::atomic<uint64_t> nextActorId_{0};
};
