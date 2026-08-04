#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include "core/actor_system/runtime/supervisor/i_supervisor.hpp"
#include "core/actor_system/runtime/supervisor/i_supervised.hpp"

class DeadLetterQueue;

enum class RestartStrategy{
    OneForOne, // 실패한 액터만 재시작
    OneForAll, // 모든 액터 재시작 (ActorRestartRequest 브로드캐스트)
    None // 재시작 없음, 영구 중단
};

// 액터 실패를 감지하고 정책에 따라 재시작/중단을 결정한다.
//
// 스레드 안전성:
//  - onActorFailed()는 크래시가 발생한 워커 스레드에서 호출되며, 여러 워커가
//    동시에 서로 다른 액터의 실패를 보고할 수 있다. 카운터는 relaxed atomic,
//    dead letter는 lock-free MPSC 큐로 동시성을 해결한다.
//  - 재시작은 해당 액터를 소유한 워커(악피니티)에서 수행되므로 다른 워커와
//    충돌이 없다. OneForAll은 각 액터가 자기 워커에서 재시작하도록
//    ActorRestartRequest를 브로드캐스트한다.
//  - 정책 설정은 start() 이전 단일 스레드에서 수행하는 것을 권장하지만,
//    내부 mutex로 보호하여 런타임 변경도 안전하다.
class Supervisor : public ISupervisor {
public:
    explicit Supervisor(DeadLetterQueue& deadLetterQueue);

    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;
    Supervisor(Supervisor&&) = delete;
    Supervisor& operator=(Supervisor&&) = delete;

    // 정책 설정 (start() 전에 설정 권장)
    void setDefaultStrategy(RestartStrategy strategy);
    void setStrategy(uint64_t actorId, RestartStrategy strategy); // 액터별 오버라이드
    void removePolicy(uint64_t actorId);
    void setMaxRestarts(int maxRestarts);

    // OneForAll 시 전체 런타임에 ActorRestartRequest를 브로드캐스트하는 콜백.
    // 실제로 재시작이 요청된 액터 수를 반환해야 한다.
    void setRestartAll(std::function<int()> restartAll) override;

    // 실패 처리 (여러 워커에서 동시 호출 가능)
    void onActorFailed(ISupervised* runtime, Message failedMsg, const std::string& reason) override;

    // 통계 (스냅샷 / 디버깅용, relaxed 읽기)
    size_t totalFailures() const noexcept;
    size_t totalRestarts() const noexcept;
    size_t oneForAllBroadcasts() const noexcept;
    size_t deadLetterCount() const noexcept;

private:
    DeadLetterQueue& deadLetterQueue_;
    RestartStrategy defaultStrategy_{RestartStrategy::OneForOne};
    mutable std::mutex mutex_;
    std::function<int()> restartAll_;
    std::atomic<size_t> totalFailures_{0};
    std::atomic<size_t> totalRestarts_{0};
    std::atomic<size_t> oneForAllBroadcasts_{0};
    std::unordered_map<uint64_t, RestartStrategy> perActorStrategy_;
    std::unordered_map<uint64_t, int> oneForAllRestartCount_;
    int maxRestarts_{5};
};
