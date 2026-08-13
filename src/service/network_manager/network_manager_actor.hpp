#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "wifi_handler.hpp"
#include "service/tick/tick_messages.hpp"
#include "service/network_manager/network_manager_messages.hpp"
#include "service/network_manager/wifi_messages.hpp"
#include <string>

#if V2_PLATFORM_LINUX
#include <sdbus-c++/sdbus-c++.h>

class NetworkManagerActor : public Actor{
public:
    NetworkManagerActor(std::string name, uint64_t id);
    ~NetworkManagerActor() override;

    NetworkManagerActor(const NetworkManagerActor&) = delete;
    NetworkManagerActor& operator=(const NetworkManagerActor&) = delete;
    NetworkManagerActor(NetworkManagerActor&&) = delete;
    NetworkManagerActor& operator=(NetworkManagerActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;
    void handle(const Tick& m);
    void handle(const WifiScanRequest& m);
    void handle(const WifiConnectRequest& m);
    void handle(const WifiDisconnectRequest& m);
    void handle(const WifiAutoReconnectRequest& m);
    void handle(const NmStatusRequest& m);

private:
    void reportStatus();

    sdbus::IConnection* connection_{nullptr};
    std::unique_ptr<sdbus::IProxy> nmProxy_;
    WifiHandler wifi_;
    int wifiSyncIntervalMs_{3000};
};

#endif // V2_PLATFORM_LINUX
