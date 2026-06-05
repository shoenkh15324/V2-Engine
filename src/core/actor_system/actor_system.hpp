#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/actor_system/scheduler.hpp"
#include "core/actor_system/runtime/actor_context.hpp"

class Actor;
class Worker;

class ActorSystem{
public:
    explicit ActorSystem(int numWorkers = 1);
    ~ActorSystem();
    
    template<typename T, typename ... Args>
    Actor* createActor(const std::string& name, size_t mailboxSize = 64, Args&& ... args){
        static_assert(std::is_base_of_v<Actor, T>, "T must derive from Actor");
        auto actor = std::make_unique<T>(name, nextActorId_++, std::forward<Args>(args)...);
        auto actorCtx = std::make_unique<ActorContext>(std::move(actor), mailboxSize);
        actorCtx->attachDispatcher(&dispatcher_);
        Actor* ptr = actorCtx->actor();
        actorContexts_.push_back(std::move(actorCtx));
        return ptr;
    }

    void start();
    void stop();
    Scheduler& scheduler(){ return scheduler_; }

private:
    Dispatcher dispatcher_;
    Scheduler scheduler_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::unique_ptr<ActorContext>> actorContexts_;
    std::atomic<uint64_t> nextActorId_{0};
};
