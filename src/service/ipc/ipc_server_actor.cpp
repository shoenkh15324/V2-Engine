#include "ipc_server_actor.hpp"

#if V2_ENABLE_IPC_SERVER_ACTOR
#include "core/common/config.h"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/common/log.hpp"
#include "core/common/return.hpp"

#if V2_PLATFORM_LINUX
    #include <sys/socket.h>
    #include <sys/stat.h>
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
    ::chmod(socketPath_.c_str(), 0777);
    startTime_ = Time::now();
    subscribeListener();
    V2_LOG_INFO("IpcServerActor: listening on %s", socketPath_.c_str());
}

int IpcServerActor::handleCommand(ConnHandle conn, const std::string& cmd){
    V2_LOG_INFO("IpcServerActor: command received (conn=%d) [%s]", conn, cmd.c_str());
    /*
     * Response format (key: value lines, parsed by CLI):
     *   key: value
     *   error: <message>
     *   cmd_name: Description    ← for help listing
     */
    std::string response;
    if(cmd == "info"){
        auto uptimeMs = Time::toMs(Time::now() - startTime_);
        response += "name: " V2_ENGINE_NAME "\n";
        response += "version: " V2_ENGINE_VERSION "\n";
        response += "uptime: " + std::to_string(uptimeMs) + "\n";
        response += "clients: " + std::to_string(connections_.size());
    }else{
        response = "error: unknown command '" + cmd + "'";
    }
    server_.send(conn, response.data(), response.size());
    actorContext()->dispatcher()->unsubscribe(conn);
    connections_.erase(conn);
    server_.closeClient(conn);
    return Ok;
}

void IpcServerActor::handle(const Message& msg){
    std::visit(overloaded{
        [this](const IpcNewConnection& ev){
            V2_LOG_INFO("IpcServerActor: client connected (conn=%d)", ev.conn);
            subscribeClient(ev.conn);
        },
        [this](const IpcDataReceived& ev){
            uint8_t buf[V2_IPC_RECV_BUFFER_SIZE];
            ssize_t n = ::recv(ev.conn, buf, sizeof(buf), MSG_DONTWAIT);
            if(n > 0){
                V2_LOG_INFO("IpcServerActor: received %zd bytes from conn=%d", n, ev.conn);
                std::string cmd(reinterpret_cast<char*>(buf), n);
                if(!cmd.empty() && cmd.back() == '\n') cmd.pop_back();
                handleCommand(ev.conn, cmd);
            }else if(n == 0){
                V2_LOG_INFO("IpcServerActor: client disconnected (conn=%d)", ev.conn);
                actorContext()->dispatcher()->unsubscribe(ev.conn);
                server_.closeClient(ev.conn);
                connections_.erase(ev.conn);
            }else{
                V2_LOG_ERROR("IpcServerActor: recv error (conn=%d)", ev.conn);
            }
        },
        [](const auto&){ // ← catch-all: 관심 없는 메시지는 무시
        }
    }, msg);
}

void IpcServerActor::subscribeListener(){
    auto* dispatcher = actorContext()->dispatcher();
    int listenFd = server_.fd();
    ActorContext* ctx = actorContext();
    dispatcher->subscribe(listenFd, [ctx, this, listenFd](){
        ConnHandle conn = static_cast<ConnHandle>(server_.accept());
        if(conn >= 0){
            connections_.insert(conn);
            ctx->enqueue(IpcNewConnection{conn});
        }
    });
}

void IpcServerActor::subscribeClient(ConnHandle conn){
    auto* dispatcher = actorContext()->dispatcher();
    ActorContext* ctx = actorContext();
    dispatcher->subscribe(conn, [ctx, this, conn](){
        ctx->enqueue(IpcDataReceived{conn});
    });
}

void IpcServerActor::unsubscribeAll(){
    auto* dispatcher = actorContext() ? actorContext()->dispatcher() : nullptr;
    if(!dispatcher) return;
    for(ConnHandle conn : connections_){
        dispatcher->unsubscribe(conn);
        ::close(conn);
    }
    connections_.clear();
    if(server_.fd() >= 0){
        dispatcher->unsubscribe(server_.fd());
    }
}
#endif