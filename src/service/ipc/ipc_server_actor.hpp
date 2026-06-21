#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config.h"
#include "core/common/time.hpp"
#include "infra/transport/uds/uds_server.hpp"
#include <unordered_set>

#if V2_ENABLE_IPC_SERVER_ACTOR
class IpcServerActor : public Actor{
public:
    IpcServerActor(const std::string& name, uint64_t id, const std::string& socketPath);
    ~IpcServerActor() override;
    
    IpcServerActor(const IpcServerActor&) = delete;
    IpcServerActor& operator=(const IpcServerActor&) = delete;
    IpcServerActor(IpcServerActor&&) = delete;
    IpcServerActor& operator=(IpcServerActor&&) = delete;

    void onStart() override;
    void handle(const Message& msg) override;
    void open();

private:
    void subscribeListener();
    void subscribeClient(ConnHandle conn);
    void unsubscribeAll();
    int handleCommand(ConnHandle conn, const std::string& cmd);

    UdsServer server_;
    std::string socketPath_;
    std::unordered_set<ConnHandle> connections_;
    Time::TimeStamp startTime_{};
};
#endif
