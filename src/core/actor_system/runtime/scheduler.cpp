#include "scheduler.hpp"
#include "core/common/log/log.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

void Scheduler::timerCallback(int /*id*/, void* ctx){
    auto* tctx = static_cast<TimerCtx*>(ctx);
    tctx->target->enqueue(tctx->msg);
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
    timer_.stop();
}

int Scheduler::addTimer(IActorRuntime* target, Message msg, uint64_t timeMs, bool repeating){
    cleanupTimerCtxs(); // 매 add시 비활성 ctx 정리
    auto ctx = std::make_unique<TimerCtx>();
    ctx->target = target;
    ctx->msg = std::move(msg);
    TimerCtx* raw = ctx.get();
    int id = timer_.add(timeMs, repeating, timerCallback, raw);
    timerCtxs_[id] = std::move(ctx);
    return id;
}

void Scheduler::cancel(int id){
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
