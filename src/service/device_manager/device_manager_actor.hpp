#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "infra/hal/pmu/i_pmu.hpp"
#include "service/device_manager/device_manager_messages.hpp"

class DeviceManagerActor : public Actor{
public:
    DeviceManagerActor(std::string name, uint64_t id, IPmu* pmu, int pollIntervalMs);
    ~DeviceManagerActor() override;

    DeviceManagerActor(const DeviceManagerActor&) = delete;
    DeviceManagerActor& operator=(const DeviceManagerActor&) = delete;
    DeviceManagerActor(DeviceManagerActor&&) = delete;
    DeviceManagerActor& operator=(DeviceManagerActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    void pumpIfNeeded();

    IPmu* pmu_ = nullptr;
    PmuData latestPmuData_;
    int pollIntervalMs_ = 500;
    std::unordered_set<std::string> subscribers_;
};
