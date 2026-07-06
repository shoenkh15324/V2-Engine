#include "pmu_actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/messages/pmu_messages.hpp"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"
#include "infra/hal/pmu/pmu_rsp5.hpp"
#include "infra/hal/pmu/pmu_mock.hpp"

PmuActor::PmuActor(const std::string& name, uint64_t id, int pollIntervalMs) : Actor(std::move(name), id), pollIntervalMs_(pollIntervalMs){
    //
}

PmuActor::~PmuActor(){
    close();
}

void PmuActor::collectAndBroadcast(){
    if(!pmu_ || !pmu_->isOpen()) return;
    PmuData data;
    if(pmu_->readPmuData(data)){
        latestData_ = data;
        sendMsg("monitor", SendPmuData{data});
    }
}

bool PmuActor::hasVacgencmd(){
    auto pipe = popen("vcgencmd version 2>/dev/null", "r");
    if(!pipe) return false;
    int ret = pclose(pipe);
    return ret == 0;
}

int PmuActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    if(hasVacgencmd()){
        pmu_ = std::make_unique<PmuRsp5>();
    }else{
        pmu_ = std::make_unique<PmuMock>();
    }
    if(pmu_->open() != Ok){ V2_LOG_ERROR("Pmu Actor: failed to opend PMU driver");
        state_ = Closed;
        return Fail;
    }
    startTimer(PmuPoll{}, pollIntervalMs_, true);
    //
    state_ = Opened;
    V2_LOG_INFO("PmuActor: started (poll interval %d ms)", pollIntervalMs_);
    return Ok;
}

int PmuActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    if(pmu_) pmu_->close();
    //
    state_ = Closed;
    return Ok;
}

void PmuActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const PmuPoll& msg){
            collectAndBroadcast();
        },
        [](const auto&){}
    }, msg);
}
