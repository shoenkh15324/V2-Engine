#include "system_actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/common/os/signal_handler.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include <unistd.h>

SystemActor::SystemActor(const std::string& name, uint64_t id) : Actor(std::move(name), id){}

int SystemActor::open(){
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
                ctx->enqueue(SignalNotify{sig});
            }
        });
    }
    //
    state_ = Opened;
    V2_LOG_INFO("System Actor opened");
    return Ok;
}

int SystemActor::close(){
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

void SystemActor::handle(const Message& msg){
    if(state_ < Opened) return;
    std::visit(overloaded{
        [this](const SignalNotify& msg){
            SignalHandler::instance().dispatch(msg.signum);
        },
        [](const auto&){}
    }, msg);
}

void SystemActor::onSignal(int signum, Callback cb){
    SignalHandler::instance().install(signum, std::move(cb));
}
