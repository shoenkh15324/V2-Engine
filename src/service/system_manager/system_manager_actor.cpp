#include "system_manager_actor.hpp"
#include <unistd.h>
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/messages/core_messages.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "infra/platform/linux/signal_handler.hpp"


SystemManagerActor::SystemManagerActor(std::string name, uint64_t id, ISys* sys, int pollIntervalMs) 
    : Actor(std::move(name), id), sys_(sys), pollIntervalMs_(pollIntervalMs){}

int SystemManagerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    SignalHandler::instance().init();
    signalPipeFd_ = SignalHandler::instance().fd();
    auto* eventLoop = runtime()->eventLoop();
    if(signalPipeFd_ >= 0 && eventLoop){
        IActorRuntime* ctx = runtime();
        eventLoop->subscribe(signalPipeFd_, [ctx, this](){
            int sig;
            ssize_t n = ::read(signalPipeFd_, &sig, sizeof(sig));
            if(n == sizeof(sig)){
                ctx->enqueue(Message::make(SignalNotify{sig}));
            }
        });
    }
    startTime_ = Time::now();
    startTimer(SysDataTick{}, pollIntervalMs_, true);
    //
    state_ = Opened;
    V2_LOG_INFO("System Actor opened");
    return Ok;
}

int SystemManagerActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    auto* eventLoop = runtime() ? runtime()->eventLoop() : nullptr;
    if(eventLoop && signalPipeFd_ >= 0){
        eventLoop->unsubscribe(signalPipeFd_);
    }
    //
    state_ = Closed;
    V2_LOG_INFO("System Actor closed");
    return Ok;
}

void SystemManagerActor::handle(const Message& msg){
    if(state_ < Opened) return;
    switch(msg.id()){
        case MessageId::SignalNotify:
            SignalHandler::instance().dispatch(msg.as<SignalNotify>().signum);
            break;
        case MessageId::SysDataTick:
            if(!subscribers_.empty()) pumpIfNeeded(); // 수요 없으면 수집 자체 중단
            break;
        case MessageId::SysDataSubscribe:{
            subscribers_.insert(msg.as<SysDataSubscribe>().subscriber);
            pumpIfNeeded(); // 구독 즉시 1회 발행 (retained-latest)
            break;
        }
        case MessageId::SysDataUnsubscribe:{
            subscribers_.erase(msg.as<SysDataUnsubscribe>().subscriber);
            if(subscribers_.empty()) latestSysRes_ = {};
            break;
        }
        default:
            break;
    }
}

void SystemManagerActor::onSignal(int signum, Callback cb){
    SignalHandler::instance().install(signum, std::move(cb));
}

void SystemManagerActor::pumpIfNeeded(){
    if(!sys_) return;
    if(sys_->collect(latestSysRes_) != Ok){
        V2_LOG_ERROR("System Manager Actor: collect failed");
        return;
    }
    latestSysRes_.uptimeMs = static_cast<uint64_t>(Time::toMs(Time::now() - startTime_));
    for(const auto& sub : subscribers_){
        sendMsg(sub, SysDataUpdate{latestSysRes_});
    }
}
