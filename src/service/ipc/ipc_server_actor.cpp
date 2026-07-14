#include "ipc_server_actor.hpp"
#include "core/actor_system/messages/cmd_messages.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <vector>
#include <cerrno>

#if V2_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

IpcServerActor::IpcServerActor(const std::string& name, uint64_t id, const std::string& socketPath, int backlog, int recvBufferSize) : Actor(std::move(name), id), socketPath_(socketPath), backlog_(backlog), recvBufferSize_(recvBufferSize){
    //
}

IpcServerActor::~IpcServerActor(){
    unsubscribeAll();
    server_.shutdown();
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

int IpcServerActor::handleCommand(ConnHandle conn, const std::string& cmd){
    V2_LOG_INFO("IpcServerActor: command received (conn=%d) [%s]", conn, cmd.c_str());
    sendMsg("cmd_actor", CmdRequest{conn, cmd});
    return Ok;
}

int IpcServerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    if(server_.start(socketPath_, backlog_) != Ok){ V2_LOG_ERROR("IpcServerActor: failed to start UDS server on %s", socketPath_.c_str());
        state_ = Closed;
        return Fail;
    }
    ::chmod(socketPath_.c_str(), 0777);
    subscribeListener();
    //
    state_ = Opened;
    V2_LOG_INFO("IpcServerActor: listening on %s", socketPath_.c_str());
    return Ok;
}

int IpcServerActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    unsubscribeAll();
    server_.shutdown();
    //
    state_ = Closed;
    return Ok;
}

void IpcServerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const IpcNewConnection& msg){
            V2_LOG_INFO("IpcServerActor: client connected (conn=%d)", msg.conn);
            subscribeClient(msg.conn);
        },
        [this](const IpcDataReceived& msg){
            std::vector<uint8_t> buf(recvBufferSize_);
            ssize_t n = ::recv(msg.conn, buf.data(), buf.size(), MSG_DONTWAIT);
            if(n > 0){
                V2_LOG_INFO("IpcServerActor: received %zd bytes from conn=%d", n, msg.conn);
                std::string cmd(reinterpret_cast<char*>(buf.data()), n);
                if(!cmd.empty() && cmd.back() == '\n') cmd.pop_back();
                handleCommand(msg.conn, cmd);
                actorContext()->dispatcher()->unsubscribe(msg.conn);
            }else if(n == 0){
                V2_LOG_INFO("IpcServerActor: client disconnected (conn=%d)", msg.conn);
                actorContext()->dispatcher()->unsubscribe(msg.conn);
                server_.closeClient(msg.conn);
                connections_.erase(msg.conn);
            }else if(errno != EAGAIN && errno != EWOULDBLOCK){
                //V2_LOG_ERROR("IpcServerActor: recv error (conn=%d) errno=%d", msg.conn, errno);
            }else{
               V2_LOG_ERROR("IpcServerActor: recv error (conn=%d) errno=%d", msg.conn, errno);
            }
        },
        [this](const CmdResponse& msg){
            V2_LOG_INFO("IpcServerActor: response received for conn=%d", msg.conn);
            server_.send(msg.conn, msg.result.data(), msg.result.size());
            actorContext()->dispatcher()->unsubscribe(msg.conn);
            connections_.erase(msg.conn);
            server_.closeClient(msg.conn);
        },
        [](const auto&){}
    }, msg);
}

#endif
