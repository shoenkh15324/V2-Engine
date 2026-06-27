#include "core/common/os/signal_handler.hpp"
#include "core/common/util/platform_config.h"

#if V2_PLATFORM_LINUX || V2_PLATFORM_MACOS
#include <csignal>

SignalHandler* SignalHandler::sInstance_ = nullptr;

SignalHandler& SignalHandler::instance(){
    static SignalHandler inst;
    sInstance_ = &inst;
    return inst;
}

void SignalHandler::listen(int signum, Callback cb){
    handlers_[signum].push_back(std::move(cb));
    std::signal(signum, onSignal);
}

void SignalHandler::ignore(int signum){
    handlers_.erase(signum);
    std::signal(signum, SIG_DFL);
}

void SignalHandler::onSignal(int sig){
    if(!sInstance_) return;
    auto it = sInstance_->handlers_.find(sig);
    if(it != sInstance_->handlers_.end()){
        for(auto& cb : it->second){
            cb(sig);
        }
    }
}

#elif V2_PLATFORM_WINDOWS

// 

#endif
