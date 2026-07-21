#include "actor_system.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/runtime/dispatcher/worker.hpp"
#include "core/actor_system/actor/actor.hpp"

ActorSystem::ActorSystem(int numWorkers, int maxBatch, int epollMaxEvents, int epollWaitTimeoutMs) : workDispatcher_(numWorkers), eventLoop_(epollMaxEvents, epollWaitTimeoutMs){
    Metrics::init(numWorkers);
    workers_.reserve(numWorkers);
    for(int i = 0; i < numWorkers; i++){
        workers_.push_back(std::make_unique<Worker>(&workDispatcher_, i, maxBatch));
    }
}

ActorSystem::~ActorSystem(){
    actorRegistry_.clear();
    stop();
}

void ActorSystem::start(){
    workDispatcher_.start();
    eventLoop_.start();
    scheduler_.start(&eventLoop_);
    for(auto& ctx : actorRuntimes_){
        int ret = ctx->actor()->open();
        if(ret != Ok) V2_LOG_ERROR("Actor %s failed to open", ctx->actor()->name().c_str());
    }
    for(auto& w : workers_){
        w->start();
    }
}

void ActorSystem::stop(){
    for(auto& ctx : actorRuntimes_){
        ctx->actor()->close();
    }
    scheduler_.stop();
    eventLoop_.stop();
    workDispatcher_.stop();
    for(auto& w : workers_){
        w->stop();
    }
}

void ActorSystem::run(){
    eventLoop_.run();
}

void ActorSystem::requestStop(){
    stop();
}
