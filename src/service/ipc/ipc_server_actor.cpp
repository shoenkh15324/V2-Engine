#include "ipc_server_actor.hpp"
#include "service/cmd/cmd_messages.hpp"
#include "service/ipc/ipc_messages.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <vector>
#include <cerrno>

#if V2_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using IpcServerActorMessages = std::tuple<IpcNewConnection, IpcDataReceived, CmdResponse>;

IpcServerActor::IpcServerActor(std::string name, uint64_t id, std::string socketPath, int backlog, int recvBufferSize)
 : Actor(std::move(name), id), socketPath_(std::move(socketPath)), backlog_(backlog), recvBufferSize_(recvBufferSize){}

IpcServerActor::~IpcServerActor(){
    unsubscribeAll();
    server_.shutdown();
}


void IpcServerActor::subscribeListener(){
    auto* ioLoop = runtime()->eventLoop();
    int listenFd = server_.fd();
    IActorRuntime* ctx = runtime();
    ioLoop->subscribe(listenFd, [ctx, this, listenFd](){
        ConnHandle conn = static_cast<ConnHandle>(server_.accept());
        if(conn >= 0){
            connections_.insert(conn);
            ctx->enqueue(Message::make(IpcNewConnection{conn}));
        }
    });
}

void IpcServerActor::subscribeClient(ConnHandle conn){
    auto* ioLoop = runtime()->eventLoop();
    IActorRuntime* ctx = runtime();
    ioLoop->subscribe(conn, [ctx, this, conn](){
        ctx->enqueue(Message::make(IpcDataReceived{conn}));
    });
}

void IpcServerActor::unsubscribeAll(){
    auto* ioLoop = runtime() ? runtime()->eventLoop() : nullptr;
    if(!ioLoop) return;
    for(ConnHandle conn : connections_){
        ioLoop->unsubscribe(conn);
        ::close(conn);
    }
    connections_.clear();
    if(server_.fd() >= 0){
        ioLoop->unsubscribe(server_.fd());
    }
}

int IpcServerActor::handleCommand(ConnHandle conn, const std::string& cmd){
    V2_LOG_INFO("IpcServerActor: command received (conn={}) [{}]", conn, cmd.c_str());
    sendMsg("cmd", CmdRequest{conn, cmd});
    return Ok;
}

int IpcServerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    if(server_.start(socketPath_, backlog_) != Ok){ V2_LOG_ERROR("IpcServerActor: failed to start UDS server on {}", socketPath_.c_str());
        state_ = Closed;
        return Fail;
    }
    ::chmod(socketPath_.c_str(), 0777);
    subscribeListener();
    //
    state_ = Opened;
    V2_LOG_INFO("Ipc Server Actor opened");
    V2_LOG_INFO("IpcServerActor: listening on {}", socketPath_.c_str());
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
    V2_LOG_INFO("Ipc Server Actor closed");
    return Ok;
}

void IpcServerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    dispatch(*this, msg, IpcServerActorMessages{});
}

void IpcServerActor::handle(const IpcNewConnection& m){
    V2_LOG_INFO("IpcServerActor: client connected (conn={})", m.conn);
    subscribeClient(m.conn);
}

void IpcServerActor::handle(const IpcDataReceived& m){
    std::vector<uint8_t> buf(recvBufferSize_);
    ssize_t n = ::recv(m.conn, buf.data(), buf.size(), MSG_DONTWAIT);
    if(n > 0){
        V2_LOG_INFO("IpcServerActor: received {} bytes from conn={}", n, m.conn);
        std::string cmd(reinterpret_cast<char*>(buf.data()), n);
        if(!cmd.empty() && cmd.back() == '\n') cmd.pop_back();
        handleCommand(m.conn, cmd);
        runtime()->eventLoop()->unsubscribe(m.conn);
    }else if(n == 0){
        V2_LOG_INFO("IpcServerActor: client disconnected (conn={})", m.conn);
        runtime()->eventLoop()->unsubscribe(m.conn);
        server_.closeClient(m.conn);
        connections_.erase(m.conn);
    }else if(errno == EAGAIN && errno == EWOULDBLOCK){
        // Not an error — no more data, ignore
    }else{
        //V2_LOG_ERROR("IpcServerActor: recv error (conn={}) errno={}", m.conn, errno);
    }
}

void IpcServerActor::handle(const CmdResponse& m){
    V2_LOG_INFO("IpcServerActor: response received for conn={}", m.conn);
    server_.send(m.conn, m.result.data(), m.result.size());
    if(m.closeOnSend){
        runtime()->eventLoop()->unsubscribe(m.conn);
        connections_.erase(m.conn);
        server_.closeClient(m.conn);
    }
}

#endif
