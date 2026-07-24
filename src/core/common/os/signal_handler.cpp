#include "core/common/os/signal_handler.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/util/return.hpp"

#if V2_PLATFORM_LINUX || V2_PLATFORM_MACOS
#include <csignal>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>

SignalHandler* SignalHandler::sInstance_ = nullptr;

SignalHandler& SignalHandler::instance(){
    static SignalHandler inst;
    sInstance_ = &inst;
    return inst;
}

SignalHandler::~SignalHandler(){
    if(pipeFd_[0] >= 0) ::close(pipeFd_[0]);
    if(pipeFd_[1] >= 0) ::close(pipeFd_[1]);
}

int SignalHandler::init(){
    if(pipeFd_[0] >= 0) return Fail;
    return ::pipe2(pipeFd_, O_NONBLOCK | O_CLOEXEC);
}

int SignalHandler::fd() const {
    return pipeFd_[0];
}

void SignalHandler::onSignal(int sig){
    if(!sInstance_ || sInstance_->pipeFd_[1] < 0) return;
    int saved = errno;
    ssize_t n = ::write(sInstance_->pipeFd_[1], &sig, sizeof(sig));
    errno = saved;
}

bool SignalHandler::install(int signum, Callback cb){
    if(signum < 1 || signum > 64) return false;
    if(signum == SIGKILL || signum == SIGSTOP) return false;
    callbacks_[signum].push_back(std::move(cb));
    std::signal(signum, onSignal);
    return true;
}

bool SignalHandler::uninstall(int signum){
    if(signum < 1 || signum > 64) return false;
    callbacks_[signum].clear();
    std::signal(signum, SIG_DFL);
    return true;
}

void SignalHandler::dispatch(int signum){
    if(signum < 1 || signum > 64) return;
    for(auto& cb: callbacks_[signum]){
        cb(signum);
    }
}

#elif V2_PLATFORM_WINDOWS

// 

#endif
