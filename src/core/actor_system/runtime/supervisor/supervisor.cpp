#include "supervisor.hpp"
#include "core/actor_system/runtime/supervisor/dead_letter_queue.hpp"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"

Supervisor::Supervisor(DeadLetterQueue& deadLetterQueue) : deadLetterQueue_(deadLetterQueue){}

void Supervisor::setDefaultStrategy(RestartStrategy strategy){
    std::lock_guard lock(mutex_);
    defaultStrategy_ = strategy;
}

void Supervisor::setStrategy(uint64_t actorId, RestartStrategy strategy){
    std::lock_guard lock(mutex_);
    perActorStrategy_[actorId] = strategy;
}

void Supervisor::removePolicy(uint64_t actorId){
    std::lock_guard lock(mutex_);
    perActorStrategy_.erase(actorId);
}

void Supervisor::setMaxRestarts(int maxRestarts){
    std::lock_guard lock(mutex_);
    maxRestarts_ = maxRestarts;
}

void Supervisor::setRestartAll(std::function<int()> restartAll){
    std::lock_guard lock(mutex_);
    restartAll_ = std::move(restartAll);
}

void Supervisor::onActorFailed(ISupervised* runtime, Message failedMsg, const std::string& reason){
    if(!runtime) return;
    totalFailures_.fetch_add(1, std::memory_order_relaxed);
    V2_LOG_ERROR("Actor {} crashed: {}", runtime->actorName().c_str(), reason.c_str());

    // 1. 정책 스냅샷: strategy + maxRestarts를 한 번의 락으로 함께 읽는다.
    //    (각각 다른 락으로 읽으면 스냅샷이 불일치할 수 있다.)
    RestartStrategy strategy = defaultStrategy_;
    int limit = maxRestarts_;
    {
        std::lock_guard lock(mutex_);
        auto it = perActorStrategy_.find(runtime->actorId());
        if(it != perActorStrategy_.end()){
            strategy = it->second;
        }
        limit = maxRestarts_;
    }

    // 2. 실패 메시지 + 남은 메일박스를 dead letter로 이관.
    //    deadLetterQueue_는 reference(필수 의존성)이므로 null 체크 없이 항상 수행된다.
    uint64_t nowNs = static_cast<uint64_t>(Time::nowNs());
    DeadLetter letter{runtime->actorId(), runtime->actorName(), reason, nowNs, std::move(failedMsg)};
    if(!deadLetterQueue_.push(std::move(letter))){
        V2_LOG_WARN("Dead letter queue full, dropping failed message from {}", runtime->actorName().c_str());
    }
    Message msg;
    while(runtime->drainMailbox(msg)){
        DeadLetter rest{runtime->actorId(), runtime->actorName(), reason, nowNs, std::move(msg)};
        if(!deadLetterQueue_.push(std::move(rest))){
            V2_LOG_WARN("Dead letter queue full, dropping drained message from {}", runtime->actorName().c_str());
        }
    }

    // 3. 전략 적용
    switch(strategy){
    case RestartStrategy::OneForOne:
        // 재시작 한계 판정은 tryRestart() 내부의 CAS로 원자적으로 수행된다.
        if(runtime->tryRestart(reason, limit)){
            totalRestarts_.fetch_add(1, std::memory_order_relaxed);
        }else{
            V2_LOG_ERROR("Actor {} restart failed or exceeded max restarts", runtime->actorName().c_str());
        }
        break;
    case RestartStrategy::OneForAll:
        {
            std::function<int()> fn;
            {
                std::lock_guard lock(mutex_);
                fn = restartAll_;
            }
            int restarted = 0;
            if(fn){
                // 사용자 콜백이 예외를 던져도 워커 스레드를 죽이면 안 된다.
                try{
                    restarted = fn();
                }catch(const std::exception& e){
                    V2_LOG_ERROR("restartAll callback threw: {}", e.what());
                }catch(...){
                    V2_LOG_ERROR("restartAll callback threw unknown exception");
                }
            }
            oneForAllBroadcasts_.fetch_add(1, std::memory_order_relaxed);
            if(restarted > 0){
                totalRestarts_.fetch_add(static_cast<size_t>(restarted), std::memory_order_relaxed);
            }
        }
        break;
    case RestartStrategy::None:
        V2_LOG_WARN("Actor {} restart disabled by policy, shutting down", runtime->actorName().c_str());
        runtime->shutdown();
        break;
    }
}

size_t Supervisor::totalFailures() const noexcept {
    return totalFailures_.load(std::memory_order_relaxed);
}

size_t Supervisor::totalRestarts() const noexcept {
    return totalRestarts_.load(std::memory_order_relaxed);
}

size_t Supervisor::oneForAllBroadcasts() const noexcept {
    return oneForAllBroadcasts_.load(std::memory_order_relaxed);
}

size_t Supervisor::deadLetterCount() const noexcept {
    return deadLetterQueue_.count();
}
