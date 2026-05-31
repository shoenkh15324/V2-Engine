#include "worker.hpp"
#include "dispatcher.hpp"
#include "actor_context.hpp"

Worker::Worker(Dispatcher* dispatcher, int maxBatch)
    : dispatcher_(dispatcher), maxBatch_(maxBatch)
{
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
    dispatcher_->wakeup();
    if(thread_.joinable()){
        thread_.join();
    }
}

void Worker::runLoop(){
    while(running_){
        ActorContext* actorCtx = dispatcher_->pop();
        if(!actorCtx){
            continue;
        }
        actorCtx->run(maxBatch_);
    }
}
