#include "work_dispatcher.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/perf/metrics/metrics.hpp"

WorkDispatcher::WorkDispatcher(int workerCount) : workerCount_(workerCount){
    for(int i = 0; i < workerCount; i++){
        queues_.push_back(std::make_unique<LockFreeMpscQueue<ActorRuntime*>>(1024));
        semas_.push_back(std::make_unique<std::counting_semaphore<>>(0));
    }
}

WorkDispatcher::~WorkDispatcher(){
    stop();
}

void WorkDispatcher::start(){
    running_ = true;
}

void WorkDispatcher::stop(){
    running_ = false;
    for(int i = 0; i < workerCount_; i++){
        semas_[i]->release();
    }
}

void WorkDispatcher::dispatch(ActorRuntime* actorRuntime){
    uint64_t actorId = actorRuntime->actor()->id();
    int workerId = static_cast<int>(actorId % workerCount_);
    queues_[workerId]->push(std::move(actorRuntime));
    Metrics::recordDispatch(false, queues_[workerId]->count());
    semas_[workerId]->release();
}

ActorRuntime* WorkDispatcher::acquire(int workerId){
    semas_[workerId]->acquire();
    ActorRuntime* ctx = nullptr;
    queues_[workerId]->pop(ctx);
    if(ctx) Metrics::recordAcquire();
    return ctx;
}
