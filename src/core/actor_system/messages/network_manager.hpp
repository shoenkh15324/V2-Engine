#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ApInfo{
    uint16_t frequency{0};
    uint32_t maxBitrate{0};
    int32_t signalStrength{0};
    std::string ssid;
    std::string bssid;
    std::string security;
    std::string mode;
    std::string objectPath;
};

struct NetScanRequest{
    //
};

struct NetScanResult{
    std::vector<ApInfo> accessPoints;
};

struct NetConnectRequest{
    std::string ssid;
    std::string password;
};

struct NetConnectResult{
    bool result{false};
    std::string errorMsg;
};

struct NetDisconnectRequest{
    //
};

struct NetDisconnectResult{
    bool result{false};
};

struct NetStatusRequest{
    //
};

struct NetStatusResult{
    bool connected{false};
    std::string ssid;
    std::string ipAddress;
    std::string state; // "disconnected", "scanning", "connecting", "connected"
    std::string interfaceName;
    int32_t signalStrength{0};
};

struct NetWifiToggleRequest{
    bool enable;
};
