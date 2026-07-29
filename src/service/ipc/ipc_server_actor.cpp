#include "ipc_server_actor.hpp"
#include "core/actor_system/messages/cmd_messages.hpp"
#include "core/actor_system/messages/ipc_messages.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <vector>
#include <cerrno>

#if V2_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

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
    V2_LOG_INFO("IpcServerActor: command received (conn=%d) [%s]", conn, cmd.c_str());
    sendMsg("cmd_actor", Message::make(CmdRequest{conn, cmd}));
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
    V2_LOG_INFO("Ipc Server Actor opened");
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
    V2_LOG_INFO("Ipc Server Actor closed");
    return Ok;
}

void IpcServerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    switch(msg.id()){
    case MessageId::IpcNewConnection:{
        const auto& m = msg.as<IpcNewConnection>();
        V2_LOG_INFO("IpcServerActor: client connected (conn=%d)", m.conn);
        subscribeClient(m.conn);
        break;
    }
    case MessageId::IpcDataReceived:{
        const auto& m = msg.as<IpcDataReceived>();
        std::vector<uint8_t> buf(recvBufferSize_);
        ssize_t n = ::recv(m.conn, buf.data(), buf.size(), MSG_DONTWAIT);
        if(n > 0){
            V2_LOG_INFO("IpcServerActor: received %zd bytes from conn=%d", n, m.conn);
            std::string cmd(reinterpret_cast<char*>(buf.data()), n);
            if(!cmd.empty() && cmd.back() == '\n') cmd.pop_back();
            handleCommand(m.conn, cmd);
            runtime()->eventLoop()->unsubscribe(m.conn);
        }else if(n == 0){
            V2_LOG_INFO("IpcServerActor: client disconnected (conn=%d)", m.conn);
            runtime()->eventLoop()->unsubscribe(m.conn);
            server_.closeClient(m.conn);
            connections_.erase(m.conn);
        }else if(errno != EAGAIN && errno != EWOULDBLOCK){
            //V2_LOG_ERROR("IpcServerActor: recv error (conn=%d) errno=%d", m.conn, errno);
        }else{
           V2_LOG_ERROR("IpcServerActor: recv error (conn=%d) errno=%d", m.conn, errno);
        }
        break;
    }
    case MessageId::CmdResponse:{
        const auto& m = msg.as<CmdResponse>();
        V2_LOG_INFO("IpcServerActor: response received for conn=%d", m.conn);
        server_.send(m.conn, m.result.data(), m.result.size());
        runtime()->eventLoop()->unsubscribe(m.conn);
        connections_.erase(m.conn);
        server_.closeClient(m.conn);
        break;
    }
    default:
        break;
    }
}

#endif
