#include "actor_context.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "actor_registry.hpp"

ActorContext::ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize, Dispatcher* dispatcher, Scheduler* scheduler, ActorRegistry* actorRegistry)
: actor_(std::move(actor)), mailbox_(mailboxSize)
{
    actor_->context_ = this;
    dispatcher_ = dispatcher;
    scheduler_ = scheduler;
    actorRegistry_ = actorRegistry;
}

ActorContext::~ActorContext(){
    if(actorRegistry_){
        actorRegistry_->remove(actor_.get());
    }
}

void ActorContext::enqueue(Message msg){
    bool wasEmpty = mailbox_.push(std::move(msg));
    if(!wasEmpty) return;
    if(dispatcher_ && !scheduled_.exchange(true)){
        dispatcher_->schedule(this);
    }
}

void ActorContext::run(int maxBatch){
    Message msg;
    int processed = 0;
    while((maxBatch < 0) || (processed < maxBatch)){
        if(!mailbox_.pop(msg)) break;
        actor_->handle(msg);
        processed++;
    }
    scheduled_ = false;
    if(!mailbox_.empty()){
        dispatcher_->schedule(this);
    }
}

