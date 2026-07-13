#pragma once
#include "core/common/config/platform_config.h"
#include "core/actor_system/messages/network_manager/wifi_messages.hpp"
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <map>

#if V2_PLATFORM_LINUX
#include <sdbus-c++/sdbus-c++.h>

class WifiHandler{
public:
    WifiHandler();
    ~WifiHandler();

    WifiHandler(const WifiHandler&) = delete;
    WifiHandler& operator=(const WifiHandler&) = delete;
    WifiHandler(WifiHandler&&) = delete;
    WifiHandler& operator=(WifiHandler&&) = delete;

    int open(sdbus::IConnection& connection);
    int close();

    // Wifi operations
    void requestScan();
    void refreshAps();
    bool addAndActivateConnection(const std::string& ssid, const std::string& password);
    bool disconnectDevice();

    // State queries
    uint32_t getDeviceState();
    std::string getActiveApPath();
    WifiApInfo readApInfo(const std::string& apPath);
    std::string readInterfaceName();
    std::string readIp4Address();
    const char* deviceStateToString(uint32_t s);

    // Flags / cache access
    bool consumeScanRefreshPending();
    bool getConnectPending() const;
    void setConnectPending(bool v);
    const std::vector<WifiApInfo>& lastScanResults() const;
    std::string activeConnectionPath() const;
    bool deviceFound() const;

private:
    // Proxy management
    sdbus::IProxy* getProxy(const std::string& destination, const std::string& objectPath);
    sdbus::IProxy* nmProxy();
    sdbus::IProxy* deviceProxy();

    // Device discovery
    bool findWirelessDevice();

    // Utilities
    std::string ssidBytesToString(const std::vector<uint8_t>& ssid);
    std::string bssidBytesToString(const std::vector<uint8_t>& bssid);
    std::string flagsToSecurity(uint32_t wpaFlags, uint32_t rsnFlags);

    sdbus::IConnection* connection_{nullptr};
    std::unique_ptr<sdbus::IProxy> nmProxy_;
    std::unique_ptr<sdbus::IProxy> deviceProxy_;
    std::map<std::string, std::unique_ptr<sdbus::IProxy>> proxies_;
    std::string devicePath_;
    std::string activeConnectionPath_;
    std::vector<WifiApInfo> lastScanResults_;
    std::atomic<bool> scanRefreshPending_{false};
    bool connectPending_{false};
};

#endif // V2_PLATFORM_LINUX
