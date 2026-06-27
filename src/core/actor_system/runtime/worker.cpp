#include "worker.hpp"
#include "dispatcher.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/common/util/platform_config.h"
#include <pthread.h>

Worker::Worker(Dispatcher* dispatcher, int id, int maxBatch) : dispatcher_(dispatcher), id_(id), maxBatch_(maxBatch){
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
        ActorContext* actorCtx = dispatcher_->acquire();
        if(!actorCtx){
            if(!dispatcher_->isRunning()) break;
            continue;
        }
        actorCtx->run(maxBatch_);
    }
}
