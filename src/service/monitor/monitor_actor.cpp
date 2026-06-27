#include "monitor_actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <vector>
#include <fstream>
#include <sstream>

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
                MonitorSnapshot::ActorInfo actorInfo;
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

std::string MonitorActor::serializeSnapshot(const MonitorSnapshot& snap){
    std::string data;
    data += "timestamp:" + std::to_string(snap.timestampMs) + "\n";
    data += "clients:" + std::to_string(snap.clientCount) + "\n";
    for(const auto& actor : snap.actors){
        data += "actor:" + actor.name +
                " id:" + std::to_string(actor.id) +
                " mb:" + std::to_string(actor.mailboxCount) +
                "/" + std::to_string(actor.mailboxCapacity) + "\n";
    }
    data += "mem_rss:" + std::to_string(snap.resources.memoryRssKb) + "\n";
    data += "mem_total:" + std::to_string(snap.resources.memoryTotalKb) + "\n";
    data += "cpu:" + std::to_string(snap.resources.cpuPercent) + "\n";
    return data;
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

void MonitorActor::collectSystemResources(MonitorSnapshot::SystemResources& resources){
#if V2_PLATFORM_LINUX
    std::ifstream status("/proc/self/status");
    std::string line;
    while(std::getline(status, line)){
        if(line.compare(0, 7, "VmRss:") == 0){
            resources.memoryRssKb = std::stoul(line.substr(7));
        }else if(line.compare(0, 8, "VmSize:") == 0){
            resources.memoryTotalKb = std::stoul(line.substr(8));
        }
    }
    std::ifstream stat("/proc/self/stat");
    uint64_t utime, stime;
    std::string ignore;
    for(int i = 0; i < 14; i++){
        if(i == 0){
            stat >> ignore;
        }else if(i == 1){
            std::getline(stat, ignore, ')');
        }else{
            stat >> ignore;
        }
    }
    (void)utime; (void)stime;
#endif
}

#endif
