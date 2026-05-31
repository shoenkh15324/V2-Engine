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
    thread_ = std::make_unique<Thread>(&Worker::runLoop, this);
}

void Worker::stop(){
    running_ = false;
    dispatcher_->wakeup();
    if(thread_){
        thread_->join();
        thread_.reset();
    }
}

void Worker::runLoop(void* arg){
    Worker* self = static_cast<Worker*>(arg);
    while(self->running_){
        ActorContext* actorCtx = self->dispatcher_->pop();
        if(!actorCtx){
            continue;
        }
        actorCtx->run(self->maxBatch_);
    }
}
