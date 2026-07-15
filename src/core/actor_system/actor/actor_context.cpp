#include "actor_context.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/perf/metrics.hpp"
#include "core/common/time/time.hpp"

ActorContext::ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize, Dispatcher* dispatcher, IScheduler* scheduler, IActorRegistry* actorRegistry)
: actor_(std::move(actor)), mailbox_(mailboxSize){
    actor_->actorCtx_ = this;
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
    if(!mailbox_.push(std::move(msg))){
        Metrics::recordEnqueue(actor_->id(), false, 0);
        return;
    }
    Metrics::recordEnqueue(actor_->id(), true, mailbox_.count());
    if(dispatcher_ && !scheduled_.exchange(true)){
        dispatcher_->dispatch(this);
    }
}

int ActorContext::run(int maxBatch){
    auto startTime = Time::now();
    Message msg;
    int processed = 0;
    while((maxBatch < 0) || (processed < maxBatch)){
        if(!mailbox_.pop(msg)) break;
        actor_->handle(msg);
        processed++;
    }
    auto endTime = Time::now();
    uint64_t gapNs = Time::toNs(endTime - startTime);
    Metrics::recordHandle(actor_->id(), processed, gapNs);

    scheduled_ = false;
    if(!mailbox_.empty()){
        dispatcher_->dispatch(this);
    }
    return processed;
}

size_t ActorContext::mailboxCount() const {
    return mailbox_.count();
}

size_t ActorContext::mailboxCapacity() const {
    return mailbox_.capacity();
}
