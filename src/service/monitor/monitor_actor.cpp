#include "monitor_actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "service/monitor/monitor_messages.hpp"
#include "service/system_manager/system_manager_messages.hpp"
#include "service/device_manager/device_manager_messages.hpp"

using MonitorActorMessages = std::tuple<MonitorSubscribe, MonitorUnsubscribe, SysDataUpdate, PmuDataUpdate>;

MonitorActor::MonitorActor(std::string name, uint64_t id) : Actor(std::move(name), id){}

int MonitorActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    //
    state_ = Opened;
    V2_LOG_INFO("Monitor Actor opened");
    return Ok;
}

int MonitorActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    subscribers_.clear();
    dataSubscribed_ = false;
    sysResCache_ = {};
    pmuDataCache_ = {};
    //
    state_ = Closed;
    V2_LOG_INFO("Monitor Actor closed");
    return Ok;
}

void MonitorActor::handle(const Message& msg){
    if(state_ < Opened) return;
    dispatch(*this, msg, MonitorActorMessages{});
}

void MonitorActor::handle(const MonitorSubscribe& m){
    subscribers_.insert(m.subscriber);
    if(!dataSubscribed_) subscribeToData(); // 첫 구독자 -> sys/pmu 구독 시작
}

void MonitorActor::handle(const MonitorUnsubscribe& m){
    subscribers_.erase(m.subscriber);
    if(subscribers_.empty() && dataSubscribed_) unsubscribeFromData(); // 마지막 -> 해제
}

void MonitorActor::handle(const SysDataUpdate& m){
    sysResCache_ = m.data;
    tryPublish();
}

void MonitorActor::handle(const PmuDataUpdate& m){
    pmuDataCache_ = m.data;
    tryPublish();
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

void MonitorActor::subscribeToData(){
    dataSubscribed_ = true;
    sendMsg("system_manager", SysDataSubscribe{"monitor"});
    sendMsg("device_manager", PmuDataSubscribe{"monitor"});
}

void MonitorActor::unsubscribeFromData(){
    dataSubscribed_ = false;
    sendMsg("system_manager", SysDataUnsubscribe{"monitor"});
    sendMsg("device_manager", PmuDataUnsubscribe{"monitor"});
    sysResCache_ = {};
    pmuDataCache_ = {};
}

void MonitorActor::tryPublish(){
    if(subscribers_.empty()) return;
    MonitorSnapshot snap;
    snap.timestampMs = Time::nowMs();
    snap.clientCount = 0;
    collectActorInfo(snap.actors);
    snap.sysResources = sysResCache_;
    snap.pmuData = pmuDataCache_;
    for(const auto& sub : subscribers_){
        sendMsg(sub, MonitorSnapshotUpdate{snap});
    }
}
