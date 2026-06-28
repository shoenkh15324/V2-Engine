#include "actor_system.hpp"
#include "core/actor_system/runtime/worker.hpp"
#include "core/actor_system/actor/actor.hpp"

ActorSystem::ActorSystem(int numWorkers, int maxBatch, int epollMaxEvents, int epollWaitTimeoutMs)
    : dispatcher_(numWorkers, epollMaxEvents, epollWaitTimeoutMs){
    workers_.reserve(numWorkers);
    for(int i = 0; i < numWorkers; i++){
        workers_.push_back(std::make_unique<Worker>(&dispatcher_, i, maxBatch));
    }
}

ActorSystem::~ActorSystem(){
    actorRegistry_.clear();
    stop();
}

void ActorSystem::start(){
    dispatcher_.start();
    scheduler_.start(&dispatcher_);
    for(auto& ctx : actorContexts_){
        ctx->actor()->open();
    }
    for(auto& w : workers_){
        w->start();
    }
}

void ActorSystem::stop(){
    for(auto& ctx : actorContexts_){
        ctx->actor()->close();
    }
    scheduler_.stop();
    dispatcher_.stop();
    for(auto& w : workers_){
        w->stop();
    }
}

void ActorSystem::run(){
#if V2_PLATFORM_LINUX
    dispatcher_.run();
#endif
}

void ActorSystem::requestStop(){
    for(auto& ctx : actorContexts_){
        ctx->actor()->close();
    }
    scheduler_.stop();
    dispatcher_.stop();
    for(auto& w : workers_){
        w->stop();
    }
}
