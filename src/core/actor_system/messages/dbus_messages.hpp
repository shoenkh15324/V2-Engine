#pragma once
#include <cstdint>
#include <string>
#include "core/common/config/platform_config.h"
#include "type_id.hpp"

struct DbusRegisterMethod{
    static constexpr MessageId kId = MessageId::DbusRegisterMethod;
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string ownerActorName;
};

struct DbusUnregisterMethod{
    static constexpr MessageId kId = MessageId::DbusUnregisterMethod;
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
};

struct DbusRegisterResult{
    static constexpr MessageId kId = MessageId::DbusRegisterResult;
    std::string methodKey;
    bool success{false};
    std::string errorMsg;
};

struct DbusMethodCallResult{
    static constexpr MessageId kId = MessageId::DbusMethodCallResult;
    uint64_t callId{0};
    std::string result;
    bool isError{false};
};

struct DbusIncomingMethodCall {
    static constexpr MessageId kId = MessageId::DbusIncomingMethodCall;
    uint64_t callId{0};
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string args;
    std::string senderActorName;
};

struct DbusProxyCallRequest{
    static constexpr MessageId kId = MessageId::DbusProxyCallRequest;
    uint64_t callId{0};
    std::string destination;
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string args;
    std::string requesterActorName;
};

struct DbusProxyCallResult{
    static constexpr MessageId kId = MessageId::DbusProxyCallResult;
    uint64_t callId{0};
    std::string result;
    bool isError{false};
};

struct DbusSubscribeSignal{
    static constexpr MessageId kId = MessageId::DbusSubscribeSignal;
    std::string destination;
    std::string objectPath;
    std::string interfaceName;
    std::string signalName;
    std::string subscriberActorName;
};

struct DbusSignalEvent{
    static constexpr MessageId kId = MessageId::DbusSignalEvent;
    std::string destination;
    std::string objectPath;
    std::string interfaceName;
    std::string signalName;
    std::string args;
};
