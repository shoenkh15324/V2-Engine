#pragma once
#include <vector>
#include <functional>
#include <unordered_set>
#include "core/common/time/time.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/core_messages.hpp"
#include "infra/hal/sys/i_sys.hpp"
#include "service/system_manager/system_manager_messages.hpp"

class SystemManagerActor : public Actor {
public:
    using Callback = std::function<void(int)>;

    SystemManagerActor(std::string name, uint64_t id, ISys* sys, int pollIntervalMs);
    ~SystemManagerActor() override = default;

    SystemManagerActor(const SystemManagerActor&) = delete;
    SystemManagerActor& operator=(const SystemManagerActor&&) = delete;
    SystemManagerActor(SystemManagerActor&&) = delete;
    SystemManagerActor& operator=(SystemManagerActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;
    void handle(const SignalNotify& m);
    void handle(const SysDataTick& m);
    void handle(const SysDataSubscribe& m);
    void handle(const SysDataUnsubscribe& m);
    static void onSignal(int signum, Callback cb);

private:
    void pumpIfNeeded();

    ISys* sys_ = nullptr;
    Time::TimeStamp startTime_;
    SystemResources latestSysRes_;
    int signalPipeFd_ = -1;
    int pollIntervalMs_ = 500;
    std::unordered_set<std::string> subscribers_;
};
