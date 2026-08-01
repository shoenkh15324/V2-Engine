#include "worker.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/time/time.hpp"
#include "core/perf/metrics/metrics.hpp"
#include <pthread.h>

Worker::Worker(WorkDispatcher* workDispatcher, int id, int maxBatch) : workDispatcher_(workDispatcher), id_(id), maxBatch_(maxBatch){
    threadName_ = "v2-worker" + std::to_string(id_);
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
#if V2_PLATFORM_LINUX
    pthread_setname_np(pthread_self(), threadName_.c_str());
#elif V2_PLATFORM_MACOS
    pthread_setname_np(threadName_.c_str());
#endif
    while(running_.load(std::memory_order_relaxed) || workDispatcher_->isDraining()){
        auto idleStartTime = Time::now();
        ActorRuntime* actorRuntime = workDispatcher_->acquire(id_);
        auto idleEndTime = Time::now();

        if(!actorRuntime){
            if(workDispatcher_->isDraining() && (workDispatcher_->pendingWork() == 0)) break;
            if(!running_.load(std::memory_order_relaxed)) break;
            continue;
        }

        auto busyStartTime = Time::now();
        bool more = false;
        int processed = actorRuntime->run(maxBatch_, &more);
        auto busyEndTime = Time::now();
        if(!more) workDispatcher_->onWorkDone();

        uint64_t gapIdleNs = Time::toNs(idleEndTime - idleStartTime);
        uint64_t gapBusyNs = Time::toNs(busyEndTime - busyStartTime);
        if(Metrics::isEnabled()) Metrics::recordBatch(id_, processed, gapBusyNs, gapIdleNs);
    }
}
