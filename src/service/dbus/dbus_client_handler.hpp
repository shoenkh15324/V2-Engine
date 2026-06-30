#pragma once
#include "core/common/util/platform_config.h"
#include <string>
#include <memory>
#include <unordered_map>

#if V2_PLATFORM_LINUX
#include <sdbus-c++/sdbus-c++.h>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/dbus_messages.hpp"

class DbusClientHandler{
public:
    DbusClientHandler(sdbus::IConnection& connection, Actor& actor);
    ~DbusClientHandler();

    DbusClientHandler(const DbusClientHandler&) = delete;
    DbusClientHandler& operator=(const DbusClientHandler&) = delete;
    DbusClientHandler(DbusClientHandler&&) = delete;
    DbusClientHandler& operator=(DbusClientHandler&&) = delete;

    void close();
    void handleProxyCallRequest(const DbusProxyCallRequest& msg);
    void handleSubscribeSignal(const DbusSubscribeSignal& msg);
    void handleUnsubscribeSignal(const DbusSubscribeSignal& msg);

private:
    struct SignalSubscription{
        std::string subscriberActorName;
        std::unique_ptr<sdbus::IProxy> proxy;
    };

    sdbus::IProxy* createProxy(const std::string& destination, const std::string& objectPath);

    Actor& actor_;
    std::unordered_map<std::string, std::unique_ptr<sdbus::IProxy>> proxies_;
    std::unordered_map<std::string, SignalSubscription> signalSubscriptions_;
    sdbus::IConnection& connection_;
};

#endif // V2_PLATFORM_LINUX
