#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "service/dbus/dbus_messages.hpp"
#include <memory>

#if V2_PLATFORM_LINUX

#include <sdbus-c++/sdbus-c++.h>

class DbusServerHandler;
class DbusClientHandler;

class DbusActor : public Actor{
public:
    DbusActor(std::string name, uint64_t id, std::string dbusName, std::string dbusObjectPath, std::string dbusInterfaceName);
    ~DbusActor() override;

    DbusActor(const DbusActor&) = delete;
    DbusActor& operator=(const DbusActor&) = delete;
    DbusActor(DbusActor&&) = delete;
    DbusActor& operator=(DbusActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

    sdbus::IConnection& connection(){ return *connection_; }

private:
    std::string busName_;
    std::string objectPath_;
    std::string interfaceName_;
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<DbusServerHandler> serverHandler_;
    std::unique_ptr<DbusClientHandler> clientHandler_;
};

#endif
