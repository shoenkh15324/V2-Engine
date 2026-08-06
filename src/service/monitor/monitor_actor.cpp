#include "monitor_actor.hpp"
#include <memory>
#include <vector>
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/messages/system_messages.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "service/monitor/monitor_messages.hpp"

#if V2_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

MonitorActor::MonitorActor(std::string name, uint64_t id, MonitorConfig config, ISys* sys, IPmu* pmu)
    : Actor(std::move(name), id), config_(std::move(config)), sys_(sys), pmu_(pmu){
        //
}

MonitorActor::~MonitorActor(){
    unsubscribeAll();
    server_.shutdown();
}

void MonitorActor::subscribeListener(){
    auto* ioLoop = runtime()->eventLoop();
    int listenFd = server_.fd();
    IActorRuntime* ctx = runtime();
    ioLoop->subscribe(listenFd, [this, ctx, listenFd](){
        ConnHandle conn = static_cast<ConnHandle>(server_.accept());
        if(conn >= 0){
            connections_.insert(conn);
            ctx->enqueue(Message::make(MonitorNewConnection{conn}));
        }
    });
}

void MonitorActor::subscribeClient(ConnHandle conn){
    auto* ioLoop = runtime()->eventLoop();
    IActorRuntime* ctx = runtime();
    ioLoop->subscribe(conn, [this, ctx, conn](){
        char buf[64];
        ssize_t n = ::recv(conn, buf, sizeof(buf), MSG_DONTWAIT);
        if(n <= 0 && (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))){
            ctx->enqueue(Message::make(MonitorClientDisconnected{conn}));
        }
    });
}

void MonitorActor::unsubscribeAll(){
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

void MonitorActor::collectActorInfo(std::vector<ActorInfo>& actors){
    runtime()->actorRegistry()->forEachActor([&](ActorHandle h){
        Actor* a = h.get();
        if(!a) return;
        ActorInfo info;
        info.name = a->name();
        info.id = a->id();
        info.mailboxCount = a->mailboxCount();
        info.mailboxCapacity = a->mailboxCapacity();
        info.state = static_cast<int>(a->getState());
        info.essential = a->isEssential();
        actors.push_back(std::move(info));
    });
}

void MonitorActor::collectSystemResources(SystemResources& resources){
    if(sys_) sys_->collect(resources);
    resources.uptimeMs = static_cast<uint64_t>(Time::toMs(Time::now() - startTime_));
}

void MonitorActor::prepareSnapshot(MonitorSnapshot& snap){
    snap.timestampMs = Time::nowMs();
    snap.clientCount = static_cast<int>(connections_.size());
    collectActorInfo(snap.actors);
    collectSystemResources(snap.resources);
    if(pmu_) pmu_->readPmuData(snap.pmu);
}

void MonitorActor::broadcastSnapshot(const MonitorSnapshot& snap){
    std::string data = nlohmann::json(snap).dump() + "\n";
    for(ConnHandle conn : connections_){
        server_.send(conn, data.data(), data.size());
    }
}

int MonitorActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    if(server_.start(config_.socketPath, config_.backlog) != Ok){
        V2_LOG_ERROR("MonitorActor: failed to start UDS server on {}", config_.socketPath.c_str());
        state_ = Closed;
        return Fail;
    }
    ::chmod(config_.socketPath.c_str(), 0777);
    startTime_ = Time::now();
    subscribeListener();
    startTimer(MonitorPoll{}, config_.pollIntervalMs, true);
    //
    state_ = Opened;
    V2_LOG_INFO("Monitor Actor opened");
    V2_LOG_INFO("MonitorActor: listening on {}", config_.socketPath.c_str());
    return Ok;
}

int MonitorActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    unsubscribeAll();
    server_.shutdown();
    sys_ = nullptr;
    pmu_ = nullptr;
    //
    state_ = Closed;
    V2_LOG_INFO("Monitor Actor closed");
    return Ok;
}

void MonitorActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    switch(msg.id()){
    case MessageId::MonitorPoll:{
        MonitorSnapshot snap;
        prepareSnapshot(snap);
        broadcastSnapshot(snap);
        break;
    }
    case MessageId::MonitorNewConnection:{
        const auto& m = msg.as<MonitorNewConnection>();
        V2_LOG_INFO("MonitorActor: client connected (conn={})", m.conn);
        connections_.insert(m.conn);
        subscribeClient(m.conn);
        break;
    }
    case MessageId::MonitorClientDisconnected:{
        const auto& m = msg.as<MonitorClientDisconnected>();
        if(connections_.find(m.conn) == connections_.end()) return;
        runtime()->eventLoop()->unsubscribe(m.conn);
        server_.closeClient(m.conn);
        connections_.erase(m.conn);
        V2_LOG_INFO("MonitorActor: client disconnected (conn={})", m.conn);
        break;
    }
    default:
        break;
    }
}

#endif
