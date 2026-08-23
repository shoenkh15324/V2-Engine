#include "work_dispatcher.hpp"
#include <chrono>
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/perf/metrics/metrics.hpp"

WorkDispatcher::WorkDispatcher(
    int workerCount,
    int queueCapacity,
    int highWatermark,
    int busyStealIntervalUs,
    int idleStealIntervalUs
) : workerCount_(workerCount),
    queueCapacity_(queueCapacity),
    highWatermark_(highWatermark),
    busyStealIntervalUs_(busyStealIntervalUs),
    idleStealIntervalUs_(idleStealIntervalUs)    
{
    for(int i = 0; i < workerCount; i++){
        queues_.push_back(std::make_unique<LockFreeMpmcQueue<ActorRuntime*>>(queueCapacity_));
        semas_.push_back(std::make_unique<std::counting_semaphore<>>(0));
    }
    idleBackoff_.assign(workerCount, 0);
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
    std::lock_guard lock(mutex_);
    pendingActorList_.clear();
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
    ActorRuntime* ctx = nullptr;
    if(tryAcquireOwn(workerId, ctx)){
        idleBackoff_[workerId] = 0;
        V2_METRICS()->recordAcquire();
        return ctx;
    }
    while(running_.load(std::memory_order_relaxed) || draining_.load(std::memory_order_relaxed)){
        if(draining_.load(std::memory_order_relaxed) && (pendingWork_.load(std::memory_order_relaxed) == 0)){
            break;
        }
        auto interval = idleBackoff_[workerId] ? idleStealIntervalUs_ : busyStealIntervalUs_;
        if(semas_[workerId]->try_acquire_for(std::chrono::microseconds(interval))){
            if(tryAcquireOwn(workerId, ctx)){
                idleBackoff_[workerId] = 0;
                V2_METRICS()->recordAcquire();
                return ctx;
            }
            continue;
        }
        if(trySteal(workerId, ctx)){
            idleBackoff_[workerId] = 0;
            V2_METRICS()->recordAcquire();
            V2_METRICS()->recordSteal(true);
            return ctx;
        }
        idleBackoff_[workerId] = 1;
        V2_METRICS()->recordSteal(false);
    }
    return nullptr;
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
    if(!queues_[workerId]->push(std::move(actorRuntime))){
        std::lock_guard lock(mutex_);
        pendingActorList_.push_back(actorRuntime);
        return false;
    }
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

bool WorkDispatcher::tryAcquireOwn(int workerId, ActorRuntime*& out){
    return queues_[workerId]->pop(out);
}

bool WorkDispatcher::trySteal(int workerId, ActorRuntime*& out){
    for(int i = 1; i < workerCount_; i++){
        int  victim = (workerId + i) % workerCount_;
        if(queues_[victim]->empty()) continue;
        if(queues_[victim]->pop(out)){
            semas_[victim]->try_acquire();
            return true;
        }
    }
    return false;
}

void WorkDispatcher::drainPendedActor(){
    std::lock_guard lock(mutex_);
    for(auto it = pendingActorList_.begin(); it != pendingActorList_.end();){
        ActorRuntime* rt = *it;
        int workerId = pickWorker(rt->actor()->id());
        if(queues_[workerId]->push(std::move(rt))){
            semas_[workerId]->release();
            it = pendingActorList_.erase(it);
        }else{
            ++it;
        }
    }
}
