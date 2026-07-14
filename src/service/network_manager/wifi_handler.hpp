#pragma once
#include "core/common/config/platform_config.h"
#include "core/actor_system/messages/network_manager/wifi_messages.hpp"
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <atomic>

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

    int open(sdbus::IConnection& connection, sdbus::IProxy& nmProxy);
    int close();

    // Wifi operations
    void requestScan();
    void refreshAps();
    bool addAndActivateConnection(const std::string& ssid, const std::string& password);
    bool disconnectDevice();
    void autoReconnect();
    void setAutoReconnect(bool enable){ autoReconnectEnabled_ = enable; }
    bool autoReconnectEnabled() const { return autoReconnectEnabled_; }

    // State queries
    WifiState state() const { return wifiState_; }
    uint32_t getDeviceState();
    std::string getActiveApPath();
    WifiApInfo readApInfo(const std::string& apPath);
    std::string readInterfaceName();
    std::string readIp4Address();
    void syncDeviceState();
    static WifiState mapDeviceState(uint32_t nmState);

    // Flags / cache access
    bool consumeScanRefreshPending(){ return scanRefreshPending_.exchange(false); }
    const std::vector<WifiApInfo>& lastScanResults() const { return lastScanResults_; }
    std::string activeConnectionPath() const { return activeConnectionPath_; }
    bool deviceFound() const { return !devicePath_.empty(); }

private:
    // Device discovery
    bool findWirelessDevice();

    // Utilities
    std::string ssidBytesToString(const std::vector<uint8_t>& ssid);
    std::string bssidBytesToString(const std::vector<uint8_t>& bssid);
    std::string flagsToSecurity(uint32_t wpaFlags, uint32_t rsnFlags);
    
    WifiState wifiState_{WifiState::Disconnected};
    sdbus::IConnection* connection_{nullptr};
    sdbus::IProxy* nmProxy_;
    std::unique_ptr<sdbus::IProxy> deviceProxy_;
    std::string devicePath_;
    std::string activeConnectionPath_;
    std::string lastConnectedSsid_;
    std::string lastConnectedPassword_;
    std::vector<WifiApInfo> lastScanResults_;
    std::atomic<bool> scanRefreshPending_{false};
    bool autoReconnectEnabled_{true};
};

#endif // V2_PLATFORM_LINUX
