#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "core/actor_system/messages/network_manager.hpp"

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

#if V2_PLATFORM_LINUX
#include <sdbus-c++/sdbus-c++.h>

class NetmanagerActor : public Actor{
public:
    NetmanagerActor(const std::string& name, uint64_t id);
    ~NetmanagerActor() override;

    NetmanagerActor(const NetmanagerActor&) = delete;
    NetmanagerActor& operator=(const NetmanagerActor&) = delete;
    NetmanagerActor(NetmanagerActor&&) = delete;
    NetmanagerActor& operator=(NetmanagerActor&&) = delete;

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
    std::string addAndActivateConnection(const std::string& ssid, const std::string& password);
    void disconnectDevice();
    uint32_t getDeviceState();
    std::string getActiveApPath();
    ApInfo readApInfo(const std::string& apPath);

    // Helpers
    std::string ssidBytesToString(const std::vector<uint8_t>& ssid);
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
    std::vector<ApInfo> lastScanResults_;
    uint32_t nmDeviceState_{0};
};

#endif // V2_PLATFORM_LINUX
