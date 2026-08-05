#include "scheduler.hpp"
#include "core/common/log/log.hpp"
#include "core/common/timer/timer.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"

Scheduler::Scheduler(std::unique_ptr<ITimer> timer){
    timer_ = timer ? std::move(timer) : std::make_unique<Timer>();
}

Scheduler::~Scheduler(){
    stop();
}

void Scheduler::start(){
    timer_->start();
}

void Scheduler::stop(){
    std::lock_guard<std::mutex> lock(mutex_);
    timer_->stop();
    timerCtxs_.clear();
}

int Scheduler::addTimer(IActorRuntime* target, Message msg, uint64_t delayMs, bool repeating){
    std::lock_guard<std::mutex> lock(mutex_);
    cleanupTimerCtxs(); // 매 add시 비활성 ctx 정리
    auto ctx = std::make_unique<TimerCtx>();
    ctx->target = target;
    ctx->msg = std::move(msg);
    int id = timer_->add(delayMs, repeating, timerCallback, this);
    timerCtxs_[id] = std::move(ctx);
    return id;
}

void Scheduler::cancel(int id){
    std::lock_guard<std::mutex> lock(mutex_);
    timer_->cancel(id);
    timerCtxs_.erase(id);
}

void Scheduler::cleanupTimerCtxs(){
    for(auto it = timerCtxs_.begin(); it != timerCtxs_.end();){
        if(!timer_->isAlive(it->first)){
            it = timerCtxs_.erase(it);
        }else{
            ++it;
        }
    }
}

void Scheduler::timerCallback(int id, void* ctx){
    auto* self = static_cast<Scheduler*>(ctx);
    std::lock_guard<std::mutex> lock(self->mutex_);
    auto it = self->timerCtxs_.find(id);
    if(it == self->timerCtxs_.end()) return; // 취소됐거나 아직 등록 전 → 안전하게 무시
    it->second->target->enqueue(it->second->msg.clone());
}

