#include "work_dispatcher.hpp"
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/perf/metrics/metrics.hpp"

WorkDispatcher::WorkDispatcher(int workerCount, int highWatermark) : workerCount_(workerCount), highWatermark_(highWatermark){
    for(int i = 0; i < workerCount; i++){
        queues_.push_back(std::make_unique<LockFreeMpscQueue<ActorRuntime*>>(kQueueCapacity));
        semas_.push_back(std::make_unique<std::counting_semaphore<>>(0));
    }
}

WorkDispatcher::~WorkDispatcher(){
    stop();
}

void WorkDispatcher::start(){
    running_.store(true, std::memory_order_release);
}

void WorkDispatcher::stop(){
    running_.store(false, std::memory_order_release);
    draining_.store(false, std::memory_order_release);
    for(int i = 0; i < workerCount_; i++){
        semas_[i]->release();
    }
}

bool WorkDispatcher::dispatch(ActorRuntime* actorRuntime){
    bool ok = enqueueEntry(actorRuntime);
    if(ok) pendingWork_.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

bool WorkDispatcher::redispatch(ActorRuntime* actorRuntime){
    return enqueueEntry(actorRuntime);
}

ActorRuntime* WorkDispatcher::acquire(int workerId){
    semas_[workerId]->acquire();
    ActorRuntime* ctx = nullptr;
    queues_[workerId]->pop(ctx);
    if(ctx) V2_METRICS()->recordAcquire();
    return ctx;
}

void WorkDispatcher::beginDrain(){
    running_.store(false, std::memory_order_release);
    draining_.store(true, std::memory_order_release);
    for(int i = 0; i < workerCount_; i++){
        semas_[i]->release();
    }
}

void WorkDispatcher::onWorkDone(){
    if(pendingWork_.fetch_sub(1, std::memory_order_acq_rel) == 1){
        if(draining_.load(std::memory_order_acquire)){
            for(int i = 0; i < workerCount_; i++) semas_[i]->release();
        }
    }
}

bool WorkDispatcher::enqueueEntry(ActorRuntime* actorRuntime){
    uint64_t actorId = actorRuntime->actor()->id();
    int workerId = pickWorker(actorId);
    if(!queues_[workerId]->push(std::move(actorRuntime))) return false;
    V2_METRICS()->recordDispatch(false, queues_[workerId]->count());
    semas_[workerId]->release();
    return true;
}

int WorkDispatcher::pickWorker(uint64_t actorId){
    int home = static_cast<int>(actorId % workerCount_);
    if(workerCount_ <= 1) return home;
    if(queues_[home]->count() < static_cast<size_t>(highWatermark_)) return home;
    return pickLeastLoaded(actorId);
}

int WorkDispatcher::pickLeastLoaded(uint64_t actorId){
    int best = static_cast<int>(actorId % workerCount_);
    size_t bestCount = queues_[best]->count();
    for(int i = 0; i < workerCount_; i++){
        int w = static_cast<int>((best + i) % workerCount_);
        size_t c = queues_[w]->count();
        if(c < bestCount){
            best = w;
            bestCount = c;
            if(bestCount == 0) break;
        }
    }
    return best;
}
