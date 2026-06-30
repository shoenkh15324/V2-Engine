#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/util/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <memory>
#include <unordered_map>

#if V2_PLATFORM_LINUX

#include <sdbus-c++/sdbus-c++.h>

class DbusActor : public Actor{
public:
    DbusActor(const std::string& name, uint64_t id, const std::string& dbusName, const std::string& dbusObjectPath, const std::string& dbusInterfaceName);
    ~DbusActor() override;

    DbusActor(const DbusActor&) = delete;
    DbusActor& operator=(const DbusActor&) = delete;
    DbusActor(DbusActor&&) = delete;
    DbusActor& operator=(DbusActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    std::string busName_;
    std::string objectPath_;
    std::string interfaceName_;
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unordered_map<std::string, std::unique_ptr<sdbus::IObject>> objects_;
    std::unordered_map<std::string, std::unique_ptr<sdbus::IProxy>> proxies_;
    int dbusFd_{-1};
};

#endif
