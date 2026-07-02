#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/device_manager_messages.hpp"

class DeviceManagerActor : public Actor{
public:
    DeviceManagerActor(const std::string& name, uint64_t id);
    ~DeviceManagerActor() override;

    DeviceManagerActor(const DeviceManagerActor&) = delete;
    DeviceManagerActor& operator=(const DeviceManagerActor&) = delete;
    DeviceManagerActor(DeviceManagerActor&&) = delete;
    DeviceManagerActor& operator=(DeviceManagerActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    struct DeviceEntry{
        std::string name;
        HalType type;
        int bus;
    };

    std::vector<DeviceEntry> devices_;
};
