#include "scheduler.hpp"
#include "core/common/log/log.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

void Scheduler::timerCallback(int id, void* ctx){
    auto* self = static_cast<Scheduler*>(ctx);
    std::lock_guard<std::mutex> lock(self->mutex_);
    auto it = self->timerCtxs_.find(id);
    if(it == self->timerCtxs_.end()) return; // 취소됐거나 아직 등록 전 → 안전하게 무시
    it->second->target->enqueue(it->second->msg.clone());
}

Scheduler::~Scheduler(){
    stop();
}

void Scheduler::start(IEventLoop* eventLoop){
    timer_.start();
    eventLoop_ = eventLoop;
    subscribeTimer();
}

void Scheduler::stop(){
    unsubscribeTimer();
    std::lock_guard<std::mutex> lock(mutex_);
    timer_.stop();
    timerCtxs_.clear();
}

int Scheduler::addTimer(IActorRuntime* target, Message msg, uint64_t timeMs, bool repeating){
    std::lock_guard<std::mutex> lock(mutex_);
    cleanupTimerCtxs(); // 매 add시 비활성 ctx 정리
    auto ctx = std::make_unique<TimerCtx>();
    ctx->target = target;
    ctx->msg = std::move(msg);
    int id = timer_.add(timeMs, repeating, timerCallback, this);
    timerCtxs_[id] = std::move(ctx);
    return id;
}

void Scheduler::cancel(int id){
    std::lock_guard<std::mutex> lock(mutex_);
    timer_.cancel(id);
    timerCtxs_.erase(id);
}

void Scheduler::subscribeTimer(){
    if(eventLoop_ && (timer_.fd() >= 0)){
        eventLoop_->subscribe(timer_.fd(), [this](){ timer_.handleTimerEvent(); });
    }
}

void Scheduler::unsubscribeTimer(){
    if(eventLoop_ && (timer_.fd() >= 0)){
        eventLoop_->unsubscribe(timer_.fd());
    }
}

void Scheduler::cleanupTimerCtxs(){
    for(auto it = timerCtxs_.begin(); it != timerCtxs_.end();){
        if(!timer_.isAlive(it->first)){
            it = timerCtxs_.erase(it);
        }else{
            ++it;
        }
    }
}
