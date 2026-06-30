#pragma once
#include <cstdint>
#include <string>
#include "core/common/util/platform_config.h"

struct DbusRegisterMethod{
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string ownerActorName;
};

struct DbusUnregisterMethod{
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
};

struct DbusRegisterResult{
    std::string methodKey;
    bool success{false};
    std::string errorMsg;
};

struct DbusMethodCallResult{
    uint64_t callId{0};
    std::string result;
    bool isError{false};
};

struct DbusIncomingMethodCall {
    uint64_t callId{0};
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string args;
    std::string senderActorName;
};

struct DbusProxyCallRequest{
    uint64_t callId{0};
    std::string destination;
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string args;
    std::string requesterActorName;
};

struct DbusProxyCallResult{
    uint64_t callId{0};
    std::string result;
    bool isError{false};
};

struct DbusSubscribeSignal{
    std::string destination;
    std::string objectPath;
    std::string interfaceName;
    std::string signalName;
    std::string subscriberActorName;
};

struct DbusSignalEvent{
    std::string destination;
    std::string objectPath;
    std::string interfaceName;
    std::string signalName;
    std::string args;
};
