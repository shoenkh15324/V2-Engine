#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "infra/transport/uds/uds_server.hpp"
#include <unordered_set>

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
    void subscribeListenFd();
    void subscribeClientFd(int clientFd);
    void unsubscribeAll();

    UdsServer server_;
    std::string socketPath_;
    std::unordered_set<int> clientFds_;
};
