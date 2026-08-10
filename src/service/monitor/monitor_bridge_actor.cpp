#include "monitor_bridge_actor.hpp"
#include <cerrno>
#include <nlohmann/json.hpp>
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "service/monitor/monitor_data.hpp"

#if V2_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

MonitorBridgeActor::MonitorBridgeActor(std::string name, uint64_t id, std::string socketPath, int backlog)
    : Actor(std::move(name), id), socketPath_(socketPath), backlog_(backlog){}

MonitorBridgeActor::~MonitorBridgeActor(){
    unsubscribeAll();
    server_.shutdown();
}

int MonitorBridgeActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    if(server_.start(socketPath_, backlog_) != Ok){
        V2_LOG_ERROR("MonitorBridgeActor: failed to start UDS server on {}", socketPath_.c_str());
        state_ = Closed;
        return Fail;
    }
    ::chmod(socketPath_.c_str(), 0777);
    subscribeListener();
    //
    state_ = Opened;
    V2_LOG_INFO("Monitor Bridge Actor opened, listening on {}", socketPath_.c_str());
    return Ok;
}

int MonitorBridgeActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    unsubscribeAll();
    server_.shutdown();
    //
    state_ = Closed;
    V2_LOG_INFO("Monitor Bridge Actor closed");
    return Ok;
}

void MonitorBridgeActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    switch(msg.id()){
        case MessageId::MonitorNewConnection:{
            const auto& m = msg.as<MonitorNewConnection>();
            connections_.insert(m.conn);
            if(connections_.size() == 1) sendMsg("monitor", MonitorSubscribe{"monitor_bridge"}); // 첫 소켓: 수요 시작
            subscribeClient(m.conn);
            break;
        }
        case MessageId::MonitorClientDisconnected:{
            const auto& m = msg.as<MonitorClientDisconnected>();
            if(connections_.erase(m.conn) == 0) return;
            runtime()->eventLoop()->unsubscribe(m.conn);
            server_.closeClient(m.conn);
            if(connections_.empty()) sendMsg("monitor", MonitorUnsubscribe{"monitor_bridge"}); // 마지막 소켓: 수요 중단
            break;
        }
        case MessageId::MonitorSnapshotUpdate:{
            const auto& m = msg.as<MonitorSnapshotUpdate>();
            auto snap = m.snapshot;
            snap.clientCount = static_cast<int>(connections_.size());
            std::string payload = nlohmann::json(snap).dump() + "\n"; // to_json은 monitor_data.hpp
            for(ConnHandle conn : connections_){
                server_.send(conn, payload.data(), payload.size());
            }
            break;
        }
        default: break;
    }
}

void MonitorBridgeActor::subscribeListener(){
    auto* ioLoop = runtime()->eventLoop();
    int listenFd = server_.fd();
    IActorRuntime* ctx = runtime();
    ioLoop->subscribe(listenFd, [ctx, this](){
        ConnHandle conn = static_cast<ConnHandle>(server_.accept());
        if(conn >= 0) ctx->enqueue(Message::make(MonitorNewConnection{conn})); 
    });
}

void MonitorBridgeActor::subscribeClient(ConnHandle conn){
    IActorRuntime* ctx = runtime();
    runtime()->eventLoop()->subscribe(conn, [ctx, conn](){
        char b;
        ssize_t n = ::recv(conn, &b, 1, MSG_PEEK | MSG_DONTWAIT);
        if(n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
            ctx->enqueue(Message::make(MonitorClientDisconnected{conn})); // EOF/HUP 감지
    });
}

void MonitorBridgeActor::unsubscribeAll(){
    auto* ioLoop = runtime() ? runtime()->eventLoop() : nullptr;
    if(!ioLoop) return;
    for(ConnHandle conn : connections_){
        ioLoop->unsubscribe(conn);
        ::close(conn);
    }
    connections_.clear();
    if(server_.fd() >= 0) ioLoop->unsubscribe(server_.fd());
}

#endif // V2_PLATFORM_LINUX
