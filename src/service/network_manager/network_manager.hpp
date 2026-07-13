#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "core/actor_system/messages/network_manager/wifi_messages.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <atomic>

#if V2_PLATFORM_LINUX
#include <sdbus-c++/sdbus-c++.h>

class NetworkManagerActor : public Actor{
public:
    NetworkManagerActor(const std::string& name, uint64_t id);
    ~NetworkManagerActor() override;

    NetworkManagerActor(const NetworkManagerActor&) = delete;
    NetworkManagerActor& operator=(const NetworkManagerActor&) = delete;
    NetworkManagerActor(NetworkManagerActor&&) = delete;
    NetworkManagerActor& operator=(NetworkManagerActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    // NM proxy helpers
    sdbus::IProxy* nmProxy();
    sdbus::IProxy* deviceProxy();
    sdbus::IProxy* proxyFor(const std::string& destination, const std::string& objectPath);

    // NM D-Bus calls
    void requestScan();
    void addAndActivateConnection(const std::string& ssid, const std::string& password);
    void disconnectDevice();
    uint32_t getDeviceState();
    std::string getActiveApPath();
    WifiApInfo readApInfo(const std::string& apPath);

    void syncDeviceState();
    NmStatusRequest buildStatusRequest();

    // Helpers
    std::string readInterfaceName();
    std::string readIp4Address();
    std::string ssidBytesToString(const std::vector<uint8_t>& ssid);
    std::string bssidBytesToString(const std::vector<uint8_t>& bssid);
    std::string flagsToSecurity(uint32_t wpaFlags, uint32_t rsnFlags);
    bool findWirelessDevice();
    void refreshAps();
    const char* deviceStateToString(uint32_t s);

    sdbus::IConnection* connection_{nullptr};
    std::unique_ptr<sdbus::IProxy> nmProxy_;
    std::unique_ptr<sdbus::IProxy> deviceProxy_;
    std::map<std::string, std::unique_ptr<sdbus::IProxy>> proxies_;
    std::string devicePath_;
    std::string activeConnectionPath_;
    std::vector<WifiApInfo> lastScanResults_;
    std::atomic<bool> scanRefreshPending_{false};
    bool connectPending_{false};
    uint32_t nmDeviceState_{0};
    int wifiSyncIntervalMs_{3000};
};

#endif // V2_PLATFORM_LINUX
