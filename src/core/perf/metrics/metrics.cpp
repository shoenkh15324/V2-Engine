#include "metrics.hpp"

void Metrics::init(size_t numWorkers){
    workers_.clear();
    workers_.reserve(numWorkers);
    for(size_t i = 0; i < numWorkers; i++){
        workers_.push_back(std::make_unique<WorkerMetrics>());
    }
}

void Metrics::registerActor(uint64_t actorId){
    while(actorId >= actors_.size()){
        actors_.push_back(std::make_unique<ActorMetrics>());
    }
}

void Metrics::recordEnqueue(uint64_t actorId, bool success, size_t depth){
    if(!enabled_) return;
    if(actorId >= actors_.size()) return;
    auto& m = *actors_[actorId];
    if(success){
        m.enqueued.fetch_add(1, std::memory_order_relaxed);
        updatePeak(m.peakDepth, depth);
    }else{
        m.dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void Metrics::recordHandle(uint64_t actorId, size_t count, uint64_t durationNs){
    if(!enabled_) return;
    if(actorId >= actors_.size()) return;
    auto& m = *actors_[actorId];
    m.processed.fetch_add(count, std::memory_order_relaxed);
    m.handleTimeNs.fetch_add(durationNs, std::memory_order_relaxed);
    m.batches.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::recordBatch(int workerId, size_t msgCount, uint64_t busyNs, uint64_t idleNs){
    if(!enabled_) return;
    if(workerId < 0 || static_cast<size_t>(workerId) >= workers_.size()) return;
    auto& w = *workers_[workerId];
    w.batches.fetch_add(1, std::memory_order_relaxed);
    w.busyTimeNs.fetch_add(busyNs, std::memory_order_relaxed);
    w.idleTimeNs.fetch_add(idleNs, std::memory_order_relaxed);
    w.messages.fetch_add(msgCount, std::memory_order_relaxed);
}

void Metrics::recordDispatch(bool deduped, size_t queueDepth){
    if(!enabled_) return;
    dispatcher_.dispatchCount.fetch_add(1, std::memory_order_relaxed);
    if(deduped){
        dispatcher_.deduplicated.fetch_add(1, std::memory_order_relaxed);
    }
    updatePeak(dispatcher_.readyQueuePeak, queueDepth);
}

void Metrics::recordAcquire(){
    if(!enabled_) return;
    dispatcher_.acquireCount.fetch_add(1, std::memory_order_relaxed);
}

Metrics::Snapshot Metrics::snapshot(){
    Snapshot snap;
    snap.actors.reserve(actors_.size());
    for(size_t i = 0; i < actors_.size(); i++){
        snap.actors.push_back({i, "", actors_[i]->snapshot()});
    }
    snap.workers.reserve(workers_.size());
    for(auto& w : workers_){
        snap.workers.push_back(w->snapshot());
    }
    snap.dispatcher = dispatcher_.snapshot();
    return snap;
}

void Metrics::reset(){
    for(auto& m : actors_){
        m->reset();
    }
    for(auto& w : workers_){
        w->reset();
    }
    dispatcher_.reset();
}

void Metrics::updatePeak(std::atomic<size_t>& peak, size_t current){
    size_t p = peak.load(std::memory_order_relaxed);
    while(current > p){
        if(peak.compare_exchange_weak(p, current, std::memory_order_relaxed)){
            break;
        }
    }
}
