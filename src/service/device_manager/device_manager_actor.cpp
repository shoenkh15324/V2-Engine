#include "device_manager_actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"

using DeviceManagerActorMessages = std::tuple<PmuDataSubscribe, PmuDataUnsubscribe, PmuDataTick>;

DeviceManagerActor::DeviceManagerActor(std::string name, uint64_t id, IPmu* pmu, int pollIntervalMs)
    : Actor(std::move(name), id), pmu_(pmu), pollIntervalMs_(pollIntervalMs){}

DeviceManagerActor::~DeviceManagerActor(){
    close();
}

int DeviceManagerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    startTimer(PmuDataTick{}, pollIntervalMs_, true);
    //
    state_ = Opened;
    V2_LOG_INFO("Device Manager Actor opened");
    return Ok;
}

int DeviceManagerActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    latestPmuData_ = {};
    //
    state_ = Closed;
    V2_LOG_INFO("Device Manager closed");
    return Ok;
}

void DeviceManagerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Device Manager is not opened"); return; }
    dispatch(*this, msg, DeviceManagerActorMessages{});
}

void DeviceManagerActor::handle(const PmuDataSubscribe& m){
    subscribers_.insert(m.subscriber);
    pumpIfNeeded(); // 구독 즉시 최신 캐시 1회 발행
}

void DeviceManagerActor::handle(const PmuDataUnsubscribe& m){
    subscribers_.erase(m.subscriber);
    if(subscribers_.empty()) latestPmuData_ = {}; // stale 캐시 폐기
}

void DeviceManagerActor::handle(const PmuDataTick&){
    if(!subscribers_.empty()) pumpIfNeeded(); // 수요 없으면 vcgencmd 아예 안 돎
}

void DeviceManagerActor::pumpIfNeeded(){
    if(!pmu_) return;
    if(pmu_->readPmuData(latestPmuData_) != Ok){
        V2_LOG_ERROR("DeviceManagerActor: readPmuData failed");
        return;
    }
    for(const auto& sub : subscribers_){
        sendMsg(sub, PmuDataUpdate{latestPmuData_});
    }
}
