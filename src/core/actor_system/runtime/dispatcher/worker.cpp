#include "worker.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/time/time.hpp"
#include "core/perf/metrics/metrics.hpp"

Worker::Worker(IWorkDispatcher* workDispatcher, int id, int maxBatch) : workDispatcher_(workDispatcher), id_(id), maxBatch_(maxBatch){
    //
}

Worker::~Worker(){
    stop();
}

void Worker::start(){
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this]{ runLoop(); });
}

void Worker::stop(){
    running_.store(false, std::memory_order_release);
    if(thread_.joinable() && (std::this_thread::get_id() != thread_.get_id())){
        thread_.join();
    }
}

void Worker::runLoop(){
    while(running_.load(std::memory_order_relaxed) || workDispatcher_->isDraining()){
        auto idleStartTime = Time::now();
        ActorRuntime* actorRuntime = workDispatcher_->acquire(id_);
        auto idleEndTime = Time::now();

        if(!actorRuntime){
            workDispatcher_->drainPendedActor();
            if(workDispatcher_->isDraining() && (workDispatcher_->pendingWork() == 0)) break;
            if(!running_.load(std::memory_order_relaxed)) break;
            continue;
        }

        auto busyStartTime = Time::now();
        int processed = actorRuntime->run(maxBatch_);
        auto busyEndTime = Time::now();

        uint64_t gapIdleNs = Time::toNs(idleEndTime - idleStartTime);
        uint64_t gapBusyNs = Time::toNs(busyEndTime - busyStartTime);
        V2_METRICS()->recordBatch(id_, processed, gapBusyNs, gapIdleNs);
    }
}
