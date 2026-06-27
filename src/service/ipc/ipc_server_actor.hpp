#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/util/platform_config.h"
#include "core/common/time/time.hpp"
#include "infra/transport/uds/uds_server.hpp"
#include <unordered_set>

class IpcServerActor : public Actor{
public:
    IpcServerActor(const std::string& name, uint64_t id, const std::string& socketPath, int backlog = 5, int recvBufferSize = 4096);
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
    int backlog_;
    int recvBufferSize_;
    std::unordered_set<ConnHandle> connections_;
    Time::TimeStamp startTime_{};
};
