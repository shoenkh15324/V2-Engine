#include "worker.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"
#include "core/actor_system/runtime/actor_runtime.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/time/time.hpp"
#include "core/perf/metrics/metrics.hpp"
#include <pthread.h>

Worker::Worker(IWorkDispatcher* workDispatcher, int id, int maxBatch) : workDispatcher_(workDispatcher), id_(id), maxBatch_(maxBatch){
    threadName_ = "v2-worker" + std::to_string(id_);
}

Worker::~Worker(){
    stop();
}

void Worker::start(){
    running_ = true;
    thread_ = std::thread([this]{ runLoop(); });
}

void Worker::stop(){
    running_ = false;
    if(thread_.joinable()){
        thread_.join();
    }
}

void Worker::runLoop(){
#if V2_PLATFORM_LINUX
    pthread_setname_np(pthread_self(), threadName_.c_str());
#elif V2_PLATFORM_MACOS
    pthread_setname_np(threadName_.c_str());
#endif
    while(running_){
        auto idleStartTime = Time::now();
        ActorRuntime* actorRuntime = workDispatcher_->acquire(id_);
        auto idleEndTime = Time::now();

        if(!actorRuntime){
            if(!workDispatcher_->isRunning()) break;
            continue;
        }

        auto busyStartTime = Time::now();
        int processed = actorRuntime->run(maxBatch_);
        auto busyEndTime = Time::now();

        uint64_t gapIdleNs = Time::toNs(idleEndTime - idleStartTime);
        uint64_t gapBusyNs = Time::toNs(busyEndTime - busyStartTime);
        Metrics::recordBatch(id_, processed, gapBusyNs, gapIdleNs);
    }
}
