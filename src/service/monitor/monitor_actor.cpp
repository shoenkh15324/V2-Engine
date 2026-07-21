#include "monitor_actor.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/i_actor_registry.hpp"
#include "core/actor_system/runtime/dispatcher/dispatcher.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "infra/hal/sys/sys_linux.hpp"
#include "infra/hal/sys/sys_mock.hpp"
#include "infra/hal/pmu/pmu_rsp5.hpp"
#include "infra/hal/pmu/pmu_mock.hpp"
#include <memory>
#include <vector>

#if V2_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

MonitorActor::MonitorActor(const std::string& name, uint64_t id, MonitorConfig config)
    : Actor(std::move(name), id), config_(std::move(config)){
}

MonitorActor::~MonitorActor(){
    unsubscribeAll();
    server_.shutdown();
}

void MonitorActor::subscribeListener(){
    auto* dispatcher = runtime()->dispatcher();
    int listenFd = server_.fd();
    IActorRuntime* ctx = runtime();
    dispatcher->subscribe(listenFd, [this, ctx, listenFd](){
        ConnHandle conn = static_cast<ConnHandle>(server_.accept());
        if(conn >= 0){
            connections_.insert(conn);
            ctx->enqueue(MonitorNewConnection{conn});
        }
    });
}

void MonitorActor::subscribeClient(ConnHandle conn){
    auto* dispatcher = runtime()->dispatcher();
    IActorRuntime* ctx = runtime();
    dispatcher->subscribe(conn, [this, ctx, conn](){
        char buf[64];
        ssize_t n = ::recv(conn, buf, sizeof(buf), MSG_DONTWAIT);
        if(n <= 0 && (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))){
            ctx->enqueue(MonitorClientDisconnected{conn});
        }
    });
}

void MonitorActor::unsubscribeAll(){
    auto* dispatcher = runtime() ? runtime()->dispatcher() : nullptr;
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

void MonitorActor::collectActorInfo(std::vector<ActorInfo>& actors){
    runtime()->actorRegistry()->forEachActor([&](Actor* a){
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
        V2_LOG_ERROR("MonitorActor: failed to start UDS server on %s", config_.socketPath.c_str());
        state_ = Closed;
        return Fail;
    }
    ::chmod(config_.socketPath.c_str(), 0777);
    startTime_ = Time::now();

    sys_ = []() -> std::unique_ptr<ISys> {
#if V2_PLATFORM_LINUX
        return std::make_unique<SysLinux>();
#else
        return std::make_unique<SysMock>();
#endif
    }();
    if(sys_->open() != 0){
        V2_LOG_ERROR("MonitorActor: failed to open system resources HAL");
    }

    pmu_ = []() -> std::unique_ptr<IPmu> {
#if V2_PLATFORM_LINUX && defined(__aarch64__)
        return std::make_unique<PmuRsp5>();
#else
        return std::make_unique<PmuMock>();
#endif
    }();
    if(pmu_) pmu_->open();

    subscribeListener();
    startTimer(MonitorPoll{}, config_.pollIntervalMs, true);
    //
    state_ = Opened;
    V2_LOG_INFO("MonitorActor: listening on %s", config_.socketPath.c_str());
    return Ok;
}

int MonitorActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    unsubscribeAll();
    server_.shutdown();
    if(sys_){ sys_->close(); sys_.reset(); }
    if(pmu_){ pmu_->close(); pmu_.reset(); }
    //
    state_ = Closed;
    return Ok;
}

void MonitorActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const MonitorPoll&){
            MonitorSnapshot snap;
            prepareSnapshot(snap);
            broadcastSnapshot(snap);
        },
        [this](const MonitorNewConnection& msg){
            V2_LOG_INFO("MonitorActor: client connected (conn=%d)", msg.conn);
            connections_.insert(msg.conn);
            subscribeClient(msg.conn);
        },
        [this](const MonitorClientDisconnected& msg){
            if(connections_.find(msg.conn) == connections_.end()) return;
            runtime()->dispatcher()->unsubscribe(msg.conn);
            server_.closeClient(msg.conn);
            connections_.erase(msg.conn);
            V2_LOG_INFO("MonitorActor: client disconnected (conn=%d)", msg.conn);
        },
        [](const auto&){}
    }, msg);
}

#endif
