#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "infra/transport/uds/uds_server.hpp"
#include <unordered_set>

#if V2_PLATFORM_LINUX

class IpcServerActor : public Actor{
public:
    IpcServerActor(const std::string& name, uint64_t id, const std::string& socketPath, int backlog = 5, int recvBufferSize = 4096);
    ~IpcServerActor() override;
    
    IpcServerActor(const IpcServerActor&) = delete;
    IpcServerActor& operator=(const IpcServerActor&) = delete;
    IpcServerActor(IpcServerActor&&) = delete;
    IpcServerActor& operator=(IpcServerActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    void subscribeListener();
    void subscribeClient(ConnHandle conn);
    void unsubscribeAll();
    int handleCommand(ConnHandle conn, const std::string& cmd);

    UdsServer server_;
    std::string socketPath_;
    std::unordered_set<ConnHandle> connections_;
    int backlog_;
    int recvBufferSize_;
};

#endif
