#include "actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"

ActorRuntime::ActorRuntime(std::unique_ptr<Actor> actor, std::unique_ptr<LockFreeMpscQueue<Message>> mailbox, IWorkDispatcher* workDispatcher, IScheduler* scheduler, IActorRegistry* actorRegistry, IEventLoop* eventLoop)
: actor_(std::move(actor)), mailbox_(std::move(mailbox)){
    actor_->setRuntime(this);
    workDispatcher_ = workDispatcher;
    scheduler_ = scheduler;
    actorRegistry_ = actorRegistry;
    eventLoop_ = eventLoop;
}

ActorRuntime::~ActorRuntime(){
    {
        std::lock_guard lock(timerMutex_);
        for(int id : timerIds_){
            if(scheduler_) scheduler_->cancel(id);
        }
        timerIds_.clear();
    }
    if(actorRegistry_){
        actorRegistry_->remove(actor_.get());
    }
}

void ActorRuntime::enqueue(Message msg){
    if(!mailbox_->push(std::move(msg))){
        Metrics::recordEnqueue(actor_->id(), false, 0);
        return;
    }
    Metrics::recordEnqueue(actor_->id(), true, mailbox_->count());
    if(workDispatcher_ && !scheduled_.exchange(true)){
        workDispatcher_->dispatch(this);
    }
}

int ActorRuntime::run(int maxBatch){
    auto startTime = Time::now();
    Message msg;
    int processed = 0;
    while((maxBatch < 0) || (processed < maxBatch)){
        if(!mailbox_->pop(msg)) break;
        actor_->handle(msg);
        processed++;
    }
    auto endTime = Time::now();
    uint64_t gapNs = Time::toNs(endTime - startTime);
    Metrics::recordHandle(actor_->id(), processed, gapNs);

    scheduled_ = false;
    if(!mailbox_->empty()){
        workDispatcher_->dispatch(this);
    }
    return processed;
}

int ActorRuntime::addTimer(Actor* target, Message msg, uint64_t delayMs, bool repeating){
    if(!scheduler_) return Fail;
    std::lock_guard lock(timerMutex_);
    int id = scheduler_->addTimer(target, std::move(msg), delayMs, repeating);
    if(id != Fail) timerIds_.insert(id);
    return id;
}

void ActorRuntime::cancelTimer(int timerId){
    if(!scheduler_) return;
    std::lock_guard lock(timerMutex_);
    scheduler_->cancel(timerId);
    timerIds_.erase(timerId);
}

void ActorRuntime::cancelAllTimers(){
    if(!scheduler_) return;
    std::lock_guard lock(timerMutex_);
    for(int id : timerIds_){
        scheduler_->cancel(id);
    }
    timerIds_.clear();
}

size_t ActorRuntime::timerCount() const {
    std::lock_guard lock(timerMutex_);
    return timerIds_.size();
}
