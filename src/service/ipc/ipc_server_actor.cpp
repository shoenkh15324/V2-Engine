#include "ipc_server_actor.hpp"

#if V2_ENABLE_IPC
#include "core/common/config.h"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/common/log.hpp"
#include "core/common/return.hpp"

#if V2_PLATFORM_LINUX
    #include <sys/socket.h>
    #include <unistd.h>
#endif

IpcServerActor::IpcServerActor(const std::string& name, uint64_t id, const std::string& socketPath) : Actor(std::move(name), id), socketPath_(socketPath){
    //
}

IpcServerActor::~IpcServerActor(){
    unsubscribeAll();
    server_.shutdown();
}

void IpcServerActor::onStart(){
    open();
}

void IpcServerActor::open(){
    if(server_.start(socketPath_) != Ok){ V2_LOG_ERROR("IpcServerActor: failed to start UDS server on %s", socketPath_.c_str());
        return;
    }
    subscribeListenFd();
    V2_LOG_INFO("IpcServerActor: listening on %s", socketPath_.c_str());
}

void IpcServerActor::handle(const Message& msg){
    std::visit(overloaded{
        [this](const IpcNewConnection& ev){
            V2_LOG_INFO("IpcServerActor: client connected (fd=%d)", ev.clientFd);
            subscribeClientFd(ev.clientFd);
        },
        [this](const IpcDataReceived& ev){
            uint8_t buf[V2_IPC_RECV_BUFFER_SIZE];
            ssize_t n = ::recv(ev.clientFd, buf, sizeof(buf), MSG_DONTWAIT);
            if(n > 0){
                V2_LOG_INFO("IpcServerActor: received %zd bytes from fd=%d", n, ev.clientFd);
                server_.send(ev.clientFd, buf, static_cast<size_t>(n));
            }else if(n == 0){
                V2_LOG_INFO("IpcServerActor: client disconnected (fd=%d)", ev.clientFd);
                context()->dispatcher()->unsubscribe(ev.clientFd);
                server_.closeClient(ev.clientFd);
                clientFds_.erase(ev.clientFd);
            }else{
                V2_LOG_ERROR("IpcServerActor: recv error (fd=%d)", ev.clientFd);
            }
        },
        [](const auto&){ // ← catch-all: 관심 없는 메시지는 무시
        }
    }, msg);
}

void IpcServerActor::subscribeListenFd(){
    auto* dispatcher = context()->dispatcher();
    int listenFd = server_.fd();
    ActorContext* ctx = context();
    dispatcher->subscribe(listenFd, [ctx, this, listenFd](){
        int clientFd = server_.accept();
        if(clientFd >= 0){
            clientFds_.insert(clientFd);
            ctx->enqueue(IpcNewConnection{clientFd});
        }
    });
}

void IpcServerActor::subscribeClientFd(int clientFd){
    auto* dispatcher = context()->dispatcher();
    ActorContext* ctx = context();
    dispatcher->subscribe(clientFd, [ctx, this, clientFd](){
        ctx->enqueue(IpcDataReceived{clientFd});
    });
}

void IpcServerActor::unsubscribeAll(){
    auto* dispatcher = context() ? context()->dispatcher() : nullptr;
    if(!dispatcher) return;
    for(int fd : clientFds_){
        dispatcher->unsubscribe(fd);
        ::close(fd);
    }
    clientFds_.clear();
    if(server_.fd() >= 0){
        dispatcher->unsubscribe(server_.fd());
    }
}
#endif