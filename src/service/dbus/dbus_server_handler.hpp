#pragma once
#include "core/common/util/platform_config.h"
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>

#if V2_PLATFORM_LINUX
#include <sdbus-c++/sdbus-c++.h>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/dbus_messages.hpp"

class DbusServerHandler{
public:
    DbusServerHandler(sdbus::IConnection& connection, Actor& actor);
    ~DbusServerHandler() = default;

    DbusServerHandler(const DbusServerHandler&) = delete;
    DbusServerHandler& operator=(const DbusServerHandler&) = delete;
    DbusServerHandler(DbusServerHandler&&) = delete;
    DbusServerHandler& operator=(DbusServerHandler&&) = delete;

    void close();
    void handleRegisterMethod(const DbusRegisterMethod& msg);
    void handleUnregisterMethod(const DbusUnregisterMethod& msg);
    void handleMethodCallResult(const DbusMethodCallResult& msg);

private:
    struct RegisteredMethod{
        std::string ownerActorName;
        std::unique_ptr<sdbus::IObject> object;
    };

    Actor& actor_;
    std::mutex mutex_;
    std::atomic<uint64_t> nextCallId_{0};
    sdbus::IConnection& connection_;
    std::unordered_map<std::string, RegisteredMethod> methods_;
    std::unordered_map<uint64_t, sdbus::MethodCall> pendingMethodCalls_;
};

#endif // V2_PLATFORM_LINUX
