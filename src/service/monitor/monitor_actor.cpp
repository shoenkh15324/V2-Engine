#include "monitor_actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <vector>
#include <fstream>
#include <ctime>

#if V2_PLATFORM_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

MonitorActor::MonitorActor(const std::string& name, uint64_t id, const std::string& socketPath, int backlog, int recvBufferSize, int pollIntervalMs) : Actor(std::move(name), id), socketPath_(socketPath), backlog_(backlog), recvBufferSize_(recvBufferSize), pollIntervalMs_(pollIntervalMs){
    //
}

MonitorActor::~MonitorActor(){
    unsubscribeAll();
    server_.shutdown();
}

void MonitorActor::onStart(){
    if(server_.start(socketPath_, backlog_) != Ok){ V2_LOG_ERROR("MonitorActor: failed to start UDS server on %s", socketPath_.c_str());
        return;
    }
    ::chmod(socketPath_.c_str(), 0777);
    startTime_ = Time::now();
    subscribeListener();
    startTimer(MonitorPoll{}, pollIntervalMs_, true);
    V2_LOG_INFO("MonitorActor: listening on %s", socketPath_.c_str());
}

void MonitorActor::handle(const Message& msg){
    std::visit(overloaded{
        [this](const MonitorPoll&){
            MonitorSnapshot snap;
            snap.timestampMs = Time::nowMs();
            snap.clientCount = static_cast<int>(connections_.size());

            actorContext()->actorRegistry()->forEachActor([&](Actor* actor){
                ActorInfo actorInfo;
                actorInfo.name = actor->name();
                actorInfo.id = actor->id();
                actorInfo.mailboxCount = actor->mailboxCount();
                actorInfo.mailboxCapacity = actor->mailboxCapacity();
                snap.actors.push_back(std::move(actorInfo));
            });

            collectSystemResources(snap.resources);

            std::string data = serializeSnapshot(snap);
            for(ConnHandle conn : connections_){
                server_.send(conn, data.data(), data.size());
            }
        },
        [this](const MonitorNewConnection& ev){
            V2_LOG_INFO("MonitorActor: client connected (conn=%d)", ev.conn);
            connections_.insert(ev.conn);
            subscribeClient(ev.conn);
        },
        [this](const MonitorClientDisconnected& ev){
            if(connections_.find(ev.conn) == connections_.end()) return;
            actorContext()->dispatcher()->unsubscribe(ev.conn);
            server_.closeClient(ev.conn);
            connections_.erase(ev.conn);
            V2_LOG_INFO("MonitorActor: client disconnected (conn=%d)", ev.conn);
        },
        [](const auto&){}
    }, msg);
}

void MonitorActor::subscribeListener(){
    auto* dispatcher = actorContext()->dispatcher();
    int listenFd = server_.fd();
    ActorContext* ctx = actorContext();
    dispatcher->subscribe(listenFd, [this, ctx, listenFd](){
        ConnHandle conn = static_cast<ConnHandle>(server_.accept());
        if(conn >= 0){
            connections_.insert(conn);
            ctx->enqueue(MonitorNewConnection{conn});
        }
    });
}

void MonitorActor::subscribeClient(ConnHandle conn){
    auto* dispatcher = actorContext()->dispatcher();
    ActorContext* ctx = actorContext();
    dispatcher->subscribe(conn, [this, ctx, conn](){
        char buf[64];
        ssize_t n = ::recv(conn, buf, sizeof(buf), MSG_DONTWAIT);
        if(n <= 0 && (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))){
            ctx->enqueue(MonitorClientDisconnected{conn});
        }
    });
}

void MonitorActor::unsubscribeAll(){
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

void MonitorActor::collectSystemResources(SystemResources& resources){
#if V2_PLATFORM_LINUX
    std::ifstream status("/proc/self/status");
    std::string line;
    while(std::getline(status, line)){
        if(line.compare(0, 6, "VmRSS:") == 0){
            resources.memoryRssKb = std::stoul(line.substr(6));
        }else if(line.compare(0, 7, "VmSize:") == 0){
            resources.memoryTotalKb = std::stoul(line.substr(7));
        }
    }

    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    uint64_t cpuNs = (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
    uint64_t nowWall = Time::nowMs();

    cpuHistory_.push_back({cpuNs, nowWall});
    while(cpuHistory_.size() > 1 && nowWall - cpuHistory_.front().wallMs > kCpuWindowMs)
        cpuHistory_.pop_front();

    if(cpuHistory_.size() >= 2){
        auto& first = cpuHistory_.front();
        auto& last = cpuHistory_.back();
        uint64_t deltaCpuNs = last.cpuNs - first.cpuNs;
        uint64_t deltaWall = last.wallMs - first.wallMs;
        if(deltaWall > 0){
            resources.cpuPercent = (double)deltaCpuNs * 100.0 / (deltaWall * 1000000.0);
            if(resources.cpuPercent > 100.0f) resources.cpuPercent = 100.0f;
        }
    }
#endif
}

#endif
