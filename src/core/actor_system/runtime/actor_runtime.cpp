#include "actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/time/time.hpp"

ActorRuntime::ActorRuntime(std::unique_ptr<Actor> actor, std::unique_ptr<LockFreeMpscQueue<Message>> mailbox, IWorkDispatcher* workDispatcher, IScheduler* scheduler, IActorRegistry* actorRegistry, IEventLoop* eventLoop)
: actor_(std::move(actor)), mailbox_(std::move(mailbox)){
    actor_->setRuntime(this);
    workDispatcher_ = workDispatcher;
    scheduler_ = scheduler;
    actorRegistry_ = actorRegistry;
    eventLoop_ = eventLoop;
}

ActorRuntime::~ActorRuntime(){
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

int ActorRuntime::enableActor(const std::string& name){
    if(!actorRegistry_) return -1;
    ActorHandle h = actorRegistry_->findByName(name);
    if(!h.valid()) return -1;
    Actor* a = h.get();
    if(!a) return -1;
    return a->open() == 0 ? 0 : -3;
}

int ActorRuntime::disableActor(const std::string& name){
    if(!actorRegistry_) return -1;
    ActorHandle h = actorRegistry_->findByName(name);
    if(!h.valid()) return -1;
    Actor* a = h.get();
    if(!a) return -1;
    if(a->isEssential()) return -2;
    return a->close() == 0 ? 0 : -3;
}
