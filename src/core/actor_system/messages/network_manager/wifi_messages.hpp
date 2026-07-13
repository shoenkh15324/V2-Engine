#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct WifiApInfo{
    bool connected{false};
    uint16_t frequency{0};
    uint32_t maxBitrate{0};
    int32_t signalStrength{0};
    std::string ssid;
    std::string bssid;
    std::string security;
    std::string mode;
    std::string objectPath;
};

struct WifiScanRequest{
    //
};

struct WifiScanResult{
    std::vector<WifiApInfo> accessPoints;
};

struct WifiConnectRequest{
    std::string ssid;
    std::string password;
};

struct WifiConnectResult{
    bool result{false};
    std::string errorMsg;
};

struct WifiDisconnectRequest{
    //
};

struct WifiDisconnectResult{
    bool result{false};
};

struct WifiStatusResult{
    bool connected{false};
    std::string ssid;
    std::string ipAddress;
    std::string state; // "disconnected", "scanning", "connecting", "connected"
    std::string interfaceName;
    int32_t signalStrength{0};
};
