#include "actor_runtime.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"
#include "core/actor_system/runtime/supervisor/i_supervisor.hpp"
#include "core/actor_system/messages/core_messages.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"

ActorRuntime::ActorRuntime(std::unique_ptr<Actor> actor, std::unique_ptr<IMailbox> mailbox, const ActorRuntimeDeps& deps)
: actor_(std::move(actor)), mailbox_(std::move(mailbox)){
    actor_->setRuntime(this);
    workDispatcher_ = deps.workDispatcher;
    scheduler_ = deps.scheduler;
    actorRegistry_ = deps.actorRegistry;
    eventLoop_ = deps.eventLoop;
    supervisor_ = deps.supervisor;
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
        V2_METRICS()->recordEnqueue(actor_->id(), false, 0);
        V2_LOG_WARN("Actor {} mailbox full, dropping message id={}", actor_->name().c_str(), static_cast<int>(msg.id()));
        return;
    }
    V2_METRICS()->recordEnqueue(actor_->id(), true, mailbox_->count());
    if(workDispatcher_) workDispatcher_->dispatch(this);
}

int ActorRuntime::run(int maxBatch, bool* hasMoreWork){
    if(hasMoreWork) *hasMoreWork = false;
    auto startTime = Time::now();
    auto r = processBatch(maxBatch);
    uint64_t gapNs = Time::toNs(Time::now() - startTime);
    V2_METRICS()->recordHandle(actor_->id(), r.processed, gapNs);

    bool resumed = workDispatcher_ ? workDispatcher_->settleToken(this) : false;
    if(hasMoreWork) *hasMoreWork = resumed;
    return r.processed;
}

bool ActorRuntime::tryConsumeLifecycle(const Message& msg){
    switch(msg.id()){
        case MessageId::ActorEnableRequest:
            if(actor_->getState() == Closed){
                actor_->open();
            }
            return true;
        case MessageId::ActorDisableRequest:
            if(actor_->getState() == Opened && !actor_->isEssential()){
                actor_->close();
            }
            return true;
        case MessageId::ActorRestartRequest:
            // OneForAll 브로드캐스트. 실행 중(Opened)인 액터는 상태 무관하게 재시작한다.
            // Closed(비활성/종료 중) 액터는 건너뛴다 — 셧다운 중 재오픈을 방지한다.
            // maxRestarts 한계는 OneForOne 개별 실패 루프 방지용이므로 여기서는
            // restartCount_를 증가시키지 않는다 (Supervisor가 oneForAllBroadcasts_로 집계).
            if(actor_->getState() == Opened){
                performRestart(msg.as<ActorRestartRequest>().reason);
            }
            return true;
        default:
            return false;
    }
}

bool ActorRuntime::tryRestart(const std::string& reason, int maxRestarts){
    int prev = restartCount_.load(std::memory_order_relaxed);
    while(true){
        if(prev >= maxRestarts) return false;
        if(restartCount_.compare_exchange_weak(prev, prev + 1, std::memory_order_relaxed)) break;
    }
    performRestart(reason);
    return true;
}

bool ActorRuntime::popDeadLetter(Message& msg){
    return mailbox_->pop(msg);
}

int ActorRuntime::restartCount() const {
    return restartCount_.load(std::memory_order_relaxed);
}

uint64_t ActorRuntime::actorId() const {
    return actor_->id();
}

const std::string& ActorRuntime::actorName() const {
    return actor_->name();
}

void ActorRuntime::shutdown(){
    stopped_.store(true, std::memory_order_relaxed);
    actor_->close();
}

void ActorRuntime::performRestart(const std::string& reason){
    V2_LOG_INFO("Restarting actor {} reason: {}", actor_->name().c_str(), reason.c_str());
    // 1. close() 시도 (실패해도 open()은 시도)
    try{
        actor_->close();
    }catch(const std::exception& e){
        V2_LOG_ERROR("Actor {} close() failed during restart: {}", actor_->name().c_str(), e.what());
    }catch(...){
        V2_LOG_ERROR("Actor {} close() failed with unknown exception", actor_->name().c_str());
    }
    // 2. close() 성공 시에만 open() 시도
    if(actor_->getState() == Closed){
        try{
            actor_->open();
        }catch(const std::exception& e){
            V2_LOG_ERROR("Actor {} open() failed during restart: {}", actor_->name().c_str(), e.what());
            // open() 실패 시 supervisor에 알림
            if(supervisor_){
                supervisor_->onActorFailed(this, Message(), std::string("open() failed: ") + e.what());
            }
        }catch(...){
            V2_LOG_ERROR("Actor {} open() failed with unknown exception", actor_->name().c_str());
            if(supervisor_){
                supervisor_->onActorFailed(this, Message(), "open() failed with unknown exception");
            }
        }
    }
}

int ActorRuntime::addTimer(Actor* target, Message msg, uint64_t delayMs, bool repeating){
    if(!scheduler_ || !target) return Fail;
    IActorRuntime* targetRuntime = target->runtime();
    if(!targetRuntime) return Fail;
    std::lock_guard lock(timerMutex_);
    int id = scheduler_->addTimer(targetRuntime, std::move(msg), delayMs, repeating);
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

ActorRuntime::BatchResult ActorRuntime::processBatch(int maxBatch){
    if(stopped_.load(std::memory_order_relaxed)) return {};
    Message msg;
    int processed = 0;
    while((maxBatch < 0) || (processed < maxBatch)){
        if(!mailbox_->pop(msg)) break;
        try{
            if(!tryConsumeLifecycle(msg)){
                actor_->handle(msg);
            }
        }catch(const std::exception& e){
            if(supervisor_){
                supervisor_->onActorFailed(this, std::move(msg), e.what());
            }else{
                V2_LOG_ERROR("Actor {} threw: {}", actor_->name().c_str(), e.what());
            }
            break;
        }catch(...){
            if(supervisor_){
                supervisor_->onActorFailed(this, std::move(msg), "unknown exception");
            }else{
                V2_LOG_ERROR("Actor {} threw unknown exception", actor_->name().c_str());
            }
            break;
        }
        processed++;
    }
    return { processed, !mailbox_->empty() };
}
