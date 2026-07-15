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
    void setAutoReconnect(bool enable){ reconnect_.enabled = enable; }
    bool getAutoReconnect() const { return reconnect_.enabled; }

    // State queries
    WifiState state() const { return conn_.state; }
    std::string getActiveApPath();
    WifiApInfo readApInfo(const std::string& apPath);
    std::string readInterfaceName();
    std::string readIp4Address();
    void syncDeviceState();

    // Flags / cache access
    bool consumeScanRefreshPending(){ return scan_.refreshPending.exchange(false); }
    const std::vector<WifiApInfo>& lastScanResults() const { return scan_.results; }
    std::string activeConnectionPath() const { return conn_.activePath; }
    bool deviceFound() const { return !dbus_.devicePath.empty(); }

private:
    struct DbusInfo{
        sdbus::IConnection* connection{nullptr};
        sdbus::IProxy* nmProxy{nullptr};
        std::unique_ptr<sdbus::IProxy> deviceProxy;
        std::string devicePath;
    };

    struct ConnectionState{
        WifiState state{WifiState::Disconnected};
        std::string activePath;
    };

    struct AutoReconnect{
        bool enabled{true};
        std::string lastSsid;
        std::string lastPassword;
    };

    struct ScanCache{
        std::vector<WifiApInfo> results;
        std::atomic<bool> refreshPending{false};
    };

    // Device discovery
    bool findWirelessDevice();

    // Utilities
    std::string ssidBytesToString(const std::vector<uint8_t>& ssid);
    std::string flagsToSecurity(uint32_t wpaFlags, uint32_t rsnFlags);
    uint32_t getDeviceState();
    static WifiState mapDeviceState(uint32_t nmState);

    DbusInfo dbus_;
    ConnectionState conn_;
    AutoReconnect reconnect_;
    ScanCache scan_;
};

#endif // V2_PLATFORM_LINUX
