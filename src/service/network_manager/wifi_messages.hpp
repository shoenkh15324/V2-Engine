#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/actor_system/messages/message_traits.hpp"

enum class WifiState : uint8_t{
    Disconnected = 0,
    Scanning,
    Connecting,
    Connected,
    Disconnecting,
    Error
};

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
    static constexpr MessageId kId = MessageId::WifiScanRequest;
};

struct WifiScanResult{
    static constexpr MessageId kId = MessageId::WifiScanResult;
    std::vector<WifiApInfo> accessPoints;
};

struct WifiConnectRequest{
    static constexpr MessageId kId = MessageId::WifiConnectRequest;
    std::string ssid;
    std::string password;
};

struct WifiConnectResult{
    static constexpr MessageId kId = MessageId::WifiConnectResult;
    bool result{false};
    std::string errorMsg;
};

struct WifiDisconnectRequest{
    static constexpr MessageId kId = MessageId::WifiDisconnectRequest;
};

struct WifiDisconnectResult{
    static constexpr MessageId kId = MessageId::WifiDisconnectResult;
    bool result{false};
};

struct WifiStatusResult{
    static constexpr MessageId kId = MessageId::WifiStatusResult;
    bool connected{false};
    std::string ssid;
    std::string ipAddress;
    WifiState state;
    std::string interfaceName;
    int32_t signalStrength{0};
    bool autoReconnect{false};
};

struct WifiAutoReconnectRequest{
    static constexpr MessageId kId = MessageId::WifiAutoReconnectRequest;
    bool enable;
};
